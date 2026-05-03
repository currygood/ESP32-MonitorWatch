#ifndef __KEY_H__
#define __KEY_H__

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

/* ===================== 用户配置区 ===================== */
#define KEY_NUM           2
#define KEY_DEBOUNCE_MS   20    // 消抖
#define KEY_LONG_MS       800   // 长按（800ms后触发）
#define KEY_DOUBLE_MS     200   // 双击间隔（200ms内有效）

#define KEY_GPIO_1        GPIO_NUM_15
#define KEY_GPIO_2        GPIO_NUM_9
/* ===================================================== */

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SINGLE_CLICK,
    KEY_EVENT_DOUBLE_CLICK,
    KEY_EVENT_LONG_PRESS,
} key_event_t;

typedef enum {
    KEY_NONE = 0,
    KEY_1    = 1,
    KEY_2    = 2,
} key_id_t;

typedef struct {
    key_id_t id;
    key_event_t event;
} key_result_t;

/**
 * @brief 初始化按键模块
 */
void Key_Init(void);

/**
 * @brief 从队列获取按键事件（多任务安全，不丢失）
 * @param res 结果存储指针
 * @param wait_ms 等待超时。0则立即返回，portMAX_DELAY则永久等待
 */
bool Key_Get_Event(key_result_t *res, uint32_t wait_ms);

#endif