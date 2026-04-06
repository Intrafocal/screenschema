#include "ss_background.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "SS_BG";

SSBackground& SSBackground::instance() {
    static SSBackground inst;
    return inst;
}

void SSBackground::registerTask(const SSBackgroundTaskDef& def) {
    if (find(def.id)) {
        ESP_LOGW(TAG, "Task '%s' already registered — skipping", def.id.c_str());
        return;
    }
    if (count_ >= MAX_TASKS) {
        ESP_LOGE(TAG, "Max background tasks reached (%u)", (unsigned)MAX_TASKS);
        return;
    }
    tasks_[count_].def = def;
    tasks_[count_].task_handle = nullptr;
    tasks_[count_].running = false;
    count_++;
    ESP_LOGI(TAG, "Registered task '%s' (stack=%lu, pri=%u, core=%d, boot=%s)",
             def.id.c_str(), (unsigned long)def.stack_size, def.priority,
             def.core, def.start_on_boot ? "yes" : "no");
}

esp_err_t SSBackground::start(const std::string& id) {
    TaskEntry* e = find(id);
    if (!e) {
        ESP_LOGW(TAG, "Task '%s' not registered", id.c_str());
        return ESP_ERR_NOT_FOUND;
    }
    if (e->running) {
        ESP_LOGW(TAG, "Task '%s' already running", id.c_str());
        return ESP_OK;
    }

    e->running = true;

    BaseType_t ret;
    if (e->def.core >= 0) {
        ret = xTaskCreatePinnedToCore(
            task_wrapper, e->def.id.c_str(), e->def.stack_size,
            e, e->def.priority, (TaskHandle_t*)&e->task_handle, e->def.core);
    } else {
        ret = xTaskCreate(
            task_wrapper, e->def.id.c_str(), e->def.stack_size,
            e, e->def.priority, (TaskHandle_t*)&e->task_handle);
    }

    if (ret != pdPASS) {
        e->running = false;
        e->task_handle = nullptr;
        ESP_LOGE(TAG, "Failed to create task '%s'", id.c_str());
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Started task '%s'", id.c_str());
    return ESP_OK;
}

esp_err_t SSBackground::stop(const std::string& id) {
    TaskEntry* e = find(id);
    if (!e) return ESP_ERR_NOT_FOUND;
    if (!e->running) return ESP_OK;

    // Signal the task to exit. The handler should check isRunning() and return.
    e->running = false;

    // Give the task time to notice and exit cleanly
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!e->task_handle) break;  // task_wrapper cleared it on exit
    }

    // If still running after 1s, force delete
    if (e->task_handle) {
        ESP_LOGW(TAG, "Task '%s' did not exit cleanly — deleting", id.c_str());
        vTaskDelete((TaskHandle_t)e->task_handle);
        e->task_handle = nullptr;
    }

    ESP_LOGI(TAG, "Stopped task '%s'", id.c_str());
    return ESP_OK;
}

void SSBackground::startBootTasks() {
    for (size_t i = 0; i < count_; i++) {
        if (tasks_[i].def.start_on_boot) {
            start(tasks_[i].def.id);
        }
    }
}

bool SSBackground::isRunning(const std::string& id) const {
    const TaskEntry* e = find(id);
    return e && e->running;
}

bool SSBackground::isRegistered(const std::string& id) const {
    return find(id) != nullptr;
}

void SSBackground::stopAll() {
    for (size_t i = 0; i < count_; i++) {
        if (tasks_[i].running) {
            stop(tasks_[i].def.id);
        }
    }
}

void SSBackground::task_wrapper(void* param) {
    auto* entry = static_cast<TaskEntry*>(param);
    ESP_LOGI(TAG, "Task '%s' running on core %d", entry->def.id.c_str(), xPortGetCoreID());

    if (entry->def.handler) {
        entry->def.handler();
    }

    // Handler returned — mark as stopped
    entry->running = false;
    entry->task_handle = nullptr;
    ESP_LOGI(TAG, "Task '%s' exited", entry->def.id.c_str());
    vTaskDelete(nullptr);
}

SSBackground::TaskEntry* SSBackground::find(const std::string& id) {
    for (size_t i = 0; i < count_; i++) {
        if (tasks_[i].def.id == id) return &tasks_[i];
    }
    return nullptr;
}

const SSBackground::TaskEntry* SSBackground::find(const std::string& id) const {
    for (size_t i = 0; i < count_; i++) {
        if (tasks_[i].def.id == id) return &tasks_[i];
    }
    return nullptr;
}
