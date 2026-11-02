// microSD card (P50): SPI3 on the slot's own IO-MUX pins (CS 5, MOSI 23, CLK 18, MISO 19) at
// 40 MHz, FAT32 at /sd with long file names. Findings: docs/board-info.md "SD card check".
// Mounting runs in a short-lived task (the card takes ~75 ms, a fresh FAT32 scan longer), so
// boot and the UI never wait for it; the task ends when done and frees its stack.
// There is no card-detect pin: sd_probe() checks a mounted card is still there, or mounts one.
// The app store (store.cpp) scans the card's apps from the same task (P51).
#include <string.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "fenix_sys.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

namespace fx::sys {

static constexpr int PIN_MOSI = 23, PIN_MISO = 19, PIN_CLK = 18, PIN_CS = 5;
static constexpr int CLOCK_KHZ = 40000;  // the highest ESP32 step (80 / 2) under the card's 50 MHz

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static fnx::SdState s_st;  // under s_mux
static sdmmc_card_t* s_card;
static bool s_bus;
static volatile bool s_busy;  // a probe task is running
static bool s_fail_logged;    // probe task only: a failed mount was logged since the last good one
// Held while s_card may change (probe task) or is checked (sd_tick). Made on first use: the link
// task can probe (HELLO) before sd_start runs.
static SemaphoreHandle_t lock() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}

static void report(const fnx::SdState& s) {
  if (!link_connected()) return;
  uint8_t b[16];
  fnx::Writer w(b, sizeof b);
  fnx::write_sd_state(w, s);
  link_send(fnx::ty::SD_STATE, b, w.size());
}

static void set(uint8_t state, uint32_t total_kb = 0, uint32_t free_kb = 0, uint16_t khz = 0) {
  fnx::SdState c{state, total_kb, free_kb, khz};
  portENTER_CRITICAL(&s_mux);
  bool changed = s_st.state != c.state || s_st.total_kb != c.total_kb || s_st.free_kb != c.free_kb ||
                 s_st.clock_khz != c.clock_khz;
  s_st = c;
  portEXIT_CRITICAL(&s_mux);
  if (!changed) return;
  report(c);
  link_post(LinkEv::SD_CHANGED);
}

// FatFs clears a new folder's cluster through a heap buffer of up to 32 KB (ff.c dir_clear,
// MAX_MALLOC); with this card's 32 KB clusters a mkdir took the free heap from 63 to 29 KB for a
// moment (P53, measured), enough to starve an LVGL allocation on the UI task. Refusing its
// power-of-two asks over 4 KB makes it halve down to 4 KB. Other asks pass: the VFS context at
// mount is ~6 KB and not a power of two (refusing it failed the mount with ESP_ERR_NO_MEM);
// f_write_zero_mem (growing a file with ftruncate, unused here) would fail instead of halving.
extern "C" void* __real_ff_memalloc(unsigned msize);
extern "C" void* __wrap_ff_memalloc(unsigned msize) {
  bool clear_buf = msize > 4096 && !(msize & (msize - 1));
  return clear_buf ? nullptr : __real_ff_memalloc(msize);
}

static void unmount() {
  fonts_release();  // the UI's CJK font file (P54)
  if (s_card) esp_vfs_fat_sdcard_unmount(SD_PATH, s_card);
  s_card = nullptr;
}

