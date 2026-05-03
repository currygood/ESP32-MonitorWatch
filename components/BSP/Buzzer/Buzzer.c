#include "Buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

static const char* TAG = "Buzzer";
static int s_buzzer_gpio = -1;
static bool s_is_muted = false;      // 按键按下的静音锁定
static bool s_is_running = false;    // 当前是否正在响
static TaskHandle_t s_buzzer_task_handle = NULL;

// 硬件底层
static void buzzer_hw_on() { if (s_buzzer_gpio != -1) gpio_set_level(s_buzzer_gpio, 0); }
static void buzzer_hw_off() { if (s_buzzer_gpio != -1) gpio_set_level(s_buzzer_gpio, 1); }

esp_err_t buzzer_init(int gpio_buzze_Pin, int freq_hz) {
    s_buzzer_gpio = gpio_buzze_Pin;
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << s_buzzer_gpio), .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&io_conf);
    buzzer_hw_off();
    return ESP_OK;
}

// 供传感器调用的接口
void Buzzer_Trigger_Alarm(bool enable) {
    if (s_buzzer_task_handle == NULL) return;
    if (enable) {
        // 只有没被静音 且 当前没在响，才启动
        if (!s_is_muted && !s_is_running) {
            xTaskNotify(s_buzzer_task_handle, 1, eSetValueWithOverwrite);
        }
    } else {
        // 如果传感器指示恢复正常，解除静音锁定，允许下一次异常时报警
        s_is_muted = false;
        // 如果正在响，就停掉
        if (s_is_running) xTaskNotify(s_buzzer_task_handle, 0, eSetValueWithOverwrite);
    }
}

// 供按键调用的接口：立刻闭嘴并进入静音锁定
void Buzzer_Set_Mute(bool mute) {
    s_is_muted = mute;
    if (mute && s_is_running) {
        xTaskNotify(s_buzzer_task_handle, 0, eSetValueWithOverwrite);
    }
}

// 获取状态（兼容你原来的逻辑）
bool Buzzer_Get_Finish(void) { return !s_is_running; }

void Task_Buzzer(void *pvParameters) {
    s_buzzer_task_handle = xTaskGetCurrentTaskHandle();
    uint32_t notifyValue;

    while (1) {
        // 1. 阻塞等待“开始报警”指令 (notifyValue == 1)
        if (xTaskNotifyWait(0, ULONG_MAX, &notifyValue, portMAX_DELAY) == pdTRUE) {
            
            if (notifyValue == 1) { 
                s_is_running = true;
                buzzer_hw_on();  // 开启蜂鸣器，长鸣
                ESP_LOGI(TAG, "报警启动：长鸣模式 (最多15秒)");

                // 2. 关键点：等待“停止信号”(0)，但只等 15 秒
                // 如果 15 秒内收到了信号（如按键按下的 0），函数会立刻返回 pdTRUE
                // 如果 15 秒内啥也没收到，函数会因为超时返回 pdFALSE
                // 无论哪种情况，都会执行下面的 hw_off
                xTaskNotifyWait(0, ULONG_MAX, &notifyValue, pdMS_TO_TICKS(15000));

                buzzer_hw_off(); // 停止响声
                s_is_running = false;
                ESP_LOGI(TAG, "报警结束");
                
                // 3. 强行清除一次可能存在的残留通知，防止连续触发
                xTaskNotifyStateClear(NULL);
            }
        }
    }
}