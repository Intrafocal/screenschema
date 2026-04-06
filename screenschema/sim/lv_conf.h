// screenschema sim — LVGL configuration for desktop SDL2
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// Color depth: 16-bit (RGB565) to match typical ESP32 displays
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0   // No byte-swap needed on desktop

// Memory
#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE         (512U * 1024U)

// Display refresh
#define LV_DISP_DEF_REFR_PERIOD 10
#define LV_INDEV_DEF_READ_PERIOD 10

// DPI for desktop
#define LV_DPI_DEF          130

// Logging
#define LV_USE_LOG           1
#define LV_LOG_LEVEL         LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF        1

// Built-in fonts
#define LV_FONT_MONTSERRAT_8   1
#define LV_FONT_MONTSERRAT_10  1
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_22  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_36  1
#define LV_FONT_MONTSERRAT_48  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

// Widgets (match ESP-IDF build — all enabled)
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1

// Extra widgets
#define LV_USE_CHART      1
#define LV_USE_COLORWHEEL 1
#define LV_USE_IMGBTN     1
#define LV_USE_LED        1
#define LV_USE_LIST       1
#define LV_USE_METER      1
#define LV_USE_MSGBOX     1
#define LV_USE_SPAN       1
#define LV_USE_SPINBOX    1
#define LV_USE_SPINNER    1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   1
#define LV_USE_WIN        1

// Themes
#define LV_USE_THEME_DEFAULT  1
#define LV_USE_THEME_MONO     0

// Layouts
#define LV_USE_FLEX       1
#define LV_USE_GRID       1

// Required by widget factory for label long mode
#define LV_LABEL_TEXT_SELECTION 1
#define LV_LABEL_LONG_TXT_HINT 1

#endif // LV_CONF_H
