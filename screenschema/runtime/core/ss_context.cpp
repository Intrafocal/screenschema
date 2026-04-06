#include "ss_context.hpp"
#include "esp_log.h"

static const char* TAG = "SS_CTX";

SSContext& SSContext::instance() {
    static SSContext ctx;
    return ctx;
}

void SSContext::registerWidget(const std::string& id, lv_obj_t* obj, SSWidgetType type) {
    if (widgets_.count(id)) {
        ESP_LOGW(TAG, "Widget id '%s' already registered — overwriting", id.c_str());
    }
    widgets_[id] = { obj, type };
    ESP_LOGI(TAG, "Registered widget '%s' (type %d)", id.c_str(), static_cast<int>(type));
}

// ---------------------------------------------------------------------------
// set() overloads
// ---------------------------------------------------------------------------

void SSContext::set(const std::string& id, int32_t value) {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "set(int32): widget '%s' not found", id.c_str());
        return;
    }
    lv_obj_t* obj = it->second.obj;
    switch (it->second.type) {
        case SSWidgetType::Label:
            lv_label_set_text_fmt(obj, "%d", (int)value);
            break;
        case SSWidgetType::Slider:
            lv_slider_set_value(obj, value, LV_ANIM_ON);
            lv_obj_invalidate(obj);
            break;
        case SSWidgetType::Arc:
            lv_arc_set_value(obj, value);
            lv_obj_invalidate(obj);
            break;
        case SSWidgetType::ProgressBar:
            lv_bar_set_value(obj, value, LV_ANIM_ON);
            lv_obj_invalidate(obj);
            break;
        case SSWidgetType::Toggle:
            if (value) {
                lv_obj_add_state(obj, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(obj, LV_STATE_CHECKED);
            }
            lv_obj_invalidate(obj);
            break;
        default:
            lv_obj_invalidate(obj);
            break;
    }
}

void SSContext::set(const std::string& id, float value) {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "set(float): widget '%s' not found", id.c_str());
        return;
    }
    switch (it->second.type) {
        case SSWidgetType::Label: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", value);
            lv_label_set_text(it->second.obj, buf);
            break;
        }
        default:
            // For arc/slider/bar: round to nearest int
            set(id, static_cast<int32_t>(value + 0.5f));
            return;
    }
}

void SSContext::set(const std::string& id, const std::string& text) {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "set(string): widget '%s' not found", id.c_str());
        return;
    }
    lv_obj_t* obj = it->second.obj;
    switch (it->second.type) {
        case SSWidgetType::Label:
            lv_label_set_text(obj, text.c_str());
            lv_obj_invalidate(obj);
            break;
        case SSWidgetType::TextInput:
            lv_textarea_set_text(obj, text.c_str());
            lv_obj_invalidate(obj);
            break;
        case SSWidgetType::Dropdown:
            lv_dropdown_set_options(obj, text.c_str());
            lv_obj_invalidate(obj);
            break;
        default:
            lv_obj_invalidate(obj);
            break;
    }
}

void SSContext::set(const std::string& id, bool value) {
    set(id, static_cast<int32_t>(value ? 1 : 0));
}

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------

void SSContext::show(const std::string& id) {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "show: widget '%s' not found", id.c_str());
        return;
    }
    lv_obj_t* obj = it->second.obj;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(lv_obj_get_parent(obj));
}

void SSContext::hide(const std::string& id) {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "hide: widget '%s' not found", id.c_str());
        return;
    }
    lv_obj_t* obj = it->second.obj;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(lv_obj_get_parent(obj));
}

void SSContext::toggle_visible(const std::string& id) {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "toggle_visible: widget '%s' not found", id.c_str());
        return;
    }
    lv_obj_t* obj = it->second.obj;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(lv_obj_get_parent(obj));
}

// ---------------------------------------------------------------------------
// Event delegation
// ---------------------------------------------------------------------------

void SSContext::on(const std::string& id, SSEventType event, SSEventCallback cb) {
    SSEventBus::instance().subscribe(id, event, std::move(cb));
}

