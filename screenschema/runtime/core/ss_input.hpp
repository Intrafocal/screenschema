#pragma once
#include <cstdint>
#include <functional>
#include <vector>

enum class SSKeySource : uint8_t {
    Keyboard,    // I2C BB keyboard
    Trackball,   // Trackball direction mapped to key
};

/// Global key interceptor — called by input drivers before forwarding to LVGL.
/// Register handlers that return true to consume a key event (prevents LVGL from seeing it).
/// Useful for routing keystrokes to a WebSocket PTY in terminal mode, or for
/// global shortcuts that bypass focused widgets.
class SSInput {
public:
    static SSInput& instance();

    using KeyHandler = std::function<bool(uint8_t key, SSKeySource source)>;

    /// Register a key interceptor.  Multiple handlers are called in registration
    /// order; the first one to return true wins and the event is consumed.
    void onKey(KeyHandler handler);

    /// Called by input drivers before forwarding to LVGL.
    /// Returns true if any handler consumed the event.
    bool dispatch(uint8_t key, SSKeySource source);

    /// Remove all handlers (call on app switch).
    void clearHandlers();

private:
    SSInput() = default;
    std::vector<KeyHandler> handlers_;
};
