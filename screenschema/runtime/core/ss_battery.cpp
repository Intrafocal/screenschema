#include "ss_battery.hpp"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "lvgl.h"

static const char* TAG = "SS_BATTERY";

static adc_oneshot_unit_handle_t s_adc_handle = nullptr;
static adc_cali_handle_t          s_cali_handle = nullptr;
static adc_channel_t              s_channel = ADC_CHANNEL_0;

SSBattery& SSBattery::instance() {
    static SSBattery inst;
    return inst;
}

// Map a GPIO to (unit, channel) for ESP32-S3.  Limited table — extend as needed.
// On ESP32-S3, ADC1 covers GPIO 1-10 as ADC1_CH0-9.
static bool gpio_to_adc(int gpio, adc_unit_t& unit, adc_channel_t& channel) {
    if (gpio >= 1 && gpio <= 10) {
        unit = ADC_UNIT_1;
        channel = (adc_channel_t)(gpio - 1);
        return true;
    }
    return false;
}

esp_err_t SSBattery::init(const SSBatteryConfig& cfg) {
    if (initialized_) return ESP_OK;
    cfg_ = cfg;

    adc_unit_t unit;
    if (!gpio_to_adc(cfg.adc_gpio, unit, s_channel)) {
        ESP_LOGE(TAG, "GPIO %d is not a supported ADC pin", cfg.adc_gpio);
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id  = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,   // ~0–3.3V range
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, s_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    // Calibration (curve fitting on ESP32-S3)
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = unit,
        .chan     = s_channel,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration unavailable: %s — readings will be raw",
                 esp_err_to_name(err));
        s_cali_handle = nullptr;
    }

    initialized_ = true;

    // Start periodic sampling
    int interval = cfg_.sample_interval_ms > 0 ? cfg_.sample_interval_ms : 30000;
    lv_timer_create(timer_cb, interval, this);

    // Take an immediate first sample
    last_ = read();

    ESP_LOGI(TAG, "Battery init (GPIO %d, divider=%.2f, %d-%dmV, sample %dms)",
             cfg.adc_gpio, cfg.voltage_divider, cfg.empty_mv, cfg.full_mv, interval);
    return ESP_OK;
}

SSBatteryReading SSBattery::read() {
    SSBatteryReading r = {};
    if (!initialized_) return r;

    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, s_channel, &raw) != ESP_OK) {
        return last_;
    }

    int adc_mv = 0;
    if (s_cali_handle) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &adc_mv);
    } else {
        // Fallback: rough scaling assuming 12-bit, ADC_ATTEN_DB_12 → ~3100mV full scale
        adc_mv = (raw * 3100) / 4095;
    }

    r.voltage_mv = (int)(adc_mv * cfg_.voltage_divider);

    int range = cfg_.full_mv - cfg_.empty_mv;
    if (range <= 0) {
        r.percent = 0;
    } else if (r.voltage_mv >= cfg_.full_mv) {
        r.percent = 100;
    } else if (r.voltage_mv <= cfg_.empty_mv) {
        r.percent = 0;
    } else {
        r.percent = (uint8_t)(((r.voltage_mv - cfg_.empty_mv) * 100) / range);
    }

    last_ = r;
    return r;
}

void SSBattery::onChange(std::function<void(SSBatteryReading)> cb) {
    on_change_ = std::move(cb);
}

void SSBattery::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<SSBattery*>(t->user_data);
    SSBatteryReading r = self->read();
    if (self->on_change_) self->on_change_(r);
}
