#ifndef __BUZZER_H__
#define __BUZZER_H__

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUZZER_GPIO_NUM       5
#define BUZZER_FREQ_HZ        2000
#define BUZZER_ON_LOCKOUT_MS  15000  // 15秒冷却时间

// 定义蜂鸣器任务接收的指令
typedef enum {
    BUZZER_CMD_ON = 1,
    BUZZER_CMD_OFF = 2
} buzzer_cmd_t;

// 声明导出给 main.c 使用的任务句柄（在 main.c 中定义）
extern TaskHandle_t Buzzer_Task_Handle;

esp_err_t buzzer_init(int gpio_buzze_Pin, int freq_hz);

// 修改后的通知函数：直接发送任务通知
void buzzer_notify_on_from_sensor(void); // 传感器统一调用这个
void buzzer_notify_off_from_key(void);   // 按键调用这个

void Task_Buzzer(void *pvParameters);

#ifdef __cplusplus
}
#endif
#endif