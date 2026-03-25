#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>
#include "OLED_Data.h"
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "i2c_driver.h"
#include "MessageQueue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <time.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_sntp.h"


/*参数宏定义*********************/

// OLED设备地址定义
#define OLED_ADDR 0x3C

/*FontSize参数取值*/
#define OLED_8X16       8
#define OLED_6X8        6
#define OLED_12X24      12

/*IsFilled参数数值*/
#define OLED_UNFILLED   0
#define OLED_FILLED     1

/*********************参数宏定义*/


/*初始化函数*/

/**
 * OLED_Init - 初始化 OLED 屏幕
 *
 * 调用前需已完成 I2C 总线初始化，并通过 I2c_Add_Device() 添加 OLED 设备，
 * 将设备句柄传入本函数。
 *
 * @param dev_handle  由 I2c_Add_Device() 返回的 I2C 设备句柄
 */
esp_err_t OLED_Init(i2c_master_bus_handle_t bus_handle);

/*更新函数*/
void OLED_Update(void);
void OLED_UpdateArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

/*显存控制函数*/
void OLED_Clear(void);
void OLED_ClearArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);
void OLED_Reverse(void);
void OLED_ReverseArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

/*显示函数*/
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize);
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize);
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize);
void OLED_ShowImage(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image);
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...);

/*绘图函数*/
void OLED_DrawPoint(int16_t X, int16_t Y);
uint8_t OLED_GetPoint(int16_t X, int16_t Y);
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1);
void OLED_DrawRectangle(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled);
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled);
void OLED_DrawCircle(int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled);
void OLED_DrawEllipse(int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled);
void OLED_DrawArc(int16_t X, int16_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled);


//freertos 任务函数
void Task_OLED_Show(void *pvParameters);



#endif