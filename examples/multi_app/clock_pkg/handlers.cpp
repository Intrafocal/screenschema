// Package source — consumed in place by screenschema codegen (copied into
// the generated project's main/handlers_clock_app.cpp with a #line prologue
// on every build; edit THIS file, never the copy).
#include "handlers.hpp"
#include "ss_context.hpp"
#include "esp_log.h"
#include <cstdio>

static const char* TAG = "CLOCK";

static int g_ticks = 0;

void handler_clock_resume(const SSEvent& event) {
    ESP_LOGI(TAG, "clock resumed");
    SSContext::instance().set("clock_label", "00:00");
}

void handler_clock_tick(const SSEvent& event) {
    g_ticks++;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", g_ticks / 60, g_ticks % 60);
    SSContext::instance().set("clock_label", buf);
}
