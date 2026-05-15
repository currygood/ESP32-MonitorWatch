# ESP32-S3 ULP-RISC-V 集成与低功耗开发文档

## 1. 概述

在 ESP32-S3 癫痫监测项目中，ULP (Ultra-Low Power) 协处理器用于在 SoC 进入 Deep-sleep 模式时，持续以低功耗模式监控传感器（MPU6050/MAX30102）。一旦检测到符合癫痫发作特征的异常数据（如剧烈震动或心率突变），ULP 将唤醒主核进行报警和联网操作。

**核心优势：**

- **低功耗：** ULP 运行电流仅为微安级（

  ```
  μAμA
  ```

  ）。

  

- **C 语言支持：** ESP32-S3 的 ULP-RISC-V 支持标准 C 开发。

- **独立 I2C 控制：** ULP 可独立操作硬件 I2C 控制器。

------



## 2. 硬件约束 (重要)

ESP32-S3 的 ULP 专用 I2C 控制器仅能映射到以下特定引脚：

- **SDA:** GPIO 1
- **SCL:** GPIO 2
- **注意：** 如果你的传感器未连接到这两个引脚，ULP 必须使用软件模拟 I2C（Bit-banging），这会增加功耗且开发难度较高。

------



## 3. 项目结构变更

为了支持 ULP，需要在 main 目录下建立 ulp 源码文件夹：

codeText

```
my_project/
├── main/
│   ├── main.c              # 主核逻辑
│   ├── CMakeLists.txt      # 修改此文件以编译 ULP
│   └── ulp/
│       ├── ulp_main.c      # ULP C 源码
│       └── shared_data.h   # 主核与 ULP 共享的变量结构体定义
```

------



## 4. 软件配置 (SDKConfig)   这个不需要改，我直接通过vs code图形化改好了

必须在 idf.py menuconfig 中开启以下选项：

- CONFIG_ULP_COPROCESSOR_ENABLED=y
- CONFIG_ULP_COPROCESSOR_TYPE_RISCV=y
- CONFIG_ULP_COPROCESSOR_RESERVE_MEM=4096 (建议预留 4KB)

------



## 5. 核心代码模板

### A. 共享变量定义 (main/ulp/shared_data.h)

主核和 ULP 通过 RTC 慢速内存交换数据。

codeC

```
#pragma once
#include <stdint.h>

// 结构体必须对齐，建议使用 uint32_t 以保证兼容性
typedef struct {
    uint32_t accel_threshold; // 震动触发阈值
    uint32_t hr_threshold;    // 心率报警阈值
    uint32_t status_flag;     // 0:正常, 1:检测到异常
    uint32_t last_accel_raw;  // 记录最后一次原始数据供主核查看
} ulp_shared_data_t;
```

### B. ULP 逻辑实现 (main/ulp/ulp_main.c)

此代码独立编译，无法调用主核的 BSP 驱动，需使用 ulp_riscv 专用库。

codeC

```
#include <stdio.h>
#include <stdint.h>
#include "ulp_riscv_utils.h"
#include "ulp_riscv_i2c_utils.h"
#include "shared_data.h"

// 声明外部共享变量 (在主核中用 RTC_DATA_ATTR 定义)
extern ulp_shared_data_t shared_data;

int main (void) {
    // 1. 初始化 ULP I2C
    ulp_riscv_i2c_cfg_t i2c_cfg = ULP_RISCV_I2C_DEFAULT_CONFIG();
    ulp_riscv_i2c_master_init(&i2c_cfg);

    while(1) {
        uint8_t data_h, data_l;
        // 2. 读取 MPU6050 寄存器 (示例地址 0x68, 加速度 X 轴)
        ulp_riscv_i2c_read_reg(0x68, 0x3B, &data_h);
        ulp_riscv_i2c_read_reg(0x68, 0x3C, &data_l);
        
        int16_t accel_raw = (data_h << 8) | data_l;
        shared_data.last_accel_raw = (uint32_t)accel_raw;

        // 3. 癫痫简易检测逻辑 (如：加速度绝对值超过阈值)
        if (accel_raw > (int16_t)shared_data.accel_threshold || accel_raw < -(int16_t)shared_data.accel_threshold) {
            shared_data.status_flag = 1;
            // 4. 唤醒主核
            ulp_riscv_wakeup_main_processor();
        }

        // 5. 降低采样频率以省电 (采样间隔 20ms = 50Hz)
        ulp_riscv_delay_cycles(20 * 1000 * 16); 
    }
}
```

