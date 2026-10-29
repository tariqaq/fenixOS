// Simulator stub: the app version comes from the repo's VERSION file (board/sim/CMakeLists.txt).
#pragma once
typedef struct {
  char version[32];
} esp_app_desc_t;
static inline const esp_app_desc_t* esp_app_get_description(void) {
  static const esp_app_desc_t d = {FENIX_VERSION};
  return &d;
}
