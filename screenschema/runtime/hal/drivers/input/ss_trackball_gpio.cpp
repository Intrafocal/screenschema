#include "ss_trackball_gpio.hpp"
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "SS_TRACKBALL";

SSTrackballGPIO::SSTrackballGPIO(const SSTrackballGPIOConfig& cfg)
    : cfg_(cfg) {
    cursor_x_ = cfg_.width / 2;
    cursor_y_ = cfg_.height / 2;
}

void SSTrackballGPIO::read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    auto* self = static_cast<SSTrackballGPIO*>(drv->user_data);

    // Pin order matches LILYGO's reference: right, up, left, down, click.
    // Each transition (rising or falling edge) on a direction pin moves the
    // virtual cursor by step_px pixels.  We don't care about high vs low —
    // only that the level changed since the last poll.
    const int pins[5] = {
        self->cfg_.pin_right,
        self->cfg_.pin_up,
        self->cfg_.pin_left,
        self->cfg_.pin_down,
        self->cfg_.pin_click,
    };

    bool click_pressed = false;
    bool any_event = false;
    int  step       = self->cfg_.step_px > 0 ? self->cfg_.step_px : 10;

    for (int i = 0; i < 5; i++) {
        bool level = gpio_get_level((gpio_num_t)pins[i]) != 0;
        if (level != self->last_level_[i]) {
            self->last_level_[i] = level;
            any_event = true;
            switch (i) {
                case 0:  // right
                    self->cursor_x_ += step;
                    if (self->cursor_x_ >= self->cfg_.width)  self->cursor_x_ = self->cfg_.width - 1;
                    break;
                case 1:  // up
                    self->cursor_y_ -= step;
                    if (self->cursor_y_ < 0) self->cursor_y_ = 0;
                    break;
                case 2:  // left
                    self->cursor_x_ -= step;
                    if (self->cursor_x_ < 0) self->cursor_x_ = 0;
                    break;
                case 3:  // down
                    self->cursor_y_ += step;
                    if (self->cursor_y_ >= self->cfg_.height) self->cursor_y_ = self->cfg_.height - 1;
                    break;
                case 4:  // click
                    click_pressed = true;
                    break;
            }
        }
    }

    if (!self->first_event_logged_ && any_event) {
        ESP_LOGI(TAG, "First trackball event detected — driver alive (cursor=%d,%d click=%d)",
                 self->cursor_x_, self->cursor_y_, click_pressed);
        self->first_event_logged_ = true;
    }

    data->point.x = self->cursor_x_;
    data->point.y = self->cursor_y_;
    data->state   = click_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

esp_err_t SSTrackballGPIO::init() {
    // Configure all five pins as input with internal pullup.  No ISR — read_cb
    // polls them via gpio_get_level().  This matches LILYGO's UnitTest
    // reference firmware and is robust against the GPIO 0 / BOOT pin sharing.
    const int pins[5] = {
        cfg_.pin_right, cfg_.pin_up, cfg_.pin_left, cfg_.pin_down, cfg_.pin_click,
    };

    for (int i = 0; i < 5; i++) {
        if (pins[i] < 0) continue;
        gpio_config_t io_cfg = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        esp_err_t ret = gpio_config(&io_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "gpio_config failed for pin %d: %s", pins[i], esp_err_to_name(ret));
            return ret;
        }
        // Seed last_level_ from the actual idle state so we don't fire spurious
        // edges on the first poll.
        last_level_[i] = gpio_get_level((gpio_num_t)pins[i]) != 0;
    }

    // Register as an LVGL pointer device — same input model as touch
    lv_indev_drv_init(&indev_drv_);
    indev_drv_.type      = LV_INDEV_TYPE_POINTER;
    indev_drv_.read_cb   = read_cb;
    indev_drv_.user_data = this;
    indev_ = lv_indev_drv_register(&indev_drv_);
    if (indev_ == nullptr) {
        ESP_LOGE(TAG, "lv_indev_drv_register returned nullptr");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Trackball initialized as pointer (UP=%d DOWN=%d LEFT=%d RIGHT=%d CLICK=%d, %dx%d step=%dpx)",
             cfg_.pin_up, cfg_.pin_down, cfg_.pin_left, cfg_.pin_right, cfg_.pin_click,
             cfg_.width, cfg_.height, cfg_.step_px > 0 ? cfg_.step_px : 10);
    return ESP_OK;
}

lv_indev_t* SSTrackballGPIO::lv_indev() const {
    return indev_;
}
