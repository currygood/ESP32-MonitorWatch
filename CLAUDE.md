# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于ESP32的癫痫智能监测手环项目，用于监测发作前症状。项目采用分层架构：BSP层处理底层硬件驱动，Middlewares层提供传感器数据和中间件功能。

## 构建与开发命令

```bash
# 配置项目
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录到设备
idf.py -p COMX flash

# 监控串口输出
idf.py -p COMX monitor

# 清理构建
idf.py fullclean
```

## 代码架构

### 分层结构
- **main/** - 主程序入口，任务调度和初始化
- **components/BSP/** - 硬件抽象层驱动
  - I2C驱动 (`i2c_driver.c/h`) - 全局I2C总线管理
  - MQTT (`mqtt.c/h`) - MQTT客户端和配网
  - RTC (`rtc_driver.c/h`) - RTC时钟
  - 蜂鸣器 (`Buzzer.c/h`)
  - 按键 (`Key.c/h`)
- **components/Middlewares/** - 传感器和数据中间件
  - MAX30102 (`max30102.c/h`) - 心率/血氧监测
  - OLED (`OLED.c/h`, `OLED_Data.c/h`) - 显示界面
  - MPU6050 (`MPU6050.c/h`) - 加速度计/陀螺仪
  - MessageQueue (`MessageQueue.c/h`) - 任务间通信
  - GetBatteryLevel (`GetBaLevel.c/h`) - 电池电量获取

### 核心任务架构
- `Task_Max30102_Monitor` (Core 1) - MAX30102传感器监测
- `Task_Mpu6050_Monitor` (Core 1) - MPU6050传感器监测
- `Task_OLED_Show` (Core 1) - OLED显示更新
- `Task_MQTT_Message_Handler` (Core 0) - MQTT消息处理
- `Task_Buzzer` (Core 0) - 蜂鸣器控制

### 数据流
传感器数据 → MessageQueue → 对应处理任务 → 更新显示/发布MQTT

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

### BLE配网
通过ESP-BLE-PROVISIONING进行首次配网，完成后凭证持久化到NVS。
