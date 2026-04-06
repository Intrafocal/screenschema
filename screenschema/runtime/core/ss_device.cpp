#include "ss_device.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include <cstdio>

static const char* TAG = "SS_DEVICE";

std::string SSDevice::id_;
std::string SSDevice::location_;
std::string SSDevice::friendly_name_;

void SSDevice::init(const char* id, const char* location, const char* friendly_name) {
    if (id && id[0] != '\0') {
        id_ = id;
    } else {
        // Derive from WiFi STA MAC address: hub-AABBCC (last 3 bytes)
        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char buf[16];
        snprintf(buf, sizeof(buf), "hub-%02X%02X%02X", mac[3], mac[4], mac[5]);
        id_ = buf;
    }
    location_      = location      ? location      : "";
    friendly_name_ = (friendly_name && friendly_name[0] != '\0') ? friendly_name : id_;

    ESP_LOGI(TAG, "Device: id=%s  location=%s  name=%s",
             id_.c_str(), location_.c_str(), friendly_name_.c_str());
}

const std::string& SSDevice::id()           { return id_; }
const std::string& SSDevice::location()     { return location_; }
const std::string& SSDevice::friendlyName() { return friendly_name_; }
