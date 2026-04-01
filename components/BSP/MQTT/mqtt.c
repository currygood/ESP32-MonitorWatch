#include "mqtt.h"

static const char *TAG = "MQTT";

static EventGroupHandle_t wifi_event_group;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static TaskHandle_t sensor_task_handle = NULL;
// static TaskHandle_t mqtt_message_task_handle = NULL;

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1
#define MAX_RETRY_COUNT 5
static int retry_count = 0;
static bool mqtt_connected = false;
static int mqtt_error_count = 0;
// static int wifi_error_count = 0;
static int publish_error_count = 0;
static int publish_success_count = 0;
static const int ERROR_LOG_INTERVAL = 10;   // 每10次错误才记录一次日志
static const int SUCCESS_LOG_INTERVAL = 10; // 每10次成功才记录一次日志

// 模拟癫痫监测传感器数据生成函数
void generate_sensor_data(sensor_data_t *data)
{
    // 模拟正常心率变化 (癫痫患者正常心率范围)
    int normal_heart_rate = 65 + (rand() % 21); // 65-85基础心率
    int heart_variation = (rand() % 15) - 7;    // -7到+7的随机波动，增加变化幅度
    data->heart_rate = normal_heart_rate + heart_variation;

    // 限制心率在合理范围内
    if (data->heart_rate < 60)
        data->heart_rate = 60;
    if (data->heart_rate > 120)
        data->heart_rate = 120;

    // 模拟血氧饱和度 (癫痫发作时可能降低)
    data->oxygen_saturation = 92 + (rand() % 9); // 92-100%，增加变化幅度

    // 模拟癫痫发作风险等级 (基于多参数综合评估)
    // 风险因素：心率异常、血氧降低、异常运动
    int risk_score = 0;

    // 心率异常风险 (心率>100或<50为高风险)
    if (data->heart_rate > 100 || data->heart_rate < 50)
    {
        risk_score += 40;
    }
    else if (data->heart_rate > 90 || data->heart_rate < 60)
    {
        risk_score += 20;
    }

    // 血氧降低风险 (血氧<95%为高风险)
    if (data->oxygen_saturation < 95)
    {
        risk_score += 30;
    }
    else if (data->oxygen_saturation < 97)
    {
        risk_score += 15;
    }

    // 异常运动检测 (模拟MPU6050检测到抽搐样运动)
    // 5%的概率模拟检测到异常运动
    if (rand() % 100 < 5)
    {
        data->abnormal_motion_detected = 1;
        risk_score += 50; // 异常运动是重要风险因素
    }
    else
    {
        data->abnormal_motion_detected = 0;
    }

    // 随机基础风险 (模拟正常波动)
    risk_score += rand() % 11; // 0-10的随机波动

    // 限制风险等级在0-100范围内
    data->seizure_risk_level = risk_score > 100 ? 100 : risk_score;

    // 获取当前时间戳
    data->timestamp = (int)time(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        ESP_LOGW(TAG, ">>> WiFi STA启动，开始连接...");
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (retry_count < MAX_RETRY_COUNT)
        {
            ESP_LOGW(TAG, "WiFi 断开，正在第 %d 次重试（最多 %d 次）...", retry_count + 1, MAX_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(1500)); // 稍长一点间隔，避免热点过载
            esp_wifi_connect();
            retry_count++;
        }
        else
        {
            ESP_LOGE(TAG, "达到最大重试次数，WiFi 仍无法连接");
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        retry_count = 0;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGW(TAG, ">>> WiFi连接成功！获得IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;
    switch (id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGW(TAG, ">>> MQTT连接成功！");
        mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT 断开连接，尝试重连...");
        mqtt_connected = false;
        // 5秒后尝试重新连接
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_mqtt_client_reconnect(mqtt_client);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGW(TAG, "订阅成功，msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGW(TAG, "取消订阅成功，msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGW(TAG, "发布成功，msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGW(TAG, "收到消息 - 主题: %.*s | 数据: %.*s",
                 event->topic_len, event->topic, event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        mqtt_error_count++;
        if (mqtt_error_count % ERROR_LOG_INTERVAL == 1)
        {
            ESP_LOGE(TAG, "MQTT 发生错误 (错误计数: %d)", mqtt_error_count);
            if (event->error_handle)
            {
                ESP_LOGE(TAG, "错误类型: %d", event->error_handle->error_type);
                if (event->error_handle->connect_return_code != 0)
                {
                    ESP_LOGE(TAG, "连接返回码: %d", event->error_handle->connect_return_code);
                }
            }
        }
        break;
    default:
        ESP_LOGW(TAG, "MQTT 其他事件: %ld", id);
        break;
    }
}
// static const char json_template[] =
// "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
// "\"heart_rate\":{\"value\":\"%d\",\"time\":%lld},"
// "\"oxygen_saturation\":{\"value\":\"%d\",\"time\":%lld},"
// "\"seizure_risk_level\":{\"value\":\"%d\",\"time\":%lld},"
// "\"abnormal_motion_detected\":{\"value\":\"%s\",\"time\":%lld}"
// "}}";
static const char json_template[] =
    "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
    "\"heart_rate\":{\"value\":%d,\"time\":%lld},"
    "\"oxygen_saturation\":{\"value\":%d,\"time\":%lld},"
    "\"seizure_risk_level\":{\"value\":%d,\"time\":%lld},"
    "\"abnormal_motion_detected\":{\"value\":%s,\"time\":%lld}"
    "}}";
// 传感器数据上报任务
static void sensor_report_task(void *arg)
{
    ESP_LOGW(TAG, ">>> 传感器数据上报任务启动");

    // 初始化随机数种子
    srand((unsigned int)time(NULL));

    while (1)
    {
        // 只在MQTT连接时上报数据
        if (mqtt_client != NULL && mqtt_connected)
        {
            sensor_data_t sensor_data;
            generate_sensor_data(&sensor_data);

            char json_data[400];

            // 获取毫秒级时间戳（如果NTP同步失败，使用ESP32启动时间）
            long long timestamp_ms;
            time_t current_time = time(NULL);

            if (current_time > 1000000000LL)
            {
                // NTP同步成功，使用真实时间戳
                timestamp_ms = (long long)current_time * 1000LL;
            }
            else
            {
                // NTP同步失败，使用启动后的相对时间 + 基准时间（2024年1月1日）
                // 这样可以确保时间戳递增且相对合理
                static time_t start_time = 0;
                if (start_time == 0)
                    start_time = current_time;
                timestamp_ms = (long long)(1704067200LL + (current_time - start_time)) * 1000LL; // 1704067200 = 2024-01-01 00:00:00
            }
            snprintf(json_data, sizeof(json_data), json_template,
                     12345,
                     sensor_data.heart_rate, timestamp_ms,
                     sensor_data.oxygen_saturation, timestamp_ms,
                     sensor_data.seizure_risk_level, timestamp_ms,
                     sensor_data.abnormal_motion_detected ? "true" : "false", // 注意：用字符串 "true"/"false"
                     timestamp_ms);
            // snprintf(json_data, sizeof(json_data), json_template,
            // 12345,  // 固定ID: 12345
            // sensor_data.heart_rate, timestamp_ms,
            // sensor_data.oxygen_saturation, timestamp_ms,
            // sensor_data.seizure_risk_level, timestamp_ms,
            // sensor_data.abnormal_motion_detected ? "true" : "false",   // bool值转为字符串
            // timestamp_ms);

            // 打印传感器数据到串口终端（限流输出）
            publish_success_count++;
            if (publish_success_count % SUCCESS_LOG_INTERVAL == 1)
            {
                ESP_LOGW(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                ESP_LOGW(TAG, "📊 传感器数据 (发送计数: %d):", publish_success_count);
                ESP_LOGW(TAG, "   心率: %d 次/分钟", sensor_data.heart_rate);
                ESP_LOGW(TAG, "   血氧饱和度: %d%%", sensor_data.oxygen_saturation);
                ESP_LOGW(TAG, "   癫癎风险等级: %d/100", sensor_data.seizure_risk_level);
                ESP_LOGW(TAG, "   异常运动检测: %s", sensor_data.abnormal_motion_detected ? "是" : "否");
                ESP_LOGW(TAG, "   时间戳: %lld", timestamp_ms);
                ESP_LOGW(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                ESP_LOGW(TAG, "上报JSON: %s", json_data);
                ESP_LOGW(TAG, "数据ID固定为: 12345");
            }

            // 发布到OneNET物模型主题
            if (publish_success_count % SUCCESS_LOG_INTERVAL == 1)
            {
                ESP_LOGI(TAG, "🚀 正在发布数据到主题: %s", SENSOR_REPORT_TOPIC);
            }
            esp_err_t ret = MQTT_Publish(SENSOR_REPORT_TOPIC, json_data, 0);

            if (ret == ESP_OK)
            {
                // 根据风险等级显示不同级别的警告
                if (sensor_data.seizure_risk_level >= 80)
                {
                    ESP_LOGE(TAG, "🚨 高风险警报 - 心率: %d, 血氧: %d%%, 风险等级: %d, 异常运动: %s",
                             sensor_data.heart_rate, sensor_data.oxygen_saturation,
                             sensor_data.seizure_risk_level,
                             sensor_data.abnormal_motion_detected ? "是" : "否");
                }
                else if (sensor_data.seizure_risk_level >= 50)
                {
                    ESP_LOGW(TAG, "⚠️ 中等风险 - 心率: %d, 血氧: %d%%, 风险等级: %d, 异常运动: %s",
                             sensor_data.heart_rate, sensor_data.oxygen_saturation,
                             sensor_data.seizure_risk_level,
                             sensor_data.abnormal_motion_detected ? "是" : "否");
                }
                else
                {
                    ESP_LOGI(TAG, "✅ 正常状态 - 心率: %d, 血氧: %d%%, 风险等级: %d, 异常运动: %s",
                             sensor_data.heart_rate, sensor_data.oxygen_saturation,
                             sensor_data.seizure_risk_level,
                             sensor_data.abnormal_motion_detected ? "是" : "否");
                }
            }
            else
            {
                publish_error_count++;
                if (publish_error_count % ERROR_LOG_INTERVAL == 1)
                {
                    ESP_LOGE(TAG, "❌ 数据上报失败 (失败计数: %d)", publish_error_count);
                }
            }
        }

        // 每3秒上报一次数据
        if (!mqtt_connected)
        {
            ESP_LOGW(TAG, "⚠️ MQTT未连接，等待重连...");
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

esp_err_t Wifi_Init(void)
{
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "创建 WiFi 事件组失败");
        vTaskDelete(NULL);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGW(TAG, ">>> 等待WiFi连接（最长30秒）...");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGW(TAG, ">>> WiFi初始化完成！");
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "WiFi 连接超时（30秒） → 可能是热点不稳定，即将重启...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart(); // ← 核心改动：超时自动重启
        vTaskDelete(NULL);
        return ESP_FAIL;
    }
}

esp_err_t MQTT_App_Start(void)
{
    ESP_LOGW(TAG, ">>> 开始初始化MQTT客户端...");

    esp_mqtt_client_config_t cfg = {
        .broker.address.hostname = MQTT_HOST,
        .broker.address.port = MQTT_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP, // 手机热点使用普通TCP连接
        .credentials.client_id = MQTT_CLIENT_ID,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
    };

    mqtt_client = esp_mqtt_client_init(&cfg);
    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "MQTT 客户端初始化失败");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));

    // 创建传感器数据上报任务
    BaseType_t task_ret = xTaskCreate(sensor_report_task,
                                      "sensor_report",
                                      4096,
                                      NULL,
                                      5,
                                      &sensor_task_handle);

    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "创建传感器上报任务失败");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, ">>> MQTT客户端已启动，传感器上报任务已创建");
    return ESP_OK;
}

esp_err_t MQTT_Publish(const char *topic, const char *data, int len)
{
    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "MQTT 未初始化");
        return ESP_FAIL;
    }

    if (len == 0)
    {
        len = strlen(data);
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, len, 1, 0);
    if (msg_id < 0)
    {
        ESP_LOGE(TAG, "发布失败 - 主题: %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "发布成功 - 主题: %s, msg_id: %d", topic, msg_id);
    return ESP_OK;
}

// MQTT消息处理任务
void Task_MQTT_Message_Handler(void *pvParameters)
{
    ESP_LOGW(TAG, ">>> MQTT消息处理任务启动");

    // 智能NVS管理：只在需要时清除
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGI(TAG, "检测到NVS需要清理，正在清除并重新初始化...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        ESP_LOGI(TAG, "NVS存储已成功清除并重新初始化");
    }
    else if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "NVS初始化成功，保留现有配置");
    }
    else
    {
        ESP_LOGE(TAG, "NVS初始化失败: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "尝试强制清除NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        ESP_LOGI(TAG, "NVS已强制清除并重新初始化");
    }
    ESP_ERROR_CHECK(ret);

    // 连接WiFi
    ESP_LOGI(TAG, "正在连接WiFi...");
    ret = Wifi_Init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi 初始化失败，已在 Wifi_Init 内处理重启");
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "WiFi 连接成功！");

    // 手机热点经常需要额外时间让 NAT/路由稳定
    ESP_LOGI(TAG, "额外等待网络完全稳定（手机热点常用）...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // ====================== NTP 时间同步（ESP-IDF v5.4 正确写法） ======================
    ESP_LOGI(TAG, "正在同步 NTP 时间（用于物模型时间戳）...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "cn.pool.ntp.org");
    esp_sntp_setservername(1, "ntp.aliyun.com");
    esp_sntp_setservername(2, "pool.ntp.org"); // 增加备用NTP服务器
    esp_sntp_init();

    // 等待更长时间进行NTP同步，并多次检查
    for (int i = 0; i < 15; i++)
    { // 检查15次，总共15秒
        time_t now = time(NULL);
        if (now > 1000000000LL)
        { // 时间戳有效
            ESP_LOGI(TAG, "✅ NTP 时间同步成功！当前时间戳: %lld (秒)", (long long)now);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "⏳ 等待NTP时间同步... (%d/15秒)", i + 1);
    }

    time_t now = time(NULL);
    if (now < 1000000000LL)
    {
        ESP_LOGW(TAG, "⚠️ NTP 同步未完成，使用系统时间，可能影响时间戳准确性");
    }
    // ===========================================================================

    // 启动MQTT
    ESP_LOGI(TAG, "启动 MQTT 客户端...");
    ret = MQTT_App_Start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "MQTT 启动失败，错误码: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
    }
    else
    {
        ESP_LOGI(TAG, "MQTT 已启动");
    }

    // 等待MQTT连接建立
    ESP_LOGI(TAG, "等待MQTT连接建立...");
    for (int i = 0; i < 10; i++)
    {
        if (mqtt_connected)
        {
            ESP_LOGI(TAG, "✅ MQTT连接已建立");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "⏳ 等待MQTT连接... (%d/10秒)", i + 1);
    }

    if (!mqtt_connected)
    {
        ESP_LOGW(TAG, "⚠️ MQTT连接未建立，但将继续处理消息队列");
    }

    ESP_LOGI(TAG, "✅ MQTT消息处理任务初始化完成，开始处理消息队列...");

    while (1)
    {
        Sensor_Message_t message;

        // 从消息队列接收数据（阻塞方式，最多等待100ms）
        if (Message_Queue_Receive(Message_Queue_Get_Handle(QUEUE_TYPE_MQTT), &message, pdMS_TO_TICKS(100)))
        {
            // 只在MQTT连接时处理消息
            if (mqtt_client != NULL && mqtt_connected)
            {
                char json_data[400];
                long long timestamp_ms;
                time_t current_time = time(NULL);

                if (current_time > 1000000000LL)
                {
                    // NTP同步成功，使用真实时间戳
                    timestamp_ms = (long long)current_time * 1000LL;
                }
                else
                {
                    // NTP同步失败，使用启动后的相对时间 + 基准时间（2024年1月1日）
                    static time_t start_time = 0;
                    if (start_time == 0)
                        start_time = current_time;
                    timestamp_ms = (long long)(1704067200LL + (current_time - start_time)) * 1000LL;
                }

                switch (message.Message_Type)
                {
                case MESSAGE_TYPE_HEART_RATE_SPO2:
                    // 处理心率血氧数据
                    snprintf(json_data, sizeof(json_data),
                             "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                             "\"heart_rate\":{\"value\":%lu,\"time\":%lld},"
                             "\"oxygen_saturation\":{\"value\":%lu,\"time\":%lld},"
                             "\"heart_rate_baseline\":{\"value\":%lu,\"time\":%lld},"
                             "\"heart_rate_warning\":{\"value\":%s,\"time\":%lld}"
                             "}}",
                             12346, // 与原有任务不同的ID
                             message.Data.Heart_Rate_SPO2_Data.Heart_Rate, timestamp_ms,
                             message.Data.Heart_Rate_SPO2_Data.SpO2, timestamp_ms,
                             message.Data.Heart_Rate_SPO2_Data.Baseline, timestamp_ms,
                             message.Data.Heart_Rate_SPO2_Data.Warning_Active ? "true" : "false", timestamp_ms);

                    ESP_LOGI(TAG, "📊 处理心率血氧数据 - 心率: %lu, 血氧: %lu, 基准: %lu, 预警: %s",
                             message.Data.Heart_Rate_SPO2_Data.Heart_Rate,
                             message.Data.Heart_Rate_SPO2_Data.SpO2,
                             message.Data.Heart_Rate_SPO2_Data.Baseline,
                             message.Data.Heart_Rate_SPO2_Data.Warning_Active ? "是" : "否");
                    break;

                case MESSAGE_TYPE_ACCELEROMETER:
                    // 处理加速度计数据
                    snprintf(json_data, sizeof(json_data),
                             "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                             "\"accelerometer_x\":{\"value\":%d,\"time\":%lld},"
                             "\"accelerometer_y\":{\"value\":%d,\"time\":%lld},"
                             "\"accelerometer_z\":{\"value\":%d,\"time\":%lld}"
                             "}}",
                             12347,
                             message.Data.Accelerometer_Data.Accel_X, timestamp_ms,
                             message.Data.Accelerometer_Data.Accel_Y, timestamp_ms,
                             message.Data.Accelerometer_Data.Accel_Z, timestamp_ms);

                    ESP_LOGI(TAG, "📊 处理加速度计数据 - X: %d, Y: %d, Z: %d",
                             message.Data.Accelerometer_Data.Accel_X,
                             message.Data.Accelerometer_Data.Accel_Y,
                             message.Data.Accelerometer_Data.Accel_Z);
                    break;

                case MESSAGE_TYPE_GYROSCOPE:
                    // 处理陀螺仪数据
                    snprintf(json_data, sizeof(json_data),
                             "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                             "\"gyroscope_x\":{\"value\":%d,\"time\":%lld},"
                             "\"gyroscope_y\":{\"value\":%d,\"time\":%lld},"
                             "\"gyroscope_z\":{\"value\":%d,\"time\":%lld}"
                             "}}",
                             12348,
                             message.Data.Gyroscope_Data.Gyro_X, timestamp_ms,
                             message.Data.Gyroscope_Data.Gyro_Y, timestamp_ms,
                             message.Data.Gyroscope_Data.Gyro_Z, timestamp_ms);

                    ESP_LOGI(TAG, "📊 处理陀螺仪数据 - X: %d, Y: %d, Z: %d",
                             message.Data.Gyroscope_Data.Gyro_X,
                             message.Data.Gyroscope_Data.Gyro_Y,
                             message.Data.Gyroscope_Data.Gyro_Z);
                    break;

                case MESSAGE_TYPE_ALERT:
                    // 处理预警消息
                    snprintf(json_data, sizeof(json_data),
                             "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
                             "\"fall_detected\":{\"value\":%s,\"time\":%lld},"
                             "\"convulsion_detected\":{\"value\":%s,\"time\":%lld},"
                             "\"heart_rate_warning\":{\"value\":%s,\"time\":%lld}"
                             "}}",
                             12349,
                             message.Data.Alert_Data.Fall_Detected ? "true" : "false", timestamp_ms,
                             message.Data.Alert_Data.Convulsion_Detected ? "true" : "false", timestamp_ms,
                             message.Data.Alert_Data.Heart_Rate_Warning ? "true" : "false", timestamp_ms);

                    ESP_LOGW(TAG, "🚨 处理预警消息 - 跌倒: %s, 抽搐: %s, 心率预警: %s",
                             message.Data.Alert_Data.Fall_Detected ? "是" : "否",
                             message.Data.Alert_Data.Convulsion_Detected ? "是" : "否",
                             message.Data.Alert_Data.Heart_Rate_Warning ? "是" : "否");
                    break;

                default:
                    ESP_LOGW(TAG, "未知消息类型: %d", message.Message_Type);
                    continue; // 跳过未知消息类型
                }

                // 发布到OneNET物模型主题
                esp_err_t ret = MQTT_Publish(SENSOR_REPORT_TOPIC, json_data, 0);
                if (ret == ESP_OK)
                {
                    ESP_LOGI(TAG, "✅ 消息成功发送到MQTT平台");
                }
                else
                {
                    ESP_LOGE(TAG, "❌ 消息发送失败");
                }
            }
            else
            {
                ESP_LOGW(TAG, "⚠️ MQTT未连接，跳过消息处理");
            }
        }

        // 短暂延迟，避免过度占用CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}