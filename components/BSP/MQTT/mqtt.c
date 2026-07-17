#include "mqtt.h"
#include "MPU6050.h"
#include "max30102.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/ip4_addr.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "cJSON.h"
#include "esp_spiffs.h"
#include "http_parser.h"
#include "esp_http_server.h"
#include "onenet_token.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#define WIFI_CONNECTED_BIT      BIT0
#define MQTT_CONNECTED_BIT      BIT1
#define APCFG_BIT 				BIT2
#define OTA_BUFFER_SIZE 			1024

static const char *TAG = "MQTT";
static TaskHandle_t MQTT_Task_Handle = NULL;
// ============================================================
// 模块内部状态
// ============================================================
static EventGroupHandle_t    wifi_event_group      = NULL;
static esp_mqtt_client_handle_t mqtt_client        = NULL;

static bool  mqtt_connected       = false;
static int   mqtt_error_count     = 0;
static int   publish_error_count  = 0;
static int   publish_success_count = 0;


#define MAX_RETRY_COUNT         5
static int   retry_count = 0;

static const int ERROR_LOG_INTERVAL   = 10;
static const int SUCCESS_LOG_INTERVAL = 10;

// ============================================================
// AP配网相关变量
// ============================================================
static esp_netif_t *esp_netif_sta = NULL;
static esp_netif_t *esp_netif_ap = NULL;
//回调函数
static p_wifi_state_callback    wifi_state_cb = NULL;
//当前sta连接状态
static bool is_sta_connected = false;
//html网页在spiffs文件系统中的路径
#define INDEX_HTML_PATH "/spiffs/apcfg.html"
//html网页缓存
static char* index_html = NULL;
//配网事件
static EventGroupHandle_t   apcfg_event = NULL;
//接收到ap配网的ssid和密码
static char current_ssid[CRED_SSID_MAX_LEN] = {0};
static char current_password[CRED_PASS_MAX_LEN] = {0};
//onenet用户名，client_id，key
static char MQTT_username[CRED_MQTT_USER_MAX_LEN] = {0};
static char MQTT_client_id[CRED_MQTT_CLIENT_ID_MAX_LEN] = {0};
static char MQTT_device_key[CRED_MQTT_KEY_MAX_LEN] = {0};
//html页面
static const char* http_html = NULL;
//接收回调函数
static ws_receive_cb  ws_receive_fn = NULL;
//http服务器句柄
httpd_handle_t http_ws_server = NULL;
//连接的客户端fds
static int client_sockfd = -1;
static TaskHandle_t Scan_Task_Handle = NULL;
static TaskHandle_t AP_Task_Handle = NULL;

// Onenet平台相关变量
char SendTopic[TOPIC_STR_SIZE] = {0};
char RecvSetTopic[TOPIC_STR_SIZE] = {0};
char OTATopic[TOPIC_STR_SIZE] = {0};
static char target_version[VERSION_STR_SIZE] = {0};
static char onenet_product_access_key[PRODUCT_ACCESS_KEY_SIZE] = {0};
static int task_id = 0;
static bool ota_task_running = false;
static SemaphoreHandle_t ota_mutex = NULL;

typedef struct {
    char buffer[OTA_BUFFER_SIZE];
    int data_size;
} ota_http_context_t;

// 函数前向声明
esp_err_t onenet_ota_upload_version(void);
void set_app_valid(int valid);
void onenet_ota_update(void);
static void mqtt_onenet_subscribe(void);
static esp_err_t MQTT_Solve_OnenetMessage(cJSON *json, const char *topic);
static esp_err_t MQTT_Onenet_Ack(const char *id,int code,const char* msg);
static esp_err_t MQTT_Onenet_OTA_Ack(const char *id,int code,const char* msg);

void MQTT_Get_SendTopic(char* topic)
{
	strncpy(topic, SendTopic, sizeof(topic));
}

void MQTT_Get_RecvSetTopic(char* topic)
{
	strncpy(topic, RecvSetTopic, sizeof(topic));
}

void MQTT_Get_OTATopic(char* topic)
{
	strncpy(topic, OTATopic, sizeof(topic));
}

esp_mqtt_client_handle_t MQTT_Give()
{
	return mqtt_client;
}

static const char *get_product_access_key(void)
{
    if (onenet_product_access_key[0] != '\0') {
        return onenet_product_access_key;
    }
    return DEFAULT_ONENET_PRODUCT_ACCESS_KEY;
}

// ============================================================
// ①  NVS 凭据管理
// ============================================================

/**
 * @brief 将 WiFi SSID/密码写入 NVS
 */
esp_err_t NVS_Save_Wifi_Credentials(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 打开失败: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_WIFI_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_WIFI_PASS, pass);
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✅ WiFi 凭据已保存到 NVS  SSID: %s", ssid);
    } else {
        ESP_LOGE(TAG, "❌ WiFi 凭据保存失败: %s", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief 从 NVS 读取 WiFi SSID/密码
 */
esp_err_t NVS_Load_Wifi_Credentials(char *ssid, size_t ssid_len,
                                     char *pass, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(handle, NVS_WIFI_SSID, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, NVS_WIFI_PASS, pass, &pass_len);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief 将 MQTT 用户名/密码 client_id 写入 NVS
 * @brief 将 MQTT 用户名/密码 client_id 写入 NVS
 */
esp_err_t NVS_Save_MQTT_Credentials(const char *username, const char *client_id,const char* key,const char* product_access_key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_MQTT_USER, username);
	if (err == ESP_OK) err = nvs_set_str(handle, NVS_MQTT_CLIENT_ID, client_id);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_MQTT_KEY, key);
	if (err == ESP_OK) err = nvs_set_str(handle, NVS_MQTT_PRODUCT_ACCESS_KEY, product_access_key);
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✅ MQTT 凭据已保存到 NVS  User: %s", username);
    } else {
        ESP_LOGE(TAG, "❌ MQTT 凭据保存失败: %s", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief 从 NVS 读取 MQTT 用户名/密码 client_id
 * @brief 从 NVS 读取 MQTT 用户名/密码 client_id
 */
esp_err_t NVS_Load_MQTT_Credentials(char *username, size_t user_len,
                                     char *client_id, size_t client_id_len,
                                     char *key, size_t key_len,
									 char *product_access_key, size_t product_access_key_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(handle, NVS_MQTT_USER, username, &user_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, NVS_MQTT_CLIENT_ID, client_id, &client_id_len);
    }
    if (err == ESP_OK) {
        err = nvs_get_str(handle, NVS_MQTT_KEY, key, &key_len);
    }
	if (err == ESP_OK) {
        err = nvs_get_str(handle, NVS_MQTT_PRODUCT_ACCESS_KEY, product_access_key, &product_access_key_len);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief 检查 NVS 中是否已存有 WiFi 凭据
*/
bool NVS_Has_Wifi_Credentials(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    uint8_t done = 0;
    esp_err_t err = nvs_get_u8(handle, NVS_PROV_DONE, &done);
    nvs_close(handle);
    return (err == ESP_OK && done == 1);
}

/**
 * @brief 清除 NVS 中全部手表配置（WiFi + MQTT 凭据）
 */
esp_err_t NVS_Clear_All_Credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_erase_key(handle, NVS_WIFI_SSID);
    nvs_erase_key(handle, NVS_WIFI_PASS);
    nvs_erase_key(handle, NVS_MQTT_USER);
	nvs_erase_key(handle, NVS_MQTT_CLIENT_ID);
    nvs_erase_key(handle, NVS_MQTT_KEY);
	nvs_erase_key(handle, NVS_MQTT_PRODUCT_ACCESS_KEY);
    nvs_erase_key(handle, NVS_PROV_DONE);
    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGW(TAG, "🗑️ NVS 凭据已全部清除");
    return err;
}

// ============================================================
// ③  WiFi 与 MQTT 事件回调
// ============================================================

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        // ESP_LOGW(TAG, ">>> WiFi STA 启动，开始连接...");
        // esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < MAX_RETRY_COUNT) {
            ESP_LOGW(TAG, "WiFi 断开，第 %d/%d 次重试...", retry_count + 1, MAX_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_wifi_connect();
            retry_count++;
        } else {
            ESP_LOGE(TAG, "达到最大重试次数，WiFi 仍无法连接");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        retry_count = 0;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGW(TAG, ">>> WiFi 连接成功！IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (wifi_event_group) {
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}




// ============================================================
// AP 配网相关事件处理（内部函数）
// ============================================================

/** 当其他设备WS访问时触发此回调函数
 * @param req http请求
 * @return ESP_OK or ESP_FAIL
*/
static esp_err_t handle_ws_req(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        //把套接字描述符保存下来，方便后续发送数据用
        client_sockfd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG,"Save client_fds:%d",client_sockfd);
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        return ret;
    }
    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
    {
        if(ws_receive_fn)
            ws_receive_fn(ws_pkt.payload,ws_pkt.len);
        free(buf);
    }
    return ESP_OK;
}

/** 当其他设备http HTTP_GET 访问时，返回html页面
 * @param req http请求
 * @return ESP_OK or ESP_FAIL
*/
esp_err_t get_req_handler(httpd_req_t *req)
{
    esp_err_t response = ESP_FAIL;
    if(http_html)
    {
        response = httpd_resp_send(req, http_html, HTTPD_RESP_USE_STRLEN);
    }
    return response;
}

esp_err_t   web_ws_send(uint8_t* data, int len)
{
    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(httpd_ws_frame_t));
    pkt.payload = data;
    pkt.len = len;
    pkt.type = HTTPD_WS_TYPE_TEXT;
    return httpd_ws_send_data(http_ws_server,client_sockfd,&pkt);
}

/** 初始化ws
 * @param cfg ws一些配置,请看ws_cfg_t定义
 * @return  ESP_OK or ESP_FAIL
*/
esp_err_t   web_ws_start(ws_cfg_t *cfg)
{
    if(cfg == NULL)
        return ESP_FAIL;
    http_html = cfg->html_code;
    ws_receive_fn = cfg->receive_fn;

    //http和websocket初始化
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t uri_get = 
    {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_req_handler,
    };
    httpd_uri_t ws = 
    {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .is_websocket = true
    };

    if (httpd_start(&http_ws_server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(http_ws_server, &uri_get);
        httpd_register_uri_handler(http_ws_server, &ws);
    }

    return ESP_OK;
}

esp_err_t   web_ws_stop(void)
{
    if(http_ws_server)
    {
        return httpd_stop(http_ws_server);
        http_ws_server = NULL;
    }
    return ESP_OK;
}


/** 事件回调函数
 * @param arg   用户传递的参数
 * @param event_base    事件类别
 * @param event_id      事件ID
 * @param event_data    事件携带的数据
 * @return 无
*/
static void ap_wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{   
    if(event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:      //WIFI以STA模式启动后触发此事件
        {
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if(mode == WIFI_MODE_STA)
                esp_wifi_connect();         //启动WIFI连接
            break;
        }
        case WIFI_EVENT_STA_CONNECTED:  //WIFI连上路由器后，触发此事件
            ESP_LOGI(TAG, "Connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:   //WIFI从路由器断开连接后触发此事件
            if(is_sta_connected)
            {
                if(wifi_state_cb)
                    wifi_state_cb(false);
                is_sta_connected = false;
            }
            if(retry_count < MAX_RETRY_COUNT)
            {
                wifi_mode_t mode;
                esp_wifi_get_mode(&mode);
                if(mode == WIFI_MODE_STA)
                    esp_wifi_connect();             //继续重连
                retry_count++;
            }
            ESP_LOGI(TAG,"connect to the AP fail,retry now");
            break;
        case WIFI_EVENT_AP_STACONNECTED:
        {
            //有设备连接了热点，把它的MAC打印出来
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
            ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d",
                    MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            //有设备断开了热点
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
            ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d",
                    MAC2STR(event->mac), event->aid);
            break;
        }
        default:
            break;
        }
    }
    if(event_base == IP_EVENT)                  //IP相关事件
    {
        switch(event_id)
        {
            case IP_EVENT_STA_GOT_IP:           //只有获取到路由器分配的IP，才认为是连上了路由器
                ESP_LOGI(TAG,"Get ip address");
                is_sta_connected = true;
                if(wifi_state_cb)
                    wifi_state_cb(true);
                break;
            default:break;
        }
    }
}

/** 进入ap+sta模式
 * @param 无
 * @return 成功/失败
*/
esp_err_t wifi_manager_ap(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    // if(mode == WIFI_MODE_APSTA) //需要使用AP+STA模式，才可以执行扫描同时保持客户端连接
    //     return ESP_OK;
    esp_wifi_disconnect();
    // esp_wifi_stop();  //已经在WIFI_Init中关掉了，
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_config_t wifi_config = 
    {
        .ap = 
        {
            .channel = 1,               //wifi的通信信道
            .max_connection = 2,        //最大连接数
            .authmode = WIFI_AUTH_WPA2_PSK, //加密方式
        }
    };
    //填充ap的ssid名称
    snprintf((char*)wifi_config.ap.ssid,31,"%s",AP_SSID);
    wifi_config.ap.ssid_len = strlen(AP_SSID);
    //填充密码
    snprintf((char*)wifi_config.ap.password,63,"%s",AP_PASSWORD);

    //设置wifi
    esp_wifi_set_config(WIFI_IF_AP,&wifi_config);

    //如果是AP模式，则需要设置如下网络层信息
    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192,168,100,1);    //本地的IP地址
	IP4_ADDR(&ipInfo.gw, 192,168,100,1);    //网关IP地址
	IP4_ADDR(&ipInfo.netmask, 255,255,255,0);   //子网掩码
	esp_netif_dhcps_stop(esp_netif_ap);
	esp_netif_set_ip_info(esp_netif_ap, &ipInfo);
	esp_netif_dhcps_start(esp_netif_ap);

    esp_wifi_start();
    return ESP_OK;
}

