#include "MessageQueue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MESSAGE_QUEUE";

// 消息队列句柄
//调试代码注释了
//QueueHandle_t Sensor_Message_Queue_TO_OLED = NULL;
QueueHandle_t Sensor_Message_Queue_TO_MQTT = NULL;
static bool queue_init_error_logged = false;
static bool queue_not_init_error_logged = false;

// 消息队列初始化
bool Message_Queue_Init(void)
{
    // 创建消息队列，最多存储10条消息
    Sensor_Message_Queue_TO_MQTT = xQueueCreate(10, sizeof(Sensor_Message_t));

	//调试的代码注释了
	//Sensor_Message_Queue_TO_OLED = xQueueCreate(10, sizeof(Sensor_Message_t));
    
    if (Sensor_Message_Queue_TO_MQTT == NULL) {
        if (!queue_init_error_logged) {
            ESP_LOGE(TAG, "消息队列创建失败");
            queue_init_error_logged = true;
        }
        return false;
    }
    
    ESP_LOGI(TAG, "消息队列初始化成功");
    queue_init_error_logged = false;
    queue_not_init_error_logged = false;
    return true;
}

// 发送心率血氧数据消息
bool Message_Queue_Send_Heart_Rate(uint32_t heart_rate, uint32_t spo2, uint32_t baseline, bool warning_active)
{
    if (Sensor_Message_Queue_TO_MQTT == NULL) {
        if (!queue_not_init_error_logged) {
            ESP_LOGE(TAG, "消息队列未初始化");
            queue_not_init_error_logged = true;
        }
        return false;
    }
    
    Sensor_Message_t message;
    message.Message_Type = MESSAGE_TYPE_HEART_RATE_SPO2;
    message.Timestamp = (uint32_t)(esp_timer_get_time() / 1000); // 毫秒时间戳
    
    message.Data.Heart_Rate_SPO2_Data.Heart_Rate = heart_rate;
    message.Data.Heart_Rate_SPO2_Data.SpO2 = spo2;
    message.Data.Heart_Rate_SPO2_Data.Baseline = baseline;
    message.Data.Heart_Rate_SPO2_Data.Warning_Active = warning_active;
    
    BaseType_t result_MQTT = xQueueSend(Sensor_Message_Queue_TO_MQTT, &message, pdMS_TO_TICKS(100));
    //这个调试代码注释了
	//BaseType_t result_OLED = xQueueSend(Sensor_Message_Queue_TO_OLED, &message, pdMS_TO_TICKS(100));
    
    if (result_MQTT == pdTRUE) {
        // ESP_LOGD(TAG, "心率血氧消息发送成功: HR=%lu, SpO2=%lu, Baseline=%lu, Warning=%d", 
        //          heart_rate, spo2, baseline, warning_active);
        return true;
    } else {
        // ESP_LOGW(TAG, "心率血氧消息发送失败，队列可能已满");
        return false;
    }

	//调试代码注释了
	// if(result_OLED==pdTRUE)
	// 	return true;
}

// 发送加速度计数据消息
bool Message_Queue_Send_Accelerometer(int16_t ax, int16_t ay, int16_t az)
{
    if (Sensor_Message_Queue_TO_MQTT == NULL) {
        if (!queue_not_init_error_logged) {
            ESP_LOGE(TAG, "消息队列未初始化");
            queue_not_init_error_logged = true;
        }
        return false;
    }
    
    Sensor_Message_t message;
    message.Message_Type = MESSAGE_TYPE_ACCELEROMETER;
    message.Timestamp = (uint32_t)(esp_timer_get_time() / 1000); // 毫秒时间戳
    
    message.Data.Accelerometer_Data.Accel_X = ax;
    message.Data.Accelerometer_Data.Accel_Y = ay;
    message.Data.Accelerometer_Data.Accel_Z = az;
    
    BaseType_t result_MQTT = xQueueSend(Sensor_Message_Queue_TO_MQTT, &message, pdMS_TO_TICKS(100));
    //调试代码注释了
	//BaseType_t result_OLED = xQueueSend(Sensor_Message_Queue_TO_OLED, &message, pdMS_TO_TICKS(100));
    
    if (result_MQTT == pdTRUE) {
        // ESP_LOGD(TAG, "加速度计消息发送成功: X=%d, Y=%d, Z=%d", ax, ay, az);
        return true;
    } else {
        // ESP_LOGW(TAG, "加速度计消息发送失败，队列可能已满");
        return false;
    }


	//调试代码注释了
	// if(result_OLED==pdTRUE)
	// 	return true;
}

