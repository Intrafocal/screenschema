#pragma once
#include "hal/ss_touch.hpp"
#include "esp_lcd_touch_gt911.h"

struct SSTouchGT911Config {
    int sda_gpio;    // 7 on ESP32-P4
    int scl_gpio;    // 8 on ESP32-P4
    int rst_gpio;    // -1 if not connected
    int int_gpio;    // -1 if not connected
    int width;       // display width (for coordinate transform)
    int height;      // display height
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
};

class SSTouchGT911 : public ISSTouch {
public:
    explicit SSTouchGT911(const SSTouchGT911Config& cfg);
    esp_err_t init() override;
    uint8_t read(SSTouchPoint* points, uint8_t max_points) override;
    lv_indev_t* lv_indev() const override;
private:
    SSTouchGT911Config cfg_;
    esp_lcd_touch_handle_t touch_ = nullptr;
    lv_indev_drv_t indev_drv_;
    lv_indev_t* indev_ = nullptr;
};
