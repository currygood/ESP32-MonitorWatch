#ifndef __KEY_H__
#define __KEY_H__

#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* ===================== 用户配置区 ===================== */
#define KEY_NUM           1
#define KEY_DEBOUNCE_MS   20

#define KEY_GPIO_1        GPIO_NUM_15
/* ===================================================== */

/**
 * @brief 按键 ID 枚举
 */
typedef enum {
    KEY_NONE = 0,
    KEY_1    = 1,
    KEY_2    = 2,
} key_id_t;

/**
 * @brief 按键回调函数类型（事件驱动模式使用）
 *        在 FreeRTOS 定时器任务上下文中调用，不可阻塞
 */
typedef void (*key_callback_t)(key_id_t key);

/**
 * @brief 初始化按键模块
 * @param cb 按键按下时的回调，传 NULL 则使用轮询模式（Key_Get）
 */
void Key_Init(key_callback_t cb);

/**
 * @brief 轮询获取一次按键事件（非阻塞）
 * @return KEY_NONE 无事件；KEY_1 / KEY_2 对应按键被按下
 * @note   仅在 cb=NULL（轮询模式）时有效
 */
key_id_t Key_Get(void);

#endif /* __KEY_H__ */