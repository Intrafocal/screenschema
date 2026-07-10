#include "ss_event_bus.hpp"
#include "esp_log.h"

static const char* TAG = "SS_EVBUS";

SSEventBus& SSEventBus::instance() {
    static SSEventBus bus;
    return bus;
}

std::string SSEventBus::make_key(const std::string& id, SSEventType type) {
    return id + ":" + std::to_string(static_cast<int>(type));
}

void SSEventBus::subscribe(const std::string& widget_id, SSEventType type, SSEventCallback cb) {
    const std::string key = make_key(widget_id, type);
    listeners_[key].push_back(std::move(cb));
    ESP_LOGI(TAG, "Subscribed to widget '%s' event %d", widget_id.c_str(), static_cast<int>(type));
}

void SSEventBus::unsubscribe(const std::string& widget_id, SSEventType type) {
    const std::string key = make_key(widget_id, type);
    auto it = listeners_.find(key);
    if (it != listeners_.end()) {
        listeners_.erase(it);
        ESP_LOGI(TAG, "Unsubscribed from widget '%s' event %d", widget_id.c_str(), static_cast<int>(type));
    } else {
        ESP_LOGW(TAG, "No subscription found for widget '%s' event %d", widget_id.c_str(), static_cast<int>(type));
    }
}

void SSEventBus::clearWidget(const std::string& widget_id) {
    // Keys are "<widget_id>:<int type>" — erase every event type for this id.
    // The trailing no-further-colon check keeps widget ids that merely share
    // a prefix (e.g. "a" vs "a:1") from being swept together.
    const std::string prefix = widget_id + ":";
    for (auto it = listeners_.begin(); it != listeners_.end(); ) {
        const std::string& key = it->first;
        if (key.size() > prefix.size() &&
            key.compare(0, prefix.size(), prefix) == 0 &&
            key.find(':', prefix.size()) == std::string::npos) {
            it = listeners_.erase(it);
        } else {
            ++it;
        }
    }
}

void SSEventBus::clear() {
    listeners_.clear();
    ESP_LOGI(TAG, "All subscriptions cleared");
}

void SSEventBus::publish(const SSEvent& event) {
    const std::string key = make_key(event.widget_id, event.type);
    auto it = listeners_.find(key);
    if (it == listeners_.end()) {
        return;
    }
    for (const auto& cb : it->second) {
        cb(event);
    }
}
