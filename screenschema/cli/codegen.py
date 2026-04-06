import pathlib
from typing import Dict, Any
from jinja2 import Environment, FileSystemLoader
from .asset_pipeline import process_icon


def generate(schema: Dict[str, Any], board: Dict[str, Any], out_dir: pathlib.Path, project_dir: pathlib.Path):
    """Generate ESP-IDF project files from schema + board profile."""

    templates_dir = pathlib.Path(__file__).parent / "templates"
    def snake_to_pascal(s: str) -> str:
        return "".join(w.capitalize() for w in s.split("_"))

    env = Environment(
        loader=FileSystemLoader(str(templates_dir)),
        trim_blocks=True,
        lstrip_blocks=True,
    )
    env.filters["snake_to_pascal"] = snake_to_pascal

    # Prepare context for templates
    ctx = _build_context(schema, board, project_dir)

    main_dir = out_dir / "main"
    main_dir.mkdir(parents=True, exist_ok=True)

    # Process app launcher icons
    # SPI displays use LV_COLOR_16_SWAP=1 — image bytes must be big-endian
    byte_swap = ctx["display_interface"] == "spi"
    icon_srcs = []  # generated .c filenames for CMakeLists
    for app in ctx["apps"]:
        if app["icon"]:
            var_name = f"icon_{app['id']}"
            result = process_icon(app["icon"], var_name, project_dir, main_dir,
                                  byte_swap=byte_swap)
            if result:
                app["icon_var"] = var_name
                icon_srcs.append(result)
            else:
                app["icon_var"] = None
        else:
            app["icon_var"] = None
    ctx["icon_srcs"] = icon_srcs

    # Always regenerated
    _render(env, "main_cpp.j2", ctx, main_dir / "main.cpp")
    _render(env, "cmakelists.j2", ctx, out_dir / "CMakeLists.txt")
    _render(env, "main_cmakelists.j2", ctx, main_dir / "CMakeLists.txt")
    _render(env, "handlers_hpp.j2", ctx, main_dir / "handlers.hpp")

    for app in ctx["apps"]:
        app_ctx = {**ctx, "app": app}
        _render(env, "app_hpp.j2", app_ctx, main_dir / f"{app['class_name']}.hpp")
        _render(env, "app_cpp.j2", app_ctx, main_dir / f"{app['class_name']}.cpp")

    # handlers.cpp: never overwrite if exists
    handlers_cpp = main_dir / "handlers.cpp"
    if not handlers_cpp.exists():
        _render(env, "handlers_cpp.j2", ctx, handlers_cpp)
    else:
        # Append stubs for any new handlers not already in the file
        _append_missing_handlers(ctx, handlers_cpp)

    # Write sdkconfig.defaults and partitions.csv
    _write_sdkconfig(schema, board, out_dir)
    _write_partitions(board, out_dir)

    print(f"Generated {len(ctx['apps'])} app(s) to {out_dir}")


