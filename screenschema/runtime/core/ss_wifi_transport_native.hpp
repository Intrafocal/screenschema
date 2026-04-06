#pragma once
#include "ss_wifi_transport.hpp"

// Native WiFi transport: chip has integrated WiFi (ESP32-S3, C3, C6, etc.).
// No transport init needed — esp_wifi_* works directly after esp_wifi_init().
class SSWifiTransportNative : public ISSWifiTransport {
public:
    esp_err_t   init() override { return ESP_OK; }
    const char* name() const override { return "native"; }
};
