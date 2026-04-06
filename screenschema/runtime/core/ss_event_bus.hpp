#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>

enum class SSEventType {
    Click, LongPress, ValueChanged, Released,
    Focused, Defocused, Submit, Selected
};

struct SSEvent {
    std::string widget_id;
    SSEventType type;
    union {
        int32_t  int_value;
        float    float_value;
        bool     bool_value;
    };
    const char* string_value = nullptr;

    SSEvent() : int_value(0) {}
};

using SSEventCallback = std::function<void(const SSEvent&)>;

class SSEventBus {
public:
    static SSEventBus& instance();

    void subscribe(const std::string& widget_id, SSEventType type, SSEventCallback cb);
    void unsubscribe(const std::string& widget_id, SSEventType type);
    void publish(const SSEvent& event);
    void clear();  // Remove all subscriptions (call when app closes)

private:
    SSEventBus() = default;
    struct SubKey { std::string id; SSEventType type; };
    std::unordered_map<std::string, std::vector<SSEventCallback>> listeners_;
    // key: widget_id + ":" + (int)type
    static std::string make_key(const std::string& id, SSEventType type);
};
