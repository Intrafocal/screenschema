#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "esp_brookesia.hpp"
#include "ss_input.hpp"
#include "ss_widget_config.hpp"
#include "lvgl.h"

class SSAppBase : public ESP_Brookesia_PhoneApp {
public:
    SSAppBase(const char* name, const void* launcher_icon = nullptr, bool use_default_screen = true);
    virtual ~SSAppBase() = default;

    // Public entry point for desktop sim — calls buildUI() on the given screen.
    void simBuildUI(lv_obj_t* screen) { buildUI(screen); }

    /// True while this app is the foreground (visible) app.
    bool isForeground() const { return foreground_; }

protected:
    // Generated app implements this to create widgets. Called by init() after screen is created.
    virtual void buildUI(lv_obj_t* screen) = 0;

    // Widget registry — records the id in this app's widget_map_ AND registers
    // it with the global SSContext. close() uses widget_map_ to unregister
    // exactly this app's widgets/subscriptions (paused apps keep theirs).
    void registerWidget(const char* id, lv_obj_t* obj,
                        SSWidgetType type = SSWidgetType::Custom);
    lv_obj_t* getWidget(const char* id) const;

    /// Register a key interceptor that only fires while this app is foreground.
    /// Auto-gated by the lifecycle (active after run()/resume(), inert after
    /// pause(), cleared on close()). Call from buildUI()/onInit() — LVGL task.
    void onAppKey(SSInput::KeyHandler handler);

    // Lifecycle hooks for generated apps
    // NOTE: onResume() now fires on resume-from-background as well as launch.
    virtual void onInit()   {}
    virtual void onResume() {}
    virtual void onPause()  {}
    virtual void onClose()  {}

private:
    bool init()   final;
    bool run()    final;
    bool resume() final;   // brookesia resume-from-background hook
    bool pause()  final;
    bool close()  final;
    bool back()   final;

    std::unordered_map<std::string, lv_obj_t*> widget_map_;
    std::vector<SSInput::KeyHandler> app_key_handlers_;
    bool foreground_ = false;
    bool key_trampoline_registered_ = false;
};
