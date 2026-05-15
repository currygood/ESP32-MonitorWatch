**目前只是支持方法二，方法三还不可以**

修改 MQTT 的用户名和密码（OneNET 平台的鉴权信息）有 三种方式，取决于你是想在代码里写死、还是想通过手机动态修改。
方法一：修改代码中的“默认值”（最直接）
如果你想在程序烧录后直接生效，修改 mqtt.h 文件开头的宏定义：
code
C
// 找到这两行，把引号里的内容换成你 OneNET 平台生成的
#define DEFAULT_MQTT_USERNAME   "你的设备名称或ProductID"
#define DEFAULT_MQTT_PASSWORD   "你的Token鉴权字符串"
⚠️ 注意：
如果你的 ESP32 之前已经通过蓝牙配网成功并保存了信息，它会优先读取 NVS（闪存） 里的旧账号，而忽略你改的代码。
解决方法： 修改完代码后，在终端执行 idf.py erase-flash 擦除全部内存，再重新烧录。
方法二：通过手机 App 修改（推荐，无需重烧代码）
由于你的代码里写了 custom-mqtt 自定义端点，你可以在蓝牙配网的过程中直接下发新的账号。
打开手机 ESP Provisioning App。
进入到输入 WiFi 密码的那个页面，或者连接成功后的页面（取决于 App 版本），找 "Custom Data" 选项。
向端点 custom-mqtt 发送如下格式的 JSON 字符串：
code
JSON
{"user":"新的用户名","pass":"新的密码"}
你的 ESP32 接收到后会自动把这对账号存入 NVS，以后开机都会用这一对。
方法三：修改 OneNET 的 Topic（如果更换了产品/设备）
如果你更换了 OneNET 的产品，除了用户名密码，发布主题 (Topic) 也要改。在 mqtt.h 中修改：
code
C
// 修改其中的 ProductID (6wam422raC) 和 DeviceName (ESP32)
#define SENSOR_REPORT_TOPIC     "$sys/你的ProductID/你的DeviceName/thing/property/post"
💡 针对 OneNET 的特别提醒：
OneNET 的密码（Password）通常是一个 Token（很长的一串，包含 version, res, et, method, sign）。
如果你是手动生成的 Token，请确保它的 过期时间 (et) 还没到。
如果账号信息不匹配，串口日志会不停报错 MQTT_EVENT_ERROR，且连接返回码通常是 5 (Unauthorized)。


####

根据这些参考代码，帮我在我mqtt模块中把ble配网删掉，用ap配网，这个ap配网的网页你也要生成，html放在storage存储区；ap配网要求：这个ap配网的网页不只是需要有连接wifi的部分，还要上传m

  qtt的信息（如果没上传，使用默认宏定义的，上传了用上传的，和wifi信息一起保存到nvs） @components/BSP/MQTT/mqtt.c

  @components/BSP/MQTT/mqtt.h  获得的mqtt信息要求：user，name，，client_id，key 四个。esp_mqtt_client_config_t

  mqtt_cfg = {

          .broker.address.hostname        = MQTT_HOST,

          .broker.address.port            = MQTT_PORT,

          .broker.address.transport       = MQTT_TRANSPORT_OVER_TCP,

          .credentials.client_id          = MQTT_CLIENT_ID,

          .credentials.username           = mqtt_user,

          .credentials.authentication.password = mqtt_pass,

      }; 这里的client_id对应client_id，username对应user，然后password是一串东西的组合，看这个文件：passwd.md    然后我是需要长按key2才进行ap配网（三分钟以内），没长按的情况就是检查如mqtt.c中的

路径 A：NVS 中有凭据 → 直接连接

ESP_LOGE(TAG, " WiFi 连接失败，进入离线模式或等待key2长按进入ap配网");
	esp_wifi_stop();
    return ESP_FAIL;

maic.c中的

if(event == KEY_EVENT_LONG_PRESS) {
			// 长按触发ap配网
			
		}

触发配网

整个流程都放到mqtt模块