void SSContext::off(const std::string& id, SSEventType event) {
    SSEventBus::instance().unsubscribe(id, event);
}

// ---------------------------------------------------------------------------
// setOptions (dropdown)
// ---------------------------------------------------------------------------

void SSContext::setOptions(const std::string& id, const std::string& options_nl) {
    set(id, options_nl);  // routes through set(string) → lv_dropdown_set_options
}

void SSContext::setOptions(const std::string& id, const std::vector<std::string>& options) {
    std::string joined;
    for (size_t i = 0; i < options.size(); ++i) {
        if (i > 0) joined += '\n';
        joined += options[i];
    }
    setOptions(id, joined);
}

// ---------------------------------------------------------------------------
// List widget helpers
// ---------------------------------------------------------------------------

void SSContext::list_add(const std::string& id, const std::string& text, const char* symbol) {
    auto it = widgets_.find(id);
    if (it == widgets_.end() || it->second.type != SSWidgetType::List) {
        ESP_LOGW(TAG, "list_add: '%s' not found or not a list", id.c_str());
        return;
    }
    lv_list_add_btn(it->second.obj, symbol, text.c_str());
}

void SSContext::list_clear(const std::string& id) {
    auto it = widgets_.find(id);
    if (it == widgets_.end() || it->second.type != SSWidgetType::List) {
        ESP_LOGW(TAG, "list_clear: '%s' not found or not a list", id.c_str());
        return;
    }
    lv_obj_clean(it->second.obj);
}

void SSContext::list_set_items(const std::string& id, const std::vector<std::string>& items) {
    list_clear(id);
    for (const auto& item : items) {
        list_add(id, item);
    }
}

// ---------------------------------------------------------------------------
// Raw access
// ---------------------------------------------------------------------------

lv_obj_t* SSContext::raw(const std::string& id) const {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "raw: widget '%s' not found", id.c_str());
        return nullptr;
    }
    return it->second.obj;
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void SSContext::clear() {
    widgets_.clear();
    SSEventBus::instance().clear();
    ESP_LOGI(TAG, "Widget registry and subscriptions cleared");
}

// ---------------------------------------------------------------------------
// get<T> specialisations
// ---------------------------------------------------------------------------

template<>
int32_t SSContext::get<int32_t>(const std::string& id) const {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "get<int32>: widget '%s' not found", id.c_str());
        return 0;
    }
    lv_obj_t* obj = it->second.obj;
    switch (it->second.type) {
        case SSWidgetType::Slider:
            return lv_slider_get_value(obj);
        case SSWidgetType::Arc:
            return lv_arc_get_value(obj);
        case SSWidgetType::ProgressBar:
            return lv_bar_get_value(obj);
        case SSWidgetType::Toggle:
            return lv_obj_has_state(obj, LV_STATE_CHECKED) ? 1 : 0;
        default:
            return 0;
    }
}

template<>
float SSContext::get<float>(const std::string& id) const {
    return static_cast<float>(get<int32_t>(id));
}

template<>
bool SSContext::get<bool>(const std::string& id) const {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "get<bool>: widget '%s' not found", id.c_str());
        return false;
    }
    if (it->second.type == SSWidgetType::Toggle) {
        return lv_obj_has_state(it->second.obj, LV_STATE_CHECKED);
    }
    return false;
}

template<>
std::string SSContext::get<std::string>(const std::string& id) const {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        ESP_LOGW(TAG, "get<string>: widget '%s' not found", id.c_str());
        return "";
    }
    lv_obj_t* obj = it->second.obj;
    switch (it->second.type) {
        case SSWidgetType::Label: {
            const char* txt = lv_label_get_text(obj);
            return txt ? txt : "";
        }
        case SSWidgetType::TextInput: {
            const char* txt = lv_textarea_get_text(obj);
            return txt ? txt : "";
        }
        case SSWidgetType::Dropdown: {
            // Return the currently selected option text
            char buf[128] = {};
            lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
            return std::string(buf);
        }
        default:
            return "";
    }
}
