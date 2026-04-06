#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include <cstdint>

struct SSKeyboardI2CConfig {
    uint8_t i2c_addr;       // 0x55 for T-Deck
    int     int_gpio;       // Interrupt GPIO (-1 for polling only)
    bool    backlight_ctrl; // true if keyboard supports brightness via I2C
    int     sda_gpio;       // Shared I2C bus (same as touch)
    int     scl_gpio;
};

class SSKeyboardI2C {
public:
    explicit SSKeyboardI2C(const SSKeyboardI2CConfig& cfg);
    esp_err_t init();
    lv_indev_t* lv_indev() const;

    /// Set keyboard backlight brightness (0–255). Only works if backlight_ctrl is true.
    esp_err_t setBacklight(uint8_t brightness);

private:
    SSKeyboardI2CConfig cfg_;
    lv_indev_drv_t indev_drv_ = {};
    lv_indev_t* indev_ = nullptr;
    uint8_t last_key_ = 0;
    bool key_pressed_ = false;

    static void read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data);
    uint8_t readKey();
    static uint32_t mapToLvKey(uint8_t ascii);
};
