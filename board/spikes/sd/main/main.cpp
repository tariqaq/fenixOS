// Spike SD: what card is in the microSD slot and how fast it is over SPI (VSPI pins).
// Reports on the UART0 console at 115200 (python tools/serial_read.py 40 --reset).
// Never formats. Writes one 1 MB temp file (FNXSPIKE.BIN) per clock and deletes it again.
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const int PIN_MOSI = 23, PIN_MISO = 19, PIN_CLK = 18, PIN_CS = 5;
static const char* MNT = "/sd";
static const size_t BUF = 32 * 1024;
static uint8_t* s_buf;  // DMA-capable

static unsigned long kb_per_s(uint32_t bytes, int64_t us) {
  return us > 0 ? (unsigned long)((uint64_t)bytes * 1000000 / 1024 / us) : 0;
}

static const char* part_name(uint8_t t) {
  switch (t) {
    case 0x01: return "FAT12";
    case 0x04: case 0x06: case 0x0E: return "FAT16";
    case 0x0B: case 0x0C: return "FAT32";
    case 0x07: return "exFAT or NTFS";
    case 0xEE: return "GPT protective";
    default: return "other";
  }
}

static void print_fs(const uint8_t* b) {
  if (!memcmp(b + 3, "EXFAT   ", 8)) {
    printf("file system: exFAT (cluster %u KB)\n", (unsigned)((1u << b[108]) * (1u << b[109]) / 1024));
  } else if (!memcmp(b + 3, "NTFS    ", 8)) {
    printf("file system: NTFS\n");
  } else if (!memcmp(b + 82, "FAT32   ", 8)) {
    uint16_t bps;
    memcpy(&bps, b + 11, 2);
    printf("file system: FAT32 (%u B sectors, cluster %u KB)\n", bps, (unsigned)(bps * b[13] / 1024));
  } else if (!memcmp(b + 54, "FAT", 3)) {
    printf("file system: FAT12/16\n");
  } else {
    printf("file system: unknown\n");
  }
}

static void boot_sector(sdmmc_card_t* card) {
  if (sdmmc_read_sectors(card, s_buf, 0, 1) != ESP_OK) return (void)printf("sector 0: read failed\n");
  char oem[9] = {};
  memcpy(oem, s_buf + 3, 8);
  bool sig = s_buf[510] == 0x55 && s_buf[511] == 0xAA;
  printf("sector 0: signature %s, OEM \"%s\"\n", sig ? "55AA" : "none", oem);
  if (!memcmp(oem, "EXFAT   ", 8) || !memcmp(oem, "NTFS    ", 8) || !memcmp(s_buf + 82, "FAT32   ", 8)) {
    printf("sector 0 is a volume boot record (no partition table)\n");
    return print_fs(s_buf);
  }
  uint32_t first = 0;
  for (int i = 0; i < 4; i++) {
    const uint8_t* e = s_buf + 446 + i * 16;
    if (!e[4]) continue;
    uint32_t start, n;
    memcpy(&start, e + 8, 4);
    memcpy(&n, e + 12, 4);
    printf("partition %d: type 0x%02X (%s), start sector %lu, %lu MB\n", i + 1, e[4], part_name(e[4]),
           (unsigned long)start, (unsigned long)(n / 2048));
    if (!first) first = start;
  }
  if (!first) return (void)printf("no partitions\n");
  if (sdmmc_read_sectors(card, s_buf, first, 1) != ESP_OK) return (void)printf("volume boot record: read failed\n");
  memcpy(oem, s_buf + 3, 8);
  printf("volume boot record: OEM \"%s\"\n", oem);
  print_fs(s_buf);
}

static void raw_read(sdmmc_card_t* card, int khz) {
  const uint32_t total = 2 * 1024 * 1024, per = BUF / 512;
  int64_t t = esp_timer_get_time();
  for (uint32_t s = 0; s < total / 512; s += per)
    if (sdmmc_read_sectors(card, s_buf, 2048 + s, per) != ESP_OK) return (void)printf("raw read failed\n");
  int64_t big = esp_timer_get_time() - t;
  uint32_t span = card->csd.capacity > 2048 * 1024 ? 1024 * 1024 * 2 / 8 : card->csd.capacity / 16;  // first 1 GB
  t = esp_timer_get_time();
  for (uint32_t i = 0; i < 256; i++)
    if (sdmmc_read_sectors(card, s_buf, 2048 + (i * 7919 % span) * 8, 8) != ESP_OK)
      return (void)printf("raw read failed\n");
  int64_t small = esp_timer_get_time() - t;
  printf("raw read @ %d kHz: 32 KB blocks %lu KB/s; 4 KB random %lu KB/s (%lu us each)\n", khz, kb_per_s(total, big),
         kb_per_s(256 * 4096, small), (unsigned long)(small / 256));
}

