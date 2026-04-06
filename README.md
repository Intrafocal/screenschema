# ScreenSchema

YAML-driven UI framework for ESP32 devices. Define your apps, widgets, and device configuration in a single YAML file — ScreenSchema generates the ESP-IDF C++ project and bundles the runtime.

## Requirements

- Python 3.10+
- [ESP-IDF v5.4](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/)

## Installation

```bash
pip install screenschema
```

Or install from source (editable):

```bash
git clone https://github.com/intrafocal/screenschema.git
pip install -e screenschema/
```

## Quick start

**1. Write a schema file** (`screenschema.yaml`):

```yaml
schema_version: "1.0"
firmware_version: "1.0.0"

board: esp32s3-es3c28p

device:
  id: "my-hub"
  location: "living-room"
  friendly_name: "Living Room Hub"

shell:
  theme: dark
  orientation: portrait
  navigation: gesture

apps:
  - id: hello_app
    name: "Hello"
    layout: single_screen
    widgets:
      - type: label
        id: greeting_label
        text: "Hello!"
        align: center
        style:
          color: "#FFFFFF"
      - type: button
        id: tap_btn
        label: "Tap Me"
        align: center
        offset: [0, 80]
        on_click: handler_tap
    on_init: handler_hello_init
```

**2. Generate the ESP-IDF project:**

```bash
screenschema build screenschema.yaml
# → generates project at build/generated/
```

**3. Implement your handlers** (`build/generated/main/handlers.cpp`):

ScreenSchema generates stubs for every handler declared in the YAML. Fill them in:

```cpp
#include "handlers.hpp"

void handler_hello_init(const SSEvent& e) {
    // Called when hello_app starts
}

void handler_tap(const SSEvent& e) {
    SSContext::set("greeting_label", "You tapped it!");
}
```

**4. Build and flash:**

```bash
cd build/generated
./idf.sh build flash -p /dev/ttyACM0
```

## Commands

```
screenschema build <yaml>            Generate ESP-IDF project
screenschema validate <yaml>         Validate schema without generating
screenschema boards                  List available board profiles
```

## Supported boards

| Board ID | Description |
|---|---|
| `esp32s3-es3c28p` | ESP32-S3 2.8" 240×320 touch display |
| `esp32s3-4848s040c` | ESP32-S3 4" 480×480 touch display |
| `esp32s3-wroom` | ESP32-S3-WROOM devkit (external display) |
| `esp32c6-waveshare-lcd` | ESP32-C6 Waveshare LCD |
| `esp32p4-jc4880p443c` | ESP32-P4 4.4" 480×800 DSI display |

## Schema reference

### Top-level keys

| Key | Required | Description |
|---|---|---|
| `schema_version` | yes | Always `"1.0"` |
| `board` | yes | Board profile ID (see above) |
| `firmware_version` | no | Semver string, baked into firmware |
| `device` | no | Device identity (id, location, friendly_name) |
| `shell` | no | UI shell options |
| `network` | no | HTTP endpoints, heartbeat, WebSocket |
| `server` | no | Embedded HTTP API server |
| `apps` | yes | List of app definitions |

### Shell options

```yaml
shell:
  theme: dark          # dark | light
  orientation: portrait  # portrait | landscape
  status_bar: true
  navigation: gesture  # gesture | button
  system_app:
    wifi: true         # include WiFi settings
    ota: true          # include OTA update trigger
```

### Network

```yaml
network:
  # IoT Gateway — device registers with PSK, gets JWT + runtime config
  gateway:
    url: "http://gateway.example.local:8888"
    secret: "<your-device-psk>"   # per-device PSK, written to NVS on first flash

  endpoints:
    gateway:
      base_url: "http://gateway.example.local:8888/api/gateway"
      timeout_ms: 5000
      retry: 2
  heartbeat:
    endpoint: gateway
    path: /devices/heartbeat
    interval_s: 30
  websocket:
    url: "ws://gateway.example.local:8888/ws"
    reconnect_s: 5
```

When `network.gateway` is configured, the device registers at boot and receives a JWT used for all subsequent requests. The gateway also returns runtime URLs (WebSocket, OTA, heartbeat) which override the static config above. The gateway is expected to expose `/api/register` (PSK → JWT) and forward authenticated requests to backend services.

Named endpoints are accessible in handlers via `SSHttpClient`:

```cpp
SSHttpClient::instance().post("gateway", "/events", payload, [](bool ok, cJSON* resp) {
    if (resp) cJSON_Delete(resp);
});
```

### Embedded HTTP server

```yaml
server:
  enabled: true
  port: 80
  mdns: true  # advertises <device-id>._screenschema._tcp.local
```

When enabled, the device exposes:

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/status` | Device info, WiFi RSSI, free heap |
| `POST` | `/api/widget/set` | `{"id":"...", "value":"..."}` |
| `POST` | `/api/widget/set/batch` | `{"updates":[...]}` |
| `GET` | `/api/widget/get?id=...` | Read a widget value |
| `POST` | `/api/widget/show` | `{"id":"..."}` |
| `POST` | `/api/widget/hide` | `{"id":"..."}` |
| `POST` | `/api/apps/launch` | `{"id":"<schema-app-id>"}` |
| `POST` | `/api/notify` | `{"title":"...","message":"...","duration_ms":4000}` |
| `POST` | `/api/ota/update` | `{"url":"http://..."}` — triggers OTA |

### Widget types

| Type | Key props |
|---|---|
| `label` | `text`, `align`, `offset`, `style.color` |
| `button` | `label`, `align`, `offset`, `on_click` |
| `dropdown` | `options`, `on_change` |
| `text_input` | `placeholder`, `on_submit` |
| `toggle` | `on_change` |
| `slider` | `min`, `max`, `on_change` |

### Handlers

Any `on_*` value in the YAML becomes a C++ function stub:

```yaml
on_click: handler_tap       # button
on_change: handler_select   # dropdown / toggle / slider
on_submit: handler_submit   # text_input
on_init: handler_app_init   # app lifecycle
on_resume: handler_app_resume
on_pause: handler_app_pause
```

All handlers receive `const SSEvent& e` and run on the LVGL task.

## Using from another project

Add to your `pyproject.toml`:

```toml
dependencies = [
    "screenschema @ git+https://github.com/intrafocal/screenschema.git@v0.2.0",
]
```

Or pin to a specific commit:

```toml
"screenschema @ git+https://github.com/intrafocal/screenschema.git@<sha>",
```

The generated `CMakeLists.txt` automatically points ESP-IDF at the bundled runtime — no extra configuration needed.
