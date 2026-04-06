#include "ss_heartbeat.hpp"
#include "ss_device.hpp"
#include "ss_wifi_manager.hpp"
#include "ss_update_manager.hpp"
#include "ss_http_client.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char* TAG = "SS_HB";

SSHeartbeat& SSHeartbeat::instance() {
    static SSHeartbeat inst;
    return inst;
}

void SSHeartbeat::start(const std::string& endpoint_name,
                         const std::string& path,
                         uint32_t interval_ms) {
    if (timer_) {
        ESP_LOGW(TAG, "Already started — call stop() first");
        return;
    }
    endpoint_ = endpoint_name;
    path_     = path;

    timer_ = xTimerCreate("ss_hb",
                           pdMS_TO_TICKS(interval_ms),
                           pdTRUE,     // auto-reload
                           this,
                           timer_cb);
    if (!timer_) {
        ESP_LOGE(TAG, "xTimerCreate failed");
        return;
    }
    xTimerStart(timer_, 0);
    ESP_LOGI(TAG, "Heartbeat started: endpoint=%s path=%s interval=%ums",
             endpoint_name.c_str(), path.c_str(), (unsigned)interval_ms);
}

void SSHeartbeat::stop() {
    if (!timer_) return;
    xTimerStop(timer_, pdMS_TO_TICKS(100));
    xTimerDelete(timer_, pdMS_TO_TICKS(100));
    timer_ = nullptr;
    ESP_LOGI(TAG, "Heartbeat stopped");
}

void SSHeartbeat::onSuccess(std::function<void()> cb) {
    success_cb_ = std::move(cb);
}

void SSHeartbeat::onFailure(std::function<void(const std::string&)> cb) {
    failure_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// FreeRTOS timer callback — runs on timer service task
// ---------------------------------------------------------------------------

void SSHeartbeat::timer_cb(TimerHandle_t xTimer) {
    auto* self = static_cast<SSHeartbeat*>(pvTimerGetTimerID(xTimer));
    self->send_heartbeat();
}

void SSHeartbeat::send_heartbeat() {
    if (!SSWifiManager::instance().isConnected()) {
        ESP_LOGD(TAG, "Not connected — skipping heartbeat");
        return;
    }

    // Build payload
    cJSON* payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "device_id",        SSDevice::id().c_str());
    cJSON_AddStringToObject(payload, "location",         SSDevice::location().c_str());
    cJSON_AddStringToObject(payload, "friendly_name",    SSDevice::friendlyName().c_str());
    cJSON_AddStringToObject(payload, "ip",               SSWifiManager::instance().ipAddress().c_str());
    cJSON_AddStringToObject(payload, "firmware_version", SSUpdateManager::currentVersion().c_str());
    cJSON_AddNumberToObject(payload, "uptime_s",  (double)(esp_timer_get_time() / 1000000LL));
    cJSON_AddNumberToObject(payload, "free_heap", (double)esp_get_free_heap_size());

    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(payload, "wifi_rssi", ap.rssi);
    } else {
        cJSON_AddNullToObject(payload, "wifi_rssi");
    }

    // SSHttpClient::post() serializes the JSON to a string before spawning its task,
    // so it is safe to delete payload immediately after this call.
    auto scb = success_cb_;
    auto fcb = failure_cb_;
    SSHttpClient::instance().post(endpoint_, path_, payload,
        [scb, fcb](bool ok, cJSON* resp) {
            if (resp) cJSON_Delete(resp);
            if (ok) {
                ESP_LOGD(TAG, "Heartbeat OK");
                if (scb) scb();
            } else {
                ESP_LOGW(TAG, "Heartbeat failed");
                if (fcb) fcb("heartbeat POST failed");
            }
        });
    cJSON_Delete(payload);
}
