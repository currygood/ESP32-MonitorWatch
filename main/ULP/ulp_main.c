#include <stdint.h>
#include <stdbool.h>
#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
#include "ulp_i2c_sw.h"

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

/* --- 算法配置参数 --- */
#define SAMPLE_COUNT        100  
#define MAX_HRABNORMAL_NUM  2     // 连续多少包心率异常才唤醒
#define MAX_FALLABNORMAL    1     // 连续多少包检测到摔倒就唤醒

// 跌倒阈值：数值越小，摔倒报警越灵敏
#define FALL_THRESHOLD_SQ   9625000

// 心率灵敏度调整
#define HR_RISE_THRESHOLD   800   // 上升斜率阈值 (越大越不敏感)
#define HR_COOLDOWN_SAMPLES 10    // 避震期：检测到一次心跳后忽略接下来的10个点(约250ms)
#define FINGER_ON_THRESHOLD 20000 // IR值低于此值认为手指没放在传感器上

/* --- 共享变量 (必须保留，供主CPU访问) --- */
volatile uint32_t shared_ir_buf[SAMPLE_COUNT];
volatile uint32_t shared_red_buf[SAMPLE_COUNT];
volatile int16_t  shared_ax_buf[SAMPLE_COUNT];
volatile int16_t  shared_ay_buf[SAMPLE_COUNT];
volatile int16_t  shared_az_buf[SAMPLE_COUNT];
volatile uint32_t wakeup_reason = 0; // 1: 心率异常, 2: 摔倒

/* --- 内部私有变量 --- */
static int hr_abnormal_consecutive_count = 0;
static int fall_consecutive_count = 0;
static uint8_t firstIgnore = 4;

/* --- 传感器初始化函数 --- */
void sensors_init() {
    I2C_init();
	
    // 1. 初始化 MPU6050
    uint8_t mpu_init_seq[][2] = {
        {MPU_REG_PWR_MGMT_1, 0x01}, // 唤醒
        {MPU_REG_ACCEL_CFG,  0x10}  // 设置量程为 ±8g (4096 LSB/g)
    };
    for(int i=0; i<2; i++) {
        I2C_transmit(I2C_MPU_ADDR << 1, mpu_init_seq[i], 2);
    }
    
    // 2. 初始化 MAX30102
    uint8_t max_reset[] = {MAX_REG_MODE_CFG, 0x40};
    I2C_transmit(I2C_MAX_ADDR << 1, max_reset, 2);
    ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 35);

    uint8_t max_init_seq[] = {
        MAX_REG_MODE_CFG, 0x03, // SpO2 模式
        MAX_REG_SPO2_CFG, 0x27, // 100sps, 18bit
        MAX_REG_LED1_PA,  0x24, // LED电流，适中
        MAX_REG_LED2_PA,  0x24
    };
    for(int i=0; i<8; i+=2) {
        I2C_transmit(I2C_MAX_ADDR << 1, &max_init_seq[i], 2);
    }
}

/* --- 主循环 --- */
int main(void) {
    sensors_init();

    while(1) {
        bool current_batch_fall = false;
        int peak_count = 0;
        uint32_t last_ir = 0;
        bool rising = false;
        int cooldown_cnt = 0;
        bool finger_detected = false;

        // 采集一包数据 (100个样本)
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            // A. 读取并实时判断 MPU6050 加速度
            uint8_t reg_mpu = MPU_REG_ACCEL_X_H;
            uint8_t d_mpu[6];
            I2C_receive(I2C_MPU_ADDR << 1, &reg_mpu, d_mpu, 1, 6);
            
            int16_t ax = (int16_t)((d_mpu[0] << 8) | d_mpu[1]);
            int16_t ay = (int16_t)((d_mpu[2] << 8) | d_mpu[3]);
            int16_t az = (int16_t)((d_mpu[4] << 8) | d_mpu[5]);
            
            shared_ax_buf[i] = ax;
            shared_ay_buf[i] = ay;
            shared_az_buf[i] = az;

            // 实时检查冲击 (使用int64避免平方溢出)
            int64_t v_sq = (int64_t)ax*ax + (int64_t)ay*ay + (int64_t)az*az;
            if (v_sq > FALL_THRESHOLD_SQ) {
                current_batch_fall = true;
            }

            // B. 读取 MAX30102 FIFO
            uint8_t reg_max = MAX_REG_FIFO_DATA;
            uint8_t d_max[6];
            I2C_receive(I2C_MAX_ADDR << 1, &reg_max, d_max, 1, 6);
            
            uint32_t red = ((uint32_t)(d_max[0] & 0x03) << 16) | ((uint32_t)d_max[1] << 8) | d_max[2];
            uint32_t ir  = ((uint32_t)(d_max[3] & 0x03) << 16) | ((uint32_t)d_max[4] << 8) | d_max[5];
            
            shared_red_buf[i] = red;
            shared_ir_buf[i]  = ir;

            // C. 心率实时波峰分析
            if (ir > FINGER_ON_THRESHOLD) {
                finger_detected = true;
                if (last_ir > 0 && cooldown_cnt == 0) {
                    int32_t diff = (int32_t)ir - (int32_t)last_ir;
                    if (diff > HR_RISE_THRESHOLD && !rising) {
                        rising = true;
                        peak_count++;
                        cooldown_cnt = HR_COOLDOWN_SAMPLES; // 进入不应期
                    } else if (diff < -HR_RISE_THRESHOLD) {
                        rising = false;
                    }
                }
            }
            
            if (cooldown_cnt > 0) cooldown_cnt--;
            last_ir = ir;

            // 采样频率控制: 25ms
            ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS * 25);
        }

        /* --- 决策逻辑 --- */
        bool current_batch_hr_error = false;

        // 仅在检测到手指时判断心率是否异常
        if (finger_detected) {
            // 2.5秒内，正常心率 60-120bpm 应有 2-5 次跳动
            // 设定范围 1-7 次，超出则认为异常
            if (peak_count < 1 || peak_count > 7) {
                current_batch_hr_error = true;
            }
        } else {
            // 手指离开时，重置异常计数，不触发唤醒
            hr_abnormal_consecutive_count = 0;
        }

        // 连续异常计数
        if (current_batch_hr_error) 
		{
			if (firstIgnore>0)
			{
				firstIgnore--;
			}
			else
			{
				hr_abnormal_consecutive_count++;
			}
		}else hr_abnormal_consecutive_count = 0;

        if (current_batch_fall) fall_consecutive_count++;
        else fall_consecutive_count = 0;

        /* --- 唤醒执行 --- */
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