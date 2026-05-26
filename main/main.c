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
#include "ulp_riscv_i2c.h"
#include "soc/rtc_cntl_reg.h" 
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
TaskHandle_t Max30102_Task_Handle = NULL;
TaskHandle_t Mpu6050_Task_Handle  = NULL;
TaskHandle_t OLED_Task_Handle = NULL;

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static uint8_t Wake_Warm = 0;   //1: HR   2: FALL

// 函数声明
void My_Key_Callback(key_id_t id, key_event_t event);
static void init_ulp_program(void);
static void enter_deep_sleep_with_ulp(void);
static bool isNotFirst=false;

void app_main(void) 
{
	isNotFirst=false;
	APP_MAIN_Handle = xTaskGetCurrentTaskHandle();
	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	int32_t final_spo2, final_hr;
			int8_t spo2_v, hr_v;
    if (cause == ESP_SLEEP_WAKEUP_ULP) {
        ESP_LOGE("MainWake","🔔 ULP 唤醒！检测到潜在异常...wakeup_reaon=%ld\n",ulp_wakeup_reason);
		isNotFirst=true;
        if (ulp_wakeup_reason == 1) 
		{
            ESP_LOGE("MainWake","原因：心率异常（连续2次）\n");
			// 使用你提供的算法进行二次确认
			uint32_t main_ir[ULP_BUF_SIZE];
			uint32_t main_red[ULP_BUF_SIZE];
			uint32_t *ulp_ir_buf = (uint32_t *)&ulp_shared_ir_buf;
			uint32_t *ulp_red_buf = (uint32_t *)&ulp_shared_red_buf;
			for(int i=0; i<ULP_BUF_SIZE; i++) {
				main_ir[i] = ulp_ir_buf[i];  // 直接赋值，因为都是 uint32
				main_red[i] = ulp_red_buf[i];
			}
			// 注意：将 ULP 的 uint32_t 缓冲区传给你主 CPU 的算法
			Max30102_Algorithm_Calculate((uint32_t*)main_ir, ULP_BUF_SIZE, 
										(uint32_t*)main_red, 
										&final_spo2, &spo2_v, &final_hr, &hr_v);
			
			if (hr_v) ESP_LOGE("MainWake","主算法确认心率: %ld bpm, 血氧: %ld%%\n", final_hr, final_spo2);
			if(final_hr > Max30102_Get_Heart_Rate_Baseline()+HEART_RATE_WARNING_THRESHOLD_HIGH 
				|| final_hr < Max30102_Get_Heart_Rate_Baseline()-HEART_RATE_WARNING_THRESHOLD_LOW 
				|| final_spo2<90)
			{
				Wake_Warm = 1;
			}
        } else if (ulp_wakeup_reason == 2) 
		{
            ESP_LOGE("MainWake","原因：跌倒冲击\n");
			// 1. 定义主 CPU 的临时缓冲区（确保是紧凑排列的）
			int16_t main_ax[ULP_BUF_SIZE];
			int16_t main_ay[ULP_BUF_SIZE];
			int16_t main_az[ULP_BUF_SIZE];
			// 必须取地址 & 之后再强转
			uint32_t *ulp_ax_ptr = (uint32_t *)&ulp_shared_ax_buf;
			uint32_t *ulp_ay_ptr = (uint32_t *)&ulp_shared_ay_buf;
			uint32_t *ulp_az_ptr = (uint32_t *)&ulp_shared_az_buf;
			// 2. 从 ULP 的 32 位扩展空间中提取低 16 位有效数据
			for(int i = 0; i < ULP_BUF_SIZE; i++) {
				// 注意：ulp_shared_ax_buf[i] 实际上是 uint32_t
				main_ax[i] = (int16_t)(ulp_ax_ptr[i] & 0xFFFF);
				main_ay[i] = (int16_t)(ulp_ay_ptr[i] & 0xFFFF);
				main_az[i] = (int16_t)(ulp_az_ptr[i] & 0xFFFF);
			}

			// 3. 将解包后的紧凑数组传给算法
			bool is_fall = Mpu6050_Detect_Fall_Or_Convulsion(main_ax, main_ay, main_az, ULP_BUF_SIZE);
			
			if(is_fall) {
				ESP_LOGE("MainWake","🚨 确认跌倒/抽搐报警！\n");
				Wake_Warm = 2;
			}
        }else
		{
			ESP_LOGW("MainWake", "非法唤醒原因，忽略");
			ulp_wakeup_reason = 0; // 未知原因，重置后继续正常流程
		}

        // 报警处理完后，清除唤醒原因，准备再次入睡
        ulp_wakeup_reason = 0;
    } else 
	{
        ESP_LOGE("MainWaitInDeepSleep","🚀 等待电量过低或者手动进入深度睡眠+ulp\n");
    }

	if(isNotFirst)	ulp_riscv_halt(),ulp_riscv_reset(); // 硬件复位 ULP，确保寄存器和状态完全清零，避免残留状态干扰下一轮监测
	
	// 1. NVS 必须在最前面初始化（WiFi、MQTT 凭据都需要它）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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
	xTaskCreatePinnedToCore(Task_MQTT_Message_Handler, "MQTT_Task", 10240, NULL, 3, &MQTT_Task_Handle, 0); 
	xTaskCreatePinnedToCore(Task_OLED_Show, "Task_OLED_Show", 10240, NULL, 2, &OLED_Task_Handle, 1);
	xTaskCreatePinnedToCore(Task_Max30102_Monitor, "max30102_Task", 4096, (void *)Buzzer_Task_Handle, 5, &Max30102_Task_Handle, 1);
	xTaskCreatePinnedToCore(Task_Mpu6050_Monitor, "MPU6050_Task", 4096, (void *)Buzzer_Task_Handle, 5, &Mpu6050_Task_Handle, 1);
	xTaskCreatePinnedToCore(Task_Buzzer, "Buzzer_Task", 2048, NULL, 2, &Buzzer_Task_Handle,0); // 创建蜂鸣器任务并保存句柄

	vTaskDelay(pdMS_TO_TICKS(11000)); // 等待初始化 等待连接wifi
	// 初始化结束后检查是不是因为唤醒重启，如果是，那有异常数据处理    放到这里的原因是message模块和mqtt初始化后才能发送数据
	if(Wake_Warm == 1)
	{
		// 获取时间戳（毫秒）
        long long ts_ms;
        time_t now = time(NULL);
        if (now > 1000000000LL) {
            ts_ms = (long long)now * 1000LL;
        } else {
            static time_t start_time = 0;
            if (start_time == 0) start_time = now;
            ts_ms = (long long)(1704067200LL + (now - start_time)) * 1000LL;
        }
		char json_data[512] = {0};
		char time_str[64] = {0};
		int current_risk = Calculate_Risk_Level(
                    final_hr, 
                    final_spo2,
                    Get_isFall());
		snprintf(time_str, sizeof(time_str), ",\"time\":%lld", ts_ms);
		snprintf(json_data, sizeof(json_data), 
                        "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                        "\"heart_rate\":{\"value\":%lu %s},"
                        "\"oxygen_saturation\":{\"value\":%lu %s},"
                        "\"seizure_risk_level\":{\"value\":%d %s}" 
                        "}}",
                        rand() % 1000,
                        final_hr, time_str,
                        final_spo2, time_str,
                        current_risk, time_str); // 发送计算出的风险等级
		esp_err_t err = MQTT_Publish(SENSOR_REPORT_TOPIC, json_data, 0);
		if(err!= ESP_OK) {
			ESP_LOGE("MainWake", "异常数据上报失败: %s", esp_err_to_name(err));
		}
	}
	else if(Wake_Warm == 2)
	{
		// 获取时间戳（毫秒）
        long long ts_ms;
        time_t now = time(NULL);
        if (now > 1000000000LL) {
            ts_ms = (long long)now * 1000LL;
        } else {
            static time_t start_time = 0;
            if (start_time == 0) start_time = now;
            ts_ms = (long long)(1704067200LL + (now - start_time)) * 1000LL;
        }
		char json_data[512] = {0};
		char time_str[64] = {0};
		snprintf(time_str, sizeof(time_str), ",\"time\":%lld", ts_ms);
		snprintf(json_data, sizeof(json_data),
                         "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                         "\"abnormal_motion_detected\":{\"value\":%s %s}"
                         "}}",
                         rand() % 10000,
                         "true",
                         time_str);
		esp_err_t err = MQTT_Publish(SENSOR_REPORT_TOPIC, json_data, 0);
		if(err!= ESP_OK) {
			ESP_LOGE("MainWake", "异常数据上报失败: %s", esp_err_to_name(err));
		}
	}

	vTaskDelay(pdMS_TO_TICKS(10000)); // 等待发送数据完成
	// 进入深度睡眠+ULP逻辑
	uint32_t received_cmd;
	while(1)
	{
		if(MQTT_Is_Connected())	//各种初始化成功后就进入深度睡眠+ULP
		{
			ESP_LOGI("MainSleep", "准备进入深度睡眠...");
			enter_deep_sleep_with_ulp(); // 进入深度睡眠的函数，封装了之前的逻辑
		}
		if(xTaskNotifyWait(0, 0xFFFFFFFF, &received_cmd, pdMS_TO_TICKS(100)) == pdPASS) 
		{
			if(received_cmd == KEY2_LONGPRESS_ENTER_SLEEP || received_cmd == LOW_POWER_ENTER_SLEEP)
			{
				ESP_LOGI("MainSleep", "准备进入深度睡眠...");

				enter_deep_sleep_with_ulp(); // 进入深度睡眠的函数，封装了之前的逻辑
			}
		}
		//vTaskDelay(pdMS_TO_TICKS(100)); // 避免死循环占用 CPU   不需要了，因为前面等待通知有阻塞了
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
    // 加载固件
    esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);

    // 配置 I2C
    ulp_riscv_i2c_cfg_t i2c_cfg = {
        ULP_RISCV_I2C_FAST_MODE_CONFIG()
    };
    i2c_cfg.i2c_pin_cfg.sda_io_num = GPIO_NUM_1;
    i2c_cfg.i2c_pin_cfg.scl_io_num = GPIO_NUM_2;
    i2c_cfg.i2c_pin_cfg.sda_pullup_en = true;
    i2c_cfg.i2c_pin_cfg.scl_pullup_en = true;
    i2c_cfg.i2c_timing_cfg.sda_duty_period = 1; 

    err = ulp_riscv_i2c_master_init(&i2c_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("ULP_INIT", "RTC I2C 配置失败: %s", esp_err_to_name(err));
    }

    err = ulp_riscv_run();
    ESP_ERROR_CHECK(err);
}

