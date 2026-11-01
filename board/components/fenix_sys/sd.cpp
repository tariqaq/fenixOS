// microSD card (P50): SPI3 on the slot's own IO-MUX pins (CS 5, MOSI 23, CLK 18, MISO 19) at
// 40 MHz, FAT32 at /sd with long file names. Findings: docs/board-info.md "SD card check".
// Mounting runs in a short-lived task (the card takes ~75 ms, a fresh FAT32 scan longer), so
// boot and the UI never wait for it; the task ends when done and frees its stack.
// There is no card-detect pin: sd_probe() checks a mounted card is still there, or mounts one.
// The app store (store.cpp) scans the card's apps from the same task (P51).
#include <string.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "fenix_sys.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
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

static void unmount() {
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
  mc.max_files = 4;
  esp_err_t err = esp_vfs_fat_sdspi_mount(SD_PATH, &host, &dev, &mc, &s_card);
  if (err != ESP_OK) {
    s_card = nullptr;
    link_log(3, "sd: mount failed (0x%x)", (unsigned)err);
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
  uint64_t cl = (uint64_t)fs->csize * 512;
  set(fnx::sd_state::READY, (uint32_t)((uint64_t)(fs->n_fatent - 2) * cl / 1024), (uint32_t)(free_cl * cl / 1024),
      (uint16_t)s_card->real_freq_khz);
  store_sd_changed(true);
}

static void probe_task(void*) {
  if (s_card && sdmmc_get_status(s_card) != ESP_OK) {  // pulled out
    unmount();
    set(fnx::sd_state::NONE);
    store_sd_changed(false);
  }
  if (!s_card) mount();
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
