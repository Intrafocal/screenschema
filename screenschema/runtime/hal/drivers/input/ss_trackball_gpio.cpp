#include "ss_trackball_gpio.hpp"
#include "ss_input.hpp"
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "SS_TRACKBALL";

SSTrackballGPIO::SSTrackballGPIO(const SSTrackballGPIOConfig& cfg)
    : cfg_(cfg) {}

// --- ISR handlers (IRAM-resident, minimal work) ---

void IRAM_ATTR SSTrackballGPIO::isr_up(void* arg) {
    auto* self = static_cast<SSTrackballGPIO*>(arg);
    self->ticks_up_.fetch_add(1, std::memory_order_relaxed);
}

void IRAM_ATTR SSTrackballGPIO::isr_down(void* arg) {
    auto* self = static_cast<SSTrackballGPIO*>(arg);
    self->ticks_down_.fetch_add(1, std::memory_order_relaxed);
}

void IRAM_ATTR SSTrackballGPIO::isr_left(void* arg) {
    auto* self = static_cast<SSTrackballGPIO*>(arg);
    self->ticks_left_.fetch_add(1, std::memory_order_relaxed);
}

void IRAM_ATTR SSTrackballGPIO::isr_right(void* arg) {
    auto* self = static_cast<SSTrackballGPIO*>(arg);
    self->ticks_right_.fetch_add(1, std::memory_order_relaxed);
}

void IRAM_ATTR SSTrackballGPIO::isr_click(void* arg) {
    auto* self = static_cast<SSTrackballGPIO*>(arg);
    self->click_pressed_.store(true, std::memory_order_relaxed);
}

// --- LVGL read callback ---

void SSTrackballGPIO::read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    auto* self = static_cast<SSTrackballGPIO*>(drv->user_data);

    // Read and reset tick counters atomically
    int8_t up    = self->ticks_up_.exchange(0, std::memory_order_relaxed);
    int8_t down  = self->ticks_down_.exchange(0, std::memory_order_relaxed);
    int8_t left  = self->ticks_left_.exchange(0, std::memory_order_relaxed);
    int8_t right = self->ticks_right_.exchange(0, std::memory_order_relaxed);
    bool click   = self->click_pressed_.exchange(false, std::memory_order_relaxed);

    // One-shot debug log on first detected event — confirms ISRs are firing
    // and the read_cb is being polled.  Helps diagnose B12-style "trackball
    // looks dead" issues without spamming the log on normal use.
    if (!self->first_event_logged_ && (up || down || left || right || click)) {
        ESP_LOGI(TAG, "First trackball event: up=%d down=%d left=%d right=%d click=%d",
                 up, down, left, right, click);
        self->first_event_logged_ = true;
    }

    // Handle click — press on ISR edge, release on next poll with no click
    if (click) {
        // Let SSInput middleware intercept (D5)
        if (!SSInput::instance().dispatch(LV_KEY_ENTER, SSKeySource::Trackball)) {
            data->key = LV_KEY_ENTER;
            data->state = LV_INDEV_STATE_PR;
            self->click_was_pressed_ = true;
            return;
        }
    }
    if (self->click_was_pressed_ && !click) {
        data->key = LV_KEY_ENTER;
        data->state = LV_INDEV_STATE_REL;
        self->click_was_pressed_ = false;
        return;
    }

    // Pick the dominant direction (most accumulated ticks)
    struct { int8_t ticks; uint32_t key; } dirs[] = {
        { up,    LV_KEY_UP    },
        { down,  LV_KEY_DOWN  },
        { left,  LV_KEY_LEFT  },
        { right, LV_KEY_RIGHT },
    };

    int8_t max_ticks = 0;
    uint32_t best_key = 0;
    for (auto& d : dirs) {
        if (d.ticks > max_ticks) {
            max_ticks = d.ticks;
            best_key = d.key;
        }
    }

    if (max_ticks > 0) {
        // Let SSInput middleware intercept (D5)
        if (SSInput::instance().dispatch((uint8_t)best_key, SSKeySource::Trackball)) {
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        data->key = best_key;
        data->state = LV_INDEV_STATE_PR;

        // If there are remaining ticks, put them back for the next poll.
        // This gives natural acceleration — faster rolling = more events per second.
        if (max_ticks > 1) {
            int8_t remaining = max_ticks - 1;
            if (best_key == LV_KEY_UP)    self->ticks_up_.fetch_add(remaining, std::memory_order_relaxed);
            if (best_key == LV_KEY_DOWN)  self->ticks_down_.fetch_add(remaining, std::memory_order_relaxed);
            if (best_key == LV_KEY_LEFT)  self->ticks_left_.fetch_add(remaining, std::memory_order_relaxed);
            if (best_key == LV_KEY_RIGHT) self->ticks_right_.fetch_add(remaining, std::memory_order_relaxed);
            data->continue_reading = true;  // Tell LVGL to call us again immediately
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// --- Pin configuration ---

esp_err_t SSTrackballGPIO::configurePin(int pin, gpio_isr_t handler) {
    if (pin < 0) return ESP_OK;

    gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,  // Active low pulses
    };
    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) return ret;

    return gpio_isr_handler_add((gpio_num_t)pin, handler, this);
}

esp_err_t SSTrackballGPIO::init() {
    // Install GPIO ISR service (shared, may already be installed)
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure direction pins
    ret = configurePin(cfg_.pin_up, isr_up);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to configure UP pin"); return ret; }
    ret = configurePin(cfg_.pin_down, isr_down);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to configure DOWN pin"); return ret; }
    ret = configurePin(cfg_.pin_left, isr_left);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to configure LEFT pin"); return ret; }
    ret = configurePin(cfg_.pin_right, isr_right);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to configure RIGHT pin"); return ret; }
    ret = configurePin(cfg_.pin_click, isr_click);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to configure CLICK pin"); return ret; }

    // Register LVGL keypad input device
    lv_indev_drv_init(&indev_drv_);
    indev_drv_.type      = LV_INDEV_TYPE_KEYPAD;
    indev_drv_.read_cb   = read_cb;
    indev_drv_.user_data = this;
    indev_ = lv_indev_drv_register(&indev_drv_);
    if (indev_ == nullptr) {
        ESP_LOGE(TAG, "lv_indev_drv_register returned nullptr");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Trackball initialized (UP=%d DOWN=%d LEFT=%d RIGHT=%d CLICK=%d)",
             cfg_.pin_up, cfg_.pin_down, cfg_.pin_left, cfg_.pin_right, cfg_.pin_click);
    return ESP_OK;
}

lv_indev_t* SSTrackballGPIO::lv_indev() const {
    return indev_;
}
