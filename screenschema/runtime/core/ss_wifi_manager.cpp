#include "ss_wifi_manager.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstring>

static const char* TAG = "SS_WIFI";

// ---------------------------------------------------------------------------
// SSWifiManager
// ---------------------------------------------------------------------------

SSWifiManager& SSWifiManager::instance() {
    static SSWifiManager inst;
    return inst;
}

esp_err_t SSWifiManager::init(std::unique_ptr<ISSWifiTransport> transport) {
    if (initialized_) return ESP_OK;
    transport_ = std::move(transport);

    ESP_LOGI(TAG, "Initialising WiFi transport: %s", transport_->name());
    esp_err_t ret = transport_->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Transport init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(esp_netif_init());

    // esp_event_loop_create_default() returns ESP_ERR_INVALID_STATE if the
    // default loop was already created by another component. That's fine.
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(loop_err);
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Mutex and LVGL pump timer for safe cross-task callback delivery.
    // lv_timer runs inside lv_timer_handler() (main task) — safe to call LVGL.
    pending_mutex_ = xSemaphoreCreateMutex();
    pump_timer_ = lv_timer_create(pump_timer_cb, 50, this);

    initialized_ = true;
    ESP_LOGI(TAG, "WiFi ready (%s)", transport_->name());
    return ESP_OK;
}

void SSWifiManager::scan(std::function<void(std::vector<AP>)> on_done) {
    if (!initialized_) { on_done({}); return; }
    scan_cb_ = std::move(on_done);

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
        auto cb = std::move(scan_cb_);
        scan_cb_ = nullptr;
        if (cb) cb({});
        return;
    }
    ESP_LOGI(TAG, "Scan started");
}

void SSWifiManager::connect(const std::string& ssid, const std::string& password,
                             std::function<void(bool)> on_done) {
    if (!initialized_) { on_done(false); return; }
    connect_cb_ = std::move(on_done);

    wifi_config_t cfg = {};
    strncpy((char*)cfg.sta.ssid,     ssid.c_str(),     sizeof(cfg.sta.ssid)     - 1);
    strncpy((char*)cfg.sta.password, password.c_str(), sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to: %s", ssid.c_str());
}

void SSWifiManager::disconnect() {
    if (initialized_) esp_wifi_disconnect();
}

bool        SSWifiManager::isConnected()   const { return connected_; }
std::string SSWifiManager::connectedSSID() const { return connected_ssid_; }
std::string SSWifiManager::ipAddress()     const { return ip_address_; }

void SSWifiManager::onStateChanged(std::function<void(bool, int8_t)> cb) {
    state_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// NVS credential persistence
// ---------------------------------------------------------------------------

void SSWifiManager::saveCredentials(const std::string& ssid, const std::string& password) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ss_wifi", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_str(handle, "ssid", ssid.c_str());
    nvs_set_str(handle, "password", password.c_str());
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi credentials saved (SSID: %s)", ssid.c_str());
}

std::pair<std::string, std::string> SSWifiManager::loadCredentials() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ss_wifi", NVS_READONLY, &handle);
    if (err != ESP_OK) return {"", ""};

    char ssid_buf[64] = {};
    char pw_buf[64]   = {};
    size_t ssid_len = sizeof(ssid_buf);
    size_t pw_len   = sizeof(pw_buf);
    nvs_get_str(handle, "ssid",     ssid_buf, &ssid_len);
    nvs_get_str(handle, "password", pw_buf,   &pw_len);
    nvs_close(handle);

    return {ssid_buf, pw_buf};
}

void SSWifiManager::autoConnect() {
    auto [ssid, pw] = loadCredentials();
    if (ssid.empty()) {
        ESP_LOGI(TAG, "No saved credentials — skipping auto-connect");
        return;
    }
    ESP_LOGI(TAG, "Auto-connecting to: %s", ssid.c_str());
    connect(ssid, pw, [](bool ok) {
        // no-op: state_cb_ will fire when IP is obtained
        (void)ok;
    });
}

// ---------------------------------------------------------------------------
// Thread-safe callback dispatch
// ---------------------------------------------------------------------------

void SSWifiManager::post(std::function<void()> fn) {
    if (!pending_mutex_) return;
    if (xSemaphoreTake(pending_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending_queue_.push_back(std::move(fn));
        xSemaphoreGive(pending_mutex_);
    } else {
        ESP_LOGW(TAG, "post: mutex timeout, callback dropped");
    }
}

void SSWifiManager::pump_timer_cb(lv_timer_t* t) {
    auto* self = static_cast<SSWifiManager*>(t->user_data);
    self->pump_pending();
}

void SSWifiManager::pump_pending() {
    std::vector<std::function<void()>> to_run;
    if (xSemaphoreTake(pending_mutex_, 0) == pdTRUE) {
        to_run = std::move(pending_queue_);
        pending_queue_.clear();
        xSemaphoreGive(pending_mutex_);
    }
    for (auto& fn : to_run) fn();
}

// ---------------------------------------------------------------------------
// Event handler (runs on WiFi/event-loop task)
// ---------------------------------------------------------------------------

void SSWifiManager::event_handler(void* arg, esp_event_base_t base,
                                   int32_t id, void* data) {
    auto* self = static_cast<SSWifiManager*>(arg);

    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t count = 0;
        esp_wifi_scan_get_ap_num(&count);
        std::vector<wifi_ap_record_t> records(count);
        esp_wifi_scan_get_ap_records(&count, records.data());

        std::vector<AP> aps;
        aps.reserve(count);
        for (auto& r : records) {
            std::string ssid((char*)r.ssid);
            if (!ssid.empty()) {
                aps.push_back({ ssid, r.rssi, (r.authmode != WIFI_AUTH_OPEN) });
            }
        }
        ESP_LOGI(TAG, "Scan done: %u APs found", (unsigned)aps.size());

        if (self->scan_cb_) {
            auto cb  = std::move(self->scan_cb_);
            self->scan_cb_ = nullptr;
            self->post([cb = std::move(cb), aps = std::move(aps)]() mutable {
                cb(std::move(aps));
            });
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        char buf[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, buf, sizeof(buf));
        self->connected_  = true;
        self->ip_address_ = buf;

        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            self->connected_ssid_ = (char*)ap.ssid;
        ESP_LOGI(TAG, "Connected: %s  IP: %s", self->connected_ssid_.c_str(), buf);

        int8_t rssi = 0;
        wifi_ap_record_t ap_info = {};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) rssi = ap_info.rssi;

        if (self->connect_cb_) {
            auto cb = std::move(self->connect_cb_);
            self->connect_cb_ = nullptr;
            self->post([cb = std::move(cb)]() { cb(true); });
        }
        if (self->state_cb_) {
            auto cb = self->state_cb_;
            self->post([cb, rssi]() { cb(true, rssi); });
        }
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(data);
        bool was_connected = self->connected_;
        self->connected_  = false;
        self->ip_address_ = "";
        ESP_LOGW(TAG, "Disconnected (reason %d)", event->reason);

        if (!was_connected && self->connect_cb_) {
            auto cb = std::move(self->connect_cb_);
            self->connect_cb_ = nullptr;
            self->post([cb = std::move(cb)]() { cb(false); });
        }
        if (self->state_cb_) {
            auto cb = self->state_cb_;
            self->post([cb]() { cb(false, 0); });
        }
    }
}
