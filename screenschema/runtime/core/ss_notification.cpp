#include "ss_notification.hpp"
#include "esp_log.h"

static const char* TAG = "SS_NOTIFY";

SSNotification& SSNotification::instance() {
    static SSNotification inst;
    return inst;
}

void SSNotification::init() {
    if (pump_timer_) return;
    pending_mutex_ = xSemaphoreCreateMutex();
    pump_timer_    = lv_timer_create(pump_timer_cb, 20, this);
    ESP_LOGI(TAG, "Notification system ready");
}

void SSNotification::show(const std::string& title,
                           const std::string& message,
                           uint32_t duration_ms,
                           SSNotificationType type) {
    if (!pending_mutex_) {
        ESP_LOGE(TAG, "show() called before init()");
        return;
    }
    // Capture by value so the lambda is safe across tasks
    post_fn([this, title, message, duration_ms, type]() {
        create_overlay(title, message, duration_ms, type);
    });
}

// ---------------------------------------------------------------------------
// Called on the LVGL task: build (or replace) the overlay widget
// ---------------------------------------------------------------------------

void SSNotification::create_overlay(const std::string& title,
                                     const std::string& message,
                                     uint32_t duration_ms,
                                     SSNotificationType type) {
    // Dismiss existing notification if any
    if (dismiss_timer_) {
        lv_timer_del(dismiss_timer_);
        dismiss_timer_ = nullptr;
    }
    if (overlay_) {
        lv_obj_del(overlay_);
        overlay_ = nullptr;
    }

    // Parent: lv_layer_top() stays above all apps across screen transitions
    lv_obj_t* layer = lv_layer_top();

    overlay_ = lv_obj_create(layer);
    lv_obj_set_width(overlay_, lv_pct(90));
    lv_obj_set_height(overlay_, LV_SIZE_CONTENT);
    lv_obj_align(overlay_, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(overlay_, 10, 0);
    lv_obj_set_style_pad_all(overlay_, 10, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);

    // Background color by type
    lv_color_t bg;
    switch (type) {
        case SSNotificationType::WARNING:   bg = lv_color_hex(0xC97B00); break;
        case SSNotificationType::ERROR_TYPE: bg = lv_color_hex(0xAA2222); break;
        default: /* INFO */                 bg = lv_color_hex(0x222233); break;
    }
    lv_obj_set_style_bg_color(overlay_, bg, 0);
    lv_obj_set_style_bg_opa(overlay_, 230, 0);

    // Flex column layout
    lv_obj_set_flex_flow(overlay_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(overlay_, 4, 0);

    // Title label (if provided)
    if (!title.empty()) {
        lv_obj_t* title_lbl = lv_label_create(overlay_);
        lv_label_set_text(title_lbl, title.c_str());
        lv_obj_set_width(title_lbl, lv_pct(100));
        lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xFFFFFF), 0);
    }

    // Message label
    lv_obj_t* msg_lbl = lv_label_create(overlay_);
    lv_label_set_text(msg_lbl, message.c_str());
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_lbl, lv_pct(100));
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xDDDDDD), 0);

    lv_obj_move_foreground(overlay_);

    // Auto-dismiss timer
    if (duration_ms > 0) {
        dismiss_timer_ = lv_timer_create(dismiss_timer_cb, duration_ms, this);
        lv_timer_set_repeat_count(dismiss_timer_, 1);
    }

    ESP_LOGI(TAG, "Notification shown: \"%s\" (duration=%ums)", message.c_str(), (unsigned)duration_ms);
}

void SSNotification::dismiss_timer_cb(lv_timer_t* t) {
    auto* self = static_cast<SSNotification*>(t->user_data);
    if (self->overlay_) {
        lv_obj_del(self->overlay_);
        self->overlay_ = nullptr;
    }
    self->dismiss_timer_ = nullptr;  // LVGL frees the one-shot timer after this callback
}

// ---------------------------------------------------------------------------
// Thread-safe dispatch to LVGL task
// ---------------------------------------------------------------------------

void SSNotification::post_fn(std::function<void()> fn) {
    if (!pending_mutex_) return;
    if (xSemaphoreTake(pending_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending_queue_.push_back(std::move(fn));
        xSemaphoreGive(pending_mutex_);
    } else {
        ESP_LOGW(TAG, "post_fn: mutex timeout");
    }
}

void SSNotification::pump_timer_cb(lv_timer_t* t) {
    static_cast<SSNotification*>(t->user_data)->pump_pending();
}

void SSNotification::pump_pending() {
    std::vector<std::function<void()>> to_run;
    if (xSemaphoreTake(pending_mutex_, 0) == pdTRUE) {
        to_run = std::move(pending_queue_);
        pending_queue_.clear();
        xSemaphoreGive(pending_mutex_);
    }
    for (auto& fn : to_run) fn();
}
