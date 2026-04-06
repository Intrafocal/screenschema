#include "ss_gateway.hpp"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "SS_GW";
static const char* NVS_NAMESPACE = "ss_gateway";
static const char* NVS_KEY_TOKEN = "jwt";

static constexpr int MAX_RETRIES = 10;
static constexpr int INITIAL_BACKOFF_MS = 1000;
static constexpr int MAX_BACKOFF_MS = 30000;

SSGateway& SSGateway::instance() {
    static SSGateway inst;
    return inst;
}

void SSGateway::init(const std::string& gateway_url,
                     const std::string& device_id,
                     const std::string& secret) {
    gateway_url_ = gateway_url;
    device_id_   = device_id;
    secret_      = secret;
    loadTokenFromNvs();
}

// ---------------------------------------------------------------------------
// Registration (blocking, with retry + exponential backoff)
// ---------------------------------------------------------------------------

esp_err_t SSGateway::registerDevice() {
    int backoff_ms = INITIAL_BACKOFF_MS;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        esp_err_t err = do_register();
        if (err == ESP_OK) {
            registered_ = true;
            saveTokenToNvs();
            ESP_LOGI(TAG, "Registered as '%s' (token len=%d)",
                     device_id_.c_str(), (int)token_.size());
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Registration attempt %d/%d failed, retrying in %d ms",
                 attempt + 1, MAX_RETRIES, backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        backoff_ms = (backoff_ms * 2 > MAX_BACKOFF_MS) ? MAX_BACKOFF_MS : backoff_ms * 2;
    }
    ESP_LOGE(TAG, "Registration failed after %d attempts", MAX_RETRIES);
    return ESP_FAIL;
}

esp_err_t SSGateway::refreshToken() {
    ESP_LOGI(TAG, "Refreshing token...");
    return do_register();
}

esp_err_t SSGateway::do_register() {
    std::string url = gateway_url_ + "/api/register";

    // Build request body
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "device_id", device_id_.c_str());
    cJSON_AddStringToObject(body, "secret", secret_.c_str());
    char* body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return ESP_ERR_NO_MEM;

    // Response buffer
    std::string resp_body;
    resp_body.reserve(512);

    auto event_cb = [](esp_http_client_event_t* evt) -> esp_err_t {
        if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
            auto* buf = static_cast<std::string*>(evt->user_data);
            buf->append(static_cast<const char*>(evt->data), evt->data_len);
        }
        return ESP_OK;
    };

    esp_http_client_config_t cfg = {};
    cfg.url            = url.c_str();
    cfg.method         = HTTP_METHOD_POST;
    cfg.timeout_ms     = 10000;
    cfg.event_handler  = event_cb;
    cfg.user_data      = &resp_body;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body_str);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_str, (int)strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body_str);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Registration rejected: HTTP %d", status);
        return ESP_FAIL;
    }

    // Parse response
    cJSON* resp = cJSON_Parse(resp_body.c_str());
    if (!resp) {
        ESP_LOGE(TAG, "Failed to parse registration response");
        return ESP_FAIL;
    }

    cJSON* token_item = cJSON_GetObjectItem(resp, "token");
    if (cJSON_IsString(token_item)) {
        token_ = token_item->valuestring;
    }

    cJSON* config = cJSON_GetObjectItem(resp, "config");
    if (config) {
        cJSON* ws = cJSON_GetObjectItem(config, "gateway_ws");
        if (cJSON_IsString(ws)) ws_url_ = ws->valuestring;

        cJSON* ota = cJSON_GetObjectItem(config, "ota_url");
        if (cJSON_IsString(ota)) ota_url_ = ota->valuestring;

        cJSON* hb = cJSON_GetObjectItem(config, "heartbeat_url");
        if (cJSON_IsString(hb)) heartbeat_url_ = hb->valuestring;

        cJSON* hb_int = cJSON_GetObjectItem(config, "heartbeat_interval_s");
        if (cJSON_IsNumber(hb_int)) heartbeat_interval_s_ = hb_int->valueint;
    }

    cJSON_Delete(resp);

    if (token_.empty()) {
        ESP_LOGE(TAG, "No token in registration response");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// NVS persistence — cache token across reboots
// ---------------------------------------------------------------------------

void SSGateway::loadTokenFromNvs() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;

    size_t len = 0;
    if (nvs_get_str(handle, NVS_KEY_TOKEN, nullptr, &len) == ESP_OK && len > 0) {
        std::string buf(len - 1, '\0');
        nvs_get_str(handle, NVS_KEY_TOKEN, buf.data(), &len);
        token_ = std::move(buf);
        ESP_LOGI(TAG, "Loaded cached token from NVS (len=%d)", (int)token_.size());
    }
    nvs_close(handle);
}

void SSGateway::saveTokenToNvs() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_str(handle, NVS_KEY_TOKEN, token_.c_str());
    nvs_commit(handle);
    nvs_close(handle);
}
