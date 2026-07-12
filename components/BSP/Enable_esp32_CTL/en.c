#include "en.h"

void En_Init(void)
{
	gpio_config_t io_conf = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = (1ULL << EN_CTL_GPIO),
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_DISABLE
	};
	gpio_config(&io_conf);
}

void En_Set(gpio_num_t gpio_num, uint8_t level)
{
	gpio_set_level(gpio_num, level);
}

uint8_t En_Get(gpio_num_t gpio_num)
{
	return (uint8_t)gpio_get_level(gpio_num);
}