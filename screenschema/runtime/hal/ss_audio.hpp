#pragma once
#include <cstdint>
#include <cstddef>
#include "esp_err.h"

class ISSAudio {
public:
    virtual ~ISSAudio() = default;

    virtual esp_err_t init() = 0;

    // Microphone capture
    virtual esp_err_t startCapture() = 0;
    virtual esp_err_t stopCapture() = 0;
    virtual size_t readCapture(int16_t* buf, size_t samples) = 0;
    virtual bool isCapturing() const = 0;

    // Speaker playback
    virtual esp_err_t playBuffer(const int16_t* buf, size_t samples) = 0;
    virtual esp_err_t stopPlayback() = 0;
    virtual bool isPlaying() const = 0;

    // Volume control (0.0–1.0)
    virtual esp_err_t setVolume(float level) = 0;

    // Query capabilities
    virtual bool hasMic() const = 0;
    virtual bool hasSpeaker() const = 0;
};
