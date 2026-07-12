# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于ESP32的癫痫前期症状监测报警手表，实时性要求极高，用于监测发作前症状并立即报警。系统采用双核架构：主CPU负责复杂的数据处理和报警逻辑，ULP协处理器负责低功耗持续监测。

**实时性要求**：
- 禁用传感器休眠机制，确保50Hz持续采样
- I2C总线始终活跃，不可休眠
- MQTT数据实时上报，不可延迟/打包
- OLED显示实时更新

## 开发分支策略

| 分支 | 用途 |
|------|------|
| main | 主分支，生产就绪状态 |
| EmbeddedGame | 癫痫监测游戏化开发（当前分支） |
| MAX30102 | MAX30102传感器相关改进 |
| GDDesignedGame | 完整游戏化设计分支 |

开发新功能时从对应特性分支创建新分支，完成后合并回main。重大改动先合并到GDDesignedGame再合并到main。

## 构建与开发命令

**ESP-IDF版本**: 5.2.6

```bash
# 配置项目（目标为ESP32-S3）
idf.py set-target esp32s3

# 编译项目
idf.py build

# 编译特定组件
idf.py build components/BSP/I2C
idf.py build components/Middlewares/OLED

# 烧录到设备（COM口根据实际情况修改）
idf.py -p COMX flash

# 监控串口输出
idf.py -p COMX monitor

# 清理构建
idf.py fullclean && idf.py set-target esp32s3

# 清理特定组件
idf.py fullclean components/BSP/I2C
```

## 代码架构

