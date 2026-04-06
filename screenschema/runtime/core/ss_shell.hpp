#pragma once
#include <string>
#include <unordered_map>
#include "esp_brookesia.hpp"

class SSShell {
public:
    static SSShell& instance();

    // Store the phone pointer. Call from app_main after phone.begin().
    // phone must outlive SSShell (it lives in app_main's infinite loop).
    void init(ESP_Brookesia_Phone* phone);

    // Register a schema app id → brookesia integer id mapping.
    // app must already be installed (phone.installApp called) so getId() is valid.
    void registerApp(const std::string& schema_id, ESP_Brookesia_PhoneApp* app);

    // Launch a registered app by its schema id.
    // MUST be called from the LVGL task (use SSDeviceServer's post_and_wait).
    // Returns false if schema_id is not registered.
    bool launchApp(const std::string& schema_id);

    bool hasApp(const std::string& schema_id) const;

    ESP_Brookesia_Phone* phone() const { return phone_; }

private:
    SSShell() = default;

    ESP_Brookesia_Phone* phone_ = nullptr;
    std::unordered_map<std::string, int> app_ids_;  // schema_id → brookesia int id
};
