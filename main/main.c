#include <stdio.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "max30102.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "i2c_driver.h"
#include "OLED.h"
#include "OLED_Font.h"
#include "MPU6050.h"
#include "MessageQueue.h"

// 消息队列定义
#define OLED_QUEUE_LENGTH 10
#define OLED_QUEUE_ITEM_SIZE sizeof(char[40])

// 全局消息队列句柄
QueueHandle_t Oled_Message_Queue = NULL;

// 消息类型枚举
enum {
    MSG_TYPE_NORMAL = 0,
    MSG_TYPE_HEART_RATE_WARNING,
    MSG_TYPE_FALL_DETECTED
};

void MainControl(void *pvParameters)
{
	// 等待各种硬件初始化完成
	vTaskDelay(pdMS_TO_TICKS(1000));
	
	// 初始化消息队列
	if (!Message_Queue_Init()) {
		ESP_LOGE("TASK_SENSOR", "消息队列初始化失败");
	}
	
	while(1)
	{
		Sensor_Message_t message;
		
		// 从消息队列接收数据（非阻塞方式，最多等待50ms）
		if (Message_Queue_Receive(&message, pdMS_TO_TICKS(50))) {
			switch (message.Message_Type) {
				case MESSAGE_TYPE_HEART_RATE:
					// 处理心率血氧数据
					
					// 检测异常数据：心率过高（>120）或血氧过低（<90%），发送警告消息
					if (message.Data.Heart_Rate_Data.Heart_Rate > 120 || message.Data.Heart_Rate_Data.SpO2 < 90) {
						char warning_msg[40];
						if (message.Data.Heart_Rate_Data.Heart_Rate > 120) {
							snprintf(warning_msg, sizeof(warning_msg), "High HR: %lu", message.Data.Heart_Rate_Data.Heart_Rate);
						} else {
							snprintf(warning_msg, sizeof(warning_msg), "Low SpO2: %lu%%", message.Data.Heart_Rate_Data.SpO2);
						}
						
						// 发送警告消息到OLED任务
						if (Oled_Message_Queue != NULL) {
							xQueueSend(Oled_Message_Queue, warning_msg, pdMS_TO_TICKS(10));
							ESP_LOGW("TASK_SENSOR", "Warning sent: %s", warning_msg);
						}
					}
					
					// 发送JSON数据（保持原有功能）
					Max30102_Send_JSON_Data();
					break;
					
				case MESSAGE_TYPE_ACCELEROMETER:
					// 处理加速度数据 - 移除OLED显示
					ESP_LOGD("TASK_SENSOR", "加速度数据: X=%d, Y=%d, Z=%d", 
							 message.Data.Accelerometer_Data.Accel_X,
							 message.Data.Accelerometer_Data.Accel_Y,
							 message.Data.Accelerometer_Data.Accel_Z);
					break;
					
				case MESSAGE_TYPE_GYROSCOPE:
					// 处理陀螺仪数据
					ESP_LOGD("TASK_SENSOR", "陀螺仪数据: X=%d, Y=%d, Z=%d", 
							 message.Data.Gyroscope_Data.Gyro_X,
							 message.Data.Gyroscope_Data.Gyro_Y,
							 message.Data.Gyroscope_Data.Gyro_Z);
					break;
					
				case MESSAGE_TYPE_ALERT:
					// 处理预警消息
					if (message.Data.Alert_Data.Fall_Detected || message.Data.Alert_Data.Convulsion_Detected) {
						char alert_msg[40];
						if (message.Data.Alert_Data.Fall_Detected) {
							snprintf(alert_msg, sizeof(alert_msg), "Fall Detected!");
						} else {
							snprintf(alert_msg, sizeof(alert_msg), "Convulsion!");
						}
						
						// 发送紧急预警消息到OLED任务
						if (Oled_Message_Queue != NULL) {
							xQueueSend(Oled_Message_Queue, alert_msg, pdMS_TO_TICKS(10));
							ESP_LOGE("TASK_SENSOR", "Emergency alert: %s", alert_msg);
						}
					}
					break;
					
				default:
					ESP_LOGW("TASK_SENSOR", "未知消息类型: %d", message.Message_Type);
					break;
				}
		}

		vTaskDelay(pdMS_TO_TICKS(100));	//每100ms处理一次数据
	}
}

