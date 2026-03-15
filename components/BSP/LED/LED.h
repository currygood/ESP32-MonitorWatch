#ifndef __LED_H
#define __LED_H

#include "driver/gpio.h"

#define LED_GPIO_PIN_1 GPIO_NUM_1

enum GPIO_OUTPUT_LEVEL
{
	LOW = 0,
	HIGH = 1
};

static int led_state = 0;

#define LED(X) do{X ? \
	gpio_set_level(LED_GPIO_PIN_1, HIGH) : \
	gpio_set_level(LED_GPIO_PIN_1, LOW);} while(0)


void LED_TOGGLE(void);
void LED_Init(void);

#endif