def generate_sim(schema: Dict[str, Any], board: Dict[str, Any], out_dir: pathlib.Path, project_dir: pathlib.Path):
    """Generate desktop sim files alongside the normal ESP-IDF project."""

    templates_dir = pathlib.Path(__file__).parent / "templates"
    def snake_to_pascal(s: str) -> str:
        return "".join(w.capitalize() for w in s.split("_"))

    env = Environment(
        loader=FileSystemLoader(str(templates_dir)),
        trim_blocks=True,
        lstrip_blocks=True,
    )
    env.filters["snake_to_pascal"] = snake_to_pascal

    ctx = _build_context(schema, board, project_dir)

    # Process icons (same as normal build)
    main_dir = out_dir / "main"
    main_dir.mkdir(parents=True, exist_ok=True)
    icon_srcs = []
    for app in ctx["apps"]:
        if app["icon"]:
            var_name = f"icon_{app['id']}"
            result = process_icon(app["icon"], var_name, project_dir, main_dir)
            if result:
                app["icon_var"] = var_name
                icon_srcs.append(result)
            else:
                app["icon_var"] = None
        else:
            app["icon_var"] = None
    ctx["icon_srcs"] = icon_srcs

    # Resolve paths for the sim CMakeLists
    sim_dir = str(pathlib.Path(__file__).parent.parent / "sim")
    runtime_dir = str(pathlib.Path(__file__).parent.parent / "runtime")
    ctx["sim_dir"] = sim_dir
    ctx["runtime_dir"] = runtime_dir

    # Generate app code (same as normal build)
    _render(env, "handlers_hpp.j2", ctx, main_dir / "handlers.hpp")
    for app in ctx["apps"]:
        app_ctx = {**ctx, "app": app}
        _render(env, "app_hpp.j2", app_ctx, main_dir / f"{app['class_name']}.hpp")
        _render(env, "app_cpp.j2", app_ctx, main_dir / f"{app['class_name']}.cpp")

    # handlers.cpp: never overwrite if exists
    handlers_cpp = main_dir / "handlers.cpp"
    if not handlers_cpp.exists():
        _render(env, "handlers_cpp.j2", ctx, handlers_cpp)
    else:
        _append_missing_handlers(ctx, handlers_cpp)

    # Sim-specific files
    _render(env, "sim_main.j2", ctx, out_dir / "sim_main.cpp")
    _render(env, "sim_cmakelists.j2", ctx, out_dir / "CMakeLists.txt")

    print(f"Generated sim for {len(ctx['apps'])} app(s) to {out_dir}")


def _render(env, template_name: str, ctx: dict, out_path: pathlib.Path):
    tmpl = env.get_template(template_name)
    out_path.write_text(tmpl.render(**ctx))


