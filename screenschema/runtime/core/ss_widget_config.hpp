#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

enum class SSWidgetType {
    Label, Button, Image, Slider, Arc, Toggle,
    ProgressBar, Spinner, Dropdown, TextInput, Chart, List, Custom
};

// Alignment — mirrors lv_align_t names
enum class SSAlign {
    Default, Center, TopLeft, TopMid, TopRight,
    BottomLeft, BottomMid, BottomRight,
    LeftMid, RightMid
};

struct SSStyleConfig {
    std::string font;        // font name; empty = theme default
    std::string color;       // hex "#RRGGBB"; empty = theme default
    std::string bg_color;
    std::string border_color;
    int32_t border_width = 0;
    int32_t radius = 0;
    int32_t padding_v = 0;
    int32_t padding_h = 0;
    uint8_t opacity = 255;
};

struct SSWidgetConfig {
    std::string id;
    SSWidgetType type = SSWidgetType::Label;
    SSAlign align = SSAlign::Default;
    int32_t offset_x = 0, offset_y = 0;
    int32_t width = -1, height = -1;   // -1 = natural size (LV_SIZE_CONTENT)
    bool visible = true;
    SSStyleConfig style;

    // Type-specific properties: text, label, range_min/max, value, checked, etc.
    std::unordered_map<std::string, std::string> props;

    // Event handler names (C function name strings)
    std::string on_click;
    std::string on_long_press;
    std::string on_change;
    std::string on_release;
    std::string on_submit;
    std::string on_select;
};
