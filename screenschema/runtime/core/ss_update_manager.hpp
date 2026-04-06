#pragma once
#include <string>
#include <vector>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

class SSUpdateManager {
public:
    static SSUpdateManager& instance();

    // Returns the running firmware version string (from esp_app_get_description).
    static std::string currentVersion();

    // Check manifest URL for a newer version; if found, starts OTA automatically.
    // Posts complete(false, "Up to date (vX.Y.Z)") when already current.
    // Spawns a background FreeRTOS task; returns immediately.
    void checkForUpdates(const std::string& manifest_url);

    // Start OTA update from URL (http or https).
    // Spawns a background FreeRTOS task; returns immediately.
    // All callbacks fire on the LVGL task (safe to update UI directly).
    void startUpdate(const std::string& url);

    // Persistent callbacks — set before calling startUpdate.
    // progress: 0-100; -1 if content-length unknown.
    void onProgress(std::function<void(int percent)> cb);
    void onComplete(std::function<void(bool ok, const std::string& msg)> cb);

    bool isUpdating() const { return updating_; }

private:
    SSUpdateManager() = default;

    // Post fn to LVGL task via mutex-protected queue.
    void post(std::function<void()> fn);
    static void pump_timer_cb(lv_timer_t* t);
    void pump_pending();
    void ensure_timer();

    struct OtaArgs         { SSUpdateManager* self; std::string url; };
    struct ManifestArgs    { SSUpdateManager* self; std::string manifest_url; };
    static void ota_task(void* arg);
    static void manifest_task(void* arg);
    void run_ota(const std::string& url);
    void run_manifest_check(const std::string& manifest_url);

    bool        updating_      = false;

    std::function<void(int)>                            progress_cb_;
    std::function<void(bool, const std::string&)>       complete_cb_;

    SemaphoreHandle_t                  pending_mutex_ = nullptr;
    std::vector<std::function<void()>> pending_queue_;
    lv_timer_t*                        pump_timer_    = nullptr;
};
