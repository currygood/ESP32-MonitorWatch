#include "MessageQueue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MESSAGE_QUEUE";

// 消息队列句柄
QueueHandle_t Sensor_Message_Queue = NULL;

// 消息队列初始化
bool Message_Queue_Init(void)
{
    // 创建消息队列，最多存储10条消息
    Sensor_Message_Queue = xQueueCreate(10, sizeof(Sensor_Message_t));
    
    if (Sensor_Message_Queue == NULL) {
        ESP_LOGE(TAG, "消息队列创建失败");
        return false;
    }
    
    ESP_LOGI(TAG, "消息队列初始化成功");
    return true;
}

// 发送心率血氧数据消息
bool Message_Queue_Send_Heart_Rate(uint32_t heart_rate, uint32_t spo2, uint32_t baseline, bool warning_active)
{
    if (Sensor_Message_Queue == NULL) {
        ESP_LOGE(TAG, "消息队列未初始化");
        return false;
    }
    
    Sensor_Message_t message;
    message.Message_Type = MESSAGE_TYPE_HEART_RATE;
    message.Timestamp = (uint32_t)(esp_timer_get_time() / 1000); // 毫秒时间戳
    
    message.Data.Heart_Rate_Data.Heart_Rate = heart_rate;
    message.Data.Heart_Rate_Data.SpO2 = spo2;
    message.Data.Heart_Rate_Data.Baseline = baseline;
    message.Data.Heart_Rate_Data.Warning_Active = warning_active;
    
    BaseType_t result = xQueueSend(Sensor_Message_Queue, &message, pdMS_TO_TICKS(100));
    
    if (result == pdTRUE) {
        ESP_LOGD(TAG, "心率血氧消息发送成功: HR=%lu, SpO2=%lu, Baseline=%lu, Warning=%d", 
                 heart_rate, spo2, baseline, warning_active);
        return true;
    } else {
        ESP_LOGW(TAG, "心率血氧消息发送失败，队列可能已满");
        return false;
    }
}

// 发送加速度计数据消息
bool Message_Queue_Send_Accelerometer(int16_t ax, int16_t ay, int16_t az)
{
    if (Sensor_Message_Queue == NULL) {
        ESP_LOGE(TAG, "消息队列未初始化");
        return false;
    }
    
    Sensor_Message_t message;
    message.Message_Type = MESSAGE_TYPE_ACCELEROMETER;
    message.Timestamp = (uint32_t)(esp_timer_get_time() / 1000); // 毫秒时间戳
    
    message.Data.Accelerometer_Data.Accel_X = ax;
    message.Data.Accelerometer_Data.Accel_Y = ay;
    message.Data.Accelerometer_Data.Accel_Z = az;
    
    BaseType_t result = xQueueSend(Sensor_Message_Queue, &message, pdMS_TO_TICKS(100));
    
    if (result == pdTRUE) {
        ESP_LOGD(TAG, "加速度计消息发送成功: X=%d, Y=%d, Z=%d", ax, ay, az);
        return true;
    } else {
        ESP_LOGW(TAG, "加速度计消息发送失败，队列可能已满");
        return false;
    }
}

// 发送陀螺仪数据消息
bool Message_Queue_Send_Gyroscope(int16_t gx, int16_t gy, int16_t gz)
{
    if (Sensor_Message_Queue == NULL) {
        ESP_LOGE(TAG, "消息队列未初始化");
        return false;
    }
    
    Sensor_Message_t message;
    message.Message_Type = MESSAGE_TYPE_GYROSCOPE;
    message.Timestamp = (uint32_t)(esp_timer_get_time() / 1000); // 毫秒时间戳
    
    message.Data.Gyroscope_Data.Gyro_X = gx;
    message.Data.Gyroscope_Data.Gyro_Y = gy;
    message.Data.Gyroscope_Data.Gyro_Z = gz;
    
    BaseType_t result = xQueueSend(Sensor_Message_Queue, &message, pdMS_TO_TICKS(100));
    
    if (result == pdTRUE) {
        ESP_LOGD(TAG, "陀螺仪消息发送成功: X=%d, Y=%d, Z=%d", gx, gy, gz);
        return true;
    } else {
        ESP_LOGW(TAG, "陀螺仪消息发送失败，队列可能已满");
        return false;
    }
}

// 发送预警消息
bool Message_Queue_Send_Alert(bool fall_detected, bool convulsion_detected, bool heart_rate_warning)
{
    if (Sensor_Message_Queue == NULL) {
        ESP_LOGE(TAG, "消息队列未初始化");
        return false;
    }
    
    Sensor_Message_t message;
    message.Message_Type = MESSAGE_TYPE_ALERT;
    message.Timestamp = (uint32_t)(esp_timer_get_time() / 1000); // 毫秒时间戳
    
    message.Data.Alert_Data.Fall_Detected = fall_detected;
    message.Data.Alert_Data.Convulsion_Detected = convulsion_detected;
    message.Data.Alert_Data.Heart_Rate_Warning = heart_rate_warning;
    
    BaseType_t result = xQueueSend(Sensor_Message_Queue, &message, pdMS_TO_TICKS(100));
    
    if (result == pdTRUE) {
        ESP_LOGW(TAG, "预警消息发送: 跌倒=%d, 抽搐=%d, 心率预警=%d", 
                 fall_detected, convulsion_detected, heart_rate_warning);
        return true;
    } else {
        ESP_LOGW(TAG, "预警消息发送失败，队列可能已满");
        return false;
    }
}

// 接收消息
bool Message_Queue_Receive(Sensor_Message_t *message, TickType_t timeout)
{
    if (Sensor_Message_Queue == NULL) {
        ESP_LOGE(TAG, "消息队列未初始化");
        return false;
    }
    
    BaseType_t result = xQueueReceive(Sensor_Message_Queue, message, timeout);
    
    return (result == pdTRUE);
}