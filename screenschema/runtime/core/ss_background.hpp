#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include "esp_err.h"

// Background task definition.
// handler runs on a dedicated FreeRTOS task until it returns or stop() is called.
struct SSBackgroundTaskDef {
    std::string id;
    std::function<void()> handler;  // task body — loop internally, check SSBackground::isRunning()
    uint32_t stack_size = 4096;
    uint8_t  priority   = 5;        // tskIDLE_PRIORITY + 5
    int      core       = 1;        // -1 = no affinity, 0 = LVGL core, 1 = background core
    bool     start_on_boot = false;
};

// Manages long-running FreeRTOS background tasks (sensor polling, audio pipeline, etc.).
// Tasks communicate with the UI via SSContext/SSEventBus (already thread-safe).
class SSBackground {
public:
    static SSBackground& instance();

    // Register a task definition. Does not start it.
    void registerTask(const SSBackgroundTaskDef& def);

    // Start/stop by id.
    esp_err_t start(const std::string& id);
    esp_err_t stop(const std::string& id);

    // Start all tasks marked start_on_boot.
    void startBootTasks();

    // Query state.
    bool isRunning(const std::string& id) const;
    bool isRegistered(const std::string& id) const;

    // Stop all running tasks.
    void stopAll();

private:
    SSBackground() = default;

    struct TaskEntry {
        SSBackgroundTaskDef def;
        void* task_handle = nullptr;  // TaskHandle_t
        bool  running     = false;
    };

    static void task_wrapper(void* param);

    // Flat map — we'll never have more than a handful of background tasks
    static constexpr size_t MAX_TASKS = 16;
    TaskEntry tasks_[MAX_TASKS] = {};
    size_t    count_ = 0;

    TaskEntry* find(const std::string& id);
    const TaskEntry* find(const std::string& id) const;
};
