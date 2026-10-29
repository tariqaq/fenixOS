// SYSINFO for the PC's Task manager (proto/spec.md): heap, flash, link counters and the
// FreeRTOS tasks with their run times. Raw counters only; the PC works out load and rates.
// Link task only.
#include <string.h>

#include "esp_app_format.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace fx::sys {

uint32_t link_rx_bytes();
uint32_t link_tx_bytes();

static constexpr int MAX_TASKS = 40;  // 40 tasks fit in one frame (spec: MAX_PAYLOAD)
static TaskStatus_t s_tasks[MAX_TASKS + 8];
static uint32_t s_flash_size, s_app_size, s_app_slot;

// Flash and image sizes do not change while running: read them once.
static void read_flash_once() {
  if (s_flash_size) return;
  esp_flash_get_size(nullptr, &s_flash_size);
  const esp_partition_t* p = esp_ota_get_running_partition();
  if (!p) return;
  s_app_slot = p->size;
  esp_partition_pos_t pos = {p->address, p->size};
  esp_image_metadata_t meta{};
  if (esp_image_get_metadata(&pos, &meta) == ESP_OK) s_app_size = meta.image_len;
}

void sysinfo_write(fnx::Writer& w) {
  read_flash_once();
  fnx::Sysinfo s{};
  s.cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  s.heap_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  s.heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  s.heap_min = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  s.heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  s.dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
  s.flash_size = s_flash_size;
  s.app_size = s_app_size;
  s.app_slot = s_app_slot;
  uint32_t used_kb = 0, total_kb = 0;
  store_usage(&used_kb, &total_kb);
  s.apps_used = used_kb * 1024;
  s.apps_total = total_kb * 1024;
  nvs_stats_t nvs{};
  if (nvs_get_stats(nullptr, &nvs) == ESP_OK) {
    s.nvs_used = nvs.used_entries;
    s.nvs_total = nvs.total_entries;
  }
  s.rx_bytes = link_rx_bytes();
  s.tx_bytes = link_tx_bytes();
  s.rx_frames = link_rx_frames();
  s.rx_errors = link_rx_errors();
  BoardInfo b = board_info();
  s.fps_x10 = b.fps_x10;
  s.backlight = b.backlight;
  s.flags = (b.dimmed ? fnx::sysflag::DIMMED : 0) | (b.resting ? fnx::sysflag::RESTING : 0) |
            (settings().burn_guard ? fnx::sysflag::BURN_GUARD : 0) |
            (stream_active() ? fnx::sysflag::STREAMING : 0);  // no Wi-Fi or Bluetooth stack yet

  // The run-time clock (esp_timer, µs) also stamps the sample. 0 tasks when the table is full.
  configRUN_TIME_COUNTER_TYPE now = 0;
  UBaseType_t n = uxTaskGetSystemState(s_tasks, MAX_TASKS + 8, &now);
  s.time_us = (uint32_t)now;
  fnx::write_sysinfo(w, s);
  if (n > MAX_TASKS) n = MAX_TASKS;
  w.u8(n);
  for (UBaseType_t i = 0; i < n; i++) {
    const TaskStatus_t& t = s_tasks[i];
    fnx::TaskInfo ti{};
    ti.id = t.xTaskNumber;
    BaseType_t core = xTaskGetCoreID(t.xHandle);
    ti.core = core == 0 || core == 1 ? core : 255;
    ti.priority = t.uxCurrentPriority;
    ti.state = t.eCurrentState;
    ti.runtime = (uint32_t)t.ulRunTimeCounter;
    ti.stack_free = t.usStackHighWaterMark > 0xFFFF ? 0xFFFF : t.usStackHighWaterMark;
    strlcpy(ti.name, t.pcTaskName, sizeof ti.name);
    fnx::write_task_info(w, ti);
  }
}

}  // namespace fx::sys
