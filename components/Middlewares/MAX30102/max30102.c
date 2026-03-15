#include "max30102.h"
#include "i2c_driver.h"
#include <stdlib.h>
#include <math.h>

static const char *TAG = "MAX30102";
static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t max30102_dev = NULL;
static volatile bool max30102_int_flag = false;
static TaskHandle_t notify_task = NULL;

// --- I2C初始化 ---
void max30102_init(void) {
    ESP_ERROR_CHECK(i2c_init_bus(MAX30102_I2C_PORT, MAX30102_I2C_SDA_GPIO, MAX30102_I2C_SCL_GPIO, MAX30102_I2C_FREQ, &i2c_bus));
    ESP_ERROR_CHECK(i2c_add_device(i2c_bus, MAX30102_ADDR, MAX30102_I2C_FREQ, &max30102_dev));

    // 传感器初始化流程（参考原始代码）
    vTaskDelay(pdMS_TO_TICKS(100));
    max30102_write_reg(REG_MODE_CONFIG, 0x40); // 软件复位
    vTaskDelay(pdMS_TO_TICKS(10));
    max30102_write_reg(REG_INTR_ENABLE_1, 0xC0);
    max30102_write_reg(REG_INTR_ENABLE_2, 0x00);
    max30102_write_reg(REG_FIFO_WR_PTR, 0x00);
    max30102_write_reg(REG_OVF_COUNTER, 0x00);
    max30102_write_reg(REG_FIFO_RD_PTR, 0x00);
    max30102_write_reg(REG_FIFO_CONFIG, 0x0F);
    max30102_write_reg(REG_MODE_CONFIG, 0x03);
    max30102_write_reg(REG_SPO2_CONFIG, 0x27);
    max30102_write_reg(REG_LED1_PA, 0x24);
    max30102_write_reg(REG_LED2_PA, 0x24);
    max30102_write_reg(REG_PILOT_PA, 0x7f);
}

// --- I2C读写 ---
esp_err_t max30102_write_reg(uint8_t reg, uint8_t data) {
    return i2c_write_reg(max30102_dev, reg, data);
}

esp_err_t max30102_read_reg(uint8_t reg, uint8_t *data) {
    return i2c_read_reg(max30102_dev, reg, data);
}

esp_err_t max30102_read_fifo(uint8_t *buffer, uint8_t count) {
    return i2c_read_bytes(max30102_dev, REG_FIFO_DATA, buffer, count);
}

uint8_t max30102_can_read(void) {
    return max30102_int_flag ? 1 : 0;
}

// --- GPIO/ISR ---
static void IRAM_ATTR max30102_isr_handler(void *arg) {
    if (notify_task) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(notify_task, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    }
}

// 标志清除函数
static inline void max30102_clear_flag(void) {
    max30102_int_flag = false;
}

void max30102_gpio_isr_init(TaskHandle_t task_handle) {
    notify_task = task_handle;
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << MAX30102_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(MAX30102_INT_GPIO, max30102_isr_handler, NULL));
}

// --- 汉明窗系数 ---
static const uint16_t auw_hamm[5] = { 41, 276, 512, 276, 41 };

// --- SpO2 查找表 ---
static const uint8_t spo2_table[184] = { 
    95, 95, 95, 96, 96, 96, 97, 97, 97, 97, 97, 98, 98, 98, 98, 98, 99, 99, 99, 99, 
    99, 99, 99, 99, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 
    100, 100, 100, 100, 99, 99, 99, 99, 99, 99, 99, 99, 98, 98, 98, 98, 98, 98, 97, 97, 
    97, 97, 96, 96, 96, 96, 95, 95, 95, 94, 94, 94, 93, 93, 93, 92, 92, 92, 91, 91, 
    90, 90, 89, 89, 89, 88, 88, 87, 87, 86, 86, 85, 85, 84, 84, 83, 82, 82, 81, 81, 
    80, 80, 79, 78, 78, 77, 76, 76, 75, 74, 74, 73, 72, 72, 71, 70, 69, 69, 68, 67, 
    66, 66, 65, 64, 63, 62, 62, 61, 60, 59, 58, 57, 56, 56, 55, 54, 53, 52, 51, 50, 
    49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 31, 30, 29, 
    28, 27, 26, 25, 23, 22, 21, 20, 19, 17, 16, 15, 14, 12, 11, 10, 9, 7, 6, 5, 
    3, 2, 1 
};

