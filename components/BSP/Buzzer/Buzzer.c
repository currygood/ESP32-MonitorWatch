#include "Buzzer.h"
#include "driver/gpio.h"
#include "freertos/timers.h"

#define BUZZER_ON_LEVEL 1
#define BUZZER_OFF_LEVEL 0

static int s_buzzer_gpio = -1;
static bool s_can_enable = true;      // 是否允许开启（冷却标志）
static TimerHandle_t s_lockout_timer = NULL;

// 定义内部使用的指令（增加一个自动结束指令）
typedef enum {
    CMD_ON = 1,          // 传感器触发：开启
    CMD_OFF_MANUAL = 2,  // 按键触发：手动关闭
    CMD_OFF_AUTO = 3     // 定时器触发：15秒到期自动关闭
} buzzer_internal_msg_t;

// 定时器回调函数：15秒时间到
static void lockout_timer_callback(TimerHandle_t timer) {
    // 15秒到了，向任务发送“自动关闭”通知
    if (Buzzer_Task_Handle != NULL) {
        xTaskNotify(Buzzer_Task_Handle, CMD_OFF_AUTO, eSetValueWithOverwrite);
    }
}

esp_err_t buzzer_init(int gpio_buzze_Pin, int freq_hz) {
    s_buzzer_gpio = gpio_buzze_Pin;
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << s_buzzer_gpio),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf);
    
    gpio_set_level(s_buzzer_gpio, BUZZER_OFF_LEVEL); // 默认关闭

    // 创建单次定时器（15秒）
    s_lockout_timer = xTimerCreate("BuzzerLock", pdMS_TO_TICKS(BUZZER_ON_LOCKOUT_MS), 
                                   pdFALSE, NULL, lockout_timer_callback);
    return ESP_OK;
}

// 供传感器调用的通知
void buzzer_notify_on_from_sensor(void) {
    if (Buzzer_Task_Handle != NULL) {
        xTaskNotify(Buzzer_Task_Handle, CMD_ON, eSetValueWithOverwrite);
    }
}

// 供按键调用的通知
void buzzer_notify_off_from_key(void) {
    if (Buzzer_Task_Handle != NULL) {
        xTaskNotify(Buzzer_Task_Handle, CMD_OFF_MANUAL, eSetValueWithOverwrite);
    }
}

// 蜂鸣器主任务
void Task_Buzzer(void *pvParameters) 
{
    uint32_t received_cmd;
	
    while (1) 
	{
        // 等待通知，portMAX_DELAY 表示一直等待直到有消息
        // 【注意】这里不要加 vTaskDelay
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &received_cmd, portMAX_DELAY) == pdPASS) 
		{
            
            switch (received_cmd) 
			{
                case CMD_ON:
                    if (s_can_enable) {
                        gpio_set_level(s_buzzer_gpio, BUZZER_ON_LEVEL); // 响
                        s_can_enable = false;             // 进入冷却锁定
                        xTimerStart(s_lockout_timer, 0);  // 开启15秒计时
					}
                	// ESP_LOGW(TAG, ">>> 报警开启，锁定15秒");
                    //else {
                    //     ESP_LOGI(TAG, "冷却中，忽略开启指令");
                    // }
                    break;

                case CMD_OFF_MANUAL:
                    gpio_set_level(s_buzzer_gpio, BUZZER_OFF_LEVEL); // 关
                    // ESP_LOGI(TAG, ">>> 按键手动关闭 (冷却继续有效)");
                    // 注意：这里不需要停止定时器，s_can_enable 保持 false 直到定时器回调
                    break;

                case CMD_OFF_AUTO:
                    gpio_set_level(s_buzzer_gpio, BUZZER_OFF_LEVEL); // 关
                    s_can_enable = true;              // 15秒到，解锁冷却
                    // ESP_LOGI(TAG, ">>> 15秒到期：自动关闭并解除锁定");
                    break;

                default:
                    break;
            }
        }
    }
<<<<<<< HEAD
}
=======
}
>>>>>>> e085dd42cab267ed7dfa903d4539851e3a370c88
