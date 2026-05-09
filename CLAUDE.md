# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

ESP32 嵌入式智能手表，用于癫痫发作前症状监测。硬件包括 MAX30102 心率血氧传感器、MPU6050 加速度计/陀螺仪、0.96 寸 OLED 显示屏、按键和蜂鸣器。

**项目规则**: 编码需参考项目根目录 `DataSheet` 中的硬件数据手册：癫痫发作监测手环.docx、MAX30102.pdf、PS_MPU6050.pdf、RM_MPU6050.pdf、中景园电子0.96OLED显示屏_驱动芯片手册.pdf

## 构建和烧录

```bash
# 设置 ESP-IDF 环境
source $HOME/esp/esp-idf/export.sh  # 或使用 idf.py

# 配置项目
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录
idf.py -p COMx flash monitor

# 清理
idf.py fullclean
```

## 架构概览

### 层次结构
- `components/Middlewares/` - 传感器驱动和中间件：OLED、MAX30102、MPU6050、MessageQueue、GetBatteryLevel
- `components/BSP/` - 硬件抽象层：I2C 总线、按键、蜂鸣器、RTC、MQTT

### 核心任务架构
- `Task_Max30102_Monitor` - MAX30102 心率/血氧数据采集任务
- `Task_Mpu6050_Monitor` - MPU6050 加速度计/陀螺仪数据采集任务
- `Task_MQTT_Message_Handler` - MQTT 消息处理任务（数据上报）
- `Task_OLED_Show` - OLED 显示更新任务
- `Task_Buzzer` - 蜂鸣器控制任务

### 通信机制
使用消息队列在不同任务间传递传感器数据：
- `Sensor_Message_t` 包含多种消息类型：心率血氧、加速度计、陀螺仪、预警
- 各传感器任务向队列发送数据，MQTT 和 OLED 任务从队列读取

### I2C 总线架构
- 统一的 I2C 总线初始化（main.c），所有 I2C 设备共享同一总线句柄
- `I2C_PORT` = I2C_NUM_0，SDA=GPIO_20，SCL=GPIO_21，频率 400kHz
- 支持多个设备通过 `I2c_Add_Device()` 添加

## 组件结构

### Middlewares (传感器驱动)
- `MessageQueue/` - 消息队列管理，维护 MQTT 队列和 OLED 队列
- `OLED/` - OLED 显示驱动和显存管理
- `MAX30102/` - 心率血氧传感器驱动
- `MPU6050/` - 加速度计/陀螺仪驱动
- `GetBatteryLevel/` - 电池电量检测

### BSP (硬件抽象层)
- `I2C/` - I2C 总线管理，单例模式管理全局句柄
- `KEY/` - 按键驱动，轮询模式
- `Buzzer/` - 蜂鸣器驱动
- `RTC/` - RTC 时间驱动
- `MQTT/` - MQTT 通信，支持 BLE 配网和 NVS 凭据存储

## 关键组件说明

### MessageQueue
- 单例模式，维护两个队列：MQTT 队列和 OLED 队列
- 传感器任务根据消息类型分发给不同队列
- OLED 任务和 MQTT 任务从各自队列读取处理

### MQTT
- 使用 WiFi Provisioning BLE 配网（`BLE_PROV_SERVICE_NAME = "PROV_EpiWatch"`）
- NVS 存储凭据（namespace: `watch_cfg`），首次启动后自动使用
- MQTT Broker: `mqtts.heclouds.com:1883`

### MPU6050
- 50Hz 采样率，使用环形缓冲区进行跌倒和抽搐检测
- 检测算法：`Mpu6050_Detect_Fall_Or_Convulsion()`

### OLED
- 8x128 显存，显示通过修改显存再调用 `OLED_Update()` 同步
- 支持 ASCII 字符、数字、字符串、绘图函数
- 通过 `OLED_Notify_Show()` 通知 OLED 任务切换显示状态

### 按键
- 单键（KEY_1），支持单击、长按
- 中断驱动：GPIO 任意沿触发中断，通过定时器处理去抖和状态机
- 长按切换 OLED 显示状态，单击关闭蜂鸣器
- 传入回调函数处理事件，传 NULL 则通过 `Key_Get()` 轮询

## 开发注意事项

1. **任务优先级**: MQTT 任务优先级 3，传感器任务 5，OLED 任务 2，蜂鸣器任务 2
2. **I2C 设备**: 所有传感器通过统一 I2C 总线，dev_addr 需参考数据手册
3. **NVS 初始化**: 必须在 WiFi、MQTT 凭据初始化前完成
4. **消息队列**: 发送失败时传感器任务会阻塞，确保队列有足够空间
5. **OLED 更新**: 直接修改显存后调用 `OLED_Update()` 同步，避免频繁调用
6. **ESP32-S3 核心**: 绑定到特定核心（0/1）以避免中断冲突