static SemaphoreHandle_t scan_sem = NULL;

/** 扫描任务
 * @param 无
 * @return 成功/失败
*/
static void scan_task(void* param)
{
    p_wifi_scan_callback callback = (p_wifi_scan_callback)param;
    uint16_t number = 20;
    wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t)*number);
    uint16_t ap_count = 0;
    ESP_LOGI(TAG,"Start wifi scan");
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);
    if(callback)
        callback(number,ap_info);
    xSemaphoreGive(scan_sem);
    vTaskDelete(NULL);
}

/** 启动扫描
 * @param 无
 * @return 成功/失败
*/
esp_err_t wifi_manager_scan(p_wifi_scan_callback f)
{
    if(!scan_sem)
    {
        scan_sem = xSemaphoreCreateBinary();
        xSemaphoreGive(scan_sem);
    }
    if(pdTRUE == xSemaphoreTake(scan_sem,0))
    {
        //清除上次的扫描信息
        esp_wifi_clear_ap_list();
        //启动一个扫描任务
        if(pdTRUE == xTaskCreatePinnedToCore(scan_task,"scan",8192,f,3,&Scan_Task_Handle,0))
            return ESP_OK;
    }
    return ESP_FAIL;
}

/** 连接wifi
 * @param ssid
 * @param password
 * @return 成功/失败
*/
esp_err_t wifi_manager_connect(const char* ssid,const char* password)
{
	if(strlen(ssid)>31 || strlen(password)>63)
        return ESP_FAIL;
    retry_count = 0;
    wifi_config_t wifi_config = 
    {
        .sta = 
        {
	        .threshold.authmode = WIFI_AUTH_WPA2_PSK,   //加密方式
        },
    };
	strncpy((char *)wifi_config.sta.ssid,ssid,sizeof(wifi_config.sta.ssid));
	strncpy((char *)wifi_config.sta.password,password,sizeof(wifi_config.sta.password));
    // snprintf((char*)wifi_config.sta.ssid,
    //                sizeof(wifi_config.sta.ssid),
    //                "%s", ssid);
    // snprintf((char*)wifi_config.sta.password,
    //                sizeof(wifi_config.sta.password),
    //                "%s", password);
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if(mode != WIFI_MODE_STA)
    {
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_start();
    }
    else
    {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();
    }
    return ESP_OK;
}

/** 是否已经连接了路由器
 * @param 无
 * @return 是/否
*/
bool wifi_manager_is_connect(void)
{
    return is_sta_connected;
}

/** 从spiffs中加载html页面到内存
 * @param 无
 * @return 无 
*/
static char* initi_web_page_buffer(void)
{
    //定义挂载点
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",            //挂载点
        .partition_label = "storage",     //分区名称
        .max_files = 5,                    //最大打开的文件数
        .format_if_mount_failed = false    //挂载失败是否执行格式化
        };
    //挂载spiffs  防止重复挂载
    if (!esp_spiffs_mounted("storage")) {
		ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
	}
    //查找文件是否存在
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st))
    {
        ESP_LOGE(TAG, "apcfg.html not found");
        return NULL;
    }
    //打开html文件并且读取到内存中
    char* page = (char*)malloc(st.st_size + 1);
    if(!page)
    {
        return NULL;
    }
    memset(page,0,st.st_size + 1);
    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (fread(page, st.st_size, 1, fp) == 0)
    {
        free(page);
        page = NULL;
        ESP_LOGE(TAG, "fread failed");
    }
    fclose(fp);
    return page;
}

/** wifi扫描完成
 * @param numbers 扫描到的ap个数
 * @param ap_records ap信息
 * @return 无 
*/
static void wifi_scan_finish_handle(int numbers,wifi_ap_record_t *ap_records)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* wifilist_js = cJSON_AddArrayToObject(root,"wifi_list");
    for(int i = 0;i < numbers;i++)
    {
        cJSON* wifi_js = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_js,"ssid",(char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(wifi_js,"rssi",ap_records[i].rssi);
        if(ap_records[i].authmode == WIFI_AUTH_OPEN)
            cJSON_AddBoolToObject(wifi_js,"encrypted",0);
        else
            cJSON_AddBoolToObject(wifi_js,"encrypted",1);
        cJSON_AddItemToArray(wifilist_js,wifi_js);
    }
    char* data = cJSON_Print(root);
    ESP_LOGI(TAG,"WS send:%s",data);
    web_ws_send((uint8_t*)data,strlen(data));
    cJSON_free(data);
    cJSON_Delete(root);
}

