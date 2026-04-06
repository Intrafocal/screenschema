#include "gt911.hpp"
#include "esp_log.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"

static const char* TAG = "SS_GT911";

SSTouchGT911::SSTouchGT911(const SSTouchGT911Config& cfg)
    : cfg_(cfg) {}

static void read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    auto* self = static_cast<SSTouchGT911*>(drv->user_data);
    SSTouchPoint pt;
    uint8_t n = self->read(&pt, 1);
    if (n > 0 && pt.pressed) {
        data->point.x = pt.x;
        data->point.y = pt.y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

esp_err_t SSTouchGT911::init() {
    esp_err_t ret;

    // 1. Configure and install I2C driver
    i2c_config_t i2c_conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = cfg_.sda_gpio,
        .scl_io_num       = cfg_.scl_gpio,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master           = {
            .clk_speed    = 400000,
        },
        .clk_flags        = 0,
    };
    ret = i2c_param_config(I2C_NUM_0, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Create I2C panel IO for GT911
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr            = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .on_color_trans_done = nullptr,
        .user_ctx            = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset       = 0,
        .lcd_cmd_bits        = 16,
        .lcd_param_bits      = 8,
        .flags               = {
            .dc_low_on_data  = 0,
            .disable_control_phase = 0,
        },
    };
    ret = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &io_config, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C panel IO: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Configure and create GT911 touch handle
    esp_lcd_touch_config_t touch_cfg = {
        .x_max       = (uint16_t)cfg_.width,
        .y_max       = (uint16_t)cfg_.height,
        .rst_gpio_num = (gpio_num_t)cfg_.rst_gpio,
        .int_gpio_num = (gpio_num_t)cfg_.int_gpio,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = (uint32_t)cfg_.swap_xy,
            .mirror_x = (uint32_t)cfg_.mirror_x,
            .mirror_y = (uint32_t)cfg_.mirror_y,
        },
        .process_coordinates = nullptr,
        .interrupt_callback  = nullptr,
        .user_data           = nullptr,
    };
    ret = esp_lcd_touch_new_i2c_gt911(io, &touch_cfg, &touch_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT911 touch handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Register LVGL input device
    lv_indev_drv_init(&indev_drv_);
    indev_drv_.type      = LV_INDEV_TYPE_POINTER;
    indev_drv_.read_cb   = read_cb;
    indev_drv_.user_data = this;
    indev_ = lv_indev_drv_register(&indev_drv_);
    if (indev_ == nullptr) {
        ESP_LOGE(TAG, "lv_indev_drv_register returned nullptr");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GT911 touch initialized (SDA=%d, SCL=%d, %dx%d)",
             cfg_.sda_gpio, cfg_.scl_gpio, cfg_.width, cfg_.height);
    return ESP_OK;
}

uint8_t SSTouchGT911::read(SSTouchPoint* points, uint8_t max_points) {
    if (touch_ == nullptr || points == nullptr || max_points == 0) {
        return 0;
    }

    esp_lcd_touch_read_data(touch_);

    uint16_t touch_x[5];
    uint16_t touch_y[5];
    uint16_t touch_strength[5];
    uint8_t  touch_cnt = 0;

    uint8_t read_max = (max_points > 5) ? 5 : max_points;
    bool touched = esp_lcd_touch_get_coordinates(touch_,
        touch_x, touch_y, touch_strength, &touch_cnt, read_max);

    if (!touched || touch_cnt == 0) {
        // Report the first point as released if caller only wants 1
        if (max_points >= 1) {
            points[0].x       = 0;
            points[0].y       = 0;
            points[0].id      = 0;
            points[0].pressed = false;
        }
        return 0;
    }

    uint8_t out = (touch_cnt < max_points) ? touch_cnt : max_points;
    for (uint8_t i = 0; i < out; i++) {
        points[i].x       = touch_x[i];
        points[i].y       = touch_y[i];
        points[i].id      = i;
        points[i].pressed = true;
    }
    return out;
}

lv_indev_t* SSTouchGT911::lv_indev() const {
    return indev_;
}