def _build_context(schema: dict, board: dict, project_dir: pathlib.Path) -> dict:
    apps = []
    all_handlers = set()

    for app_def in schema.get("apps", []):
        app_id = app_def["id"]
        # Convert snake_case id to PascalCase class name; ensure it ends in "App" exactly once
        pascal = "".join(w.capitalize() for w in app_id.split("_"))
        class_name = pascal if pascal.endswith("App") else pascal + "App"

        widgets = []
        for w in app_def.get("widgets", []):
            widget = dict(w)
            # Collect all handler names
            for key in ("on_click", "on_long_press", "on_change", "on_release", "on_submit", "on_select"):
                if key in widget and widget[key]:
                    all_handlers.add(widget[key])
            widgets.append(widget)

        # Collect app-level handlers
        for key in ("on_init", "on_resume", "on_pause", "on_close"):
            if key in app_def and app_def[key]:
                all_handlers.add(app_def[key])

        apps.append({
            "id": app_id,
            "class_name": class_name,
            "name": app_def.get("name", app_id),
            "icon": app_def.get("icon", None),
            "layout": app_def.get("layout", "single_screen"),
            "widgets": widgets,
            "on_init": app_def.get("on_init"),
            "on_resume": app_def.get("on_resume"),
            "on_pause": app_def.get("on_pause"),
            "on_close": app_def.get("on_close"),
        })

    # Board-derived values
    display = board.get("display", {})
    touch = board.get("touch", {})
    audio = board.get("audio", {})
    memory = board.get("memory", {})
    brookesia = board.get("brookesia", {})
    shell = schema.get("shell", {})
    wifi = board.get("wifi", {})
    early_init = board.get("early_init", {})
    keyboard = board.get("keyboard", {})
    trackball = board.get("trackball", {})
    battery = board.get("battery", {})

    # system_app: true | false | {wifi: true, ota: true}
    sys_app_raw = shell.get("system_app", False)
    if isinstance(sys_app_raw, dict):
        system_app_enabled = True
        system_app_wifi = sys_app_raw.get("wifi", True)
        system_app_ota  = sys_app_raw.get("ota", True)
    else:
        system_app_enabled = bool(sys_app_raw)
        system_app_wifi = system_app_enabled
        system_app_ota  = system_app_enabled

    # Gesture navigation: enabled if schema says gesture OR board explicitly enables it
    gesture_nav_back = (shell.get("navigation") == "gesture" or
                        brookesia.get("gesture_navigation_back", False))

    # Device identity (R4.1)
    device = schema.get("device", {})
    device_id            = device.get("id", "")
    device_location      = device.get("location", "")
    device_friendly_name = device.get("friendly_name", "")

    # Network endpoints (R1.1)
    network_endpoints_raw = schema.get("network", {}).get("endpoints", {})
    network_endpoints = [
        {
            "name":       name,
            "base_url":   cfg.get("base_url", ""),
            "timeout_ms": cfg.get("timeout_ms", 5000),
            "retry":      cfg.get("retry", 1),
            "headers":    cfg.get("headers", {}),
        }
        for name, cfg in network_endpoints_raw.items()
    ]

    # Device server (R3.1)
    server = schema.get("server", {})
    server_enabled = server.get("enabled", False)
    server_port    = server.get("port", 80)
    server_mdns    = server.get("mdns", server_enabled)  # default: on when server is on

    # WebSocket (R1.2)
    ws_raw         = schema.get("network", {}).get("websocket", {})
    ws_enabled     = bool(ws_raw.get("url", "")) if isinstance(ws_raw, dict) else False
    ws_url         = ws_raw.get("url", "") if isinstance(ws_raw, dict) else ""
    ws_reconnect_s = int(ws_raw.get("reconnect_s", 5)) if isinstance(ws_raw, dict) else 5

    # IoT Gateway (device registration + reverse proxy)
    gw_raw          = schema.get("network", {}).get("gateway", {})
    gw_enabled      = bool(gw_raw.get("url", "")) if isinstance(gw_raw, dict) else False
    gw_url          = gw_raw.get("url", "") if isinstance(gw_raw, dict) else ""
    gw_secret       = gw_raw.get("secret", "") if isinstance(gw_raw, dict) else ""

    # Heartbeat (R4.3)
    heartbeat_raw      = schema.get("network", {}).get("heartbeat", {})
    heartbeat_enabled  = bool(heartbeat_raw)
    heartbeat_endpoint = heartbeat_raw.get("endpoint", "gateway")
    heartbeat_path     = heartbeat_raw.get("path", "/devices/heartbeat")
    heartbeat_interval_s = int(heartbeat_raw.get("interval_s", 30))

    return {
        "schema_version": schema.get("schema_version", "1.0"),
        "board_id": board.get("id", schema.get("board")),
        "board": board,
        "chip": board.get("chip", "esp32s3"),
        "apps": apps,
        "all_handlers": sorted(all_handlers),
        "display_driver": display.get("driver", "st7789"),
        "display_width": display.get("width", 240),
        "display_height": display.get("height", 320),
        "display_orientation": display.get("orientation", "portrait"),
        "display_interface": display.get("interface", "spi"),
        "display_rotation": display.get("rotation", 0),
        "display_mirror_x": "true" if display.get("mirror_x", False) else "false",
        "display_mirror_y": "true" if display.get("mirror_y", False) else "false",
        # Display SPI pins (for SPI drivers; -1 if unused)
        "display_backlight_gpio": display.get("backlight_gpio", -1),
        "display_spi_host": display.get("spi_host", "SPI2_HOST"),
        "display_pin_cs": display.get("pin_cs", -1),
        "display_pin_dc": display.get("pin_dc", -1),
        "display_pin_rst": display.get("pin_rst", -1),
        "display_pin_sclk": display.get("pin_sclk", -1),
        "display_pin_mosi": display.get("pin_mosi", -1),
        "display_pin_miso": display.get("pin_miso", -1),
        # Touch
        "touch_driver": touch.get("driver"),
        "touch_sda": touch.get("sda", -1),
        "touch_scl": touch.get("scl", -1),
        "touch_rst": touch.get("rst_gpio", -1),
        "touch_int": touch.get("int_gpio", -1),
        "touch_swap_xy": "true" if touch.get("swap_xy", False) else "false",
        "touch_mirror_x": "true" if touch.get("mirror_x", False) else "false",
        "touch_mirror_y": "true" if touch.get("mirror_y", False) else "false",
        "lvgl_buf_kb": memory.get("lvgl_buf_kb", 40),
        "has_psram": memory.get("psram_mb", 0) > 0,
        "brookesia_stylesheet": brookesia.get("stylesheet", "ESP_BROOKESIA_PHONE_DARK_STYLESHEET"),
        "brookesia_gesture_nav_back": gesture_nav_back,
        "serial_bridge_enabled": board.get("serial_bridge", {}).get("enabled", False),
        "serial_bridge_uart":    board.get("serial_bridge", {}).get("uart", "UART_NUM_0"),
        "serial_bridge_baud":    board.get("serial_bridge", {}).get("baud", 115200),
        "screenschema_dir": str(pathlib.Path(__file__).parent.parent.resolve()),
        # Device identity
        "device_id":            device_id,
        "device_location":      device_location,
        "device_friendly_name": device_friendly_name,
        # Network endpoints
        "network_endpoints":    network_endpoints,
        # Device server
        "server_enabled":       server_enabled,
        "server_port":          server_port,
        "server_mdns":          server_mdns,
        # IoT Gateway
        "gw_enabled":           gw_enabled,
        "gw_url":               gw_url,
        "gw_secret":            gw_secret,
        # WebSocket
        "ws_enabled":           ws_enabled,
        "ws_url":               ws_url,
        "ws_reconnect_ms":      ws_reconnect_s * 1000,
        # Heartbeat
        "heartbeat_enabled":    heartbeat_enabled,
        "heartbeat_endpoint":   heartbeat_endpoint,
        "heartbeat_path":       heartbeat_path,
        "heartbeat_interval_s": heartbeat_interval_s,
        # Audio (R2.1)
        "audio_driver":         audio.get("driver", "none"),  # "es8311" | "i2s" | "none"
        "has_audio":            audio.get("driver", "none") != "none",
        # ES8311 codec config
        "audio_codec_i2c_addr": audio.get("codec_i2c_addr", 0x18),
        "audio_i2s_port":       audio.get("i2s_port", 0),
        "audio_sample_rate":    audio.get("sample_rate", 16000),
        "audio_bits":           audio.get("bits", 16),
        "audio_pin_mck":        audio.get("pin_mck", -1),
        "audio_pin_bck":        audio.get("pin_bck", -1),
        "audio_pin_ws":         audio.get("pin_ws", -1),
        "audio_pin_dout":       audio.get("pin_dout", -1),
        "audio_pin_din":        audio.get("pin_din", -1),
        "audio_amp_gpio":       audio.get("amp_enable_gpio", -1),
        "audio_amp_active_low": "true" if audio.get("amp_active_low", True) else "false",
        # Bare I2S config (mic/speaker on separate I2S peripherals)
        "audio_mic_driver":     audio.get("mic", {}).get("driver", "none"),
        "audio_mic_sample_rate": audio.get("mic", {}).get("sample_rate", 16000),
        "audio_mic_bits":       audio.get("mic", {}).get("bits", 16),
        "audio_mic_pin_clk":    audio.get("mic", {}).get("pin_clk", -1),
        "audio_mic_pin_data":   audio.get("mic", {}).get("pin_data", -1),
        "audio_mic_pin_ws":     audio.get("mic", {}).get("pin_ws", -1),
        "audio_mic_gain_db":    audio.get("mic", {}).get("gain_db", 0),
        "audio_mic_i2s_port":   audio.get("mic", {}).get("i2s_port", 0),
        "audio_spk_driver":     audio.get("speaker", {}).get("driver", "none"),
        "audio_spk_sample_rate": audio.get("speaker", {}).get("sample_rate", 16000),
        "audio_spk_bits":       audio.get("speaker", {}).get("bits", 16),
        "audio_spk_pin_bclk":   audio.get("speaker", {}).get("pin_bclk", -1),
        "audio_spk_pin_lrclk":  audio.get("speaker", {}).get("pin_lrclk", -1),
        "audio_spk_pin_data":   audio.get("speaker", {}).get("pin_data", -1),
        "audio_spk_amp_gpio":   audio.get("speaker", {}).get("amp_enable_gpio", -1),
        "audio_spk_i2s_port":   audio.get("speaker", {}).get("i2s_port", 1),
        # WiFi
        "wifi_driver": wifi.get("driver", "native"),   # "native" | "hosted"
        "wifi_hosted_transport": wifi.get("transport", "spi"),
        "wifi_hosted_spi_host": wifi.get("spi_host", "SPI2_HOST"),
        "wifi_hosted_pin_mosi": wifi.get("pin_mosi", -1),
        "wifi_hosted_pin_miso": wifi.get("pin_miso", -1),
        "wifi_hosted_pin_sclk": wifi.get("pin_sclk", -1),
        "wifi_hosted_pin_cs":   wifi.get("pin_cs",   -1),
        "wifi_hosted_pin_handshake": wifi.get("pin_handshake", -1),
        "wifi_hosted_pin_reset": wifi.get("pin_reset", -1),
        # Early init (D6 — power gates)
        "early_init_gpio_high": early_init.get("gpio_set_high", []),
        "early_init_gpio_low":  early_init.get("gpio_set_low", []),
        # Keyboard (D2)
        "has_keyboard":       keyboard.get("driver", "none") != "none",
        "keyboard_driver":    keyboard.get("driver", "none"),
        "keyboard_i2c_addr":  keyboard.get("i2c_addr", 0x55),
        "keyboard_int_gpio":  keyboard.get("int_gpio", -1),
        "keyboard_backlight": keyboard.get("backlight_control", False),
        # Trackball (D3)
        "has_trackball":      trackball.get("driver", "none") != "none",
        "trackball_driver":   trackball.get("driver", "none"),
        "trackball_pin_up":   trackball.get("pin_up", -1),
        "trackball_pin_down": trackball.get("pin_down", -1),
        "trackball_pin_left": trackball.get("pin_left", -1),
        "trackball_pin_right": trackball.get("pin_right", -1),
        "trackball_pin_click": trackball.get("pin_click", -1),
        # Battery (D9)
        "has_battery":            bool(battery),
        "battery_adc_gpio":       battery.get("adc_gpio", -1),
        "battery_voltage_divider": battery.get("voltage_divider", 2.0),
        "battery_full_mv":        battery.get("full_mv", 4200),
        "battery_empty_mv":       battery.get("empty_mv", 3300),
        "battery_sample_ms":      battery.get("sample_interval_ms", 30000),
        # System app
        "system_app_enabled": system_app_enabled,
        "system_app_wifi":    system_app_wifi,
        "system_app_ota":     system_app_ota,
        "ota_manifest_url":   board.get("ota", {}).get("manifest_url", ""),
        "firmware_version":   schema.get("firmware_version", "0.0.0"),
    }


