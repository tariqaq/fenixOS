// Simulator stub: waiting moves the virtual clock (and lets the simulator's window breathe).
#pragma once
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
void vTaskDelay(TickType_t ticks);
#ifdef __cplusplus
}
#endif
