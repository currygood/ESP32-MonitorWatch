#include "mpu6050.h"
#include "i2c_driver.h"
#include <stdlib.h>
#include <math.h>
#include "Buzzer.h"
#include "esp_timer.h"


static const char *TAG = "MPU6050";

static i2c_master_dev_handle_t mpu6050_dev = NULL;

// 全局数据就绪标志
bool Mpu6050_Data_Ready_Flag = false;

// 最新传感器数据缓存
static int16_t Mpu6050_Last_Accel_X = 0;
static int16_t Mpu6050_Last_Accel_Y = 0;
static int16_t Mpu6050_Last_Accel_Z = 0;
static int16_t Mpu6050_Last_Gyro_X = 0;
static int16_t Mpu6050_Last_Gyro_Y = 0;
static int16_t Mpu6050_Last_Gyro_Z = 0;
// 运动指数（|幅值-1g| 的 EMA），供上层 SignalFusion 融合使用
static float Mpu6050_Motion_Index = 0.0f;

// 采集缓冲
static int16_t ax_buffer[MPU6050_BUFFER_SIZE];
static int16_t ay_buffer[MPU6050_BUFFER_SIZE];
static int16_t az_buffer[MPU6050_BUFFER_SIZE];
static int buffer_idx = 0;

// ==================== software filtering (main CPU, ~50Hz) ====================
// Stage 1: single-sample spike suppressor. A jump is only replaced when the
//          previous step was calm, so real impact ramps keep their peak value.
// Stage 2: first-order IIR (EMA) low-pass for smooth exported values.
#define MPU_ACCEL_SPIKE_LIMIT   2048   // ~1.0 g @ 2048 LSB/g (+-16g)
#define MPU_GYRO_SPIKE_LIMIT    4000   // ~61 deg/s @ 65.5 LSB/(deg/s) (+-2000)
#define MPU_EMA_ALPHA           0.40f  // smoother = higher; 0.40 ~ fc 4.1Hz @ 50Hz

// detection thresholds (accel magnitude in g, 50Hz window ~2.2s)
#define MPU_ACCEL_LSB_PER_G     2048.0f  // +-16g FSR: 2048 LSB/g
#define MPU_FALL_PEAK_G         2.0f     // fall needs peak magnitude above this
#define MPU_FALL_ACTIVITY       0.12f    // fall also needs avg sample change > this
#define MPU_PEAK_MAG_G          1.8f     // samples above this count as strong peaks

static int16_t s_acc_prev2[3] = {0, 0, 0};
static int16_t s_acc_prev [3] = {0, 0, 0};
static int16_t s_gyr_prev2[3] = {0, 0, 0};
static int16_t s_gyr_prev [3] = {0, 0, 0};
static float   s_acc_sm[3] = {0.0f, 0.0f, 0.0f};
static float   s_gyr_sm[3] = {0.0f, 0.0f, 0.0f};

static void filter_axis(int16_t raw, int16_t limit,
                        int16_t *prev2, int16_t *prev, float *sm,
                        int16_t *clean, int16_t *smooth)
{
    int16_t out = raw;
    if (*prev != 0) {
        int32_t dv  = (int32_t)raw - (int32_t)*prev;
        int32_t dpv = (int32_t)*prev - (int32_t)*prev2;
        if ((dv > limit && dpv <= limit) || (dv < -limit && dpv >= -limit))
            out = *prev;   /* isolated single-sample spike */
    }
    *prev2 = *prev;
    *prev  = raw;
    *sm += MPU_EMA_ALPHA * ((float)out - *sm);
    *clean  = out;
    *smooth = (int16_t)(*sm + 0.5f);
}

