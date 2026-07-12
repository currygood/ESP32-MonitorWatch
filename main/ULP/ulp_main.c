#include <stdint.h>
#include <stdbool.h>
#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
#include "ulp_riscv_i2c_ulp_core.h"

/* --- 寄存器与地址定义 --- */
#define I2C_MPU_ADDR       0x68
#define I2C_MAX_ADDR       0x57

#define MPU_REG_PWR_MGMT_1 0x6B
#define MPU_REG_ACCEL_CFG  0x1C
#define MPU_REG_ACCEL_X_H  0x3B

#define MAX_REG_FIFO_DATA  0x07
#define MAX_REG_MODE_CFG   0x09
#define MAX_REG_SPO2_CFG   0x0A
#define MAX_REG_LED1_PA    0x0C
#define MAX_REG_LED2_PA    0x0D

#define SAMPLE_COUNT        100  
#define MAX_HRABNORMAL_NUM  2     
#define MAX_FALLABNORMAL    1     
#define FALL_THRESHOLD_SQ   150994944   //150,994,944  (4096*3)^2
#define HR_RISE_THRESHOLD   800   
#define HR_COOLDOWN_SAMPLES 10    
#define FINGER_ON_THRESHOLD 20000 

// 串口
#define UART_TX_PIN 4 
// 定义波特率延时（115200bps 约为 8.68us）
// 在 20MHz 的 ULP 时钟下，可以使用这个宏计算：
#define UART_BIT_TICKS (ULP_RISCV_CYCLES_PER_MS / 115) 

/* --- 共享变量 --- */
volatile uint32_t shared_ir_buf[SAMPLE_COUNT];
volatile uint32_t shared_red_buf[SAMPLE_COUNT];
volatile uint32_t shared_ax_buf[SAMPLE_COUNT];
volatile uint32_t shared_ay_buf[SAMPLE_COUNT];
volatile uint32_t shared_az_buf[SAMPLE_COUNT];
volatile uint32_t wakeup_reason = 0; 

static int hr_abnormal_consecutive_count = 0;
static int fall_consecutive_count = 0;
static uint8_t firstIgnore = 4;

/* --- 硬件 I2C 封装函数 --- */
static void i2c_write_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t value) {
    // 注意：这里 slave_addr 直接传 0x68 或 0x57，不要左移！
    ulp_riscv_i2c_master_set_slave_addr(slave_addr);
    ulp_riscv_i2c_master_set_slave_reg_addr(reg_addr);
    // 可以在这里加一个极短的延时确保硬件寄存器同步
    ulp_riscv_delay_cycles(1000); 
    ulp_riscv_i2c_master_write_to_device(&value, 1);
}

static void i2c_read_regs(uint8_t slave_addr, uint8_t reg_addr, uint8_t *data, size_t len) {
    ulp_riscv_i2c_master_set_slave_addr(slave_addr);
    ulp_riscv_i2c_master_set_slave_reg_addr(reg_addr);
    ulp_riscv_i2c_master_read_from_device(data, len);
}

/* --- 修复3：sensors_init() 内所有 I2C 操作加保护 --- */
void sensors_init() {
	// 刚进入时多等一会儿，确保主 CPU 的电压域切换完全稳定
    ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 50);
    // MPU6050 复位
    i2c_write_reg(I2C_MPU_ADDR, 0x6B, 0x80);
    ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 150);
    i2c_write_reg(I2C_MPU_ADDR, 0x6B, 0x01);
    i2c_write_reg(I2C_MPU_ADDR, 0x1C, 0x10);

    // MAX30102 复位
    i2c_write_reg(I2C_MAX_ADDR, 0x09, 0x40);
    ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 150);

    // ★ 修复3：固定等待足够时间，不依赖轮询（避免 I2C 挂死时死锁）
    // MAX30102 datasheet 说复位最多 1ms，150ms 绰绰有余，直接跳过轮询
    // 如果一定要轮询，改用带超时的非阻塞方式：
    uint8_t mode_reg = 0x40;
    for (int retry = 0; retry < 20 && (mode_reg & 0x40); retry++) {
        ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 10);
        // 先延时再读，而不是先读再延时
        // 若 I2C 挂死，最多等 200ms 后放弃
        i2c_read_regs(I2C_MAX_ADDR, 0x09, &mode_reg, 1);
    }
    // 无论轮询结果如何，继续执行（不要卡死）

    i2c_write_reg(I2C_MAX_ADDR, 0x04, 0x00);
    i2c_write_reg(I2C_MAX_ADDR, 0x05, 0x00);
    i2c_write_reg(I2C_MAX_ADDR, 0x06, 0x00);
    i2c_write_reg(I2C_MAX_ADDR, 0x09, 0x03);
    i2c_write_reg(I2C_MAX_ADDR, 0x0A, 0x27);
    i2c_write_reg(I2C_MAX_ADDR, 0x0C, 0x24);
    i2c_write_reg(I2C_MAX_ADDR, 0x0D, 0x24);

    uint8_t dummy[6];
    for(int i = 0; i < 10; i++) {
        i2c_read_regs(I2C_MAX_ADDR, 0x07, dummy, 6);
        ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 5);
    }
}