void OLED_Show(void *pvParameters)
{
	// 等待各种硬件初始化完成
	vTaskDelay(pdMS_TO_TICKS(1000));

	// 获取I2C总线句柄并初始化OLED
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	if (i2c_bus != NULL) {
		if (OLED_Init(i2c_bus) == ESP_OK) {
			ESP_LOGI("OLED", "OLED初始化成功");
			
			// OLED显示初始化界面 - 64x128屏幕适配
			OLED_Clear();
			OLED_ShowString(1, 1, "Health Monitor");
			OLED_ShowString(2, 1, "Status: Normal");
			OLED_ShowString(4, 1, "HR: ---");
			OLED_ShowString(5, 1, "SpO2: ---%");
			OLED_ShowString(7, 1, "Ready");
		} else {
			ESP_LOGE("OLED", "OLED初始化失败");
		}
	} else {
		ESP_LOGE("OLED", "无法获取I2C总线句柄");
	}

	char received_msg[40];
	TickType_t last_warning_time = 0;
	const TickType_t warning_duration = pdMS_TO_TICKS(5000); // 警告显示5秒
	
	while(1)
	{
		// 检查消息队列
		if (Oled_Message_Queue != NULL) {
			if (xQueueReceive(Oled_Message_Queue, received_msg, pdMS_TO_TICKS(100)) == pdTRUE) {
				ESP_LOGI("OLED_QUEUE", "收到警告消息: %s", received_msg);
				
				// 在OLED上显示警告消息 - 适配64x128屏幕
				OLED_ShowString(2, 1, "Status: ALERT!");
				OLED_ShowString(6, 1, received_msg);
				OLED_ShowString(7, 1, "Check Patient!");
				
				// 记录警告时间
				last_warning_time = xTaskGetTickCount();
			}
		}
		
		// 检查是否需要清除警告显示
		if (last_warning_time > 0) {
			TickType_t current_time = xTaskGetTickCount();
			if (current_time - last_warning_time > warning_duration) {
				// 清除警告显示，恢复正常状态
				OLED_ShowString(2, 1, "Status: Normal  ");
				OLED_ShowString(6, 1, "              ");
				OLED_ShowString(7, 1, "Monitoring...  ");
				last_warning_time = 0;
			}
		}
		
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}


void app_main(void) 
{
	// 统一初始化I2C总线
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	if (I2c_Init_Bus(I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ, &i2c_bus) != ESP_OK) {
		ESP_LOGE("APP_MAIN", "I2C总线初始化失败");
		return;
	}
	
	ESP_LOGI("APP_MAIN", "I2C总线初始化成功");
	
	// 创建消息队列
	Oled_Message_Queue = xQueueCreate(OLED_QUEUE_LENGTH, OLED_QUEUE_ITEM_SIZE);
	if (Oled_Message_Queue == NULL) {
		ESP_LOGE("APP_MAIN", "消息队列创建失败");
		return;
	}
	ESP_LOGI("APP_MAIN", "消息队列创建成功");
	
	// 创建任务
	xTaskCreate(Max30102_Monitor_Task, "Max30102_Monitor_Task", 4096, NULL, 2, NULL);
	xTaskCreate(Mpu6050_Monitor_Task, "Mpu6050_Monitor_Task", 4096, NULL, 2, NULL);
	xTaskCreate(MainControl, "MainControl", 2048, NULL, 1, NULL);
	xTaskCreate(OLED_Show,"OLED_Show",2048,NULL,1,NULL);
}