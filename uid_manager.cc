#include "uid_manager.h"

#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "UidManager"
#define NVS_NAMESPACE "UserInfo"
#define UID_KEY "uid"

UidManager::UidManager() {
    LoadFromNvs();
}

UidManager::~UidManager() {
}

void UidManager::Clear() {
    uid_.clear();
    SaveToNvs();
}

void UidManager::LoadFromNvs() {
    uid_.clear();

    // Load uid from NVS from namespace "device"
    nvs_handle_t nvs_handle;
    auto ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        // The namespace doesn't exist, just return
        ESP_LOGW(TAG, "NVS namespace %s doesn't exist", NVS_NAMESPACE);
        return;
    }
    
    char uid[65];
    size_t length = sizeof(uid);
    if (nvs_get_str(nvs_handle, UID_KEY, uid, &length) == ESP_OK) {
        uid_ = uid;
        ESP_LOGI(TAG, "Loaded UID: %s", uid_.c_str());
    }
    nvs_close(nvs_handle);
}

void UidManager::SaveToNvs() {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle));
    
    if (!uid_.empty()) {
        nvs_set_str(nvs_handle, UID_KEY, uid_.c_str());
        ESP_LOGI(TAG, "Saved UID: %s", uid_.c_str());
    } else {
        nvs_erase_key(nvs_handle, UID_KEY);
        ESP_LOGI(TAG, "Erased UID");
    }
    
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

void UidManager::SetUid(const std::string& uid) {
    if (uid_ == uid) {
        ESP_LOGW(TAG, "UID %s already set", uid.c_str());
        return;
    }
    
    uid_ = uid;
    SaveToNvs();
    ESP_LOGI(TAG, "Set UID: %s", uid_.c_str());
}
