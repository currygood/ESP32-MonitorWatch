#include "max30102.h"
#include "i2c_driver.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_task_wdt.h"
#include "esp_timer.h"


static const char *TAG = "MAX30102";
static i2c_master_dev_handle_t max30102_dev = NULL;
static volatile bool max30102_int_flag = false;
static TaskHandle_t notify_task = NULL;

// --- 采集缓冲和结果变量 ---
static uint32_t aun_ir_buffer[IR_BUF_LEN];
static uint32_t aun_red_buffer[RED_BUF_LEN];
static int32_t n_spo2;
static int8_t ch_spo2_valid;
static int32_t n_heart_rate;
static int8_t ch_hr_valid;

// --- 心率预警相关变量 ---
static uint32_t Heart_Rate_Baseline = 70;                    // 基准心率（默认70bpm）
static uint32_t Heart_Rate_Warning_Threshold = 90;           // 预警阈值（默认比基准高20）
static uint32_t Heart_Rate_Baseline_History[HEART_RATE_BASELINE_SAMPLES]; // 历史心率记录
static uint8_t Heart_Rate_Baseline_Index = 0;               // 历史记录索引
static uint8_t Heart_Rate_Stable_Count = 0;                 // 心率稳定计数器
static bool Heart_Rate_Warning_Active = false;              // 预警状态标志
static bool Heart_Rate_Baseline_Initialized = false;        // 基准心率是否已初始化

// --- I2C初始化 ---
void Max30102_Init(i2c_master_bus_handle_t bus_handle) {
    ESP_ERROR_CHECK(I2c_Add_Device(bus_handle, MAX30102_ADDR, I2C_FREQ, &max30102_dev));
    
    vTaskDelay(pdMS_TO_TICKS(100));
    Max30102_Write_Reg(REG_MODE_CONFIG, 0x40); // 软件复位
    vTaskDelay(pdMS_TO_TICKS(10));
    Max30102_Write_Reg(REG_INTR_ENABLE_1, 0xC0);
    Max30102_Write_Reg(REG_INTR_ENABLE_2, 0x00);
    Max30102_Write_Reg(REG_FIFO_WR_PTR, 0x00);
    Max30102_Write_Reg(REG_OVF_COUNTER, 0x00);
    Max30102_Write_Reg(REG_FIFO_RD_PTR, 0x00);
    Max30102_Write_Reg(REG_FIFO_CONFIG, 0x0F);
    Max30102_Write_Reg(REG_MODE_CONFIG, 0x03);
    Max30102_Write_Reg(REG_SPO2_CONFIG, 0x27);
    Max30102_Write_Reg(REG_LED1_PA, 0x24);
    Max30102_Write_Reg(REG_LED2_PA, 0x24);
    Max30102_Write_Reg(REG_PILOT_PA, 0x7f);
}

// --- MAX30102的I2C读写 ---
esp_err_t Max30102_Write_Reg(uint8_t reg, uint8_t data) {
    return I2c_Write_Reg(max30102_dev, reg, data);
}

esp_err_t Max30102_Read_Reg(uint8_t reg, uint8_t *data) {
    return I2c_Read_Reg(max30102_dev, reg, data);
}

// --- 心率预警功能实现 ---

// 初始化心率预警系统
void Max30102_Heart_Rate_Warning_Init(void)
{
    // 初始化历史记录数组
    for (int i = 0; i < HEART_RATE_BASELINE_SAMPLES; i++) {
        Heart_Rate_Baseline_History[i] = Heart_Rate_Baseline;
    }
    Heart_Rate_Baseline_Index = 0;
    Heart_Rate_Stable_Count = 0;
    Heart_Rate_Warning_Active = false;
    Heart_Rate_Baseline_Initialized = false;
    
    ESP_LOGI(TAG, "心率预警系统初始化完成");
}

