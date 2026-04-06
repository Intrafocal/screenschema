#pragma once
#include "lvgl.h"

/// Named font registry (D7).
///
/// LVGL ships built-in monospace bitmap fonts (unscii_8 and unscii_16) which
/// are enabled via CONFIG_LV_FONT_UNSCII_8/16 in the generated sdkconfig.
/// These are exposed here under friendlier aliases for handler-side use:
///
///     lv_obj_set_style_text_font(label, ss_font_monospace_8(), 0);
///
/// Use unscii_8 for ~40 column terminals on a 320px-wide display.
/// Use unscii_16 for ~20 columns at a more readable size.
namespace SSFonts {

inline const lv_font_t* monospace_8()  { return &lv_font_unscii_8;  }
inline const lv_font_t* monospace_16() { return &lv_font_unscii_16; }

/// Look up a font by name.  Returns nullptr if not found.
/// Names: "monospace_8", "monospace_16", "unscii_8", "unscii_16".
inline const lv_font_t* byName(const char* name) {
    if (!name) return nullptr;
    if (strcmp(name, "monospace_8")  == 0 || strcmp(name, "unscii_8")  == 0) return &lv_font_unscii_8;
    if (strcmp(name, "monospace_16") == 0 || strcmp(name, "unscii_16") == 0) return &lv_font_unscii_16;
    return nullptr;
}

}  // namespace SSFonts
