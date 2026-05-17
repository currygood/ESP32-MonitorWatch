# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于ESP32的癫痫前期症状监测报警手表，**实时性要求极高**，用于监测发作前症状并立即报警。项目采用分层架构：BSP层处理底层硬件驱动，Middlewares层提供传感器数据和中间件功能。

**实时性要求**：
- 禁用传感器休眠机制，确保50Hz持续采样
- I2C总线始终活跃，不可休眠
- MQTT数据实时上报，不可延迟/打包
- OLED显示实时更新

## 开发分支策略

**main** - 主分支，生产就绪状态
**EmbeddedGame** - 当前开发分支（癫痫监测游戏化）
**MAX30102** - MAX30102传感器相关改进
**GDDesignedGame** - 完整游戏化设计分支

开发新功能时：
1. 从对应特性分支创建新分支（如 EmbeddedGame）
2. 开发完成后合并回主分支
3. 重大改动考虑先合并到 GDDesignedGame 分支再合并到 main

## 构建与开发命令

**ESP-IDF版本**: 5.2.6

```bash
# 配置项目（目标为ESP32-S3）
idf.py set-target esp32s3

# 编译项目
idf.py build

# 只编译特定组件
idf.py build components/BSP/I2C
idf.py build components/Middlewares/OLED

# 烧录到设备
idf.py -p COMX flash

# 监控串口输出
idf.py -p COMX monitor

# 清理构建
idf.py fullclean

# 清理并重新配置
idf.py fullclean && idf.py set-target esp32s3
```

## 代码架构