static void Mpu6050_Software_Filter_Update(int16_t ax, int16_t ay, int16_t az,
                                           int16_t gx, int16_t gy, int16_t gz,
                                           int16_t *cax, int16_t *cay, int16_t *caz,
                                           int16_t *cgx, int16_t *cgy, int16_t *cgz,
                                           int16_t *sax, int16_t *say, int16_t *saz,
                                           int16_t *sgx, int16_t *sgy, int16_t *sgz)
{
    int16_t ra[3] = { ax, ay, az };
    int16_t rg[3] = { gx, gy, gz };
    int16_t ca[3], cg[3], sa[3], sg[3];
    for (int k = 0; k < 3; k++) {
        filter_axis(ra[k], MPU_ACCEL_SPIKE_LIMIT, &s_acc_prev2[k], &s_acc_prev[k], &s_acc_sm[k], &ca[k], &sa[k]);
        filter_axis(rg[k], MPU_GYRO_SPIKE_LIMIT,  &s_gyr_prev2[k], &s_gyr_prev[k], &s_gyr_sm[k], &cg[k], &sg[k]);
    }
    *cax = ca[0]; *cay = ca[1]; *caz = ca[2];
    *cgx = cg[0]; *cgy = cg[1]; *cgz = cg[2];
    *sax = sa[0]; *say = sa[1]; *saz = sa[2];
    *sgx = sg[0]; *sgy = sg[1]; *sgz = sg[2];
}

// 初始化
void Mpu6050_Init(i2c_master_bus_handle_t bus_handle) {
    // 添加 MPU6050 设备
    ESP_ERROR_CHECK(I2c_Add_Device(bus_handle, MPU6050_ADDR, MPU6050_I2C_FREQ, &mpu6050_dev));

    ESP_LOGI(TAG, "MPU6050 设备添加成功");

    vTaskDelay(pdMS_TO_TICKS(100));

    // 软件复位
    Mpu6050_Write_Reg(MPU6050_REG_PWR_MGMT_1, 0x80);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 唤醒，选择陀螺仪时钟
    Mpu6050_Write_Reg(MPU6050_REG_PWR_MGMT_1, 0x01);

    Mpu6050_Write_Reg(MPU6050_REG_SMPLRT_DIV, 19);
    // DLPF_CFG=4: ACCEL 21Hz/GYRO 20Hz，保留高频振动频段(2~30Hz)；采样率仍 50Hz
    Mpu6050_Write_Reg(MPU6050_REG_CONFIG, 0x04);
    Mpu6050_Write_Reg(MPU6050_REG_ACCEL_CONFIG, 0x18);
    Mpu6050_Write_Reg(MPU6050_REG_GYRO_CONFIG, 0x18);

    ESP_LOGI(TAG, "MPU6050 初始化完成");
}

// 读写函数
esp_err_t Mpu6050_Write_Reg(uint8_t reg, uint8_t data) {
    return I2c_Write_Reg(mpu6050_dev, reg, data);
}

esp_err_t Mpu6050_Read_Reg(uint8_t reg, uint8_t *data) {
    return I2c_Read_Reg(mpu6050_dev, reg, data);
}

esp_err_t Mpu6050_Read_Raw(int16_t *ax, int16_t *ay, int16_t *az,
                           int16_t *gx, int16_t *gy, int16_t *gz) {
    uint8_t raw[14];
    esp_err_t ret = I2c_Read_Bytes(mpu6050_dev, MPU6050_REG_ACCEL_XOUT_H, raw, 14);
    if (ret != ESP_OK) return ret;

    *ax = (int16_t)((raw[0] << 8) | raw[1]);
    *ay = (int16_t)((raw[2] << 8) | raw[3]);
    *az = (int16_t)((raw[4] << 8) | raw[5]);

    *gx = (int16_t)((raw[8]  << 8) | raw[9]);
    *gy = (int16_t)((raw[10] << 8) | raw[11]);
    *gz = (int16_t)((raw[12] << 8) | raw[13]);

    return ESP_OK;
}

// // 检测函数
// bool Mpu6050_Detect_Fall(int16_t *ax_buf, int16_t *ay_buf, int16_t *az_buf, int len) {
//     if (len < 10) return false;

//     float max_mag = 0;
//     float avg_mag = 0;
//     int fall_count = 0;

//     for (int i = 0; i < len; i++) {

//         float mag = sqrtf((float)ax_buf[i]*ax_buf[i] +
//                           (float)ay_buf[i]*ay_buf[i] +
//                           (float)az_buf[i]*az_buf[i]) / 2048.0f;   // ±16g

//         avg_mag += mag;

