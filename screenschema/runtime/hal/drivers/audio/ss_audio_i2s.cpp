#include "ss_audio_i2s.hpp"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static const char* TAG = "SS_AUDIO_I2S";

SSAudioI2S::SSAudioI2S(const SSAudioI2SConfig& cfg) : cfg_(cfg) {}

SSAudioI2S::~SSAudioI2S() {
    if (mic_handle_) {
        auto h = static_cast<i2s_chan_handle_t>(mic_handle_);
        i2s_channel_disable(h);
        i2s_del_channel(h);
    }
    if (spk_handle_) {
        auto h = static_cast<i2s_chan_handle_t>(spk_handle_);
        i2s_channel_disable(h);
        i2s_del_channel(h);
    }
    // Release amplifier enable pin
    if (cfg_.spk.amp_enable_gpio >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(cfg_.spk.amp_enable_gpio), 0);
    }
}

esp_err_t SSAudioI2S::init() {
    esp_err_t ret = ESP_OK;

    // Init microphone
    if (cfg_.mic.mode == SSAudioMicConfig::Mode::I2S_STD) {
        ret = initMicStd();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Mic (I2S STD) init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        mic_inited_ = true;
        ESP_LOGI(TAG, "Mic (I2S STD) ready — %luHz %ubit port %d",
                 (unsigned long)cfg_.mic.sample_rate, cfg_.mic.bits, cfg_.mic.i2s_port);
    } else if (cfg_.mic.mode == SSAudioMicConfig::Mode::I2S_PDM) {
        ret = initMicPdm();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Mic (I2S PDM) init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        mic_inited_ = true;
        ESP_LOGI(TAG, "Mic (I2S PDM) ready — %luHz %ubit port %d",
                 (unsigned long)cfg_.mic.sample_rate, cfg_.mic.bits, cfg_.mic.i2s_port);
    }

    // Init speaker
    if (cfg_.spk.mode == SSAudioSpeakerConfig::Mode::I2S_STD) {
        ret = initSpeakerStd();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Speaker (I2S STD) init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        spk_inited_ = true;
        ESP_LOGI(TAG, "Speaker (I2S STD) ready — %luHz %ubit port %d",
                 (unsigned long)cfg_.spk.sample_rate, cfg_.spk.bits, cfg_.spk.i2s_port);
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Microphone — I2S Standard mode
// ---------------------------------------------------------------------------

esp_err_t SSAudioI2S::initMicStd() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(cfg_.mic.i2s_port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t rx_handle = nullptr;
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&chan_cfg, nullptr, &rx_handle),
        TAG, "i2s_new_channel (mic std)");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg_.mic.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            static_cast<i2s_data_bit_width_t>(cfg_.mic.bits),
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(cfg_.mic.pin_clk),
            .ws   = static_cast<gpio_num_t>(cfg_.mic.pin_ws),
            .dout = I2S_GPIO_UNUSED,
            .din  = static_cast<gpio_num_t>(cfg_.mic.pin_data),
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(rx_handle, &std_cfg),
        TAG, "i2s_channel_init_std_mode (mic)");

    mic_handle_ = rx_handle;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Microphone — I2S PDM mode
// ---------------------------------------------------------------------------

esp_err_t SSAudioI2S::initMicPdm() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(cfg_.mic.i2s_port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t rx_handle = nullptr;
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&chan_cfg, nullptr, &rx_handle),
        TAG, "i2s_new_channel (mic pdm)");

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(cfg_.mic.sample_rate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
            static_cast<i2s_data_bit_width_t>(cfg_.mic.bits),
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = static_cast<gpio_num_t>(cfg_.mic.pin_clk),
            .din = static_cast<gpio_num_t>(cfg_.mic.pin_data),
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_cfg),
        TAG, "i2s_channel_init_pdm_rx_mode");

    mic_handle_ = rx_handle;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Speaker — I2S Standard mode
// ---------------------------------------------------------------------------

esp_err_t SSAudioI2S::initSpeakerStd() {
    // Configure amplifier enable pin if present
    if (cfg_.spk.amp_enable_gpio >= 0) {
        gpio_config_t io_cfg = {
            .pin_bit_mask = 1ULL << cfg_.spk.amp_enable_gpio,
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_cfg);
        gpio_set_level(static_cast<gpio_num_t>(cfg_.spk.amp_enable_gpio), 0);
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(cfg_.spk.i2s_port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t tx_handle = nullptr;
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&chan_cfg, &tx_handle, nullptr),
        TAG, "i2s_new_channel (spk std)");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg_.spk.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            static_cast<i2s_data_bit_width_t>(cfg_.spk.bits),
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(cfg_.spk.pin_bclk),
            .ws   = static_cast<gpio_num_t>(cfg_.spk.pin_lrclk),
            .dout = static_cast<gpio_num_t>(cfg_.spk.pin_data),
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(tx_handle, &std_cfg),
        TAG, "i2s_channel_init_std_mode (spk)");

    spk_handle_ = tx_handle;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Capture (microphone)
