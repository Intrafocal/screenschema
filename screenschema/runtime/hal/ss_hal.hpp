#pragma once
#include "esp_err.h"
#include "ss_display.hpp"
#include "ss_touch.hpp"
#include "ss_audio.hpp"

struct SSHalConfig {
    ISSDisplay* display;
    ISSTouch*   touch;       // may be nullptr
    ISSAudio*   audio;       // may be nullptr
    uint32_t    lvgl_buf_kb; // LVGL draw buffer size in KB
};

esp_err_t ss_hal_init(const SSHalConfig& config);

// Access HAL subsystems after init
ISSDisplay* ss_hal_display();
ISSAudio*   ss_hal_audio();