//         if (mag > max_mag)
//             max_mag = mag;

//         if (mag > 2.0f)
//             fall_count++;
//     }

//     avg_mag /= len;

//     if (max_mag > 2.2f && avg_mag > 1.05f) {
//         ESP_LOGW(TAG, "可能跌倒或剧烈抽搐! max=%.2f g avg=%.2f g", max_mag, avg_mag);
//         return true;
//     }

//     return false;
// }

// 检测函数：仅保留撞击/跌倒检测（抽搐判断已由端侧模型接管）
// 定义全局变量
static bool s_current_is_abnormal = false; 

bool Mpu6050_Detect_Fall(int16_t *ax_buf, int16_t *ay_buf, int16_t *az_buf, int len) {
    if (len < 10) return false;

    // --- 每次进入检测时，先初始化为 false ---
    // 这样如果这 4 秒没出事，s_current_is_abnormal 就会变回 false
    s_current_is_abnormal = false; 

    float max_mag = 0;
    float total_variation = 0;
    int high_peak_count = 0;
    float last_mag = 1.0f;

    for (int i = 0; i < len; i++) {
        float mag = sqrtf((float)ax_buf[i]*ax_buf[i] + (float)ay_buf[i]*ay_buf[i] + (float)az_buf[i]*az_buf[i]) / MPU_ACCEL_LSB_PER_G;
        if (mag > max_mag) max_mag = mag;
        total_variation += fabsf(mag - last_mag);
        if (mag > MPU_PEAK_MAG_G) high_peak_count++;
        last_mag = mag;
    }

    float activity_score = total_variation / len;

    // calibration log: read these values while wearing the watch, then set
    // the thresholds below just above the normal-activity values
    ESP_LOGI(TAG, "Detect: max=%.2fg act=%.2f peaks=%d",
             max_mag, activity_score, high_peak_count);

    // A. 跌倒检测
    if (max_mag > MPU_FALL_PEAK_G && activity_score > MPU_FALL_ACTIVITY) {   // raise to kill fall false alarms
        ESP_LOGW(TAG, "🔔 撞击报警!");
        s_current_is_abnormal = true; 
        return true;
    }

    // B. 抽搐检测已移除：抽搐/持续抖动判断统一交给 SignalFusion 端侧模型，
    //    此处不再报警，避免刷牙等正常活动触发误报
    return false;
}

// 获取当前的异常状态
bool Get_isFall(void) {
    return s_current_is_abnormal;
}

// --- 标志位管理函数 ---

// 判断是否可以读取数据
bool Mpu6050_Can_Read(void) {
    return Mpu6050_Data_Ready_Flag ? true : false;
}

// 清除数据就绪标志
void Mpu6050_Clear_Flag(void) {
    Mpu6050_Data_Ready_Flag = false;
}

// --- 数据获取函数 ---

// 获取加速度数据
void Mpu6050_Get_Accel_Data(int16_t *ax, int16_t *ay, int16_t *az) {
    if (ax != NULL) *ax = Mpu6050_Last_Accel_X;
    if (ay != NULL) *ay = Mpu6050_Last_Accel_Y;
    if (az != NULL) *az = Mpu6050_Last_Accel_Z;
}

// 获取陀螺仪数据
void Mpu6050_Get_Gyro_Data(int16_t *gx, int16_t *gy, int16_t *gz) {
    if (gx != NULL) *gx = Mpu6050_Last_Gyro_X;
    if (gy != NULL) *gy = Mpu6050_Last_Gyro_Y;
    if (gz != NULL) *gz = Mpu6050_Last_Gyro_Z;
}

float Mpu6050_Get_Motion_Index(void) {
    return Mpu6050_Motion_Index;
}

