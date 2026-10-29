// Simulator stub: the virtual clock (sim_platform.cpp). It only moves when the simulator advances
// it or code waits in vTaskDelay, so runs are repeatable.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int64_t esp_timer_get_time(void);
#ifdef __cplusplus
}
#endif
