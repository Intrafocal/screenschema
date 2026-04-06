#include "ss_update_manager.hpp"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <cstring>

static const char* TAG = "SS_OTA";

SSUpdateManager& SSUpdateManager::instance() {
    static SSUpdateManager inst;
    return inst;
}

std::string SSUpdateManager::currentVersion() {
    const esp_app_desc_t* desc = esp_app_get_description();
    return desc ? std::string(desc->version) : "unknown";
}

void SSUpdateManager::onProgress(std::function<void(int)> cb) {
    progress_cb_ = std::move(cb);
}

void SSUpdateManager::onComplete(std::function<void(bool, const std::string&)> cb) {
    complete_cb_ = std::move(cb);
}

void SSUpdateManager::checkForUpdates(const std::string& manifest_url) {
    if (updating_) {
        ESP_LOGW(TAG, "OTA already in progress");
        return;
    }
    ensure_timer();
    updating_ = true;

    auto* args = new ManifestArgs{ this, manifest_url };
    xTaskCreate(manifest_task, "ss_manifest", 8192, args, 5, nullptr);
}

void SSUpdateManager::manifest_task(void* arg) {
    auto* a = static_cast<ManifestArgs*>(arg);
    a->self->run_manifest_check(a->manifest_url);
    delete a;
    vTaskDelete(nullptr);
}

void SSUpdateManager::run_manifest_check(const std::string& manifest_url) {
    ESP_LOGI(TAG, "Checking manifest: %s", manifest_url.c_str());

    // Fetch manifest JSON
    std::string body;
    body.reserve(512);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url         = manifest_url.c_str();
    http_cfg.timeout_ms  = 10000;
    http_cfg.user_data   = &body;
    http_cfg.event_handler = [](esp_http_client_event_t* evt) -> esp_err_t {
        if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
            auto* buf = static_cast<std::string*>(evt->user_data);
            buf->append(static_cast<const char*>(evt->data), evt->data_len);
        }
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        std::string msg = std::string("Manifest fetch failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", msg.c_str());
        updating_ = false;
        if (complete_cb_) {
            auto cb = complete_cb_;
            post([cb, msg]() { cb(false, msg); });
        }
        return;
    }

    // Parse JSON
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        updating_ = false;
        std::string msg = "Invalid manifest JSON";
        ESP_LOGE(TAG, "%s", msg.c_str());
        if (complete_cb_) {
            auto cb = complete_cb_;
            post([cb, msg]() { cb(false, msg); });
        }
        return;
    }

    const cJSON* ver_item = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON* url_item = cJSON_GetObjectItemCaseSensitive(root, "firmware_url");
    std::string manifest_version = (cJSON_IsString(ver_item) && ver_item->valuestring)
        ? ver_item->valuestring : "";
    std::string firmware_url = (cJSON_IsString(url_item) && url_item->valuestring)
        ? url_item->valuestring : "";
    cJSON_Delete(root);

    std::string current = currentVersion();
    ESP_LOGI(TAG, "Current: %s  Manifest: %s", current.c_str(), manifest_version.c_str());

    if (manifest_version.empty() || firmware_url.empty()) {
        updating_ = false;
        std::string msg = "Manifest missing version or firmware_url";
        if (complete_cb_) {
            auto cb = complete_cb_;
            post([cb, msg]() { cb(false, msg); });
        }
        return;
    }

    if (manifest_version == current) {
        updating_ = false;
        std::string msg = "Up to date (v" + current + ")";
        ESP_LOGI(TAG, "%s", msg.c_str());
        if (complete_cb_) {
            auto cb = complete_cb_;
            post([cb, msg]() { cb(false, msg); });
        }
        return;
    }

    // Different version found — start OTA (updating_ stays true)
    ESP_LOGI(TAG, "New version available: %s — starting OTA", manifest_version.c_str());
    if (progress_cb_) {
        auto cb = progress_cb_;
        post([cb]() { cb(-1); });  // signal "starting"
    }
    run_ota(firmware_url);
}

void SSUpdateManager::startUpdate(const std::string& url) {
    if (updating_) {
        ESP_LOGW(TAG, "OTA already in progress");
        return;
    }
    ensure_timer();
    updating_ = true;

    auto* args = new OtaArgs{ this, url };
    xTaskCreate(ota_task, "ss_ota", 8192, args, 5, nullptr);
}

// ---------------------------------------------------------------------------
// OTA task — runs on its own FreeRTOS task; never touches LVGL directly.
// ---------------------------------------------------------------------------

void SSUpdateManager::ota_task(void* arg) {
    auto* a = static_cast<OtaArgs*>(arg);
    a->self->run_ota(a->url);
    delete a;
    vTaskDelete(nullptr);
}

void SSUpdateManager::run_ota(const std::string& url) {
    ESP_LOGI(TAG, "OTA starting: %s", url.c_str());

    esp_http_client_config_t http_cfg = {};
    http_cfg.url                 = url.c_str();
    http_cfg.timeout_ms          = 30000;
    http_cfg.keep_alive_enable   = true;
    http_cfg.skip_cert_common_name_check = true;  // dev convenience; tighten for prod

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_https_ota_handle_t handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) {
        std::string msg = std::string("Begin failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", msg.c_str());
        updating_ = false;
        if (complete_cb_) {
            auto cb = complete_cb_;
            post([cb, msg]() { cb(false, msg); });
        }
        return;
    }

    int last_pct = -1;
    while (true) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        if (progress_cb_) {
            int read  = esp_https_ota_get_image_len_read(handle);
            int total = esp_https_ota_get_image_size(handle);
            int pct   = (total > 0) ? (read * 100 / total) : -1;
            if (pct != last_pct) {
                last_pct = pct;
                auto cb = progress_cb_;
                post([cb, pct]() { cb(pct); });
            }
        }
    }

    if (err == ESP_OK) {
        err = esp_https_ota_finish(handle);
    } else {
        esp_https_ota_abort(handle);
    }

    updating_ = false;

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA complete — rebooting");
        if (complete_cb_) {
            auto cb = complete_cb_;
            post([cb]() { cb(true, "Done — rebooting"); });
        }
        vTaskDelay(pdMS_TO_TICKS(1500));  // let UI update before reboot
        esp_restart();
    } else {
        std::string msg = std::string("OTA failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", msg.c_str());
        if (complete_cb_) {
            auto cb = complete_cb_;
            post([cb, msg]() { cb(false, msg); });
        }
    }
}

// ---------------------------------------------------------------------------
// Thread-safe callback dispatch (same pattern as SSWifiManager)
// ---------------------------------------------------------------------------

void SSUpdateManager::ensure_timer() {
    if (pump_timer_) return;
    pending_mutex_ = xSemaphoreCreateMutex();
    pump_timer_    = lv_timer_create(pump_timer_cb, 50, this);
}

void SSUpdateManager::post(std::function<void()> fn) {
    if (!pending_mutex_) return;
    if (xSemaphoreTake(pending_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending_queue_.push_back(std::move(fn));
        xSemaphoreGive(pending_mutex_);
    } else {
        ESP_LOGW(TAG, "post: mutex timeout, callback dropped");
    }
}

void SSUpdateManager::pump_timer_cb(lv_timer_t* t) {
    static_cast<SSUpdateManager*>(t->user_data)->pump_pending();
}

void SSUpdateManager::pump_pending() {
    std::vector<std::function<void()>> to_run;
    if (xSemaphoreTake(pending_mutex_, 0) == pdTRUE) {
        to_run = std::move(pending_queue_);
        pending_queue_.clear();
        xSemaphoreGive(pending_mutex_);
    }
    for (auto& fn : to_run) fn();
}
