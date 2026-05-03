#ifndef __BUZZER_H__
#define __BUZZER_H__

#include "esp_err.h"
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 硬件配置 ===================== */
#define BUZZER_GPIO_NUM     4       // 蜂鸣器连接的GPIO引脚
#define BUZZER_FREQ_HZ      2000    // 建议频率（注：当前GPIO版本不使用PWM，仅作保留）

/**
 * @brief 初始化蜂鸣器模块
 * @param gpio_buzze_Pin 蜂鸣器引脚编号
 * @param freq_hz 频率（保留参数）
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t buzzer_init(int gpio_buzze_Pin, int freq_hz);

/**
 * @brief 蜂鸣器核心处理任务
 * @note  应在 main.c 中通过 xTaskCreatePinnedToCore 创建
 */
void Task_Buzzer(void *pvParameters);

/* ===================== 逻辑控制接口 ===================== */

/**
 * @brief 触发或关闭报警 (由传感器任务调用)
 * 
 * @param enable true: 如果当前未静音且未在响，则启动报警（含15秒自动停止逻辑）
 *               false: 关闭报警，并重置静音锁定状态
 */
void Buzzer_Trigger_Alarm(bool enable);

/**
 * @brief 设置静音锁定状态 (由按键处理任务调用)
 * 
 * @param mute true: 立即停止当前响声，并锁定蜂鸣器，防止传感器再次触发
 *             false: 解除锁定
 */
void Buzzer_Set_Mute(bool mute);

/* ===================== 状态查询接口 ===================== */

/**
 * @brief 获取报警是否已完成/停止
 * @return true: 当前没有响声
 *         false: 正在执行报警循环
 */
bool Buzzer_Get_Finish(void);

/**
 * @brief 获取当前是否处于人工静音锁定状态
 */
bool Buzzer_Get_Mute(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUZZER_H__ */