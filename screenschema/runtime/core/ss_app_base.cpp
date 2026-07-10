#include "ss_app_base.hpp"
#include "ss_context.hpp"
#include "ss_shell.hpp"
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
    foreground_ = true;                               // before the user hook
    SSShell::instance().notifyForeground(this);
    onResume();
    return true;
}

bool SSAppBase::resume() {
    ESP_LOGI(TAG, "resume()");
    // Screen was preserved (pause, not close) — do NOT rebuild UI.
    foreground_ = true;
    SSShell::instance().notifyForeground(this);
    onResume();
    return true;
}

bool SSAppBase::pause() {
    ESP_LOGI(TAG, "pause()");
    foreground_ = false;                              // gate keys before user hook
    SSShell::instance().notifyForeground(nullptr);    // incoming app re-asserts
    onPause();
    return true;
}

bool SSAppBase::close() {
    ESP_LOGI(TAG, "close()");
    foreground_ = false;
    SSShell::instance().notifyForeground(nullptr);
    onClose();
    app_key_handlers_.clear();   // buildUI closures may capture per-launch state;
                                 // trampoline stays registered but is inert
    // Scoped teardown: unregister only THIS app's widgets and their event
    // subscriptions. A global SSContext::clear() here would kill the bindings
    // of apps that are merely paused — their screens are preserved and
    // resume() never rebuilds UI, so they'd come back permanently dead.
    for (const auto& entry : widget_map_) {
        SSContext::instance().remove(entry.first);
    }
    widget_map_.clear();
    return true;
}

bool SSAppBase::back() {
    ESP_LOGI(TAG, "back()");
    notifyCoreClosed();
    return true;
}

void SSAppBase::onAppKey(SSInput::KeyHandler handler) {
    app_key_handlers_.push_back(std::move(handler));
    if (!key_trampoline_registered_) {
        key_trampoline_registered_ = true;
        // One trampoline per app instance. Apps are static in generated main
        // (main_cpp.j2:430) so `this` never dangles. SSInput registration is
        // append-only by design — the gate is foreground_, not (de)registration.
        SSInput::instance().onKey([this](uint8_t key, SSKeySource source) {
            if (!foreground_) return false;
            for (auto& h : app_key_handlers_)
                if (h(key, source)) return true;
            return false;
        });
    }
}

void SSAppBase::registerWidget(const char* id, lv_obj_t* obj, SSWidgetType type) {
    if (!id || !obj) {
        ESP_LOGW(TAG, "registerWidget called with null id or obj");
        return;
    }
    widget_map_[id] = obj;
    SSContext::instance().registerWidget(id, obj, type);
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
