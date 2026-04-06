#include "ss_audio_es8311.hpp"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cmath>

static const char* TAG = "SS_AUDIO_ES8311";

SSAudioES8311::SSAudioES8311(const SSAudioES8311Config& cfg) : cfg_(cfg) {}

SSAudioES8311::~SSAudioES8311() {
    ampOff();
    if (tx_chan_) {
        auto h = static_cast<i2s_chan_handle_t>(tx_chan_);
        i2s_channel_disable(h);
        i2s_del_channel(h);
    }
    if (rx_chan_) {
        auto h = static_cast<i2s_chan_handle_t>(rx_chan_);
        i2s_channel_disable(h);
        i2s_del_channel(h);
    }
}

// ---------------------------------------------------------------------------
// I2C codec register access (uses legacy API — bus already init'd by touch)
// ---------------------------------------------------------------------------

void SSAudioES8311::codecWrite(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_master_write_to_device(I2C_NUM_0, cfg_.codec_i2c_addr, buf, 2, pdMS_TO_TICKS(100));
}

uint8_t SSAudioES8311::codecRead(uint8_t reg) {
    uint8_t val = 0;
    i2c_master_write_read_device(I2C_NUM_0, cfg_.codec_i2c_addr, &reg, 1, &val, 1, pdMS_TO_TICKS(100));
    return val;
}

// ---------------------------------------------------------------------------
// MCLK generation via LEDC PWM — 256 * sample_rate
// ---------------------------------------------------------------------------

void SSAudioES8311::initMclk() {
    uint32_t mclk_freq = 256 * cfg_.sample_rate;  // e.g. 4,096,000 Hz for 16kHz

    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer_conf.duty_resolution = LEDC_TIMER_1_BIT;
    timer_conf.timer_num       = LEDC_TIMER_0;
    timer_conf.freq_hz         = mclk_freq;
    timer_conf.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {};
    ch_conf.gpio_num   = cfg_.pin_mck;
    ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_conf.channel    = LEDC_CHANNEL_0;
    ch_conf.timer_sel  = LEDC_TIMER_0;
    ch_conf.duty       = 1;  // 50% duty for 1-bit resolution
    ch_conf.hpoint     = 0;
    ledc_channel_config(&ch_conf);

    ESP_LOGI(TAG, "MCLK started on GPIO %d (%lu Hz)", cfg_.pin_mck, (unsigned long)mclk_freq);
}

// ---------------------------------------------------------------------------
// ES8311 codec init — register sequence from manufacturer reference
// ---------------------------------------------------------------------------

void SSAudioES8311::initCodec() {
    // Reset
    codecWrite(0x00, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(20));
    codecWrite(0x00, 0x00);
    codecWrite(0x00, 0x80);  // Power on

    // Clock: MCLK from pin, all clocks on
    codecWrite(0x01, 0x3F);

    // Dividers for MCLK = 256 * sample_rate
    codecWrite(0x02, 0x20);
    codecWrite(0x03, 0x10);
    codecWrite(0x04, 0x10);
    codecWrite(0x05, 0x00);
    codecWrite(0x06, 0x03);
    codecWrite(0x07, 0x00);
    codecWrite(0x08, 0xFF);

    // I2S format: slave, 16-bit
    codecWrite(0x09, 0x0C);
    codecWrite(0x0A, 0x0C);

    // Power up analog, enable ADC + DAC
    codecWrite(0x0D, 0x01);
    codecWrite(0x0E, 0x02);
    codecWrite(0x12, 0x00);
    codecWrite(0x13, 0x10);

    // Microphone: enable analog MIC, max PGA gain
    codecWrite(0x14, 0x1A);

    // ADC gain and mic gain
    codecWrite(0x16, 0x12);  // ADC gain scale moderate
    codecWrite(0x17, 0xBF);  // ADC volume ~75%

    // EQ bypass
    codecWrite(0x1C, 0x6A);
    codecWrite(0x37, 0x08);

    // DAC volume max
    codecWrite(0x32, 0xFF);

    ESP_LOGI(TAG, "ES8311 codec initialized (addr=0x%02X)", cfg_.codec_i2c_addr);
}

// ---------------------------------------------------------------------------
// I2S — single peripheral, both TX and RX channels
// ---------------------------------------------------------------------------

void SSAudioES8311::initI2S() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(cfg_.i2s_port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t tx = nullptr;
    i2s_chan_handle_t rx = nullptr;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, &rx));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg_.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            static_cast<i2s_data_bit_width_t>(cfg_.bits),
            I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = static_cast<gpio_num_t>(cfg_.pin_mck),
            .bclk = static_cast<gpio_num_t>(cfg_.pin_bck),
            .ws   = static_cast<gpio_num_t>(cfg_.pin_ws),
            .dout = static_cast<gpio_num_t>(cfg_.pin_dout),
            .din  = static_cast<gpio_num_t>(cfg_.pin_din),
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &std_cfg));

    // Codec needs clocks running — enable both channels immediately
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_ERROR_CHECK(i2s_channel_enable(rx));

    tx_chan_ = tx;
    rx_chan_ = rx;

    ESP_LOGI(TAG, "I2S initialized (%lu Hz, %u-bit stereo, port %d)",
             (unsigned long)cfg_.sample_rate, cfg_.bits, cfg_.i2s_port);
}

