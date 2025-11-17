#include "application.h"
#include "wifi_configuration_sc.h"
#include "ssid_manager.h"

//*idf common
#include "esp_err.h"
#include "esp_log.h"

//*net
#include "esp_netif.h"
#include <lwip/ip_addr.h>

//*idf wifi
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_smartconfig.h"
#include "esp_mac.h"

#define TAG "WifiConfigurationSc"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define SC_DONE_BIT BIT2

WifiConfigurationSc::WifiConfigurationSc()
{
}

WifiConfigurationSc::~WifiConfigurationSc()
{
}

WifiConfigurationSc &WifiConfigurationSc::GetInstance()
{
  static WifiConfigurationSc instance;
  return instance;
}

void WifiConfigurationSc::Start()
{
  //! idf资源申请
  // 事件组
  event_group_ = xEventGroupCreate();
  // 定时器
  esp_timer_create_args_t timer_args = {
      .callback = [](void *arg)
      {
        auto *self = static_cast<WifiConfigurationSc *>(arg);
        if (!self->is_connecting_)
        {
          esp_wifi_scan_start(nullptr, false);
        }
      },
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "wifi_scan_timer",
      .skip_unhandled_events = true};
  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &scan_timer_));
  // wifi
  ESP_ERROR_CHECK(esp_netif_init());
  sta_netif_ = esp_netif_create_default_wifi_sta();
  assert(sta_netif_);
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &WifiConfigurationSc::WifiEventHandler,
                                                      this,
                                                      &wifi_event_instance_));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &WifiConfigurationSc::IpEventHandler,
                                                      this,
                                                      &ip_event_instance_));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(SC_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &WifiConfigurationSc::SmartConfigEventHandler,
                                                      this,
                                                      &sc_event_instance_));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

void WifiConfigurationSc::Stop()
{
  //! idf资源释放
  // wifi
  if (wifi_event_instance_)
  {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_instance_);
  }
  if (ip_event_instance_)
  {
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_instance_);
  }
  if (sc_event_instance_)
  {
    esp_event_handler_instance_unregister(SC_EVENT, ESP_EVENT_ANY_ID, sc_event_instance_);
  }
  // 定时器
  if (scan_timer_)
  {
    esp_timer_stop(scan_timer_);
    esp_timer_delete(scan_timer_);
  }
  // 事件组
  if (event_group_)
  {
    vEventGroupDelete(event_group_);
  }
}

void WifiConfigurationSc::WifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  WifiConfigurationSc *self = static_cast<WifiConfigurationSc *>(arg);

  switch (event_id)
  {
  case WIFI_EVENT_STA_START:
  {
    xTaskCreate([](void *arg) {
      //------------------------------------------------------------------------
      WifiConfigurationSc* self = static_cast<WifiConfigurationSc*>(arg);
      EventBits_t uxBits;

      ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );

      smartconfig_start_config_t cfg = { 
        .enable_log = true, 
        .esp_touch_v2_enable_crypt = false,
        .esp_touch_v2_key = NULL 
      };

      ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );

      while (1) {
        uxBits = xEventGroupWaitBits(self->event_group_, WIFI_CONNECTED_BIT | SC_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & WIFI_CONNECTED_BIT) {
          ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & SC_DONE_BIT) {
          ESP_LOGI(TAG, "smartconfig over");
          esp_smartconfig_stop();
          vTaskDelete(NULL);
        }
      } 
      //------------------------------------------------------------------------
    }, TAG, 4096, arg, 3, NULL);
    break;
  }

  case WIFI_EVENT_STA_DISCONNECTED:
  {
    esp_wifi_connect();
    xEventGroupClearBits(self->event_group_, WIFI_CONNECTED_BIT);
    break;
  }

  default:
    break;
  }
}

void WifiConfigurationSc::IpEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  WifiConfigurationSc *self = static_cast<WifiConfigurationSc *>(arg);

  switch (event_id)
  {
  case IP_EVENT_STA_GOT_IP:
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    break;
  }

  default:
    break;
  }
}

