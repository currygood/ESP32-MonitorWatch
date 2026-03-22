#ifndef MAX30102_H
#define MAX30102_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "i2c_driver.h"
#include "MessageQueue.h"

// --- 硬件映射 ---
#define MAX30102_INT_GPIO 6
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
#define IR_BUF_LEN               500
#define RED_BUF_LEN              500

// 心率预警相关配置
#define HEART_RATE_BASELINE_SAMPLES     30    // 基准心率计算样本数
#define HEART_RATE_WARNING_THRESHOLD_LOW 20    // 预警阈值下限（比基准高20）
#define HEART_RATE_WARNING_THRESHOLD_HIGH 30   // 预警阈值上限（比基准高30）
#define HEART_RATE_STABLE_COUNT          10    // 心率稳定连续次数
#define HEART_RATE_MIN_VALID             40    // 最小有效心率
#define HEART_RATE_MAX_VALID             180   // 最大有效心率

// --- API声明 ---
void Max30102_Init(i2c_master_bus_handle_t bus_handle);
void Max30102_Gpio_Isr_Init(TaskHandle_t task_handle);
esp_err_t Max30102_Write_Reg(uint8_t reg, uint8_t data);
esp_err_t Max30102_Read_Reg(uint8_t reg, uint8_t *data);
esp_err_t Max30102_Read_Fifo(uint8_t *buffer, uint8_t count);
uint8_t Max30102_Can_Read(void);
void Max30102_Clear_Flag(void);

void Max30102_Algorithm_Calculate(uint32_t *ir_buffer, int32_t buffer_len, uint32_t *red_buffer,
                                  int32_t *spo2, int8_t *spo2_valid,
                                  int32_t *heart_rate, int8_t *hr_valid);

// --- 监测任务 ---
void Task_Max30102_Monitor(void *pvParameters);


// --- 数据输出 ---
void Max30102_Send_Waveform_Data(void);
void Max30102_Send_JSON_Data(void);
uint32_t Max301020_Get_Heart_Rate(void);
uint32_t Max30102_Get_Spo2(void);

// --- 心率预警功能 ---
void Max30102_Heart_Rate_Warning_Init(void);
void Max30102_Update_Heart_Rate_Baseline(uint32_t current_hr);
bool Max30102_Check_Heart_Rate_Warning(uint32_t current_hr);
uint32_t Max30102_Get_Heart_Rate_Baseline(void);
uint32_t Max30102_Get_Heart_Rate_Warning_Threshold(void);
bool Max30102_Is_Heart_Rate_Warning_Active(void);
void Max30102_Reset_Heart_Rate_Warning(void);

#endif // MAX30102_H