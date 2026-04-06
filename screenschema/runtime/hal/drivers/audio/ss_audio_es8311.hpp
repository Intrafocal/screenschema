#pragma once
#include "ss_audio.hpp"
#include "driver/i2s_types.h"

struct SSAudioES8311Config {
    uint8_t  codec_i2c_addr = 0x18;
    i2s_port_t i2s_port     = I2S_NUM_0;
    uint32_t sample_rate    = 16000;
    uint8_t  bits           = 16;
    int      pin_mck        = -1;   // MCLK (generated via LEDC)
    int      pin_bck        = -1;   // Bit clock
    int      pin_ws         = -1;   // Word select / LRCLK
    int      pin_dout       = -1;   // Data out (speaker → codec DAC)
    int      pin_din        = -1;   // Data in (mic → codec ADC)
    int      amp_enable_gpio = -1;
    bool     amp_active_low  = true; // FM8002E is active LOW
};

class SSAudioES8311 : public ISSAudio {
public:
    explicit SSAudioES8311(const SSAudioES8311Config& cfg);
    ~SSAudioES8311() override;

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
    void codecWrite(uint8_t reg, uint8_t val);
    uint8_t codecRead(uint8_t reg);
    void initMclk();
    void initCodec();
    void initI2S();
    void ampOn();
    void ampOff();

    SSAudioES8311Config cfg_;
    void* tx_chan_   = nullptr;  // i2s_chan_handle_t
    void* rx_chan_   = nullptr;  // i2s_chan_handle_t
    float volume_    = 1.0f;
    bool  capturing_ = false;
    bool  playing_   = false;
    bool  inited_    = false;
};