static void file_test() {
  char p[32];
  snprintf(p, sizeof p, "%s/FNXSPIKE.BIN", MNT);
  const uint32_t total = 1024 * 1024;
  memset(s_buf, 0xA5, BUF);
  FILE* f = fopen(p, "wb");
  if (!f) return (void)printf("file write: open failed (read-only or full?)\n");
  int64_t t = esp_timer_get_time();
  for (uint32_t n = 0; n < total; n += BUF) fwrite(s_buf, 1, BUF, f);
  fflush(f);
  fsync(fileno(f));
  fclose(f);
  int64_t w = esp_timer_get_time() - t;
  uint32_t got = 0;
  bool ok = true;
  f = fopen(p, "rb");
  t = esp_timer_get_time();
  for (size_t n; f && (n = fread(s_buf, 1, BUF, f)) > 0;) {
    got += n;
    for (size_t i = 0; i < n; i++) ok &= s_buf[i] == 0xA5;
  }
  int64_t r = esp_timer_get_time() - t;
  if (f) fclose(f);
  unlink(p);
  printf("file 1 MB: write %lu KB/s, read %lu KB/s, %s, deleted\n", kb_per_s(total, w), kb_per_s(got, r),
         ok && got == total ? "verified" : "MISMATCH");
}

static void run(int khz) {
  printf("\n--- SPI %d kHz ---\n", khz);
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = khz;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs = (gpio_num_t)PIN_CS;
  dev.host_id = SPI3_HOST;
  esp_vfs_fat_sdmmc_mount_config_t mc = {};
  mc.format_if_mount_failed = false;  // never format the user's card
  mc.max_files = 2;
  sdmmc_card_t* card = nullptr;
  int64_t t = esp_timer_get_time();
  esp_err_t err = esp_vfs_fat_sdspi_mount(MNT, &host, &dev, &mc, &card);
  unsigned long ms = (unsigned long)((esp_timer_get_time() - t) / 1000);
  static sdmmc_card_t probe;
  bool own_host = false;
  if (err != ESP_OK) {
    printf("mount: FAILED (%s) after %lu ms\n", esp_err_to_name(err), ms);
    // Talk to the card directly, to tell "no card" from "card without FAT".
    sdspi_dev_handle_t h;
    sdspi_host_init();
    own_host = true;
    card = nullptr;
    if (sdspi_host_init_device(&dev, &h) == ESP_OK) {
      host.slot = h;
      if (sdmmc_card_init(&host, &probe) == ESP_OK) card = &probe;
    }
    if (!card) {
      printf("card: does not answer (no card, bad contact, or wiring)\n");
      sdspi_host_deinit();
      spi_bus_free(SPI3_HOST);
      return;
    }
  } else {
    printf("mount: ok in %lu ms\n", ms);
  }
  sdmmc_card_print_info(stdout, card);
  uint64_t bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
  printf("capacity: %lu MB, %s, real clock %d kHz\n", (unsigned long)(bytes / (1024 * 1024)),
         card->ocr & (1u << 30) ? "SDHC/SDXC" : "SDSC", card->real_freq_khz);
  boot_sector(card);
  if (err == ESP_OK) {
    FATFS* fs;
    DWORD free_cl;
    if (f_getfree("0:", &free_cl, &fs) == FR_OK) {
      uint64_t cl = (uint64_t)fs->csize * 512;
      printf("FatFs: %s, cluster %lu KB, total %lu MB, free %lu MB\n",
             fs->fs_type == FS_FAT32 ? "FAT32" : fs->fs_type == FS_FAT16 ? "FAT16" : "FAT12",
             (unsigned long)(cl / 1024), (unsigned long)((uint64_t)(fs->n_fatent - 2) * cl / (1024 * 1024)),
             (unsigned long)((uint64_t)free_cl * cl / (1024 * 1024)));
    }
    FF_DIR d;
    FILINFO fi;
    int n = 0;
    if (f_opendir(&d, "0:/") == FR_OK) {
      while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) n++;
      f_closedir(&d);
    }
    printf("root entries: %d\n", n);
  }
  raw_read(card, khz);
  if (err == ESP_OK) {
    file_test();
    esp_vfs_fat_sdcard_unmount(MNT, card);
  } else if (own_host) {
    sdspi_host_deinit();
  }
  spi_bus_free(SPI3_HOST);
}

extern "C" void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  printf("\n=== fenixOS spike SD ===\n");
  s_buf = (uint8_t*)heap_caps_malloc(BUF, MALLOC_CAP_DMA);
  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_MOSI;
  bus.miso_io_num = PIN_MISO;
  bus.sclk_io_num = PIN_CLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = BUF;
  static const int CLOCKS[] = {20000, 40000};
  for (int khz : CLOCKS) {
    if (spi_bus_initialize(SPI3_HOST, &bus, SDSPI_DEFAULT_DMA) != ESP_OK) return (void)printf("spi bus init failed\n");
    run(khz);
  }
  printf("\n=== done ===\n");
}
