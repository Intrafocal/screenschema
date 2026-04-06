#pragma once
#include <string>
#include <vector>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "cJSON.h"

class SSDeviceServer {
public:
    static SSDeviceServer& instance();

    // Start the HTTP server. Call from app_main after phone.begin().
    esp_err_t start(uint16_t port = 80);
    void stop();

    // Dispatch fn to the LVGL task and block until it completes (max 2 s).
    // Used by handlers to safely mutate LVGL widgets from non-LVGL tasks.
    void post_and_wait(std::function<void()> fn);

private:
    SSDeviceServer() = default;

    httpd_handle_t server_ = nullptr;

    // Endpoint handlers (run on esp_http_server task — NOT the LVGL task)
    static esp_err_t handle_status(httpd_req_t* req);
    static esp_err_t handle_widget_set(httpd_req_t* req);
    static esp_err_t handle_widget_set_batch(httpd_req_t* req);
    static esp_err_t handle_widget_get(httpd_req_t* req);
    static esp_err_t handle_widget_show(httpd_req_t* req);
    static esp_err_t handle_widget_hide(httpd_req_t* req);
    static esp_err_t handle_ota_update(httpd_req_t* req);
    static esp_err_t handle_app_launch(httpd_req_t* req);   // R3.3
    static esp_err_t handle_notify(httpd_req_t* req);        // R3.4
    static esp_err_t handle_display_backlight(httpd_req_t* req);  // R3.5
    static esp_err_t handle_display_sleep(httpd_req_t* req);      // R3.5
    static esp_err_t handle_display_wake(httpd_req_t* req);       // R3.5

    // Read full request body (up to 4 KB).
    static std::string read_body(httpd_req_t* req);
    // Send a cJSON object as an application/json response.
    static void send_json(httpd_req_t* req, cJSON* obj, int status = 200);

    void post_fn(std::function<void()> fn);
    static void pump_timer_cb(lv_timer_t* t);
    void pump_pending();

    SemaphoreHandle_t pending_mutex_ = nullptr;
    std::vector<std::function<void()>> pending_queue_;
    lv_timer_t* pump_timer_ = nullptr;
};
