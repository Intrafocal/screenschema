# ScreenSchema Implementation Plan

A declarative, YAML-driven UI framework for ESP32 devices with capacitive touchscreens,
built on top of esp-brookesia (LVGL v8). Generates complete ESP-IDF C++ projects from
human-readable schema files, with a hardware abstraction layer that supports a wide range
of displays, touch controllers, resolutions, and orientations behind a single unified API.

---

## Table of Contents

1. [Goals and Non-Goals](#1-goals-and-non-goals)
2. [Architecture Overview](#2-architecture-overview)
3. [YAML Schema Design](#3-yaml-schema-design)
4. [Hardware Abstraction Layer (HAL)](#4-hardware-abstraction-layer-hal)
5. [Runtime Library](#5-runtime-library)
6. [Component Library](#6-component-library)
7. [CLI Tool](#7-cli-tool)
8. [Hot Reload and Serial Bridge](#8-hot-reload-and-serial-bridge)
9. [Asset Pipeline](#9-asset-pipeline)
10. [Memory and Performance Strategy](#10-memory-and-performance-strategy)
11. [Board Profiles](#11-board-profiles)
12. [Backend API](#12-backend-api)
13. [Project Directory Structure](#13-project-directory-structure)
14. [Phased Implementation Plan](#14-phased-implementation-plan)
15. [Testing Strategy](#15-testing-strategy)

---

## 1. Goals and Non-Goals

### Goals

- **Declarative UI**: Describe entire UI in YAML — apps, screens, layouts, widgets, events.
- **Hot reload**: Watch mode rebuilds and reflashes on YAML save. Serial bridge enables
  data-only updates (widget values, text) without any reflash.
- **Component library**: Reusable YAML-level and C++-level components. Reference
  `type: metric_card` or `type: settings_screen` and get a full, styled, working UI element.
- **Standard screen layouts**: Built-in layouts for common patterns (dashboard, settings,
  list, single-screen) that are extensible by user-defined layouts.
- **Hardware abstraction layer**: One C++ interface for display init and one for touch input,
  regardless of whether the display is SPI, I2C, RGB parallel, or MIPI DSI, and regardless
  of the touch controller IC.
- **Resolution and orientation agnostic**: Layout engine uses percentage-based and
  flex/grid sizing. Board profile drives orientation and stylesheet selection automatically.
- **Low memory footprint**: LVGL dirty-region partial redraws, configurable draw buffer
  size, font subsetting in the asset pipeline, per-board memory budgets.
- **Clean backend API**: `SSContext::instance().set("widget_id", value)` on-device;
  `DeviceBridge.get("widget_id")` from host Python over serial.
- **Extensible**: Custom widget types and custom handlers require no changes to screenschema
  core — just add a component and register it.

### Non-Goals (for now)

- Runtime YAML parsing on-device (too expensive; structure is always compile-time).
- Supporting non-ESP32 microcontrollers (Arduino Uno, STM32, etc.).
- LVGL v9 support (esp-brookesia requires v8; v9 migration is a future major version).
- A visual drag-and-drop editor (use Squareline Studio for that; screenschema can consume
  Squareline exported code inside an app).
- Network/cloud OTA of full firmware (out of scope; use ESP-IDF OTA directly).

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Developer Workflow                          │
│                                                                     │
│  screenschema.yaml  ──►  screenschema build  ──►  ESP-IDF project  │
│  handlers.cpp       ──►  screenschema flash  ──►  flashed device   │
│  assets/            ──►  screenschema watch  ──►  auto-rebuild     │
└─────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│                        CLI Tool (Python)                             │
│                                                                      │
│  schema.py ──► validate YAML against JSON Schema                    │
│  board_registry.py ──► load board profile                           │
│  component_registry.py ──► resolve component/layout references      │
│  asset_pipeline.py ──► PNG→lv_img_dsc_t, font subsetting           │
│  codegen.py ──► Jinja2 templates → C++ project                     │
│  watcher.py ──► inotify/polling → rebuild loop                     │
│  serial_bridge.py ──► DeviceBridge Python class                    │
└──────────────────────────────────────────────────────────────────────┘
                              │
                              │ generates
                              ▼
┌──────────────────────────────────────────────────────────────────────┐
│                     Generated ESP-IDF Project                        │
│                                                                      │
│  main/                                                               │
│    main.cpp          ← app_main(): HAL init, Phone setup, app reg  │
│    SensorApp.hpp/cpp ← generated PhoneApp subclass per YAML app    │
│    SettingsApp.hpp/cpp                                               │
│    handlers.hpp      ← forward decls for all named event handlers  │
│    handlers.cpp      ← user-written; never overwritten             │
│    assets/           ← generated C image/font arrays               │
│    CMakeLists.txt    ← generated                                    │
│                                                                      │
│  components/                                                         │
│    screenschema_runtime/  ← symlink or copy of runtime/            │
└──────────────────────────────────────────────────────────────────────┘
                              │
                              │ links to
                              ▼
┌──────────────────────────────────────────────────────────────────────┐
│                      Runtime Library (C++)                           │
│                                                                      │
│  hal/                                                                │
│    ISSDisplay         ← pure virtual display interface              │
│    ISSTouch           ← pure virtual touch interface                │
│    drivers/display/   ← ST7789, ILI9341, ST7701S-DSI, GC9A01...   │
│    drivers/touch/     ← XPT2046, FT6236, GT911, CST816...         │
│                                                                      │
│  core/                                                               │
│    SSAppBase          ← thin PhoneApp wrapper; buildUI() lifecycle  │
│    SSWidgetFactory    ← type string → lv_obj_t* builder             │
│    SSContext          ← backend API singleton (get/set/on)         │
│    SSEventBus         ← internal pub/sub for widget events         │
│    SSSerialBridge     ← FreeRTOS task; data-only hot reload        │
│                                                                      │
│  components/                                                         │
│    SSMetricCard, SSToggleRow, SSSliderRow, SSSelectRow...           │
└──────────────────────────────────────────────────────────────────────┘
                              │
                              │ wraps
                              ▼
┌──────────────────────────────────────────────────────────────────────┐
│           esp-brookesia (LVGL v8 phone shell)                        │
│           esp-idf BSP / esp_lcd / esp_touch components              │
└──────────────────────────────────────────────────────────────────────┘
```

### Key design decisions

**Code generation over runtime parsing.** YAML is parsed on the host; the device only ever
runs compiled C++. This keeps memory footprint minimal, keeps LVGL usage idiomatic and
debuggable, and means zero startup overhead from parsing.

**The HAL is compile-time selected.** Board profiles specify which display driver and touch
driver to compile in. The generated `main.cpp` instantiates the correct concrete driver
classes; no runtime driver dispatch is needed. This keeps flash usage low.

**handlers.cpp is user territory.** The generator emits weak-linked stubs on first run,
then never touches the file again. All user logic lives there. YAML changes never clobber
handler code.

**SSContext is the seam between generated code and user code.** Generated widget
constructors register themselves with SSContext. User handlers and backend integrations
interact with widgets exclusively through SSContext, never through raw lv_obj_t pointers.
This makes handlers board- and layout-agnostic.

---

## 3. YAML Schema Design

### 3.1 Top-level structure

```yaml
# screenschema.yaml

schema_version: "1.0"

board: esp32p4-jc4880p443c    # board profile id; drives HAL, resolution, stylesheet

shell:
  theme: dark                  # dark | light | custom
  orientation: portrait        # portrait | landscape | portrait_inverted | landscape_inverted
  status_bar: true
  navigation: gesture          # gesture | bar | none
  wallpaper: assets/bg.png     # optional; omit for theme default

imports:                       # optional: include other YAML files for component defs
  - components/my_custom_card.yaml

apps:
  - ...                        # app definitions (see 3.2)
```

### 3.2 App definition

```yaml
apps:
  - id: sensor_dashboard       # C++ class name: SensorDashboardApp
    name: "Sensors"            # display name in launcher
    icon: assets/icons/sensor.png
    launcher_page: 0           # which home screen page (0-indexed)

    layout: dashboard          # built-in: single_screen | dashboard | settings_screen
                               #           list_screen | tabbed | scrollable
    layout_config:             # layout-specific parameters
      columns: 2
      rows: 3
      gap: 8                   # px between cells

    widgets:
      - ...                    # widget list (see 3.3)

    # Lifecycle event handlers
    on_init: handler_dashboard_init       # called once at first app launch
    on_resume: handler_start_polling      # called each time app becomes active
    on_pause: handler_stop_polling        # called when app goes to background
    on_close: handler_cleanup             # called when app is closed
```

### 3.3 Widget definitions

Every widget has a common set of base properties, then type-specific properties.

**Base properties (all widgets):**

```yaml
- type: label                  # widget type (built-in or custom)
  id: temp_label               # unique id; used in backend API and handler access
  align: center                # LVGL align names in snake_case
  offset: [0, -20]             # [x, y] pixel offset from align anchor
  size: [200, 40]              # [width, height]; omit to use natural size
  visible: true                # initial visibility
  style:                       # inline style overrides
    font: roboto_28            # font name from assets (subset defined in assets section)
    color: "#FFFFFF"           # text/foreground color
    bg_color: "#1A1A2E"        # background color
    border_color: "#444444"
    border_width: 1
    radius: 8
    padding: [8, 12]           # [vertical, horizontal]
    opacity: 255               # 0-255
```

**label:**
```yaml
- type: label
  id: temp_label
  text: "-- °C"
  long_mode: wrap              # wrap | scroll | clip | dots
```

**button:**
```yaml
- type: button
  id: refresh_btn
  label: "Refresh"
  icon: assets/icons/refresh.png   # optional icon alongside label
  on_click: handler_refresh
  on_long_press: handler_refresh_deep
```

**image:**
```yaml
- type: image
  id: logo
  src: assets/logo.png
  zoom: 256                    # 256 = 100%; LVGL scale factor
```

**slider:**
```yaml
- type: slider
  id: brightness_slider
  range: [0, 100]
  value: 80
  on_change: handler_brightness_change
  on_release: handler_brightness_released
```

**arc:**
```yaml
- type: arc
  id: temp_arc
  range: [0, 100]
  value: 72
  start_angle: 135
  end_angle: 45
  on_change: handler_arc_change
```

**toggle (lv_switch):**
```yaml
- type: toggle
  id: wifi_toggle
  checked: false
  on_change: handler_wifi_toggle
```

**progress_bar:**
```yaml
- type: progress_bar
  id: upload_progress
  range: [0, 100]
  value: 0
  anim_speed: 200              # ms per unit; 0 = no animation
```

**spinner:**
```yaml
- type: spinner
  id: loading_spinner
  speed: 1000                  # arc rotation period in ms
  arc_length: 60               # arc length in degrees
```

**dropdown:**
```yaml
- type: dropdown
  id: theme_select
  options:
    - "Dark"
    - "Light"
    - "System"
  selected: 0
  on_change: handler_theme_change
```

**text_input:**
```yaml
- type: text_input
  id: ssid_input
  placeholder: "Enter SSID..."
  max_length: 64
  keyboard: true               # show LVGL keyboard on focus
  on_submit: handler_ssid_submit
```

**chart:**
```yaml
- type: chart
  id: temp_chart
  chart_type: line             # line | bar | scatter
  point_count: 20
  series:
    - id: temp_series
      color: "#FF6B6B"
      name: "Temp"
```

**list:**
```yaml
- type: list
  id: device_list
  items: []                    # populated at runtime via backend API
  on_select: handler_device_selected
```

**component reference (custom or library):**
```yaml
- type: metric_card            # references component definition
  id: temp_card
  # props defined by the component's schema
  label: "Temperature"
  value_id: temp_value
  unit: "°C"
  icon: assets/icons/therm.png
```

### 3.4 Built-in layout types

**single_screen**: Default. Widgets positioned via align/offset.

**dashboard**: Grid layout. `layout_config.columns` and `layout_config.rows` divide screen
into cells. Widgets can span cells with `grid_col_span` and `grid_row_span` properties.

**settings_screen**: Scrollable list of sections and items. Widgets are `toggle_row`,
`slider_row`, `select_row`, `info_row`, `button_row`. App-level `sections:` key used instead
of `widgets:` when this layout is active.

**list_screen**: Full-screen scrollable list. Items added at runtime via backend API or
populated from a static `items:` list in YAML.

**tabbed**: Multiple named tabs, each containing their own widget list.
```yaml
layout: tabbed
tabs:
  - id: overview_tab
    label: "Overview"
    widgets: [...]
  - id: history_tab
    label: "History"
    widgets: [...]
```

**scrollable**: Single scrollable container with free-positioned widgets.

### 3.5 Assets section

```yaml
assets:
  fonts:
    - name: roboto_28
      src: assets/fonts/Roboto-Regular.ttf
      size: 28
      subset: "0x20-0x7E"       # ASCII range; keep flash usage minimal
    - name: roboto_16
      src: assets/fonts/Roboto-Regular.ttf
      size: 16
      subset: "0x20-0x7E,0x00B0"  # add degree symbol

  images:
    - src: assets/icons/sensor.png
      dither: false
    - src: assets/bg.png
      dither: true
      color_format: RGB565      # override default; useful for large images
```

If `assets:` is omitted, the generator uses the board's default theme fonts (from the
brookesia stylesheet) and auto-discovers all image paths referenced in the widget tree.

---

## 4. Hardware Abstraction Layer (HAL)

The HAL lives in `runtime/hal/` and provides two pure virtual interfaces. The generated
`main.cpp` instantiates the correct concrete implementations based on the board profile,
then passes them to the brookesia Phone setup.

### 4.1 ISSDisplay interface

```cpp
// runtime/hal/ss_display.hpp

class ISSDisplay {
public:
    virtual ~ISSDisplay() = default;

    // One-time hardware init: configure SPI/I2C/DSI bus, reset pin, init sequence.
    // Must register an lv_disp_t with LVGL before returning.
    virtual esp_err_t init() = 0;

    // Set backlight level 0.0–1.0 (PWM duty or GPIO on/off).
    virtual esp_err_t set_backlight(float level) = 0;

    // Return actual display width/height after rotation is applied.
    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;

    // Return the lv_disp_t registered with LVGL during init().
    virtual lv_disp_t* lv_display() const = 0;
};
```

### 4.2 ISSTouch interface

```cpp
// runtime/hal/ss_touch.hpp

struct SSTouchPoint {
    uint16_t x, y;
    uint8_t  id;       // finger/slot id for multi-touch
    bool     pressed;
};

class ISSTouch {
public:
    virtual ~ISSTouch() = default;

    // One-time hardware init: configure I2C/SPI bus, INT pin, reset pin.
    // Must register an lv_indev_t with LVGL before returning.
    virtual esp_err_t init() = 0;

    // Read all active touch points. Returns number of points populated.
    virtual uint8_t read(SSTouchPoint* points, uint8_t max_points) = 0;

    // Return the lv_indev_t registered with LVGL during init().
    virtual lv_indev_t* lv_indev() const = 0;
};
```

### 4.3 HAL init helper

```cpp
// runtime/hal/ss_hal.hpp

struct SSHalConfig {
    ISSDisplay* display;
    ISSTouch*   touch;          // may be nullptr for non-touch boards
    uint16_t    lvgl_buf_kb;    // LVGL draw buffer size; from board profile
};

// Called from generated main.cpp before brookesia Phone is created.
esp_err_t ss_hal_init(const SSHalConfig& config);
```

### 4.4 Display drivers

Each driver is a concrete `ISSDisplay` subclass. Drivers wrap the ESP-IDF `esp_lcd`
component APIs where available (ST7789, ILI9341, ST7701S DSI, GC9A01), falling back to
direct SPI/I2C transactions where not.

| Driver class | Interface | Chip | Boards |
|---|---|---|---|
| `SSDisplayST7789` | SPI | ST7789 | Various 240x240, 240x320 |
| `SSDisplayILI9341` | SPI | ILI9341 | 2.8" 240x320, telegraph board |
| `SSDisplayST7701SDSI` | MIPI DSI | ST7701S | ESP32-P4 4.3" 480x800 |
| `SSDisplayGC9A01` | SPI | GC9A01 | Round 240x240 |
| `SSDisplayST7796` | SPI | ST7796S | 4.0" 320x480 |
| `SSDisplaySSD1306` | I2C | SSD1306 | OLED 128x64 (no brookesia; basic only) |
| `SSDisplaySH8601` | QSPI | SH8601 | Waveshare C6 AMOLED |

Each driver's constructor takes a config struct specific to its wiring:

```cpp
struct SSDisplayST7789Config {
    spi_host_device_t spi_host;
    int pin_cs, pin_dc, pin_rst, pin_backlight;
    int width, height;
    uint8_t rotation;            // 0-3 (90° increments)
};

class SSDisplayST7789 : public ISSDisplay {
public:
    explicit SSDisplayST7789(const SSDisplayST7789Config& cfg);
    esp_err_t init() override;
    esp_err_t set_backlight(float level) override;
    uint16_t width() const override;
    uint16_t height() const override;
    lv_disp_t* lv_display() const override;
private:
    SSDisplayST7789Config cfg_;
    esp_lcd_panel_handle_t panel_ = nullptr;
    lv_disp_t* disp_ = nullptr;
};
```

### 4.5 Touch drivers

| Driver class | Interface | Chip | Notes |
|---|---|---|---|
| `SSTouchXPT2046` | SPI | XPT2046 | Resistive; needs calibration |
| `SSTouchFT6236` | I2C | FT6236/FT6336 | Capacitive; common on SPI display boards |
| `SSTouchGT911` | I2C | GT911 | Capacitive; multi-touch; ESP32-P4 board |
| `SSTouchCST816` | I2C | CST816S | Capacitive; Waveshare round boards |
| `SSTouchCST328` | I2C | CST328 | Capacitive; Waveshare 4" boards |

All drivers apply the display rotation transform in their `read()` method, so widget
coordinates in LVGL always match the displayed orientation regardless of physical wiring.

### 4.6 Resolution and orientation handling

The board profile specifies `display.orientation` (portrait/landscape/etc). The display
driver applies the corresponding LVGL rotation at init time. The effective `width()` and
`height()` returned by the driver reflect the post-rotation logical dimensions.

The codegen selects the brookesia stylesheet that most closely matches the logical
resolution. If an exact match exists (e.g. 480×800 dark), it is used. If not, the closest
smaller resolution stylesheet is used and a compile-time warning is emitted.

LVGL's percentage-based sizing (`LV_PCT()`) is used for all layout calculations in the
generated widget constructors, so layouts reflow correctly across resolutions. Fixed pixel
sizes are only used when explicitly specified in the YAML.

---

## 5. Runtime Library

### 5.1 SSAppBase

Thin subclass of `ESP_Brookesia_PhoneApp`. Generated app classes inherit from this, not
directly from the brookesia type.

```cpp
// runtime/core/ss_app_base.hpp

class SSAppBase : public ESP_Brookesia_PhoneApp {
public:
    explicit SSAppBase(const ESP_Brookesia_PhoneAppData_t& data);

protected:
    // Generated app implements this to create widgets.
    // Called by init() after the screen is created.
    virtual void buildUI(lv_obj_t* screen) = 0;

    // Widget registry — populated by generated buildUI() code.
    void registerWidget(const char* id, lv_obj_t* obj);

    // Retrieve a registered widget by id. Returns nullptr if not found.
    lv_obj_t* getWidget(const char* id) const;

    // Lifecycle — generated apps override these as needed.
    virtual void onInit()   {}
    virtual void onResume() {}
    virtual void onPause()  {}
    virtual void onClose()  {}

private:
    bool init()   final;   // calls buildUI() then onInit()
    bool run()    final;   // calls onResume()
    bool pause()  final;   // calls onPause()
    bool close()  final;   // calls onClose()
    bool back()   final;   // navigates back / requests app close

    std::unordered_map<std::string, lv_obj_t*> widget_map_;
};
```

### 5.2 SSWidgetFactory

Registry mapping type-name strings to builder functions. The generated `buildUI()` code
calls the factory for each widget in the YAML. Custom widget types are registered before
`app_main()` creates apps.

```cpp
// runtime/core/ss_widget_factory.hpp

using SSWidgetBuilderFn = std::function<lv_obj_t*(lv_obj_t* parent,
                                                   const SSWidgetConfig& config)>;

class SSWidgetFactory {
public:
    static SSWidgetFactory& instance();

    // Register a custom widget type. Call from register_custom_widgets().
    void registerType(const char* type_name, SSWidgetBuilderFn fn);

    // Called by generated buildUI() code.
    lv_obj_t* build(const char* type_name, lv_obj_t* parent,
                    const SSWidgetConfig& config) const;

private:
    std::unordered_map<std::string, SSWidgetBuilderFn> builders_;
};
```

`SSWidgetConfig` carries the per-widget YAML properties (id, align, offset, size, style,
and a key-value map of type-specific properties) as a lightweight struct populated from
generated code at compile time — no runtime YAML parsing.

Built-in types registered at startup: `label`, `button`, `image`, `slider`, `arc`,
`toggle`, `progress_bar`, `spinner`, `dropdown`, `text_input`, `chart`, `list`.

### 5.3 SSContext — Backend API

The central seam between generated widget code and user handler code.

```cpp
// runtime/core/ss_context.hpp

class SSContext {
public:
    static SSContext& instance();

    // --- Widget registration (called by generated code) ---
    void registerWidget(const std::string& id, lv_obj_t* obj,
                        SSWidgetType type);

    // --- Read widget values ---
    template<typename T>
    T get(const std::string& id) const;

    // --- Write widget values (triggers partial redraw of that widget) ---
    void set(const std::string& id, int32_t value);
    void set(const std::string& id, float value);
    void set(const std::string& id, const std::string& text);
    void set(const std::string& id, bool value);

    // --- Visibility ---
    void show(const std::string& id);
    void hide(const std::string& id);
    void toggle_visible(const std::string& id);

    // --- Event subscription ---
    using EventCallback = std::function<void(const SSEvent&)>;
    void on(const std::string& id, SSEventType event, EventCallback cb);
    void off(const std::string& id, SSEventType event);

    // --- Direct widget access (escape hatch for advanced LVGL use) ---
    lv_obj_t* raw(const std::string& id) const;

private:
    struct WidgetEntry {
        lv_obj_t*   obj;
        SSWidgetType type;
    };
    std::unordered_map<std::string, WidgetEntry>                   widgets_;
    std::unordered_map<std::string, std::vector<EventCallback>>    listeners_;
};
```

Specializations of `get<T>()` are provided for `int32_t`, `float`, `bool`, `std::string`.
The implementation reads the appropriate LVGL property based on the widget type (e.g.
`lv_slider_get_value()` for sliders, `lv_switch_is_checked()` for toggles).

`set()` dispatches to the correct LVGL setter and then marks only that widget's area as
dirty, triggering a partial redraw on the next LVGL tick rather than a full-screen refresh.

### 5.4 SSEvent and SSEventType

```cpp
enum class SSEventType {
    Click,
    LongPress,
    ValueChanged,
    Released,
    Focused,
    Defocused,
    Submit,         // text_input
    Selected,       // list, dropdown
};

struct SSEvent {
    std::string     widget_id;
    SSEventType     type;
    union {
        int32_t     int_value;
        float       float_value;
        bool        bool_value;
    };
    const char*     string_value = nullptr;
};
```

### 5.5 SSEventBus

Internal publish/subscribe. Generated widget constructors register LVGL event callbacks
that publish to the bus. SSContext's `on()` method subscribes to the bus. User handlers
(named in YAML) are also subscribed by generated code during `buildUI()`.

The bus runs on the LVGL task thread. Callbacks must not block. For heavy work, post to
a FreeRTOS queue from the callback.

### 5.6 SSSerialBridge

Optional FreeRTOS task that listens on UART0 for a simple binary protocol enabling
data-only hot reload and programmatic control from a host Python script without reflashing.

See section 8 for full protocol design.

---

## 6. Component Library

### 6.1 What a component is

A component is a reusable, parameterised widget composition with:
- A YAML definition in `components/` specifying its schema (accepted props) and its
  internal widget tree.
- A C++ implementation in `runtime/components/` that builds the widget tree.

The codegen resolves component references at build time — `type: metric_card` expands
to the component's internal widget tree, with props substituted. The resulting generated
C++ looks identical to what hand-written code would look like, with no runtime component
overhead.

### 6.2 Component YAML definition format

```yaml
# components/metric_card.yaml

component:
  name: metric_card
  description: "Single-value metric tile with label, large value, and unit"

  props:
    label:
      type: string
      required: true
    value_id:
      type: string
      required: true
      description: "SSContext id for the value display label"
    unit:
      type: string
      default: ""
    icon:
      type: image_path
      default: null
    color:
      type: color
      default: "#FFFFFF"
    bg_color:
      type: color
      default: "#1E1E2E"

  size: [LV_PCT(100), LV_PCT(100)]   # fills its grid cell by default

  template:
    - type: label
      id: "{{value_id}}"
      text: "--"
      align: center
      style:
        font: roboto_48_bold
        color: "{{color}}"
    - type: label
      id: "{{id}}_unit"
      text: "{{unit}}"
      align: center
      offset: [0, 32]
      style:
        font: roboto_16
        color: "{{color}}"
        opacity: 180
    - type: label
      id: "{{id}}_label"
      text: "{{label}}"
      align: bottom_mid
      offset: [0, -8]
      style:
        font: roboto_14
        color: "#888888"
```

### 6.3 Built-in component library

**Display components:**
- `metric_card` — large value + label + unit, for dashboards
- `stat_bar` — horizontal bar with label and percentage fill
- `icon_label` — icon + text side by side
- `badge` — small colored pill with text

**Input components:**
- `toggle_row` — label on left, lv_switch on right, for settings lists
- `slider_row` — label above, full-width slider below
- `select_row` — label on left, current selection + chevron on right (opens dropdown)
- `button_row` — full-width button with label, for action items in settings
- `info_row` — label on left, value text on right (read-only)
- `stepper_row` — label + minus button + value + plus button

**Navigation components:**
- `section_header` — styled section title for settings screens
- `breadcrumb` — path indicator for drill-down navigation

### 6.4 Built-in layout library

**`single_screen`**: Freeform. Widgets positioned with align + offset. Suitable for
simple displays or fully custom layouts.

**`dashboard`**: Grid of cells. `columns` and `rows` in `layout_config`. Each widget fills
one cell; `grid_col_span` / `grid_row_span` allow spanning. Cell gutters configurable via
`gap`.

**`settings_screen`**: Scrollable page. `sections:` list at app level. Each section has a
`title:` and `items:` list of settings rows. Standard iOS/Android-style settings chrome
applied automatically from the theme.

**`list_screen`**: Full-screen `lv_list`. Items can be populated from YAML or pushed at
runtime via `SSContext::instance().list_add("list_id", {text, icon, id})`. Supports
lazy population via `on_scroll_end` handler.

**`tabbed`**: Tab bar at top or bottom (configurable via `layout_config.tab_position`).
Each tab has its own widget list with independent scroll position.

**`scrollable`**: Single vertically scrollable container. Widgets positioned with align +
offset within the full scrollable height. Useful for long forms.

**`split`**: Two-panel layout (left/right or top/bottom). `layout_config.split: 30/70`
controls the ratio. Each panel contains its own widget list.

### 6.5 Extensibility

Custom components follow the same pattern:
1. Create `my_project/components/my_gauge/my_gauge.yaml` with `component:` definition.
2. Create `my_project/components/my_gauge/my_gauge.cpp` implementing `SSWidgetFactory`
   registration via `register_custom_widgets()`.
3. Reference as `type: my_gauge` in `screenschema.yaml`.

The codegen's `component_registry.py` searches for component YAML files in `components/`
relative to the project directory, then in the screenschema global `components/`. Local
definitions take precedence.

---

## 7. CLI Tool

### 7.1 Commands

```
screenschema build <yaml>             Build only; output to build/generated/
screenschema build <yaml> --out <dir> Build to specified directory
screenschema flash <yaml> -p <port>   Build then flash via idf.sh
screenschema watch <yaml> -p <port>   Build+flash on YAML/handler save; data bridge active
screenschema new <name>               Scaffold new project directory with stubs
screenschema validate <yaml>          Validate YAML only; no codegen
screenschema bridge -p <port>         Start serial bridge without rebuilding
screenschema boards                   List all known board profiles
screenschema components               List all built-in components and layouts
```

### 7.2 schema.py — YAML validation

Loads `screenschema.yaml` and validates it against `schema/screenschema.schema.json` using
the `jsonschema` library. Reports human-friendly errors with line numbers and suggestions
before touching codegen.

Also validates:
- All `id` values in the widget tree are unique within an app.
- All `on_*` handler names are valid C identifiers.
- All `type:` values resolve to either a built-in widget, a built-in component, or a
  component file in the search path.
- All asset paths exist relative to the YAML file's directory.
- The `board:` value resolves to a known board profile.

### 7.3 codegen.py — code generation

Walks the validated schema object and renders Jinja2 templates into the output directory.

**Generated files:**

| File | Template | Overwrite policy |
|---|---|---|
| `main/main.cpp` | `main_cpp.j2` | Always regenerated |
| `main/<AppId>.hpp` | `app_hpp.j2` | Always regenerated |
| `main/<AppId>.cpp` | `app_cpp.j2` | Always regenerated |
| `main/handlers.hpp` | `handlers_hpp.j2` | Always regenerated |
| `main/handlers.cpp` | `handlers_cpp.j2` | **Never if file exists** |
| `main/CMakeLists.txt` | `cmakelists.j2` | Always regenerated |
| `main/sdkconfig.defaults` | `sdkconfig.j2` | Always regenerated |

`handlers.cpp` preservation is the critical rule: this is user territory. If the YAML
adds a new handler that doesn't yet exist in `handlers.cpp`, the generator appends a
commented stub at the bottom of the file rather than overwriting.

**`main.cpp` generation responsibilities:**
- NVS flash init
- BSP / HAL init (display driver instantiation + touch driver instantiation from board
  profile)
- `ss_hal_init()` call
- LVGL task start
- `ESP_Brookesia_Phone` construction with the correct stylesheet macro
- `register_custom_widgets()` call (user-defined; no-op stub if not provided)
- `phone.installApp()` for each app in the YAML order
- `phone.begin()`

**`<AppId>.cpp` generation responsibilities:**
- Class constructor with `ESP_Brookesia_PhoneAppData_t` populated from YAML
  (name, icon, enable_default_screen, enable_recycle_resource)
- `buildUI(lv_obj_t* screen)` containing one `SSWidgetFactory::instance().build()` call
  per widget, followed by `registerWidget(id, obj)`
- `onInit()`, `onResume()`, `onPause()`, `onClose()` calling the named YAML handlers

### 7.4 board_registry.py

Loads all `.yaml` files in `boards/` at startup. Provides `BoardRegistry.get(id)` returning
a `BoardProfile` dataclass. Raises `UnknownBoardError` with a list of known board ids if
the id is not found.

### 7.5 component_registry.py

Resolves component type names to component definitions. Search order:
1. `<project_dir>/components/`
2. `<screenschema_install>/components/`

Returns a `ComponentDefinition` with the props schema and template widget tree, ready
for the codegen to expand inline.

### 7.6 watcher.py

Uses `watchdog` (Python library) to monitor:
- `screenschema.yaml`
- `handlers.cpp`
- `assets/` directory (recursive)
- Any imported component YAML files

On change, debounces 300ms then runs the full build+flash pipeline. Keeps the serial
bridge running between builds so data connections are not interrupted.

### 7.7 serial_bridge.py — DeviceBridge

```python
from screenschema import DeviceBridge

device = DeviceBridge("/dev/ttyACM0", baud=115200)
device.connect()

# Read widget values
brightness = device.get("brightness_slider")   # → int
wifi_on    = device.get("wifi_toggle")         # → bool
temp_text  = device.get("temp_label")          # → str

# Write widget values (no reflash needed)
device.set("temp_label", "23.5 °C")
device.set("progress_bar", 75)
device.set("wifi_toggle", True)

# Subscribe to events
device.on("refresh_btn", "click", lambda e: print("refresh clicked"))

# Batch update (sent in one serial transaction)
with device.batch():
    device.set("temp_label", "24.1 °C")
    device.set("humidity_label", "61 %")
    device.set("temp_arc", 72)

device.disconnect()
```

---

## 8. Hot Reload and Serial Bridge

### 8.1 Two modes of "hot reload"

**Structural hot reload** (layout/widget tree changes): Requires code regeneration and
reflash. `screenschema watch` automates this to a ~10-30 second round-trip depending on
board and change scope. Incremental build (ESP-IDF's CMake) minimizes rebuild time.

**Data hot reload** (widget values, text, visibility): No reflash. The `SSSerialBridge`
FreeRTOS task on the device accepts `set`/`get` commands over UART0, applies them via
`SSContext`, and returns responses. Round-trip latency is <5ms on USB-Serial.

### 8.2 Serial protocol

Binary framing over UART0 at 115200 baud (configurable in board profile):

```
Frame format:
┌──────┬──────┬──────────┬────────────┬──────┐
│ 0xAA │ CMD  │ LEN (2B) │ PAYLOAD    │ CRC  │
└──────┴──────┴──────────┴────────────┴──────┘

CMD values:
  0x01  SET_INT    id_str (null-term) + int32_t value
  0x02  SET_FLOAT  id_str + float value
  0x03  SET_STR    id_str + string value (null-term)
  0x04  SET_BOOL   id_str + uint8_t (0/1)
  0x10  GET        id_str
  0x11  GET_RESP   id_str + type_byte + value bytes
  0x20  EVENT      id_str + event_type_byte + value bytes (device→host)
  0x30  SHOW       id_str
  0x31  HIDE       id_str
  0xF0  PING
  0xF1  PONG
  0xFF  ERROR      error_code + message string
```

CRC is CRC-8/MAXIM over CMD+LEN+PAYLOAD.

The device bridge task runs at `tskIDLE_PRIORITY + 1`, yielding on every UART read.
It holds the LVGL mutex before calling SSContext::set() to ensure thread safety.

### 8.3 Enabling the serial bridge

Controlled by sdkconfig and by the board profile:

```yaml
# in board profile
serial_bridge:
  enabled: true
  uart: UART_NUM_0
  baud: 115200
```

If disabled, `SSSerialBridge` is not compiled in (via `#ifdef SS_SERIAL_BRIDGE_ENABLED`),
saving ~2KB flash and ~512B stack.

---

## 9. Asset Pipeline

### 9.1 Font subsetting

Fonts are converted from TTF to LVGL C arrays using `lv_font_conv` (Node.js tool wrapping
the LVGL font converter). The asset pipeline calls it automatically for each font defined
in the YAML `assets.fonts` section.

```python
# asset_pipeline.py
def convert_font(ttf_path, size, subset, output_name, output_dir):
    # Calls: npx lv_font_conv --font {ttf} --size {size}
    #        --range {subset} --format lvgl --output {output_dir}/{name}.c
```

Subset strings follow Unicode range notation: `"0x20-0x7E"` for printable ASCII,
`"0x20-0x7E,0x00B0,0x2103"` to add ° and ℃.

If `subset` is omitted, only the characters actually referenced in the YAML text values
are included (the pipeline scans all `text:` values and `label:` values in components).
This dramatically reduces font data size for displays showing sensor readings.

### 9.2 Image conversion

PNG images are converted to LVGL C arrays using the `lv_img_conv` Python utility:

```python
def convert_image(png_path, color_format, dither, output_dir):
    # color_format: RGB565 (default), RGB888, ARGB8888, A8 (for icons with alpha)
    # Outputs: {filename}.c with lv_img_dsc_t declaration
```

Large background images should use `color_format: RGB565` to halve memory usage.
Icons with transparency use `ARGB8888` or `A8` (alpha-only, for coloring via LVGL style).

All generated asset `.c` files go to `main/assets/`. The generated `CMakeLists.txt`
adds this directory to `SRCS`.

### 9.3 Asset caching

The pipeline hashes each source file. If the hash matches the previous build and the
output already exists, conversion is skipped. This keeps watch-mode rebuild times fast
for runs where only YAML (not assets) changed.

---

## 10. Memory and Performance Strategy

### 10.1 LVGL draw buffer

Sized per board in the board profile (`memory.lvgl_buf_kb`). The HAL init allocates the
buffer from PSRAM if available, falling back to internal SRAM. Recommended sizes:

| Board | PSRAM | Recommended buf |
|---|---|---|
| ESP32-P4 480×800 | 32MB | 100KB (full row ×2) |
| ESP32-S3 480×480 | 8MB | 80KB |
| ESP32-S3 240×320 | 8MB | 40KB |
| ESP32-C6 172×320 | none | 8KB (partial, ~1/8 screen) |

With partial buffers (<full frame), LVGL renders in strips. This is transparent to widget
code but means full-screen animations have higher CPU cost.

### 10.2 Partial redraws

LVGL's dirty-region system is always active. `SSContext::set()` calls `lv_obj_invalidate()`
on only the affected widget, not `lv_obj_invalidate(lv_scr_act())`. This means updating
a sensor reading label redraws only that label's bounding box.

For smooth animations (charts, arcs), call `lv_anim_start()` directly. The `SSAppBase`
resource tracker will clean up animations on app close if `enable_recycle_resource` is set.

### 10.3 Font subsetting

See section 9.1. A typical full ASCII font at 28px is ~15KB. Subsetting to digits + units
(e.g. `"0x30-0x39,0x20,0x25,0x2E,0x00B0,0x43,0x46"` for `0-9 . % ° C F`) brings it
to ~3KB.

The asset pipeline automatically warns when a font conversion would produce a file
larger than a configurable threshold (default 30KB per font face).

### 10.4 PSRAM placement

For boards with PSRAM, the generated `sdkconfig.defaults` enables:
```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_LV_MEM_CUSTOM=y
CONFIG_LV_MEM_SIZE_KILOBYTES=512
```

For boards without PSRAM (e.g. ESP32-C6), a lean config is applied:
```
CONFIG_LV_MEM_SIZE_KILOBYTES=64
```
and the codegen limits the widget tree depth with a compile-time warning if the YAML
defines more than 20 widgets per screen.

---

## 11. Board Profiles

Board profiles live in `boards/` as YAML files. The board id in `screenschema.yaml` must
match a filename (without extension).

### 11.1 Full profile schema

```yaml
id: esp32p4-jc4880p443c
description: "Jingcai 4.3\" ESP32-P4 480x800 capacitive touch"
chip: esp32p4

display:
  width: 480
  height: 800
  driver: st7701s_dsi           # maps to SSDisplayST7701SDSI
  interface: mipi_dsi
  orientation: portrait         # portrait | landscape | portrait_inverted | landscape_inverted
  color_depth: 24               # bits per pixel
  backlight_gpio: -1            # -1 if PWM-controlled outside GPIO
  swap_xy: false
  mirror_x: false
  mirror_y: false

touch:
  driver: gt911                 # maps to SSTouchGT911
  interface: i2c
  sda: 7
  scl: 8
  rst_gpio: -1
  int_gpio: -1
  swap_xy: false
  mirror_x: false
  mirror_y: false

memory:
  flash_mb: 16
  psram_mb: 32
  lvgl_buf_kb: 100

brookesia:
  stylesheet: ESP_BROOKESIA_PHONE_480_800_DARK_STYLESHEET
  # If no exact stylesheet exists for this resolution, codegen warns and uses closest.

serial_bridge:
  enabled: true
  uart: UART_NUM_0
  baud: 115200

sdkconfig_defaults: sdkconfig.defaults.esp32p4
```

### 11.2 Initial board support matrix

| Profile id | Chip | Display | Touch | Resolution |
|---|---|---|---|---|
| `esp32p4-jc4880p443c` | ESP32-P4 | ST7701S (MIPI DSI) | GT911 | 480×800 |
| `esp32s3-4848s040c` | ESP32-S3 | GC9A01 (SPI) | FT6236 | 480×480 round |
| `esp32s3-es3c28p` | ESP32-S3 | ILI9341 (SPI) | XPT2046 | 240×320 |
| `esp32s3-wroom` | ESP32-S3 | — (no built-in display) | — | configurable |
| `esp32c6-waveshare-lcd` | ESP32-C6 | ST7789 (SPI) | — | 172×320 |

---

## 12. Backend API

### 12.1 On-device C++ API

Available in any handler or FreeRTOS task. Thread-safe; internally holds LVGL mutex
before widget operations.

```cpp
#include "ss_context.hpp"

auto& ss = SSContext::instance();

// --- Reading values ---
int32_t     brightness = ss.get<int32_t>("brightness_slider");
float       temp       = ss.get<float>("temp_arc");
bool        wifi_on    = ss.get<bool>("wifi_toggle");
std::string status     = ss.get<std::string>("status_label");

// --- Writing values (partial redraw) ---
ss.set("temp_label",     "23.5 °C");
ss.set("progress_bar",   75);
ss.set("temp_arc",       72.3f);
ss.set("wifi_toggle",    true);

// --- Visibility ---
ss.show("error_banner");
ss.hide("loading_spinner");

// --- Events ---
ss.on("refresh_btn", SSEventType::Click, [](const SSEvent& e) {
    esp_event_post(APP_EVENTS, EVT_REFRESH, nullptr, 0, 0);
});

// --- Direct lv_obj_t access (escape hatch) ---
lv_obj_t* chart = ss.raw("temp_chart");
lv_chart_set_next_value(chart, series_handle, new_reading);

// --- List operations ---
ss.list_clear("device_list");
ss.list_add("device_list", {.text = "Sensor A", .icon = nullptr, .id = "sensor_a"});

// --- Batch update (single LVGL flush at end) ---
{
    SSBatch batch(ss);
    ss.set("temp_label",     "24.1 °C");
    ss.set("humidity_label", "61 %");
    ss.set("temp_arc",       75);
}   // flush happens here
```

### 12.2 Host Python API (via serial bridge)

```python
from screenschema import DeviceBridge, SSEventType

device = DeviceBridge("/dev/ttyACM0", baud=115200)
device.connect()

# Read
print(device.get("brightness_slider"))   # 80
print(device.get("wifi_toggle"))         # False

# Write
device.set("temp_label", "25.0 °C")
device.set("progress_bar", 90)

# Show/hide
device.show("error_banner")
device.hide("loading_spinner")

# Events (non-blocking; handled in background thread)
device.on("refresh_btn", SSEventType.Click, lambda e: print("refresh!"))

# Batch
with device.batch():
    device.set("temp_label", "26.0 °C")
    device.set("humidity_label", "58 %")

# Wait for events (blocking)
device.run_forever()   # or device.run_until(condition_fn)

device.disconnect()
```

---

## 13. Project Directory Structure

```
screenschema/
│
├── ImplementationPlan.md          ← this document
├── README.md
├── CHANGELOG.md
│
├── cli/                           ← Python host tool
│   ├── __init__.py
│   ├── __main__.py                ← entry: python -m screenschema <cmd>
│   ├── main.py                    ← argparse command dispatch
│   ├── schema.py                  ← YAML loading + jsonschema validation
│   ├── codegen.py                 ← core: schema → C++ via Jinja2
│   ├── asset_pipeline.py          ← font subsetting + image conversion
│   ├── board_registry.py          ← board profile loader
│   ├── component_registry.py      ← component definition resolver
│   ├── watcher.py                 ← watchdog-based file watcher
│   ├── serial_bridge.py           ← DeviceBridge Python class
│   ├── requirements.txt           ← pyyaml jinja2 jsonschema pillow watchdog pyserial
│   └── templates/
│       ├── main_cpp.j2            ← app_main() + HAL + brookesia setup
│       ├── app_hpp.j2             ← generated PhoneApp subclass header
│       ├── app_cpp.j2             ← generated PhoneApp subclass impl
│       ├── handlers_hpp.j2        ← forward declarations for all handlers
│       ├── handlers_cpp.j2        ← weak stubs (never overwrites existing file)
│       └── cmakelists.j2          ← project CMakeLists.txt
│
├── boards/                        ← board profiles
│   ├── esp32p4-jc4880p443c.yaml
│   ├── esp32s3-4848s040c.yaml
│   ├── esp32s3-es3c28p.yaml
│   ├── esp32s3-wroom.yaml
│   └── esp32c6-waveshare-lcd.yaml
│
├── components/                    ← reusable YAML component definitions
│   ├── metric_card.yaml
│   ├── stat_bar.yaml
│   ├── toggle_row.yaml
│   ├── slider_row.yaml
│   ├── select_row.yaml
│   ├── button_row.yaml
│   ├── info_row.yaml
│   ├── stepper_row.yaml
│   ├── section_header.yaml
│   └── icon_label.yaml
│
├── layouts/                       ← standard screen layout definitions
│   ├── single_screen.yaml
│   ├── dashboard.yaml
│   ├── settings_screen.yaml
│   ├── list_screen.yaml
│   ├── tabbed.yaml
│   ├── scrollable.yaml
│   └── split.yaml
│
├── schema/                        ← JSON Schema for validation
│   ├── screenschema.schema.json   ← top-level schema
│   ├── board.schema.json          ← board profile schema
│   └── component.schema.json      ← component definition schema
│
├── runtime/                       ← C++ component; included in every generated project
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   │
│   ├── hal/
│   │   ├── ss_hal.hpp/cpp         ← combined HAL init
│   │   ├── ss_display.hpp         ← ISSDisplay pure virtual interface
│   │   ├── ss_touch.hpp           ← ISSTouch pure virtual interface
│   │   └── drivers/
│   │       ├── display/
│   │       │   ├── st7789.hpp/cpp
│   │       │   ├── ili9341.hpp/cpp
│   │       │   ├── st7701s_dsi.hpp/cpp
│   │       │   ├── gc9a01.hpp/cpp
│   │       │   ├── st7796.hpp/cpp
│   │       │   └── sh8601.hpp/cpp
│   │       └── touch/
│   │           ├── xpt2046.hpp/cpp
│   │           ├── ft6236.hpp/cpp
│   │           ├── gt911.hpp/cpp
│   │           ├── cst816.hpp/cpp
│   │           └── cst328.hpp/cpp
│   │
│   ├── core/
│   │   ├── ss_app_base.hpp/cpp
│   │   ├── ss_widget_factory.hpp/cpp
│   │   ├── ss_widget_config.hpp   ← SSWidgetConfig struct
│   │   ├── ss_context.hpp/cpp     ← backend API singleton
│   │   ├── ss_event_bus.hpp/cpp
│   │   └── ss_serial_bridge.hpp/cpp
│   │
│   └── components/                ← C++ implementations of YAML components
│       ├── ss_metric_card.hpp/cpp
│       ├── ss_stat_bar.hpp/cpp
│       ├── ss_toggle_row.hpp/cpp
│       ├── ss_slider_row.hpp/cpp
│       ├── ss_select_row.hpp/cpp
│       └── ss_section_header.hpp/cpp
│
└── examples/
    ├── hello_world/
    │   ├── screenschema.yaml
    │   └── handlers.cpp
    ├── sensor_dashboard/
    │   ├── screenschema.yaml
    │   ├── handlers.cpp
    │   └── assets/
    │       └── icons/
    ├── settings_demo/
    │   ├── screenschema.yaml
    │   └── handlers.cpp
    └── multi_board/               ← same handlers.cpp, different board: in screenschema.yaml
        ├── screenschema.p4.yaml
        ├── screenschema.s3.yaml
        └── handlers.cpp
```

---

## 14. Phased Implementation Plan

### Phase 1 — Walking skeleton

**Goal**: Prove the full pipeline end-to-end. One board, two apps, label + button widgets,
no components, no assets, no serial bridge.

**Deliverable**: `screenschema build examples/hello_world/screenschema.yaml` produces an
ESP-IDF project that compiles, flashes, and runs on the ESP32-P4 board, showing a
brookesia phone launcher with two apps containing labels and a button.

Tasks:
1. Create `boards/esp32p4-jc4880p443c.yaml` board profile.
2. Write `runtime/hal/ss_display.hpp` + `ss_touch.hpp` interfaces.
3. Write `runtime/hal/drivers/display/st7701s_dsi.hpp/cpp` (wraps esp_lcd_panel DSI API).
4. Write `runtime/hal/drivers/touch/gt911.hpp/cpp` (wraps esp_lcd_touch_gt911 component).
5. Write `runtime/hal/ss_hal.hpp/cpp` init helper.
6. Write `runtime/core/ss_app_base.hpp/cpp`.
7. Write `runtime/core/ss_widget_factory.hpp/cpp` with label + button types.
8. Write `runtime/core/ss_context.hpp/cpp` with `set(string)`, `get<string>()`, `on()`.
9. Write `runtime/CMakeLists.txt`.
10. Write `cli/schema.py` — load + minimal validation.
11. Write `cli/board_registry.py`.
12. Write `cli/codegen.py` — main.cpp, one app class, CMakeLists.txt.
13. Write Jinja2 templates: `main_cpp.j2`, `app_hpp.j2`, `app_cpp.j2`, `cmakelists.j2`.
14. Write `cli/main.py` with `build` and `flash` commands.
15. Write `examples/hello_world/screenschema.yaml`.
16. End-to-end test: build → flash → verify on hardware.

### Phase 2 — Full widget set + second board + asset pipeline

**Goal**: All standard widgets work. Asset pipeline converts fonts and images. Second board
(ESP32-S3 round display) supported. `screenschema validate` command. Component library
started with `metric_card` and settings row types.

Tasks:
1. Expand `SSWidgetFactory` to all standard types: `image`, `toggle`, `slider`, `arc`,
   `progress_bar`, `spinner`, `dropdown`, `text_input`, `chart`, `list`.
2. Implement `cli/asset_pipeline.py` for PNG→LVGL and font subsetting.
3. Implement `cli/component_registry.py` and component expansion in codegen.
4. Write `components/metric_card.yaml` + `runtime/components/ss_metric_card.hpp/cpp`.
5. Write settings row components: `toggle_row`, `slider_row`, `select_row`, `info_row`.
6. Write `layouts/dashboard.yaml` + `layouts/settings_screen.yaml` implementations.
7. Add `boards/esp32s3-4848s040c.yaml` (480×480 round).
8. Write `runtime/hal/drivers/display/gc9a01.hpp/cpp`.
9. Write `runtime/hal/drivers/touch/ft6236.hpp/cpp`.
10. Add `screenschema validate` command with rich error output.
11. Write `schema/screenschema.schema.json` formally.
12. Write `examples/sensor_dashboard/` end-to-end.
13. Write `examples/settings_demo/` end-to-end.
14. Test multi-board: same YAML targets P4 and S3 round with board field changed.

### Phase 3 — Hot reload + serial bridge + watch mode

**Goal**: `screenschema watch` auto-rebuilds on save. `DeviceBridge` Python class
enables data-only updates without reflash from host.

Tasks:
1. Implement `runtime/core/ss_serial_bridge.hpp/cpp` FreeRTOS task + binary protocol.
2. Implement `cli/serial_bridge.py` with `DeviceBridge` class.
3. Implement `cli/watcher.py` using `watchdog`.
4. Add `screenschema watch` and `screenschema bridge` commands.
5. Add `SSBatch` batch-update helper to SSContext.
6. Document and test `screenschema watch` round-trip time on P4 board.
7. Add `examples/sensor_dashboard/simulate.py` — pushes mock sensor data over serial.

### Phase 4 — Memory optimisation + additional boards + tabbed/list layouts

**Goal**: Verified low-memory operation on ESP32-C6. Full layout library. Font subsetting
applied automatically from scanned text values.

Tasks:
1. Add `boards/esp32c6-waveshare-lcd.yaml`.
2. Write `runtime/hal/drivers/display/st7789.hpp/cpp`.
3. Implement auto-subsetting: scan all YAML text values to build minimum character set.
4. Add `layouts/tabbed.yaml`, `layouts/list_screen.yaml`, `layouts/scrollable.yaml`.
5. Implement `SSContext::list_add()`, `list_clear()`, `list_set_items()`.
6. Validate memory usage on C6 (no PSRAM); ensure widget budget warnings fire.
7. Add `boards/esp32s3-es3c28p.yaml` (2.8" SPI ILI9341 + XPT2046).
8. Write `runtime/hal/drivers/display/ili9341.hpp/cpp`.
9. Write `runtime/hal/drivers/touch/xpt2046.hpp/cpp` with calibration support.

### Phase 5 — Polish, extensibility docs, screenschema new scaffolding

**Goal**: `screenschema new` scaffolds a complete project. Extensibility model documented
with worked examples. README covers all commands.

Tasks:
1. Implement `screenschema new <name>` scaffolding command.
2. Write extensibility guide: custom widget types + custom layouts.
3. Write `examples/multi_board/` showing same handlers.cpp targeting two boards.
4. Write `examples/custom_component/` showing full custom component workflow.
5. Write final README covering installation, quickstart, all commands, YAML reference.
6. Add `screenschema boards` and `screenschema components` info commands.
7. Add `CHANGELOG.md` and semantic versioning.

---

## 15. Testing Strategy

### 15.1 CLI tests (pytest)

Unit test the Python tool in isolation using fixture YAML files and snapshot testing on
generated C++ output:

```
tests/
├── fixtures/
│   ├── hello_world.yaml
│   ├── sensor_dashboard.yaml
│   └── invalid_*.yaml          ← validation failure cases
├── test_schema.py              ← validation accept/reject cases
├── test_codegen.py             ← snapshot tests on generated .cpp/.hpp
├── test_board_registry.py
├── test_component_registry.py
└── test_asset_pipeline.py      ← mocked lv_font_conv and img2c calls
```

### 15.2 Runtime unit tests (host build)

The runtime library can be built for Linux/macOS host using LVGL's simulator. Tests verify
SSContext get/set/on semantics, SSWidgetFactory registration, and SSEventBus dispatch
without requiring hardware.

### 15.3 Hardware integration tests

Manual smoke tests run on each supported board after each phase:
- App launcher appears with correct icons and names.
- All widget types render without corruption.
- Touch input registers correctly.
- `SSContext::set()` updates widget visually.
- Serial bridge `set`/`get` round-trip functions.
- Memory usage (logged at boot) stays within board profile budget.

### 15.4 Multi-board regression

The `examples/multi_board/` example is the regression target: the same `handlers.cpp`
must compile and run correctly when pointed at each supported board profile.