// ---------------------------------------------------------------------------
// Amplifier control
// ---------------------------------------------------------------------------

void SSAudioES8311::ampOn() {
    if (cfg_.amp_enable_gpio < 0) return;
    gpio_set_level(static_cast<gpio_num_t>(cfg_.amp_enable_gpio),
                   cfg_.amp_active_low ? 0 : 1);
}

void SSAudioES8311::ampOff() {
    if (cfg_.amp_enable_gpio < 0) return;
    gpio_set_level(static_cast<gpio_num_t>(cfg_.amp_enable_gpio),
                   cfg_.amp_active_low ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

esp_err_t SSAudioES8311::init() {
    // Configure amplifier enable pin
    if (cfg_.amp_enable_gpio >= 0) {
        gpio_config_t io_cfg = {
            .pin_bit_mask = 1ULL << cfg_.amp_enable_gpio,
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_cfg);
        ampOff();
    }

    // I2S first (generates clocks), then codec
    initI2S();
    vTaskDelay(pdMS_TO_TICKS(10));
    initCodec();

    inited_ = true;
    ESP_LOGI(TAG, "ES8311 audio ready (mic + speaker)");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Capture (microphone)
// ---------------------------------------------------------------------------

esp_err_t SSAudioES8311::startCapture() {
    if (!inited_) return ESP_ERR_INVALID_STATE;
    // RX channel is already enabled (codec needs clocks)
    capturing_ = true;
    ESP_LOGI(TAG, "Mic capture started");
    return ESP_OK;
}

esp_err_t SSAudioES8311::stopCapture() {
    capturing_ = false;
    return ESP_OK;
}

size_t SSAudioES8311::readCapture(int16_t* buf, size_t samples) {
    if (!capturing_ || !rx_chan_) return 0;

    auto h = static_cast<i2s_chan_handle_t>(rx_chan_);
    size_t bytes_read = 0;

    // ES8311 uses stereo framing — read stereo then extract one channel
    size_t stereo_samples = samples * 2;
    size_t stereo_bytes = stereo_samples * sizeof(int16_t);
    auto* stereo_buf = static_cast<int16_t*>(malloc(stereo_bytes));
    if (!stereo_buf) return 0;

    esp_err_t ret = i2s_channel_read(h, stereo_buf, stereo_bytes, &bytes_read, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        free(stereo_buf);
        return 0;
    }

    // Extract left channel (mic data in left slot)
    size_t stereo_read = bytes_read / sizeof(int16_t);
    size_t mono_count = 0;
    for (size_t i = 0; i < stereo_read && mono_count < samples; i += 2) {
        buf[mono_count++] = stereo_buf[i];
    }

    free(stereo_buf);
    return mono_count;
}

bool SSAudioES8311::isCapturing() const {
    return capturing_;
}

// ---------------------------------------------------------------------------
// Playback (speaker)
// ---------------------------------------------------------------------------

esp_err_t SSAudioES8311::playBuffer(const int16_t* buf, size_t samples) {
    if (!inited_) return ESP_ERR_INVALID_STATE;

    auto h = static_cast<i2s_chan_handle_t>(tx_chan_);

    if (!playing_) {
        ampOn();
        playing_ = true;
    }

    // Convert mono to stereo for ES8311 (duplicate L→R)
    size_t stereo_samples = samples * 2;
    size_t stereo_bytes = stereo_samples * sizeof(int16_t);
    auto* stereo_buf = static_cast<int16_t*>(malloc(stereo_bytes));
    if (!stereo_buf) return ESP_ERR_NO_MEM;

    for (size_t i = 0; i < samples; i++) {
        int16_t val = (volume_ < 0.99f)
            ? static_cast<int16_t>(buf[i] * volume_)
            : buf[i];
        stereo_buf[i * 2]     = val;  // Left
        stereo_buf[i * 2 + 1] = val;  // Right
    }

    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(h, stereo_buf, stereo_bytes, &bytes_written, pdMS_TO_TICKS(1000));

    free(stereo_buf);
    return ret;
}

esp_err_t SSAudioES8311::stopPlayback() {
    if (!playing_) return ESP_OK;
    ampOff();
    playing_ = false;
    ESP_LOGI(TAG, "Speaker playback stopped");
    return ESP_OK;
}

bool SSAudioES8311::isPlaying() const {
    return playing_;
}

// ---------------------------------------------------------------------------
// Volume
// ---------------------------------------------------------------------------

esp_err_t SSAudioES8311::setVolume(float level) {
    volume_ = std::clamp(level, 0.0f, 1.0f);

    // Also set codec DAC volume register (0x32, range 0x00–0xFF)
    if (inited_) {
        uint8_t dac_vol = static_cast<uint8_t>(volume_ * 255.0f);
        codecWrite(0x32, dac_vol);
    }

    return ESP_OK;
}

bool SSAudioES8311::hasMic() const { return inited_; }
bool SSAudioES8311::hasSpeaker() const { return inited_; }
