#pragma once
#include "ss_audio.hpp"
#include "driver/i2s_types.h"

struct SSAudioMicConfig {
    enum class Mode { NONE, I2S_STD, I2S_PDM };
    Mode     mode         = Mode::NONE;
    uint32_t sample_rate  = 16000;
    uint8_t  bits         = 16;
    int      pin_clk      = -1;     // BCLK (std) or CLK (pdm)
    int      pin_data     = -1;     // DIN
    int      pin_ws       = -1;     // WS/LRCLK (std only, -1 for pdm)
    int      gain_db      = 0;      // Software gain for quiet MEMS mics
    i2s_port_t i2s_port   = I2S_NUM_0;
};

struct SSAudioSpeakerConfig {
    enum class Mode { NONE, I2S_STD };
    Mode     mode           = Mode::NONE;
    uint32_t sample_rate    = 16000;
    uint8_t  bits           = 16;
    int      pin_bclk       = -1;
    int      pin_lrclk      = -1;
    int      pin_data       = -1;
    int      amp_enable_gpio = -1;  // Amplifier enable pin (PAM8403, MAX98357)
    i2s_port_t i2s_port     = I2S_NUM_1;
};

struct SSAudioI2SConfig {
    SSAudioMicConfig     mic;
    SSAudioSpeakerConfig spk;
};

class SSAudioI2S : public ISSAudio {
public:
    explicit SSAudioI2S(const SSAudioI2SConfig& cfg);
    ~SSAudioI2S() override;

    esp_err_t init() override;

    // Mic
    esp_err_t startCapture() override;
    esp_err_t stopCapture() override;
    size_t readCapture(int16_t* buf, size_t samples) override;
    bool isCapturing() const override;

    // Speaker
    esp_err_t playBuffer(const int16_t* buf, size_t samples) override;
    esp_err_t stopPlayback() override;
    bool isPlaying() const override;

    esp_err_t setVolume(float level) override;

    bool hasMic() const override;
    bool hasSpeaker() const override;

private:
    esp_err_t initMicStd();
    esp_err_t initMicPdm();
    esp_err_t initSpeakerStd();
    void applyGain(int16_t* buf, size_t samples) const;

    SSAudioI2SConfig cfg_;
    void* mic_handle_    = nullptr;  // i2s_chan_handle_t
    void* spk_handle_    = nullptr;  // i2s_chan_handle_t
    float volume_        = 1.0f;
    bool  capturing_     = false;
    bool  playing_       = false;
    bool  mic_inited_    = false;
    bool  spk_inited_    = false;
};
