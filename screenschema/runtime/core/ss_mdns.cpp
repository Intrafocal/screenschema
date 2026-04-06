#include "ss_mdns.hpp"
#include "ss_device.hpp"
#include "ss_update_manager.hpp"
#include "mdns.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <cstring>

static const char* TAG = "SS_MDNS";

bool SSMdns::initialized_ = false;

esp_err_t SSMdns::ensureInit() {
    if (initialized_) return ESP_OK;
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return err;
    }
    initialized_ = true;
    return ESP_OK;
}

esp_err_t SSMdns::advertise(uint16_t service_port) {
    bool was_initialized = initialized_;
    esp_err_t err = ensureInit();
    if (err != ESP_OK) return err;
    if (was_initialized) {
        ESP_LOGW(TAG, "Already initialized — skipping advertise setup");
        return ESP_OK;
    }

    // Hostname: hub-kitchen → accessible as hub-kitchen.local
    err = mdns_hostname_set(SSDevice::id().c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_hostname_set failed: %s", esp_err_to_name(err));
        return err;
    }

    mdns_instance_name_set(SSDevice::friendlyName().c_str());

    // TXT records for _screenschema._tcp service
    std::string version = SSUpdateManager::currentVersion();
    mdns_txt_item_t txt[] = {
        { "id",       SSDevice::id().c_str()       },
        { "location", SSDevice::location().c_str() },
        { "version",  version.c_str()              },
    };

    err = mdns_service_add(nullptr, "_screenschema", "_tcp", service_port, txt, 3);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_service_add failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "%s._screenschema._tcp.local:%u (id=%s location=%s v%s)",
             SSDevice::id().c_str(), (unsigned)service_port,
             SSDevice::id().c_str(), SSDevice::location().c_str(), version.c_str());
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// mDNS query (D4)
// ---------------------------------------------------------------------------

namespace {

struct QueryArgs {
    std::string service_type;
    std::string proto;
    uint32_t timeout_ms;
    SSMdns::QueryCallback cb;
};

// Run query on a worker task so we don't block the LVGL loop, then dispatch
// the result back via an LVGL async callback.
struct DispatchArg {
    SSMdns::QueryCallback cb;
    std::vector<SSMdnsResult>* results;
};

void deliver_on_lvgl(void* arg) {
    auto* d = static_cast<DispatchArg*>(arg);
    d->cb(std::move(*d->results));
    delete d->results;
    delete d;
}

void query_task(void* arg) {
    auto* a = static_cast<QueryArgs*>(arg);
    mdns_result_t* found = nullptr;
    auto* results = new std::vector<SSMdnsResult>();

    esp_err_t err = mdns_query_ptr(a->service_type.c_str(), a->proto.c_str(),
                                    a->timeout_ms, 20 /* max results */, &found);
    if (err == ESP_OK && found) {
        for (mdns_result_t* r = found; r != nullptr; r = r->next) {
            SSMdnsResult res;
            if (r->hostname) res.hostname = r->hostname;
            res.port = r->port;
            if (r->addr) {
                // Find first IPv4 address
                for (mdns_ip_addr_t* ip = r->addr; ip != nullptr; ip = ip->next) {
                    if (ip->addr.type == ESP_IPADDR_TYPE_V4) {
                        char buf[16];
                        esp_ip4addr_ntoa(&ip->addr.u_addr.ip4, buf, sizeof(buf));
                        res.ip = buf;
                        break;
                    }
                }
            }
            for (size_t i = 0; i < r->txt_count; ++i) {
                if (r->txt[i].key && r->txt[i].value) {
                    res.txt[r->txt[i].key] = r->txt[i].value;
                }
            }
            results->push_back(std::move(res));
        }
        mdns_query_results_free(found);
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_query_ptr(%s.%s) failed: %s",
                 a->service_type.c_str(), a->proto.c_str(), esp_err_to_name(err));
    }

    auto* d = new DispatchArg{ std::move(a->cb), results };
    lv_async_call(deliver_on_lvgl, d);

    delete a;
    vTaskDelete(nullptr);
}

}  // namespace

void SSMdns::query(const char* service_type, const char* proto,
                    uint32_t timeout_ms, QueryCallback cb) {
    if (ensureInit() != ESP_OK) {
        cb({});
        return;
    }
    auto* args = new QueryArgs{ service_type, proto, timeout_ms, std::move(cb) };
    xTaskCreate(query_task, "ss_mdns_q", 4096, args, 4, nullptr);
}

void SSMdns::stop() {
    if (!initialized_) return;
    mdns_service_remove("_screenschema", "_tcp");
    mdns_free();
    initialized_ = false;
    ESP_LOGI(TAG, "mDNS stopped");
}