// --- 查找峰值 ---
static void find_peaks(int32_t *locs, int32_t *npks, int32_t *x, int32_t size, 
                       int32_t min_h, int32_t min_d, int32_t max_n) {
    int32_t i = 1, width, count = 0;
    int32_t temp_locs[15];
    memset(temp_locs, 0, sizeof(temp_locs));

    while (i < size - 1 && count < 15) {
        if (x[i] > min_h && x[i] > x[i - 1] && x[i] > x[i + 1]) {
            width = 1;
            while (i + width < size && x[i + width] == x[i]) width++;
            if (i + width < size - 1) {
                if (width == 0 || (x[i + width] < x[i - width])) {
                    temp_locs[count] = i + width / 2;
                    count++;
                }
            }
            i += width;
        }
        i++;
    }

    // 按值大小降序排列
    for (i = 1; i < count; i++) {
        for (int32_t j = i; j > 0 && x[temp_locs[j]] > x[temp_locs[j - 1]]; j--) {
            int32_t temp = temp_locs[j];
            temp_locs[j] = temp_locs[j - 1];
            temp_locs[j - 1] = temp;
        }
    }

    // 移除距离太近的峰值，保留最大的
    int32_t final_count = 0;
    memset(locs, 0, sizeof(int32_t) * 15);
    for (i = 0; i < count && final_count < max_n; i++) {
        bool too_close = false;
        for (int32_t j = 0; j < final_count; j++) {
            if (llabs((long)temp_locs[i] - (long)locs[j]) < min_d) {
                too_close = true;
                break;
            }
        }
        if (!too_close) {
            locs[final_count++] = temp_locs[i];
        }
    }
    *npks = final_count;

    // 升序排列
    for (i = 1; i < *npks; i++) {
        for (int32_t j = i; j > 0 && locs[j] < locs[j - 1]; j--) {
            int32_t temp = locs[j];
            locs[j] = locs[j - 1];
            locs[j - 1] = temp;
        }
    }
}

// --- 升序排列 ---
static void sort_ascend(int32_t *x, int32_t size) {
    if (size <= 1) return;
    int32_t i, j, temp;
    for (i = 1; i < size; i++) {
        for (j = i; j > 0 && x[j] < x[j - 1]; j--) {
            temp = x[j];
            x[j] = x[j - 1];
            x[j - 1] = temp;
        }
    }
}

