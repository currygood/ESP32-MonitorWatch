/* 低电平有效有源蜂鸣器  通过改变输入电压调节音量  */
#include "Buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define BUZZER_TAG "Buzzer"

static int s_buzzer_gpio = -1;
static int s_buzzer_freq = 0;
static bool s_buzzer_on = false;

esp_err_t buzzer_init(int gpio_buzze_Pin,int freq_hz)
{
	s_buzzer_gpio = gpio_buzze_Pin;
	s_buzzer_freq = freq_hz;

	//初始化Buzzer GPIO
	gpio_config_t io_conf = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = (1ULL << s_buzzer_gpio),
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_DISABLE
	};
	gpio_config(&io_conf);

	s_buzzer_on = false;
	buzzer_off();
	return ESP_OK;
}

void buzzer_on(void)
{
	gpio_set_level(s_buzzer_gpio, 0); // 低电平有效  高电平关闭
	s_buzzer_on = true;
}

void buzzer_off(void)
{
	gpio_set_level(s_buzzer_gpio, 1); // 低电平有效  高电平关闭
	s_buzzer_on = false;
}

void buzzer_toggle(void)
{
	if (s_buzzer_on) {
		buzzer_off();
	} else {
		buzzer_on();
	}
}

void Task_Buzzer(void *pvParameters)
{
	while (1) 
	{
		
		vTaskDelay(pdMS_TO_TICKS(5000)); // 每5秒切换一次状态
	}
}
