/*
  stm32_sw_i2c.h - Library for implementing I2C in SW on any GPIO on STM32 MCU.
  Created by Mawaba Pascal Dao, Dec 2, 2020.
  Released into the public domain.
*/
#ifndef I2C_SW_H
#define I2C_SW_H

#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
#include "ulp_riscv_gpio.h"

#include <stdbool.h>

#define ULP_I2C_SW_SDA_GPIO GPIO_NUM_1
#define ULP_I2C_SW_SCL_GPIO GPIO_NUM_2

#define I2C_CLEAR_SDA ulp_riscv_gpio_output_level(ULP_I2C_SW_SDA_GPIO, 0);
#define I2C_SET_SDA ulp_riscv_gpio_output_level(ULP_I2C_SW_SDA_GPIO, 1);
//#define I2C_READ_SDA {if (HAL_GPIO_ReadPin(SW_I2C_SDA_GPIO_Port, SW_I2C_SDA_Pin)) == GPIO_PIN_SET) return 1; else return 0; return 0;};
#define I2C_CLEAR_SCL ulp_riscv_gpio_output_level(ULP_I2C_SW_SCL_GPIO, 0);
#define I2C_SET_SCL ulp_riscv_gpio_output_level(ULP_I2C_SW_SCL_GPIO, 1);
#define I2C_DELAY ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_US * 10); // microsecond delay

//void I2C_bus_init(uint8_t scl_pin, uint8_t sda_pin, uint8_t port);

bool I2C_read_pin(gpio_num_t gpioPinToRead);

void I2C_init(void);

void I2C_start_cond(void);

void I2C_stop_cond(void);

void I2C_write_bit(uint8_t b);

uint8_t I2C_read_SDA(void);

// Reading a bit in I2C:
uint8_t I2C_read_bit(void);

_Bool I2C_write_byte(uint8_t B, _Bool start, _Bool stop);

uint8_t I2C_read_byte(_Bool ack, _Bool stop);

_Bool I2C_send_byte(uint8_t address, uint8_t data);

uint8_t I2C_receive_byte(uint8_t address);

_Bool I2C_send_byte_data(uint8_t address, uint8_t reg, uint8_t data);

uint8_t I2C_receive_byte_data(uint8_t address, uint8_t reg);

_Bool I2C_transmit(uint8_t address, uint8_t data[], uint8_t size);

_Bool I2C_receive(uint8_t address, uint8_t reg[], uint8_t *data, uint8_t reg_size, uint8_t size);

#endif /*__STM32_SW_I2C_H */