/** ws接收回调函数
 * @param payload 数据
 * @param len 数据长度
 * @return 无 
*/
static void ws_receive_handle(uint8_t* payload, int len)
{
    cJSON* root = cJSON_Parse((char*)payload);
    if(root)
    {
        // 1. 处理扫描请求
        cJSON* scan_js = cJSON_GetObjectItem(root, "scan");
        if(scan_js)
        {
            char* scan_value = cJSON_GetStringValue(scan_js);
            if(strcmp(scan_value, "start") == 0)
            {
                wifi_manager_scan(wifi_scan_finish_handle);
            }
        }

        // 2. 解析 WiFi 信息
        cJSON* ssid_js = cJSON_GetObjectItem(root, "ssid");
        cJSON* password_js = cJSON_GetObjectItem(root, "password");
        
        // 3. 解析 MQTT 信息
        cJSON* username_js = cJSON_GetObjectItem(root, "username");
        cJSON* client_id_js = cJSON_GetObjectItem(root, "client_id");
        cJSON* key_js = cJSON_GetObjectItem(root, "key");
        cJSON* product_key_js = cJSON_GetObjectItem(root, "onenet_product_access_key");

         // 只有当所有字段都存在时才进行保存和触发事件
		if(ssid_js && password_js && username_js && client_id_js && key_js)
		{
			// 拷贝 WiFi 信息
			snprintf(current_ssid, sizeof(current_ssid), "%s", ssid_js->valuestring);
			snprintf(current_password, sizeof(current_password), "%s", password_js->valuestring);
			
			// 拷贝 MQTT 信息
			snprintf(MQTT_username, sizeof(MQTT_username), "%s", username_js->valuestring);
			snprintf(MQTT_client_id, sizeof(MQTT_client_id), "%s", client_id_js->valuestring);
			snprintf(MQTT_device_key, sizeof(MQTT_device_key), "%s", key_js->valuestring);
			snprintf(onenet_product_access_key,sizeof(onenet_product_access_key),"%s",product_key_js->valuestring);


			ESP_LOGI(TAG, "Config Received! WiFi:%s, MQTT_User:%s", current_ssid, MQTT_username);
			
			// 设置事件标志位，通知主任务停止 HTTP Server 并开始后续保存/连接逻辑
			xEventGroupSetBits(apcfg_event, APCFG_BIT);  
		}
        
        cJSON_Delete(root);
    }
    else
    {
        ESP_LOGE(TAG, "Receive invalid json");
    }
}

static void ap_wifi_task(void* param)
{
    EventBits_t ev;
    while(1)
    {
        ev = xEventGroupWaitBits(apcfg_event,APCFG_BIT,pdTRUE,pdFALSE,pdMS_TO_TICKS(10*1000));
        if(ev &APCFG_BIT)
        {
            web_ws_stop();
            wifi_manager_connect(current_ssid,current_password);
        }
    }
}

/** wifi功能和ap配网功能初始化
 * @param f wifi连接状态回调函数
 * @return 无 
*/
void ap_wifi_init(p_wifi_state_callback f)
{
	index_html = initi_web_page_buffer();
    apcfg_event = xEventGroupCreate();
	wifi_state_cb = f; // 必须将传入的函数指针保存到全局变量中
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ap_wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ap_wifi_event_handler, NULL, NULL));
    xTaskCreatePinnedToCore(ap_wifi_task,"apcfg",4096,NULL,2,&AP_Task_Handle,1);
}

/** 连接某个热点
 * @param ssid
 * @param password
 * @return 无 
*/
void ap_wifi_set(const char* ssid,const char* password)
{
    wifi_manager_connect(ssid,password);
}

/** 启动配网模式
 * @param enable 暂无用，强制true
 * @return 无 
*/
void ap_wifi_apcfg(bool enable)
{
    if(enable)
    {
		ESP_LOGW(TAG, ">>> 启动 AP 配网模式");
        wifi_manager_ap();
        ws_cfg_t ws = 
        {
            .html_code = index_html,
            .receive_fn = ws_receive_handle,
        };
        web_ws_start(&ws);
    }
}

void Wifi_State_Handle(bool isConnected)
{
	if(isConnected)
		xTaskNotify(MQTT_Task_Handle,AP_Provision_Complete,eSetValueWithOverwrite);
}


// ============================================================
// ⑥  Wifi_Init：优先读 NVS，否则等待key2长按走 AP 配网
// ⑥  Wifi_Init：优先读 NVS，否则等待key2长按走 AP 配网
// ============================================================
/***
 * @brief 初始化 WiFi 模块，优先尝试使用 NVS 中的凭据连接 WiFi；如果没有凭据或连接失败，则进入 AP 配网模式
 * @return 0:成功通过nvs中保存的wifi信息连接了wifi 
 *         1:nvs有信息，但是无法连接wifi 
 *         2: nvs没有wifi信息，等待进入ap配网模式 
 *         3: 创建事件组失败
 */
#define THROUGH_NVS_CONNECTION 0
#define NVS_CONNECTION_FAILED 1
#define WAITING_FOR_AP_PROVISIONING 2
#define EVENT_GROUP_CREATION_FAILED 3
uint8_t Wifi_Init(void)
{
    if (wifi_event_group == NULL) {
        wifi_event_group = xEventGroupCreate();
        if (wifi_event_group == NULL) {
            ESP_LOGE(TAG, "创建 WiFi 事件组失败");
            return EVENT_GROUP_CREATION_FAILED;
        }
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_sta = esp_netif_create_default_wifi_sta();
	esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册 IP 事件
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    // ----------------------------------------------------------
    // 路径 A：NVS 中有凭据 → 直接连接
    // ----------------------------------------------------------
    if (NVS_Has_Wifi_Credentials()) {
        char ssid[CRED_SSID_MAX_LEN]  = {0};
        char pass[CRED_PASS_MAX_LEN]  = {0};

        if (NVS_Load_Wifi_Credentials(ssid, sizeof(ssid),
                                      pass, sizeof(pass)) == ESP_OK) {
            ESP_LOGW(TAG, ">>> 使用 NVS 存储的 WiFi 凭据连接  SSID: %s", ssid);

            ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                       &wifi_event_handler, NULL));

            wifi_config_t wifi_config = {0};
            strlcpy((char *)wifi_config.sta.ssid,     ssid, sizeof(wifi_config.sta.ssid));
            strlcpy((char *)wifi_config.sta.password,  pass, sizeof(wifi_config.sta.password));
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_start());

			esp_wifi_connect();

            ESP_LOGW(TAG, ">>> 等待 WiFi 连接（最长 90 秒）...");
            EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                                   WIFI_CONNECTED_BIT,
                                                   pdFALSE, pdFALSE,
                                                   pdMS_TO_TICKS(90000));
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGW(TAG, ">>> WiFi 连接成功（NVS 凭据）");
                ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
                return THROUGH_NVS_CONNECTION;
            } else {
                ESP_LOGE(TAG, "WiFi 连接超时（NVS 凭据失效？）或者未开启wifi");
                return NVS_CONNECTION_FAILED;
            }
        }
    }

	// ----------------------------------------------------------
    // 路径 B：NVS 无凭据 → AP 配网
	// ----------------------------------------------------------
    ESP_LOGE(TAG, " WiFi 连接失败，进入离线模式或等待key2长按进入ap配网");
	esp_wifi_stop();
	ap_wifi_init(Wifi_State_Handle);
    return WAITING_FOR_AP_PROVISIONING;
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;
    switch (id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGW(TAG, ">>> MQTT 连接成功！");
            mqtt_connected = true;
			// 连接成功后，订阅主题
			mqtt_onenet_subscribe();
			// 发送一次ota版本号
			onenet_ota_upload_version();
			set_app_valid(1);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT 断开，5秒后尝试重连...");
            mqtt_connected = false;
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_mqtt_client_reconnect(mqtt_client);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "发布成功 msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "收到消息 主题: %.*s | 数据: %.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);
			// 处理收到的消息
			if(strncmp(event->topic, RecvSetTopic, event->topic_len) == 0 &&
			   strlen(RecvSetTopic) == (size_t)event->topic_len)	//设置
			{
				static char data_buf[512] = {0};
				strncpy(data_buf, event->data, sizeof(data_buf)-1);

				cJSON *json = cJSON_Parse(data_buf);
				if(json == NULL)
				{
					ESP_LOGE(TAG, "JSON解析失败");
				}
				else
				{
					cJSON *id_js = cJSON_GetObjectItem(json, "id");
					const char *msg_id = (id_js != NULL) ? id_js->valuestring : "unknown";

					if(MQTT_Solve_OnenetMessage(json, RecvSetTopic) != ESP_OK)
					{
						ESP_LOGE(TAG, "参数解析失败");
						MQTT_Onenet_Ack(msg_id, 404, "参数解析失败");
					}
					else
					{
						ESP_LOGI(TAG, "参数解析成功");
						MQTT_Onenet_Ack(msg_id, 200, "处理成功");
					}

					cJSON_Delete(json);
				}
			}
			else if(strncmp(event->topic, OTATopic, event->topic_len) == 0 &&
			   strlen(OTATopic) == (size_t)event->topic_len)	//OTA
			{
				ESP_LOGI(TAG, ">>> 收到 OTA 升级通知！Topic 匹配成功！");
				ESP_LOGI(TAG, "OTA 消息数据: %.*s", event->data_len, (char*)event->data);

				char data_buf[512] = {0};
				strncpy(data_buf, event->data, sizeof(data_buf)-1);

				cJSON *json = cJSON_Parse(data_buf);
				if(json == NULL)
				{
					ESP_LOGE(TAG, "OTA JSON解析失败! 原始数据: %s", data_buf);
				}
				else
				{
					cJSON *id_js = cJSON_GetObjectItem(json, "id");
					const char *msg_id = (id_js != NULL) ? id_js->valuestring : "unknown";

					ESP_LOGI(TAG, "OTA 消息ID: %s, 正在回复ACK...", msg_id);
					MQTT_Onenet_OTA_Ack(msg_id, 200, "处理成功");
					cJSON_Delete(json);

					ESP_LOGI(TAG, ">>> 收到OTA升级通知，发送任务通知给MQTT主任务...");
					if(MQTT_Task_Handle != NULL)
					{
						BaseType_t notify_result = xTaskNotify(MQTT_Task_Handle, OTA_Upgrade_Requested, eSetValueWithOverwrite);

						if(notify_result == pdPASS)
						{
							ESP_LOGI(TAG, "✅ OTA升级任务通知已发送，将在MQTT主任务中异步处理");
						}
						else
						{
							ESP_LOGE(TAG, "❌ 发送OTA任务通知失败!");
						}
					}
					else
					{
						ESP_LOGE(TAG, "❌ MQTT_Task_Handle为NULL，无法发送OTA通知!");
					}
				}
			}
			else
			{
				// 调试：显示不匹配的 topic（仅在收到 ota/inform 时）
				if(strstr(event->topic, "ota/inform") != NULL)
				{
					ESP_LOGW(TAG, "⚠️ 收到OTA通知但Topic不匹配!");
					ESP_LOGW(TAG, "   收到的Topic: %.*s (长度:%d)", event->topic_len, (char*)event->topic, event->topic_len);
					ESP_LOGW(TAG, "   期望的Topic: %s (长度:%d)", OTATopic, strlen(OTATopic));
				}
			}
            break;
        case MQTT_EVENT_ERROR:
            mqtt_error_count++;
            if (mqtt_error_count % ERROR_LOG_INTERVAL == 1) {
                ESP_LOGE(TAG, "MQTT 错误 (计数: %d)", mqtt_error_count);
                if (event->error_handle) {
                    ESP_LOGE(TAG, "  错误类型: %d  连接返回码: %d",
                             event->error_handle->error_type,
                             event->error_handle->connect_return_code);
                }
            }
            break;
        default:
            ESP_LOGD(TAG, "MQTT 事件 id=%ld", id);
            break;
    }
}


