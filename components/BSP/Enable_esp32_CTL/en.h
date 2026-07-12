#ifndef __EN_H__
#define __EN_H__

#include "esp_err.h"
#include "driver/gpio.h"

#define EN_CTL_GPIO GPIO_NUM_8  // 使能引脚GPIO8

void En_Init(void);
void En_Set(gpio_num_t gpio_num, uint8_t level);
uint8_t En_Get(gpio_num_t gpio_num);

#endif
