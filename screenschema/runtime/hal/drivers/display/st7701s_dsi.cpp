#include "st7701s_dsi.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_lcd_st7701.h"

static const char* TAG = "SS_ST7701S";

SSDisplayST7701SDSI::SSDisplayST7701SDSI(const SSDisplayST7701SDSIConfig& cfg)
    : cfg_(cfg), w_(0), h_(0) {}

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px_map) {
    auto* self = static_cast<SSDisplayST7701SDSI*>(drv->user_data);
    esp_lcd_panel_draw_bitmap(self->panel_,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_disp_flush_ready(drv);
}

esp_err_t SSDisplayST7701SDSI::init() {
    esp_err_t ret;

    // 1. Create MIPI DSI bus
    esp_lcd_dsi_bus_config_t dsi_bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = cfg_.lane_num,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = cfg_.lane_mbps,
    };
    ret = esp_lcd_new_dsi_bus(&dsi_bus_cfg, &dsi_bus_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DSI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Create panel IO (DBI over DSI)
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(dsi_bus_, &dbi_cfg, &panel_io_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DBI panel IO: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Create ST7701S panel using driver defaults (no custom init sequence)
    esp_lcd_st7701_vendor_config_t vendor_cfg = {
        .init_cmds = NULL,
        .init_cmds_size = 0,
        .flags = {
            .use_mipi_interface = 1,
        },
        .mipi_config = {
            .dsi_bus = dsi_bus_,
            .display_resolution = {
                .h_size = (uint32_t)cfg_.width,
                .v_size = (uint32_t)cfg_.height,
            },
            .lane_num = (uint8_t)cfg_.lane_num,
        },
    };

    esp_lcd_panel_dev_config_t panel_dev_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ret = esp_lcd_new_panel_st7701(panel_io_, &panel_dev_cfg, &panel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ST7701S panel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Reset and initialize panel
    ret = esp_lcd_panel_reset(panel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_lcd_panel_init(panel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 5. Turn display on
    ret = esp_lcd_panel_disp_on_off(panel_, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn display on: %s", esp_err_to_name(ret));
        return ret;
    }

    // 6. Apply rotation
    bool swap_xy  = (cfg_.rotation == 1 || cfg_.rotation == 3);
    bool mirror_x = (cfg_.rotation == 2 || cfg_.rotation == 3);
    bool mirror_y = (cfg_.rotation == 1 || cfg_.rotation == 2);
    esp_lcd_panel_swap_xy(panel_, swap_xy);
    esp_lcd_panel_mirror(panel_, mirror_x, mirror_y);

    // Compute logical dimensions after rotation
    if (swap_xy) {
        w_ = (uint16_t)cfg_.height;
        h_ = (uint16_t)cfg_.width;
    } else {
        w_ = (uint16_t)cfg_.width;
        h_ = (uint16_t)cfg_.height;
    }

    // 7. Register LVGL display driver
    // Allocate a partial draw buffer: width * 10 lines in PSRAM
    size_t draw_buf_size = (size_t)w_ * 10 * sizeof(lv_color_t);
    lv_color_t* draw_buf = (lv_color_t*)heap_caps_malloc(draw_buf_size, MALLOC_CAP_SPIRAM);
    if (draw_buf == nullptr) {
        ESP_LOGW(TAG, "PSRAM draw buffer alloc failed, falling back to internal");
        draw_buf = (lv_color_t*)heap_caps_malloc(draw_buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (draw_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer (%u bytes)", (unsigned)draw_buf_size);
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buf_desc;
    lv_disp_draw_buf_init(&draw_buf_desc, draw_buf, nullptr, w_ * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res    = (lv_coord_t)w_;
    disp_drv.ver_res    = (lv_coord_t)h_;
    disp_drv.flush_cb   = flush_cb;
    disp_drv.draw_buf   = &draw_buf_desc;
    disp_drv.user_data  = this;

    // 8. Register and store
    disp_ = lv_disp_drv_register(&disp_drv);
    if (disp_ == nullptr) {
        ESP_LOGE(TAG, "lv_disp_drv_register returned nullptr");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ST7701S DSI display initialized (%ux%u, rotation=%u)", w_, h_, cfg_.rotation);
    return ESP_OK;
}

esp_err_t SSDisplayST7701SDSI::set_backlight(float level) {
    if (cfg_.backlight_gpio < 0) {
        // Hardware-controlled backlight — nothing to do
        return ESP_OK;
    }

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << cfg_.backlight_gpio),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);
    gpio_set_level((gpio_num_t)cfg_.backlight_gpio, (level >= 0.5f) ? 1 : 0);
    return ESP_OK;
}

uint16_t SSDisplayST7701SDSI::width() const {
    return w_;
}

uint16_t SSDisplayST7701SDSI::height() const {
    return h_;
}

lv_disp_t* SSDisplayST7701SDSI::lv_display() const {
    return disp_;
}
