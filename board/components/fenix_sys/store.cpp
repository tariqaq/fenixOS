#include "esp_littlefs.h"
#include "fenix_sys.h"

namespace fx::sys {

static bool s_mounted;

bool store_mount() {
  esp_vfs_littlefs_conf_t c = {};
  c.base_path = "/apps";
  c.partition_label = "apps";
  c.format_if_mount_failed = true;
  s_mounted = esp_vfs_littlefs_register(&c) == ESP_OK;
  return s_mounted;
}

void store_usage(uint32_t* used_kb, uint32_t* total_kb) {
  size_t total = 0, used = 0;
  if (s_mounted) esp_littlefs_info("apps", &total, &used);
  *used_kb = used / 1024;
  *total_kb = total / 1024;
}

}  // namespace fx::sys