def _append_missing_handlers(ctx: dict, handlers_cpp: pathlib.Path):
    existing = handlers_cpp.read_text()
    stubs = []
    for handler in ctx["all_handlers"]:
        if handler and handler not in existing:
            stubs.append(f'\nvoid {handler}(const SSEvent& e) {{\n    // TODO: implement {handler}\n}}\n')
    if stubs:
        with open(handlers_cpp, "a") as f:
            f.write("\n// === Added by screenschema (new handlers) ===\n")
            f.writelines(stubs)


def _write_sdkconfig(schema: dict, board: dict, out_dir: pathlib.Path):
    memory = board.get("memory", {})
    display = board.get("display", {})
    system = board.get("system", {})
    has_psram = memory.get("psram_mb", 0) > 0
    flash_mb = memory.get("flash_mb", 0)

    lines = [
        "# Generated by screenschema — do not edit manually",
        "CONFIG_LV_USE_LOG=y",
        "CONFIG_LV_LOG_LEVEL_WARN=y",
        # Built-in monospace fonts (D7 — for terminal output / code rendering)
        "CONFIG_LV_FONT_UNSCII_8=y",
        "CONFIG_LV_FONT_UNSCII_16=y",
    ]

    # RGB565 byte swap needed for SPI displays (ESP32 little-endian, SPI sends MSB first)
    if display.get("interface") == "spi":
        lines.append("CONFIG_LV_COLOR_16_SWAP=y")

    if has_psram:
        psram_mode = memory.get("psram_mode", "quad")
        lines += [
            "CONFIG_SPIRAM=y",
            f"CONFIG_SPIRAM_MODE_{'OCT' if psram_mode == 'oct' else 'QUAD'}=y",
            "CONFIG_SPIRAM_USE_MALLOC=y",
            "CONFIG_LV_MEM_CUSTOM=y",
            "CONFIG_LV_MEM_SIZE_KILOBYTES=512",
        ]
    else:
        lines.append("CONFIG_LV_MEM_SIZE_KILOBYTES=64")

    # System settings
    if system.get("freertos_unicore", False):
        lines.append("CONFIG_FREERTOS_UNICORE=y")
    stack_size = system.get("main_task_stack_size", 0)
    if stack_size:
        lines.append(f"CONFIG_ESP_MAIN_TASK_STACK_SIZE={stack_size}")

    # Flash size + OTA
    if flash_mb:
        lines += [
            "CONFIG_PARTITION_TABLE_CUSTOM=y",
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"',
            f"CONFIG_ESPTOOLPY_FLASHSIZE_{flash_mb}MB=y",
            f'CONFIG_ESPTOOLPY_FLASHSIZE="{flash_mb}MB"',
        ]
    if flash_mb >= 8:
        lines += [
            # Allow plain http:// OTA URLs (handy for local dev server)
            "CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y",
            # Roll back to previous firmware if new one fails to boot
            "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
        ]

    (out_dir / "sdkconfig.defaults").write_text("\n".join(lines) + "\n")


