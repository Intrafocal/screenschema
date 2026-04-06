#pragma once
#include "ss_wifi_transport.hpp"

// Hosted WiFi transport: WiFi is handled by a coprocessor (e.g. ESP32-C6 on P4 boards).
// Uses esp-wifi-remote over SPI/SDIO. After init(), esp_wifi_* APIs are transparently
// proxied to the coprocessor — callers (SSWifiManager) need no changes.
//
// C6 must run Espressif hosted slave firmware:
//   https://github.com/espressif/esp-hosted (slave/ directory, target esp32c6)
//
// Requires in sdkconfig:
//   CONFIG_ESP_WIFI_REMOTE_ENABLED=y
//   CONFIG_ESP_WIFI_REMOTE_LIBRARY_EPPP=y   (or HOSTED, depending on firmware)

struct SSWifiTransportHostedConfig {
    int spi_host;       // e.g. SPI2_HOST
    int pin_mosi;
    int pin_miso;
    int pin_sclk;
    int pin_cs;
    int pin_handshake;  // data-ready / interrupt from coprocessor
    int pin_reset;      // optional, -1 if not wired
};

class SSWifiTransportHosted : public ISSWifiTransport {
public:
    explicit SSWifiTransportHosted(const SSWifiTransportHostedConfig& cfg);
    esp_err_t   init() override;
    const char* name() const override { return "hosted"; }
private:
    SSWifiTransportHostedConfig cfg_;
};
