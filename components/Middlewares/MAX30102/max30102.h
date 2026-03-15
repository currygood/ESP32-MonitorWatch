#ifndef MAX30102_H
#define MAX30102_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// --- 硬件映射 ---
#define MAX30102_I2C_PORT I2C_NUM_0
#define MAX30102_I2C_SDA_GPIO 4
#define MAX30102_I2C_SCL_GPIO 5
#define MAX30102_INT_GPIO 6
#define MAX30102_I2C_FREQ 400000

#define MAX30102_ADDR 0x57

// --- 常用寄存器 ---
#define REG_INTR_STATUS_1   0x00
#define REG_INTR_STATUS_2   0x01
#define REG_INTR_ENABLE_1   0x02
#define REG_INTR_ENABLE_2   0x03
#define REG_FIFO_WR_PTR     0x04
#define REG_OVF_COUNTER     0x05
#define REG_FIFO_RD_PTR     0x06
#define REG_FIFO_DATA       0x07
#define REG_FIFO_CONFIG     0x08
#define REG_MODE_CONFIG     0x09
#define REG_SPO2_CONFIG     0x0A
#define REG_LED1_PA         0x0C
#define REG_LED2_PA         0x0D
#define REG_PILOT_PA        0x10
#define REG_PART_ID         0xFF

// 算法相关宏
#define MAX30102_BUFFER_SIZE     500
#define MAX30102_MA4_SIZE        4
#define MAX30102_HAMMING_SIZE    5

// --- API声明 ---
void max30102_init(void);
void max30102_gpio_isr_init(TaskHandle_t task_handle);
esp_err_t max30102_write_reg(uint8_t reg, uint8_t data);
esp_err_t max30102_read_reg(uint8_t reg, uint8_t *data);
esp_err_t max30102_read_fifo(uint8_t *buffer, uint8_t count);
uint8_t max30102_can_read(void);
static inline void max30102_clear_flag(void);

void max30102_algorithm_calculate(uint32_t *ir_buffer, int32_t buffer_len, uint32_t *red_buffer,
                                  int32_t *spo2, int8_t *spo2_valid,
                                  int32_t *heart_rate, int8_t *hr_valid);

#endif // MAX30102_H