// ---------------------------------------------------------------------------

esp_err_t SSAudioI2S::startCapture() {
    if (!mic_inited_) return ESP_ERR_INVALID_STATE;
    if (capturing_) return ESP_OK;

    auto h = static_cast<i2s_chan_handle_t>(mic_handle_);
    ESP_RETURN_ON_ERROR(i2s_channel_enable(h), TAG, "mic enable");
    capturing_ = true;
    ESP_LOGI(TAG, "Mic capture started");
    return ESP_OK;
}

esp_err_t SSAudioI2S::stopCapture() {
    if (!capturing_) return ESP_OK;

    auto h = static_cast<i2s_chan_handle_t>(mic_handle_);
    ESP_RETURN_ON_ERROR(i2s_channel_disable(h), TAG, "mic disable");
    capturing_ = false;
    ESP_LOGI(TAG, "Mic capture stopped");
    return ESP_OK;
}

size_t SSAudioI2S::readCapture(int16_t* buf, size_t samples) {
    if (!capturing_ || !mic_handle_) return 0;

    size_t bytes_to_read = samples * sizeof(int16_t);
    size_t bytes_read = 0;
    auto h = static_cast<i2s_chan_handle_t>(mic_handle_);

    esp_err_t ret = i2s_channel_read(h, buf, bytes_to_read, &bytes_read, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "i2s_channel_read: %s", esp_err_to_name(ret));
        return 0;
    }

    size_t samples_read = bytes_read / sizeof(int16_t);

    // Apply software gain if configured
    if (cfg_.mic.gain_db != 0 && samples_read > 0) {
        applyGain(buf, samples_read);
    }

    return samples_read;
}

bool SSAudioI2S::isCapturing() const {
    return capturing_;
}

// ---------------------------------------------------------------------------
// Playback (speaker)
// ---------------------------------------------------------------------------

esp_err_t SSAudioI2S::playBuffer(const int16_t* buf, size_t samples) {
    if (!spk_inited_) return ESP_ERR_INVALID_STATE;

    auto h = static_cast<i2s_chan_handle_t>(spk_handle_);

    // Enable channel + amplifier on first play
    if (!playing_) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(h), TAG, "spk enable");
        if (cfg_.spk.amp_enable_gpio >= 0) {
            gpio_set_level(static_cast<gpio_num_t>(cfg_.spk.amp_enable_gpio), 1);
        }
        playing_ = true;
    }

    // Apply volume scaling if not 1.0
    if (volume_ < 0.99f) {
        // Work on a temporary copy to avoid mutating caller's buffer
        size_t byte_len = samples * sizeof(int16_t);
        auto* scaled = static_cast<int16_t*>(malloc(byte_len));
        if (!scaled) return ESP_ERR_NO_MEM;

        for (size_t i = 0; i < samples; i++) {
            scaled[i] = static_cast<int16_t>(buf[i] * volume_);
        }

        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(h, scaled, byte_len, &bytes_written, pdMS_TO_TICKS(1000));
        free(scaled);
        return ret;
    }

    size_t bytes_written = 0;
    return i2s_channel_write(h, buf, samples * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(1000));
}

esp_err_t SSAudioI2S::stopPlayback() {
    if (!playing_) return ESP_OK;

    auto h = static_cast<i2s_chan_handle_t>(spk_handle_);

    // Disable amplifier first to avoid pop
    if (cfg_.spk.amp_enable_gpio >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(cfg_.spk.amp_enable_gpio), 0);
    }

    ESP_RETURN_ON_ERROR(i2s_channel_disable(h), TAG, "spk disable");
    playing_ = false;
    ESP_LOGI(TAG, "Speaker playback stopped");
    return ESP_OK;
}

bool SSAudioI2S::isPlaying() const {
    return playing_;
}

// ---------------------------------------------------------------------------
// Volume & utility
// ---------------------------------------------------------------------------

esp_err_t SSAudioI2S::setVolume(float level) {
    volume_ = std::clamp(level, 0.0f, 1.0f);
    return ESP_OK;
}

bool SSAudioI2S::hasMic() const {
    return mic_inited_;
}

bool SSAudioI2S::hasSpeaker() const {
    return spk_inited_;
}

void SSAudioI2S::applyGain(int16_t* buf, size_t samples) const {
    // Convert dB to linear multiplier: gain = 10^(dB/20)
    float multiplier = std::pow(10.0f, cfg_.mic.gain_db / 20.0f);
    for (size_t i = 0; i < samples; i++) {
        int32_t val = static_cast<int32_t>(buf[i] * multiplier);
        // Clamp to int16 range
        if (val > 32767) val = 32767;
        else if (val < -32768) val = -32768;
        buf[i] = static_cast<int16_t>(val);
    }
}
