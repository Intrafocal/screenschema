#pragma once
#include <cstdint>
#include "esp_err.h"
#include "lvgl.h"

struct SSTouchPoint {
    uint16_t x, y;
    uint8_t  id;
    bool     pressed;
};

class ISSTouch {
public:
    virtual ~ISSTouch() = default;
    virtual esp_err_t init() = 0;
    virtual uint8_t read(SSTouchPoint* points, uint8_t max_points) = 0;
    virtual lv_indev_t* lv_indev() const = 0;
};
