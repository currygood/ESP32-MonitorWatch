#ifndef __KEY_H__
#define __KEY_H__

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

/* ===================== 用户配置区 ===================== */
#define KEY_NUM           3
#define KEY_DEBOUNCE_MS   20    // 消抖
#define KEY_LONG_MS       800   // 长按（800ms后触发）
#define KEY_DOUBLE_MS     200   // 双击间隔（200ms内有效）

#define KEY_GPIO_1        GPIO_NUM_39
#define KEY_GPIO_2        GPIO_NUM_21
#define KEY_GPIO_3        GPIO_NUM_13
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
    KEY_3    = 3,
} key_id_t;

typedef struct {
    key_id_t id;
    key_event_t event;
} key_result_t;

typedef void (*key_callback_t)(key_id_t id, key_event_t event);

void Key_Init(key_callback_t cb);
key_result_t Key_Get(void);

#endif