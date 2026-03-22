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


void Task_MainControl(void *pvParameters)
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


void app_main(void) 
{
	// 统一初始化I2C总线
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	if (I2c_Init_Bus(I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ, &i2c_bus) != ESP_OK) {
		ESP_LOGE("APP_MAIN", "I2C总线初始化失败");
		return;
	}
	
	ESP_LOGI("APP_MAIN", "I2C总线初始化成功");
	
	// 创建任务
	xTaskCreate(Task_Max30102_Monitor, "Task_Max30102_Monitor", 4096, NULL, 2, NULL);
	xTaskCreate(Task_Mpu6050_Monitor, "Task_Mpu6050_Monitor", 4096, NULL, 2, NULL);
	xTaskCreate(Task_MainControl, "MainControl", 2048, NULL, 1, NULL);
	xTaskCreate(Task_OLED_Show,"Task_OLED_Show",2048,NULL,1,NULL);
}