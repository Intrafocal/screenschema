#include "ss_widget_factory.hpp"
#include "ss_event_bus.hpp"
#include "esp_log.h"
#include <string>

static const char* TAG = "SS_FACTORY";

// ---------------------------------------------------------------------------
// Helper: map SSAlign to lv_align_t
// ---------------------------------------------------------------------------
static lv_align_t ss_align_to_lv(SSAlign align) {
    switch (align) {
        case SSAlign::Center:      return LV_ALIGN_CENTER;
        case SSAlign::TopLeft:     return LV_ALIGN_TOP_LEFT;
        case SSAlign::TopMid:      return LV_ALIGN_TOP_MID;
        case SSAlign::TopRight:    return LV_ALIGN_TOP_RIGHT;
        case SSAlign::BottomLeft:  return LV_ALIGN_BOTTOM_LEFT;
        case SSAlign::BottomMid:   return LV_ALIGN_BOTTOM_MID;
        case SSAlign::BottomRight: return LV_ALIGN_BOTTOM_RIGHT;
        case SSAlign::LeftMid:     return LV_ALIGN_LEFT_MID;
        case SSAlign::RightMid:    return LV_ALIGN_RIGHT_MID;
        case SSAlign::Default:
        default:                   return LV_ALIGN_DEFAULT;
    }
}

// ---------------------------------------------------------------------------
// Helper: apply common config to any widget
// ---------------------------------------------------------------------------
static void apply_base_config(lv_obj_t* obj, const SSWidgetConfig& cfg) {
    lv_align_t lv_align = ss_align_to_lv(cfg.align);
    lv_obj_align(obj, lv_align, cfg.offset_x, cfg.offset_y);

    if (cfg.width > 0) {
        lv_obj_set_width(obj, cfg.width);
    }
    if (cfg.height > 0) {
        lv_obj_set_height(obj, cfg.height);
    }
    if (!cfg.visible) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Stub builder helper — used for unimplemented widget types
// ---------------------------------------------------------------------------
static lv_obj_t* make_stub(lv_obj_t* parent, const std::string& type_name) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_t* lbl = lv_label_create(obj);
    lv_label_set_text(lbl, ("[stub:" + type_name + "]").c_str());
    ESP_LOGW(TAG, "Widget type '%s' not yet implemented", type_name.c_str());
    return obj;
}

// ---------------------------------------------------------------------------
// SSWidgetFactory implementation
// ---------------------------------------------------------------------------

SSWidgetFactory::SSWidgetFactory() {
    registerBuiltins();
}

SSWidgetFactory& SSWidgetFactory::instance() {
    static SSWidgetFactory factory;
    return factory;
}

void SSWidgetFactory::registerType(const std::string& type_name, SSWidgetBuilderFn fn) {
    builders_[type_name] = std::move(fn);
    ESP_LOGI(TAG, "Registered widget builder for type '%s'", type_name.c_str());
}

lv_obj_t* SSWidgetFactory::build(const std::string& type_name, lv_obj_t* parent, const SSWidgetConfig& cfg) const {
    auto it = builders_.find(type_name);
    if (it == builders_.end()) {
        ESP_LOGE(TAG, "No builder found for widget type '%s'", type_name.c_str());
        return nullptr;
    }
    return it->second(parent, cfg);
}

// ---------------------------------------------------------------------------
// registerBuiltins
// ---------------------------------------------------------------------------

