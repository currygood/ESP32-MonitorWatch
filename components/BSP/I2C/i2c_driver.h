#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// I2C 总线句柄
typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
} i2c_handle_t;

// 初始化 I2C 总线
esp_err_t i2c_init_bus(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint32_t freq_hz, i2c_master_bus_handle_t *bus_handle);

// 添加 I2C 设备
esp_err_t i2c_add_device(i2c_master_bus_handle_t bus_handle, uint16_t dev_addr, uint32_t freq_hz, i2c_master_dev_handle_t *dev_handle);

// 写寄存器
esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t data);

// 读寄存器
esp_err_t i2c_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data);

// 读 FIFO 或多字节
esp_err_t i2c_read_bytes(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *buffer, size_t count);

// 删除设备
esp_err_t i2c_remove_device(i2c_master_dev_handle_t dev_handle);

// 删除总线
esp_err_t i2c_delete_bus(i2c_master_bus_handle_t bus_handle);

#endif // I2C_DRIVER_H