// ============================================================
// ⑦  MQTT_App_Start：从 NVS 读取 MQTT 凭据
// ============================================================
typedef enum
{
	MQTT_App_Start_Direct_State = 0,
	MQTT_APP_Start_AP_State = 1
}MQTT_Start_Event;
/***
 * @brief 启动 MQTT 客户端
 * @param uint8_t choice 选择模式，0:直接连接wifi了，用nvs保存的 1:ap配网获得的mqtt信息连接onenet
 */
esp_err_t MQTT_App_Start(uint8_t choice)
{
    ESP_LOGW(TAG, ">>> 初始化 MQTT 客户端...");
	static char token[CRED_MQTT_KEY_MAX_LEN] = {0};		//onenet平台凭证
	static char mqtt_user[CRED_MQTT_USER_MAX_LEN] = {0};
	static char mqtt_client_id[CRED_MQTT_CLIENT_ID_MAX_LEN] = {0};
	static char mqtt_key[CRED_MQTT_KEY_MAX_LEN] = {0};
	static char mqtt_product_access[CRED_MQTT_KEY_MAX_LEN] = {0};

	if(choice == 0)	// 从 NVS 读取 MQTT 凭据，读不到则使用默认值
	{
		if (NVS_Load_MQTT_Credentials(mqtt_user, sizeof(mqtt_user),
                                   mqtt_client_id, sizeof(mqtt_client_id),
                                   mqtt_key, sizeof(mqtt_key),
                                   mqtt_product_access, sizeof(mqtt_product_access)) == ESP_OK) {
			ESP_LOGI(TAG, "✅ 使用 NVS 存储的 MQTT 凭据  User: %s", mqtt_user);
		} else {
			ESP_LOGW(TAG, "⚠️ NVS 无 MQTT 凭据，使用默认值");
			strlcpy(mqtt_user, DEFAULT_MQTT_USERNAME, sizeof(mqtt_user));
			strlcpy(mqtt_client_id, DEFAULT_MQTT_CLIENT_ID, sizeof(mqtt_client_id));
			strlcpy(mqtt_key,DEFUALT_MQTT_KEY,sizeof(mqtt_key));
			strlcpy(mqtt_product_access, DEFAULT_ONENET_PRODUCT_ACCESS_KEY, sizeof(mqtt_product_access));
		}
		snprintf(SendTopic, sizeof(SendTopic), "$sys/%s/%s/thing/property/post", mqtt_user, mqtt_client_id);
		snprintf(RecvSetTopic, sizeof(RecvSetTopic), "$sys/%s/%s/thing/property/set", mqtt_user, mqtt_client_id);
		snprintf(OTATopic, sizeof(OTATopic), "$sys/%s/%s/ota/inform", mqtt_user, mqtt_client_id);
		// 保存onenet平台凭证到全局变量
		strlcpy(onenet_product_access_key, mqtt_product_access, sizeof(onenet_product_access_key));
		strlcpy(MQTT_username, mqtt_user, sizeof(MQTT_username));
		strlcpy(MQTT_client_id, mqtt_client_id, sizeof(MQTT_client_id));
		// 生成onenet平台凭证
		dev_token_generate(token, SIG_METHOD_SHA256, 2147483600, mqtt_user, mqtt_client_id, mqtt_key);
	}
	else if(choice==1)
	{
		strlcpy(mqtt_user, MQTT_username, sizeof(MQTT_username));
		strlcpy(mqtt_client_id, MQTT_client_id, sizeof(MQTT_client_id));
		strlcpy(mqtt_product_access, onenet_product_access_key, sizeof(mqtt_product_access));
		strlcpy(mqtt_key,MQTT_device_key,sizeof(mqtt_key));
		snprintf(SendTopic, sizeof(SendTopic), "$sys/%s/%s/thing/property/post", MQTT_username,MQTT_client_id);
		snprintf(RecvSetTopic, sizeof(RecvSetTopic), "$sys/%s/%s/thing/property/set", MQTT_username,MQTT_client_id);
		snprintf(OTATopic, sizeof(OTATopic), "$sys/%s/%s/ota/inform", MQTT_username,MQTT_client_id);

		dev_token_generate(token,SIG_METHOD_SHA256, 2147483600,MQTT_username,MQTT_client_id,mqtt_key);
		NVS_Save_Wifi_Credentials(current_ssid,current_password);
		NVS_Save_MQTT_Credentials(mqtt_user,mqtt_client_id,mqtt_key,mqtt_product_access);

		nvs_handle_t handle;
		if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
			nvs_set_u8(handle, NVS_PROV_DONE, 1); // 设置配网完成标志
			nvs_commit(handle);
			nvs_close(handle);
		}
	}

	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.hostname        = MQTT_HOST,
		.broker.address.port            = MQTT_PORT,
		.broker.address.transport       = MQTT_TRANSPORT_OVER_TCP,
		.credentials.client_id          = mqtt_client_id,
		.credentials.username           = mqtt_user,
		.credentials.authentication.password = token,
		.task.stack_size                = 10240,
	};

	mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	if (mqtt_client == NULL) {
		ESP_LOGE(TAG, "MQTT 客户端初始化失败");
		return ESP_FAIL;
	}
	mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	if (mqtt_client == NULL) {
		ESP_LOGE(TAG, "MQTT 客户端初始化失败");
		return ESP_FAIL;
	}

	ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
												mqtt_event_handler, NULL));
	ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
	ESP_LOGW(TAG, ">>> MQTT 客户端已启动");
	return ESP_OK;
	ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
												mqtt_event_handler, NULL));
	ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
	ESP_LOGW(TAG, ">>> MQTT 客户端已启动");
	return ESP_OK;
}

// ============================================================
// ⑧  MQTT_Publish
// ============================================================

esp_err_t MQTT_Publish(const char *topic, const char *data, int len)
{
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT 未初始化");
        return ESP_FAIL;
    }
    if (len == 0) len = strlen(data);

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, len, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发布失败 主题: %s", topic);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "发布成功 主题: %s  msg_id: %d", topic, msg_id);
    return ESP_OK;
}

// ============================================================
// ⑨  WiFi 连接状态查询 API
// ⑨  WiFi 连接状态查询 API
// ============================================================

bool MQTT_Is_Connected(void)
{
    return mqtt_connected;
}

// =============================================================
// 风险等级计算逻辑（根据心率和血氧计算，是否检测到异常运动计算）
// 通过前3次计算风险等级
// =============================================================
uint8_t Risk_Last[3] = {0};
uint8_t Risk_Count=0;
float HeartRate_Last[3] = {0};
uint8_t OxygenSaturationCount_Last[3] = {0};

