#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "max30102.h"

#define IR_BUF_LEN 500
#define RED_BUF_LEN 500

static uint32_t aun_ir_buffer[IR_BUF_LEN];
static uint32_t aun_red_buffer[RED_BUF_LEN];
static int32_t n_spo2;
static int8_t ch_spo2_valid;
static int32_t n_heart_rate;
static int8_t ch_hr_valid;

static void max30102_task(void *pvParameters) {
	// 禁用 GPIO 和 I2C 的调试日志
	esp_log_level_set("gpio", ESP_LOG_ERROR);
	esp_log_level_set("i2c", ESP_LOG_ERROR);
	
	uint8_t temp[6];
	int32_t n_ir_buffer_length = IR_BUF_LEN;
	int32_t un_min = UINT32_MAX, un_max = 0;
	uint8_t fifo_wp, fifo_rp;
	int samples_read;
	int i;
	
	ESP_LOGI("MAX30102", "Task started");
	
	// 1. 初始化 I2C、GPIO、ISR
	max30102_init();
	vTaskDelay(pdMS_TO_TICKS(100));
	
	// 测试 I2C 通信
	uint8_t part_id = 0;
	esp_err_t ret = max30102_read_reg(0xFF, &part_id);
	if (ret == ESP_OK) {
		ESP_LOGI("MAX30102", "Part ID: 0x%02X", part_id);
	} else {
		ESP_LOGE("MAX30102", "I2C read failed: %d", ret);
		vTaskDelete(NULL);
		return;
	}
	
	// 检查中断状态和配置
	uint8_t intr_status, intr_en;
	max30102_read_reg(0x00, &intr_status);  // INTR_STATUS_1
	max30102_read_reg(0x02, &intr_en);      // INTR_ENABLE_1
	max30102_read_reg(0x04, &fifo_wp);      // FIFO_WR_PTR
	max30102_read_reg(0x06, &fifo_rp);      // FIFO_RD_PTR
	ESP_LOGI("MAX30102", "INTR_STATUS: 0x%02X, INTR_EN: 0x%02X, FIFO_WP: %d, FIFO_RP: %d", 
		intr_status, intr_en, fifo_wp, fifo_rp);
	
	max30102_gpio_isr_init(xTaskGetCurrentTaskHandle());
	vTaskDelay(pdMS_TO_TICKS(100));
	
	// 再读一次 FIFO 指针，看是否有数据产生
	max30102_read_reg(0x04, &fifo_wp);
	max30102_read_reg(0x06, &fifo_rp);
	ESP_LOGI("MAX30102", "After ISR init - FIFO_WP: %d, FIFO_RP: %d", fifo_wp, fifo_rp);

	// 2. 读取前500个样本（改为轮询模式）
	samples_read = 0;
	ESP_LOGI("MAX30102", "Starting sample collection...");
	
	while (samples_read < n_ir_buffer_length) {
		max30102_read_reg(0x04, &fifo_wp);  // FIFO_WR_PTR
		max30102_read_reg(0x06, &fifo_rp);  // FIFO_RD_PTR
		
		if (fifo_wp != fifo_rp) {
			esp_err_t ret = max30102_read_fifo(temp, 6);
			if (ret != ESP_OK) {
				continue;
			}
			
			aun_red_buffer[samples_read] = ((temp[0] & 0x03) << 16) | (temp[1] << 8) | temp[2];
			aun_ir_buffer[samples_read] = ((temp[3] & 0x03) << 16) | (temp[4] << 8) | temp[5];
			if (un_min > aun_red_buffer[samples_read]) un_min = aun_red_buffer[samples_read];
			if (un_max < aun_red_buffer[samples_read]) un_max = aun_red_buffer[samples_read];
			
			samples_read++;
			
			// 每读 100 个样本打印进度
			if (samples_read % 100 == 0) {
				ESP_LOGI("MAX30102", "Collected %d samples", samples_read);
			}
		} else {
			vTaskDelay(pdMS_TO_TICKS(5));  // 没有数据时，稍微延迟后再读
		}
	}
	
	ESP_LOGI("MAX30102", "Sample collection complete!");

	// 3. 算法计算（先注释掉测试）
	// max30102_algorithm_calculate(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_spo2, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid);
	
	ESP_LOGI("MAX30102", "Ready for heartbeat monitoring...");

	while (1) {
		// 移位缓存
		for (i = 100; i < 500; i++) {
			aun_red_buffer[i - 100] = aun_red_buffer[i];
			aun_ir_buffer[i - 100] = aun_ir_buffer[i];
			if (un_min > aun_red_buffer[i]) un_min = aun_red_buffer[i];
			if (un_max < aun_red_buffer[i]) un_max = aun_red_buffer[i];
		}
		
		// 采集新样本（轮询模式，加看门狗喂食）
		samples_read = 400;
		while (samples_read < 500) {
			vTaskDelay(pdMS_TO_TICKS(1));  // 喂狗 + 让出CPU
			
			max30102_read_reg(0x04, &fifo_wp);
			max30102_read_reg(0x06, &fifo_rp);
			
			if (fifo_wp != fifo_rp) {
				if (max30102_read_fifo(temp, 6) == ESP_OK) {
					aun_red_buffer[samples_read] = ((temp[0] & 0x03) << 16) | (temp[1] << 8) | temp[2];
					aun_ir_buffer[samples_read] = ((temp[3] & 0x03) << 16) | (temp[4] << 8) | temp[5];
					samples_read++;
				}
			}
		}
		
		// 调用算法计算
		max30102_algorithm_calculate(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_spo2, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid);
		
		if (ch_hr_valid == 1 && n_heart_rate < 120 && ch_spo2_valid == 1 && n_spo2 < 101) {
			ESP_LOGI("**********MAX30102", "HR=%ld; SpO2=%ld%%", (long)n_heart_rate, (long)n_spo2);
		}
		
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

void app_main(void) {
	xTaskCreate(max30102_task, "max30102_task", 4096, NULL, 5, NULL);
}
