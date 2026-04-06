#pragma once
#include <string>
#include <functional>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "lvgl.h"

// SSSerialBridge — binary serial protocol for host↔device data exchange (Phase 3)
//
// Frame format (both directions):
//   [0xAA][0x55][CMD][ID_LEN][ID...][VAL_TYPE][VAL_LEN_H][VAL_LEN_L][VAL...][XOR]
//
// Host→device commands: SET=0x01, GET=0x02, SHOW=0x03, HIDE=0x04, PING=0x05,
//                        LIST_ADD=0x06, LIST_CLEAR=0x07
// Device→host commands: GET_RESP=0x81, EVENT=0x82, PONG=0x85
//
// Value types: NONE=0, INT32=1, FLOAT=2, STRING=3, BOOL=4
//
// XOR checksum covers all bytes from CMD through end of VAL (exclusive of magic).

class SSSerialBridge {
public:
    static SSSerialBridge& instance();

    // Start the bridge task. Call from app_main after phone.begin().
    void start(uart_port_t uart_num = UART_NUM_0, int baud = 115200);
    void stop();

    // Send an event to the host (e.g. button click). Safe to call from any task.
    void sendEvent(const std::string& widget_id, uint8_t event_type, int32_t value = 0);

private:
    SSSerialBridge() = default;

    static void bridge_task(void* arg);
    void run();

    // Frame parsing
    bool read_byte(uint8_t& out, TickType_t ticks = pdMS_TO_TICKS(100));
    bool read_bytes(uint8_t* buf, size_t len, TickType_t ticks = pdMS_TO_TICKS(500));
    bool parse_frame();
    void dispatch_command(uint8_t cmd, const std::string& id,
                          uint8_t val_type, const std::vector<uint8_t>& val);

    // Frame building
    void send_frame(uint8_t cmd, const std::string& id,
                    uint8_t val_type, const uint8_t* val, size_t val_len);

    // LVGL task dispatch (same pattern as other singletons)
    void post_fn(std::function<void()> fn);
    static void pump_timer_cb(lv_timer_t* t);
    void pump_pending();

    uart_port_t uart_num_ = UART_NUM_0;
    TaskHandle_t task_handle_ = nullptr;
    bool running_ = false;

    SemaphoreHandle_t pending_mutex_ = nullptr;
    std::vector<std::function<void()>> pending_queue_;
    lv_timer_t* pump_timer_ = nullptr;

    // Value type constants
    static constexpr uint8_t VAL_NONE   = 0;
    static constexpr uint8_t VAL_INT32  = 1;
    static constexpr uint8_t VAL_FLOAT  = 2;
    static constexpr uint8_t VAL_STRING = 3;
    static constexpr uint8_t VAL_BOOL   = 4;

    // Command constants
    static constexpr uint8_t CMD_SET        = 0x01;
    static constexpr uint8_t CMD_GET        = 0x02;
    static constexpr uint8_t CMD_SHOW       = 0x03;
    static constexpr uint8_t CMD_HIDE       = 0x04;
    static constexpr uint8_t CMD_PING       = 0x05;
    static constexpr uint8_t CMD_LIST_ADD   = 0x06;
    static constexpr uint8_t CMD_LIST_CLEAR = 0x07;
    static constexpr uint8_t CMD_GET_RESP   = 0x81;
    static constexpr uint8_t CMD_EVENT      = 0x82;
    static constexpr uint8_t CMD_PONG       = 0x85;

    static constexpr uint8_t MAGIC_HI = 0xAA;
    static constexpr uint8_t MAGIC_LO = 0x55;
};