// Non-static; called from constructor
void SSWidgetFactory::registerBuiltins() {

    // --- label ---
    builders_["label"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* label = lv_label_create(parent);

        // Set text content
        auto text_it = cfg.props.find("text");
        if (text_it != cfg.props.end() && !text_it->second.empty()) {
            lv_label_set_text(label, text_it->second.c_str());
        } else {
            lv_label_set_text(label, "");
        }

        // Long mode
        auto lm_it = cfg.props.find("long_mode");
        if (lm_it != cfg.props.end()) {
            const std::string& lm = lm_it->second;
            if (lm == "wrap") {
                lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            } else if (lm == "dot") {
                lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            } else if (lm == "scroll") {
                lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
            } else if (lm == "scroll_circular") {
                lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
            } else if (lm == "clip") {
                lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            }
        }

        apply_base_config(label, cfg);
        return label;
    };

    // --- button ---
    builders_["button"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* btn = lv_btn_create(parent);

        lv_obj_t* label = lv_label_create(btn);
        auto lbl_it = cfg.props.find("label");
        if (lbl_it != cfg.props.end() && !lbl_it->second.empty()) {
            lv_label_set_text(label, lbl_it->second.c_str());
        } else {
            lv_label_set_text(label, "");
        }
        lv_obj_center(label);

        apply_base_config(btn, cfg);

        // Register click event → SSEventBus
        // Heap-allocate the widget id so it outlives this scope (lives with the widget)
        std::string* id_ptr = new std::string(cfg.id);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            const std::string* id = static_cast<const std::string*>(lv_event_get_user_data(e));
            if (!id) return;

            SSEvent evt;
            evt.widget_id  = *id;
            evt.type       = SSEventType::Click;
            evt.int_value  = 0;
            SSEventBus::instance().publish(evt);
        }, LV_EVENT_CLICKED, static_cast<void*>(id_ptr));

        // Long-press event
        std::string* id_lp_ptr = new std::string(cfg.id);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            const std::string* id = static_cast<const std::string*>(lv_event_get_user_data(e));
            if (!id) return;
            SSEvent evt;
            evt.widget_id = *id;
            evt.type      = SSEventType::LongPress;
            evt.int_value = 0;
            SSEventBus::instance().publish(evt);
        }, LV_EVENT_LONG_PRESSED, static_cast<void*>(id_lp_ptr));

        // Released event
        std::string* id_rel_ptr = new std::string(cfg.id);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            const std::string* id = static_cast<const std::string*>(lv_event_get_user_data(e));
            if (!id) return;
            SSEvent evt;
            evt.widget_id = *id;
            evt.type      = SSEventType::Released;
            evt.int_value = 0;
            SSEventBus::instance().publish(evt);
        }, LV_EVENT_RELEASED, static_cast<void*>(id_rel_ptr));

        return btn;
    };

    // --- dropdown ---
    builders_["dropdown"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* dd = lv_dropdown_create(parent);

        // Initial options (newline-separated)
        auto opts_it = cfg.props.find("options");
        if (opts_it != cfg.props.end() && !opts_it->second.empty()) {
            lv_dropdown_set_options(dd, opts_it->second.c_str());
        } else {
            lv_dropdown_set_options(dd, "—");
        }

        apply_base_config(dd, cfg);

        // on_select → SSEventBus
        if (!cfg.on_select.empty()) {
            std::string* id_ptr = new std::string(cfg.id);
            lv_obj_add_event_cb(dd, [](lv_event_t* e) {
                auto* id = static_cast<std::string*>(lv_event_get_user_data(e));
                SSEvent evt;
                evt.widget_id  = *id;
                evt.type       = SSEventType::Selected;
                evt.int_value  = (int32_t)lv_dropdown_get_selected(lv_event_get_target(e));
                SSEventBus::instance().publish(evt);
            }, LV_EVENT_VALUE_CHANGED, static_cast<void*>(id_ptr));
        }

        return dd;
    };

    // --- text_input ---
    builders_["text_input"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* ta = lv_textarea_create(parent);
        lv_textarea_set_one_line(ta, true);

        auto ph_it = cfg.props.find("placeholder");
        if (ph_it != cfg.props.end() && !ph_it->second.empty()) {
            lv_textarea_set_placeholder_text(ta, ph_it->second.c_str());
        }

        auto pw_it = cfg.props.find("password");
        if (pw_it != cfg.props.end() && pw_it->second == "true") {
            lv_textarea_set_password_mode(ta, true);
        }

        apply_base_config(ta, cfg);

        // Create keyboard on the app screen — shown/hidden on focus/defocus.
        // Move to foreground so it renders on top of the scrollable container.
        lv_obj_t* screen = lv_obj_get_screen(parent);
        lv_obj_t* kb = lv_keyboard_create(screen);
        lv_obj_set_size(kb, LV_PCT(100), LV_PCT(45));
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb);

        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            lv_obj_t* kb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
            lv_obj_t* ta = lv_event_get_target(e);
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
            // Expand container bottom padding to match keyboard height so
            // scroll_to_view knows the usable area and places textarea above keyboard.
            lv_obj_t* container = lv_obj_get_parent(ta);
            lv_coord_t kb_h = lv_obj_get_height(kb);
            lv_obj_set_style_pad_bottom(container, kb_h + 4, 0);
            lv_obj_scroll_to_view(ta, LV_ANIM_ON);
        }, LV_EVENT_FOCUSED, kb);

        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            lv_obj_t* kb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
            // Restore normal bottom padding
            lv_obj_t* container = lv_obj_get_parent(lv_event_get_target(e));
            lv_obj_set_style_pad_bottom(container, 10, 0);
        }, LV_EVENT_DEFOCUSED, kb);

        // on_submit: fires when keyboard "Ok" is pressed
        if (!cfg.on_submit.empty()) {
            std::string* id_ptr = new std::string(cfg.id);
            lv_obj_add_event_cb(kb, [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_READY) {
                    auto* id = static_cast<std::string*>(lv_event_get_user_data(e));
                    SSEvent evt;
                    evt.widget_id = *id;
                    evt.type      = SSEventType::Submit;
                    SSEventBus::instance().publish(evt);
                }
            }, LV_EVENT_READY, static_cast<void*>(id_ptr));
        }

        return ta;
    };

    // --- toggle ---
    builders_["toggle"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* sw = lv_switch_create(parent);

        auto checked_it = cfg.props.find("checked");
        if (checked_it != cfg.props.end() && checked_it->second == "true") {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }

        apply_base_config(sw, cfg);

        if (!cfg.on_change.empty()) {
            std::string* id_ptr = new std::string(cfg.id);
            lv_obj_add_event_cb(sw, [](lv_event_t* e) {
                const std::string* id = static_cast<const std::string*>(lv_event_get_user_data(e));
                if (!id) return;
                SSEvent evt;
                evt.widget_id  = *id;
                evt.type       = SSEventType::ValueChanged;
                evt.bool_value = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
                SSEventBus::instance().publish(evt);
            }, LV_EVENT_VALUE_CHANGED, static_cast<void*>(id_ptr));
        }

        return sw;
    };

    // --- slider ---
    builders_["slider"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* slider = lv_slider_create(parent);

        auto min_it = cfg.props.find("min");
        auto max_it = cfg.props.find("max");
        if (min_it != cfg.props.end() && max_it != cfg.props.end()) {
            lv_slider_set_range(slider, std::atoi(min_it->second.c_str()), std::atoi(max_it->second.c_str()));
        }

        auto val_it = cfg.props.find("value");
        if (val_it != cfg.props.end()) {
            lv_slider_set_value(slider, std::atoi(val_it->second.c_str()), LV_ANIM_OFF);
        }

        apply_base_config(slider, cfg);

        if (!cfg.on_change.empty()) {
            std::string* id_ptr = new std::string(cfg.id);
            lv_obj_add_event_cb(slider, [](lv_event_t* e) {
                const std::string* id = static_cast<const std::string*>(lv_event_get_user_data(e));
                if (!id) return;
                SSEvent evt;
                evt.widget_id  = *id;
                evt.type       = SSEventType::ValueChanged;
                evt.int_value  = lv_slider_get_value(lv_event_get_target(e));
                SSEventBus::instance().publish(evt);
            }, LV_EVENT_VALUE_CHANGED, static_cast<void*>(id_ptr));
        }

        return slider;
    };

    // --- arc ---
    builders_["arc"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* arc = lv_arc_create(parent);

        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);

        auto min_it = cfg.props.find("min");
        auto max_it = cfg.props.find("max");
        if (min_it != cfg.props.end() && max_it != cfg.props.end()) {
            lv_arc_set_range(arc, std::atoi(min_it->second.c_str()), std::atoi(max_it->second.c_str()));
        }

        auto val_it = cfg.props.find("value");
        if (val_it != cfg.props.end()) {
            lv_arc_set_value(arc, std::atoi(val_it->second.c_str()));
        }

        apply_base_config(arc, cfg);

        if (!cfg.on_change.empty()) {
            std::string* id_ptr = new std::string(cfg.id);
            lv_obj_add_event_cb(arc, [](lv_event_t* e) {
                const std::string* id = static_cast<const std::string*>(lv_event_get_user_data(e));
                if (!id) return;
                SSEvent evt;
                evt.widget_id  = *id;
                evt.type       = SSEventType::ValueChanged;
                evt.int_value  = lv_arc_get_value(lv_event_get_target(e));
                SSEventBus::instance().publish(evt);
            }, LV_EVENT_VALUE_CHANGED, static_cast<void*>(id_ptr));
        }

        return arc;
    };

    // --- progress_bar ---
    builders_["progress_bar"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* bar = lv_bar_create(parent);

        auto min_it = cfg.props.find("min");
        auto max_it = cfg.props.find("max");
        if (min_it != cfg.props.end() && max_it != cfg.props.end()) {
            lv_bar_set_range(bar, std::atoi(min_it->second.c_str()), std::atoi(max_it->second.c_str()));
        }

        auto val_it = cfg.props.find("value");
        if (val_it != cfg.props.end()) {
            lv_bar_set_value(bar, std::atoi(val_it->second.c_str()), LV_ANIM_OFF);
        }

        apply_base_config(bar, cfg);

        return bar;
    };

    // --- spinner ---
    builders_["spinner"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* spinner = lv_spinner_create(parent, 1000, 60);
        apply_base_config(spinner, cfg);
        return spinner;
    };

    // --- chart ---
    builders_["chart"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* chart = lv_chart_create(parent);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, 10);
        apply_base_config(chart, cfg);
        return chart;
    };

    // --- list ---
    builders_["list"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        lv_obj_t* list = lv_list_create(parent);
        apply_base_config(list, cfg);
        return list;
    };

    // --- image (stub — needs asset pipeline) ---
    builders_["image"] = [](lv_obj_t* parent, const SSWidgetConfig& cfg) -> lv_obj_t* {
        (void)cfg;
        return make_stub(parent, "image");
    };
}
