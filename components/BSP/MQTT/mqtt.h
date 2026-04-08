#ifndef __MQTT_H__
#define __MQTT_H__

#include "esp_err.h"
#include "MessageQueue.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mqtt_client.h"
#include "esp_sntp.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "time.h"
#include "stdlib.h"
#include "string.h"
#include "stdbool.h"

// ============================================================
// 后备默认凭据
// 仅当 NVS 中无任何配置且 BLE 配网超时时作为兜底使用。
// 正常流程：首次启动通过 BLE 配网写入 NVS，之后不再使用这些宏。
// ============================================================
#define DEFAULT_WIFI_SSID       "RedmiK70"
#define DEFAULT_WIFI_PASS       "88888888"
#define DEFAULT_MQTT_USERNAME   "1nF1D22kt0"
#define DEFAULT_MQTT_PASSWORD   "version=2018-10-31&res=products%2F1nF1D22kt0%2Fdevices%2FMyTest&et=2091187496&method=md5&sign=VuvjYUj0KPTQ4e8zOJeyOw%3D%3D"

// ============================================================
// MQTT Broker 固定配置（与账号无关，不存入 NVS）
// ============================================================
#define MQTT_HOST               "mqtts.heclouds.com"
#define MQTT_PORT               1883
#define MQTT_CLIENT_ID          "MyTest"    // 可自定义，通常与设备 ID 相关
#define SENSOR_REPORT_TOPIC     "$sys/1nF1D22kt0/MyTest/thing/property/post"

// ============================================================
// NVS 存储配置
// ============================================================
#define NVS_NAMESPACE           "watch_cfg"        // ≤15字节限制
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_MQTT_USER       "mqtt_user"
#define NVS_KEY_MQTT_PASS       "mqtt_pass"
#define NVS_KEY_PROV_DONE       "prov_done"        // 配网完成标志位（uint8）

// ============================================================
// BLE 配网配置
// ============================================================
// 手机 App（Espressif BLE Provisioning）扫描时显示的设备名
#define BLE_PROV_SERVICE_NAME   "PROV_EpiWatch"
// Proof of Possession 安全码（防止陌生人修改手表配置）
#define BLE_PROV_POP            "watch1234"
// 自定义 BLE 端点名：用于在配网时额外下发 MQTT 凭据
#define BLE_PROV_ENDPOINT_MQTT  "custom-mqtt"
// BLE 配网等待超时（毫秒）：3 分钟
#define BLE_PROV_TIMEOUT_MS     (3 * 60 * 1000)

// ============================================================
// 凭据缓冲区大小
// ============================================================
#define CRED_SSID_MAX_LEN       64
#define CRED_PASS_MAX_LEN       128
#define CRED_MQTT_USER_MAX_LEN  64
#define CRED_MQTT_PASS_MAX_LEN  256

// ============================================================
// 癫痫监测传感器数据结构
// ============================================================
typedef struct {
    int heart_rate;                // 心率 (次/分钟)
    int oxygen_saturation;         // 血氧饱和度 (%)
    int seizure_risk_level;        // 癫痫发作风险等级 0-100
    int abnormal_motion_detected;  // 异常运动检测标志 0/1
    int timestamp;                 // Unix 时间戳
} sensor_data_t;

// ============================================================
// 核心功能函数声明
// ============================================================
esp_err_t Wifi_Init(void);
esp_err_t MQTT_App_Start(void);
esp_err_t MQTT_Publish(const char *topic, const char *data, int len);
void      generate_sensor_data(sensor_data_t *data);

// MQTT 消息处理主任务（在 main.c 中通过 xTaskCreate 创建）
void Task_MQTT_Message_Handler(void *pvParameters);

// ============================================================
// NVS 凭据管理 API（可供 OLED 菜单等模块调用）
// ============================================================
esp_err_t NVS_Save_Wifi_Credentials(const char *ssid, const char *pass);
esp_err_t NVS_Load_Wifi_Credentials(char *ssid, size_t ssid_len,
                                     char *pass, size_t pass_len);
esp_err_t NVS_Save_MQTT_Credentials(const char *username, const char *password);
esp_err_t NVS_Load_MQTT_Credentials(char *username, size_t user_len,
                                     char *password, size_t pass_len);
bool      NVS_Has_Wifi_Credentials(void);
esp_err_t NVS_Clear_All_Credentials(void);  // 清除全部配置（重置手表）

// ============================================================
// BLE 配网管理 API（可供 OLED 设置菜单调用）
// ============================================================
// 清除 NVS 凭据并重启，下次启动自动进入 BLE 配网模式
esp_err_t BLE_Provisioning_Reset(void);

#endif // __MQTT_H__