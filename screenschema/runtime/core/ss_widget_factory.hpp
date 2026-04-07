#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include "lvgl.h"
#include "ss_widget_config.hpp"

using SSWidgetBuilderFn = std::function<lv_obj_t*(lv_obj_t* parent, const SSWidgetConfig& cfg)>;

class SSWidgetFactory {
public:
    static SSWidgetFactory& instance();

    void registerType(const std::string& type_name, SSWidgetBuilderFn fn);
    lv_obj_t* build(const std::string& type_name, lv_obj_t* parent, const SSWidgetConfig& cfg) const;

    // Register custom widget type (call before apps start)
    // Built-in types are registered automatically in the constructor.

    // Tell the factory whether a hardware keyboard is available on this board.
    // When true, text_input widgets skip the LVGL on-screen keyboard popup
    // because the user can type directly via the I2C keyboard's keypad indev.
    // Set this from main.cpp (codegen does it automatically when the board
    // profile defines a `keyboard:` block).
    static void setHasHardwareKeyboard(bool present);
    static bool hasHardwareKeyboard();

private:
    SSWidgetFactory();   // constructor calls registerBuiltins()
    void registerBuiltins();
    std::unordered_map<std::string, SSWidgetBuilderFn> builders_;
};