def _write_partitions(board: dict, out_dir: pathlib.Path):
    """Write partitions.csv. Uses OTA layout when flash >= 8 MB."""
    memory = board.get("memory", {})
    flash_mb = memory.get("flash_mb", 4)

    if flash_mb >= 8:
        # Two OTA slots of 6 MB each; remaining ~4 MB for FAT storage.
        # Layout (16 MB = 0x1000000):
        #   nvs      0x9000   0x6000   (24 KB)
        #   otadata  0xf000   0x2000   (8 KB)
        #   ota_0    0x20000  0x600000 (6 MB)
        #   ota_1   0x620000  0x600000 (6 MB)
        #   storage 0xC20000 0x3E0000  (~4 MB)
        rows = [
            ("nvs",     "data", "nvs",     "0x9000",   "0x6000"),
            ("otadata", "data", "ota",     "0xf000",   "0x2000"),
            ("ota_0",   "app",  "ota_0",   "0x20000",  "0x600000"),
            ("ota_1",   "app",  "ota_1",   "0x620000", "0x600000"),
            ("storage", "data", "fat",     "0xC20000", "0x3E0000"),
        ]
    else:
        # Single factory slot (no OTA)
        rows = [
            ("nvs",      "data", "nvs",     "0x9000",   "0x5000"),
            ("phy_init", "data", "phy",     "0xe000",   "0x1000"),
            ("factory",  "app",  "factory", "0x10000",  "0x300000"),
            ("storage",  "data", "fat",     "0x310000", "0xF0000"),
        ]

    lines = [
        "# ESP-IDF Partition Table — generated by screenschema",
        "# Name,     Type, SubType,  Offset,    Size",
    ]
    for row in rows:
        name, typ, sub, offset, size = row
        lines.append(f"{name},{typ},{sub},{offset},{size}")

    (out_dir / "partitions.csv").write_text("\n".join(lines) + "\n")
