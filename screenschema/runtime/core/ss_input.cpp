#include "ss_input.hpp"

SSInput& SSInput::instance() {
    static SSInput inst;
    return inst;
}

void SSInput::onKey(KeyHandler handler) {
    handlers_.push_back(std::move(handler));
}

bool SSInput::dispatch(uint8_t key, SSKeySource source) {
    for (auto& handler : handlers_) {
        if (handler(key, source)) {
            return true;  // consumed
        }
    }
    return false;  // pass through to LVGL
}

void SSInput::clearHandlers() {
    handlers_.clear();
}
