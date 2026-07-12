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
#define DEFAULT_MQTT_CLIENT_ID 	"MyTest"
#define DEFUALT_MQTT_KEY		"cFJaTlc4UkNzbnhBdG5QajVuVko0U3JTMlFUZm5Sb2E="
// TODO: 请填入你的 OneNET 产品访问密钥 (Product Access Key)
// 获取位置: OneNET 控制台 -> 产品详情 -> 产品概况 -> AccessKey
// 注意: 这是产品级别的密钥，不是设备密钥！
#define DEFAULT_ONENET_PRODUCT_ACCESS_KEY "3S1H19uDdRWLXVslxembo2+P0RWTXcXB56txJWnhA9c="


#define ONENE_OTA_URL_FORMAT "http://iot-api.heclouds.com/fuse-ota/"


// onenet 平台默认主题
#define DEFAULT_SEND_TOPIC		"$sys/1nF1D22kt0/MyTest/thing/property/post"
#define DEFAULT_RECVV_SET_TOPIC "$sys/1nF1D22kt0/MyTest/thing/property/set"
#define DEFAULT_OTA_TOPIC		"$sys/1nF1D22kt0/MyTest/ota/inform"

// ============================================================
// MQTT Broker 固定配置（与账号无关，不存入 NVS）
// ============================================================
#define MQTT_HOST               "mqtts.heclouds.com"
#define MQTT_PORT               1883

// ============================================================
// NVS 存储配置
// ============================================================
#define NVS_NAMESPACE           	"watch_cfg"        // ≤15字节限制
#define NVS_WIFI_SSID       		"wifi_ssid"
#define NVS_WIFI_PASS       		"wifi_pass"
#define NVS_MQTT_USER       		"mqtt_user"
#define NVS_MQTT_PASS       		"mqtt_pass"
#define NVS_MQTT_CLIENT_ID  		"mqtt_client_id"
#define NVS_MQTT_KEY       			"mqtt_key"
#define NVS_MQTT_PRODUCT_ACCESS_KEY 	"mqtt_pa_key"
#define NVS_PROV_DONE       		"prov_done"        // 配网完成标志位（uint8）

// ============================================================
// 凭据缓冲区大小
// ============================================================
#define CRED_SSID_MAX_LEN      			64
#define CRED_PASS_MAX_LEN       		128
#define CRED_MQTT_USER_MAX_LEN  		64
#define CRED_MQTT_PASS_MAX_LEN  		256
#define CRED_MQTT_CLIENT_ID_MAX_LEN		64
#define CRED_MQTT_KEY_MAX_LEN  			256
#define TOPIC_STR_SIZE			512
#define VERSION_STR_SIZE		32
#define PRODUCT_ACCESS_KEY_SIZE	256

// ============================================================
// AP 配网配置
// ============================================================
#define AP_SSID               "EpiWatch_AP"
#define AP_PASSWORD           "watch1234"
typedef enum {
    AP_Enter_Provision = 1,   // 进入 AP 配网模式的事件
	AP_Provision_Complete = 2, // AP 配网完成的事件
	OTA_Upgrade_Requested = 3  // 收到 OTA 升级请求事件
}AP_Provision_Event_t;

typedef void (*p_wifi_state_callback)(bool connected); // WiFi 状态回调函数类型
typedef void (*p_wifi_scan_callback)(int num,wifi_ap_record_t *ap_records); // WiFi 扫描结果回调函数类型
typedef void(*ws_receive_cb)(uint8_t* payload,int len);			//ws接收到的处理回调函数

typedef struct
{
    const char* html_code;              //当执行http访问时返回的html页面
    ws_receive_cb   receive_fn;         //当ws接收到数据时，调用此函数
}ws_cfg_t;

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
uint8_t Wifi_Init(void);
esp_err_t MQTT_App_Start(uint8_t choice);
esp_err_t MQTT_Publish(const char *topic, const char *data, int len);
esp_mqtt_client_handle_t MQTT_Give();
// ============================================================
// NVS 凭据管理 API（可供 OLED 菜单等模块调用）
// ============================================================
esp_err_t NVS_Save_Wifi_Credentials(const char *ssid, const char *pass);
esp_err_t NVS_Load_Wifi_Credentials(char *ssid, size_t ssid_len,
                                     char *pass, size_t pass_len);
esp_err_t NVS_Save_MQTT_Credentials(const char *username, const char *client_id,const char* key,const char* product_access_key);
esp_err_t NVS_Load_MQTT_Credentials(char *username, size_t user_len,
                                     char *client_id, size_t client_id_len,
                                     char *key, size_t key_len,
									 char *product_access_key, size_t product_access_key_len);
bool      NVS_Has_Wifi_Credentials(void);
esp_err_t NVS_Clear_All_Credentials(void);  // 清除全部配置（重置手表）
int Calculate_Risk_Level(uint32_t hr, uint32_t spo2, bool abnormal_motion_detected);// 癫痫发作风险等级计算函数
// ============================================================
// WiFi 连接状态查询 API（供 OLED 等模块使用）
// ============================================================
/** 
 * @brief 是否已经连接了路由器
 * @param 无
 * @return 是/否
*/
bool wifi_manager_is_connect(void);

/***
 * @brief 是否连接onenet平台
 * @param 无
 * @return true/false 是否
*/
bool MQTT_Is_Connected(void);

// MQTT 消息处理主任务（在 main.c 中通过 xTaskCreate 创建）
void Task_MQTT_Message_Handler(void *pvParameters);

// ============================================================
// MQTT 主题相关 API（供其他模块调用）
// ============================================================
void MQTT_Get_SendTopic(char* topic);
void MQTT_Get_RecvSetTopic(char* topic);
void MQTT_Get_OTATopic(char* topic);

#endif // __MQTT_H__