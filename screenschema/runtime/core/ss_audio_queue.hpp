#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include "esp_err.h"

// Audio playback queue — plays sounds and PCM buffers through the HAL audio driver.
// Runs on a dedicated FreeRTOS task. Thread-safe: can be called from any task.
class SSAudioQueue {
public:
    static SSAudioQueue& instance();

    // Start the playback task. Call after ss_hal_init().
    esp_err_t start();
    void stop();

    // Queue a PCM buffer for playback (16-bit mono, copied internally).
    // sample_rate is informational — resampling is not performed; caller must
    // provide data at the HAL's configured rate.
    void playPCM(const int16_t* data, size_t samples);

    // Queue a WAV asset embedded in flash (must be 16-bit PCM, mono or stereo).
    // name is looked up in a registered asset table.
    void playAsset(const std::string& name);

    // Register a flash-embedded WAV asset.
    void registerAsset(const std::string& name, const uint8_t* data, size_t len);

    // Stop current playback and clear the queue.
    void clear();

    // Volume (delegates to HAL audio driver)
    void setVolume(float level);

    bool isPlaying() const;

private:
    SSAudioQueue() = default;

    struct Clip {
        int16_t* samples;  // heap-allocated, owned by queue
        size_t   count;
    };

    static void playback_task(void* param);
    void taskLoop();
    Clip parseWav(const uint8_t* data, size_t len);

    void*  task_handle_  = nullptr;
    void*  queue_handle_ = nullptr;  // FreeRTOS queue of Clip
    bool   running_      = false;
};
