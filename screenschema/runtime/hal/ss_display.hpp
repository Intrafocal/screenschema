#pragma once
#include "esp_err.h"
#include "lvgl.h"

class ISSDisplay {
public:
    virtual ~ISSDisplay() = default;
    virtual esp_err_t init() = 0;
    virtual esp_err_t set_backlight(float level) = 0;  // 0.0–1.0
    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;
    virtual lv_disp_t* lv_display() const = 0;
};
