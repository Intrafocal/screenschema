#include "st7789.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

static const char* TAG = "SS_ST7789";

SSDisplayST7789::SSDisplayST7789(const SSDisplayST7789Config& cfg)
    : cfg_(cfg) {}

void st7789_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px_map) {
    auto* self = static_cast<SSDisplayST7789*>(drv->user_data);
    esp_lcd_panel_draw_bitmap(self->panel_,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_disp_flush_ready(drv);
}

esp_err_t SSDisplayST7789::init() {
    esp_err_t ret;

    // SPI bus — ignore "already initialized" if shared (e.g., display + SD card)
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = cfg_.pin_mosi,
        .miso_io_num     = cfg_.pin_miso,
        .sclk_io_num     = cfg_.pin_sclk,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = (int)(cfg_.width * cfg_.height * sizeof(uint16_t)),
    };
    ret = spi_bus_initialize(cfg_.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Panel IO
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = cfg_.pin_cs,
        .dc_gpio_num       = cfg_.pin_dc,
        .pclk_hz           = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg_.spi_host, &io_cfg, &io_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI panel IO failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // ST7789 panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num  = cfg_.pin_rst,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel  = 16,
    };
    ret = esp_lcd_new_panel_st7789(io_, &panel_cfg, &panel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 panel create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_lcd_panel_reset(panel_);
    esp_lcd_panel_init(panel_);

    // ST7789 needs inversion enabled for correct colors
    esp_lcd_panel_invert_color(panel_, true);

    // T-Deck custom init: LILYGO's ST7789V uses a non-standard gamma and voltage
    // sequence.  Send the LILYGO-specific register values after the standard init.
    // These come from the LILYGO TFT_eSPI fork (Setup210_LilyGo_T_Deck.h).
    // Positive gamma correction
    const uint8_t pgamma[] = {
        0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33,
        0x48, 0x17, 0x14, 0x15, 0x31, 0x34
    };
    esp_lcd_panel_io_tx_param(io_, 0xE0, pgamma, sizeof(pgamma));
    // Negative gamma correction
    const uint8_t ngamma[] = {
        0xD0, 0x08, 0x0E, 0x09, 0x09, 0x15, 0x31, 0x33,
        0x48, 0x17, 0x14, 0x15, 0x31, 0x34
    };
    esp_lcd_panel_io_tx_param(io_, 0xE1, ngamma, sizeof(ngamma));

    esp_lcd_panel_disp_on_off(panel_, true);

    // Rotation + optional per-axis swap/mirror (all three are XOR overrides
    // on top of the rotation-derived defaults).
    bool swap_xy  = (cfg_.rotation == 1 || cfg_.rotation == 3) ^ cfg_.swap_xy;
    bool mirror_x = (cfg_.rotation == 2 || cfg_.rotation == 3) ^ cfg_.mirror_x;
    bool mirror_y = (cfg_.rotation == 1 || cfg_.rotation == 2) ^ cfg_.mirror_y;
    esp_lcd_panel_swap_xy(panel_, swap_xy);
    esp_lcd_panel_mirror(panel_, mirror_x, mirror_y);

    w_ = (uint16_t)cfg_.width;
    h_ = (uint16_t)cfg_.height;

    // Backlight on
    set_backlight(1.0f);

    // LVGL draw buffers (double-buffered, 40 lines, DMA-capable internal RAM)
    size_t buf_pixels = (size_t)w_ * 40;
    lv_color_t* buf1 = (lv_color_t*)heap_caps_malloc(buf_pixels * sizeof(lv_color_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t* buf2 = (lv_color_t*)heap_caps_malloc(buf_pixels * sizeof(lv_color_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Draw buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_pixels);

    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res   = (lv_coord_t)w_;
    disp_drv_.ver_res   = (lv_coord_t)h_;
    disp_drv_.flush_cb  = st7789_flush_cb;
    disp_drv_.draw_buf  = &draw_buf;
    disp_drv_.user_data = this;

    disp_drv_.full_refresh = 1;  // always redraw entire screen, avoids stale display memory
    disp_ = lv_disp_drv_register(&disp_drv_);
    ESP_LOGI(TAG, "ST7789 initialized (%ux%u, rotation=%u)", w_, h_, cfg_.rotation);
    return ESP_OK;
}

esp_err_t SSDisplayST7789::set_backlight(float level) {
    if (cfg_.pin_backlight < 0) return ESP_OK;
    gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << cfg_.pin_backlight,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);
    gpio_set_level((gpio_num_t)cfg_.pin_backlight, level >= 0.5f ? 1 : 0);
    return ESP_OK;
}

uint16_t SSDisplayST7789::width()  const { return w_; }
uint16_t SSDisplayST7789::height() const { return h_; }
lv_disp_t* SSDisplayST7789::lv_display() const { return disp_; }
