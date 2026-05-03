#include "mqtt.h"
#include "MPU6050.h"
#include "max30102.h"

static const char *TAG = "MQTT";

// ============================================================
// 模块内部状态
// ============================================================
static EventGroupHandle_t    wifi_event_group      = NULL;
static esp_mqtt_client_handle_t mqtt_client        = NULL;

static bool  mqtt_connected       = false;
static int   mqtt_error_count     = 0;
static int   publish_error_count  = 0;
static int   publish_success_count = 0;

#define WIFI_CONNECTED_BIT      BIT0
#define MQTT_CONNECTED_BIT      BIT1
#define PROV_SUCCESS_BIT        BIT2   // BLE 配网成功
#define PROV_FAILED_BIT         BIT3   // BLE 配网失败/超时

#define MAX_RETRY_COUNT         5
static int   retry_count = 0;

static const int ERROR_LOG_INTERVAL   = 10;
static const int SUCCESS_LOG_INTERVAL = 10;

// 配网过程中通过自定义端点收到的 MQTT 凭据（临时缓冲）
static char  s_prov_mqtt_user[CRED_MQTT_USER_MAX_LEN] = {0};
static char  s_prov_mqtt_pass[CRED_MQTT_PASS_MAX_LEN] = {0};
static bool  s_prov_mqtt_creds_received = false;

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

    err = nvs_set_str(handle, NVS_KEY_WIFI_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_KEY_WIFI_PASS, pass);
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

    err = nvs_get_str(handle, NVS_KEY_WIFI_SSID, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, NVS_KEY_WIFI_PASS, pass, &pass_len);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief 将 MQTT 用户名/密码写入 NVS
 */
esp_err_t NVS_Save_MQTT_Credentials(const char *username, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_MQTT_USER, username);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_KEY_MQTT_PASS, password);
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
 * @brief 从 NVS 读取 MQTT 用户名/密码
 */
esp_err_t NVS_Load_MQTT_Credentials(char *username, size_t user_len,
                                     char *password, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(handle, NVS_KEY_MQTT_USER, username, &user_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, NVS_KEY_MQTT_PASS, password, &pass_len);
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
    esp_err_t err = nvs_get_u8(handle, NVS_KEY_PROV_DONE, &done);
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

    nvs_erase_key(handle, NVS_KEY_WIFI_SSID);
    nvs_erase_key(handle, NVS_KEY_WIFI_PASS);
    nvs_erase_key(handle, NVS_KEY_MQTT_USER);
    nvs_erase_key(handle, NVS_KEY_MQTT_PASS);
    nvs_erase_key(handle, NVS_KEY_PROV_DONE);
    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGW(TAG, "🗑️ NVS 凭据已全部清除");
    return err;
}

/**
 * @brief 清除凭据并重启，下次启动将进入 BLE 配网模式
 */
esp_err_t BLE_Provisioning_Reset(void)
{
    ESP_LOGW(TAG, "⚠️ 触发配网重置，清除凭据后重启...");
    NVS_Clear_All_Credentials();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; // 不会执行到这里
}

// ============================================================
// ②  BLE 配网 — 自定义端点：下发 MQTT 凭据
//     手机 App 向 "custom-mqtt" 端点发送如下 JSON：
//       {"user":"your_mqtt_user","pass":"your_mqtt_password"}
//     设备解析后保存到 NVS，并回复 {"status":"ok"}
// ============================================================

/**
 * @brief 简易 JSON 值提取（不依赖 cJSON，减少依赖）
 *        提取形如 "key":"value" 中的 value（仅支持字符串值）
 */
