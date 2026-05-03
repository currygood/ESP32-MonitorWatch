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
#include "rtc_driver.h"
#include "GetBaLevel.h"
#include "Buzzer.h"
#include "Key.h"
#include "UI_Events.h"

void app_main(void) 
{
	// 1. NVS 必须在最前面初始化（WiFi、MQTT 凭据都需要它）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

	// 统一初始化I2C总线
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	if (I2c_Init_Bus(I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ, &i2c_bus) != ESP_OK) {
		ESP_LOGE("APP_MAIN", "I2C总线初始化失败");
		return;
	}
	
	// 整个程序只在这里写一次！
    ESP_ERROR_CHECK(gpio_install_isr_service(0)); 
    // ESP_LOGI("APP_MAIN", "GPIO ISR 服务安装成功");  //成功就不管，没必要输出了

	// 初始化消息队列
	if (!Message_Queue_Init()) {
		ESP_LOGE("TASK_SENSOR", "消息队列初始化失败");
	}
	
	// RTC初始化
	esp_err_t rtc_ret = Rtc_Init();
	if(rtc_ret != ESP_OK) {
		ESP_LOGE("APP_MAIN", "RTC 初始化失败: %s", esp_err_to_name(rtc_ret));
	}

	//获取电池电量初始化
	Battery_Level_Init();
	
	//初始化按键
	Key_Init();

	// 初始化蜂鸣器
	buzzer_init(BUZZER_GPIO_NUM, BUZZER_FREQ_HZ);

	vTaskDelay(pdMS_TO_TICKS(500)); 	//等待500ms，确保I2C总线和MessageQueue等设备初始化完成
	// 1. 创建 UI 消息队列 (容量10，存 ui_command_t)
    QueueHandle_t ui_msg_queue = xQueueCreate(10, sizeof(ui_command_t));
    // 2. 创建按键逻辑任务，把队列句柄传进去
    
	// 创建任务
	// xTaskCreate(Task_Max30102_Monitor, "Task_Max30102_Monitor", 4096, NULL, 3, NULL);
	// xTaskCreate(Task_Mpu6050_Monitor, "Task_Mpu6050_Monitor", 4096, NULL, 3, NULL);
	// xTaskCreate(Task_MQTT_Message_Handler,"Task_MQTT_Message_Handler",10240 ,NULL,3,NULL);
	// xTaskCreate(Task_OLED_Show,"Task_OLED_Show",8192 ,NULL,2,NULL);
	// xTaskCreate(Task_Buzzer,"Task_Buzzer",1024 ,NULL,3,NULL);

	xTaskCreatePinnedToCore(Task_MQTT_Message_Handler, "MQTT_Task", 10240, NULL, 3, NULL, 0); 
	xTaskCreatePinnedToCore(Task_Max30102_Monitor, "Sensor_Task", 4096, NULL, 5, NULL, 1);
	xTaskCreatePinnedToCore(Task_Mpu6050_Monitor, "MPU6050_Task", 4096, NULL, 5, NULL, 1);
	xTaskCreatePinnedToCore(Task_OLED_Show, "Task_OLED_Show", 8192, (void*)ui_msg_queue, 2, NULL, 1);
	xTaskCreatePinnedToCore(Task_Buzzer,"Task_Buzzer",2048,NULL,1,NULL,0);
	xTaskCreatePinnedToCore(Task_Key_Processor, "Key_Proc", 3072, (void*)ui_msg_queue, 4, NULL, 1);
	
	// 短暂延迟确保任务启动，然后让app_main自然结束
    vTaskDelay(pdMS_TO_TICKS(100));
}