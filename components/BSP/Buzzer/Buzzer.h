#ifndef __BUZZER_H__
#define __BUZZER_H__

#include "esp_err.h"
#include "driver/ledc.h"  // 修复LEDC_CHANNEL_0未声明

#ifdef __cplusplus
extern "C" {
#endif

#define BUZZER_GPIO_NUM     11
#define BUZZER_FREQ_HZ      2000

/**
 * @brief 初始化蜂鸣器PWM
 * @param gpio_buzze_Pin 蜂鸣器引脚连接的GPIO编号
 * @param freq_hz PWM频率(建议2~5kHz)
 * @return esp_err_t
 */
esp_err_t buzzer_init(int gpio_buzze_Pin,int freq_hz);


/**
 * @brief 打开蜂鸣器
 */
void buzzer_on(void);

/**
 * @brief 关闭蜂鸣器
 */
void buzzer_off(void);

/**
 * @brief 切换蜂鸣器状态(开/关)
 * @brief 切换蜂鸣器状态(开/关)
 */
void buzzer_toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUZZER_H__ */