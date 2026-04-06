#include "ss_shell.hpp"
#include "esp_log.h"

static const char* TAG = "SS_SHELL";

SSShell& SSShell::instance() {
    static SSShell inst;
    return inst;
}

void SSShell::init(ESP_Brookesia_Phone* phone) {
    if (phone_) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }
    phone_ = phone;
    ESP_LOGI(TAG, "Shell initialized");
}

void SSShell::registerApp(const std::string& schema_id, ESP_Brookesia_PhoneApp* app) {
    if (!app) {
        ESP_LOGE(TAG, "registerApp: null app for id='%s'", schema_id.c_str());
        return;
    }
    int brookesia_id = app->getId();
    if (brookesia_id < 0) {
        ESP_LOGE(TAG, "registerApp: app '%s' not yet installed (id=%d)", schema_id.c_str(), brookesia_id);
        return;
    }
    app_ids_[schema_id] = brookesia_id;
    ESP_LOGI(TAG, "Registered app '%s' → brookesia id %d", schema_id.c_str(), brookesia_id);
}

bool SSShell::launchApp(const std::string& schema_id) {
    auto it = app_ids_.find(schema_id);
    if (it == app_ids_.end()) {
        ESP_LOGW(TAG, "Unknown app: '%s'", schema_id.c_str());
        return false;
    }
    if (!phone_) {
        ESP_LOGE(TAG, "launchApp: phone not initialized");
        return false;
    }
    // TODO: esp-brookesia 0.4.x made startApp() private — need public API
    ESP_LOGW(TAG, "launchApp('%s') not yet supported (startApp is private in esp-brookesia)", schema_id.c_str());
    return false;
}

bool SSShell::hasApp(const std::string& schema_id) const {
    return app_ids_.count(schema_id) > 0;
}
