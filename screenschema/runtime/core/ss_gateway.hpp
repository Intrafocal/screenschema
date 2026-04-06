#pragma once
#include <string>
#include "esp_err.h"

// SSGateway — IoT gateway registration client
//
// On boot, the device POSTs its device_id + PSK to the gateway's
// /api/register endpoint. The gateway returns a JWT and runtime config
// (websocket URL, OTA URL, heartbeat URL). The JWT is stored in NVS
// and attached to all subsequent HTTP/WS requests.
//
// Call flow:
//   SSGateway::instance().init(gateway_url, device_id, secret);
//   SSGateway::instance().registerDevice();  // blocks until success or max retries
//   auto& gw = SSGateway::instance();
//   gw.token();        // JWT for Authorization header
//   gw.wsUrl();        // WebSocket URL from gateway config
//   gw.otaUrl();       // OTA manifest URL
//   gw.heartbeatUrl(); // Heartbeat URL

class SSGateway {
public:
    static SSGateway& instance();

    void init(const std::string& gateway_url,
              const std::string& device_id,
              const std::string& secret);

    // Blocking registration with retry + backoff. Returns ESP_OK on success.
    esp_err_t registerDevice();

    // Re-register (call on 401 from any endpoint).
    esp_err_t refreshToken();

    bool isRegistered() const { return registered_; }

    const std::string& token() const { return token_; }
    const std::string& gatewayUrl() const { return gateway_url_; }
    const std::string& wsUrl() const { return ws_url_; }
    const std::string& otaUrl() const { return ota_url_; }
    const std::string& heartbeatUrl() const { return heartbeat_url_; }
    int heartbeatIntervalS() const { return heartbeat_interval_s_; }

private:
    SSGateway() = default;
    esp_err_t do_register();
    void loadTokenFromNvs();
    void saveTokenToNvs();

    std::string gateway_url_;
    std::string device_id_;
    std::string secret_;

    std::string token_;
    std::string ws_url_;
    std::string ota_url_;
    std::string heartbeat_url_;
    int heartbeat_interval_s_ = 30;

    bool registered_ = false;
};
