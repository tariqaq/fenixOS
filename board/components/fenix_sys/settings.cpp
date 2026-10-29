#include <stddef.h>

#include "fenix_sys.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace fx::sys {

static Settings s_settings;
static constexpr uint8_t BLOB_VERSION = 1;

struct Blob {
  uint8_t version;
  Settings s;
};

void nvs_start() {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  nvs_handle_t h;
  if (nvs_open("fenix", NVS_READONLY, &h) != ESP_OK) return;
  // A blob from older firmware is shorter (fields are only appended): the rest keeps its defaults.
  static constexpr size_t OLDEST = offsetof(Blob, s) + offsetof(Settings, burn_guard);
  Blob b;
  size_t n = sizeof b;
  if (nvs_get_blob(h, "settings", &b, &n) == ESP_OK && n >= OLDEST && b.version == BLOB_VERSION) s_settings = b.s;
  nvs_close(h);
  if (s_settings.brightness < 16) s_settings.brightness = 16;
  s_settings.rotation &= 3;
}

Settings& settings() { return s_settings; }

void settings_save() {
  nvs_handle_t h;
  if (nvs_open("fenix", NVS_READWRITE, &h) != ESP_OK) return;
  Blob b{BLOB_VERSION, s_settings};
  nvs_set_blob(h, "settings", &b, sizeof b);
  nvs_commit(h);
  nvs_close(h);
}

}  // namespace fx::sys