// 监测任务
void Task_Mpu6050_Monitor(void *pvParameters) {
    esp_log_level_set("gpio", ESP_LOG_ERROR);
    esp_log_level_set("i2c", ESP_LOG_ERROR);

	// Mpu6050_Init(i2c_bus);	// 注意：MPU6050初始化已在main.c中完成，这里不再重复初始化
	// vTaskDelay(pdMS_TO_TICKS(100));	// 等待初始化完成

    int16_t ax, ay, az, gx, gy, gz;
	static bool isBuzzerOn = false;

    ESP_LOGI(TAG, "Monitor task started");

	//初始化
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	Mpu6050_Init(i2c_bus);
    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t who_am_i = 0;
    esp_err_t ret = Mpu6050_Read_Reg(MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret == ESP_OK && who_am_i == MPU6050_ADDR) {
        ESP_LOGI(TAG, "WHO_AM_I = 0x%02X (OK)", who_am_i);
    } else {
        ESP_LOGE(TAG, "WHO_AM_I 读取失败或错误: 0x%02X, err=%d", who_am_i, ret);
        vTaskDelete(NULL);
     //   return;
   }

    ESP_LOGI(TAG, "开始采集数据 @ ~%d Hz ...", MPU6050_SAMPLES_PER_SEC);

    while (1) {
        ret = Mpu6050_Read_Raw(&ax, &ay, &az, &gx, &gy, &gz);
        if (ret == ESP_OK) {
            // 更新最新传感器数据
            int16_t cax, cay, caz, cgx, cgy, cgz;
            int16_t sax, say, saz, sgx, sgy, sgz;
            Mpu6050_Software_Filter_Update(ax, ay, az, gx, gy, gz,
                                           &cax, &cay, &caz, &cgx, &cgy, &cgz,
                                           &sax, &say, &saz, &sgx, &sgy, &sgz);
            Mpu6050_Last_Accel_X = sax;
            Mpu6050_Last_Accel_Y = say;
            Mpu6050_Last_Accel_Z = saz;
            Mpu6050_Last_Gyro_X = sgx;
            Mpu6050_Last_Gyro_Y = sgy;
            Mpu6050_Last_Gyro_Z = sgz;
            // 更新运动指数（EMA），供上层 SignalFusion 融合使用
            {
                float mag_g = sqrtf((float)sax*sax + (float)say*say + (float)saz*saz) / MPU_ACCEL_LSB_PER_G;
                float dev = fabsf(mag_g - 1.0f);
                Mpu6050_Motion_Index = 0.98f * Mpu6050_Motion_Index + 0.02f * dev;
            }
            
            // 通过消息队列发送加速度和陀螺仪数据
            // Message_Queue_Send_Accelerometer(ax, ay, az);
            // Message_Queue_Send_Gyroscope(gx, gy, gz);
            
            // 设置数据就绪标志（保持向后兼容性）
            Mpu6050_Data_Ready_Flag = true;

            if (buffer_idx < MPU6050_BUFFER_SIZE) {
                ax_buffer[buffer_idx] = cax;
                ay_buffer[buffer_idx] = cay;
                az_buffer[buffer_idx] = caz;
                buffer_idx++;
            }

            if (buffer_idx >= MPU6050_BUFFER_SIZE) {
                bool alarm = Mpu6050_Detect_Fall(ax_buffer, ay_buffer, az_buffer, MPU6050_BUFFER_SIZE);

                if (alarm) {
                    ESP_LOGW(TAG, "ALARM! 检测到撞击/跌倒事件");
                    // 通过消息队列发送跌倒/撞击预警
                    Message_Queue_Send_Alert(true, false, false);
					// 如果蜂鸣器未响，才响
					if(!isBuzzerOn)
					{
						buzzer_notify_on_from_sensor(); 
					}
                }

                int32_t sum_ax = 0, sum_ay = 0, sum_az = 0;
                for (int i = 0; i < MPU6050_BUFFER_SIZE; i++) {
                    sum_ax += ax_buffer[i];
                    sum_ay += ay_buffer[i];
                    sum_az += az_buffer[i];
                }
                // ESP_LOGI(TAG, "Avg Acc (LSB): X=%ld Y=%ld Z=%ld | Detected: %s",
                //          sum_ax / MPU6050_BUFFER_SIZE,
                //          sum_ay / MPU6050_BUFFER_SIZE,
                //          sum_az / MPU6050_BUFFER_SIZE,
                //          alarm ? "YES" : "no");

                buffer_idx = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(20));   // 50Hz sampling loop
        } else {
            ESP_LOGE(TAG, "读取原始数据失败: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
