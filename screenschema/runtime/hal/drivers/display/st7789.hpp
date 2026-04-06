#pragma once
#include "ss_display.hpp"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"

struct SSDisplayST7789Config {
    spi_host_device_t spi_host;
    int pin_cs, pin_dc, pin_rst, pin_backlight;
    int pin_sclk, pin_mosi, pin_miso;
    int width, height;
    uint8_t rotation;   // 0=landscape, 1=portrait (90°), 2=landscape inverted, 3=portrait inverted
    bool mirror_x;      // flip horizontally after rotation
    bool mirror_y;      // flip vertically after rotation
};

class SSDisplayST7789 : public ISSDisplay {
public:
    explicit SSDisplayST7789(const SSDisplayST7789Config& cfg);
    esp_err_t init() override;
    esp_err_t set_backlight(float level) override;
    uint16_t width() const override;
    uint16_t height() const override;
    lv_disp_t* lv_display() const override;
    friend void st7789_flush_cb(lv_disp_drv_t*, const lv_area_t*, lv_color_t*);
private:
    SSDisplayST7789Config cfg_;
    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_panel_io_handle_t io_ = nullptr;
    lv_disp_drv_t disp_drv_ = {};
    lv_disp_t* disp_ = nullptr;
    uint16_t w_ = 0, h_ = 0;
};