void WifiConfigurationSc::SmartConfigEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  WifiConfigurationSc *self = static_cast<WifiConfigurationSc *>(arg);

  switch (event_id)
  {
  case SC_EVENT_SCAN_DONE:
  {
    ESP_LOGI(TAG, "Scan done");
    break;
  }

  case SC_EVENT_FOUND_CHANNEL:
  {
    ESP_LOGI(TAG, "Found channel");
    break;
  }

  case SC_EVENT_GOT_SSID_PSWD:
  {
    ESP_LOGI(TAG, "Got SSID and password");
    smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

    //*从evt->password中提取actual_password和uid (格式: "actual_password@uid")
    std::string password_str(reinterpret_cast<const char *>(evt->password));
    std::string actual_password;
    std::string uid;
    size_t at_pos = password_str.find('@');
    
    if (at_pos != std::string::npos) {
      actual_password = password_str.substr(0, at_pos);
      uid = password_str.substr(at_pos + 1);
      ESP_LOGI(TAG, "Extracted Password: %s, UID: %s", actual_password.c_str(), uid.c_str());
    } else {
      actual_password = password_str;
      ESP_LOGW(TAG, "No UID found in password_str, using full string as password");
    }

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config_t));
    memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));

    size_t password_len = actual_password.length();
    if (password_len > sizeof(wifi_config.sta.password)) {
      password_len = sizeof(wifi_config.sta.password);
    }
    memcpy(wifi_config.sta.password, actual_password.c_str(), password_len);
    
    ESP_LOGI(TAG, "SmartConfig SSID: %s, Password: %s", wifi_config.sta.ssid, wifi_config.sta.password);

    std::string ssid(reinterpret_cast<const char *>(wifi_config.sta.ssid),
      strnlen(reinterpret_cast<const char *>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid)));

    std::string password(reinterpret_cast<const char *>(wifi_config.sta.password),
      strnlen(reinterpret_cast<const char *>(wifi_config.sta.password), sizeof(wifi_config.sta.password)));

    //断开当前连接准备重连
    ESP_ERROR_CHECK( esp_wifi_disconnect() );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );

    esp_err_t ret;
    //AP连接
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(ret));
    }
    self->Save(ssid, password);//可连接再保存

    //启动http服务器进行UID绑定
    xEventGroupSetBits(self->event_group_, SC_DONE_BIT);

    //打包要发送的json数据
    /*
    {
      "userId": 1,
      "agentName": "客服助手",
      "board": "",
      "appVersion": "",
      "macAddress": ""
    }
    */

    uint8_t mac[6];
    char mac_str[18];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);


    cJSON *json_root = cJSON_CreateObject();
    cJSON_AddNumberToObject(json_root, "userId", std::stoul(uid));
    cJSON_AddStringToObject(json_root, "agentName", "客服助手");
    cJSON_AddStringToObject(json_root, "board", "xuanji");
    cJSON_AddStringToObject(json_root, "appVersion", "1.0.0");
    cJSON_AddStringToObject(json_root, "macAddress", mac_str);
    char *json_data = cJSON_PrintUnformatted(json_root);//!------------------------------------
    int json_data_len = strlen(json_data);
    cJSON_Delete(json_root);
    
    //发送http请求
    esp_http_client_config_t http_client_config = {
      .url                     = "/xiaozhi/xuanji/newset",      // 完整的URL地址（包含协议、主机、路径）
      .host                    = "xjai.art",                    // HTTP服务器主机名或IP地址
      .port                    = 8002,                          // HTTP服务器端口号
      .username                = NULL,                          // HTTP基本认证用户名
      .password                = NULL,                          // HTTP基本认证密码
      .auth_type               = HTTP_AUTH_TYPE_NONE,           // HTTP认证类型（无认证/基本认证/摘要认证）
      .path                    = NULL,                          // HTTP请求路径（如果url已设置则忽略）
      .query                   = NULL,                          // HTTP查询字符串
      .cert_pem                = NULL,                          // 服务器SSL/TLS证书（PEM格式）
      .cert_len                = 0,                             // 证书长度
      .client_cert_pem         = NULL,                          // 客户端SSL/TLS证书（用于双向认证）
      .client_cert_len         = 0,                             // 客户端证书长度
      .client_key_pem          = NULL,                          // 客户端私钥（PEM格式）
      .client_key_len          = 0,                             // 客户端私钥长度
      .client_key_password     = NULL,                          // 客户端私钥密码
      .client_key_password_len = 0,                             // 客户端私钥密码长度
      .tls_version             = ESP_HTTP_CLIENT_TLS_VER_ANY,   // TLS协议版本（任意/1.0/1.1/1.2/1.3）
#ifdef CONFIG_MBEDTLS_HARDWARE_ECDSA_SIGN
      .use_ecdsa_peripheral = false,   // 是否使用硬件ECDSA外设进行签名
      .ecdsa_key_efuse_blk  = 0,       // ECDSA密钥存储的eFuse块编号
#endif
      .user_agent                  = NULL,                     // HTTP User-Agent头字段
      .method                      = HTTP_METHOD_POST,         // HTTP请求方法（GET/POST/PUT/DELETE等）
      .timeout_ms                  = 1000,                     // HTTP请求超时时间（毫秒）
      .disable_auto_redirect       = false,                    // 是否禁用自动重定向
      .max_redirection_count       = 0,                        // 最大重定向次数
      .max_authorization_retries   = 0,                        // 最大授权重试次数
      .event_handler               = NULL,                     // HTTP事件回调函数
      .transport_type              = HTTP_TRANSPORT_OVER_TCP,  // 传输类型（未知/TCP/SSL）
      .buffer_size                 = 0,                        // HTTP接收缓冲区大小
      .buffer_size_tx              = json_data_len,            // HTTP发送缓冲区大小
      .user_data                   = json_data,                // 用户自定义数据指针
      .is_async                    = false,                    // 是否使用异步模式
      .use_global_ca_store         = false,                    // 是否使用全局CA证书存储
      .skip_cert_common_name_check = false,                    // 是否跳过证书通用名称检查
      .common_name                 = NULL,                     // 期望的服务器证书通用名称
      .crt_bundle_attach           = NULL,                     // 证书包附加函数
      .keep_alive_enable           = false,                    // 是否启用TCP Keep-Alive
      .keep_alive_idle             = 5,                        // Keep-Alive空闲时间（秒）
      .keep_alive_interval         = 5,                        // Keep-Alive探测间隔（秒）
      .keep_alive_count            = 3,                        // Keep-Alive探测次数
      .if_name                     = NULL,                     // 网络接口名称
#if CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS
      .alpn_protos = NULL,   // ALPN协议列表（应用层协议协商）
#endif
#if CONFIG_ESP_TLS_USE_SECURE_ELEMENT
      .use_secure_element = false,   // 是否使用安全元件存储密钥
#endif
#if CONFIG_ESP_TLS_USE_DS_PERIPHERAL
      .ds_data = NULL,   // 数字签名外设数据
#endif
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
      .save_client_session = false,   // 是否保存客户端会话票据（用于TLS会话恢复）
#endif
#if CONFIG_ESP_HTTP_CLIENT_ENABLE_CUSTOM_TRANSPORT
      .transport = NULL,   // 自定义传输层实现
#endif
      .addr_type = HTTP_ADDR_TYPE_UNSPEC,   // 地址类型（未指定/IPv4/IPv6）
#if CONFIG_MBEDTLS_DYNAMIC_BUFFER
      .tls_dyn_buf_strategy = (esp_http_client_tls_dyn_buf_strategy_t)0,   // TLS动态缓冲区策略
#endif
    };

    self->http_client_ = esp_http_client_init(&http_client_config);

    ret = esp_http_client_perform(self->http_client_);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(self->http_client_),
                esp_http_client_get_content_length(self->http_client_));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(ret));
    }    
    
    break;
  }

  case SC_EVENT_SEND_ACK_DONE:
  {
    ESP_LOGI(TAG, "SmartConfig ACK sent");
    esp_smartconfig_stop();

    //------------------------------------------------------------------------
    xTaskCreate([](void *ctx) 
    {
      ESP_LOGI(TAG, "Restarting in 3 second");
      vTaskDelay(pdMS_TO_TICKS(3000));
      esp_restart(); 
    }, "restart_task", 4096, NULL, 5, NULL);
    //------------------------------------------------------------------------

    break;
  }

  default:
    break;
  }
}

void WifiConfigurationSc::Save(const std::string &ssid, const std::string &password)
{
  ESP_LOGI(TAG, "Save SSID %s %d", ssid.c_str(), ssid.length());
  SsidManager::GetInstance().AddSsid(ssid, password);
}