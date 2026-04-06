#include "ss_hal.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

static const char* TAG = "SS_HAL";
static ISSDisplay* s_display = nullptr;
static ISSAudio*   s_audio   = nullptr;

esp_err_t ss_hal_init(const SSHalConfig& config) {
    esp_err_t ret;

    // 0. Initialize LVGL before any display/touch driver calls it
    lv_init();

    // 1. Initialize display
    ret = config.display->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_display = config.display;

    // 2. Initialize touch if present
    if (config.touch != nullptr) {
        ret = config.touch->init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // 3. Initialize audio if present
    if (config.audio != nullptr) {
        ret = config.audio->init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Audio init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_audio = config.audio;
    }

    // 4. Allocate LVGL draw buffers (double-buffering, each half of total)
    size_t total_bytes = config.lvgl_buf_kb * 1024;
    size_t half_bytes  = total_bytes / 2;

    void* buf1 = heap_caps_malloc(half_bytes, MALLOC_CAP_SPIRAM);
    void* buf2 = heap_caps_malloc(half_bytes, MALLOC_CAP_SPIRAM);

    if (buf1 == nullptr) {
        ESP_LOGW(TAG, "PSRAM allocation failed for buf1, falling back to internal DMA memory");
        buf1 = heap_caps_malloc(half_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (buf2 == nullptr) {
        ESP_LOGW(TAG, "PSRAM allocation failed for buf2, falling back to internal DMA memory");
        buf2 = heap_caps_malloc(half_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }

    if (buf1 == nullptr || buf2 == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers (requested %u bytes each)", (unsigned)half_bytes);
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        return ESP_ERR_NO_MEM;
    }

    // 5. Log buffer addresses and sizes
    ESP_LOGI(TAG, "LVGL draw buf1: %p, size: %u bytes", buf1, (unsigned)half_bytes);
    ESP_LOGI(TAG, "LVGL draw buf2: %p, size: %u bytes", buf2, (unsigned)half_bytes);

    return ESP_OK;
}

ISSDisplay* ss_hal_display() {
    return s_display;
}

ISSAudio* ss_hal_audio() {
    return s_audio;
}
