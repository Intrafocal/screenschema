#pragma once
#include "hal/ss_display.hpp"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"

struct SSDisplayST7701SDSIConfig {
    int width;            // 480
    int height;           // 800
    uint8_t rotation;     // 0-3
    int backlight_gpio;   // -1 if not GPIO-controlled
    // DSI bus config
    int lane_num;         // 2
    uint32_t lane_mbps;   // e.g. 500
};

class SSDisplayST7701SDSI : public ISSDisplay {
public:
    explicit SSDisplayST7701SDSI(const SSDisplayST7701SDSIConfig& cfg);
    esp_err_t init() override;
    esp_err_t set_backlight(float level) override;
    uint16_t width() const override;
    uint16_t height() const override;
    lv_disp_t* lv_display() const override;
    friend void flush_cb(lv_disp_drv_t*, const lv_area_t*, lv_color_t*);
private:
    SSDisplayST7701SDSIConfig cfg_;
    esp_lcd_dsi_bus_handle_t dsi_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    lv_disp_t* disp_ = nullptr;
    uint16_t w_, h_;
};