### 分层结构
- **main/** - 主程序入口，任务调度和ULP协调
  - `main.c` - 主程序逻辑，ULP唤醒处理
  - `ULP/ulp_main.c` - ULP协处理器程序，低功耗采样
  - `ULP/ulp_i2c_sw.c` - ULP的软件I2C实现
- **components/BSP/** - 硬件抽象层驱动
  - I2C驱动 (`i2c_driver.c/h`) - 全局I2C总线管理
  - MQTT (`mqtt.c/h`, `passwd.md`) - MQTT客户端
  - RTC (`rtc_driver.c/h`) - RTC时钟
  - 蜂鸣器 (`Buzzer.c/h`)
  - 按键 (`Key.c/h`)
  - Enable控制 (`en.c/h`)
- **components/Middlewares/** - 传感器和数据中间件
  - MAX30102 (`max30102.c/h`) - 心率/血氧监测
  - OLED (`OLED.c/h`, `OLED_Data.c/h`) - 显示界面
  - MPU6050 (`MPU6050.c/h`, `Detect.md`) - 加速度计/陀螺仪
  - MessageQueue (`MessageQueue.c/h`) - 任务间通信
  - GetBatteryLevel (`GetBaLevel.c/h`) - 电池电量获取

### 核心任务架构（双核运行）

**Core 0**:
- `Task_MQTT_Message_Handler` (优先级3) - MQTT消息处理、连接管理
- `Task_Buzzer` (优先级2) - 蜂鸣器控制、报警触发

**Core 1**:
- `Task_Max30102_Monitor` (优先级5) - MAX30102心率/血氧监测（50Hz）
- `Task_Mpu6050_Monitor` (优先级5) - MPU6050加速度计/陀螺仪监测
- `Task_OLED_Show` (优先级2) - OLED显示更新

### 数据流
传感器数据 → MessageQueue → 对应处理任务 → 更新显示/发布MQTT

### I2C总线
**全局单一I2C总线实例**，所有I2C设备（MAX30102、MPU6050、OLED）共享同一根总线。主CPU的I2C初始化必须在`app_main`最前面。

## ULP协处理器

### ULP程序编译
ULP程序使用RISC-V架构，与主CPU的Xtensa架构不同，必须使用交叉编译：

```bash
# 编译ULP程序
idf.py build app

# ULP固件会自动生成到 build/ulp_main.bin
```

**重要**：ULP程序代码在`main/ULP/ulp_main.c`，编译时会自动链接到main项目。

### ULP与主CPU共享变量
共享变量定义在`ulp_main.c`中，主CPU代码通过`ulp_main.h`声明访问：

```c
// ulp_main.h
extern volatile uint32_t ulp_shared_ir_buf[];
extern volatile uint32_t ulp_shared_red_buf[];
extern volatile uint32_t ulp_shared_ax_buf[];
extern volatile uint32_t ulp_shared_ay_buf[];
extern volatile uint32_t ulp_shared_az_buf[];
extern volatile uint32_t ulp_wakeup_reason;
```

**注意**：实际变量定义在ULP程序中，使用时必须加`ulp_`前缀访问。

### ULP深度睡眠流程
长按key1或电池电量<40% → 启动深度睡眠+ULP协处理器
- ULP协处理器采样传感器数据（100个样本）
- 心率异常：连续2次过高/过低 → 唤醒主CPU → 主CPU二次确认 → 报警
- 跌倒异常：连续1次检测到剧烈撞击（加速度>4900²=g²）→ 唤醒主CPU → 确认跌倒/抽搐 → 报警
- 电池电量<40% → 唤醒主CPU
- 异常数据通过共享缓冲区传回主CPU处理

### ULP启动竞争问题
**问题**：主CPU读取到的`wakeup_reason`可能为0的根本原因是ULP在主CPU启动期间重新运行并清零。

**解决方案**：
1. 在ULP程序启动前，设置`ulp_wakeup_reason`初始值
2. 主CPU读取前检查是否为0，如果是则重置后重新读取

参考`note/2026-05-24.md`和`note/all.md`中的详细解决方案。

### ULP第二次唤醒问题
**问题**：第二次进入深度睡眠+ULP处理时，主CPU无法正确退出循环。

**解决方案**：在唤醒后添加`ulp_riscv_halt()`和`ulp_riscv_reset()`调用，确保ULP协处理器完全复位。

## 传感器初始化顺序

**重要**：I2C总线的初始化必须在`app_main`的最前面，且所有传感器必须按照以下顺序初始化：

1. `En_Init()` - 启用电池检测控制
2. `En_Set()` - 设置电池检测使能
3. `Rtc_Init()` - RTC时钟初始化
4. `Battery_Level_Init()` - 电池电量初始化
5. `Buzzer_Init()` - 蜂鸣器初始化
6. `Key_Init()` - 按键初始化
7. `I2c_Init_Bus()` - **I2C总线初始化（必须最先）**
8. `Max30102_Init()` - MAX30102初始化
9. `Mpu6050_Init()` - MPU6050初始化
10. `OLED_Init()` - OLED初始化
11. 各监测任务创建

**原因**：I2C总线是所有传感器的共享总线，必须先初始化总线再添加设备。

## 调试命令

```bash
# 实时监控日志输出
idf.py -p COMX monitor

# 只显示特定标签的日志
idf.py -p COMX monitor | grep "TAG_NAME"

# 查看UART输出
# 串口波特率：115200
# 数据位：8
# 停止位：1
# 校验位：无

# 清除NVS配置（重新配网时）
idf.py -p COMX erase-flash
idf.py -p COMX flash
idf.py -p COMX monitor
```

## 组件开发

添加新中间件组件到 `components/Middlewares/CMakeLists.txt`：

```cmake
set(src_dirs NEW_COMPONENT/new_component.c)
set(include_dirs NEW_COMPONENT)
set(requires BSP)  # 大多数中间件依赖BSP
idf_component_register(SRCS "${src_dirs}" INCLUDE_DIRS "${include_dirs}" REQUIRES ${requires})
```

## MQTT配置

### 三种配置方式
1. **修改代码默认值**（需erase-flash）：在`mqtt.h`修改`DEFAULT_MQTT_USERNAME`和`DEFAULT_MQTT_PASSWORD`
2. **手机App动态修改**（推荐）：通过AP配网网页上传凭据，保存到NVS
3. **修改OneNET Topic**：在`mqtt.h`修改`SENSOR_REPORT_TOPIC`

### AP配网
- 触发：长按key2进入AP配网网页（3分钟内）
- 网页地址：http://192.168.4.1
- SSID：EpiWatch_AP
- 密码：watch1234

## 关键算法参数

### MAX30102心率检测
- `HEART_RATE_WARNING_THRESHOLD_LOW`：基准值-20
- `HEART_RATE_WARNING_THRESHOLD_HIGH`：基准值+30
- `HR_RISE_THRESHOLD`：800（峰值检测阈值）

### MPU6050跌倒检测
- `FALL_THRESHOLD_SQ`：24090976（即加速度>4900²=g²）
- 在主CPU中阈值更严格，ULP中为3.0g（更大以减少误唤醒）

### MPU6050抽搐检测
- `activity_score`阈值：0.25
- 高频震动检测：`high_peak_count > 8`

## NVS配置存储

WiFi凭据和MQTT配置存储在NVS的`watch_cfg`命名空间中。

```c
#define NVS_NAMESPACE           "watch_cfg"
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_MQTT_USER       "mqtt_user"
#define NVS_KEY_MQTT_PASS       "mqtt_pass"
#define NVS_KEY_MQTT_CLIENT_ID  "mqtt_client_id"
```

## 代码规范

### 命名约定
- **全局变量**：驼峰+下划线（如`g_i2c_handle`）
- **常量**：全大写（如`MAX_BUFFER_SIZE`）
- **局部变量**：首单词小写，后续首字母大写（如`sensorData`）
- **函数**：驼峰+下划线（如`GetHeartRate`）
- **枚举**：全大写下划线（如`KEY_EVENT_LONG_PRESS`）

### 大括号规则
所有有括号的结构（if/for/while/函数）左括号和右括号各占一行。

## 数据手册位置

- `DataSheet/癫痫只能监测手环.docx`
- `DataSheet/MAX30102.pdf`
- `DataSheet/PS_MPU6050.pdf`
- `DataSheet/RM_MPU6050.pdf`
- `DataSheet/中景园电子0.96OLED显示屏_驱动芯片手册.pdf`

## 开发环境

**ESP-IDF版本**: 5.2.6
**目标芯片**: ESP32-S3
**依赖组件**:
- FreeRTOS (内置)
- ESP32-S3硬件驱动
- WiFi (esp_wifi, esp_netif, esp_event)
- MQTT (MQTT over WiFi)
- NVS (非易失性存储)
- SPIFFS (文件系统)
- HTTP Server (AP配网页面)

## 分支管理

开发新功能时从对应特性分支创建新分支，完成后合并回main。重大改动先合并到GDDesignedGame再合并到main。

| 分支 | 用途 |
|------|------|
| main | 主分支，生产就绪状态 |
| EmbeddedGame | 癫痫监测游戏化开发（当前分支） |
| MAX30102 | MAX30102传感器相关改进 |
| GDDesignedGame | 完整游戏化设计分支 |

**工作流程**:
1. 从特性分支创建新功能分支 (如 `git checkout -b feature/xxx`)
2. 完成开发后提交到分支
3. 创建Pull Request合并回特性分支
4. 合并到GDDesignedGame后，再合并到main

## 调试技巧

### ULP问题排查
- **ULP第二次唤醒问题**：参考`note/2026-05-24.md`，解决方案是在`init_ulp_program()`中调用`ulp_riscv_halt()`和`ulp_riscv_reset()`彻底复位ULP内核
- **ULP启动竞争**：参考`note/all.md`，解决方案是在ULP程序启动前设置`ulp_wakeup_reason`初始值，主CPU读取前检查是否为0
- 查看ULP状态：使用`idf.py monitor`查看ULP的日志输出（TAG: ULP）

### I2C问题排查
- **I2C挂死问题**：MAX30102在异常情况下可能出现I2C挂死，ULP程序中有自动检测和恢复机制（连续50次采样失败自动唤醒主CPU）
- I2C地址检查：确认所有设备的I2C地址正确（MAX30102=0x57, MPU6050=0x68, OLED=0x3C）
- I2C初始化顺序：必须在`app_main`最前面初始化I2C总线

### MQTT问题排查
- **AP配网页面**：访问 `http://192.168.4.1` 配置WiFi和MQTT凭据
- **passwd.md**：包含MQTT密码相关配置说明
- **MQTT使用说明**：参考`components/BSP/MQTT/mqttUse.md`

### 调试命令组合
```bash
# 实时监控所有日志
idf.py -p COMX monitor

# 只看关键模块日志
idf.py -p COMX monitor | grep -E "(ULP|MQTT|Task_)"
```

## AP配网

长按Key2进入AP配网模式（3分钟内有效）

- **网页地址**: http://192.168.4.1
- **SSID**: EpiWatch_AP
- **密码**: watch1234
- **配置内容**: WiFi SSID/密码、MQTT Username/Password
- **配置存储**: 自动保存到NVS的`watch_cfg`命名空间

**相关文件**:
- `html/apcfg.html` - AP配网页面源文件
- `components/BSP/MQTT/passwd.md` - MQTT密码配置说明
- `components/BSP/MQTT/mqttUse.md` - MQTT使用说明

## 分区表

使用16MB Flash分区表（`partitions-16MiB.csv`）：

| 分区 | 大小 | 用途 |
|------|------|------|
| nvs | 24KB | WiFi和MQTT配置 |
| phy_init | 4KB | PHY初始化数据 |
| factory | 2MB | 主程序固件 |
| vfs | 10MB | 文件系统 |
| storage | 4MB | SPIFFS存储 |

**重新烧录分区表**: `idf.py erase-flash && idf.py partition-table && idf.py flash`

## 代码规范

### 命名约定
- **全局变量**: 驼峰+下划线（如`g_i2c_handle`）
- **常量**: 全大写（如`MAX_BUFFER_SIZE`）
- **局部变量**: 首单词小写，后续首字母大写（如`sensorData`）
- **函数**: 驼峰+下划线（如`GetHeartRate`）
- **枚举**: 全大写下划线（如`KEY_EVENT_LONG_PRESS`）

### 大括号规则
所有有括号的结构（if/for/while/函数）左括号和右括号各占一行。

```c
if(condition)
{
    // code
}

for(int i = 0; i < count; i++)
{
    // code
}
```

### 注释规范
默认不写注释。只在以下情况添加：
- 隐藏约束或特殊情况
- 重要的设计决策
- 不明显的实现细节

避免：
- 解释"是什么"（命名已说明）
- 过度解释（注释应比代码更简洁）
- 多行注释块（单行足够）

## 关键已知问题

1. **ULP第二次唤醒问题**：第二次进入深度睡眠+ULP处理时无法正确退出主CPU的循环（参考`note/2026-05-24.md`）
2. **ULP启动竞争**：主CPU读取到的`wakeup_reason`可能为0的根本原因是ULP在主CPU启动期间重新运行并清零（参考`note/all.md`）
3. **I2C挂死问题**：MAX30102在异常情况下可能出现I2C挂死，ULP程序中有自动检测和恢复机制

## 经验总结

**问题定位原则**：
- 找AI解决3次还不行，那就自己定位找问题
- 从现象出发，建立假设，逐步验证
- 抛开通用建议，基于具体代码逻辑推理
- 区分"系统问题"和"您的代码问题"
- 从最基本的硬件机制、API行为开始理解

**ULP调试要点**：
- ULP RISC-V不会自动清零内核状态，每次重新加载前必须调用`ulp_riscv_reset()`
- RTC I2C是硬件实现的，状态机冲突会导致sensors_init卡死
- ULP日志输出使用`ESP_LOGE/ESP_LOGI/ESP_LOGW`（ULP不支持info标签）
- 使用`ulp_riscv_get_reg()`和`ulp_riscv_set_reg()`调试共享变量状态
