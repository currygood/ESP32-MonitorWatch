#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "max30102.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
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
#include "esp_sleep.h"
#include "ulp_riscv.h"

#include "soc/soc.h"
#include "soc/gpio_periph.h"
#include "hal/gpio_hal.h"

#include "ulp_main.h"


// 全局变量
typedef enum
{
	KEY2_LONGPRESS_ENTER_SLEEP,
	LOW_POWER_ENTER_SLEEP,
}DeepSleep_Event;
// 引用 ULP 中定义的缓冲区大小
#define ULP_BUF_SIZE 100
TaskHandle_t Buzzer_Task_Handle = NULL;  // Buzzer的Handler
TaskHandle_t MQTT_Task_Handle = NULL;    // MQTT的Handler
TaskHandle_t APP_MAIN_Handle = NULL;	//app_main的Handler

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static uint8_t Wake_Warm = 0;   //1: HR   2: FALL

// 函数声明
void My_Key_Callback(key_id_t id, key_event_t event);
static void init_ulp_program(void);

void app_main(void) 
{
	APP_MAIN_Handle = xTaskGetCurrentTaskHandle();
	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	int32_t final_spo2, final_hr;
			int8_t spo2_v, hr_v;
    if (cause == ESP_SLEEP_WAKEUP_ULP) {
        ESP_LOGE("MainWake","🔔 ULP 唤醒！检测到潜在异常...wakeup_reaon=%ld\n",ulp_wakeup_reason);

        if (ulp_wakeup_reason == 1) {
            ESP_LOGE("MainWake","原因：心率异常（连续2次）\n");
			// 使用你提供的算法进行二次确认
			
			// 注意：将 ULP 的 uint32_t 缓冲区传给你主 CPU 的算法
			Max30102_Algorithm_Calculate((uint32_t*)ulp_shared_ir_buf, ULP_BUF_SIZE, 
										(uint32_t*)ulp_shared_red_buf, 
										&final_spo2, &spo2_v, &final_hr, &hr_v);
			
			if (hr_v) ESP_LOGE("MainWake","主算法确认心率: %ld bpm, 血氧: %ld%%\n", final_hr, final_spo2);
			if(final_hr > Max30102_Get_Heart_Rate_Baseline()+HEART_RATE_WARNING_THRESHOLD_HIGH 
				|| final_hr < Max30102_Get_Heart_Rate_Baseline()-HEART_RATE_WARNING_THRESHOLD_LOW 
				|| final_spo2<90)
			{
				Wake_Warm = 1;
			}
			
        } else if (ulp_wakeup_reason == 2) {
            ESP_LOGE("MainWake","原因：跌倒冲击\n");
			// 使用你提供的跌倒检测函数确认
			bool is_fall = Mpu6050_Detect_Fall_Or_Convulsion((int16_t*)ulp_shared_ax_buf, 
																(int16_t*)ulp_shared_ay_buf, 
																(int16_t*)ulp_shared_az_buf, 
																ULP_BUF_SIZE);
			if(is_fall)
			{
				ESP_LOGE("MainWake","🚨 确认跌倒/抽搐报警！\n");
				Wake_Warm = 2;
			} 
			
        }

        // 报警处理完后，清除唤醒原因，准备再次入睡
        ulp_wakeup_reason = 0;
    } else {
        ESP_LOGE("MainWaitInDeepSleep","🚀 等待电量过低或者手动进入深度睡眠+ulp\n");
    }
	
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

	vTaskDelay(pdMS_TO_TICKS(500));

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

	// 初始化蜂鸣器
	buzzer_init(BUZZER_GPIO_NUM, BUZZER_FREQ_HZ);

	// 按键初始化
	Key_Init(My_Key_Callback); // 传入 NULL 使用轮询模式，后续通过 Key_Get() 获取按键事件

	vTaskDelay(pdMS_TO_TICKS(500)); 	//等待500ms，确保I2C总线和MessageQueue等设备初始化完成

	// 创建任务
	xTaskCreatePinnedToCore(Task_Buzzer, "Buzzer_Task", 2048, NULL, 2, &Buzzer_Task_Handle,0); // 创建蜂鸣器任务并保存句柄
	xTaskCreatePinnedToCore(Task_MQTT_Message_Handler, "MQTT_Task", 10240, NULL, 3, &MQTT_Task_Handle, 0); 
	xTaskCreatePinnedToCore(Task_Max30102_Monitor, "max30102_Task", 4096, (void *)Buzzer_Task_Handle, 5, NULL, 1);
	xTaskCreatePinnedToCore(Task_Mpu6050_Monitor, "MPU6050_Task", 4096, (void *)Buzzer_Task_Handle, 5, NULL, 1);
	xTaskCreatePinnedToCore(Task_OLED_Show, "Task_OLED_Show", 10240, NULL, 2, NULL, 1);

	// 短暂延迟确保任务启动，然后让app_main自然结束
    vTaskDelay(pdMS_TO_TICKS(200));

	// 初始化结束后检查是不是因为唤醒重启，如果是，那有异常数据处理    放到这里的原因是message模块和mqtt初始化后才能发送数据
	if(Wake_Warm == 1)
	{
		Message_Queue_Send_Heart_Rate((uint32_t)final_hr, final_spo2,
												Max30102_Get_Heart_Rate_Baseline(), true);
	}
	else if(Wake_Warm == 2)
	{
		Message_Queue_Send_Alert(true, true, false);	
	}

	float voltage;
	uint8_t batteryLevel = 0;
	Battery_Read_Voltage(&voltage);
	batteryLevel = Battery_Calculate_Percentage(voltage);
	if(batteryLevel<40)
	{
		xTaskNotify(APP_MAIN_Handle, LOW_POWER_ENTER_SLEEP, eSetValueWithOverwrite);
	}
	uint32_t received_cmd;
	if(xTaskNotifyWait(0, 0xFFFFFFFF, &received_cmd, portMAX_DELAY) == pdPASS) 
	{
		if(received_cmd == KEY2_LONGPRESS_ENTER_SLEEP || received_cmd == LOW_POWER_ENTER_SLEEP)
		{
			ESP_LOGI("MainSleep", "准备进入深度睡眠...");

			// 1. 关闭屏幕 (你已经做了)
			OLED_Notify_Show(false);
			OLED_Clear();
			OLED_Update();
			OLED_WriteCommand(0xAE); 

			// 2. 关闭网络 (关键！)
			esp_mqtt_client_stop(MQTT_Give()); // 需传入你的 client 句柄
			esp_wifi_stop();
			esp_netif_deinit();

			// 3. 停止蜂鸣器和任务
			buzzer_notify_off_from_key(); 

			// 4. 初始化并启动 ULP
			ulp_wakeup_reason = 0; 
			init_ulp_program();

			// 5. 配置唤醒源
			ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
			
			// (可选) 隔离 GPIO：防止引脚浮空产生漏电
			// 对于没有使用的引脚，可以设置为模拟输入模式或加上下拉
			// rtc_gpio_isolate(GPIO_NUM_x); 

			// 6. 进入深度睡眠
			ESP_LOGE("MainWake","主 CPU 即将关闭...\n");
			vTaskDelay(pdMS_TO_TICKS(100)); // 给一点时间让日志打印完
			esp_deep_sleep_start();
		}
	}
}

