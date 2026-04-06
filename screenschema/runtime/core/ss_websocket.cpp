#include "ss_websocket.hpp"
#include "ss_device.hpp"
#include "ss_wifi_manager.hpp"
#include "ss_update_manager.hpp"
#include "ss_context.hpp"
#include "ss_shell.hpp"
#include "ss_notification.hpp"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "SS_WS";

SSWebSocket& SSWebSocket::instance() {
    static SSWebSocket inst;
    return inst;
}

std::unordered_map<std::string, SSWebSocket*>& SSWebSocket::registry() {
    static std::unordered_map<std::string, SSWebSocket*> reg;
    return reg;
}

SSWebSocket& SSWebSocket::create(const std::string& name, const std::string& url,
                                  uint32_t reconnect_ms) {
    auto& reg = registry();
    auto it = reg.find(name);
    if (it != reg.end()) {
        ESP_LOGW(TAG, "WebSocket '%s' already exists, returning existing", name.c_str());
        return *it->second;
    }
    auto* ws = new SSWebSocket();
    reg[name] = ws;
    ws->init(url, reconnect_ms);
    ESP_LOGI(TAG, "Created named WebSocket '%s' → %s", name.c_str(), url.c_str());
    return *ws;
}

SSWebSocket* SSWebSocket::get(const std::string& name) {
    auto& reg = registry();
    auto it = reg.find(name);
    return (it != reg.end()) ? it->second : nullptr;
}

void SSWebSocket::destroyAll() {
    auto& reg = registry();
    for (auto& [name, ws] : reg) {
        ws->stop();
        delete ws;
    }
    reg.clear();
}

// ---------------------------------------------------------------------------
// init / stop
// ---------------------------------------------------------------------------

void SSWebSocket::init(const std::string& url, uint32_t reconnect_ms) {
    if (client_) {
        ESP_LOGW(TAG, "Already initialized — call stop() first");
        return;
    }

    url_ = url;

    pending_mutex_ = xSemaphoreCreateMutex();
    pump_timer_    = lv_timer_create(pump_timer_cb, 20, this);

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri                  = url_.c_str();
    ws_cfg.reconnect_timeout_ms = reconnect_ms;
    ws_cfg.network_timeout_ms   = 10000;
    ws_cfg.buffer_size          = 4096;

    client_ = esp_websocket_client_init(&ws_cfg);
    if (!client_) {
        ESP_LOGE(TAG, "esp_websocket_client_init failed");
        return;
    }

    esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY, event_handler, this);
    esp_websocket_client_start(client_);
    ESP_LOGI(TAG, "Connecting to %s (reconnect=%ums)", url_.c_str(), (unsigned)reconnect_ms);
}

void SSWebSocket::stop() {
    if (!client_) return;
    esp_websocket_client_stop(client_);
    esp_websocket_client_destroy(client_);
    client_    = nullptr;
    connected_ = false;
    ESP_LOGI(TAG, "Stopped");
}

// ---------------------------------------------------------------------------
// Public send API
// ---------------------------------------------------------------------------

void SSWebSocket::send(cJSON* obj) {
    if (!obj) return;
    char* text = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!text) return;
    sendRaw(std::string(text));
    cJSON_free(text);
}

void SSWebSocket::sendRaw(const std::string& text) {
    if (!client_ || !connected_) {
        ESP_LOGW(TAG, "sendRaw: not connected, dropping message");
        return;
    }
    int ret = esp_websocket_client_send_text(client_, text.c_str(), text.size(), pdMS_TO_TICKS(3000));
    if (ret < 0) {
        ESP_LOGW(TAG, "sendRaw: send failed (%d)", ret);
    }
}

void SSWebSocket::sendBinary(const uint8_t* data, size_t len) {
    if (!client_ || !connected_) {
        ESP_LOGW(TAG, "sendBinary: not connected, dropping %u bytes", (unsigned)len);
        return;
    }
    int ret = esp_websocket_client_send_bin(client_, (const char*)data, len, pdMS_TO_TICKS(3000));
    if (ret < 0) {
        ESP_LOGW(TAG, "sendBinary: send failed (%d)", ret);
    }
}

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

