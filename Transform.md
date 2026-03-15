# STM32 to ESP32-S3 Migration Guide (MAX30102)

## 1. Hardware Mapping
- **MCU:** ESP32-S3
- **I2C SDA:** GPIO 4 (SDA)
- **I2C SCL:** GPIO 5 (SCL)
- **MAX30102 INT:** GPIO 6 (Input, Pull-up, Falling edge interrupt)
- **I2C Speed:** 400kHz (Fast Mode)
- **I2C Port:** I2C_NUM_0

## 2. API Translation Requirements
| Functionality | STM32 HAL (Current) | ESP-IDF v5.x (Target) |
| :--- | :--- | :--- |
| **I2C Write** | `MAX30102_I2C_WriteData` (Bit-bang) | `i2c_master_transmit` |
| **I2C Read** | `MAX30102_I2C_ReadMultipleBytes` | `i2c_master_transmit_receive` |
| **Delay** | `I2C_Delay` (For-loop) | `vTaskDelay` or `esp_rom_delay_us` |
| **Ticks** | `HAL_GetTick()` | `esp_timer_get_time() / 1000` |
| **Interrupts** | `HAL_GPIO_EXTI_Callback` | `gpio_isr_handler_add` |
| **Logging** | `Serial_Printf` | `ESP_LOGI` |

## 3. Implementation Logic
### A. I2C Driver
- Remove all manual SCL/SDA bit-banging functions (`MAX30102_W_SCL`, etc.).
- Use the ESP-IDF `i2c_master_bus_config_t` and `i2c_master_dev_config_t` for initialization.

### B. Interrupt Handling
- Create a `static TaskHandle_t` to notify the processing task when the INT pin goes low.
- The ISR should use `vTaskNotifyGiveFromISR`.

### C. Buffer & Algorithm
- The algorithm uses large buffers (`aun_ir_buffer[500]`). In ESP32, please ensure these are allocated correctly (either static or heap) to avoid stack overflow in the FreeRTOS task.
- The sampling rate is 100Hz. The task should wait for the INT signal or use a precise timer.

### D. Main Task Structure
1. Initialize I2C.
2. Initialize GPIO & ISR.
3. Call `MAX30102_Init()`.
4. Loop: 
   - Wait for Interrupt (Data Ready).
   - Read FIFO (6 bytes per sample).
   - Store in circular buffer.
   - Every 100 new samples, call `MAX30102_Algorithm_Calculate`.
   - Log HR and SpO2 results via `ESP_LOGI`.