int Calculate_Risk_Level(uint32_t hr, uint32_t spo2, bool abnormal_motion_detected) {
    int riskAll = 0;	//四次连起来的风险
	int risk = 0;	//此次风险
	HeartRate_Last[Risk_Count] = (float)hr;
	OxygenSaturationCount_Last[Risk_Count] = spo2;
	Risk_Last[Risk_Count] = risk;
	Risk_Count++;

	if(abnormal_motion_detected)
	{
		risk+=20;
	}

	if(Risk_Count==3)
	{
		// 连续的心率超过基准值20bpm
		if(hr-Max30102_Get_Heart_Rate_Baseline()>20)
		{
			risk+=35;
			if(HeartRate_Last[2]-Max30102_Get_Heart_Rate_Baseline()>20)
			{
				risk+=20;
				if(HeartRate_Last[1]-Max30102_Get_Heart_Rate_Baseline()>20)
				{
					risk+=15;
					if(HeartRate_Last[0]-Max30102_Get_Heart_Rate_Baseline()>20)
						risk+=0;
				}
					
			}
		}
		
		if(hr<20+Max30102_Get_Heart_Rate_Baseline())
		{
			risk+=35;
			if(HeartRate_Last[2]<20+Max30102_Get_Heart_Rate_Baseline())
			{
				risk+=25;
				if(HeartRate_Last[1]<20+Max30102_Get_Heart_Rate_Baseline())
				{
					risk+=15;
					if(HeartRate_Last[0]<20+Max30102_Get_Heart_Rate_Baseline())
						risk+=5;
				}
					
			}
		}
		
		riskAll = (int)((float)risk*0.4f+(float)Risk_Last[0]*0.3f+(float)Risk_Last[1]*0.2f+(float)Risk_Last[2]*0.1f);
		Risk_Count = 0;
		return riskAll;
	}

	return 5;
}

// ============================================================
// ⑩  OTA
// ============================================================

static void mqtt_onenet_subscribe(void)
{
	char topic[512];

	//订阅上报属性主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post", MQTT_username, MQTT_client_id);
	esp_mqtt_client_subscribe_single(mqtt_client, topic, 0);

	//订阅设置属性主题
	snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set", MQTT_username, MQTT_client_id);
	esp_mqtt_client_subscribe_single(mqtt_client, topic, 0);

	//订阅OTA升级主题
	snprintf(topic,sizeof(topic),"$sys/%s/%s/ota/inform", MQTT_username, MQTT_client_id);
	esp_mqtt_client_subscribe_single(mqtt_client, topic, 0);
}

static esp_err_t MQTT_Solve_OnenetMessage(cJSON *json, const char *topic)
{
	/*
	{
		"id": "123",
		"version": "1.0",
		"params": {
			"BrightValue":500
		}
	}
	*/

	cJSON *param_js = cJSON_GetObjectItem(json, "params");
	if(param_js == NULL)
	{
		ESP_LOGE(TAG, "params is NULL");
		return ESP_FAIL;
	}
	else
	{
		cJSON *BrightValue_js = cJSON_GetObjectItem(param_js, "BrightValue");
		if(BrightValue_js == NULL)
		{
			ESP_LOGE(TAG, "BrightValue is NULL");
			return ESP_FAIL;
		}
		else
		{
			int BrightValue = BrightValue_js->valueint;
			ESP_LOGI(TAG, "BrightValue: %d", BrightValue);
		}
	}
	return ESP_OK;
}

static esp_err_t MQTT_Onenet_Ack(const char *id,int code,const char* msg)
{
	static char topic[512];
	snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set_reply", MQTT_username, MQTT_client_id);

	// 发布ack消息
	cJSON *reply_js = cJSON_CreateObject();
	cJSON_AddStringToObject(reply_js, "id", id);
	cJSON_AddNumberToObject(reply_js, "code", code);
	cJSON_AddStringToObject(reply_js, "msg", msg);
	char *reply_str = cJSON_Print(reply_js);
	if(reply_str == NULL)
	{
		ESP_LOGE(TAG, "cJSON_Print failed");
		return ESP_FAIL;
	}
	else
	{
		ESP_LOGI(TAG, "发布ack消息: %s", reply_str);
		if(MQTT_Publish(topic, reply_str, strlen(reply_str)) != ESP_OK)
		{
			return ESP_FAIL;
		}
	}

	cJSON_Delete(reply_js);
	free(reply_str);

	return ESP_OK;
}

static esp_err_t MQTT_Onenet_OTA_Ack(const char *id,int code,const char* msg)
{
	static char topic[512];
	snprintf(topic, sizeof(topic), "$sys/%s/%s/ota/inform_reply", MQTT_username, MQTT_client_id);

	// 发布ack消息
	cJSON *reply_js = cJSON_CreateObject();
	cJSON_AddStringToObject(reply_js, "id", id);
	cJSON_AddNumberToObject(reply_js, "code", code);
	cJSON_AddStringToObject(reply_js, "msg", msg);
	char *reply_str = cJSON_Print(reply_js);
	if(reply_str == NULL)
	{
		ESP_LOGE(TAG, "cJSON_Print failed");
		return ESP_FAIL;
	}
	else
	{
		ESP_LOGI(TAG, "发布ack消息: %s", reply_str);
		if(MQTT_Publish(topic, reply_str, strlen(reply_str)) != ESP_OK)
		{
			return ESP_FAIL;
		}
	}

	cJSON_Delete(reply_js);
	free(reply_str);

	return ESP_OK;
}

const char *get_app_version(void)
{
	static char app_version[32] = {0};
	if(app_version[0] == 0)
	{
		const esp_partition_t *running_part = esp_ota_get_running_partition();
		if(running_part == NULL)
		{
			ESP_LOGE(TAG, "running_part is NULL");
			return NULL;
		}
		
		esp_app_desc_t app_desc;
		esp_ota_get_partition_description(running_part, &app_desc);
		snprintf(app_version, sizeof(app_version), "%s", app_desc.version);
	}
	return app_version;
}

void set_app_valid(int valid)
{
	const esp_partition_t *running_part = esp_ota_get_running_partition();
	esp_ota_img_states_t state;
	if(esp_ota_get_state_partition(running_part, &state)==ESP_OK)
	{
		if(state == ESP_OTA_IMG_PENDING_VERIFY)
		{
			if(valid)
			{
				esp_ota_mark_app_valid_cancel_rollback();
			}
			else
			{
				esp_ota_mark_app_invalid_rollback_and_reboot();
			}
		}
	}
}

esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
	ota_http_context_t *ctx = (ota_http_context_t *)evt->user_data;

	switch(evt->event_id)
	{
		case HTTP_EVENT_ERROR:
			if(ctx) ESP_LOGE(TAG, "HTTP错误");
			break;

		case HTTP_EVENT_ON_CONNECTED:
			if(ctx)
			{
				memset(ctx->buffer, 0, OTA_BUFFER_SIZE);
				ctx->data_size = 0;
			}
			break;

		case HTTP_EVENT_HEADER_SENT:
			break;

		case HTTP_EVENT_ON_HEADER:
			break;

		case HTTP_EVENT_ON_DATA:
			if(ctx == NULL)
			{
				ESP_LOGE(TAG, "OTA上下文为NULL!");
				return ESP_FAIL;
			}

			int copy_len = 0;
			if(evt->data_len > OTA_BUFFER_SIZE - ctx->data_size)
			{
				copy_len = OTA_BUFFER_SIZE - ctx->data_size;
				ESP_LOGW(TAG, "OTA缓冲区不足! 已接收:%d, 新数据:%d, 剩余空间:%d",
				         ctx->data_size, evt->data_len, OTA_BUFFER_SIZE - ctx->data_size);
			}
			else
			{
				copy_len = evt->data_len;
			}

			memcpy(ctx->buffer + ctx->data_size, evt->data, copy_len);
			ctx->data_size += copy_len;
			ESP_LOGI(TAG, "OTA数据: 已接收%d字节 (本次%d字节)", ctx->data_size, copy_len);
			break;

		case HTTP_EVENT_ON_FINISH:
			if(ctx)
			{
				ESP_LOGI(TAG, "HTTP请求完成, 总共接收%d字节", ctx->data_size);
			}
			break;

		case HTTP_EVENT_DISCONNECTED:
			break;

		case HTTP_EVENT_REDIRECT:
			break;

		default:
			break;
	}
	return ESP_OK;
}


static esp_err_t onenet_ota_http_connect(const char *url,esp_http_client_method_t  method,const char *playload, char *output_buffer, int *output_size)
{
	esp_err_t err;
	static char token[512];
	memset(token, 0, sizeof(token));

	static ota_http_context_t ctx_stack;
	ota_http_context_t *ctx = &ctx_stack;
	memset(ctx, 0, sizeof(ota_http_context_t));

	esp_http_client_config_t config = {
		.url = url,
		.event_handler = http_event_handler,
		.user_data = ctx,
	};

	esp_http_client_handle_t http_client = esp_http_client_init(&config);

	const char *product_access_key = get_product_access_key();
	ESP_LOGI(TAG, "产品AccessKey: %s", product_access_key);
	ESP_LOGI(TAG, "AccessKey 长度: %d 字节", strlen(product_access_key));

	dev_token_generate(token, SIG_METHOD_SHA256,2099279887,MQTT_username,NULL,product_access_key);

	ESP_LOGI(TAG, "生成的OTA Token: %s", token);

	//Post
	esp_http_client_set_method(http_client, method);
	esp_http_client_set_header(http_client, "Content-Type", "application/json");

	if(strlen(token) > 0)
	{
		ESP_LOGI(TAG, "Token长度: %d 字节", strlen(token));
		ESP_LOGI(TAG, "Authorization Header: %s", token);

		esp_err_t set_result = esp_http_client_set_header(http_client, "Authorization", token);
		if(set_result != ESP_OK)
		{
			ESP_LOGE(TAG, "设置Authorization失败! 错误码: 0x%x", set_result);
		}
		else
		{
			ESP_LOGI(TAG, "设置Authorization成功");
		}
	}
	else
	{
		ESP_LOGE(TAG, "Token生成失败!");
	}

	// 注意：不要手动设置Host header，ESP-IDF会自动从URL解析
	if(playload)
	{
		ESP_LOGI(TAG, "post data:%s",playload);
		esp_http_client_set_post_field(http_client, playload, strlen(playload));
	}

	err = esp_http_client_perform(http_client);

	if(err == ESP_OK && output_buffer && output_size)
	{
		int copy_len = (ctx->data_size < OTA_BUFFER_SIZE) ? ctx->data_size : OTA_BUFFER_SIZE - 1;
		memcpy(output_buffer, ctx->buffer, copy_len);
		output_buffer[copy_len] = '\0';
		*output_size = ctx->data_size;
		ESP_LOGI(TAG, "HTTP请求成功, 复制%d字节到输出缓冲区", copy_len);
	}

	esp_http_client_cleanup(http_client);
	return err;
}