### 分层结构
- **main/** - 主程序入口，任务调度和初始化
- **components/BSP/** - 硬件抽象层驱动
  - I2C驱动 (`i2c_driver.c/h`) - 全局I2C总线管理
  - MQTT (`mqtt.c/h`, `onenet_token.c`) - MQTT客户端和配网
  - RTC (`rtc_driver.c/h`) - RTC时钟
  - 蜂鸣器 (`Buzzer.c/h`)
  - 按键 (`Key.c/h`)
- **components/Middlewares/** - 传感器和数据中间件
  - MAX30102 (`max30102.c/h`) - 心率/血氧监测
  - OLED (`OLED.c/h`, `OLED_Data.c/h`) - 显示界面
  - MPU6050 (`MPU6050.c/h`) - 加速度计/陀螺仪
  - MessageQueue (`MessageQueue.c/h`) - 任务间通信
  - GetBatteryLevel (`GetBaLevel.c/h`) - 电池电量获取

### 组件依赖关系
```
main (REQUIRES: BSP, Middlewares)
  ├── BSP (REQUIRES: driver, esp_wifi, esp_netif, esp_event, nvs_flash,
  │         mqtt, wifi_provisioning, bt, protocomm, esp_http_server, lwip, json, spiffs)
  │   └── Middlewares (REQUIRES: driver, BSP, esp_adc, esp_timer)
  │       ├── MAX30102, OLED, MPU6050, MessageQueue, GetBatteryLevel
  └── Middlewares (REQUIRES: BSP)
```

### 核心任务架构（多核双核运行）

**Core 0**:
- `Task_MQTT_Message_Handler` (优先级3) - MQTT消息处理、连接管理
- `Task_Buzzer` (优先级2) - 蜂鸣器控制、报警触发

**Core 1**:
- `Task_Max30102_Monitor` (优先级5) - MAX30102心率/血氧监测（50Hz）
- `Task_Mpu6050_Monitor` (优先级5) - MPU6050加速度计/陀螺仪监测
- `Task_OLED_Show` (优先级2) - OLED显示更新（交替刷新心率/血氧或默认界面）

### 数据流
传感器数据 → MessageQueue → 对应处理任务 → 更新显示/发布MQTT

### 调试任务优先级
查看任务运行状态：`idf.py -p COMX monitor` 过滤查看 `Task_` 开头的任务，或使用：
- ESP-IDF菜单配置：`idf.py menuconfig` → `Component config` → `FreeRTOS` → `Task and Queue` 配置

### 切换调试模式
若需查看详细日志，可在`sdkconfig`中调整日志级别（默认6=INFO）：
```bash
idf.py menuconfig
# Component config → Log output → Default log verbosity → Debug (7)
```

## 关键设计

### I2C总线
全局单一I2C总线实例，由`i2c_driver`管理，所有I2C设备共享。初始化必须在`app_main`最前面。

### 消息队列
使用FreeRTOS队列在任务间传递传感器数据：
- `MESSAGE_TYPE_HEART_RATE_SPO2` - 心率/血氧
- `MESSAGE_TYPE_ACCELEROMETER` - 加速度计
- `MESSAGE_TYPE_GYROSCOPE` - 陀螺仪
- `MESSAGE_TYPE_ALERT` - 预警消息

### NVS配置存储
WiFi凭据和MQTT配置存储在NVS的`watch_cfg`命名空间中。

### AP配网
如果无法连接wifi，长按key2进入ap配网

## 配置与调优

### 组件开发
添加新中间件组件到 `components/Middlewares/CMakeLists.txt`：
```cmake
set(src_dirs
    MAX30102/max30102.c
    OLED/OLED.c
    OLED/OLED_Data.c
    MPU6050/MPU6050.c
    MessageQueue/MessageQueue.c
    GetBatteryLevel/GetBaLevel.c
    NEW_COMPONENT/new_component.c)  # 添加新组件

set(include_dirs
    OLED
    MAX30102
    MPU6050
    MessageQueue
    GetBatteryLevel
    NEW_COMPONENT
    ./)
```

### MQTT配置三种方式

### MQTT配置三种方式
1. **修改代码默认值**（需要erase-flash）：在`mqtt.h`修改`DEFAULT_MQTT_USERNAME`和`DEFAULT_MQTT_PASSWORD`
2. **手机App动态修改**（推荐）：通过BLE配网的Custom Data端点发送`{"user":"xxx","pass":"xxx"}`
3. **修改OneNET Topic**：在`mqtt.h`修改`SENSOR_REPORT_TOPIC`

参考：`components/BSP/MQTT/mqttUse.md`

### MPU6050检测算法优化
原算法窗口期较长（200采样点=4秒），建议：
- 引入`activity_score`（活动能量）和`high_peak_count`（高频峰值计数）
- 跌倒检测：看重max_mag（瞬间撞击力）
- 抽搐检测：看重activity_score（持续高频震动）
- 可调整参数：缓冲区大小、high_peak_count阈值、activity_score阈值

参考：`components/Middlewares/MPU6050/Detect.md`

### 函数调用 vs 任务通知性能对比
- **函数调用**：纳秒级（几十时钟周期），用于周期性运行时获取状态
- **任务通知**：微秒级（内核调度开销），用于第一时间对状态变化做出反应
- 跨核访问使用原子操作/硬件信号量

## 数据手册位置
- 项目根目录/DataSheet/癫痫只能监测手环.docx
- 项目根目录/DataSheet/MAX30102.pdf
- 项目根目录/DataSheet/PS_MPU6050.pdf
- 项目根目录/DataSheet/RM_MPU6050.pdf
- 项目根目录/DataSheet/中景园电子0.96OLED显示屏_驱动芯片手册.pdf

## 代码规范
- 全局变量：驼峰+下划线命名（如`g_i2c_handle`）
- 常量：全大写（如`MAX_BUFFER_SIZE`）
- 局部变量：首单词小写，后续首字母大写（如`sensorData`）
- 函数：驼峰+下划线命名（如`GetHeartRate`）
- 有大括号时，左括号和右括号各占一行


### 进入深度睡眠+ULP协处理器
长按key1或者电池电量低于40%-进入深度睡眠+启动ULP
ULP里面处理max30102和mpu6050的数据，如果异常（这个异常是怎么判断：心率连续3次过高/过低，mpu6050连续3次监测到异常运动），唤醒主cpu，传送异常数据到主cpu，然后主cpu处理后发送到onenet平台；当电池电量大于等于85的时候也会唤醒主CPU
