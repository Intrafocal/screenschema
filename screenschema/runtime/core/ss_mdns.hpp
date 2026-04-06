#pragma once
#include "esp_err.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

struct SSMdnsResult {
    std::string hostname;       // e.g., "lee-laptop"
    std::string ip;             // first IPv4 address found
    uint16_t    port = 0;
    std::unordered_map<std::string, std::string> txt;
};

class SSMdns {
public:
    // Advertise this device on the local network as <device_id>._screenschema._tcp.local.
    // Sets the mDNS hostname to SSDevice::id() (e.g. hub-kitchen.local).
    // TXT records: id, location, version.
    // Call after SSDeviceServer::instance().start() and after WiFi transport is initialized.
    static esp_err_t advertise(uint16_t service_port);

    // Query for services of a given type (D4).  Async — callback is delivered on
    // the LVGL task. Example: query("_lee", "_tcp", cb) finds all Lee instances.
    using QueryCallback = std::function<void(std::vector<SSMdnsResult>)>;
    static void query(const char* service_type, const char* proto,
                       uint32_t timeout_ms, QueryCallback cb);

    // Tear down mDNS. Rarely needed; call before deep sleep if required.
    static void stop();

private:
    static bool initialized_;
    static esp_err_t ensureInit();
};
