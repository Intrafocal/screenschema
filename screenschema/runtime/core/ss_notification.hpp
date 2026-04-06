#pragma once
#include <string>
#include <vector>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

enum class SSNotificationType { INFO, WARNING, ERROR_TYPE };

class SSNotification {
public:
    static SSNotification& instance();

    // Initialize the LVGL dispatch queue.
    // Call from app_main (LVGL task) after the LVGL loop is ready.
    void init();

    // Show a notification overlay. Safe to call from any FreeRTOS task.
    // Dispatches to the LVGL task internally.
    // duration_ms = 0 → never auto-dismisses (caller must dismiss manually).
    void show(const std::string& title,
              const std::string& message,
              uint32_t duration_ms = 4000,
              SSNotificationType type = SSNotificationType::INFO);

private:
    SSNotification() = default;

    void post_fn(std::function<void()> fn);
    static void pump_timer_cb(lv_timer_t* t);
    void pump_pending();

    // Called on the LVGL task: build the overlay widget.
    void create_overlay(const std::string& title,
                        const std::string& message,
                        uint32_t duration_ms,
                        SSNotificationType type);

    // One-shot dismiss timer callback (runs on LVGL task).
    static void dismiss_timer_cb(lv_timer_t* t);

    SemaphoreHandle_t pending_mutex_ = nullptr;
    std::vector<std::function<void()>> pending_queue_;
    lv_timer_t* pump_timer_ = nullptr;

    lv_obj_t*   overlay_        = nullptr;
    lv_timer_t* dismiss_timer_  = nullptr;
};