static void mount() {
  if (!s_bus) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = PIN_MOSI;
    bus.miso_io_num = PIN_MISO;
    bus.sclk_io_num = PIN_CLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    // The LCD (LovyanGFX, SPI2) takes DMA channel 1. On the ESP32, IDF v5.5.5's SPI_DMA_CH_AUTO
    // then failed with ESP_ERR_NOT_FOUND while channel 2 by number worked (measured, P50; cause
    // not traced), so fall back to channel 2 by number, then 1.
    esp_err_t e = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e == ESP_ERR_NOT_FOUND) e = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH2);
    if (e == ESP_ERR_NOT_FOUND) e = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH1);
    s_bus = e == ESP_OK;
    if (!s_bus) {
      link_log(2, "sd: SPI bus failed (0x%x)", (unsigned)e);
      return set(fnx::sd_state::FAILED);
    }
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = CLOCK_KHZ;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs = (gpio_num_t)PIN_CS;
  dev.host_id = SPI3_HOST;
  esp_vfs_fat_sdmmc_mount_config_t mc = {};
  mc.format_if_mount_failed = false;  // never format on its own
  // Open at once, worst case (P53): a file read and a file write from the PC, a firmware part, an
  // app install or an opened package, the log, a font (P54). 552 B each.
  mc.max_files = 6;
  int64_t t0 = esp_timer_get_time();
  esp_err_t err = esp_vfs_fat_sdspi_mount(SD_PATH, &host, &dev, &mc, &s_card);
  if (err != ESP_OK) {
    s_card = nullptr;
    // Logged once until a mount works again: sd_tick retries every 3 s while card apps are
    // missing (P57), and the time says what a retry costs.
    if (!s_fail_logged)
      link_log(3, "sd: mount failed (0x%x, %d ms)", (unsigned)err, (int)((esp_timer_get_time() - t0) / 1000));
    s_fail_logged = true;
    // A card that answered but has no FAT (exFAT, NTFS, blank) fails the mount, not the init.
    return set(err == ESP_FAIL ? fnx::sd_state::NO_FS : fnx::sd_state::NONE);
  }
  FATFS* fs;
  DWORD free_cl = 0;
  FRESULT fr = f_getfree("0:", &free_cl, &fs);
  if (fr != FR_OK) {
    link_log(2, "sd: free space failed (%d)", (int)fr);
    unmount();
    return set(fnx::sd_state::FAILED);
  }
  s_fail_logged = false;
  uint64_t cl = (uint64_t)fs->csize * 512;
  set(fnx::sd_state::READY, (uint32_t)((uint64_t)(fs->n_fatent - 2) * cl / 1024), (uint32_t)(free_cl * cl / 1024),
      (uint16_t)s_card->real_freq_khz);
  logs_mounted();  // this boot's log file, a crash record (P53)
  logs_note(3, "sd: card mounted, %u MB free of %u", (unsigned)(free_cl * cl / 1048576),
            (unsigned)((uint64_t)(fs->n_fatent - 2) * cl / 1048576));
  store_sd_changed(true);
  ota_scan_sd();     // a firmware package on the card (P52)
  backup_mounted();  // offers the card's settings backup to an empty board, or writes one (P53)
}

static void probe_task(void*) {
  xSemaphoreTake(lock(), portMAX_DELAY);
  if (s_card && sdmmc_get_status(s_card) != ESP_OK) {  // pulled out
    unmount();
    set(fnx::sd_state::NONE);
    logs_note(2, "sd: card removed");
    store_sd_changed(false);
    ota_scan_sd();
  }
  if (!s_card) mount();
  xSemaphoreGive(lock());
  s_busy = false;
  vTaskDelete(nullptr);
}

void sd_probe() {
  if (s_busy) return;
  s_busy = true;
  // FatFs mount and f_getfree need ~3 KB of stack, the app scan (512 B package head, paths) ~2 KB
  // more; long names are on the heap. The stack is freed when the task ends.
  if (xTaskCreatePinnedToCore(probe_task, "sd", 6144, nullptr, 2, nullptr, 0) != pdPASS) s_busy = false;
}

void sd_start() { sd_probe(); }

// A pulled card is noticed within CHECK_US (P57). There is no card-detect pin, and an open app
// reads nothing from the card, so before this nothing touched it until Settings › Storage or an
// install. CMD13 is one short command; the SPI driver takes the bus for each whole command
// (sdspi_host.c, spi_device_acquire_bus), so it cannot cut into another task's read or write.
// With no card mounted, a mount is tried every RETRY_US, but only while apps on a card are
// listed as missing (their tiles are faded): a card put back brings them back on its own.
static constexpr int64_t CHECK_US = 2 * 1000000, RETRY_US = 3 * 1000000;

void sd_tick(int64_t now) {
  static int64_t next_check, next_retry;
  if (s_busy) return;
  if (s_card) {
    if (now < next_check) return;
    next_check = now + CHECK_US;
    if (xSemaphoreTake(lock(), 0) != pdTRUE) return;
    bool gone = s_card && sdmmc_get_status(s_card) != ESP_OK;
    xSemaphoreGive(lock());
    if (gone) sd_probe();  // unmounts, SD_CHANGED, the store marks the card's apps missing
  } else if (now >= next_retry) {
    next_retry = now + RETRY_US;
    if (store_card_missing()) sd_probe();
  }
}

SdInfo sd_info() {
  portENTER_CRITICAL(&s_mux);
  SdInfo r = s_st;
  portEXIT_CRITICAL(&s_mux);
  return r;
}

void sd_update_free() {
  uint64_t total = 0, free = 0;
  SdInfo s = sd_info();
  if (s.state != fnx::sd_state::READY || esp_vfs_fat_info(SD_PATH, &total, &free) != ESP_OK) return;
  set(s.state, s.total_kb, (uint32_t)(free / 1024), s.clock_khz);
}

void sd_report() {
  SdInfo s = sd_info();
  report(s);
  if (s.state != fnx::sd_state::READY) sd_probe();  // a card put in since boot is found when a PC connects
}

}  // namespace fx::sys
