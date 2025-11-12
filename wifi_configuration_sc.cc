#include "application.h"
#include "wifi_configuration_sc.h"
#include "ssid_manager.h"

//*idf common
#include "esp_err.h"
#include "esp_log.h"
//*idf wifi
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
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
    xTaskCreate([](void *arg)
                {
      WifiConfigurationSc* self = static_cast<WifiConfigurationSc*>(arg);
      EventBits_t uxBits;

      ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );

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
      } }, TAG, 4096, arg, 3, NULL);
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
    wifi_config_t wifi_config;
    // char ssid[33] = {0};
    // char password[65] = {0};
    // char rvd_data[33] = { 0 };
    bzero(&wifi_config, sizeof(wifi_config_t));
    memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
    ESP_LOGI(TAG, "SmartConfig SSID: %s, Password: %s", wifi_config.sta.ssid, wifi_config.sta.password);

    std::string ssid(reinterpret_cast<const char *>(wifi_config.sta.ssid),
                     strnlen(reinterpret_cast<const char *>(wifi_config.sta.ssid),
                             sizeof(wifi_config.sta.ssid)));

    std::string password(reinterpret_cast<const char *>(wifi_config.sta.password),
                         strnlen(reinterpret_cast<const char *>(wifi_config.sta.password),
                                 sizeof(wifi_config.sta.password)));

    self->Save(ssid, password);

    break;
  }

  case SC_EVENT_SEND_ACK_DONE:
  {
    ESP_LOGI(TAG, "SmartConfig ACK sent");
    esp_smartconfig_stop();
    xTaskCreate([](void *ctx)
                {
          ESP_LOGI(TAG, "Restarting in 3 second");
          vTaskDelay(pdMS_TO_TICKS(3000));
          esp_restart(); }, "restart_task", 4096, NULL, 5, NULL);
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