// Simulator stub: the board's 4 MB flash.
#pragma once
#include <stdint.h>
typedef int esp_err_t;
static inline esp_err_t esp_flash_get_size(void*, uint32_t* out) {
  *out = 4u << 20;
  return 0;
}