// 上报版本号
esp_err_t onenet_ota_upload_version(void)
{
	char url[512]={0};
	char version[256]={0};
	static char response_buffer[OTA_BUFFER_SIZE] = {0};
	int response_size = 0;
	const char *app_version = get_app_version();
	ESP_LOGI(TAG, "开始上报版本: 产品ID=%s, 设备名=%s", MQTT_username, MQTT_client_id);
	snprintf(url,sizeof(url),ONENE_OTA_URL_FORMAT"%s/%s/version",MQTT_username,MQTT_client_id);
	ESP_LOGI(TAG, "OTA API URL: %s", url);
	snprintf(version,sizeof(version),"{\"s_version\":\"%s\",\"f_version\":\"%s\"}",app_version,app_version);
	if(onenet_ota_http_connect(url,HTTP_METHOD_POST,version, response_buffer, &response_size) == ESP_OK)
	{
		ESP_LOGI(TAG, "版本上传API响应: %s (%d字节)", response_buffer, response_size);
		cJSON *root = cJSON_Parse(response_buffer);

		if(root)
		{
			cJSON *code_js = cJSON_GetObjectItem(root, "code");
			cJSON *msg_js = cJSON_GetObjectItem(root, "msg");

			if(code_js)
			{
				int code = (int)cJSON_GetNumberValue(code_js);
				const char *msg = msg_js ? cJSON_GetStringValue(msg_js) : "unknown";

				if(code == 0)
				{
					cJSON *data_js = cJSON_GetObjectItem(root, "data");
					if(data_js)
					{
						cJSON *target_js = cJSON_GetObjectItem(data_js, "target");
						cJSON *tid_js = cJSON_GetObjectItem(data_js, "tid");

						if(target_js && tid_js)
						{
							snprintf(target_version, sizeof(target_version), "%s", cJSON_GetStringValue(target_js));
							task_id = (int)cJSON_GetNumberValue(tid_js);
							ESP_LOGI(TAG, "Upload version success! 当前版本:%s, 目标版本:%s, 任务ID:%d", app_version, target_version, task_id);
						}
						else
						{
							ESP_LOGW(TAG, "版本上传成功但无升级任务: code=%d, msg=%s", code, msg);
						}
					}
					cJSON_Delete(root);
					return ESP_OK;
				}
				else
				{
					ESP_LOGE(TAG, "版本上传失败! code=%d, msg=%s", code, msg);
				}
			}
			else
			{
				ESP_LOGE(TAG, "版本上传响应格式错误!");
			}
			cJSON_Delete(root);
		}
		else
		{
			ESP_LOGE(TAG, "版本上传JSON解析失败! 原始数据: %s", response_buffer);
		}
	}
	ESP_LOGE(TAG, "Check version fail!");
	return ESP_FAIL;
}

// 检测升级任务
esp_err_t onenet_ota_check_task(const char *type,const char *version)
{
	char url[512]={0};
	static char response_buffer[OTA_BUFFER_SIZE] = {0};
	int response_size = 0;
	snprintf(url,sizeof(url),ONENE_OTA_URL_FORMAT"%s/%s/check?type=%s&version=%s",MQTT_username,MQTT_client_id,type,version);
	ESP_LOGI(TAG, "检查升级任务 URL: %s", url);

	if(onenet_ota_http_connect(url,HTTP_METHOD_GET,NULL, response_buffer, &response_size) == ESP_OK)
	{
		ESP_LOGI(TAG, "检查升级任务响应: %s (%d字节)", response_buffer, response_size);

		cJSON *root = cJSON_Parse(response_buffer);

		if(root)
		{
			cJSON *code_js = cJSON_GetObjectItem(root, "code");
			cJSON *msg_js = cJSON_GetObjectItem(root, "msg");

			if(code_js && cJSON_GetNumberValue(code_js) == 0)
			{
				// 解析 data 对象获取任务信息
				cJSON *data_js = cJSON_GetObjectItem(root, "data");
				if(data_js)
				{
					cJSON *target_js = cJSON_GetObjectItem(data_js, "target");
					cJSON *tid_js = cJSON_GetObjectItem(data_js, "tid");

					if(target_js && tid_js)
					{
						snprintf(target_version, sizeof(target_version), "%s", cJSON_GetStringValue(target_js));
						task_id = (int)cJSON_GetNumberValue(tid_js);
						ESP_LOGI(TAG, "发现升级任务! 目标版本:%s, 任务ID:%d", target_version, task_id);
						cJSON_Delete(root);
						return ESP_OK;
					}
					else
					{
						ESP_LOGW(TAG, "API返回成功但无升级任务数据 (可能版本相同)");
					}
				}
				else
				{
					ESP_LOGW(TAG, "API返回成功但无data字段");
				}

				const char *msg = msg_js ? cJSON_GetStringValue(msg_js) : "unknown";
				ESP_LOGI(TAG, "Check task success: code=0, msg=%s", msg);
				cJSON_Delete(root);
				return ESP_OK;
			}
			else
			{
				int code = (int)cJSON_GetNumberValue(code_js);
				const char *msg = msg_js ? cJSON_GetStringValue(msg_js) : "unknown";
				ESP_LOGE(TAG, "检查升级任务失败! code=%d, msg=%s", code, msg);
			}
			cJSON_Delete(root);
		}
		else
		{
			ESP_LOGE(TAG, "检查升级任务JSON解析失败! 原始数据: %s", response_buffer);
		}
	}
	else
	{
		ESP_LOGE(TAG, "HTTP请求失败");
	}
	ESP_LOGE(TAG, "Check task fail!");
	return ESP_FAIL;
}

// 进度
esp_err_t onenet_ota_check_upload_Progress(int tid,int step)
{
	char url[512]={0};
	char playload[64]={0};
	static char response_buffer[OTA_BUFFER_SIZE] = {0};
	int response_size = 0;
	snprintf(url,sizeof(url),ONENE_OTA_URL_FORMAT"%s/%s/%d/status",MQTT_username,MQTT_client_id,tid);
	snprintf(playload,sizeof(playload),"{\"step\":%d}",step);
	if(onenet_ota_http_connect(url,HTTP_METHOD_POST,playload, response_buffer, &response_size) == ESP_OK)
	{
		cJSON *root = cJSON_Parse(response_buffer);
		
		if(root)
		{
			cJSON *code_js = cJSON_GetObjectItem(root, "code");
			if(code_js && cJSON_GetNumberValue(code_js) == 0)
			{
				cJSON_Delete(root);
				return ESP_OK;
			}
		}
	}
	ESP_LOGE(TAG, "Upload version fail!");
	return ESP_FAIL;
}

esp_err_t onenet_ota_http_client_init_cb(esp_http_client_handle_t client)
{
	static char token[512];
	memset(token, 0, sizeof(token));

	dev_token_generate(token, SIG_METHOD_SHA256,2099279887,MQTT_username,NULL,get_product_access_key());

	ESP_LOGI(TAG, "OTA下载Token生成完成, 长度:%d字节", strlen(token));

	esp_http_client_set_method(client, HTTP_METHOD_GET);
	esp_http_client_set_header(client, "Content-Type", "application/json");

	if(strlen(token) > 0)
	{
		esp_err_t set_result = esp_http_client_set_header(client, "Authorization", token);
		if(set_result != ESP_OK)
		{
			ESP_LOGE(TAG, "设置OTA下载Authorization失败! 错误码: 0x%x", set_result);
			return ESP_FAIL;
		}
		ESP_LOGI(TAG, "OTA下载Authorization设置成功");
	}
	else
	{
		ESP_LOGE(TAG, "OTA下载Token生成失败!");
		return ESP_FAIL;
	}

	return ESP_OK;
}

// 下载升级文件
esp_err_t onenet_ota_download(int tid)
{
	char url[512] = {0};
	esp_err_t err;
	snprintf(url,sizeof(url),"http://iot-api.heclouds.com/fuse-ota/%s/%s/%d/download",MQTT_username,MQTT_client_id,tid);
	esp_http_client_config_t http_config = {
		.url = url,
	};

	esp_https_ota_config_t config = {
		.http_config = &http_config,
		.http_client_init_cb = onenet_ota_http_client_init_cb,
	};

	err = esp_https_ota(&config);
	if(err == ESP_OK)
		ESP_LOGI(TAG, "Download Success!...");
	else
		ESP_LOGE(TAG, "Download fail!...");
	return err;
}