int main(void) {
    // 等待主 CPU 完全进入睡眠
    ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 2000);

    hr_abnormal_consecutive_count = 0;
    fall_consecutive_count = 0;
    firstIgnore = 4;

    sensors_init();

    while(1) {
		int i2c_error_count = 0;
        bool current_batch_fall = false;
        int peak_count = 0;
        uint32_t last_ir = 0;
        bool rising = false;
        int cooldown_cnt = 0;
        bool finger_detected = false;

        for (int i = 0; i < SAMPLE_COUNT; i++) {
            // A. 读取 MPU6050
            uint8_t d_mpu[6];
            i2c_read_regs(I2C_MPU_ADDR, MPU_REG_ACCEL_X_H, d_mpu, 6);

            int16_t ax = (int16_t)((d_mpu[0] << 8) | d_mpu[1]);
            int16_t ay = (int16_t)((d_mpu[2] << 8) | d_mpu[3]);
            int16_t az = (int16_t)((d_mpu[4] << 8) | d_mpu[5]);

            shared_ax_buf[i] = (uint32_t)ax;
            shared_ay_buf[i] = (uint32_t)ay;
            shared_az_buf[i] = (uint32_t)az;

            int64_t v_sq = (int64_t)ax*ax + (int64_t)ay*ay + (int64_t)az*az;
            if (v_sq > FALL_THRESHOLD_SQ) current_batch_fall = true;

            // B. 读取 MAX30102
            uint8_t d_max[6];
            i2c_read_regs(I2C_MAX_ADDR, MAX_REG_FIFO_DATA, d_max, 6);

            uint32_t red = ((uint32_t)(d_max[0] & 0x03) << 16) | ((uint32_t)d_max[1] << 8) | d_max[2];
            uint32_t ir  = ((uint32_t)(d_max[3] & 0x03) << 16) | ((uint32_t)d_max[4] << 8) | d_max[5];

			// 防御性检查：如果 IR 读数始终为 0 或 0x3FFFF (MAX30102 NACK时的表现)
            // 这通常意味着 I2C 总线挂了。如果连续 50 次采样都是这种无效数据，强制唤醒主 CPU 检查。
            
            if (ir == 0 || ir == 0x3FFFF) {
                i2c_error_count++;
            } else {
                i2c_error_count = 0;
            }

            if (i2c_error_count > 50) {
                wakeup_reason = 99; // 错误码 99 代表 I2C 挂死
                ulp_riscv_wakeup_main_processor();
                break;
            }

            shared_red_buf[i] = red;
            shared_ir_buf[i]  = ir;

            // C. 简单心率逻辑
            if (ir > FINGER_ON_THRESHOLD) {
                finger_detected = true;
                if (last_ir > 0 && cooldown_cnt == 0) {
                    int32_t diff = (int32_t)ir - (int32_t)last_ir;
                    if (diff > HR_RISE_THRESHOLD && !rising) {
                        rising = true; peak_count++; cooldown_cnt = HR_COOLDOWN_SAMPLES;
                    } else if (diff < -HR_RISE_THRESHOLD) {
                        rising = false;
                    }
                }
            }
            if (cooldown_cnt > 0) cooldown_cnt--;
            last_ir = ir;

            ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 20);
        }

        // 决策判断
        bool current_batch_hr_error = false;
        if (finger_detected) {
            if (peak_count < 1 || peak_count > 7) current_batch_hr_error = true;
        } else {
            hr_abnormal_consecutive_count = 0;
        }

        if (current_batch_hr_error) {
            if (firstIgnore > 0) firstIgnore--;
            else hr_abnormal_consecutive_count++;
        } else hr_abnormal_consecutive_count = 0;

        if (current_batch_fall) fall_consecutive_count++;
        else fall_consecutive_count = 0;

        if (hr_abnormal_consecutive_count >= MAX_HRABNORMAL_NUM) {
            wakeup_reason = 1;
            ulp_riscv_wakeup_main_processor();
            break; 
        }
        if (fall_consecutive_count >= MAX_FALLABNORMAL) {
            wakeup_reason = 2;
            ulp_riscv_wakeup_main_processor();
            break;
        }
    }
    
    return 0;
}