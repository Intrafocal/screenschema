#include "ss_keyboard_i2c.hpp"
#include "ss_input.hpp"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

static const char* TAG = "SS_KBD_I2C";

SSKeyboardI2C::SSKeyboardI2C(const SSKeyboardI2CConfig& cfg)
    : cfg_(cfg) {}

uint32_t SSKeyboardI2C::mapToLvKey(uint8_t ascii) {
    switch (ascii) {
        case 0x08: return LV_KEY_BACKSPACE;
        case 0x7F: return LV_KEY_BACKSPACE;  // DEL
        case 0x0D: return LV_KEY_ENTER;
        case 0x0A: return LV_KEY_ENTER;
        case 0x1B: return LV_KEY_ESC;
        case 0x09: return LV_KEY_NEXT;       // Tab
        default:   return ascii;             // Printable characters pass through
    }
}

uint8_t SSKeyboardI2C::readKey() {
    uint8_t key = 0;
    esp_err_t ret = i2c_master_read_from_device(
        I2C_NUM_0, cfg_.i2c_addr, &key, 1, pdMS_TO_TICKS(10));
    if (ret != ESP_OK) {
        return 0;
    }
    return key;
}

void SSKeyboardI2C::read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    auto* self = static_cast<SSKeyboardI2C*>(drv->user_data);

    uint8_t raw = self->readKey();

    if (raw != 0) {
        // Let SSInput middleware intercept first (D5)
        if (SSInput::instance().dispatch(raw, SSKeySource::Keyboard)) {
            // Consumed — report released to LVGL
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        self->last_key_ = raw;
        self->key_pressed_ = true;
        data->key = mapToLvKey(raw);
        data->state = LV_INDEV_STATE_PR;
    } else {
        if (self->key_pressed_) {
            // Report release of previously pressed key
            data->key = mapToLvKey(self->last_key_);
            data->state = LV_INDEV_STATE_REL;
            self->key_pressed_ = false;
        } else {
            data->state = LV_INDEV_STATE_REL;
        }
    }
}

esp_err_t SSKeyboardI2C::init() {
    // I2C bus is shared with GT911 touch — it installs the driver first.
    // If already installed, i2c_driver_install returns ESP_ERR_INVALID_STATE.
    i2c_config_t i2c_conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = cfg_.sda_gpio,
        .scl_io_num       = cfg_.scl_gpio,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master           = { .clk_speed = 400000 },
        .clk_flags        = 0,
    };
    i2c_param_config(I2C_NUM_0, &i2c_conf);
    esp_err_t ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify keyboard is present on the bus
    uint8_t probe = 0;
    ret = i2c_master_read_from_device(
        I2C_NUM_0, cfg_.i2c_addr, &probe, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Keyboard not responding at 0x%02X: %s",
                 cfg_.i2c_addr, esp_err_to_name(ret));
    }

    // Register LVGL keypad input device
    lv_indev_drv_init(&indev_drv_);
    indev_drv_.type      = LV_INDEV_TYPE_KEYPAD;
    indev_drv_.read_cb   = read_cb;
    indev_drv_.user_data = this;
    indev_ = lv_indev_drv_register(&indev_drv_);
    if (indev_ == nullptr) {
        ESP_LOGE(TAG, "lv_indev_drv_register returned nullptr");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "I2C keyboard initialized (addr=0x%02X, INT=%d)",
             cfg_.i2c_addr, cfg_.int_gpio);
    return ESP_OK;
}

esp_err_t SSKeyboardI2C::setBacklight(uint8_t brightness) {
    if (!cfg_.backlight_ctrl) return ESP_ERR_NOT_SUPPORTED;
    uint8_t cmd[] = { 0x01, brightness };
    return i2c_master_write_to_device(
        I2C_NUM_0, cfg_.i2c_addr, cmd, sizeof(cmd), pdMS_TO_TICKS(10));
}

lv_indev_t* SSKeyboardI2C::lv_indev() const {
    return indev_;
}
