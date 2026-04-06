#include "ft6236.hpp"
#include "esp_log.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"

static const char* TAG = "SS_FT6236";

SSTouchFT6236::SSTouchFT6236(const SSTouchFT6236Config& cfg) : cfg_(cfg) {
    memset(&indev_drv_, 0, sizeof(indev_drv_));
}

void ft6236_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    auto* self = static_cast<SSTouchFT6236*>(drv->user_data);
    SSTouchPoint pt = {};
    uint8_t n = self->read(&pt, 1);
    if (n > 0 && pt.pressed) {
        data->point.x = (lv_coord_t)pt.x;
        data->point.y = (lv_coord_t)pt.y;
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

esp_err_t SSTouchFT6236::init() {
    // I2C bus
    i2c_config_t i2c_conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = cfg_.sda_gpio,
        .scl_io_num       = cfg_.scl_gpio,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master           = { .clk_speed = 400000 },
    };
    i2c_param_config(I2C_NUM_0, &i2c_conf);
    esp_err_t ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Panel IO for touch
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr              = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS,
        .control_phase_bytes   = 1,
        .dc_bit_offset         = 0,
        .lcd_cmd_bits          = 8,
        .lcd_param_bits        = 8,
        .flags                 = { .disable_control_phase = 1 },
    };
    ret = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &io_cfg, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch panel IO failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Touch config
    esp_lcd_touch_config_t touch_cfg = {
        .x_max        = (uint16_t)cfg_.width,
        .y_max        = (uint16_t)cfg_.height,
        .rst_gpio_num = (gpio_num_t)cfg_.rst_gpio,
        .int_gpio_num = (gpio_num_t)cfg_.int_gpio,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = {
            .swap_xy  = (unsigned)cfg_.swap_xy,
            .mirror_x = (unsigned)cfg_.mirror_x,
            .mirror_y = (unsigned)cfg_.mirror_y,
        },
    };
    ret = esp_lcd_touch_new_i2c_ft5x06(io, &touch_cfg, &touch_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT6236 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // LVGL input device
    lv_indev_drv_init(&indev_drv_);
    indev_drv_.type      = LV_INDEV_TYPE_POINTER;
    indev_drv_.read_cb   = ft6236_read_cb;
    indev_drv_.user_data = this;
    indev_ = lv_indev_drv_register(&indev_drv_);

    ESP_LOGI(TAG, "FT6236 touch initialized (SDA=%d SCL=%d)", cfg_.sda_gpio, cfg_.scl_gpio);
    return ESP_OK;
}

uint8_t SSTouchFT6236::read(SSTouchPoint* points, uint8_t max_points) {
    esp_lcd_touch_read_data(touch_);
    uint16_t x[1], y[1], strength[1];
    uint8_t count = 0;
    bool got = esp_lcd_touch_get_coordinates(touch_, x, y, strength, &count, 1);
    if (got && count > 0) {
        points[0] = { x[0], y[0], 0, true };
        return 1;
    }
    return 0;
}

lv_indev_t* SSTouchFT6236::lv_indev() const { return indev_; }
