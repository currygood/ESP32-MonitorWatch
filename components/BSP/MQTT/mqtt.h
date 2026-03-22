#ifndef __MQTT_H__
#define __MQTT_H__

#include "esp_err.h"
#include "MessageQueue.h"
#include "freertos/task.h"
#include "time.h"
#include "stdlib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_sntp.h"

// WiFi配置
// 建议：将以下敏感信息存储在 NVS 或通过环境变量传递，避免明文存储
#define WIFI_SSID      "RedmiK70"
#define WIFI_PASS      "88888888"

// OneNET MQTT配置 - 手机热点环境下使用普通TCP连接
#define MQTT_HOST      "mqtts.heclouds.com" //"mqtt://mqtts.heclouds.com"  // OneNET MQTT普通TCP地址
#define MQTT_PORT      1883            // 普通TCP端口
#define MQTT_CLIENT_ID      "ESP32"
#define MQTT_USERNAME       "6wam422raC"
#define MQTT_PASSWORD      "version=2018-10-31&res=products%2F6wam422raC%2Fdevices%2FESP32&et=2082844800&method=md5&sign=IgVnG5HERafYrquMPe9D7Q%3D%3D"

// 传感器数据上报主题
#define SENSOR_REPORT_TOPIC "$sys/6wam422raC/ESP32/thing/property/post"

// 癫痫监测传感器数据结构
typedef struct {
    int heart_rate;                // 心率 (次/分钟) 癫痫发作时可能异常
    int oxygen_saturation;         // 血氧饱和度 (%) 癫痫发作时可能降低
    int seizure_risk_level;        // 癫痫发作风险等级 0-100
    int abnormal_motion_detected;  // 异常运动检测标志 0/1
    int timestamp;                 // 时间戳
} sensor_data_t;

esp_err_t Wifi_Init(void);
esp_err_t MQTT_App_Start(void);
esp_err_t MQTT_Publish(const char *topic, const char *data, int len);

// 传感器数据相关函数
void generate_sensor_data(sensor_data_t *data);


// MQTT消息处理任务函数
esp_err_t Task_MQTT_Message_Handler(void *pvParameters);

#endif
