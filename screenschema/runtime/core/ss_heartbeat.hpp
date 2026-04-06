#pragma once
#include <string>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

class SSHeartbeat {
public:
    static SSHeartbeat& instance();

    // Start the repeating heartbeat timer.
    // endpoint_name: registered SSHttpClient endpoint name (e.g. "gateway")
    // path: URL path (e.g. "/devices/heartbeat")
    // interval_ms: send period in milliseconds (default 30 s)
    // Call from app_main after WiFi init and endpoint registration.
    void start(const std::string& endpoint_name,
               const std::string& path,
               uint32_t interval_ms = 30000);

    void stop();

    // Optional callbacks — fired on the LVGL task via SSHttpClient's pump.
    void onSuccess(std::function<void()> cb);
    void onFailure(std::function<void(const std::string& err)> cb);

private:
    SSHeartbeat() = default;

    static void timer_cb(TimerHandle_t xTimer);
    void send_heartbeat();

    TimerHandle_t timer_    = nullptr;
    std::string   endpoint_;
    std::string   path_;

    std::function<void()>                        success_cb_;
    std::function<void(const std::string&)>      failure_cb_;
};
