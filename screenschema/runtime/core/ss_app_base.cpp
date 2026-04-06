#include "ss_app_base.hpp"
#include "ss_context.hpp"
#include "esp_log.h"
#include <string>
#include <unordered_map>

static const char* TAG = "SS_APP";

SSAppBase::SSAppBase(const char* name, const void* launcher_icon, bool use_default_screen)
    : ESP_Brookesia_PhoneApp(name, launcher_icon, use_default_screen)
{
}

bool SSAppBase::init() {
    ESP_LOGI(TAG, "init()");
    // Note: app screen doesn't exist yet (created later in doRun → initDefaultScreen).
    // Build UI in run() instead, once the app screen is active.
    onInit();
    return true;
}

bool SSAppBase::run() {
    ESP_LOGI(TAG, "run()");
    // App screen is now active. Brookesia places a full-screen system overlay
    // on lv_layer_top() — the status bar (36px) is a child of that overlay,
    // so we can't query it from lv_layer_top() directly. Use the known height
    // from the 320_480 dark stylesheet instead.
    static constexpr lv_coord_t STATUS_BAR_H = 36;

    lv_obj_t* screen = lv_scr_act();
    lv_obj_t* container = lv_obj_create(screen);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_top(container, STATUS_BAR_H, 0);
    lv_obj_set_style_pad_bottom(container, 10, 0);
    lv_obj_set_style_pad_left(container, 0, 0);
    lv_obj_set_style_pad_right(container, 0, 0);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    buildUI(container);
    // Force layout computation so scroll_to_y takes effect immediately,
    // before lv_timer_handler can auto-scroll to the focused widget.
    lv_obj_update_layout(container);
    lv_obj_scroll_to_y(container, 0, LV_ANIM_OFF);
    onResume();
    return true;
}

bool SSAppBase::pause() {
    ESP_LOGI(TAG, "pause()");
    onPause();
    return true;
}

bool SSAppBase::close() {
    ESP_LOGI(TAG, "close()");
    onClose();
    SSContext::instance().clear();
    return true;
}

bool SSAppBase::back() {
    ESP_LOGI(TAG, "back()");
    notifyCoreClosed();
    return true;
}

void SSAppBase::registerWidget(const char* id, lv_obj_t* obj) {
    if (!id || !obj) {
        ESP_LOGW(TAG, "registerWidget called with null id or obj");
        return;
    }
    widget_map_[id] = obj;
    SSContext::instance().registerWidget(id, obj, SSWidgetType::Custom);
}

lv_obj_t* SSAppBase::getWidget(const char* id) const {
    if (!id) return nullptr;
    auto it = widget_map_.find(id);
    if (it == widget_map_.end()) {
        ESP_LOGW(TAG, "getWidget: '%s' not found", id);
        return nullptr;
    }
    return it->second;
}
