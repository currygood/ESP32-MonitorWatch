#include <stdint.h>
#include <stdbool.h>
#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
#include "ulp_i2c_sw.h"

/* --- 寄存器与地址定义 --- */
#define I2C_MPU_ADDR       0x68
#define I2C_MAX_ADDR       0x57

#define MPU_REG_PWR_MGMT_1 0x6B
#define MPU_REG_ACCEL_X_H  0x3B

#define MAX_REG_INTR_EN1   0x02
#define MAX_REG_FIFO_WP    0x04
#define MAX_REG_FIFO_RP    0x06
#define MAX_REG_FIFO_DATA  0x07
#define MAX_REG_MODE_CFG   0x09
#define MAX_REG_SPO2_CFG   0x0A
#define MAX_REG_LED1_PA    0x0C
#define MAX_REG_LED2_PA    0x0D

#define MAX_HRABNORMAL_NUM 3
#define MAX_FALLABNORMAL   1

/* --- 算法配置 --- */
#define SAMPLE_COUNT       100  // 缓冲区样本数 (占用约1.4KB RTC RAM)
// 跌倒判定阈值：2.0g. LSB=2048 (±16g范围), 2.0g=4096. 4096^2 = 16777216
#define FALL_THRESHOLD_SQ  16777216 
// 简单的红外峰值判定阈值 (需要根据环境调整)
#define IR_PEAK_THRESHOLD  50000 

/* --- 共享变量：主 CPU 可通过 ulp_前缀访问 --- */
volatile uint32_t shared_ir_buf[SAMPLE_COUNT];
volatile uint32_t shared_red_buf[SAMPLE_COUNT];
volatile int16_t  shared_ax_buf[SAMPLE_COUNT];
volatile int16_t  shared_ay_buf[SAMPLE_COUNT];
volatile int16_t  shared_az_buf[SAMPLE_COUNT];
volatile uint32_t wakeup_reason = 0; // 1: 心率异常唤醒, 2: 摔倒唤醒

/* --- 内部私有变量 --- */
static int hr_abnormal_consecutive_count = 0;
static int fall_consecutive_count = 0;
static int warmup_batches = 0; // 新增：预热计数

/* --- 传感器初始化函数 --- */
void sensors_init() {
    I2C_init();
	
    // 1. 初始化 MPU6050
    uint8_t mpu_wake[] = {MPU_REG_PWR_MGMT_1, 0x01};
    I2C_transmit(I2C_MPU_ADDR << 1, mpu_wake, 2);
    
    // 2. 初始化 MAX30102
    uint8_t max_reset[] = {MAX_REG_MODE_CFG, 0x40};
    I2C_transmit(I2C_MAX_ADDR << 1, max_reset, 2);
    ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 50); // 等待复位完成

    // 配置为 SpO2 模式, 100sps, 411us PW, 18bit
    uint8_t max_init_seq[] = {
        MAX_REG_MODE_CFG, 0x03,
        MAX_REG_SPO2_CFG, 0x27,
        MAX_REG_LED1_PA,  0x24,
        MAX_REG_LED2_PA,  0x24
    };
    for(int i=0; i<8; i+=2) {
        I2C_transmit(I2C_MAX_ADDR << 1, &max_init_seq[i], 2);
    }
}

/* --- 主循环 --- */
int main(void) {
    sensors_init();
	wakeup_reason = 0; // 明确初始化

    while(1) {
        bool current_batch_fall = false;
        bool current_batch_hr_error = false;

        // 采集一包数据 (100个样本)
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            // A. 读取 MPU6050 加速度 (6字节: X,Y,Z)
            uint8_t reg_mpu = MPU_REG_ACCEL_X_H;
            uint8_t d_mpu[6];
            I2C_receive(I2C_MPU_ADDR << 1, &reg_mpu, d_mpu, 1, 6);
            
            shared_ax_buf[i] = (int16_t)((d_mpu[0] << 8) | d_mpu[1]);
            shared_ay_buf[i] = (int16_t)((d_mpu[2] << 8) | d_mpu[3]);
            shared_az_buf[i] = (int16_t)((d_mpu[4] << 8) | d_mpu[5]);

            // 实时检查是否发生剧烈撞击 (平方和避免浮点sqrt)
            int32_t x = shared_ax_buf[i];
            int32_t y = shared_ay_buf[i];
            int32_t z = shared_az_buf[i];
            if ((x*x + y*y + z*z) > FALL_THRESHOLD_SQ) {
                current_batch_fall = true;
            }

            // B. 读取 MAX30102 FIFO (6字节: Red 3字节, IR 3字节)
            uint8_t reg_max = MAX_REG_FIFO_DATA;
            uint8_t d_max[6];
            I2C_receive(I2C_MAX_ADDR << 1, &reg_max, d_max, 1, 6);
            
            shared_red_buf[i] = ((uint32_t)(d_max[0] & 0x03) << 16) | ((uint32_t)d_max[1] << 8) | d_max[2];
            shared_ir_buf[i]  = ((uint32_t)(d_max[3] & 0x03) << 16) | ((uint32_t)d_max[4] << 8) | d_max[5];

            // 采样间隔控制：约 25Hz (40ms)
            // 除去 I2C 通信耗时，这里大约延迟 35ms 左右
            ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 35);
        }

		// 预热逻辑：跳过前 3 次大循环（约 12 秒），防止传感器启动噪声导致误报
        if (warmup_batches < 3) {
            warmup_batches++;
            continue; 
        }

        /* --- 简单的 ULP 端算法判断 --- */
        
        // 1. 简易心率监测：统计波峰数量
        // 正常 25Hz 采样下，100个样本 = 4秒数据。正常心率 (60-120bpm) 波峰应在 4-8 个之间。
        int peak_count = 0;
        for (int j = 1; j < SAMPLE_COUNT - 1; j++) {
            if (shared_ir_buf[j] > shared_ir_buf[j-1] && shared_ir_buf[j] > shared_ir_buf[j+1]) {
                if (shared_ir_buf[j] > IR_PEAK_THRESHOLD) {
                    peak_count++;
                }
            }
        }
        // 如果 4 秒内波峰极少或极多，认为异常
        if (peak_count>0 && (peak_count < 2 || peak_count > 10)) {
            current_batch_hr_error = true;
        }

        // 2. 连续异常逻辑计数
        if (current_batch_hr_error) hr_abnormal_consecutive_count++;
        else hr_abnormal_consecutive_count = 0;

        if (current_batch_fall) fall_consecutive_count++;
        else fall_consecutive_count = 0;

        /* --- 唤醒决策 --- */
        if (hr_abnormal_consecutive_count >= MAX_HRABNORMAL_NUM) {
            wakeup_reason = 1;
            ulp_riscv_wakeup_main_processor();
            break; // 停止 ULP，等待主 CPU 处理
        }
        
        if (fall_consecutive_count >= MAX_FALLABNORMAL) {
            wakeup_reason = 2;
            ulp_riscv_wakeup_main_processor();
            break;
        }
    }
    
    return 0;
}