void SSWebSocket::onMessage(std::function<void(cJSON*)> cb) {
    message_callbacks_.push_back(std::move(cb));
}

void SSWebSocket::onBinary(std::function<void(const uint8_t*, size_t)> cb) {
    binary_callbacks_.push_back(std::move(cb));
}

// ---------------------------------------------------------------------------
// esp_websocket_client event handler (runs on ws client task)
// ---------------------------------------------------------------------------

void SSWebSocket::event_handler(void* arg, esp_event_base_t /*base*/,
                                 int32_t event_id, void* event_data) {
    auto* self = static_cast<SSWebSocket*>(arg);
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED: {
            self->connected_ = true;
            ESP_LOGI(TAG, "Connected");

            // Send hello frame
            cJSON* hello = cJSON_CreateObject();
            cJSON_AddStringToObject(hello, "type",             "hello");
            cJSON_AddStringToObject(hello, "device_id",        SSDevice::id().c_str());
            cJSON_AddStringToObject(hello, "location",         SSDevice::location().c_str());
            cJSON_AddStringToObject(hello, "friendly_name",    SSDevice::friendlyName().c_str());
            cJSON_AddStringToObject(hello, "firmware_version", SSUpdateManager::currentVersion().c_str());
            cJSON_AddStringToObject(hello, "ip",               SSWifiManager::instance().ipAddress().c_str());
            self->send(hello);  // takes ownership
            break;
        }

        case WEBSOCKET_EVENT_DISCONNECTED:
            self->connected_ = false;
            ESP_LOGW(TAG, "Disconnected — will auto-reconnect");
            self->frame_buf_.clear();
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            self->connected_ = false;
            break;

        case WEBSOCKET_EVENT_DATA: {
            if (!data || data->data_len <= 0) break;

            // Accumulate fragments
            self->frame_buf_.append(data->data_ptr, data->data_len);

            // Only process when we have a complete message
            if ((data->payload_offset + data->data_len) >= data->payload_len) {
                std::string complete = std::move(self->frame_buf_);
                self->frame_buf_.clear();

                // Binary frames (opcode 2) go to binary callbacks
                if (data->op_code == 2) {
                    self->dispatch_binary(
                        reinterpret_cast<const uint8_t*>(complete.data()),
                        complete.size());
                } else {
                    self->dispatch_message(complete);
                }
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Message dispatch to LVGL task
// ---------------------------------------------------------------------------

void SSWebSocket::dispatch_binary(const uint8_t* data, size_t len) {
    // Copy data so it survives until LVGL task processes it
    std::vector<uint8_t> buf(data, data + len);
    post_fn([this, buf = std::move(buf)]() {
        for (auto& cb : binary_callbacks_) {
            cb(buf.data(), buf.size());
        }
    });
}

void SSWebSocket::dispatch_message(const std::string& text) {
    cJSON* msg = cJSON_Parse(text.c_str());
    if (!msg) {
        ESP_LOGW(TAG, "Received non-JSON message: %.80s", text.c_str());
        return;
    }

    // Post to LVGL task — capture msg by value, handle_command owns it
    post_fn([this, msg]() {
        handle_command(msg);
        // Notify user callbacks (non-owning view)
        for (auto& cb : message_callbacks_) {
            cb(msg);
        }
        cJSON_Delete(msg);
    });
}

// ---------------------------------------------------------------------------
// Built-in command handling (runs on LVGL task)
// ---------------------------------------------------------------------------

void SSWebSocket::handle_command(cJSON* msg) {
    cJSON* type_item = cJSON_GetObjectItemCaseSensitive(msg, "type");
    if (!type_item || !cJSON_IsString(type_item)) return;
    const char* type = type_item->valuestring;

    // --- ping → pong ---
    if (strcmp(type, "ping") == 0) {
        cJSON* pong = cJSON_CreateObject();
        cJSON_AddStringToObject(pong, "type",      "pong");
        cJSON_AddStringToObject(pong, "device_id", SSDevice::id().c_str());
        send(pong);
        return;
    }

    // --- widget_set ---
    if (strcmp(type, "widget_set") == 0) {
        cJSON* id_item    = cJSON_GetObjectItemCaseSensitive(msg, "id");
        cJSON* value_item = cJSON_GetObjectItemCaseSensitive(msg, "value");
        if (id_item && cJSON_IsString(id_item) && value_item && cJSON_IsString(value_item)) {
            SSContext::instance().set(id_item->valuestring, value_item->valuestring);
        }
        return;
    }

    // --- widget_set_batch ---
    if (strcmp(type, "widget_set_batch") == 0) {
        cJSON* updates = cJSON_GetObjectItemCaseSensitive(msg, "updates");
        if (cJSON_IsArray(updates)) {
            cJSON* item = nullptr;
            cJSON_ArrayForEach(item, updates) {
                cJSON* id_item    = cJSON_GetObjectItemCaseSensitive(item, "id");
                cJSON* value_item = cJSON_GetObjectItemCaseSensitive(item, "value");
                if (id_item && cJSON_IsString(id_item) && value_item && cJSON_IsString(value_item)) {
                    SSContext::instance().set(id_item->valuestring, value_item->valuestring);
                }
            }
        }
        return;
    }

    // --- widget_show ---
    if (strcmp(type, "widget_show") == 0) {
        cJSON* id_item = cJSON_GetObjectItemCaseSensitive(msg, "id");
        if (id_item && cJSON_IsString(id_item)) {
            SSContext::instance().show(id_item->valuestring);
        }
        return;
    }

    // --- widget_hide ---
    if (strcmp(type, "widget_hide") == 0) {
        cJSON* id_item = cJSON_GetObjectItemCaseSensitive(msg, "id");
        if (id_item && cJSON_IsString(id_item)) {
            SSContext::instance().hide(id_item->valuestring);
        }
        return;
    }

    // --- app_launch ---
    if (strcmp(type, "app_launch") == 0) {
        cJSON* id_item = cJSON_GetObjectItemCaseSensitive(msg, "id");
        if (id_item && cJSON_IsString(id_item)) {
            if (!SSShell::instance().launchApp(id_item->valuestring)) {
                ESP_LOGW(TAG, "app_launch: unknown app '%s'", id_item->valuestring);
            }
        }
        return;
    }

    // --- notify ---
    if (strcmp(type, "notify") == 0) {
        cJSON* title_item   = cJSON_GetObjectItemCaseSensitive(msg, "title");
        cJSON* message_item = cJSON_GetObjectItemCaseSensitive(msg, "message");
        cJSON* duration_item = cJSON_GetObjectItemCaseSensitive(msg, "duration_ms");

        std::string title   = (title_item && cJSON_IsString(title_item))     ? title_item->valuestring   : "";
        std::string message = (message_item && cJSON_IsString(message_item)) ? message_item->valuestring : "";
        uint32_t    dur     = (duration_item && cJSON_IsNumber(duration_item)) ? (uint32_t)duration_item->valuedouble : 4000;

        if (!message.empty()) {
            // SSNotification::show() posts to its own LVGL dispatch queue,
            // but we're already on the LVGL task here — call create_overlay directly
            // via the public show() which is safe to call from LVGL task too.
            SSNotification::instance().show(title, message, dur);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// Thread-safe dispatch to LVGL task
// ---------------------------------------------------------------------------

void SSWebSocket::post_fn(std::function<void()> fn) {
    if (!pending_mutex_) return;
    if (xSemaphoreTake(pending_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending_queue_.push_back(std::move(fn));
        xSemaphoreGive(pending_mutex_);
    } else {
        ESP_LOGW(TAG, "post_fn: mutex timeout");
    }
}

void SSWebSocket::pump_timer_cb(lv_timer_t* t) {
    static_cast<SSWebSocket*>(t->user_data)->pump_pending();
}

void SSWebSocket::pump_pending() {
    std::vector<std::function<void()>> to_run;
    if (xSemaphoreTake(pending_mutex_, 0) == pdTRUE) {
        to_run = std::move(pending_queue_);
        pending_queue_.clear();
        xSemaphoreGive(pending_mutex_);
    }
    for (auto& fn : to_run) fn();
}
