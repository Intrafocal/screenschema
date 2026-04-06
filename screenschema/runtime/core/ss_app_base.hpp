#pragma once
#include <string>
#include <unordered_map>
#include "esp_brookesia.hpp"
#include "lvgl.h"

class SSAppBase : public ESP_Brookesia_PhoneApp {
public:
    SSAppBase(const char* name, const void* launcher_icon = nullptr, bool use_default_screen = true);
    virtual ~SSAppBase() = default;

    // Public entry point for desktop sim — calls buildUI() on the given screen.
    void simBuildUI(lv_obj_t* screen) { buildUI(screen); }

protected:
    // Generated app implements this to create widgets. Called by init() after screen is created.
    virtual void buildUI(lv_obj_t* screen) = 0;

    // Widget registry (separate from SSContext for this app instance)
    void registerWidget(const char* id, lv_obj_t* obj);
    lv_obj_t* getWidget(const char* id) const;

    // Lifecycle hooks for generated apps
    virtual void onInit()   {}
    virtual void onResume() {}
    virtual void onPause()  {}
    virtual void onClose()  {}

private:
    bool init()  final;
    bool run()   final;
    bool pause() final;
    bool close() final;
    bool back()  final;

    std::unordered_map<std::string, lv_obj_t*> widget_map_;
};