// 定义全局回调
static bool isOLEDShow = false;
static uint8_t OLED_ShowState = 1; // 1: 默认显示时间和电量；2: 显示心率和血氧

void My_Key_Callback(key_id_t id, key_event_t event) {
    if (id == KEY_1) {
        if (event == KEY_EVENT_SINGLE_CLICK) {
            buzzer_notify_off_from_key(); // 通过任务通知关闭蜂鸣器
        }
		if(event == KEY_EVENT_DOUBLE_CLICK) {
			// 长按切换OLED显示状态
			isOLEDShow = !isOLEDShow;
			OLED_Notify_Show(isOLEDShow);
		}
		if(event == KEY_EVENT_LONG_PRESS)
		{
			// 进入ULP处理
			xTaskNotify(APP_MAIN_Handle, KEY2_LONGPRESS_ENTER_SLEEP, eSetValueWithOverwrite);
		}
    }
	if(id == KEY_2) {
		if(event == KEY_EVENT_SINGLE_CLICK) {
			// 如果OLED是打开的，那切换显示内容
			if(isOLEDShow) {
				OLED_ShowState = OLED_ShowState==1 ? 2 : 1; // 切换状态
				OLED_Set_ShowState(OLED_ShowState);
			}
		}
		if(event == KEY_EVENT_LONG_PRESS) {
			// 长按触发ap配网
			xTaskNotify(MQTT_Task_Handle, AP_Enter_Provision, eSetValueWithOverwrite);
		}
	}
}


static void init_ulp_program(void)
{
    esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);

    rtc_gpio_init(GPIO_NUM_1);
    rtc_gpio_set_direction(GPIO_NUM_1, RTC_GPIO_MODE_INPUT_OUTPUT_OD);
    rtc_gpio_pulldown_dis(GPIO_NUM_1);
    rtc_gpio_pullup_en(GPIO_NUM_1);

    rtc_gpio_init(GPIO_NUM_2);
    rtc_gpio_set_direction(GPIO_NUM_2, RTC_GPIO_MODE_INPUT_OUTPUT_OD);
    rtc_gpio_pulldown_dis(GPIO_NUM_2);
    rtc_gpio_pullup_en(GPIO_NUM_2);
    
    /* Start the program */
    err = ulp_riscv_run();
    ESP_ERROR_CHECK(err);
}