// 更新基准心率
void Max30102_Update_Heart_Rate_Baseline(uint32_t current_hr)
{
    // 检查心率是否在有效范围内
    if (current_hr < HEART_RATE_MIN_VALID || current_hr > HEART_RATE_MAX_VALID) {
        return;
    }
    
    // 记录当前心率到历史数组
    Heart_Rate_Baseline_History[Heart_Rate_Baseline_Index] = current_hr;
    Heart_Rate_Baseline_Index = (Heart_Rate_Baseline_Index + 1) % HEART_RATE_BASELINE_SAMPLES;
    
    // 计算平均心率作为基准
    uint32_t sum = 0;
    uint8_t valid_count = 0;
    
    for (int i = 0; i < HEART_RATE_BASELINE_SAMPLES; i++) {
        if (Heart_Rate_Baseline_History[i] >= HEART_RATE_MIN_VALID && 
            Heart_Rate_Baseline_History[i] <= HEART_RATE_MAX_VALID) {
            sum += Heart_Rate_Baseline_History[i];
            valid_count++;
        }
    }
    
    if (valid_count > 0) {
        uint32_t new_baseline = sum / valid_count;
        
        // 检查心率是否稳定（变化在±5bpm内）
        if (abs((int32_t)new_baseline - (int32_t)Heart_Rate_Baseline) <= 5) {
            Heart_Rate_Stable_Count++;
        } else {
            Heart_Rate_Stable_Count = 0;
        }
        
        // 只有当心率稳定一定次数后才更新基准
        if (Heart_Rate_Stable_Count >= HEART_RATE_STABLE_COUNT) {
            Heart_Rate_Baseline = new_baseline;
            Heart_Rate_Warning_Threshold = Heart_Rate_Baseline + HEART_RATE_WARNING_THRESHOLD_LOW;
            Heart_Rate_Baseline_Initialized = true;
            
            ESP_LOGI(TAG, "基准心率已更新: %lu bpm, 预警阈值: %lu bpm", 
                     Heart_Rate_Baseline, Heart_Rate_Warning_Threshold);
        }
    }
}

// 检查心率是否过快
bool Max30102_Check_Heart_Rate_Warning(uint32_t current_hr)
{
    // 检查心率是否在有效范围内
    if (current_hr < HEART_RATE_MIN_VALID || current_hr > HEART_RATE_MAX_VALID) {
        Heart_Rate_Warning_Active = false;
        return false;
    }
    
    // 如果基准心率尚未初始化，使用默认阈值
    uint32_t warning_threshold = Heart_Rate_Baseline_Initialized ? 
                                Heart_Rate_Warning_Threshold : 
                                (Heart_Rate_Baseline + HEART_RATE_WARNING_THRESHOLD_LOW);
    
    // 检查是否超过预警阈值
    if (current_hr >= warning_threshold) {
        if (!Heart_Rate_Warning_Active) {
            Heart_Rate_Warning_Active = true;
            ESP_LOGW(TAG, "⚠️ 心率过快预警! 当前心率: %lu bpm, 基准心率: %lu bpm", 
                     current_hr, Heart_Rate_Baseline);
        }
        return true;
    } else {
        if (Heart_Rate_Warning_Active) {
            Heart_Rate_Warning_Active = false;
            ESP_LOGI(TAG, "心率恢复正常: %lu bpm", current_hr);
        }
        return false;
    }
}

// 获取基准心率
uint32_t Max30102_Get_Heart_Rate_Baseline(void)
{
    return Heart_Rate_Baseline;
}

// 获取预警阈值
uint32_t Max30102_Get_Heart_Rate_Warning_Threshold(void)
{
    return Heart_Rate_Warning_Threshold;
}

// 检查预警状态
bool Max30102_Is_Heart_Rate_Warning_Active(void)
{
    return Heart_Rate_Warning_Active;
}

// 重置预警系统
void Max30102_Reset_Heart_Rate_Warning(void)
{
    Heart_Rate_Baseline = 70;
    Heart_Rate_Warning_Threshold = 90;
    Heart_Rate_Stable_Count = 0;
    Heart_Rate_Warning_Active = false;
    Heart_Rate_Baseline_Initialized = false;
    
    for (int i = 0; i < HEART_RATE_BASELINE_SAMPLES; i++) {
        Heart_Rate_Baseline_History[i] = Heart_Rate_Baseline;
    }
    
    ESP_LOGI(TAG, "心率预警系统已重置");
}

esp_err_t Max30102_Read_Fifo(uint8_t *buffer, uint8_t count) {
    return I2c_Read_Bytes(max30102_dev, REG_FIFO_DATA, buffer, count);
}

uint8_t Max30102_Can_Read(void) {
    return max30102_int_flag ? 1 : 0;
}

uint32_t Max301020_Get_Heart_Rate(void) {
	return n_heart_rate;
}

uint32_t Max30102_Get_Spo2(void) {
    return n_spo2;
}

// --- GPIO/ISR ---
static void IRAM_ATTR max30102_isr_handler(void *arg) {
    max30102_int_flag = true;
    if (notify_task) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(notify_task, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    }
}

// 标志清除函数
void Max30102_Clear_Flag(void) {
    max30102_int_flag = false;
}

