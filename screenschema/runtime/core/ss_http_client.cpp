#include "ss_http_client.hpp"
#include "esp_log.h"
#include "esp_http_client.h"
#include <cstring>

static const char* TAG = "SS_HTTP";

// ---------------------------------------------------------------------------
// SSHttpClient
// ---------------------------------------------------------------------------

SSHttpClient& SSHttpClient::instance() {
    static SSHttpClient inst;
    return inst;
}

void SSHttpClient::setAuthToken(const std::string& token) {
    auth_token_ = token;
    ESP_LOGI(TAG, "Auth token set (len=%d)", (int)token.size());
}

void SSHttpClient::registerEndpoint(const std::string& name, SSEndpointConfig cfg) {
    ensure_timer();  // safe: registerEndpoint is called from app_main (LVGL task)
    endpoints_[name] = std::move(cfg);
    ESP_LOGI(TAG, "Registered endpoint '%s' -> %s",
             name.c_str(), endpoints_[name].base_url.c_str());
}

void SSHttpClient::post(const std::string& endpoint, const std::string& path,
                         cJSON* body, std::function<void(bool, cJSON*)> cb) {
    std::string body_str;
    if (body) {
        char* s = cJSON_PrintUnformatted(body);
        if (s) { body_str = s; free(s); }
    }
    auto* req = new Request{ endpoint, path, "POST", std::move(body_str), std::move(cb) };
    xTaskCreate(http_task, "ss_http", 6144, req, 4, nullptr);
}

void SSHttpClient::get(const std::string& endpoint, const std::string& path,
                        std::function<void(bool, cJSON*)> cb) {
    auto* req = new Request{ endpoint, path, "GET", "", std::move(cb) };
    xTaskCreate(http_task, "ss_http", 6144, req, 4, nullptr);
}

void SSHttpClient::postSSE(const std::string& endpoint, const std::string& path,
                            cJSON* body, SSEEventCallback on_event, SSEDoneCallback on_done) {
    std::string body_str;
    if (body) {
        char* s = cJSON_PrintUnformatted(body);
        if (s) { body_str = s; free(s); }
    }
    auto* req = new SSERequest{ endpoint, path, std::move(body_str),
                                 std::move(on_event), std::move(on_done) };
    xTaskCreate(sse_task, "ss_sse", 8192, req, 4, nullptr);
}

void SSHttpClient::sse_task(void* arg) {
    auto* req = static_cast<SSERequest*>(arg);
    SSHttpClient::instance().run_sse(*req);
    delete req;
    vTaskDelete(nullptr);
}

void SSHttpClient::run_sse(const SSERequest& req) {
    auto it = endpoints_.find(req.endpoint);
    if (it == endpoints_.end()) {
        ESP_LOGE(TAG, "SSE: unknown endpoint '%s'", req.endpoint.c_str());
        auto done = req.on_done;
        post_fn([done]() { done(false); });
        return;
    }
    const SSEndpointConfig& cfg = it->second;
    std::string url = cfg.base_url + req.path;

    esp_http_client_config_t http_cfg = {};
    http_cfg.url               = url.c_str();
    http_cfg.timeout_ms        = 0;  // No idle timeout — long-lived stream
    http_cfg.keep_alive_enable = true;
    // No event_handler — we read manually below

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        auto done = req.on_done;
        post_fn([done]() { done(false); });
        return;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "text/event-stream");
    if (!req.body_str.empty()) {
        esp_http_client_set_post_field(client, req.body_str.c_str(),
                                        (int)req.body_str.size());
    }
    for (auto& [k, v] : cfg.headers) {
        esp_http_client_set_header(client, k.c_str(), v.c_str());
    }
    if (!auth_token_.empty()) {
        std::string bearer = "Bearer " + auth_token_;
        esp_http_client_set_header(client, "Authorization", bearer.c_str());
    }

    esp_err_t err = esp_http_client_open(client, (int)req.body_str.size());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SSE open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        auto done = req.on_done;
        post_fn([done]() { done(false); });
        return;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "SSE bad status: %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        auto done = req.on_done;
        post_fn([done]() { done(false); });
        return;
    }

    // Read loop — parse event:/data: lines as they arrive.
    // Buffer holds incomplete lines until we hit \n.
    std::string line_buf;
    std::string cur_event;
    std::string cur_data;
    char chunk[512];

    auto dispatch_event = [&](const std::string& ev, const std::string& data) {
        auto cb = req.on_event;
        std::string e = ev;
        std::string d = data;
        post_fn([cb, e = std::move(e), d = std::move(d)]() { cb(e, d); });
    };

    while (true) {
        int n = esp_http_client_read(client, chunk, sizeof(chunk));
        if (n <= 0) break;  // EOF or error
        line_buf.append(chunk, n);

        // Process all complete lines
        size_t pos;
        while ((pos = line_buf.find('\n')) != std::string::npos) {
            std::string line = line_buf.substr(0, pos);
            line_buf.erase(0, pos + 1);
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.empty()) {
                // Blank line = dispatch the accumulated event
                if (!cur_data.empty() || !cur_event.empty()) {
                    dispatch_event(cur_event, cur_data);
                    cur_event.clear();
                    cur_data.clear();
                }
                continue;
            }

            if (line.compare(0, 6, "event:") == 0) {
                cur_event = line.substr(6);
                if (!cur_event.empty() && cur_event.front() == ' ') cur_event.erase(0, 1);
            } else if (line.compare(0, 5, "data:") == 0) {
                std::string d = line.substr(5);
                if (!d.empty() && d.front() == ' ') d.erase(0, 1);
                if (!cur_data.empty()) cur_data += "\n";
                cur_data += d;
            }
            // Other SSE fields (id:, retry:, comments) are ignored
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    auto done = req.on_done;
    post_fn([done]() { done(true); });
}

