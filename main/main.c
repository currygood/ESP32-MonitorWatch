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
#include "OLED_Data.h"
#include "MPU6050.h"
#include "MessageQueue.h"
#include "mqtt.h"


void app_main(void) 
{
	// 统一初始化I2C总线
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	if (I2c_Init_Bus(I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ, &i2c_bus) != ESP_OK) {
		ESP_LOGE("APP_MAIN", "I2C总线初始化失败");
		return;
	}
	
	ESP_LOGI("APP_MAIN", "I2C总线初始化成功");
	
	// 初始化消息队列
	if (!Message_Queue_Init()) {
		ESP_LOGE("TASK_SENSOR", "消息队列初始化失败");
	}

	vTaskDelay(pdMS_TO_TICKS(500)); 	//等待500ms，确保I2C总线和MessageQueue初始化完成

	// 创建任务
	xTaskCreate(Task_Max30102_Monitor, "Task_Max30102_Monitor", 4096, NULL, 3, NULL);
	xTaskCreate(Task_Mpu6050_Monitor, "Task_Mpu6050_Monitor", 4096, NULL, 3, NULL);
	xTaskCreate(Task_MQTT_Message_Handler,"Task_MQTT_Message_Handler",10240 ,NULL,3,NULL);
	xTaskCreate(Task_OLED_Show,"Task_OLED_Show",8192 ,NULL,2,NULL);

	// 短暂延迟确保任务启动，然后让app_main自然结束
    vTaskDelay(pdMS_TO_TICKS(100));
}