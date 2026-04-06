#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "ss_wifi_transport.hpp"

class SSWifiManager {
public:
    struct AP {
        std::string ssid;
        int8_t      rssi;
        bool        secured;  // requires password
    };

    static SSWifiManager& instance();

    // Call once from main before starting the LVGL loop.
    // transport ownership is transferred here.
    esp_err_t init(std::unique_ptr<ISSWifiTransport> transport);

    // Persistent callback fired on the LVGL task whenever connection state changes.
    // connected=true: just connected; connected=false: just disconnected.
    // rssi is the AP signal strength when connected, 0 when disconnected.
    void onStateChanged(std::function<void(bool connected, int8_t rssi)> cb);

    // Async network scan. on_done fires on the LVGL task (safe to update UI directly).
    void scan(std::function<void(std::vector<AP>)> on_done);

    // Async connect. on_done(true) = connected, on_done(false) = failed.
    // Fires on the LVGL task.
    void connect(const std::string& ssid, const std::string& password,
                 std::function<void(bool)> on_done);

    void disconnect();

    // NVS credential persistence
    void saveCredentials(const std::string& ssid, const std::string& password);
    std::pair<std::string, std::string> loadCredentials();

    // Loads saved credentials and calls connect() if present. Call from app_main after init().
    void autoConnect();

    bool        isConnected()    const;
    std::string connectedSSID()  const;
    std::string ipAddress()      const;

private:
    SSWifiManager() = default;

    static void event_handler(void* arg, esp_event_base_t base,
                               int32_t id, void* data);

    // Called from lv_timer (main/LVGL task) — drains pending_queue_ safely.
    static void pump_timer_cb(lv_timer_t* t);
    void pump_pending();

    // Post a callback to be called on the LVGL task.
    // Safe to call from any FreeRTOS task.
    void post(std::function<void()> fn);

    std::unique_ptr<ISSWifiTransport> transport_;
    bool        initialized_   = false;
    bool        connected_     = false;
    std::string connected_ssid_;
    std::string ip_address_;

    std::function<void(bool)>                   connect_cb_;
    std::function<void(std::vector<AP>)>        scan_cb_;
    std::function<void(bool, int8_t)>           state_cb_;  // persistent

    // Thread-safe pending callback queue
    SemaphoreHandle_t           pending_mutex_ = nullptr;
    std::vector<std::function<void()>> pending_queue_;
    lv_timer_t*                 pump_timer_    = nullptr;
};
