// Board log and crash records on the SD card (P53).
// Every link_log line also goes to /sd/fenixos/logs/<n>.txt: one file per boot (n counts up from
// the highest number on the card), the 10 newest kept, at most 256 KB each. Lines wait in a 512 B
// buffer; the link task writes them every 5 s, and at once after an error or a warning.
// A core dump (the 56 KB "coredump" partition, written by IDF at a panic) is copied to
// /sd/fenixos/crash/<n>.elf with a short summary in <n>.txt when a card is mounted, then erased
// from flash. n is the boot that found it: the crash happened in the boot before.
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <new>

#include "esp_app_desc.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace fx::sys {

static constexpr const char* LOG_DIR = "/sd/fenixos/logs";
static constexpr const char* CRASH_DIR = "/sd/fenixos/crash";
static constexpr size_t BUF = 512;
static constexpr int KEEP = 10;
static constexpr long MAX_SIZE = 256 * 1024;
static constexpr int64_t FLUSH_US = 5000000;
static constexpr int64_t HEAP_EVERY_US = 15 * 60 * 1000000LL;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_buf[BUF];  // under s_mux
static size_t s_len;
static uint32_t s_dropped;
static bool s_urgent;

static char s_path[40];        // this boot's file; set by the sd task, read by the link task
static volatile bool s_ready;  // s_path is valid
static long s_size;
static bool s_full;
static uint32_t s_boot;
static int64_t s_next_flush, s_next_heap;

static const char* reset_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "other";
  }
}

void logs_line(uint8_t level, const char* text, size_t n) {
  static const char LEVEL[] = "?EWID";
  // Seconds since boot, then the local time once it is set. newlib nano has no %lld.
  char head[32];
  uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
  char lv = LEVEL[level < 5 ? level : 0];
  struct tm t;
  int h;
  if (clock_local(&t))
    h = snprintf(head, sizeof head, "%lu.%03u %02d:%02d:%02d %c ", (unsigned long)(ms / 1000), (unsigned)(ms % 1000),
                 t.tm_hour, t.tm_min, t.tm_sec, lv);
  else
    h = snprintf(head, sizeof head, "%lu.%03u --:--:-- %c ", (unsigned long)(ms / 1000), (unsigned)(ms % 1000), lv);
  if (h < 0) return;
  if (h >= (int)sizeof head) h = sizeof head - 1;
  taskENTER_CRITICAL(&s_mux);
  if (s_len + h + n + 1 <= BUF) {
    memcpy(s_buf + s_len, head, h);
    memcpy(s_buf + s_len + h, text, n);
    s_len += h + n;
    s_buf[s_len++] = '\n';
  } else {
    s_dropped++;
  }
  if (level <= 2) s_urgent = true;
  taskEXIT_CRITICAL(&s_mux);
}

