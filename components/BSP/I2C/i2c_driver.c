#include "i2c_driver.h"
#include "esp_log.h"

// 初始化 I2C 总线
esp_err_t i2c_init_bus(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint32_t freq_hz, i2c_master_bus_handle_t *bus_handle) {
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = port,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .flags = {
            .enable_internal_pullup = true,
        }
    };
    return i2c_new_master_bus(&bus_cfg, bus_handle);
}

// 添加 I2C 设备
esp_err_t i2c_add_device(i2c_master_bus_handle_t bus_handle, uint16_t dev_addr, uint32_t freq_hz, i2c_master_dev_handle_t *dev_handle) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = freq_hz,
    };
    return i2c_master_bus_add_device(bus_handle, &dev_cfg, dev_handle);
}

// 写寄存器
esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    return i2c_master_transmit(dev_handle, buf, sizeof(buf), -1);
}

// 读寄存器
esp_err_t i2c_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data) {
    return i2c_master_transmit_receive(dev_handle, &reg, 1, data, 1, -1);
}

// 读多字节
esp_err_t i2c_read_bytes(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *buffer, size_t count) {
    return i2c_master_transmit_receive(dev_handle, &reg, 1, buffer, count, -1);
}

// 删除设备
esp_err_t i2c_remove_device(i2c_master_dev_handle_t dev_handle) {
    return i2c_master_bus_rm_device(dev_handle);
}

// 删除总线
esp_err_t i2c_delete_bus(i2c_master_bus_handle_t bus_handle) {
    return i2c_del_master_bus(bus_handle);
}