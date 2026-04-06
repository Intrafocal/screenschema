// screenschema sim — FreeRTOS semaphore stubs (single-threaded, no-op)
#pragma once
#include "FreeRTOS.h"

typedef void* SemaphoreHandle_t;

#define xSemaphoreCreateMutex()               ((SemaphoreHandle_t)1)
#define xSemaphoreTake(sem, timeout)          pdTRUE
#define xSemaphoreGive(sem)                   pdTRUE
#define vSemaphoreDelete(sem)                 ((void)0)
#define xSemaphoreCreateBinary()              ((SemaphoreHandle_t)1)