void Max30102_Gpio_Isr_Init(TaskHandle_t task_handle) {
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
void Max30102_Algorithm_Calculate(uint32_t *ir_buffer, int32_t buffer_len, uint32_t *red_buffer,
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

// --- VOFA+ 原生格式波形数据输出 ---
// 输出格式符合VOFA+的多通道数据要求: ch0,ch1,ch2,ch3\n
void Max30102_Send_Waveform_Data(void) {
    // 输出最后100个样本，每行一个样本数据
    // 格式: RED值,IR值,心率,血氧
    for (int i = 400; i < 500; i++) {
        printf("%lu,%lu,%ld,%ld\n",
               aun_red_buffer[i],       // ch0: 红光原始值(0-262143)
               aun_ir_buffer[i],        // ch1: 红外光原始值(0-262143)
               (long)n_heart_rate,      // ch2: 心率(bpm)
               (long)n_spo2);           // ch3: 血氧饱和度(%)
    }
}

// --- JSON格式输出 ---
// 输出格式: {"id":随机ID,"params":{"heart_rate":{"value":心率,"time":时间戳},"oxygen_saturation":{"value":血氧,"time":时间戳},"seizure_risk_level":{"value":风险等级,"time":时间戳},"abnormal_motion_detected":{"value":异常运动检测,"time":时间}}}
void Max30102_Send_JSON_Data(void)
{
    // 获取当前时间戳（秒）
    uint32_t timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
    
    // 计算癫痫风险等级（基于心率和血氧数据）
    uint32_t seizure_risk_level = 0;
    if (n_heart_rate > 100 || n_spo2 < 95) {
        seizure_risk_level = 60;  // 中等风险
    }
    if (n_heart_rate > 120 || n_spo2 < 90) {
        seizure_risk_level = 80;  // 高风险
    }
    if (Max30102_Is_Heart_Rate_Warning_Active()) {
        seizure_risk_level = 90;  // 预警状态高风险
    }
    
    // 异常运动检测（基于心率变化）
    bool abnormal_motion_detected = (n_heart_rate > 120) || (n_heart_rate < 40);
    
    ESP_LOGI("", "{\"id\":%d,\"params\":{"
             "\"heart_rate\":{\"value\":%ld,\"time\":%lu},"
             "\"oxygen_saturation\":{\"value\":%ld,\"time\":%lu},"
             "\"seizure_risk_level\":{\"value\":%lu,\"time\":%lu},"
             "\"abnormal_motion_detected\":{\"value\":%d,\"time\":%lu}"
             "}}\n", 
             rand() % 10000,  // 随机ID
             (long)n_heart_rate, timestamp,
             (long)n_spo2, timestamp,
             seizure_risk_level, timestamp,
             abnormal_motion_detected ? 1 : 0, timestamp);
}

// --- 监测任务 ---
void Task_Max30102_Monitor(void *pvParameters) {
    esp_log_level_set("gpio", ESP_LOG_ERROR);
    esp_log_level_set("i2c", ESP_LOG_ERROR);
    
    uint8_t temp[6];
    int32_t n_ir_buffer_length = IR_BUF_LEN;
    int32_t un_min = UINT32_MAX, un_max = 0;
    uint8_t fifo_wp, fifo_rp;
    int samples_read;
    int i;
    
    ESP_LOGI(TAG, "Monitor task started");
    
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "无法获取I2C总线句柄");
        vTaskDelete(NULL);
        return;
    }
    
    // 1. 初始化MAX30102设备
    Max30102_Init(i2c_bus);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 测试 I2C 通信
    uint8_t part_id = 0;
    esp_err_t ret = Max30102_Read_Reg(0xFF, &part_id);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Part ID: 0x%02X", part_id);
    } else {
        ESP_LOGE(TAG, "I2C read failed: %d", ret);
        vTaskDelete(NULL);
        return;
    }
    
    // 检查中断状态和配置
    uint8_t intr_status, intr_en;
    Max30102_Read_Reg(0x00, &intr_status);  // INTR_STATUS_1
    Max30102_Read_Reg(0x02, &intr_en);      // INTR_ENABLE_1
    Max30102_Read_Reg(0x04, &fifo_wp);      // FIFO_WR_PTR
    Max30102_Read_Reg(0x06, &fifo_rp);      // FIFO_RD_PTR
    ESP_LOGI(TAG, "INTR_STATUS: 0x%02X, INTR_EN: 0x%02X, FIFO_WP: %d, FIFO_RP: %d", 
        intr_status, intr_en, fifo_wp, fifo_rp);
    
    Max30102_Gpio_Isr_Init(xTaskGetCurrentTaskHandle());
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 初始化心率预警系统
    Max30102_Heart_Rate_Warning_Init();
    
    // 再读一次 FIFO 指针，看是否有数据产生
    Max30102_Read_Reg(0x04, &fifo_wp);
    Max30102_Read_Reg(0x06, &fifo_rp);
    ESP_LOGI(TAG, "After ISR init - FIFO_WP: %d, FIFO_RP: %d", fifo_wp, fifo_rp);

    // 2. 读取前500个样本（改为轮询模式）
    samples_read = 0;
    ESP_LOGI(TAG, "Starting sample collection...");
    
    while (samples_read < n_ir_buffer_length) {
        Max30102_Read_Reg(0x04, &fifo_wp);  // FIFO_WR_PTR
        Max30102_Read_Reg(0x06, &fifo_rp);  // FIFO_RD_PTR
        
        if (fifo_wp != fifo_rp) {
            esp_err_t ret = Max30102_Read_Fifo(temp, 6);
            if (ret != ESP_OK) {
                continue;
            }
            
            aun_red_buffer[samples_read] = ((temp[0] & 0x03) << 16) | (temp[1] << 8) | temp[2];
            aun_ir_buffer[samples_read] = ((temp[3] & 0x03) << 16) | (temp[4] << 8) | temp[5];
            if (un_min > aun_red_buffer[samples_read]) un_min = aun_red_buffer[samples_read];
            if (un_max < aun_red_buffer[samples_read]) un_max = aun_red_buffer[samples_read];
            
            samples_read++;
            
            // 每读 100 个样本打印进度
            if (samples_read % 100 == 0) {
                ESP_LOGI(TAG, "Collected %d samples", samples_read);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));  // 没有数据时，稍微延迟后再读
        }
    }
    
    ESP_LOGI(TAG, "Sample collection complete!");

    // 3. 算法计算
    Max30102_Algorithm_Calculate(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_spo2, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid);
    
    ESP_LOGI(TAG, "Ready for heartbeat monitoring...");

    // 4. 循环采集和计算
    while (1) {
        // 移位缓存
        for (i = 100; i < 500; i++) {
            aun_red_buffer[i - 100] = aun_red_buffer[i];
            aun_ir_buffer[i - 100] = aun_ir_buffer[i];
            if (un_min > aun_red_buffer[i]) un_min = aun_red_buffer[i];
            if (un_max < aun_red_buffer[i]) un_max = aun_red_buffer[i];
        }
        
        // 采集新样本（轮询模式，定期喂狗）
        samples_read = 400;
        while (samples_read < 500) {
            vTaskDelay(pdMS_TO_TICKS(2));  // 让出CPU
            
            Max30102_Read_Reg(0x04, &fifo_wp);
            Max30102_Read_Reg(0x06, &fifo_rp);
            
            if (fifo_wp != fifo_rp) {
                if (Max30102_Read_Fifo(temp, 6) == ESP_OK) {
                    aun_red_buffer[samples_read] = ((temp[0] & 0x03) << 16) | (temp[1] << 8) | temp[2];
                    aun_ir_buffer[samples_read] = ((temp[3] & 0x03) << 16) | (temp[4] << 8) | temp[5];
                    samples_read++;
                }
            }
        }
        
        // 调用算法计算
        Max30102_Algorithm_Calculate(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_spo2, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid);
        
        // 算法计算后喂狗
        // esp_task_wdt_reset();
        
        // 心率预警检测
        if (ch_hr_valid == 1) {
            // 更新基准心率
            Max30102_Update_Heart_Rate_Baseline((uint32_t)n_heart_rate);
            
            // 检查心率过快预警
            bool warning_active = Max30102_Check_Heart_Rate_Warning((uint32_t)n_heart_rate);
            
            // 如果心率过快，记录预警信息
            if (warning_active) {
                ESP_LOGW(TAG, "癫痫早期症状检测: 心率过快! 当前: %ld bpm, 基准: %lu bpm, 阈值: %lu bpm", 
                         (long)n_heart_rate, Max30102_Get_Heart_Rate_Baseline(), 
                         Max30102_Get_Heart_Rate_Warning_Threshold());
            }
            
            // 通过消息队列发送心率血氧数据
            if (ch_spo2_valid == 1) {
                Message_Queue_Send_Heart_Rate((uint32_t)n_heart_rate, (uint32_t)n_spo2, 
                                             Max30102_Get_Heart_Rate_Baseline(), warning_active);
            } else {
                // 如果血氧数据无效，只发送心率数据
                Message_Queue_Send_Heart_Rate((uint32_t)n_heart_rate, 0, 
                                             Max30102_Get_Heart_Rate_Baseline(), warning_active);
            }
            
            // 如果检测到预警，发送预警消息
            if (warning_active) {
                Message_Queue_Send_Alert(false, false, true);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 任务永远不会到达这里，但为了完整性
    vTaskDelete(NULL);
}