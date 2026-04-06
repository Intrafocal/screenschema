#pragma once
#include "esp_err.h"
#include <cstdint>
#include <functional>

struct SSBatteryConfig {
    int   adc_gpio;          // GPIO number with battery voltage divider
    float voltage_divider;   // multiply ADC voltage by this to get battery voltage
    int   full_mv;           // 4200 for typical LiPo
    int   empty_mv;          // 3300 for typical LiPo
    int   sample_interval_ms; // how often to sample (default 30000)
};

struct SSBatteryReading {
    int     voltage_mv;
    uint8_t percent;        // 0–100
};

/// Battery monitoring (D9).  Periodically samples ADC, exposes voltage + percent.
class SSBattery {
public:
    static SSBattery& instance();

    esp_err_t init(const SSBatteryConfig& cfg);
    SSBatteryReading read();

    /// Register a callback fired on every sample (LVGL task).
    void onChange(std::function<void(SSBatteryReading)> cb);

private:
    SSBattery() = default;
    static void timer_cb(void* arg);

    SSBatteryConfig cfg_ = {};
    bool initialized_ = false;
    SSBatteryReading last_ = {};
    std::function<void(SSBatteryReading)> on_change_;
};
