// screenschema sim — FreeRTOS task stubs
#pragma once
#include "FreeRTOS.h"

#define vTaskDelay(ticks)  ((void)0)
#define xTaskCreate(fn, name, stack, param, prio, handle)  pdPASS
#define vTaskDelete(handle) ((void)0)