// --- 算法实现：计算心率和血氧 ---
void max30102_algorithm_calculate(uint32_t *ir_buffer, int32_t buffer_len, uint32_t *red_buffer,
                                  int32_t *spo2, int8_t *spo2_valid,
                                  int32_t *heart_rate, int8_t *hr_valid) {
    static int32_t an_dx[MAX30102_BUFFER_SIZE];
    static int32_t an_x[MAX30102_BUFFER_SIZE];
    static int32_t an_y[MAX30102_BUFFER_SIZE];

    uint32_t un_ir_mean;
    int32_t k, i, n_exact_ir_valley_locs_count, n_middle_idx;
    int32_t n_th1, n_npks;
    int32_t an_exact_ir_valley_locs[15], an_dx_peak_locs[15];
    int32_t n_peak_interval_sum;
    int32_t n_y_ac, n_x_ac, n_y_dc_max, n_x_dc_max;
    int32_t n_y_dc_max_idx, n_x_dc_max_idx;
    int32_t an_ratio[5], n_ratio_average, n_i_ratio_count;
    int32_t n_nume, n_denom;

    memset(an_exact_ir_valley_locs, 0, sizeof(an_exact_ir_valley_locs));
    memset(an_dx_peak_locs, 0, sizeof(an_dx_peak_locs));
    memset(an_ratio, 0, sizeof(an_ratio));

    // 1. 去除IR信号的直流分量
    un_ir_mean = 0;
    for (k = 0; k < buffer_len; k++) un_ir_mean += ir_buffer[k];
    un_ir_mean = un_ir_mean / buffer_len;
    for (k = 0; k < buffer_len; k++) an_x[k] = ir_buffer[k] - un_ir_mean;

    // 2. 4点滑动平均平滑处理
    for (k = 0; k < buffer_len - MAX30102_MA4_SIZE; k++) {
        an_x[k] = (an_x[k] + an_x[k + 1] + an_x[k + 2] + an_x[k + 3]) / 4;
    }

    // 3. 计算微分信号并进行汉明窗处理
    for (k = 0; k < buffer_len - MAX30102_MA4_SIZE - 1; k++)
        an_dx[k] = (an_x[k + 1] - an_x[k]);

    for (i = 0; i < buffer_len - MAX30102_HAMMING_SIZE - MAX30102_MA4_SIZE - 2; i++) {
        an_dx[i] = (an_dx[i] * auw_hamm[0] + an_dx[i + 1] * auw_hamm[1] + 
                    an_dx[i + 2] * auw_hamm[2] + an_dx[i + 3] * auw_hamm[3] + 
                    an_dx[i + 4] * auw_hamm[4]) / 1024;
    }

    // 4. 寻找峰值计算心率
    n_th1 = 0;
    for (k = 0; k < buffer_len - MAX30102_HAMMING_SIZE; k++) {
        n_th1 += an_dx[k];
    }
    n_th1 = n_th1 / (buffer_len - MAX30102_HAMMING_SIZE);

    find_peaks(an_dx_peak_locs, &n_npks, an_dx, buffer_len - MAX30102_HAMMING_SIZE, n_th1, 8, 5);

    if (n_npks >= 2) {
        n_peak_interval_sum = 0;
        for (k = 1; k < n_npks; k++)
            n_peak_interval_sum += (an_dx_peak_locs[k] - an_dx_peak_locs[k - 1]);
        n_peak_interval_sum = n_peak_interval_sum / (n_npks - 1);
        if (n_peak_interval_sum > 0) {
            *heart_rate = (int32_t)(6000 / n_peak_interval_sum);
        }
        *hr_valid = 1;
    } else {
        *hr_valid = 0;
    }

    // 5. 准备血氧计算所需的AC/DC分量
    for (k = 0; k < buffer_len; k++) {
        an_y[k] = red_buffer[k] - un_ir_mean;
    }

    n_exact_ir_valley_locs_count = 0;
    for (k = 0; k < n_npks && n_exact_ir_valley_locs_count < 15; k++) {
        if (an_dx_peak_locs[k] > MAX30102_HAMMING_SIZE && 
            an_dx_peak_locs[k] < buffer_len - MAX30102_HAMMING_SIZE) {
            for (i = an_dx_peak_locs[k] - MAX30102_MA4_SIZE; i < an_dx_peak_locs[k] + MAX30102_MA4_SIZE; i++) {
                if (i >= 0 && i < buffer_len - MAX30102_MA4_SIZE &&
                    an_x[i - MAX30102_MA4_SIZE] < an_x[i]) {
                    an_exact_ir_valley_locs[n_exact_ir_valley_locs_count] = i;
                }
            }
            n_exact_ir_valley_locs_count++;
        }
    }

    if (n_exact_ir_valley_locs_count < 2) {
        *spo2_valid = 0;
        return;
    }

    // 6. 计算R比例并查表得出血氧
    n_i_ratio_count = 0;
    for (k = 0; k < n_exact_ir_valley_locs_count - 1 && n_i_ratio_count < 5; k++) {
        n_y_dc_max = -16777216;
        n_x_dc_max = -16777216;
        n_y_dc_max_idx = 0;
        n_x_dc_max_idx = 0;
        
        if (an_exact_ir_valley_locs[k + 1] - an_exact_ir_valley_locs[k] > 10) {
            for (i = an_exact_ir_valley_locs[k]; i < an_exact_ir_valley_locs[k + 1]; i++) {
                if (i >= 0 && i < buffer_len) {
                    if (an_x[i] > n_x_dc_max) {
                        n_x_dc_max = an_x[i];
                        n_x_dc_max_idx = i;
                    }
                    if (an_y[i] > n_y_dc_max) {
                        n_y_dc_max = an_y[i];
                        n_y_dc_max_idx = i;
                    }
                }
            }
            n_y_ac = (an_y[an_exact_ir_valley_locs[k + 1]] - an_y[an_exact_ir_valley_locs[k]]) * 
                     (n_x_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_x_ac = (an_x[an_exact_ir_valley_locs[k + 1]] - an_x[an_exact_ir_valley_locs[k]]) * 
                     (n_y_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_nume = n_y_ac * n_x_dc_max;
            n_denom = n_x_ac * n_y_dc_max;
            if (n_denom != 0 && n_nume != 0) {
                an_ratio[n_i_ratio_count] = (n_nume * 100) / n_denom;
                n_i_ratio_count++;
            }
        }
    }

    if (n_i_ratio_count > 0) {
        sort_ascend(an_ratio, n_i_ratio_count);
        n_middle_idx = n_i_ratio_count / 2;
        if (n_middle_idx > 1)
            n_ratio_average = (an_ratio[n_middle_idx - 1] + an_ratio[n_middle_idx]) / 2;
        else
            n_ratio_average = an_ratio[0];

        if (n_ratio_average > 2 && n_ratio_average < 184) {
            *spo2 = spo2_table[n_ratio_average];
            *spo2_valid = 1;
        } else {
            *spo2_valid = 0;
        }
    } else {
        *spo2_valid = 0;
    }
}
