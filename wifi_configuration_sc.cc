#include "application.h"
#include "wifi_configuration_sc.h"

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

WifiConfigurationSc &WifiConfigurationSc::GetInstance()
{
  static WifiConfigurationSc instance;
  return instance;
}

WifiConfigurationSc::WifiConfigurationSc()
{
  //赋值
  
  language_ = "zh-CN";
  sleep_mode_ = false;

  //!idf资源申请
  //事件组
  event_group_ = xEventGroupCreate();
  //定时器
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
  //event handlers实例
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
}

WifiConfigurationSc::~WifiConfigurationSc()
{
  //!idf资源释放
  //事件组

  //定时器
  if (scan_timer_)
  {
    esp_timer_stop(scan_timer_);
    esp_timer_delete(scan_timer_);
  }
  if (event_group_)
  {
    vEventGroupDelete(event_group_);
  }
  //event handlers实例
}

std::vector<wifi_ap_record_t> WifiConfigurationSc::GetAccessPoints()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return ap_records_;
}

void WifiConfigurationSc::WifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
}

void WifiConfigurationSc::IpEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
}

void WifiConfigurationSc::SmartConfigEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
}