// 发送陀螺仪数据消息
bool Message_Queue_Send_Gyroscope(int16_t gx, int16_t gy, int16_t gz)
{
    if (Sensor_Message_Queue_TO_MQTT == NULL) {
        ESP_LOGE(TAG, "消息队列未初始化");
        return false;
    }
    
    Sensor_Message_t message;
    message.Message_Type = MESSAGE_TYPE_GYROSCOPE;
    message.Timestamp = (uint32_t)(esp_timer_get_time() / 1000); // 毫秒时间戳
    
    message.Data.Gyroscope_Data.Gyro_X = gx;
    message.Data.Gyroscope_Data.Gyro_Y = gy;
    message.Data.Gyroscope_Data.Gyro_Z = gz;
    
    BaseType_t result_MQTT = xQueueSend(Sensor_Message_Queue_TO_MQTT, &message, pdMS_TO_TICKS(100));
    //调试代码注释了
	//BaseType_t result_OLED = xQueueSend(Sensor_Message_Queue_TO_OLED, &message, pdMS_TO_TICKS(100));
    
    if (result_MQTT == pdTRUE) {
        // ESP_LOGD(TAG, "陀螺仪消息发送成功: X=%d, Y=%d, Z=%d", gx, gy, gz);
        return true;
    } else {
        ESP_LOGW(TAG, "陀螺仪消息发送失败，队列可能已满");
        return false;
    }

	//调试代码注释了
	// if(result_OLED==pdTRUE)
	// 	return true;
}

// 发送预警消息
bool Message_Queue_Send_Alert(bool fall_detected, bool convulsion_detected, bool heart_rate_warning)
{
    if (Sensor_Message_Queue_TO_MQTT == NULL) {
        ESP_LOGE(TAG, "消息队列未初始化");
        return false;
    }
    
    Sensor_Message_t message;
    message.Message_Type = MESSAGE_TYPE_ALERT;
    message.Timestamp = (uint32_t)(esp_timer_get_time() / 1000); // 毫秒时间戳
    
    message.Data.Alert_Data.Fall_Detected = fall_detected;
    message.Data.Alert_Data.Convulsion_Detected = convulsion_detected;
    message.Data.Alert_Data.Heart_Rate_Warning = heart_rate_warning;
    
    BaseType_t result_MQTT = xQueueSend(Sensor_Message_Queue_TO_MQTT, &message, pdMS_TO_TICKS(100));
	//这个调试代码注释了
	//BaseType_t result_OLED = xQueueSend(Sensor_Message_Queue_TO_OLED, &message, pdMS_TO_TICKS(100));
    
    if (result_MQTT == pdTRUE) {
        // ESP_LOGW(TAG, "预警消息发送: 跌倒=%d, 抽搐=%d, 心率预警=%d", 
                //  fall_detected, convulsion_detected, heart_rate_warning);
        return true;
    } else {
        // ESP_LOGW(TAG, "预警消息发送失败，队列可能已满");
        return false;
    }

	//这个调试代码注释了
	// if(result_OLED==pdTRUE)
	// 	return true;
}

// 接收消息
bool Message_Queue_Receive(QueueHandle_t queue_handle, Sensor_Message_t *message, TickType_t timeout)
{
    if (queue_handle == NULL) {
        ESP_LOGE(TAG, "消息队列未初始化");
        return false;
    }
    
    BaseType_t resultTT = xQueueReceive(queue_handle, message, timeout);
    
    return (resultTT == pdTRUE);
}



QueueHandle_t Message_Queue_Get_Handle(enum QueueType queue_type)
{
	//这个调试代码注释了
    // if (queue_type == QUEUE_TYPE_MQTT) {
    //     return Sensor_Message_Queue_TO_MQTT;
    // } else if (queue_type == QUEUE_TYPE_OLED) {
    //     return Sensor_Message_Queue_TO_OLED;
    // } else {
    //     ESP_LOGE(TAG, "无效的队列类型: %d", queue_type);
    //     return NULL;
    // }
	if(queue_type == QUEUE_TYPE_MQTT){
		return Sensor_Message_Queue_TO_MQTT;
	}else{
		ESP_LOGE(TAG, "无效的队列类型: %d", queue_type);
		return NULL;
	}
}