// screenschema sim — FreeRTOS type stubs
#pragma once
#include <cstdint>

typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  pdTRUE
#define portMAX_DELAY  0xFFFFFFFF
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configSTACK_DEPTH_TYPE uint32_t
