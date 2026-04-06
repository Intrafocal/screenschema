// handlers.cpp — example hello_world handlers
// This file is user-territory: screenschema will never overwrite it.
#include "handlers.hpp"
#include "ss_context.hpp"
#include "esp_log.h"

static const char* TAG = "HELLO";
static int tap_count = 0;

void handler_hello_init(const SSEvent& event) {
    ESP_LOGI(TAG, "Hello app initialized");
    // Example: update the subtitle after init
    SSContext::instance().set("subtitle_label", "Tap the button!");
}

void handler_tap(const SSEvent& event) {
    ESP_LOGI(TAG, "Button tapped!");
    SSContext::instance().set("greeting_label", "You tapped it!");
}

void handler_increment(const SSEvent& event) {
    tap_count++;
    SSContext::instance().set("count_label", std::to_string(tap_count));
    ESP_LOGI(TAG, "Count: %d", tap_count);
}

void handler_reset(const SSEvent& event) {
    tap_count = 0;
    SSContext::instance().set("count_label", "0");
    ESP_LOGI(TAG, "Counter reset");
}