static void onenet_ota_cleanup(int failed_step, esp_err_t last_error)
{
	ESP_LOGW(TAG, ">>> 开始OTA资源清理 (失败步骤:%d, 错误码:0x%x)", failed_step, last_error);

	switch(failed_step)
	{
			case 4:
			ESP_LOGW(TAG, " [清理步骤4] 检查OTA分区状态...");
			{
				const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);

				if(update != NULL)
				{
					esp_ota_img_states_t state;
					if(esp_ota_get_state_partition(update, &state) == ESP_OK)
					{
						if(state == ESP_OTA_IMG_PENDING_VERIFY || state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_UNDEFINED)
						{
							ESP_LOGW(TAG, "  OTA更新分区状态异常(%d), 标记为无效并回滚", state);
							esp_err_t mark_err = esp_ota_mark_app_invalid_rollback_and_reboot();
							if(mark_err != ESP_OK)
							{
								ESP_LOGE(TAG, "  ⚠️ 标记OTA分区失败! 错误码: 0x%x", mark_err);
							}
							else
							{
								ESP_LOGI(TAG, "  ✅ OTA分区已标记为无效, 将在下次启动时回滚");
							}
						}
						else if(state == ESP_OTA_IMG_ABORTED)
						{
							ESP_LOGI(TAG, "  OTA更新分区已是ABORTED状态, 无需处理");
						}
					}
				}
				else
				{
					ESP_LOGW(TAG, "  未找到OTA更新分区");
				}
			} /* fall through */

		case 3:
		case 2:
			ESP_LOGW(TAG, " [清理步骤2-3] 清理任务相关状态...");
			task_id = 0;
			memset(target_version, 0, sizeof(target_version));
			ESP_LOGI(TAG, "  ✅ task_id和target_version已重置");
			/* fall through */

		case 1:
			ESP_LOGW(TAG, " [清理步骤1] HTTP连接已在子函数中自动清理");
			break;

		default:
			ESP_LOGW(TAG, " [默认清理]");
			break;
	}

	ESP_LOGW(TAG, " [最终清理] 重置OTA运行状态...");
	if(ota_mutex != NULL)
	{
		if(xSemaphoreTake(ota_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
		{
			ota_task_running = false;
			xSemaphoreGive(ota_mutex);
			ESP_LOGI(TAG, "  ✅ ota_task_running标志位已重置");
		}
		else
		{
			ESP_LOGE(TAG, "  ❌ 获取互斥锁超时! 强制重置标志位 (不安全)");
			ota_task_running = false;
		}
	}
	else
	{
		ota_task_running = false;
		ESP_LOGI(TAG, "  ✅ ota_task_running标志位已重置 (无互斥锁)");
	}

	ESP_LOGI(TAG, "<<< OTA资源清理完成 >>>");
}

static void onenet_ota_task()
{
	esp_err_t err = ESP_OK;
	int failed_step = 0;

	ESP_LOGI(TAG, "========== OTA 升级流程开始 ==========");

	// 步骤1: 上报版本号
	failed_step = 1;
	ESP_LOGI(TAG, "[步骤1/5] 上报当前版本...");
	err = onenet_ota_upload_version();
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "[步骤1/5] 失败! 版本上报失败");
		goto ota_fail;
	}
	ESP_LOGI(TAG, "[步骤1/5] 成功! 版本已上报");

	// 步骤2: 检测升级任务
	failed_step = 2;
	ESP_LOGI(TAG, "[步骤2/5] 检查升级任务...");
	err = onenet_ota_check_task("1",get_app_version());
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "[步骤2/5] 失败! 未获取到升级任务 (task_id=%d)", task_id);
		goto ota_fail;
	}

	if(task_id == 0)
	{
		ESP_LOGW(TAG, "[步骤2/5] 警告! 任务ID为0, 可能没有待执行的升级任务");
		ESP_LOGW(TAG, "         请确认: 1)平台已创建升级任务  2)目标版本 > 当前版本");
		err = ESP_ERR_INVALID_STATE;
		goto ota_fail;
	}
	ESP_LOGI(TAG, "[步骤2/5] 成功! 获取到任务ID=%d, 目标版本=%s", task_id, target_version);

	// 步骤3: 上报任务升级状态 10%
	failed_step = 3;
	ESP_LOGI(TAG, "[步骤3/5] 上报进度 10%%...");
	err = onenet_ota_check_upload_Progress(task_id,10);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "[步骤3/5] 失败! 进度上报失败");
		goto ota_fail;
	}
	ESP_LOGI(TAG, "[步骤3/5] 成功!");

	// 步骤4: 进行HTTP下载固件
	failed_step = 4;
	ESP_LOGI(TAG, "[步骤4/5] 开始下载固件 (task_id=%d)...", task_id);
	err = onenet_ota_download(task_id);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "[步骤4/5] 失败! 固件下载失败, 错误码: 0x%x", err);
		goto ota_fail;
	}
	ESP_LOGI(TAG, "[步骤4/5] 成功! 固件下载完成");

	// 步骤5: 上报进度100%
	failed_step = 5;
	ESP_LOGI(TAG, "[步骤5/5] 上报进度 100%%...");
	err = onenet_ota_check_upload_Progress(task_id,100);
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "[步骤5/5] 警告! 最终进度上报失败 (但不影响重启)");
	}
	else
	{
		ESP_LOGI(TAG, "[步骤5/5] 成功!");
	}

	ESP_LOGI(TAG, "========== OTA 升级成功! 即将重启 ==========");
	vTaskDelay(1000 / portTICK_PERIOD_MS);

	esp_restart();

ota_fail:
	ESP_LOGE(TAG, "========== OTA 升级失败! 步骤%d, 错误码:0x%x ==========", failed_step, err);

	onenet_ota_cleanup(failed_step, err);

	ESP_LOGW(TAG, "OTA任务即将退出 (堆栈内存将自动释放)");
	vTaskDelay(500 / portTICK_PERIOD_MS);  // 给日志一点时间输出
	vTaskDelete(NULL);
}

static void ota_mutex_init(void)
{
	if(ota_mutex == NULL)
	{
		ota_mutex = xSemaphoreCreateMutex();
		if(ota_mutex == NULL)
		{
			ESP_LOGE(TAG, "创建OTA互斥锁失败!");
		}
		else
		{
			ESP_LOGI(TAG, "OTA互斥锁创建成功");
		}
	}
}

