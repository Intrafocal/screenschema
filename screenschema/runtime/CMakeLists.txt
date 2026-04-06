cmake_minimum_required(VERSION 3.16)

set(srcs
    "core/ss_event_bus.cpp"
    "core/ss_context.cpp"
    "core/ss_widget_factory.cpp"
    "core/ss_app_base.cpp"
    "core/ss_device.cpp"
    "core/ss_mdns.cpp"
    "core/ss_wifi_manager.cpp"
    "core/ss_update_manager.cpp"
    "core/ss_system_app.cpp"
    "core/ss_http_client.cpp"
    "core/ss_gateway.cpp"
    "core/ss_heartbeat.cpp"
    "core/ss_device_server.cpp"
    "core/ss_shell.cpp"
    "core/ss_notification.cpp"
    "core/ss_websocket.cpp"
    "core/ss_serial_bridge.cpp"
    "core/ss_audio_queue.cpp"
    "core/ss_background.cpp"
    "core/ss_wifi_transport_hosted.cpp"
    "core/ss_input.cpp"
    "core/ss_battery.cpp"
    "hal/ss_hal.cpp"
    "hal/drivers/display/ili9341.cpp"
    "hal/drivers/display/st7789.cpp"
    "hal/drivers/touch/gt911.cpp"
    "hal/drivers/touch/ft6236.cpp"
    "hal/drivers/audio/ss_audio_i2s.cpp"
    "hal/drivers/audio/ss_audio_es8311.cpp"
    "hal/drivers/input/ss_keyboard_i2c.cpp"
    "hal/drivers/input/ss_trackball_gpio.cpp"
)
# DSI display driver only available on ESP32-P4
if(${IDF_TARGET} STREQUAL "esp32p4")
    list(APPEND srcs "hal/drivers/display/st7701s_dsi.cpp")
endif()

set(include_dirs
    "."
    "core"
    "hal"
    "hal/drivers/display"
    "hal/drivers/touch"
    "hal/drivers/audio"
    "hal/drivers/input"
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
        esp_lcd_touch_ft5x06
        esp_lcd_ili9341
        driver
        esp_adc
        esp_driver_i2s
        esp_driver_ledc
        lvgl
        esp-brookesia
        esp_timer
        heap
        esp_wifi
        esp_netif
        esp_event
        lwip
        nvs_flash
        mdns
        esp_http_client
        esp_http_server
        esp_https_ota
        app_update
        json
        esp_websocket_client
)
