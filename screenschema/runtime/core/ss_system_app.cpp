#include "ss_system_app.hpp"
#include "ss_widget_factory.hpp"
#include "esp_log.h"
#include <vector>

static const char* TAG = "SS_SYS_APP";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SSSystemApp::SSSystemApp(bool wifi, bool ota, const std::string& ota_manifest_url)
    : SSAppBase("Settings", nullptr, true)
    , wifi_enabled_(wifi)
    , ota_enabled_(ota)
    , ota_manifest_url_(ota_manifest_url)
{}

// ---------------------------------------------------------------------------
// Widget helpers
// ---------------------------------------------------------------------------

static lv_obj_t* make_label(lv_obj_t* parent, const char* text, lv_coord_t y,
                              bool dot_overflow = false)
{
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    if (dot_overflow) {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, lv_pct(90));
    }
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
    return lbl;
}

static lv_obj_t* make_button(lv_obj_t* parent, const char* label,
                               lv_coord_t x, lv_coord_t y)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, x, y);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    return btn;
}

// ---------------------------------------------------------------------------
// buildUI — called by SSAppBase::run() each time the app opens
// ---------------------------------------------------------------------------

void SSSystemApp::buildUI(lv_obj_t* container) {
    container_     = container;
    open_          = true;
    selected_ssid_.clear();

    lv_obj_t* screen = lv_obj_get_parent(container);

    // Shared on-screen keyboard — only created when no hardware keyboard is
    // present (B15).  When kb_ is null, on_ta_focus / on_ta_defocus / on_kb_done
    // become no-ops and the user types directly via the I2C keyboard's keypad
    // indev.
    if (!SSWidgetFactory::hasHardwareKeyboard()) {
        kb_ = lv_keyboard_create(screen);
        lv_obj_set_size(kb_, lv_pct(100), lv_pct(45));
        lv_obj_align(kb_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_flag(kb_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb_);
        lv_obj_add_event_cb(kb_, on_kb_done, LV_EVENT_READY,  this);
        lv_obj_add_event_cb(kb_, on_kb_done, LV_EVENT_CANCEL, this);
    }

    lv_coord_t y = 10;

    // -----------------------------------------------------------------------
    // WiFi section
    // -----------------------------------------------------------------------
    if (wifi_enabled_) {
        make_label(container, "WiFi", y);
        y += 30;

        std::string status_text = SSWifiManager::instance().isConnected()
            ? "Connected: " + SSWifiManager::instance().connectedSSID()
              + "  " + SSWifiManager::instance().ipAddress()
            : "Not connected";
        wifi_status_ = make_label(container, status_text.c_str(), y, /*dot=*/true);
        y += 40;

        wifi_ssid_dd_ = lv_dropdown_create(container);
        lv_dropdown_set_options(wifi_ssid_dd_, "Scanning...");
        lv_obj_set_size(wifi_ssid_dd_, 200, 40);
        lv_obj_align(wifi_ssid_dd_, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_add_event_cb(wifi_ssid_dd_, on_ssid_change, LV_EVENT_VALUE_CHANGED, this);
        y += 55;

        // Auto-trigger a scan as soon as the WiFi screen opens (B14) so the
        // dropdown populates without the user having to press the Scan button.
        SSWifiManager::instance().scan([this](std::vector<SSWifiManager::AP> aps) {
            if (!open_ || !wifi_ssid_dd_) return;
            if (aps.empty()) {
                lv_dropdown_set_options(wifi_ssid_dd_, "No networks found");
                if (wifi_status_) lv_label_set_text(wifi_status_, "No networks found");
                return;
            }
            std::string opts;
            for (size_t i = 0; i < aps.size(); ++i) {
                if (i) opts += "\n";
                opts += aps[i].ssid + (aps[i].secured ? " *" : "");
            }
            lv_dropdown_set_options(wifi_ssid_dd_, opts.c_str());
            selected_ssid_ = aps[0].ssid;
            if (wifi_status_) {
                lv_label_set_text(wifi_status_,
                    (std::to_string(aps.size()) + " network(s) found").c_str());
            }
        });

        wifi_password_ = lv_textarea_create(container);
        lv_textarea_set_placeholder_text(wifi_password_, "Password");
        lv_textarea_set_password_mode(wifi_password_, true);
        lv_textarea_set_one_line(wifi_password_, true);
        lv_obj_set_size(wifi_password_, 200, 40);
        lv_obj_align(wifi_password_, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_add_event_cb(wifi_password_, on_ta_focus,   LV_EVENT_FOCUSED,   this);
        lv_obj_add_event_cb(wifi_password_, on_ta_defocus, LV_EVENT_DEFOCUSED, this);
        y += 55;

        lv_obj_t* scan_btn = make_button(container, "Scan", -60, y);
        lv_obj_add_event_cb(scan_btn, on_scan_click, LV_EVENT_CLICKED, this);

        lv_obj_t* connect_btn = make_button(container, "Connect", 60, y);
        lv_obj_add_event_cb(connect_btn, on_connect_click, LV_EVENT_CLICKED, this);
        y += 50;
    }

    // -----------------------------------------------------------------------
    // OTA section
    // -----------------------------------------------------------------------
    if (ota_enabled_) {
        y += 10;
        make_label(container, "Firmware Update", y);
        y += 30;

        std::string ver = "Version: " + SSUpdateManager::currentVersion();
        make_label(container, ver.c_str(), y, /*dot=*/true);
        y += 30;

        ota_status_ = make_label(container, "Ready", y, /*dot=*/true);
        y += 35;

        lv_obj_t* upd_btn = make_button(container, "Check for Updates", 0, y);
        lv_obj_add_event_cb(upd_btn, on_ota_click, LV_EVENT_CLICKED, this);

        // Wire progress / completion back to the status label.
        // Callbacks are re-registered each time the app opens (last-write-wins).
        SSUpdateManager::instance().onProgress([this](int pct) {
            if (!open_ || !ota_status_) return;
            std::string msg = (pct >= 0)
                ? "Downloading " + std::to_string(pct) + "%"
                : "Downloading...";
            lv_label_set_text(ota_status_, msg.c_str());
        });
        SSUpdateManager::instance().onComplete([this](bool ok, const std::string& msg) {
            if (!open_ || !ota_status_) return;
            lv_label_set_text(ota_status_,
                ok ? "Done — rebooting..." : ("Error: " + msg).c_str());
        });
    }
}

// ---------------------------------------------------------------------------
// onClose — null out all widget pointers before the screen is destroyed
// ---------------------------------------------------------------------------

void SSSystemApp::onClose() {
    open_          = false;
    container_     = nullptr;
    kb_            = nullptr;
    wifi_status_   = nullptr;
    wifi_ssid_dd_  = nullptr;
    wifi_password_ = nullptr;
    ota_status_    = nullptr;
}

// ---------------------------------------------------------------------------
// Keyboard helpers
// ---------------------------------------------------------------------------

void SSSystemApp::on_kb_done(lv_event_t* e) {
    auto* self = static_cast<SSSystemApp*>(lv_event_get_user_data(e));
    if (!self->open_ || !self->kb_) return;
    lv_obj_add_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_bottom(self->container_, 10, 0);
    lv_obj_clear_state(lv_keyboard_get_textarea(self->kb_), LV_STATE_FOCUSED);
}

void SSSystemApp::on_ta_focus(lv_event_t* e) {
    auto* self = static_cast<SSSystemApp*>(lv_event_get_user_data(e));
    if (!self->open_ || !self->kb_) return;
    lv_obj_t* ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(self->kb_, ta);
    lv_obj_clear_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
    lv_coord_t kb_h = lv_obj_get_height(self->kb_);
    lv_obj_set_style_pad_bottom(self->container_, kb_h + 4, 0);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
}

void SSSystemApp::on_ta_defocus(lv_event_t* e) {
    auto* self = static_cast<SSSystemApp*>(lv_event_get_user_data(e));
    if (!self->open_ || !self->kb_) return;
    lv_obj_add_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_bottom(self->container_, 10, 0);
}

// ---------------------------------------------------------------------------
// WiFi callbacks
// ---------------------------------------------------------------------------

void SSSystemApp::on_scan_click(lv_event_t* e) {
    auto* self = static_cast<SSSystemApp*>(lv_event_get_user_data(e));
    if (!self->open_) return;
    lv_label_set_text(self->wifi_status_, "Scanning...");
    lv_dropdown_set_options(self->wifi_ssid_dd_, "Scanning...");

    SSWifiManager::instance().scan([self](std::vector<SSWifiManager::AP> aps) {
        if (!self->open_) return;
        if (aps.empty()) {
            lv_label_set_text(self->wifi_status_, "No networks found");
            lv_dropdown_set_options(self->wifi_ssid_dd_, "No networks found");
            return;
        }
        std::string opts;
        for (size_t i = 0; i < aps.size(); ++i) {
            if (i > 0) opts += '\n';
            opts += aps[i].ssid + (aps[i].secured ? " *" : "");
        }
        lv_dropdown_set_options(self->wifi_ssid_dd_, opts.c_str());
        self->selected_ssid_ = aps[0].ssid;
        lv_label_set_text(self->wifi_status_,
            (std::to_string(aps.size()) + " network(s) found").c_str());
    });
}

void SSSystemApp::on_ssid_change(lv_event_t* e) {
    auto* self = static_cast<SSSystemApp*>(lv_event_get_user_data(e));
    if (!self->open_ || !self->wifi_ssid_dd_) return;
    char buf[64] = {};
    lv_dropdown_get_selected_str(self->wifi_ssid_dd_, buf, sizeof(buf));
    std::string ssid(buf);
    if (ssid.size() >= 2 && ssid.substr(ssid.size() - 2) == " *")
        ssid.erase(ssid.size() - 2);
    self->selected_ssid_ = ssid;
    ESP_LOGI(TAG, "Selected SSID: %s", self->selected_ssid_.c_str());
}

void SSSystemApp::on_connect_click(lv_event_t* e) {
    auto* self = static_cast<SSSystemApp*>(lv_event_get_user_data(e));
    if (!self->open_) return;
    if (self->selected_ssid_.empty()) {
        lv_label_set_text(self->wifi_status_, "Select a network first");
        return;
    }
    const char* pw = lv_textarea_get_text(self->wifi_password_);
    std::string password(pw ? pw : "");
    lv_label_set_text(self->wifi_status_,
        ("Connecting to " + self->selected_ssid_ + "...").c_str());

    SSWifiManager::instance().connect(self->selected_ssid_, password, [self, password](bool ok) {
        if (!self->open_) return;
        if (ok) {
            lv_label_set_text(self->wifi_status_,
                ("Connected: " + SSWifiManager::instance().connectedSSID()
                 + "  " + SSWifiManager::instance().ipAddress()).c_str());
            lv_textarea_set_text(self->wifi_password_, "");
            // Persist credentials so device auto-connects on next boot (R1.3)
            SSWifiManager::instance().saveCredentials(
                SSWifiManager::instance().connectedSSID(), password);
        } else {
            lv_label_set_text(self->wifi_status_, "Connection failed");
        }
    });
}

// ---------------------------------------------------------------------------
// OTA callback
// ---------------------------------------------------------------------------

void SSSystemApp::on_ota_click(lv_event_t* e) {
    auto* self = static_cast<SSSystemApp*>(lv_event_get_user_data(e));
    if (!self->open_) return;
    if (SSUpdateManager::instance().isUpdating()) {
        lv_label_set_text(self->ota_status_, "Already updating...");
        return;
    }
    if (self->ota_manifest_url_.empty()) {
        lv_label_set_text(self->ota_status_, "No manifest URL configured");
        return;
    }
    lv_label_set_text(self->ota_status_, "Checking...");
    SSUpdateManager::instance().checkForUpdates(self->ota_manifest_url_);
}