void onenet_ota_update(void)
{
	if(ota_mutex == NULL)
	{
		ota_mutex_init();
		if(ota_mutex == NULL)
		{
			ESP_LOGE(TAG, "OTA互斥锁不可用，无法启动OTA任务");
			return;
		}
	}

	if(xSemaphoreTake(ota_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
	{
		ESP_LOGE(TAG, "获取OTA互斥锁超时! 可能有其他OTA任务正在运行");
		return;
	}

	if(ota_task_running)
	{
		ESP_LOGW(TAG, "OTA任务正在运行中，拒绝重复启动");
		xSemaphoreGive(ota_mutex);
		return;
	}

	ota_task_running = true;
	xSemaphoreGive(ota_mutex);

	ESP_LOGI(TAG, "Create onenet_ota_task task...");
	BaseType_t ret = xTaskCreatePinnedToCore(onenet_ota_task, "onenet_ota_task", 8192, NULL, 5, NULL, 1);
	if(ret != pdPASS)
	{
		ESP_LOGE(TAG, "创建OTA任务失败! 错误码: %d", ret);
		if(xSemaphoreTake(ota_mutex, portMAX_DELAY) == pdTRUE)
		{
			ota_task_running = false;
			xSemaphoreGive(ota_mutex);
		}
	}
}	



// ============================================================
// 11  Task_MQTT_Message_Handler
// ============================================================

void Task_MQTT_Message_Handler(void *pvParameters)
{
    ESP_LOGW(TAG, ">>> MQTT 消息处理任务启动");
	uint8_t ret;
	uint32_t received_cmd;
	MQTT_Task_Handle = xTaskGetCurrentTaskHandle();
	uint8_t mqtt_app_choice = 0;
	// // 清除所有 MQTT 凭据
	// //debug阶段，方便测试
	// NVS_Clear_All_Credentials();

    // ---------- WiFi 连接（含 AP 配网逻辑）----------
    ret = Wifi_Init();
	if(ret == THROUGH_NVS_CONNECTION)
	{
		mqtt_app_choice = MQTT_App_Start_Direct_State;
	}
	else if(ret == NVS_CONNECTION_FAILED || ret == WAITING_FOR_AP_PROVISIONING)
	{
		//连接失败，等待ap配网
		// 直到ap配网完成才会收到通知，才会继续往下走mqtt的初始化
		if (xTaskNotifyWait(0, 0xFFFFFFFF, &received_cmd, portMAX_DELAY) == pdPASS)
		{
			if(received_cmd == AP_Enter_Provision)
			{
				ESP_LOGW(TAG,"进入AP配网模式，等待配网完成...");
				ap_wifi_apcfg(true);
				// 等待 AP 配网完成的事件通知
				if(xTaskNotifyWait(0, 0xFFFFFFFF, &received_cmd, pdMS_TO_TICKS(180000)) == pdPASS) 
				{
					if(received_cmd == AP_Provision_Complete)
					{
						ESP_LOGW(TAG,"AP 配网完成，继续 MQTT 初始化...");
						mqtt_app_choice = MQTT_APP_Start_AP_State;
					}
				}
				else
				{
					ESP_LOGE(TAG,"等待 AP 配网完成事件超时");
					if (Scan_Task_Handle) {
						vTaskDelete(Scan_Task_Handle);
						Scan_Task_Handle = NULL;
					}
					if (AP_Task_Handle) {
						vTaskDelete(AP_Task_Handle);
						AP_Task_Handle = NULL;
					}
					vTaskDelete(NULL);
					return;
				}
			}
			else
			{
				ESP_LOGE(TAG,"!!!收到未知事件通知,删除退出MQTT任务!!!");
				if (Scan_Task_Handle) {
					vTaskDelete(Scan_Task_Handle);
					Scan_Task_Handle = NULL;
				}
				if (AP_Task_Handle) {
					vTaskDelete(AP_Task_Handle);
					AP_Task_Handle = NULL;
				}
				vTaskDelete(NULL);
				return;
			}
		}
	}
	
	if(ret == THROUGH_NVS_CONNECTION)
	{
		mqtt_app_choice = MQTT_App_Start_Direct_State;
	}
	else if(ret == NVS_CONNECTION_FAILED || ret == WAITING_FOR_AP_PROVISIONING)
	{
		//连接失败，等待ap配网
		// 直到ap配网完成才会收到通知，才会继续往下走mqtt的初始化
		if (xTaskNotifyWait(0, 0xFFFFFFFF, &received_cmd, portMAX_DELAY) == pdPASS)
		{
			if(received_cmd == AP_Enter_Provision)
			{
				ESP_LOGW(TAG,"进入AP配网模式，等待配网完成...");
				ap_wifi_apcfg(true);
				// 等待 AP 配网完成的事件通知
				if(xTaskNotifyWait(0, 0xFFFFFFFF, &received_cmd, pdMS_TO_TICKS(180000)) == pdPASS) 
				{
					if(received_cmd == AP_Provision_Complete)
					{
						ESP_LOGW(TAG,"AP 配网完成，继续 MQTT 初始化...");
						mqtt_app_choice = MQTT_APP_Start_AP_State;
					}
				}
				else
				{
					ESP_LOGE(TAG,"等待 AP 配网完成事件超时");
					if (Scan_Task_Handle) {
						vTaskDelete(Scan_Task_Handle);
						Scan_Task_Handle = NULL;
					}
					if (AP_Task_Handle) {
						vTaskDelete(AP_Task_Handle);
						AP_Task_Handle = NULL;
					}
					vTaskDelete(NULL);
					return;
				}
			}
			else
			{
				ESP_LOGE(TAG,"!!!收到未知事件通知,删除退出MQTT任务!!!");
				if (Scan_Task_Handle) {
					vTaskDelete(Scan_Task_Handle);
					Scan_Task_Handle = NULL;
				}
				if (AP_Task_Handle) {
					vTaskDelete(AP_Task_Handle);
					AP_Task_Handle = NULL;
				}
				vTaskDelete(NULL);
				return;
			}
		}
	}
	
    // ---------- 等待网络稳定（手机热点 NAT 需要时间）----------
    vTaskDelay(pdMS_TO_TICKS(3000));

    // ---------- NTP 时间同步 ----------
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "cn.pool.ntp.org");
    esp_sntp_setservername(1, "ntp.aliyun.com");
    esp_sntp_setservername(2, "pool.ntp.org");
    esp_sntp_init();

    for (int i = 0; i < 15; i++) {
        if (time(NULL) > 1000000000LL) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (time(NULL) < 1000000000LL) {
        ESP_LOGW(TAG, "⚠️ NTP 同步未完成，使用相对时间戳");
    }

	// ---------- 启动 MQTT ----------
	MQTT_App_Start(mqtt_app_choice);

    // ---------- 等待 MQTT 连接建立 ----------
    for (int i = 0; i < 10; i++) {
        if (mqtt_connected) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "⏳ 等待 MQTT 连接... (%d/10)", i + 1);
    }
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "⚠️ MQTT 暂未连接，将继续处理消息队列（连接后自动发送）");
    }

    // ---------- 主循环：从消息队列取数据并上报 ----------
    while (1) {
        uint32_t ota_notification_value = 0;
        if(xTaskNotifyWait(0, OTA_Upgrade_Requested, &ota_notification_value, 0) == pdPASS)
        {
            if(ota_notification_value == OTA_Upgrade_Requested)
            {
                ESP_LOGI(TAG, "🚀 MQTT主任务收到OTA升级请求，开始异步处理...");
                onenet_ota_update();
            }
        }

        Sensor_Message_t message;

        if (!Message_Queue_Receive(Message_Queue_Get_Handle(QUEUE_TYPE_MQTT),
                                   &message, pdMS_TO_TICKS(100))) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (mqtt_client == NULL || !mqtt_connected) {
            ESP_LOGW(TAG, "⚠️ MQTT 未连接，丢弃当前消息");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 获取时间戳（毫秒）
        long long ts_ms;
        time_t now = time(NULL);
        if (now > 1000000000LL) {
            ts_ms = (long long)now * 1000LL;
        } else {
            static time_t start_time = 0;
            if (start_time == 0) start_time = now;
            ts_ms = (long long)(1704067200LL + (now - start_time)) * 1000LL;
        }

        char json_data[512] = {0};
		char time_str[64] = {0};
		
		snprintf(time_str, sizeof(time_str), ",\"time\":%lld", ts_ms);
		
        switch (message.Message_Type) {

            case MESSAGE_TYPE_HEART_RATE_SPO2:
                 int current_risk = Calculate_Risk_Level(
                    message.Data.Heart_Rate_SPO2_Data.Heart_Rate, 
                    message.Data.Heart_Rate_SPO2_Data.SpO2,
                    Get_isFall());

                snprintf(json_data, sizeof(json_data), 
                        "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                        "\"heart_rate\":{\"value\":%lu %s},"
                        "\"oxygen_saturation\":{\"value\":%lu %s},"
                        "\"seizure_risk_level\":{\"value\":%d %s}" 
                        "}}",
                        rand() % 1000,
                        message.Data.Heart_Rate_SPO2_Data.Heart_Rate, time_str,
                        message.Data.Heart_Rate_SPO2_Data.SpO2, time_str,
                        current_risk, time_str); // 发送计算出的风险等级

                ESP_LOGI(TAG, "📊 心率血氧 HR:%lu SpO2:%lu Base:%lu Warn:%s",
                         message.Data.Heart_Rate_SPO2_Data.Heart_Rate,
                         message.Data.Heart_Rate_SPO2_Data.SpO2,
                         message.Data.Heart_Rate_SPO2_Data.Baseline,
                         message.Data.Heart_Rate_SPO2_Data.Warning_Active ? "Y" : "N");
                break;

            case MESSAGE_TYPE_ACCELEROMETER:
                snprintf(json_data, sizeof(json_data),
                         "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                         "\"accel_x\":{\"value\":%d,\"time\":%lld},"
                         "\"accel_y\":{\"value\":%d,\"time\":%lld},"
                         "\"accel_z\":{\"value\":%d,\"time\":%lld}}}",
                         12347,
                         message.Data.Accelerometer_Data.Accel_X, ts_ms,
                         message.Data.Accelerometer_Data.Accel_Y, ts_ms,
                         message.Data.Accelerometer_Data.Accel_Z, ts_ms);
                break;

            case MESSAGE_TYPE_GYROSCOPE:
                snprintf(json_data, sizeof(json_data),
                         "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                         "\"gyro_x\":{\"value\":%d,\"time\":%lld},"
                         "\"gyro_y\":{\"value\":%d,\"time\":%lld},"
                         "\"gyro_z\":{\"value\":%d,\"time\":%lld}}}",
                         12348,
                         message.Data.Gyroscope_Data.Gyro_X, ts_ms,
                         message.Data.Gyroscope_Data.Gyro_Y, ts_ms,
                         message.Data.Gyroscope_Data.Gyro_Z, ts_ms);
                break;

            case MESSAGE_TYPE_ALERT:
                snprintf(json_data, sizeof(json_data),
                         "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                         "\"abnormal_motion_detected\":{\"value\":%s %s}"
                         "}}",
                         rand() % 10000,
                         (message.Data.Alert_Data.Fall_Detected || message.Data.Alert_Data.Convulsion_Detected) ? "true" : "false",
                         time_str);
                ESP_LOGW(TAG, "🚨 预警 跌倒:%s 抽搐:%s 心率:%s",
                         message.Data.Alert_Data.Fall_Detected       ? "Y" : "N",
                         message.Data.Alert_Data.Convulsion_Detected ? "Y" : "N",
                         message.Data.Alert_Data.Heart_Rate_Warning  ? "Y" : "N");
                break;

            default:
                ESP_LOGW(TAG, "未知消息类型: %d，跳过", message.Message_Type);
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
        }

        // 上报到 OneNET 物模型
        publish_success_count++;
        ret = MQTT_Publish(SendTopic, json_data, 0);
        if (ret == ESP_OK) {
            if (publish_success_count % SUCCESS_LOG_INTERVAL == 1) {
                ESP_LOGI(TAG, "✅ 数据已发送 (计数:%d)", publish_success_count);
            }
			
			
        } else {
            publish_error_count++;
            if (publish_error_count % ERROR_LOG_INTERVAL == 1) {
                ESP_LOGE(TAG, "❌ 发送失败 (失败计数:%d)", publish_error_count);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}