static bool json_get_string(const char *json, const char *key,
                             char *out, size_t out_len)
{
    // 构造搜索模式 "key":"
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *start = strstr(json, pattern);
    if (start == NULL) return false;

    start += strlen(pattern); // 跳到值的起始引号之后
    const char *end = strchr(start, '"');
    if (end == NULL) return false;

    size_t len = (size_t)(end - start);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

/**
 * @brief 自定义 BLE 端点回调：接收手机下发的 MQTT 凭据
 */
static esp_err_t mqtt_creds_endpoint_handler(uint32_t session_id,
                                              const uint8_t *inbuf, ssize_t inlen,
                                              uint8_t **outbuf, ssize_t *outlen,
                                              void *priv_data)
{
    if (inbuf == NULL || inlen <= 0) {
        ESP_LOGE(TAG, "自定义端点：收到空数据");
        return ESP_FAIL;
    }

    // 将接收数据复制到临时缓冲区并确保末尾 null
    char payload[512] = {0};
    size_t copy_len = (inlen < (ssize_t)(sizeof(payload) - 1))
                      ? (size_t)inlen : (sizeof(payload) - 1);
    memcpy(payload, inbuf, copy_len);
    ESP_LOGI(TAG, "📥 custom-mqtt 端点收到: %s", payload);

    bool ok = false;
    if (json_get_string(payload, "user", s_prov_mqtt_user, sizeof(s_prov_mqtt_user)) &&
        json_get_string(payload, "pass", s_prov_mqtt_pass, sizeof(s_prov_mqtt_pass)))
    {
        s_prov_mqtt_creds_received = true;
        ESP_LOGI(TAG, "✅ MQTT 凭据已通过 BLE 接收  User: %s", s_prov_mqtt_user);
        ok = true;
    } else {
        ESP_LOGE(TAG, "❌ MQTT 凭据 JSON 解析失败，格式：{\"user\":\"xxx\",\"pass\":\"xxx\"}");
    }

    // 构造回复
    const char *resp = ok ? "{\"status\":\"ok\"}" : "{\"status\":\"error\",\"reason\":\"parse_fail\"}";
    *outlen = strlen(resp);
    *outbuf = (uint8_t *)malloc(*outlen);
    if (*outbuf == NULL) return ESP_ERR_NO_MEM;
    memcpy(*outbuf, resp, *outlen);
    return ESP_OK;
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

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;
    switch (id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGW(TAG, ">>> MQTT 连接成功！");
            mqtt_connected = true;
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
// ④  BLE 配网事件处理（wifi_provisioning 组件标准流程）
// ============================================================

static void provisioning_event_handler(void *arg, esp_event_base_t base,
                                        int32_t id, void *data)
{
    if (base == WIFI_PROV_EVENT) {
        switch (id) {
            case WIFI_PROV_START:
                ESP_LOGW(TAG, "🔵 BLE 配网已启动，等待手机 App 连接...");
                ESP_LOGW(TAG, "   设备名: %s  PoP: %s", BLE_PROV_SERVICE_NAME, BLE_PROV_POP);
                break;

            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *cfg = (wifi_sta_config_t *)data;
                ESP_LOGW(TAG, "📲 收到 WiFi 凭据  SSID: %s", (char *)cfg->ssid);
                // 保存到 NVS（配网成功后正式写入）
                break;
            }

            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)data;
                ESP_LOGE(TAG, "❌ 配网失败，WiFi 认证错误: %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "密码错误" : "AP 未找到");
                // 清除错误状态，重新等待手机重发
                wifi_prov_mgr_reset_sm_state_on_failure();
                break;
            }

            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGW(TAG, "✅ 配网成功！WiFi 认证通过");
                if (wifi_event_group) {
                    xEventGroupSetBits(wifi_event_group, PROV_SUCCESS_BIT);
                }
                break;

            case WIFI_PROV_END:
                // 配网流程结束，释放资源
                wifi_prov_mgr_deinit();
                ESP_LOGW(TAG, "🔵 BLE 配网流程结束");
                break;

            default:
                break;
        }
    }
}

// ============================================================
// ⑤  BLE 配网主流程（内部函数）
// ============================================================

/**
 * @brief 启动 BLE 配网，阻塞直到获得 IP 地址或超时。
 *        配网成功后自动将 WiFi + MQTT 凭据写入 NVS。
 *
 * @return ESP_OK 配网并连网成功
 *         ESP_FAIL 超时或失败
 */
static esp_err_t start_ble_provisioning(void)
{
    ESP_LOGW(TAG, "🚀 启动 BLE 配网模式...");

    // 注册配网事件回调
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                               provisioning_event_handler, NULL));

    // 配置配网管理器（BLE 传输层）
    wifi_prov_mgr_config_t config = {
        .scheme               = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    // 注册自定义端点：用于下发 MQTT 凭据
    ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_create(BLE_PROV_ENDPOINT_MQTT));

    // 生成唯一服务名（基于 MAC 地址后3字节，避免同款设备冲突）
    char service_name[32];
    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, sizeof(service_name), "%s_%02X%02X%02X",
             BLE_PROV_SERVICE_NAME, eth_mac[3], eth_mac[4], eth_mac[5]);
    ESP_LOGW(TAG, "BLE 设备名: %s", service_name);

    // 启动配网广播
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *pop = BLE_PROV_POP;
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, NULL));

    // 注册自定义端点处理函数（必须在 start 之后调用）
    ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_register(BLE_PROV_ENDPOINT_MQTT,
                                                    mqtt_creds_endpoint_handler, NULL));

    ESP_LOGW(TAG, "⏳ 等待配网完成（超时: %d 秒）...", BLE_PROV_TIMEOUT_MS / 1000);

    // 等待 WiFi 连接成功（IP 获取）或超时
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | PROV_FAILED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(BLE_PROV_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        // ---- 配网成功：将凭据持久化到 NVS ----
        // 读出 wifi_provisioning 组件已经写入的 WiFi 配置
        wifi_config_t wifi_cfg;
        esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
        NVS_Save_Wifi_Credentials((char *)wifi_cfg.sta.ssid,
                                  (char *)wifi_cfg.sta.password);

        // 保存 MQTT 凭据（若 BLE 端点已收到则用新值，否则写入默认值）
        if (s_prov_mqtt_creds_received) {
            NVS_Save_MQTT_Credentials(s_prov_mqtt_user, s_prov_mqtt_pass);
        } else {
            ESP_LOGW(TAG, "⚠️ 未收到 MQTT 凭据，写入默认值（后续可通过 OLED 菜单重新配网更新）");
            NVS_Save_MQTT_Credentials(DEFAULT_MQTT_USERNAME, DEFAULT_MQTT_PASSWORD);
        }

        // 写入配网完成标志
        nvs_handle_t h;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u8(h, NVS_KEY_PROV_DONE, 1);
            nvs_commit(h);
            nvs_close(h);
        }

        ESP_LOGW(TAG, "✅ 配网完成，凭据已写入 NVS");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ BLE 配网超时（%d 秒），改用默认凭据", BLE_PROV_TIMEOUT_MS / 1000);
        wifi_prov_mgr_deinit();
        return ESP_FAIL;
    }
}

