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

// 采集缓冲
static int16_t ax_buffer[MPU6050_BUFFER_SIZE];
static int16_t ay_buffer[MPU6050_BUFFER_SIZE];
static int16_t az_buffer[MPU6050_BUFFER_SIZE];
static int buffer_idx = 0;

// 初始化
void Mpu6050_Init(void) {
    i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
    // 添加 MPU6050 设备
    ESP_ERROR_CHECK(I2c_Add_Device(i2c_bus, MPU6050_ADDR, MPU6050_I2C_FREQ, &mpu6050_dev));

    ESP_LOGI(TAG, "MPU6050 设备添加成功");

    vTaskDelay(pdMS_TO_TICKS(100));

    // 软件复位
    Mpu6050_Write_Reg(MPU6050_REG_PWR_MGMT_1, 0x80);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 唤醒，选择陀螺仪时钟
    Mpu6050_Write_Reg(MPU6050_REG_PWR_MGMT_1, 0x01);

    Mpu6050_Write_Reg(MPU6050_REG_SMPLRT_DIV, 19);
    Mpu6050_Write_Reg(MPU6050_REG_CONFIG, 0x05);
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
// bool Mpu6050_Detect_Fall_Or_Convulsion(int16_t *ax_buf, int16_t *ay_buf, int16_t *az_buf, int len) {
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

// 检测函数（改进版，增加了高频震动检测，更适合抽搐识别）
// 定义全局变量
static bool s_current_is_abnormal = false; 

bool Mpu6050_Detect_Fall_Or_Convulsion(int16_t *ax_buf, int16_t *ay_buf, int16_t *az_buf, int len) {
    if (len < 10) return false;

    // --- 每次进入检测时，先初始化为 false ---
    // 这样如果这 4 秒没出事，s_current_is_abnormal 就会变回 false
    s_current_is_abnormal = false; 

    float max_mag = 0;
    float total_variation = 0;
    int high_peak_count = 0;
    float last_mag = 1.0f;

    for (int i = 0; i < len; i++) {
        float mag = sqrtf((float)ax_buf[i]*ax_buf[i] + (float)ay_buf[i]*ay_buf[i] + (float)az_buf[i]*az_buf[i]) / 2048.0f;
        if (mag > max_mag) max_mag = mag;
        total_variation += fabsf(mag - last_mag);
        if (mag > 1.8f) high_peak_count++;
        last_mag = mag;
    }

    float activity_score = total_variation / len;

    // A. 跌倒检测
    if (max_mag > 2.3f && activity_score > 0.18f) {
        ESP_LOGW(TAG, "🔔 撞击报警!");
        s_current_is_abnormal = true; 
        return true;
    }

    // B. 抽搐检测
    if (activity_score > 0.3f || (high_peak_count > 12 && activity_score > 0.25f)) {
        ESP_LOGW(TAG, "🚨 抽搐报警!");
        s_current_is_abnormal = true;
        return true;
    }

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

// 监测任务
void Task_Mpu6050_Monitor(void *pvParameters) {
    esp_log_level_set("gpio", ESP_LOG_ERROR);
    esp_log_level_set("i2c", ESP_LOG_ERROR);

	Mpu6050_Init();
	vTaskDelay(pdMS_TO_TICKS(100));	// 等待初始化完成

    int16_t ax, ay, az, gx, gy, gz;
	static bool isBuzzerOn = false;
	static uint32_t last_buzzer_time = 0;

    ESP_LOGI(TAG, "Monitor task started");
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
            Mpu6050_Last_Accel_X = ax;
            Mpu6050_Last_Accel_Y = ay;
            Mpu6050_Last_Accel_Z = az;
            Mpu6050_Last_Gyro_X = gx;
            Mpu6050_Last_Gyro_Y = gy;
            Mpu6050_Last_Gyro_Z = gz;
            
            // 通过消息队列发送加速度和陀螺仪数据
            // Message_Queue_Send_Accelerometer(ax, ay, az);
            // Message_Queue_Send_Gyroscope(gx, gy, gz);
            
            // 设置数据就绪标志（保持向后兼容性）
            Mpu6050_Data_Ready_Flag = true;

            if (buffer_idx < MPU6050_BUFFER_SIZE) {
                ax_buffer[buffer_idx] = ax;
                ay_buffer[buffer_idx] = ay;
                az_buffer[buffer_idx] = az;
                buffer_idx++;
            }

            if (buffer_idx >= MPU6050_BUFFER_SIZE) {
                bool alarm = Mpu6050_Detect_Fall_Or_Convulsion(ax_buffer, ay_buffer, az_buffer, MPU6050_BUFFER_SIZE);

                if (alarm) {
                    ESP_LOGW(TAG, "ALARM! 可能癫痫抽搐或跌倒事件");
                    // 通过消息队列发送跌倒/抽搐预警
                    Message_Queue_Send_Alert(true, true, false);
					// 如果蜂鸣器未响，才响
					if(!isBuzzerOn)
					{
						buzzer_on();	//如果心跳过快，蜂鸣器响15秒
						isBuzzerOn=true;
						last_buzzer_time = esp_timer_get_time();
					}
                }

                int32_t sum_ax = 0, sum_ay = 0, sum_az = 0;
                for (int i = 0; i < MPU6050_BUFFER_SIZE; i++) {
                    sum_ax += ax_buffer[i];
                    sum_ay += ay_buffer[i];
                    sum_az += az_buffer[i];
                }
                ESP_LOGI(TAG, "Avg Acc (LSB): X=%ld Y=%ld Z=%ld | Detected: %s",
                         sum_ax / MPU6050_BUFFER_SIZE,
                         sum_ay / MPU6050_BUFFER_SIZE,
                         sum_az / MPU6050_BUFFER_SIZE,
                         alarm ? "YES" : "no");

                buffer_idx = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(1000 / MPU6050_SAMPLES_PER_SEC));
        } else {
            ESP_LOGE(TAG, "读取原始数据失败: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
		
		if (isBuzzerOn && esp_timer_get_time() - last_buzzer_time >= 15000000) {
			isBuzzerOn = false;
			buzzer_off(); 
		}
    }
}