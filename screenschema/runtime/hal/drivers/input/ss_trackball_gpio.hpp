#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include <cstdint>
#include <atomic>

struct SSTrackballGPIOConfig {
    int pin_up, pin_down, pin_left, pin_right, pin_click;
};

class SSTrackballGPIO {
public:
    explicit SSTrackballGPIO(const SSTrackballGPIOConfig& cfg);
    esp_err_t init();
    lv_indev_t* lv_indev() const;

private:
    SSTrackballGPIOConfig cfg_;
    lv_indev_drv_t indev_drv_ = {};
    lv_indev_t* indev_ = nullptr;

    // ISR-safe tick counters — written in ISR, read/reset in LVGL callback
    std::atomic<int8_t> ticks_up_{0};
    std::atomic<int8_t> ticks_down_{0};
    std::atomic<int8_t> ticks_left_{0};
    std::atomic<int8_t> ticks_right_{0};
    std::atomic<bool>   click_pressed_{false};
    bool click_was_pressed_ = false;  // edge detection for release

    static void IRAM_ATTR isr_up(void* arg);
    static void IRAM_ATTR isr_down(void* arg);
    static void IRAM_ATTR isr_left(void* arg);
    static void IRAM_ATTR isr_right(void* arg);
    static void IRAM_ATTR isr_click(void* arg);
    static void read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data);

    esp_err_t configurePin(int pin, gpio_isr_t handler);
};
