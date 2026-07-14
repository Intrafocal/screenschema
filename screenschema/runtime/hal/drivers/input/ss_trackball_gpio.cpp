#include "ss_trackball_gpio.hpp"
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "SS_TRACKBALL";

SSTrackballGPIO::SSTrackballGPIO(const SSTrackballGPIOConfig& cfg)
    : cfg_(cfg) {
    cursor_x_ = cfg_.width / 2;
    cursor_y_ = cfg_.height / 2;
}

void SSTrackballGPIO::long_press_async(void* self_ptr) {
    auto* self = static_cast<SSTrackballGPIO*>(self_ptr);
    if (self->cfg_.on_long_press) self->cfg_.on_long_press();
}

void SSTrackballGPIO::read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    auto* self = static_cast<SSTrackballGPIO*>(drv->user_data);

    // Pin order matches LILYGO's reference: right, up, left, down.
    // Each transition (rising or falling edge) on a direction pin moves the
    // virtual cursor by step_px pixels.  We don't care about high vs low —
    // only that the level changed since the last poll.
    const int dir_pins[4] = {
        self->cfg_.pin_right,
        self->cfg_.pin_up,
        self->cfg_.pin_left,
        self->cfg_.pin_down,
    };

    bool moved = false;
    int  step   = self->cfg_.step_px > 0 ? self->cfg_.step_px : 10;
    int  over_x = 0;  // movement swallowed by the screen-edge clamp this poll
    int  over_y = 0;

    for (int i = 0; i < 4; i++) {
        bool level = gpio_get_level((gpio_num_t)dir_pins[i]) != 0;
        if (level != self->last_level_[i]) {
            self->last_level_[i] = level;
            moved = true;
            switch (i) {
                case 0:  // right
                    self->cursor_x_ += step;
                    if (self->cursor_x_ >= self->cfg_.width) {
                        over_x += self->cursor_x_ - (self->cfg_.width - 1);
                        self->cursor_x_ = self->cfg_.width - 1;
                    }
                    break;
                case 1:  // up
                    self->cursor_y_ -= step;
                    if (self->cursor_y_ < 0) { over_y += self->cursor_y_; self->cursor_y_ = 0; }
                    break;
                case 2:  // left
                    self->cursor_x_ -= step;
                    if (self->cursor_x_ < 0) { over_x += self->cursor_x_; self->cursor_x_ = 0; }
                    break;
                case 3:  // down
                    self->cursor_y_ += step;
                    if (self->cursor_y_ >= self->cfg_.height) {
                        over_y += self->cursor_y_ - (self->cfg_.height - 1);
                        self->cursor_y_ = self->cfg_.height - 1;
                    }
                    break;
            }
        }
    }

    // Click is level-based (active low, pull-up): holding the ball down keeps
    // LV_INDEV_STATE_PRESSED asserted, so hold+roll works as an LVGL drag.
    bool pressed = gpio_get_level((gpio_num_t)self->cfg_.pin_click) == 0;

    if (pressed && !self->was_pressed_) {
        self->press_start_ = lv_tick_get();
        self->press_moved_ = false;
        self->long_fired_  = false;
    }
    if (pressed && moved) self->press_moved_ = true;  // drag intent, not long-press
    if (pressed && !self->long_fired_ && !self->press_moved_ &&
        self->cfg_.long_press_ms > 0 && self->cfg_.on_long_press &&
        lv_tick_elaps(self->press_start_) >= (uint32_t)self->cfg_.long_press_ms) {
        self->long_fired_ = true;
        // Cancel the in-flight press so the widget under the cursor doesn't
        // also get CLICKED on release, and defer the action out of the indev
        // read — it may delete the widget tree under us.
        lv_indev_reset(self->indev_, nullptr);
        lv_async_call(long_press_async, self);
    }

    // Edge-scroll: rolling against a screen edge scrolls the scrollable under
    // the cursor (only while not pressed — a held click is LVGL's own drag).
    // The object pinned under the cursor may be a non-scrollable overlay
    // (e.g. the brookesia status bar at the top edge), so probe progressively
    // inward from the edge until something scrollable is hit.
    if (!pressed && (over_x != 0 || over_y != 0)) {
        // "Scrollable" flag alone isn't enough — LVGL screens carry it by
        // default with no overflow — so require actual room in the direction
        // being scrolled.
        auto can_scroll = [&](lv_obj_t* o) {
            if (!lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE)) return false;
            return (over_y > 0 && lv_obj_get_scroll_bottom(o) > 0) ||
                   (over_y < 0 && lv_obj_get_scroll_top(o)    > 0) ||
                   (over_x > 0 && lv_obj_get_scroll_right(o)  > 0) ||
                   (over_x < 0 && lv_obj_get_scroll_left(o)   > 0);
        };
        lv_obj_t* target = nullptr;
        for (int inset = 0; inset <= 60 && !target; inset += 20) {
            lv_point_t p = { self->cursor_x_, self->cursor_y_ };
            if (over_x > 0) p.x -= inset; else if (over_x < 0) p.x += inset;
            if (over_y > 0) p.y -= inset; else if (over_y < 0) p.y += inset;
            lv_obj_t* hit = lv_indev_search_obj(lv_scr_act(), &p);
            while (hit && !can_scroll(hit)) hit = lv_obj_get_parent(hit);
            target = hit;
        }
        if (target) {
            // Rolling down at the bottom edge reveals content below → content
            // moves up → negative delta (same convention as touch drag).
            lv_obj_scroll_by_bounded(target, -over_x, -over_y, LV_ANIM_OFF);
        }
    }

    if (!self->first_event_logged_ && (moved || pressed != self->was_pressed_)) {
        ESP_LOGI(TAG, "First trackball event detected — driver alive (cursor=%d,%d click=%d)",
                 self->cursor_x_, self->cursor_y_, pressed);
        self->first_event_logged_ = true;
    }
    self->was_pressed_ = pressed;

    data->point.x = self->cursor_x_;
    data->point.y = self->cursor_y_;
    // After a long-press fires, suppress the press until physical release so
    // LVGL sees the hold as cancelled rather than a fresh press.
    data->state = (pressed && !self->long_fired_) ? LV_INDEV_STATE_PRESSED
                                                  : LV_INDEV_STATE_RELEASED;
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

    // Create a visible cursor on the top layer so it floats above all screens
    // (including any brookesia launcher / app screens).  LVGL repositions the
    // cursor object automatically on every pointer event from this indev.
    cursor_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(cursor_);
    lv_obj_set_size(cursor_, 14, 14);
    lv_obj_set_style_radius(cursor_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cursor_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(cursor_, LV_OPA_70, 0);
    lv_obj_set_style_border_color(cursor_, lv_color_black(), 0);
    lv_obj_set_style_border_width(cursor_, 2, 0);
    lv_obj_set_style_border_opa(cursor_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(cursor_, LV_OBJ_FLAG_CLICKABLE);  // don't intercept its own clicks
    lv_indev_set_cursor(indev_, cursor_);

    ESP_LOGI(TAG, "Trackball initialized as pointer (UP=%d DOWN=%d LEFT=%d RIGHT=%d CLICK=%d, %dx%d step=%dpx)",
             cfg_.pin_up, cfg_.pin_down, cfg_.pin_left, cfg_.pin_right, cfg_.pin_click,
             cfg_.width, cfg_.height, cfg_.step_px > 0 ? cfg_.step_px : 10);
    return ESP_OK;
}

lv_indev_t* SSTrackballGPIO::lv_indev() const {
    return indev_;
}
