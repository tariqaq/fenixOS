// Simulator stub: the board's chip (ESP32-D0WD-V3 rev 3.1, two cores).
#pragma once
#include <stdint.h>
typedef struct {
  int model;
  uint32_t features;
  uint16_t revision;
  uint8_t cores;
} esp_chip_info_t;
static inline void esp_chip_info(esp_chip_info_t* c) {
  c->model = 1;
  c->features = 0;
  c->revision = 301;
  c->cores = 2;
}
// The board's sdkconfig value (CPU frequency).
#ifndef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 240
#endif