// ============================================================
// ⑥  Wifi_Init：优先读 NVS，否则走 BLE 配网
// ============================================================

esp_err_t Wifi_Init(void)
{
    if (wifi_event_group == NULL) {
        wifi_event_group = xEventGroupCreate();
        if (wifi_event_group == NULL) {
            ESP_LOGE(TAG, "创建 WiFi 事件组失败");
            return ESP_FAIL;
        }
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册 IP 事件（两种模式都需要）
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

            ESP_LOGW(TAG, ">>> 等待 WiFi 连接（最长 30 秒）...");
            EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                                   WIFI_CONNECTED_BIT,
                                                   pdFALSE, pdFALSE,
                                                   pdMS_TO_TICKS(30000));
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGW(TAG, ">>> WiFi 连接成功（NVS 凭据）");
                return ESP_OK;
            } else {
                ESP_LOGE(TAG, "WiFi 连接超时（NVS 凭据失效？）或者未开启wifi");
                return ESP_FAIL;
            }
        }
    }

    // ----------------------------------------------------------
    // 路径 B：NVS 无凭据 → BLE 配网
    // ----------------------------------------------------------
    ESP_LOGW(TAG, ">>> NVS 中无 WiFi 凭据，启动 BLE 配网模式...");

    // BLE 配网内部会使用 STA 模式，也需要 WIFI_EVENT 回调
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_err_t prov_ret = start_ble_provisioning();

    if (prov_ret == ESP_OK) {
        return ESP_OK;
    }

    // ----------------------------------------------------------
    // 路径 C：BLE 配网也失败 → 回退使用默认硬编码凭据
    // ----------------------------------------------------------
    ESP_LOGW(TAG, ">>> 回退使用默认凭据（后备）  SSID: %s", DEFAULT_WIFI_SSID);
    wifi_config_t fallback_config = {0};
    strlcpy((char *)fallback_config.sta.ssid,
            DEFAULT_WIFI_SSID, sizeof(fallback_config.sta.ssid));
    strlcpy((char *)fallback_config.sta.password,
            DEFAULT_WIFI_PASS, sizeof(fallback_config.sta.password));
    fallback_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &fallback_config));
    // WiFi 已经 start 过了，直接 connect
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGW(TAG, ">>> WiFi 连接成功（默认凭据）");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "所有 WiFi 连接方式均失败，进入离线模式");
    return ESP_FAIL;
}

