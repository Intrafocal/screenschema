#include "ss_audio_queue.hpp"
#include "ss_hal.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <cstring>
#include <unordered_map>

static const char* TAG = "SS_AUDIO_Q";
static const size_t QUEUE_DEPTH = 8;
static const size_t PLAYBACK_CHUNK = 512;  // samples per write call

// Asset registry (flash-embedded WAV files)
static std::unordered_map<std::string, std::pair<const uint8_t*, size_t>> s_assets;

SSAudioQueue& SSAudioQueue::instance() {
    static SSAudioQueue inst;
    return inst;
}

esp_err_t SSAudioQueue::start() {
    if (running_) return ESP_OK;
    if (!ss_hal_audio() || !ss_hal_audio()->hasSpeaker()) {
        ESP_LOGW(TAG, "No speaker available — audio queue disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }

    queue_handle_ = xQueueCreate(QUEUE_DEPTH, sizeof(Clip));
    if (!queue_handle_) return ESP_ERR_NO_MEM;

    running_ = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        playback_task, "ss_audio_q", 4096, this, 5, (TaskHandle_t*)&task_handle_, 1);
    if (ret != pdPASS) {
        running_ = false;
        vQueueDelete((QueueHandle_t)queue_handle_);
        queue_handle_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Audio playback queue started");
    return ESP_OK;
}

void SSAudioQueue::stop() {
    if (!running_) return;
    running_ = false;
    // Unblock the task if waiting
    Clip sentinel = { nullptr, 0 };
    xQueueSend((QueueHandle_t)queue_handle_, &sentinel, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (task_handle_) {
        vTaskDelete((TaskHandle_t)task_handle_);
        task_handle_ = nullptr;
    }
    // Drain remaining clips
    Clip c;
    while (xQueueReceive((QueueHandle_t)queue_handle_, &c, 0) == pdTRUE) {
        free(c.samples);
    }
    vQueueDelete((QueueHandle_t)queue_handle_);
    queue_handle_ = nullptr;
}

void SSAudioQueue::playPCM(const int16_t* data, size_t samples) {
    if (!running_ || !queue_handle_ || samples == 0) return;

    // Copy data — caller's buffer may be temporary
    auto* buf = static_cast<int16_t*>(malloc(samples * sizeof(int16_t)));
    if (!buf) {
        ESP_LOGW(TAG, "playPCM: alloc failed (%u samples)", (unsigned)samples);
        return;
    }
    memcpy(buf, data, samples * sizeof(int16_t));

    Clip clip = { buf, samples };
    if (xQueueSend((QueueHandle_t)queue_handle_, &clip, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "playPCM: queue full, dropping clip");
        free(buf);
    }
}

void SSAudioQueue::playAsset(const std::string& name) {
    auto it = s_assets.find(name);
    if (it == s_assets.end()) {
        ESP_LOGW(TAG, "Asset not found: %s", name.c_str());
        return;
    }

    Clip clip = parseWav(it->second.first, it->second.second);
    if (clip.samples && clip.count > 0) {
        if (xQueueSend((QueueHandle_t)queue_handle_, &clip, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "playAsset: queue full");
            free(clip.samples);
        }
    }
}

void SSAudioQueue::registerAsset(const std::string& name, const uint8_t* data, size_t len) {
    s_assets[name] = { data, len };
    ESP_LOGI(TAG, "Registered asset '%s' (%u bytes)", name.c_str(), (unsigned)len);
}

void SSAudioQueue::clear() {
    if (!queue_handle_) return;
    Clip c;
    while (xQueueReceive((QueueHandle_t)queue_handle_, &c, 0) == pdTRUE) {
        free(c.samples);
    }
    // Stop any active playback
    ISSAudio* audio = ss_hal_audio();
    if (audio) audio->stopPlayback();
}

void SSAudioQueue::setVolume(float level) {
    ISSAudio* audio = ss_hal_audio();
    if (audio) audio->setVolume(level);
}

bool SSAudioQueue::isPlaying() const {
    ISSAudio* audio = ss_hal_audio();
    return audio && audio->isPlaying();
}

// ---------------------------------------------------------------------------
// Playback task
// ---------------------------------------------------------------------------

void SSAudioQueue::playback_task(void* param) {
    static_cast<SSAudioQueue*>(param)->taskLoop();
}

void SSAudioQueue::taskLoop() {
    ISSAudio* audio = ss_hal_audio();
    auto q = (QueueHandle_t)queue_handle_;

    while (running_) {
        Clip clip;
        if (xQueueReceive(q, &clip, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;
        }
        if (!clip.samples) continue;  // sentinel

        // Play clip in chunks
        size_t offset = 0;
        while (offset < clip.count && running_) {
            size_t remaining = clip.count - offset;
            size_t chunk = (remaining < PLAYBACK_CHUNK) ? remaining : PLAYBACK_CHUNK;
            audio->playBuffer(clip.samples + offset, chunk);
            offset += chunk;
        }
        audio->stopPlayback();

        free(clip.samples);
    }
}

// ---------------------------------------------------------------------------
// WAV parser — extract 16-bit PCM data from a WAV header
// ---------------------------------------------------------------------------

SSAudioQueue::Clip SSAudioQueue::parseWav(const uint8_t* data, size_t len) {
    // Minimal WAV validation: RIFF header + fmt chunk
    if (len < 44 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "Invalid WAV header");
        return { nullptr, 0 };
    }

    // Find "data" chunk
    size_t pos = 12;
    while (pos + 8 < len) {
        uint32_t chunk_size = data[pos + 4] | (data[pos + 5] << 8) |
                              (data[pos + 6] << 16) | (data[pos + 7] << 24);
        if (memcmp(data + pos, "data", 4) == 0) {
            const int16_t* pcm = reinterpret_cast<const int16_t*>(data + pos + 8);
            size_t pcm_bytes = chunk_size;
            if (pos + 8 + pcm_bytes > len) pcm_bytes = len - pos - 8;
            size_t samples = pcm_bytes / sizeof(int16_t);

            auto* buf = static_cast<int16_t*>(malloc(samples * sizeof(int16_t)));
            if (!buf) return { nullptr, 0 };
            memcpy(buf, pcm, samples * sizeof(int16_t));
            return { buf, samples };
        }
        pos += 8 + chunk_size;
        if (chunk_size & 1) pos++;  // WAV chunks are word-aligned
    }

    ESP_LOGW(TAG, "WAV: no data chunk found");
    return { nullptr, 0 };
}
