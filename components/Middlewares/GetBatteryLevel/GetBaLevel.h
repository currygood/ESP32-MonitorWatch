#ifndef GET_BATTERY_LEVEL_H
#define GET_BATTERY_LEVEL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// --- 电池电量检测配置宏 ---
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_7     // ADC通道7对应GPIO8
#define BATTERY_ADC_PIN         8                // 电池检测引脚
#define BATTERY_ADC_UNIT        ADC_UNIT_1        // ADC单元1
#define BATTERY_ADC_WIDTH       ADC_WIDTH_BIT_12  // 12位分辨率
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_11   // 11dB衰减（0-3.3V范围）

// --- 电压转换参数 ---
#define ADC_MAX_VALUE           4095             // 12位ADC最大值
#define REFERENCE_VOLTAGE       3.3f             // 参考电压3.3V
#define VOLTAGE_DIVIDER_RATIO    1.0f             // 电压分压比（无分压时为1.0）

// --- 电池电压范围（典型锂电池） ---  通过分压计算的大概结果
#define BATTERY_FULL_VOLTAGE 2.0f
#define BATTERY_EMPTY_VOLTAGE 1.5f

// --- 全局变量声明 ---
extern bool Battery_Level_Initialized;
extern uint8_t Battery_Level_Percent;
extern float Battery_Voltage;

// --- 函数声明 ---

/**
 * @brief 初始化电池电量检测模块
 * @return esp_err_t 初始化结果
 */
esp_err_t Battery_Level_Init(void);

/**
 * @brief 读取电池电压（原始ADC值）
 * @param adc_value 输出的ADC原始值
 * @return esp_err_t 读取结果
 */
uint32_t Battery_Read_Adc_Value();

/**
 * @brief 读取电池电压（转换为电压值）
 * @param voltage 输出的电压值（单位：V）
 * @return esp_err_t 读取结果
 */
esp_err_t Battery_Read_Voltage(float *voltage);

/**
 * @brief 计算电池电量百分比
 * @param voltage 电池电压
 * @return uint8_t 电量百分比（0-100）
 */
uint8_t Battery_Calculate_Percentage_Test33V(float voltage);

/**
 * @brief 计算电池电量百分比
 * @param voltage 电池电压
 * @return uint8_t 电量百分比（0-100）
 */
uint8_t Battery_Calculate_Percentage(float voltage);

/**
 * @brief 获取当前电池电量百分比
 * @return uint8_t 电量百分比（0-100）
 */
uint8_t Battery_Get_Level(void);

/**
 * @brief 获取当前电池电压
 * @return float 电池电压（单位：V）
 */
float Battery_Get_Voltage(void);

#endif // GET_BATTERY_LEVEL_H