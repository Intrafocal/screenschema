#include "ss_wifi_transport_hosted.hpp"
#include "esp_log.h"

static const char* TAG = "SS_WIFI_HOSTED";

SSWifiTransportHosted::SSWifiTransportHosted(const SSWifiTransportHostedConfig& cfg)
    : cfg_(cfg) {}

esp_err_t SSWifiTransportHosted::init() {
    ESP_LOGI(TAG, "Initialising hosted WiFi transport (SPI host=%d)", cfg_.spi_host);

    // esp-wifi-remote transport initialisation.
    //
    // The exact API depends on which hosted protocol the C6 slave firmware uses:
    //
    //   Option A — esp_hosted (classic, uses esp_wifi_remote via SPI):
    //     #include "esp_hosted.h"
    //     esp_hosted_config_t cfg = ESP_HOSTED_DEFAULT_CONFIG();
    //     cfg.transport = ESP_HOSTED_TRANSPORT_SPI;
    //     cfg.spi.host  = (spi_host_device_t)cfg_.spi_host;
    //     cfg.spi.mosi  = cfg_.pin_mosi; ...
    //     return esp_hosted_init(&cfg);
    //
    //   Option B — eppp (lightweight PPP-over-SPI, IDF 5.3+):
    //     #include "eppp_link.h"
    //     eppp_config_t cfg = EPPP_DEFAULT_CLIENT_CONFIG();
    //     cfg.transport = EPPP_TRANSPORT_SPI;
    //     cfg.spi.host  = (spi_host_device_t)cfg_.spi_host;
    //     cfg.spi.mosi  = cfg_.pin_mosi; ...
    //     eppp_handle_t h = eppp_connect(&cfg);
    //     return h ? ESP_OK : ESP_FAIL;
    //
    // Uncomment/adapt the block that matches the slave firmware on the C6.
    // Board-specific pin values come from board YAML → generated main.cpp constructor.

    ESP_LOGW(TAG, "Hosted transport not yet configured for this board — fill in init()");
    return ESP_ERR_NOT_SUPPORTED;
}
