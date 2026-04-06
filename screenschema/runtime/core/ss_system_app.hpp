#pragma once
#include "ss_app_base.hpp"
#include "ss_wifi_manager.hpp"
#include "ss_update_manager.hpp"
#include <string>

// Built-in system settings app providing WiFi config and OTA firmware updates.
// Install via phone.installApp(&system_app) in main — or use the screenschema
// codegen with shell.system_app: { wifi: true, ota: true }.
class SSSystemApp : public SSAppBase {
public:
    explicit SSSystemApp(bool wifi = true, bool ota = true, const std::string& ota_manifest_url = "");

protected:
    void buildUI(lv_obj_t* container) override;
    void onClose() override;

private:
    bool wifi_enabled_;
    bool ota_enabled_;

    // Valid only while the app is open (nulled in onClose)
    lv_obj_t* container_    = nullptr;
    lv_obj_t* kb_           = nullptr;
    lv_obj_t* wifi_status_  = nullptr;
    lv_obj_t* wifi_ssid_dd_ = nullptr;
    lv_obj_t* wifi_password_= nullptr;
    lv_obj_t* ota_status_   = nullptr;

    bool        open_          = false;
    std::string selected_ssid_;

    // LVGL event callbacks (user_data = SSSystemApp*)
    static void on_scan_click   (lv_event_t* e);
    static void on_connect_click(lv_event_t* e);
    static void on_ssid_change  (lv_event_t* e);
    static void on_ota_click    (lv_event_t* e);
    static void on_ta_focus     (lv_event_t* e);
    static void on_ta_defocus   (lv_event_t* e);
    static void on_kb_done      (lv_event_t* e);

    std::string ota_manifest_url_;
};
