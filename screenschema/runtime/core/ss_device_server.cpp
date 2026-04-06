#include "ss_device_server.hpp"
#include "ss_device.hpp"
#include "ss_context.hpp"
#include "ss_update_manager.hpp"
#include "ss_shell.hpp"
#include "ss_notification.hpp"
#include "ss_hal.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <cstring>

static const char* TAG = "SS_SRV";

// ---------------------------------------------------------------------------
// SSDeviceServer
// ---------------------------------------------------------------------------

SSDeviceServer& SSDeviceServer::instance() {
    static SSDeviceServer inst;
    return inst;
}

esp_err_t SSDeviceServer::start(uint16_t port) {
    // Initialize dispatch queue. Must be called from the LVGL task (app_main).
    pending_mutex_ = xSemaphoreCreateMutex();
    pump_timer_    = lv_timer_create(pump_timer_cb, 20, this);

    httpd_config_t cfg  = HTTPD_DEFAULT_CONFIG();
    cfg.server_port     = port;
    cfg.stack_size      = 8192;
    cfg.max_uri_handlers = 20;

    esp_err_t err = httpd_start(&server_, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t uris[] = {
        { "/api/status",           HTTP_GET,  handle_status,            nullptr },
        { "/api/widget/set",       HTTP_POST, handle_widget_set,        nullptr },
        { "/api/widget/set/batch", HTTP_POST, handle_widget_set_batch,  nullptr },
        { "/api/widget/get",       HTTP_GET,  handle_widget_get,        nullptr },
        { "/api/widget/show",      HTTP_POST, handle_widget_show,       nullptr },
        { "/api/widget/hide",      HTTP_POST, handle_widget_hide,       nullptr },
        { "/api/ota/update",       HTTP_POST, handle_ota_update,        nullptr },
        { "/api/apps/launch",      HTTP_POST, handle_app_launch,        nullptr },
        { "/api/notify",           HTTP_POST, handle_notify,            nullptr },
        { "/api/display/backlight",HTTP_POST, handle_display_backlight, nullptr },
        { "/api/display/sleep",    HTTP_POST, handle_display_sleep,     nullptr },
        { "/api/display/wake",     HTTP_POST, handle_display_wake,      nullptr },
    };
    for (auto& uri : uris) {
        httpd_register_uri_handler(server_, &uri);
    }

    ESP_LOGI(TAG, "Device server started on port %u", (unsigned)port);
    return ESP_OK;
}

void SSDeviceServer::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Thread-safe dispatch to LVGL task
// ---------------------------------------------------------------------------

void SSDeviceServer::post_fn(std::function<void()> fn) {
    if (!pending_mutex_) return;
    if (xSemaphoreTake(pending_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending_queue_.push_back(std::move(fn));
        xSemaphoreGive(pending_mutex_);
    } else {
        ESP_LOGW(TAG, "post_fn: mutex timeout");
    }
}

void SSDeviceServer::post_and_wait(std::function<void()> fn) {
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    post_fn([fn = std::move(fn), done]() mutable {
        fn();
        xSemaphoreGive(done);
    });
    // Block until the LVGL task processes the lambda (timeout: 2 s)
    xSemaphoreTake(done, pdMS_TO_TICKS(2000));
    vSemaphoreDelete(done);
}

void SSDeviceServer::pump_timer_cb(lv_timer_t* t) {
    static_cast<SSDeviceServer*>(t->user_data)->pump_pending();
}

void SSDeviceServer::pump_pending() {
    std::vector<std::function<void()>> to_run;
    if (xSemaphoreTake(pending_mutex_, 0) == pdTRUE) {
        to_run = std::move(pending_queue_);
        pending_queue_.clear();
        xSemaphoreGive(pending_mutex_);
    }
    for (auto& fn : to_run) fn();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string SSDeviceServer::read_body(httpd_req_t* req) {
    int len = req->content_len;
    if (len <= 0 || len > 4096) return "";
    std::string body(len, '\0');
    int received = httpd_req_recv(req, &body[0], len);
    if (received <= 0) return "";
    body.resize(received);
    return body;
}

void SSDeviceServer::send_json(httpd_req_t* req, cJSON* obj, int status) {
    char* str = cJSON_PrintUnformatted(obj);
    httpd_resp_set_type(req, "application/json");
    if (status != 200) {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    httpd_resp_sendstr(req, str ? str : "{}");
    free(str);
}

// ---------------------------------------------------------------------------
// GET /api/status
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_status(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id",        SSDevice::id().c_str());
    cJSON_AddStringToObject(root, "location",         SSDevice::location().c_str());
    cJSON_AddStringToObject(root, "friendly_name",    SSDevice::friendlyName().c_str());
    cJSON_AddStringToObject(root, "firmware_version", SSUpdateManager::currentVersion().c_str());
    cJSON_AddNumberToObject(root, "uptime_s",  (double)(esp_timer_get_time() / 1000000LL));
    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());

    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(root, "wifi_rssi", ap.rssi);
        cJSON_AddStringToObject(root, "wifi_ssid", (char*)ap.ssid);
    } else {
        cJSON_AddNullToObject(root, "wifi_rssi");
        cJSON_AddStringToObject(root, "wifi_ssid", "");
    }

    send_json(req, root, 200);
    cJSON_Delete(root);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/widget/set  {"id": "...", "value": "..."}
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_widget_set(httpd_req_t* req) {
    std::string body = read_body(req);
    cJSON* root = cJSON_Parse(body.c_str());
    cJSON* id_j  = root ? cJSON_GetObjectItemCaseSensitive(root, "id")    : nullptr;
    cJSON* val_j = root ? cJSON_GetObjectItemCaseSensitive(root, "value") : nullptr;

    if (!cJSON_IsString(id_j) || !cJSON_IsString(val_j)) {
        cJSON_Delete(root);
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing id or value");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    std::string id    = id_j->valuestring;
    std::string value = val_j->valuestring;
    cJSON_Delete(root);

    SSDeviceServer::instance().post_and_wait([id, value]() {
        SSContext::instance().set(id, value);
    });

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/widget/set/batch  {"updates": [{"id":"...","value":"..."}, ...]}
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_widget_set_batch(httpd_req_t* req) {
    std::string body = read_body(req);
    cJSON* root = cJSON_Parse(body.c_str());
    cJSON* updates = root ? cJSON_GetObjectItemCaseSensitive(root, "updates") : nullptr;

    if (!cJSON_IsArray(updates)) {
        cJSON_Delete(root);
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing updates array");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    std::vector<std::pair<std::string, std::string>> items;
    cJSON* item;
    cJSON_ArrayForEach(item, updates) {
        cJSON* id_j  = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON* val_j = cJSON_GetObjectItemCaseSensitive(item, "value");
        if (cJSON_IsString(id_j) && cJSON_IsString(val_j)) {
            items.emplace_back(id_j->valuestring, val_j->valuestring);
        }
    }
    cJSON_Delete(root);

    SSDeviceServer::instance().post_and_wait([items]() {
        for (auto& [id, val] : items) {
            SSContext::instance().set(id, val);
        }
    });

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "count", (double)items.size());
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /api/widget/get?id=foo
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_widget_get(httpd_req_t* req) {
    char query[256] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing query string");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    char id_val[128] = {};
    if (httpd_query_key_value(query, "id", id_val, sizeof(id_val)) != ESP_OK) {
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing id param");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    std::string id    = id_val;
    std::string value;
    SSDeviceServer::instance().post_and_wait([&id, &value]() {
        value = SSContext::instance().get<std::string>(id);
    });

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "id", id.c_str());
    cJSON_AddStringToObject(resp, "value", value.c_str());
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/widget/show  {"id": "..."}
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_widget_show(httpd_req_t* req) {
    std::string body = read_body(req);
    cJSON* root = cJSON_Parse(body.c_str());
    cJSON* id_j = root ? cJSON_GetObjectItemCaseSensitive(root, "id") : nullptr;

    if (!cJSON_IsString(id_j)) {
        cJSON_Delete(root);
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing id");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    std::string id = id_j->valuestring;
    cJSON_Delete(root);

    SSDeviceServer::instance().post_and_wait([id]() {
        SSContext::instance().show(id);
    });

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/widget/hide  {"id": "..."}
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_widget_hide(httpd_req_t* req) {
    std::string body = read_body(req);
    cJSON* root = cJSON_Parse(body.c_str());
    cJSON* id_j = root ? cJSON_GetObjectItemCaseSensitive(root, "id") : nullptr;

    if (!cJSON_IsString(id_j)) {
        cJSON_Delete(root);
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing id");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    std::string id = id_j->valuestring;
    cJSON_Delete(root);

    SSDeviceServer::instance().post_and_wait([id]() {
        SSContext::instance().hide(id);
    });

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/ota/update  {"url": "..."}   R6.1
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_ota_update(httpd_req_t* req) {
    std::string body = read_body(req);
    cJSON* root  = cJSON_Parse(body.c_str());
    cJSON* url_j = root ? cJSON_GetObjectItemCaseSensitive(root, "url") : nullptr;

    if (!cJSON_IsString(url_j)) {
        cJSON_Delete(root);
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing url");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    std::string url = url_j->valuestring;
    cJSON_Delete(root);

    if (SSUpdateManager::instance().isUpdating()) {
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "OTA already in progress");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    SSUpdateManager::instance().startUpdate(url);

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "message", "OTA started");
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/apps/launch  {"app_id": "..."}   R3.3
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_app_launch(httpd_req_t* req) {
    std::string body = read_body(req);
    cJSON* root   = cJSON_Parse(body.c_str());
    cJSON* id_j   = root ? cJSON_GetObjectItemCaseSensitive(root, "app_id") : nullptr;

    if (!cJSON_IsString(id_j)) {
        cJSON_Delete(root);
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing app_id");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    std::string app_id = id_j->valuestring;
    cJSON_Delete(root);

    if (!SSShell::instance().hasApp(app_id)) {
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "app not found");
        send_json(req, err, 404);
        cJSON_Delete(err);
        return ESP_OK;
    }

    bool launched = false;
    SSDeviceServer::instance().post_and_wait([&app_id, &launched]() {
        launched = SSShell::instance().launchApp(app_id);
    });

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", launched);
    cJSON_AddStringToObject(resp, "app_id", app_id.c_str());
    send_json(req, resp, launched ? 200 : 500);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/notify  {"title":"...","message":"...","duration_ms":4000,"type":"info"}   R3.4
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_notify(httpd_req_t* req) {
    std::string body = read_body(req);
    cJSON* root = cJSON_Parse(body.c_str());
    cJSON* msg_j = root ? cJSON_GetObjectItemCaseSensitive(root, "message") : nullptr;

    if (!cJSON_IsString(msg_j)) {
        cJSON_Delete(root);
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing message");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    std::string message = msg_j->valuestring;

    cJSON* title_j    = cJSON_GetObjectItemCaseSensitive(root, "title");
    cJSON* dur_j      = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
    cJSON* type_j     = cJSON_GetObjectItemCaseSensitive(root, "type");

    std::string title    = cJSON_IsString(title_j) ? title_j->valuestring : "";
    uint32_t    duration = cJSON_IsNumber(dur_j)   ? (uint32_t)dur_j->valuedouble : 4000;
    std::string type_str = cJSON_IsString(type_j)  ? type_j->valuestring : "info";
    cJSON_Delete(root);

    SSNotificationType type = SSNotificationType::INFO;
    if (type_str == "warning") type = SSNotificationType::WARNING;
    else if (type_str == "error") type = SSNotificationType::ERROR_TYPE;

    // SSNotification::show() handles its own LVGL dispatch — no post_and_wait needed.
    SSNotification::instance().show(title, message, duration, type);

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/display/backlight  {"level": 0.8}   R3.5
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_display_backlight(httpd_req_t* req) {
    std::string body = read_body(req);
    cJSON* root    = cJSON_Parse(body.c_str());
    cJSON* level_j = root ? cJSON_GetObjectItemCaseSensitive(root, "level") : nullptr;

    if (!cJSON_IsNumber(level_j)) {
        cJSON_Delete(root);
        cJSON* err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "ok", false);
        cJSON_AddStringToObject(err, "error", "missing level (0.0–1.0)");
        send_json(req, err, 400);
        cJSON_Delete(err);
        return ESP_OK;
    }

    float level = (float)level_j->valuedouble;
    cJSON_Delete(root);

    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;

    ISSDisplay* disp = ss_hal_display();
    if (disp) {
        disp->set_backlight(level);
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "level", level);
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/display/sleep   R3.5
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_display_sleep(httpd_req_t* req) {
    ISSDisplay* disp = ss_hal_display();
    if (disp) {
        disp->set_backlight(0.0f);
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "message", "display sleeping");
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/display/wake   R3.5
// ---------------------------------------------------------------------------

esp_err_t SSDeviceServer::handle_display_wake(httpd_req_t* req) {
    ISSDisplay* disp = ss_hal_display();
    if (disp) {
        disp->set_backlight(1.0f);
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "message", "display awake");
    send_json(req, resp, 200);
    cJSON_Delete(resp);
    return ESP_OK;
}