static void enter_deep_sleep_with_ulp(void)
{
    // 1. 关闭屏幕显示
    OLED_Notify_Show(false);
    OLED_Clear();
    OLED_Update();
    OLED_WriteCommand(0xAE);

    // 2. 关闭网络
    esp_mqtt_client_stop(MQTT_Give());
    esp_wifi_stop();
    esp_netif_deinit();

    // 3. 停止蜂鸣器
    buzzer_notify_off_from_key();

    // 4.先停止所有 I2C 任务，等待它们退出当前事务
	// 删除任务前先切换
	Max30102_Disable_Interrupts_For_ULP(); // 彻底关闭 MAX30102 的中断功能，确保 ULP 可以正常控制 GPIO 6 的状态
    if (Mpu6050_Task_Handle)  { vTaskDelete(Mpu6050_Task_Handle);  Mpu6050_Task_Handle  = NULL; }
    if (Max30102_Task_Handle) { vTaskDelete(Max30102_Task_Handle); Max30102_Task_Handle = NULL; }
    if (OLED_Task_Handle)     { vTaskDelete(OLED_Task_Handle);     OLED_Task_Handle     = NULL; }
    // 给传感器一点时间完成最后一帧传输
    vTaskDelay(pdMS_TO_TICKS(200)); 
	// 重置 GPIO 6 (INT引脚)，确保没有残留的中断触发逻辑
    gpio_isr_handler_remove(MAX30102_INT_GPIO);
    gpio_reset_pin(MAX30102_INT_GPIO);
	

    // 5. 任务全部退出后，再安全删除总线
    i2c_master_bus_handle_t bus = I2c_Get_Global_Bus_Handle();
    if (bus) {
        i2c_del_master_bus(bus);
    }

    // 6. 初始化 ULP (内部现在包含了硬件复位逻辑)
    ulp_wakeup_reason = 0;
    init_ulp_program();

    // 保持电源
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
    ESP_LOGE("MainSleep", "主 CPU 即将关闭...");
    vTaskDelay(pdMS_TO_TICKS(10)); // 给 LOG 输出一点时间
    esp_deep_sleep_start();
}