void logs_note(uint8_t level, const char* fmt, ...) {
  char text[120];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(text, sizeof text, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  logs_line(level, text, n < (int)sizeof text ? n : sizeof text - 1);
}

// Link task: write what waits, every 5 s, at once after an error.
void logs_tick(int64_t now) {
  if (now >= s_next_heap) {
    if (s_next_heap)
      link_log(4, "heap %u free, %u lowest", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    s_next_heap = now + HEAP_EVERY_US;
  }
  if (!s_ready || (!s_urgent && now < s_next_flush)) return;
  s_next_flush = now + FLUSH_US;
  char out[BUF];
  taskENTER_CRITICAL(&s_mux);
  size_t n = s_len;
  uint32_t dropped = s_dropped;
  memcpy(out, s_buf, n);
  s_len = 0;
  s_dropped = 0;
  s_urgent = false;
  taskEXIT_CRITICAL(&s_mux);
  if ((!n && !dropped) || s_full) return;
  if (sd_info().state != fnx::sd_state::READY) {
    s_ready = false;  // the card went; the next mount starts a new file
    return;
  }
  FILE* f = fopen(s_path, "ab");
  if (!f) return;
  if (dropped) s_size += fprintf(f, "(%u lines dropped: the buffer was full)\n", (unsigned)dropped);
  s_size += fwrite(out, 1, n, f);
  if (s_size >= MAX_SIZE) {
    fputs("(log full: 256 KB)\n", f);
    s_full = true;
  }
  fclose(f);
}

// The numbered files in a folder ("<n>.txt"), highest first.
static int numbers(const char* dir, const char* ext, uint32_t* out, int cap) {
  int k = 0;
  DIR* d = opendir(dir);
  if (!d) return 0;
  size_t el = strlen(ext);
  while (struct dirent* e = readdir(d)) {
    char* end;
    unsigned long v = strtoul(e->d_name, &end, 10);
    if (end == e->d_name || strcmp(end, ext) || strlen(end) != el) continue;
    if (k < cap) {
      out[k++] = v;
    } else {
      int lo = 0;  // full: keep the highest numbers
      for (int i = 1; i < cap; i++)
        if (out[i] < out[lo]) lo = i;
      if (v > out[lo]) out[lo] = v;
    }
  }
  closedir(d);
  qsort(out, k, sizeof *out, [](const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? 1 : x > y ? -1 : 0;
  });
  return k;
}

// Copy a core dump from flash to the card, with a summary; erase it when copied.
static void copy_crash() {
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK || !size) return;
  esp_core_dump_summary_t* sum = new (std::nothrow) esp_core_dump_summary_t;
  char text[160] = "crash record (no summary)";
  if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
    int n = snprintf(text, sizeof text, "crash in task %.16s at 0x%08lx; backtrace", sum->exc_task,
                     (unsigned long)sum->exc_pc);
    for (uint32_t i = 0; i < sum->exc_bt_info.depth && i < 8 && n > 0 && n < (int)sizeof text - 12; i++)
      n += snprintf(text + n, sizeof text - n, " 0x%08lx", (unsigned long)sum->exc_bt_info.bt[i]);
  }
  delete sum;
  link_log(1, "%s (%u B dump)", text, (unsigned)size);
  mkdir(CRASH_DIR, 0775);
  char path[48];
  snprintf(path, sizeof path, "%s/%u.txt", CRASH_DIR, (unsigned)s_boot);
  if (FILE* f = fopen(path, "wb")) {
    fprintf(f, "%s\nfound at boot %u (the crash was in the boot before); firmware v%s\n", text, (unsigned)s_boot,
            esp_app_get_description()->version);
    fclose(f);
  }
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[2048]);
  if (!p || !buf || addr < p->address) return;
  snprintf(path, sizeof path, "%s/%u.elf", CRASH_DIR, (unsigned)s_boot);
  FILE* f = fopen(path, "wb");
  if (!f) return;
  bool ok = true;
  for (size_t off = 0; ok && off < size; off += 2048) {
    size_t n = size - off < 2048 ? size - off : 2048;
    ok = esp_partition_read(p, addr - p->address + off, buf.get(), n) == ESP_OK && fwrite(buf.get(), 1, n, f) == n;
  }
  ok = !fclose(f) && ok;
  if (ok) esp_core_dump_image_erase();  // copied: the next crash gets the space
}

// sd task, after a mount: pick this boot's log file, keep the 9 newest, copy a crash record.
void logs_mounted() {
  mkdir("/sd/fenixos", 0775);
  mkdir(LOG_DIR, 0775);
  uint32_t have[KEEP + 4];
  int k = numbers(LOG_DIR, ".txt", have, KEEP + 4);
  s_boot = k ? have[0] + 1 : 1;
  char path[48];
  for (int i = KEEP - 1; i < k; i++) {
    snprintf(path, sizeof path, "%s/%u.txt", LOG_DIR, (unsigned)have[i]);
    unlink(path);
  }
  k = numbers(CRASH_DIR, ".txt", have, KEEP + 4);
  for (int i = KEEP; i < k; i++) {
    snprintf(path, sizeof path, "%s/%u.txt", CRASH_DIR, (unsigned)have[i]);
    unlink(path);
    snprintf(path, sizeof path, "%s/%u.elf", CRASH_DIR, (unsigned)have[i]);
    unlink(path);
  }
  snprintf(s_path, sizeof s_path, "%s/%u.txt", LOG_DIR, (unsigned)s_boot);
  s_size = 0;
  s_full = false;
  if (FILE* f = fopen(s_path, "wb")) {
    s_size = fprintf(f, "fenixOS v%s, boot %u, reset: %s, heap %u free (%u lowest)\n",
                     esp_app_get_description()->version, (unsigned)s_boot, reset_name(esp_reset_reason()),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    fclose(f);
    s_ready = true;
  }
  copy_crash();
}

}  // namespace fx::sys
