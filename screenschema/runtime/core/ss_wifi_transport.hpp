#pragma once
#include "esp_err.h"

// Abstract WiFi transport — implemented by native (no-op) and hosted (SPI/SDIO to coprocessor).
// SSWifiManager holds one of these and calls init() before any esp_wifi_* API.
class ISSWifiTransport {
public:
    virtual ~ISSWifiTransport() = default;
    virtual esp_err_t init() = 0;
    virtual const char* name() const = 0;
};
