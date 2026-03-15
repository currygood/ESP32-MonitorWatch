#include "LED.h"

void LED_Init(void)
{
	gpio_config_t gpio_init_struct ={0};
	gpio_init_struct.pin_bit_mask = BIT(LED_GPIO_PIN_1);
	gpio_init_struct.mode = GPIO_MODE_OUTPUT;
	gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
	gpio_config(&gpio_init_struct);
	LED(LOW);
}

void LED_TOGGLE(void)
{
    led_state = !led_state;
    gpio_set_level(LED_GPIO_PIN_1, led_state);
}