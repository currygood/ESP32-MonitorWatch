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

void Task_Sensor(void *pvParameters)
{
	// 等待MAX30102初始化完成
	vTaskDelay(pdMS_TO_TICKS(1000));
	
	// 获取I2C总线句柄并初始化OLED
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	if (i2c_bus != NULL) {
		if (OLED_Init(i2c_bus) == ESP_OK) {
			ESP_LOGI("OLED", "OLED初始化成功");
			
			// OLED显示初始化界面
		OLED_Clear();
		OLED_ShowString(1, 1, "HR: --- bpm");
		OLED_ShowString(2, 1, "SpO2: ---%");
		OLED_ShowString(3, 1, "Accel: ---");
		OLED_ShowString(4, 1, "Status: Ready");
		OLED_ShowString(5, 1, "Base: --- bpm");
		OLED_ShowString(6, 1, "Alert: ---");
		} else {
			ESP_LOGE("OLED", "OLED初始化失败");
		}
	} else {
		ESP_LOGE("OLED", "无法获取I2C总线句柄");
	}
	
	while(1)
	{
		if(Max30102_Can_Read())
		{
			// 获取心率和血氧数据
			uint32_t heart_rate = Max301020_Get_Heart_Rate();
			uint32_t spo2 = Max30102_Get_Spo2();
			uint32_t baseline = Max30102_Get_Heart_Rate_Baseline();
			bool warning_active = Max30102_Is_Heart_Rate_Warning_Active();
			
			// 在OLED上显示数据
			OLED_ShowNum(1, 5, heart_rate, 3);
			OLED_ShowNum(2, 7, spo2, 3);
			OLED_ShowNum(5, 7, baseline, 3);
			
			// 显示预警状态
			if (warning_active) {
				OLED_ShowString(6, 7, "WARNING!");
				OLED_ShowString(4, 8, "High HR!  ");
			} else {
				OLED_ShowString(6, 7, "Normal  ");
				OLED_ShowString(4, 8, "Measuring");
			}
			 
			// 发送JSON数据
			Max30102_Send_JSON_Data();
			Max30102_Clear_Flag();
		}
		else
		{
			// 没有新数据时显示等待状态
			OLED_ShowString(4, 8, "Waiting  ");
			OLED_ShowString(6, 7, "        ");
		}
		
		// 获取MPU6050加速度数据
		if(Mpu6050_Can_Read())
		{
			int16_t ax, ay, az;
			Mpu6050_Get_Accel_Data(&ax, &ay, &az);
			
			// 在OLED上显示加速度数据（显示X轴加速度作为示例）
			OLED_ShowNum(3, 8, (uint32_t)abs(ax), 4);
			
			// 清除MPU6050标志位
			Mpu6050_Clear_Flag();
		}

		vTaskDelay(pdMS_TO_TICKS(100));	//每100ms获取一次数据
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
	
	// 创建任务并传递I2C总线句柄
	xTaskCreate(Max30102_Monitor_Task, "Max30102_Monitor_Task", 4096, NULL, 2, NULL);
	xTaskCreate(Mpu6050_Monitor_Task, "Mpu6050_Monitor_Task", 4096, NULL, 2, NULL);
	xTaskCreate(Task_Sensor, "Task_Sensor", 2048, NULL, 1, NULL);
}