#include "ss_serial_bridge.hpp"
#include "ss_context.hpp"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "SS_BRIDGE";

SSSerialBridge& SSSerialBridge::instance() {
    static SSSerialBridge inst;
    return inst;
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

void SSSerialBridge::start(uart_port_t uart_num, int baud) {
    if (running_) return;
    uart_num_ = uart_num;
    running_  = true;

    // LVGL dispatch pump
    pending_mutex_ = xSemaphoreCreateMutex();
    pump_timer_    = lv_timer_create(pump_timer_cb, 20, this);

    // UART init
    uart_config_t cfg = {};
    cfg.baud_rate  = baud;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    uart_param_config(uart_num_, &cfg);
    // Don't reassign UART0 pins — they are fixed (GPIO43/44 on S3)
    uart_driver_install(uart_num_, 1024, 1024, 0, nullptr, 0);

    xTaskCreate(bridge_task, "ss_bridge", 4096, this, 3, &task_handle_);
    ESP_LOGI(TAG, "Serial bridge started on UART%d @ %d baud", uart_num_, baud);
}

void SSSerialBridge::stop() {
    running_ = false;
    if (task_handle_) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    uart_driver_delete(uart_num_);
}

// ---------------------------------------------------------------------------
// FreeRTOS task
// ---------------------------------------------------------------------------

void SSSerialBridge::bridge_task(void* arg) {
    static_cast<SSSerialBridge*>(arg)->run();
    vTaskDelete(nullptr);
}

void SSSerialBridge::run() {
    while (running_) {
        // Wait for magic header
        uint8_t b;
        if (!read_byte(b)) continue;
        if (b != MAGIC_HI) continue;
        if (!read_byte(b)) continue;
        if (b != MAGIC_LO) continue;

        parse_frame();
    }
}

// ---------------------------------------------------------------------------
// Frame parsing
// ---------------------------------------------------------------------------

bool SSSerialBridge::read_byte(uint8_t& out, TickType_t ticks) {
    return uart_read_bytes(uart_num_, &out, 1, ticks) == 1;
}

bool SSSerialBridge::read_bytes(uint8_t* buf, size_t len, TickType_t ticks) {
    size_t got = 0;
    while (got < len) {
        int n = uart_read_bytes(uart_num_, buf + got, len - got, ticks);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

bool SSSerialBridge::parse_frame() {
    uint8_t cmd, id_len;
    if (!read_byte(cmd))    return false;
    if (!read_byte(id_len)) return false;

    std::string id(id_len, '\0');
    if (id_len > 0 && !read_bytes(reinterpret_cast<uint8_t*>(&id[0]), id_len)) return false;

    uint8_t val_type;
    if (!read_byte(val_type)) return false;

    uint8_t vl_h, vl_l;
    if (!read_byte(vl_h)) return false;
    if (!read_byte(vl_l)) return false;
    uint16_t val_len = ((uint16_t)vl_h << 8) | vl_l;

    std::vector<uint8_t> val(val_len);
    if (val_len > 0 && !read_bytes(val.data(), val_len)) return false;

    uint8_t checksum;
    if (!read_byte(checksum)) return false;

    // Verify XOR checksum over cmd..val
    uint8_t xor_calc = cmd ^ id_len;
    for (char c : id)                     xor_calc ^= (uint8_t)c;
    xor_calc ^= val_type ^ vl_h ^ vl_l;
    for (uint8_t b : val)                 xor_calc ^= b;
    if (xor_calc != checksum) {
        ESP_LOGW(TAG, "Checksum mismatch (got 0x%02X, expected 0x%02X)", checksum, xor_calc);
        return false;
    }

    dispatch_command(cmd, id, val_type, val);
    return true;
}

// ---------------------------------------------------------------------------
// Command dispatch → LVGL task
// ---------------------------------------------------------------------------

void SSSerialBridge::dispatch_command(uint8_t cmd, const std::string& id,
                                       uint8_t val_type, const std::vector<uint8_t>& val) {
    switch (cmd) {

        case CMD_SET: {
            // Decode value and post set() to LVGL task
            if (val_type == VAL_INT32 && val.size() >= 4) {
                int32_t v = ((int32_t)val[0] << 24) | ((int32_t)val[1] << 16)
                          | ((int32_t)val[2] << 8)  |  (int32_t)val[3];
                post_fn([id, v]() { SSContext::instance().set(id, v); });
            } else if (val_type == VAL_FLOAT && val.size() >= 4) {
                uint32_t bits = ((uint32_t)val[0] << 24) | ((uint32_t)val[1] << 16)
                              | ((uint32_t)val[2] << 8)  |  (uint32_t)val[3];
                float f;
                memcpy(&f, &bits, 4);
                post_fn([id, f]() { SSContext::instance().set(id, f); });
            } else if (val_type == VAL_STRING) {
                std::string s(val.begin(), val.end());
                post_fn([id, s]() { SSContext::instance().set(id, s); });
            } else if (val_type == VAL_BOOL && !val.empty()) {
                bool b = val[0] != 0;
                post_fn([id, b]() { SSContext::instance().set(id, b); });
            }
            break;
        }

        case CMD_GET: {
            // Read current string value and send GET_RESP
            post_fn([this, id]() {
                std::string val = SSContext::instance().get<std::string>(id);
                std::vector<uint8_t> bytes(val.begin(), val.end());
                send_frame(CMD_GET_RESP, id, VAL_STRING, bytes.data(), bytes.size());
            });
            break;
        }

        case CMD_SHOW:
            post_fn([id]() { SSContext::instance().show(id); });
            break;

        case CMD_HIDE:
            post_fn([id]() { SSContext::instance().hide(id); });
            break;

        case CMD_PING: {
            uint8_t empty = 0;
            send_frame(CMD_PONG, "", VAL_NONE, &empty, 0);
            break;
        }

        case CMD_LIST_ADD: {
            std::string text(val.begin(), val.end());
            post_fn([id, text]() { SSContext::instance().list_add(id, text); });
            break;
        }

        case CMD_LIST_CLEAR:
            post_fn([id]() { SSContext::instance().list_clear(id); });
            break;

        default:
            ESP_LOGW(TAG, "Unknown command 0x%02X", cmd);
            break;
    }
}

// ---------------------------------------------------------------------------
// Send a frame
// ---------------------------------------------------------------------------

void SSSerialBridge::send_frame(uint8_t cmd, const std::string& id,
                                 uint8_t val_type, const uint8_t* val, size_t val_len) {
    uint8_t id_len = (uint8_t)std::min(id.size(), (size_t)255);
    uint8_t vl_h = (uint8_t)((val_len >> 8) & 0xFF);
    uint8_t vl_l = (uint8_t)(val_len & 0xFF);

    // Checksum: XOR of cmd through end of val
    uint8_t xor_calc = cmd ^ id_len;
    for (size_t i = 0; i < id_len; ++i) xor_calc ^= (uint8_t)id[i];
    xor_calc ^= val_type ^ vl_h ^ vl_l;
    for (size_t i = 0; i < val_len; ++i) xor_calc ^= val[i];

    // Build and send in one write to minimise interleaving
    std::vector<uint8_t> frame;
    frame.reserve(5 + id_len + 3 + val_len + 1);
    frame.push_back(MAGIC_HI);
    frame.push_back(MAGIC_LO);
    frame.push_back(cmd);
    frame.push_back(id_len);
    for (size_t i = 0; i < id_len; ++i) frame.push_back((uint8_t)id[i]);
    frame.push_back(val_type);
    frame.push_back(vl_h);
    frame.push_back(vl_l);
    for (size_t i = 0; i < val_len; ++i) frame.push_back(val[i]);
    frame.push_back(xor_calc);

    uart_write_bytes(uart_num_, frame.data(), frame.size());
}

// ---------------------------------------------------------------------------
// Send an event to the host
// ---------------------------------------------------------------------------

void SSSerialBridge::sendEvent(const std::string& widget_id, uint8_t event_type, int32_t value) {
    // Encode: [event_type (1 byte)][value (4 bytes big-endian)]
    uint8_t payload[5];
    payload[0] = event_type;
    payload[1] = (uint8_t)((value >> 24) & 0xFF);
    payload[2] = (uint8_t)((value >> 16) & 0xFF);
    payload[3] = (uint8_t)((value >>  8) & 0xFF);
    payload[4] = (uint8_t)(value & 0xFF);
    send_frame(CMD_EVENT, widget_id, VAL_INT32, payload, sizeof(payload));
}

// ---------------------------------------------------------------------------
// LVGL task dispatch
// ---------------------------------------------------------------------------

void SSSerialBridge::post_fn(std::function<void()> fn) {
    if (!pending_mutex_) return;
    if (xSemaphoreTake(pending_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending_queue_.push_back(std::move(fn));
        xSemaphoreGive(pending_mutex_);
    } else {
        ESP_LOGW(TAG, "post_fn: mutex timeout");
    }
}

void SSSerialBridge::pump_timer_cb(lv_timer_t* t) {
    static_cast<SSSerialBridge*>(t->user_data)->pump_pending();
}

void SSSerialBridge::pump_pending() {
    std::vector<std::function<void()>> to_run;
    if (xSemaphoreTake(pending_mutex_, 0) == pdTRUE) {
        to_run = std::move(pending_queue_);
        pending_queue_.clear();
        xSemaphoreGive(pending_mutex_);
    }
    for (auto& fn : to_run) fn();
}