### C. 主核加载与入口 (main/main.c)

主核负责初始化 ULP 并进入睡眠。

codeC

```
#include "esp_sleep.h"
#include "ulp_riscv.h"
#include "ulp_main.h" // 自动生成的头文件
#include "ulp/shared_data.h"

// 在 RTC 内存中定义共享变量
RTC_DATA_ATTR ulp_shared_data_t shared_data = {
    .accel_threshold = 2000,
    .status_flag = 0
};

void init_ulp_monitor() {
    // 加载二进制
    esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);

    // 设置唤醒源
    esp_sleep_enable_ulp_wakeup();
    
    // 启动 ULP
    ulp_riscv_run();
}

void app_main() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    if (cause == ESP_SLEEP_WAKEUP_ULP) {
        // 如果是由 ULP 唤醒，执行高优报警任务
        if (shared_data.status_flag == 1) {
            printf("Detection Triggered! Accel: %lu\n", shared_data.last_accel_raw);
            // 这里调用你的 Task_Buzzer 和 Task_MQTT
            shared_data.status_flag = 0;
        }
    }

    // 初始化硬件 I2C (主核用)
    // 注意：主核使用完 I2C 后，进入 Deep-sleep 前需释放引脚，否则 ULP 无法接管
    
    // 启动 ULP
    init_ulp_monitor();

    // 进入深度睡眠
    printf("Entering Deep-sleep...\n");
    esp_deep_sleep_start();
}
```

------



## 6. 构建系统集成 (main/CMakeLists.txt)

将以下内容加入到你的 main/CMakeLists.txt：

codeCmake

```
# 1. 设置 ULP 源代码列表
set(ulp_sources "ulp/ulp_main.c")

# 2. 设置 ULP 应用名称
set(ulp_app_name ulp_main)

# 3. 注册 ULP 应用
register_ulp_app_ext(${ulp_app_name} "${ulp_sources}")

# 4. 在主程序中包含生成头文件的目录
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES BSP Middlewares)
```

------



## 7. 针对癫痫监测项目的 Claude Code 修改指令

你可以给 Claude 发送以下指令：

> "请基于 ESP-IDF v5.4.2 框架，将我的 Task_Mpu6050_Monitor 逻辑迁移到 ULP-RISC-V 中。要求：
>
> 1. 在主核进入 Deep-sleep 时，由 ULP 以 50Hz 频率监控 MPU6050 的加速度。
> 2. 设定一个阈值（存于共享变量），当加速度超过该值时唤醒主核执行 Task_Buzzer 报警。
> 3. 确保 CMakeLists.txt 已正确配置 ULP 编译环境。
> 4. 注意 ESP32-S3 的 ULP I2C 必须使用 GPIO 1 (SDA) 和 GPIO 2 (SCL)。"

------



## 8. 注意事项

1. **栈深度：** ULP-RISC-V 的栈空间非常有限（默认为几百字节），**严禁在 ULP 中递归或定义巨大的局部数组**。
2. **浮点运算：** ULP 不支持硬件浮点运算，所有传感器数据处理建议使用**定点数或整数运算**。
3. **I2C 冲突：** 当主核唤醒时，ULP 和主核可能同时尝试访问 I2C。建议在唤醒后，主核先停止 ULP (ulp_riscv_timer_stop()) 或使用共享标志位做软件锁。