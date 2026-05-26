# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于ESP32的癫痫前期症状监测报警手表，实时性要求极高，用于监测发作前症状并立即报警。项目采用分层架构：BSP层处理底层硬件驱动，Middlewares层提供传感器数据和中间件功能。

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

# 烧录到设备
idf.py -p COMX flash

# 监控串口输出
idf.py -p COMX monitor

# 清理构建
idf.py fullclean && idf.py set-target esp32s3
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
- **components/Middlewares/** - 传感器和数据中间件
  - MAX30102 (`max30102.c/h`) - 心率/血氧监测
  - OLED (`OLED.c/h`, `OLED_Data.c/h`) - 显示界面
  - MPU6050 (`MPU6050.c/h`, `Detect.md`) - 加速度计/陀螺仪（含改进检测算法）
  - MessageQueue (`MessageQueue.c/h`) - 任务间通信
  - GetBatteryLevel (`GetBaLevel.c/h`) - 电池电量获取

### 核心任务架构（双核运行）

**Core 0**:
- `Task_MQTT_Message_Handler` (优先级3) - MQTT消息处理、连接管理
- `Task_Buzzer` (优先级2) - 蜂鸣器控制、报警触发

**Core 1**:
- `Task_Max30102_Monitor` (优先级5) - MAX30102心率/血氧监测（50Hz）
- `Task_Mpu6050_Monitor` (优先级5) - MPU6050加速度计/陀螺仪监测
- `Task_OLED_Show` (优先级2) - OLED显示更新（交替刷新心率/血氧或默认界面）

### 数据流
传感器数据 → MessageQueue → 对应处理任务 → 更新显示/发布MQTT

### ULP 深度睡眠流程
长按key1或电池电量<40% → 启动深度睡眠+ULP协处理器
- ULP协处理器采样传感器数据（100个样本）
- 心率异常：连续2次过高/过低 → 唤醒主CPU → 主CPU二次确认 → 报警
- 跌倒异常：连续1次检测到剧烈撞击（加速度>4900²=g²）→ 唤醒主CPU → 确认跌倒/抽搐 → 报警
- 电池电量<40% → 唤醒主CPU
- 异常数据通过共享缓冲区传回主CPU处理

### ULP与主CPU共享变量
ULP程序使用软件I2C直接操作传感器，共享变量定义在`ulp_main.c`中：
- `shared_ir_buf[100]` - 红外光强度数据
- `shared_red_buf[100]` - 红光强度数据
- `shared_ax_buf[100]` - X轴加速度
- `shared_ay_buf[100]` - Y轴加速度
- `shared_az_buf[100]` - Z轴加速度
- `wakeup_reason` - 唤醒原因：0=无，1=心率异常，2=跌倒

**重要**：主CPU代码中引用变量时使用`ulp_shared_*`前缀，但实际定义在ULP程序中。需要在`ulp_main.h`中正确声明这些全局变量以供主CPU访问。

### ULP启动竞争问题
有一些bug可以从note文件夹中的笔记中可以找到解决方案，找不到的请阅读整个项目代码文件分析


## 配置与调优

### 组件开发
添加新中间件组件到 `components/Middlewares/CMakeLists.txt`：
```cmake
set(src_dirs NEW_COMPONENT/new_component.c)
set(include_dirs NEW_COMPONENT)
set(requires BSP)  # 大多数中间件依赖BSP
idf_component_register(SRCS "${src_dirs}" INCLUDE_DIRS "${include_dirs}" REQUIRES ${requires})
```

### MQTT配置三种方式
1. **修改代码默认值**（需erase-flash）：在`mqtt.h`修改`DEFAULT_MQTT_USERNAME`和`DEFAULT_MQTT_PASSWORD`
2. **手机App动态修改**（推荐）：通过AP配网网页上传凭据，保存到NVS
3. **修改OneNET Topic**：在`mqtt.h`修改`SENSOR_REPORT_TOPIC`

AP配网触发：长按key2进入AP配网网页（3分钟内），可上传WiFi和MQTT凭据
AP配网页面地址：http://192.168.4.1

### MPU6050检测算法优化
当前ULP使用加速度平方阈值检测跌倒：`FALL_THRESHOLD_SQ = 24090976`（即加速度>4900²=g²）
- 心率检测：正常心率60-120bpm应在2.5秒内有2-5次跳动
- 跌倒检测：单次加速度平方超过阈值即触发（连续1次）

参数调整可降低灵敏度以提高可靠性：调整`FALL_THRESHOLD_SQ`减小阈值

### 函数调用 vs 任务通知性能对比
- **函数调用**：纳秒级，用于周期性运行时获取状态
- **任务通知**：微秒级，用于第一时间对状态变化做出反应
- 跨核访问使用原子操作/硬件信号量

## 关键设计

### I2C总线
全局单一I2C总线实例，由`i2c_driver`管理，所有I2C设备共享。主CPU的I2C初始化必须在`app_main`最前面。

### ULP软件I2C
ULP协处理器使用GPIO模拟I2C总线进行传感器通信，与主CPU的硬件I2C独立运行。

### 消息队列
使用FreeRTOS队列在任务间传递传感器数据：
- `MESSAGE_TYPE_HEART_RATE_SPO2` - 心率/血氧
- `MESSAGE_TYPE_ACCELEROMETER` - 加速度计
- `MESSAGE_TYPE_GYROSCOPE` - 陀螺仪
- `MESSAGE_TYPE_ALERT` - 预警消息

### NVS配置存储
WiFi凭据和MQTT配置存储在NVS的`watch_cfg`命名空间中。

## 代码规范

### 命名约定
- **全局变量**：驼峰+下划线（如`g_i2c_handle`）
- **常量**：全大写（如`MAX_BUFFER_SIZE`）
- **局部变量**：首单词小写，后续首字母大写（如`sensorData`）
- **函数**：驼峰+下划线（如`GetHeartRate`）
- **枚举**：全大写下划线（如`KEY_EVENT_LONG_PRESS`）

### 重要已知问题
1. **ULP第二次唤醒问题**：第二次进入深度睡眠+ULP处理时无法正确退出主CPU的循环（参考`note/2026-05-24.md`）
2. **ULP启动竞争**：主CPU读取到的`wakeup_reason`可能为0的根本原因是ULP在主CPU启动期间重新运行并清零（已给出解决方案，见"ULP启动竞争问题"部分）

### 大括号规则
所有有括号的结构（if/for/while/函数）左括号和右括号各占一行
```c
if(condition)
{
    // code
}
```

### 数据手册位置
- `DataSheet/癫痫只能监测手环.docx`
- `DataSheet/MAX30102.pdf`
- `DataSheet/PS_MPU6050.pdf`
- `DataSheet/RM_MPU6050.pdf`
- `DataSheet/中景园电子0.96OLED显示屏_驱动芯片手册.pdf`
