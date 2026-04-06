#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "cJSON.h"

struct SSEndpointConfig {
    std::string base_url;
    int timeout_ms = 5000;
    int retry = 1;
    std::unordered_map<std::string, std::string> headers;
};

class SSHttpClient {
public:
    static SSHttpClient& instance();

    // Register a named endpoint. Call from app_main before the LVGL loop starts.
    void registerEndpoint(const std::string& name, SSEndpointConfig cfg);

    // Set a bearer token added to all requests (from gateway registration).
    void setAuthToken(const std::string& token);

    // Async POST/GET. Callback fires on LVGL task (safe to update UI directly).
    // resp is owned by the caller; call cJSON_Delete when done.
    void post(const std::string& endpoint, const std::string& path,
              cJSON* body, std::function<void(bool ok, cJSON* resp)> cb);
    void get(const std::string& endpoint, const std::string& path,
             std::function<void(bool ok, cJSON* resp)> cb);

    // Server-Sent Events streaming POST (D8).  Opens a long-lived HTTP request,
    // parses event:/data: lines, and dispatches each event to on_event on the
    // LVGL task as it arrives.  on_done fires once when the stream ends.
    // Use for streaming chat / ReAct-style event sources.
    using SSEEventCallback = std::function<void(const std::string& event,
                                                 const std::string& data)>;
    using SSEDoneCallback  = std::function<void(bool ok)>;
    void postSSE(const std::string& endpoint, const std::string& path,
                 cJSON* body, SSEEventCallback on_event, SSEDoneCallback on_done);

private:
    SSHttpClient() = default;

    struct Request {
        std::string endpoint;
        std::string path;
        std::string method;  // "POST" | "GET"
        std::string body_str;
        std::function<void(bool, cJSON*)> cb;
    };

    static void http_task(void* arg);
    void run_request(const Request& req);

    struct SSERequest {
        std::string endpoint;
        std::string path;
        std::string body_str;
        SSEEventCallback on_event;
        SSEDoneCallback  on_done;
    };
    static void sse_task(void* arg);
    void run_sse(const SSERequest& req);

    // Post fn to be called on the LVGL task.
    void post_fn(std::function<void()> fn);
    static void pump_timer_cb(lv_timer_t* t);
    void pump_pending();
    void ensure_timer();

    std::unordered_map<std::string, SSEndpointConfig> endpoints_;
    std::string auth_token_;

    SemaphoreHandle_t pending_mutex_ = nullptr;
    std::vector<std::function<void()>> pending_queue_;
    lv_timer_t* pump_timer_ = nullptr;
};
