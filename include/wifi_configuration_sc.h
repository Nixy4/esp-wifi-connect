#pragma once

//*this
#include "dns_server.h"

//*idf
#include <esp_http_server.h>
#include <esp_event.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>

//*cpp std
#include <string>
#include <vector>
#include <mutex>

class WifiConfigurationSc
{
public:
  static WifiConfigurationSc &GetInstance();
  void SetSsidPrefix(const std::string &&ssid_prefix);
  void SetLanguage(const std::string &&language);
  void Start();
  void Stop();
  void Save(const std::string &ssid, const std::string &password);
  std::vector<wifi_ap_record_t> GetAccessPoints();

  //删除拷贝构造函数和赋值运算符
  WifiConfigurationSc(const WifiConfigurationSc &) = delete;
  WifiConfigurationSc &operator=(const WifiConfigurationSc &) = delete;

private:
  WifiConfigurationSc(/* args */);
  ~WifiConfigurationSc();

  std::mutex mutex_;
  EventGroupHandle_t event_group_;
  std::string language_;
  esp_event_handler_instance_t wifi_event_instance_;
  esp_event_handler_instance_t ip_event_instance_;
  esp_event_handler_instance_t sc_event_instance_;
  esp_timer_handle_t scan_timer_ = nullptr;
  bool is_connecting_ = false;
  esp_netif_t *sta_netif_ = nullptr;
  std::vector<wifi_ap_record_t> ap_records_;

  // 高级配置项
  std::string ota_url_;
  int8_t max_tx_power_;
  bool remember_bssid_;
  bool sleep_mode_;

  void StartStation();
  void StartSmartConfig();

  // event handlers
  static void WifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
  static void IpEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
  static void SmartConfigEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};
