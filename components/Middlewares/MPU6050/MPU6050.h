#ifndef MPU6050_H
#define MPU6050_H

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
#define MPU6050_INT_GPIO      -1
#define MPU6050_I2C_FREQ      400000
#define MPU6050_ADDR          0x68

// --- 寄存器 ---
#define MPU6050_REG_WHO_AM_I     0x75
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B

// 算法宏
#define MPU6050_BUFFER_SIZE      110
#define MPU6050_SAMPLES_PER_SEC  50

// --- API 声明 ---
void Mpu6050_Init(void);

esp_err_t Mpu6050_Write_Reg(uint8_t reg, uint8_t data);
esp_err_t Mpu6050_Read_Reg(uint8_t reg, uint8_t *data);
esp_err_t Mpu6050_Read_Raw(int16_t *ax, int16_t *ay, int16_t *az,
                           int16_t *gx, int16_t *gy, int16_t *gz);

// 检测摔倒或 convulsion	
bool Mpu6050_Detect_Fall_Or_Convulsion(int16_t *ax_buf, int16_t *ay_buf, int16_t *az_buf, int len);
bool Get_isFall(void);

// --- 标志位管理函数 ---
bool Mpu6050_Can_Read(void);
void Mpu6050_Clear_Flag(void);

// --- 数据获取函数 ---
void Mpu6050_Get_Accel_Data(int16_t *ax, int16_t *ay, int16_t *az);
void Mpu6050_Get_Gyro_Data(int16_t *gx, int16_t *gy, int16_t *gz);

void Task_Mpu6050_Monitor(void *pvParameters);

#endif // MPU6050_H