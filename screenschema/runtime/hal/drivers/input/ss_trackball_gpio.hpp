#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include <cstdint>

struct SSTrackballGPIOConfig {
    int pin_up, pin_down, pin_left, pin_right, pin_click;
    int width;        // display width — used to clamp the virtual cursor
    int height;       // display height
    int step_px;      // pixels moved per detected edge (typical: 10)
};

/// GPIO trackball driver — exposes the LilyGO-style optical trackball as an
/// LVGL pointer device with an accumulated x/y position.  Each transition on a
/// direction pin advances the virtual cursor by step_px pixels.  Click on the
/// shared GPIO 0 / BOOT pin maps to LV_INDEV_STATE_PRESSED at the current
/// cursor position.
///
/// This is the same input model LILYGO's factory firmware uses, which means
/// brookesia (and any LVGL touch UI) handles trackball events identically to
/// touch — no special group binding needed.
class SSTrackballGPIO {
public:
    explicit SSTrackballGPIO(const SSTrackballGPIOConfig& cfg);
    esp_err_t init();
    lv_indev_t* lv_indev() const;

private:
    SSTrackballGPIOConfig cfg_;
    lv_indev_drv_t indev_drv_ = {};
    lv_indev_t* indev_ = nullptr;

    // Accumulated virtual cursor position (clamped to display bounds)
    int16_t cursor_x_ = 0;
    int16_t cursor_y_ = 0;

    // Edge detection — last polled level for each input pin
    bool last_level_[5] = { true, true, true, true, true };  // pull-ups → idle high

    bool first_event_logged_ = false;  // one-shot debug confirmation

    static void read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data);
};
