#pragma once
#include "ss_touch.hpp"
#include "esp_lcd_touch_ft5x06.h"

struct SSTouchFT6236Config {
    int sda_gpio;
    int scl_gpio;
    int rst_gpio;   // -1 if not connected
    int int_gpio;   // -1 if not connected
    int width;
    int height;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
};

class SSTouchFT6236 : public ISSTouch {
public:
    explicit SSTouchFT6236(const SSTouchFT6236Config& cfg);
    esp_err_t init() override;
    uint8_t read(SSTouchPoint* points, uint8_t max_points) override;
    lv_indev_t* lv_indev() const override;
    friend void ft6236_read_cb(lv_indev_drv_t*, lv_indev_data_t*);
private:
    SSTouchFT6236Config cfg_;
    esp_lcd_touch_handle_t touch_ = nullptr;
    lv_indev_drv_t indev_drv_;
    lv_indev_t* indev_ = nullptr;
};
