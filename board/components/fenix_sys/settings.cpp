#include <stddef.h>
#include <string.h>

#include "fenix_sys.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace fx::sys {

static Settings s_settings;
static bool s_empty = true;  // no settings blob in NVS at boot
static constexpr uint8_t BLOB_VERSION = 1;

struct Blob {
  uint8_t version;
  Settings s;
};
// A blob from older firmware is shorter (fields are only appended): the rest keeps its defaults.
static constexpr size_t OLDEST = offsetof(Blob, s) + offsetof(Settings, burn_guard);

void nvs_start() {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  nvs_handle_t h;
  if (nvs_open("fenix", NVS_READONLY, &h) != ESP_OK) return;
  Blob b;
  size_t n = sizeof b;
  if (nvs_get_blob(h, "settings", &b, &n) == ESP_OK && n >= OLDEST && b.version == BLOB_VERSION) {
    s_settings = b.s;
    s_empty = false;
  }
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
  backup_changed();
}

bool settings_were_empty() { return s_empty; }

size_t settings_blob(uint8_t* out, size_t cap) {
  Blob b{BLOB_VERSION, s_settings};
  size_t n = sizeof b < cap ? sizeof b : cap;
  memcpy(out, &b, n);
  return n;
}

bool settings_restore(const uint8_t* blob, size_t n) {
  if (n < OLDEST || blob[0] != BLOB_VERSION) return false;
  Blob b{BLOB_VERSION, Settings{}};
  memcpy(&b, blob, n < sizeof b ? n : sizeof b);
  if (s_settings.cal_valid) {  // calibrated here: a backup from another board would not fit
    b.s.cal_valid = 1;
    memcpy(b.s.cal, s_settings.cal, sizeof b.s.cal);
  }
  b.s.rotation &= 3;
  if (b.s.brightness < 16) b.s.brightness = 16;
  s_settings = b.s;
  settings_save();
  return true;
}

}  // namespace fx::sys
