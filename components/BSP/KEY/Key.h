#ifndef __KEY_H__
#define __KEY_H__

#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* ===================== 用户配置区 ===================== */
#define KEY_NUM           2
#define KEY_DEBOUNCE_MS   20    // 消抖时间
#define KEY_LONG_MS       1000   // 长按判定时间 (ms)
#define KEY_DOUBLE_MS     500   // 双击间隔判定时间 (ms)

#define KEY_GPIO_1        GPIO_NUM_15
#define KEY_GPIO_2        GPIO_NUM_10
/* ===================================================== */

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SINGLE_CLICK, // 单击
    KEY_EVENT_DOUBLE_CLICK, // 双击
    KEY_EVENT_LONG_PRESS,   // 长按
} key_event_t;

typedef enum {
    KEY_NONE = 0,
    KEY_1    = 1,
    KEY_2    = 2,
} key_id_t;

// 组合结构体用于 Key_Get 返回
typedef struct {
    key_id_t id;
    key_event_t event;
} key_result_t;

/**
 * @brief 按键回调函数类型
 * @param id 按键ID
 * @param event 事件类型（单击/双击/长按）
 */
typedef void (*key_callback_t)(key_id_t id, key_event_t event);

void Key_Init(key_callback_t cb);
key_result_t Key_Get(void);

#endif