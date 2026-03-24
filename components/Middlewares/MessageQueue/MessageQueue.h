#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// 消息队列定义
#define OLED_QUEUE_LENGTH 10
#define OLED_QUEUE_ITEM_SIZE sizeof(char[40])

// 消息类型枚举
typedef enum {
    MESSAGE_TYPE_HEART_RATE = 0,
    MESSAGE_TYPE_SPO2,
    MESSAGE_TYPE_ACCELEROMETER,
    MESSAGE_TYPE_GYROSCOPE,
    MESSAGE_TYPE_ALERT
} Message_Type_t;

// 心率血氧数据结构体
typedef struct {
    uint32_t Heart_Rate;           // 心率值 (bpm)
    uint32_t SpO2;                 // 血氧饱和度 (%)
    uint32_t Baseline;             // 基准心率
    bool Warning_Active;           // 预警状态
} Heart_Rate_Data_t;

// 加速度计数据结构体
typedef struct {
    int16_t Accel_X;               // X轴加速度
    int16_t Accel_Y;               // Y轴加速度
    int16_t Accel_Z;               // Z轴加速度
} Accelerometer_Data_t;

// 陀螺仪数据结构体
typedef struct {
    int16_t Gyro_X;                // X轴角速度
    int16_t Gyro_Y;                // Y轴角速度
    int16_t Gyro_Z;                // Z轴角速度
} Gyroscope_Data_t;

// 预警消息结构体
typedef struct {
    bool Fall_Detected;            // 跌倒检测
    bool Convulsion_Detected;      // 抽搐检测
    bool Heart_Rate_Warning;       // 心率预警
} Alert_Data_t;

// 通用消息结构体
typedef struct {
    Message_Type_t Message_Type;   // 消息类型
    uint32_t Timestamp;            // 时间戳
    union {
        Heart_Rate_Data_t Heart_Rate_Data;
        Accelerometer_Data_t Accelerometer_Data;
        Gyroscope_Data_t Gyroscope_Data;
        Alert_Data_t Alert_Data;
    } Data;
} Sensor_Message_t;

// 消息类型枚举
enum {
    MSG_TYPE_NORMAL = 0,
    MSG_TYPE_HEART_RATE_WARNING,
    MSG_TYPE_FALL_DETECTED
};

// 队列类型枚举
enum QueueType {
    QUEUE_TYPE_MQTT = 0,
    QUEUE_TYPE_OLED = 1,
};

// 队列句柄声明
extern QueueHandle_t Sensor_Message_Queue;

// API函数声明
bool Message_Queue_Init(void);
bool Message_Queue_Send_Heart_Rate(uint32_t heart_rate, uint32_t spo2, uint32_t baseline, bool warning_active);
bool Message_Queue_Send_Accelerometer(int16_t ax, int16_t ay, int16_t az);
bool Message_Queue_Send_Gyroscope(int16_t gx, int16_t gy, int16_t gz);
bool Message_Queue_Send_Alert(bool fall_detected, bool convulsion_detected, bool heart_rate_warning);
bool Message_Queue_Receive(QueueHandle_t queue_handle, Sensor_Message_t *message, TickType_t timeout);
QueueHandle_t Message_Queue_Get_Handle(enum QueueType queue_type);

#endif // MESSAGE_QUEUE_H