// ---------------------------------------------------------------------------
// HTTP task — one-shot FreeRTOS task per request
// ---------------------------------------------------------------------------

void SSHttpClient::http_task(void* arg) {
    auto* req = static_cast<Request*>(arg);
    SSHttpClient::instance().run_request(*req);
    delete req;
    vTaskDelete(nullptr);
}

// Stateless event handler for esp_http_client; user_data points to response body string.
static esp_err_t ss_http_event_cb(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        auto* body = static_cast<std::string*>(evt->user_data);
        body->append(static_cast<const char*>(evt->data), evt->data_len);
    }
    return ESP_OK;
}

void SSHttpClient::run_request(const Request& req) {
    auto it = endpoints_.find(req.endpoint);
    if (it == endpoints_.end()) {
        ESP_LOGE(TAG, "Unknown endpoint: '%s'", req.endpoint.c_str());
        auto cb = req.cb;
        post_fn([cb]() { cb(false, nullptr); });
        return;
    }

    const SSEndpointConfig& cfg = it->second;
    std::string url = cfg.base_url + req.path;

    bool   ok     = false;
    cJSON* parsed = nullptr;

    for (int attempt = 0; attempt <= cfg.retry; ++attempt) {
        std::string resp_body;
        resp_body.reserve(512);

        esp_http_client_config_t http_cfg = {};
        http_cfg.url           = url.c_str();
        http_cfg.timeout_ms    = cfg.timeout_ms;
        http_cfg.keep_alive_enable = true;
        http_cfg.event_handler = ss_http_event_cb;
        http_cfg.user_data     = &resp_body;

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client) {
            ESP_LOGE(TAG, "esp_http_client_init failed");
            break;
        }

        if (req.method == "POST") {
            esp_http_client_set_method(client, HTTP_METHOD_POST);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            if (!req.body_str.empty()) {
                esp_http_client_set_post_field(client, req.body_str.c_str(),
                                               (int)req.body_str.size());
            }
        }

        for (auto& [k, v] : cfg.headers) {
            esp_http_client_set_header(client, k.c_str(), v.c_str());
        }

        if (!auth_token_.empty()) {
            std::string bearer = "Bearer " + auth_token_;
            esp_http_client_set_header(client, "Authorization", bearer.c_str());
        }

        esp_err_t err    = esp_http_client_perform(client);
        int       status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (err == ESP_OK && status >= 200 && status < 300) {
            ok = true;
            if (!resp_body.empty()) {
                parsed = cJSON_Parse(resp_body.c_str());
            }
            break;
        }
        ESP_LOGW(TAG, "%s %s failed (attempt %d/%d): %s status=%d",
                 req.method.c_str(), url.c_str(),
                 attempt + 1, cfg.retry + 1,
                 esp_err_to_name(err), status);
    }

    auto cb = req.cb;
    post_fn([cb, ok, parsed]() mutable { cb(ok, parsed); });
}

// ---------------------------------------------------------------------------
// Thread-safe callback dispatch to LVGL task
// ---------------------------------------------------------------------------

void SSHttpClient::ensure_timer() {
    if (pump_timer_) return;
    pending_mutex_ = xSemaphoreCreateMutex();
    pump_timer_    = lv_timer_create(pump_timer_cb, 50, this);
}

void SSHttpClient::post_fn(std::function<void()> fn) {
    if (!pending_mutex_) return;
    if (xSemaphoreTake(pending_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending_queue_.push_back(std::move(fn));
        xSemaphoreGive(pending_mutex_);
    } else {
        ESP_LOGW(TAG, "post_fn: mutex timeout, callback dropped");
    }
}

void SSHttpClient::pump_timer_cb(lv_timer_t* t) {
    static_cast<SSHttpClient*>(t->user_data)->pump_pending();
}

void SSHttpClient::pump_pending() {
    std::vector<std::function<void()>> to_run;
    if (xSemaphoreTake(pending_mutex_, 0) == pdTRUE) {
        to_run = std::move(pending_queue_);
        pending_queue_.clear();
        xSemaphoreGive(pending_mutex_);
    }
    for (auto& fn : to_run) fn();
}