// ============================================================
// ⑦  MQTT_App_Start：从 NVS 读取 MQTT 凭据
// ============================================================

esp_err_t MQTT_App_Start(void)
{
    ESP_LOGW(TAG, ">>> 初始化 MQTT 客户端...");

    // 从 NVS 读取 MQTT 凭据，读不到则使用默认值
    static char mqtt_user[CRED_MQTT_USER_MAX_LEN] = {0};
    static char mqtt_pass[CRED_MQTT_PASS_MAX_LEN] = {0};

    if (NVS_Load_MQTT_Credentials(mqtt_user, sizeof(mqtt_user),
                                   mqtt_pass, sizeof(mqtt_pass)) == ESP_OK) {
        ESP_LOGI(TAG, "✅ 使用 NVS 存储的 MQTT 凭据  User: %s", mqtt_user);
    } else {
        ESP_LOGW(TAG, "⚠️ NVS 无 MQTT 凭据，使用默认值");
        strlcpy(mqtt_user, DEFAULT_MQTT_USERNAME, sizeof(mqtt_user));
        strlcpy(mqtt_pass, DEFAULT_MQTT_PASSWORD, sizeof(mqtt_pass));
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname        = MQTT_HOST,
        .broker.address.port            = MQTT_PORT,
        .broker.address.transport       = MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id          = MQTT_CLIENT_ID,
        .credentials.username           = mqtt_user,
        .credentials.authentication.password = mqtt_pass,
    };

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
// ⑨  模拟传感器数据生成（测试用，真实数据来自消息队列）
// ============================================================

void generate_sensor_data(sensor_data_t *data)
{
    int base_hr = 65 + (rand() % 21);
    data->heart_rate = base_hr + (rand() % 15) - 7;
    if (data->heart_rate < 60) data->heart_rate = 60;
    if (data->heart_rate > 120) data->heart_rate = 120;

    data->oxygen_saturation = 92 + (rand() % 9);

    int risk = 0;
    if (data->heart_rate > 100 || data->heart_rate < 50)       risk += 40;
    else if (data->heart_rate > 90 || data->heart_rate < 60)   risk += 20;
    if (data->oxygen_saturation < 95)      risk += 30;
    else if (data->oxygen_saturation < 97) risk += 15;
    if (rand() % 100 < 5) { data->abnormal_motion_detected = 1; risk += 50; }
    else                    { data->abnormal_motion_detected = 0; }
    risk += rand() % 11;
    data->seizure_risk_level = (risk > 100) ? 100 : risk;
    data->timestamp = (int)time(NULL);
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
// ⑩  Task_MQTT_Message_Handler（main.c 通过 xTaskCreate 调用）
// ============================================================

void Task_MQTT_Message_Handler(void *pvParameters)
{
    ESP_LOGW(TAG, ">>> MQTT 消息处理任务启动");
	esp_err_t ret;

	// // 清除所有 MQTT 凭据
	// //debug阶段，方便测试
	// NVS_Clear_All_Credentials();

    // ---------- WiFi 连接（含 BLE 配网逻辑）----------
    ret = Wifi_Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 网络初始化完全失败，进入离线模式...");
        // 即使 WiFi 失败，如果你的业务允许离线，可以不 delete，但要跳过网络操作
        // 这里建议直接删除任务或者进入一个纯离线循环
        vTaskDelete(NULL); return; 
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
    ret = MQTT_App_Start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 启动失败");
        vTaskDelete(NULL);
        return;
    }

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
        ret = MQTT_Publish(SENSOR_REPORT_TOPIC, json_data, 0);
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