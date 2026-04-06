#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include "lvgl.h"
#include "ss_widget_config.hpp"
#include "ss_event_bus.hpp"

class SSContext {
public:
    static SSContext& instance();

    // Called by generated buildUI() code to register widgets
    void registerWidget(const std::string& id, lv_obj_t* obj, SSWidgetType type);

    // Read widget values
    template<typename T> T get(const std::string& id) const;

    // Write widget values (triggers partial redraw)
    void set(const std::string& id, int32_t value);
    void set(const std::string& id, float value);
    void set(const std::string& id, const std::string& text);
    void set(const std::string& id, bool value);

    // Visibility
    void show(const std::string& id);
    void hide(const std::string& id);
    void toggle_visible(const std::string& id);

    // Event subscription (delegates to SSEventBus)
    void on(const std::string& id, SSEventType event, SSEventCallback cb);
    void off(const std::string& id, SSEventType event);

    // Dropdown — set the option list (newline-separated string, or vector convenience)
    void setOptions(const std::string& id, const std::string& options_nl);
    void setOptions(const std::string& id, const std::vector<std::string>& options);

    // List widget — add items, clear, or replace all items
    void list_add(const std::string& id, const std::string& text, const char* symbol = nullptr);
    void list_clear(const std::string& id);
    void list_set_items(const std::string& id, const std::vector<std::string>& items);

    // Direct lv_obj_t access (escape hatch)
    lv_obj_t* raw(const std::string& id) const;

    // Clear all registrations (called on app close)
    void clear();

private:
    SSContext() = default;

    struct WidgetEntry {
        lv_obj_t*    obj;
        SSWidgetType type;
    };
    std::unordered_map<std::string, WidgetEntry> widgets_;
};

// Explicit specialisations declared here, defined in .cpp
template<> int32_t     SSContext::get<int32_t>(const std::string& id) const;
template<> float       SSContext::get<float>(const std::string& id) const;
template<> bool        SSContext::get<bool>(const std::string& id) const;
template<> std::string SSContext::get<std::string>(const std::string& id) const;
