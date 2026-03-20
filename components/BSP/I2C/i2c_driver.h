#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// --- I2C硬件配置宏 ---
#define I2C_PORT I2C_NUM_0
#define I2C_SDA_GPIO 20
#define I2C_SCL_GPIO 21
#define I2C_FREQ 400000

// I2C 总线句柄
typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
} i2c_handle_t;

// 初始化 I2C 总线
esp_err_t I2c_Init_Bus(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint32_t freq_hz, i2c_master_bus_handle_t *bus_handle);

// 添加 I2C 设备
esp_err_t I2c_Add_Device(i2c_master_bus_handle_t bus_handle, uint16_t dev_addr, uint32_t freq_hz, i2c_master_dev_handle_t *dev_handle);

// 写寄存器
esp_err_t I2c_Write_Reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t data);

// 读寄存器
esp_err_t I2c_Read_Reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data);

// 读 FIFO 或多字节
esp_err_t I2c_Read_Bytes(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *buffer, size_t count);

// 删除设备
esp_err_t I2c_Remove_Device(i2c_master_dev_handle_t dev_handle);

// 删除总线
esp_err_t I2c_Delete_Bus(i2c_master_bus_handle_t bus_handle);

// 全局I2C总线句柄获取函数
i2c_master_bus_handle_t I2c_Get_Global_Bus_Handle(void);

#endif // I2C_DRIVER_H