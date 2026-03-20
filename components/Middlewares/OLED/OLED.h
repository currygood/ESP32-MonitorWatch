#ifndef __OLED_H
#define __OLED_H
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "OLED_Font.h"
#include "i2c_driver.h"

// OLED设备地址定义
#define OLED_ADDR 0x3C

// OLED_Write宏定义，封装I2C模块的发送函数
#define OLED_Write(dev_handle, data, len) i2c_master_transmit(dev_handle, data, len, -1)

// 全局OLED设备句柄声明
extern i2c_master_dev_handle_t oled_dev;

// OLED初始化函数，使用共享I2C总线
esp_err_t OLED_Init(i2c_master_bus_handle_t bus_handle);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

//OLED写入命令函数
void OLED_WriteCommand(uint8_t Command);
//OLED写入数据函数
void OLED_WriteData(uint8_t Data);

// OLED设备句柄获取函数
i2c_master_dev_handle_t OLED_Get_Device_Handle(void);

#endif