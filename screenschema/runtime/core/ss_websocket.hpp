#pragma once
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "cJSON.h"
#include "esp_websocket_client.h"

// SSWebSocket — persistent bidirectional WebSocket connection (R1.2)
//
// Named instance API for multiple concurrent connections:
//   auto& ws = SSWebSocket::create("voice_stream", "ws://server.local:8006/ws/voice");
//   ws.onMessage([](cJSON* msg) { ... });
//   ws.send(my_json);
//
// Retrieve by name:
//   auto* ws = SSWebSocket::get("voice_stream");
//   if (ws && ws->isConnected()) { ... }
//
// Singleton shortcut (for the default/primary connection):
//   SSWebSocket::instance().init("ws://...");
//
// The client sends a "hello" frame on every (re)connect.
// Incoming frames are dispatched to the LVGL task via a mutex+timer pump.

class SSWebSocket {
public:
    // Named instance management
    static SSWebSocket& create(const std::string& name, const std::string& url,
                               uint32_t reconnect_ms = 5000);
    static SSWebSocket* get(const std::string& name);
    static void destroyAll();

    // Singleton shortcut (unnamed default instance)
    static SSWebSocket& instance();

    // Connect to `url`. Reconnects automatically on disconnect.
    void init(const std::string& url, uint32_t reconnect_ms = 5000);
    void stop();

    // Register a message callback — fires on the LVGL task.
    // Multiple callbacks are supported; each receives a non-owning cJSON*.
    void onMessage(std::function<void(cJSON*)> cb);

    // Register a binary message callback — fires on the LVGL task.
    void onBinary(std::function<void(const uint8_t*, size_t)> cb);

    // Send a JSON object. Takes ownership and frees after send.
    // Safe to call from any task.
    void send(cJSON* obj);

    // Send a raw string. Safe to call from any task.
    void sendRaw(const std::string& text);

    // Send raw binary data. Safe to call from any task.
    void sendBinary(const uint8_t* data, size_t len);

    bool isConnected() const { return connected_; }

private:
    SSWebSocket() = default;

    // Named instance registry
    static std::unordered_map<std::string, SSWebSocket*>& registry();

    // esp_websocket_client event handler
    static void event_handler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);

    // Dispatch incoming data to the LVGL task
    void dispatch_message(const std::string& text);
    void dispatch_binary(const uint8_t* data, size_t len);

    // Handle built-in commands (widget_set, app_launch, etc.)
    void handle_command(cJSON* msg);

    // LVGL task dispatch (mutex + timer pump pattern)
    void post_fn(std::function<void()> fn);
    static void pump_timer_cb(lv_timer_t* t);
    void pump_pending();

    esp_websocket_client_handle_t client_ = nullptr;
    std::string url_;
    bool connected_ = false;

    std::vector<std::function<void(cJSON*)>> message_callbacks_;
    std::vector<std::function<void(const uint8_t*, size_t)>> binary_callbacks_;

    SemaphoreHandle_t pending_mutex_ = nullptr;
    std::vector<std::function<void()>> pending_queue_;
    lv_timer_t* pump_timer_ = nullptr;

    // Frame accumulation buffer for fragmented messages
    std::string frame_buf_;
};
