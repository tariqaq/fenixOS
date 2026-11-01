// Spike SD: the microSD slot (SPI3, VSPI pins) next to touch on software SPI.
// Reports on the UART0 console at 115200 (python tools/serial_read.py 60 --reset).
//  1. Card info and layout. If the card is not in the SD Association layout (MBR, FAT32 partition
//     from sector 8192 = 4 MB, data area on a 4 MB boundary, 32 KB clusters), it is formatted so.
//     A card already in that layout is never formatted again.
//  2. Speed at 40 MHz: raw reads, a 1 MB file through stdio (4 KB buffer) and through FatFs.
//  3. Long file names: apps/io.github.tariqaq.morpheus.fxp written, listed, read, deleted.
//  4. Touch (XPT2046) on bit-banged SPI (pins 25/32/39/33, IRQ 36) while the SD card streams on
//     the other core: time per read, channel values, and any presses (X, Y) seen meanwhile.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "diskio_impl.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "soc/gpio_reg.h"

static const int SD_MOSI = 23, SD_MISO = 19, SD_CLK = 18, SD_CS = 5;
static const int TP_CLK = 25, TP_MOSI = 32, TP_MISO = 39, TP_CS = 33, TP_IRQ = 36;
static const uint32_t AU = 8192;  // 4 MB in 512 B sectors: partition start and data alignment
static const uint32_t CLUSTER = 32 * 1024;
static const char* MNT = "/sd";
static const size_t BUF = 32 * 1024;
static uint8_t* s_buf;  // DMA-capable

static unsigned long kb_per_s(uint32_t bytes, int64_t us) {
  return us > 0 ? (unsigned long)((uint64_t)bytes * 1000000 / 1024 / us) : 0;
}
static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

// ---- Card without a file system mounted (for the layout check and the format) ----

static sdmmc_host_t sd_host(sdspi_dev_handle_t* h) {
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.max_freq_khz = 40000;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs = (gpio_num_t)SD_CS;
  dev.host_id = SPI3_HOST;
  sdspi_host_init();
  if (sdspi_host_init_device(&dev, h) == ESP_OK) host.slot = *h;
  return host;
}

// Layout check: MBR with one FAT32 partition at AU, data area on an AU boundary, 32 KB clusters.
static bool sd_layout(sdmmc_card_t* c) {
  if (sdmmc_read_sectors(c, s_buf, 0, 1) != ESP_OK) return false;
  const uint8_t* e = s_buf + 446;
  printf("now: partition type 0x%02X from sector %lu\n", e[4], (unsigned long)le32(e + 8));
  if (s_buf[510] != 0x55 || e[4] != 0x0C || le32(e + 8) != AU) return false;
  if (sdmmc_read_sectors(c, s_buf, AU, 1) != ESP_OK || memcmp(s_buf + 82, "FAT32   ", 8)) return false;
  uint32_t bps = s_buf[11] | s_buf[12] << 8, spc = s_buf[13], rsv = s_buf[14] | s_buf[15] << 8, nfat = s_buf[16];
  uint32_t fatsz = le32(s_buf + 36), data = AU + rsv + nfat * fatsz;
  printf("now: FAT32, cluster %lu KB, data area at sector %lu (%s)\n", (unsigned long)(bps * spc / 1024),
         (unsigned long)data, data % AU ? "not on a 4 MB boundary" : "on a 4 MB boundary");
  return bps * spc == CLUSTER && data % AU == 0;
}

// FatFs sees the card from sector AU on, so f_mkfs (FM_SFD) builds the volume there.
static sdmmc_card_t* s_fc;
static DSTATUS d_init(BYTE) { return 0; }
static DSTATUS d_status(BYTE) { return 0; }
static DRESULT d_read(BYTE, BYTE* b, uint32_t s, UINT n) {
  return sdmmc_read_sectors(s_fc, b, s + AU, n) == ESP_OK ? RES_OK : RES_ERROR;
}
static DRESULT d_write(BYTE, const BYTE* b, uint32_t s, UINT n) {
  return sdmmc_write_sectors(s_fc, b, s + AU, n) == ESP_OK ? RES_OK : RES_ERROR;
}
static DRESULT d_ioctl(BYTE, BYTE cmd, void* b) {
  switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(DWORD*)b = s_fc->csd.capacity - AU; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD*)b = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD*)b = AU; return RES_OK;
  }
  return RES_PARERR;
}

static bool sd_format(sdmmc_card_t* c) {
  s_fc = c;
  static const ff_diskio_impl_t impl = {d_init, d_status, d_read, d_write, d_ioctl};
  ff_diskio_register(0, &impl);
  MKFS_PARM p = {};
  p.fmt = FM_FAT32 | FM_SFD;
  p.n_fat = 2;
  p.align = AU;
  p.au_size = CLUSTER;
  void* work = malloc(4096);
  int64_t t = esp_timer_get_time();
  FRESULT r = f_mkfs("0:", &p, work, 4096);
  free(work);
  ff_diskio_register(0, nullptr);
  if (r != FR_OK) return printf("format: f_mkfs failed (%d)\n", r), false;
  // The boot sector and its backup record the partition start (hidden sectors).
  if (sdmmc_read_sectors(c, s_buf, AU, 1) != ESP_OK) return false;
  uint32_t bk = s_buf[50] | s_buf[51] << 8;
  put32(s_buf + 28, AU);
  if (sdmmc_write_sectors(c, s_buf, AU, 1) != ESP_OK) return false;
  if (bk && sdmmc_read_sectors(c, s_buf, AU + bk, 1) == ESP_OK && s_buf[510] == 0x55) {
    put32(s_buf + 28, AU);
    sdmmc_write_sectors(c, s_buf, AU + bk, 1);
  }
  // MBR: one FAT32 (LBA) partition from AU to the end.
  memset(s_buf, 0, 512);
  put32(s_buf + 440, esp_random());
  uint8_t* e = s_buf + 446;
  e[1] = 0xFE, e[2] = 0xFF, e[3] = 0xFF, e[4] = 0x0C, e[5] = 0xFE, e[6] = 0xFF, e[7] = 0xFF;
  put32(e + 8, AU);
  put32(e + 12, c->csd.capacity - AU);
  s_buf[510] = 0x55, s_buf[511] = 0xAA;
  if (sdmmc_write_sectors(c, s_buf, 0, 1) != ESP_OK) return false;
  printf("format: done in %lu ms\n", (unsigned long)((esp_timer_get_time() - t) / 1000));
  return true;
}

static void check_and_format() {
  printf("\n--- 1. layout ---\n");
  sdspi_dev_handle_t h = -1;
  sdmmc_host_t host = sd_host(&h);
  static sdmmc_card_t c;
  if (h < 0 || sdmmc_card_init(&host, &c) != ESP_OK) {
    printf("card: does not answer\n");
  } else {
    sdmmc_card_print_info(stdout, &c);
    if (sd_layout(&c)) printf("layout: SD Association layout already, not formatted\n");
    else if (sd_format(&c)) sd_layout(&c);
  }
  if (h >= 0) sdspi_host_remove_device(h);
  sdspi_host_deinit();
}

// ---- Mounted tests ----

static sdmmc_card_t* s_card;

static bool mount() {
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.max_freq_khz = 40000;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs = (gpio_num_t)SD_CS;
  dev.host_id = SPI3_HOST;
  esp_vfs_fat_sdmmc_mount_config_t mc = {};
  mc.format_if_mount_failed = false;
  mc.max_files = 3;
  esp_err_t err = esp_vfs_fat_sdspi_mount(MNT, &host, &dev, &mc, &s_card);
  if (err != ESP_OK) return printf("mount: FAILED (%s)\n", esp_err_to_name(err)), false;
  FATFS* fs;
  DWORD fr;
  if (f_getfree("0:", &fr, &fs) == FR_OK)
    printf("mount: ok, %s, cluster %lu KB, free %lu MB, clock %d kHz\n", fs->fs_type == FS_FAT32 ? "FAT32" : "FAT",
           (unsigned long)(fs->csize / 2), (unsigned long)((uint64_t)fr * fs->csize / 2048), s_card->real_freq_khz);
  return true;
}

static void speed() {
  printf("\n--- 2. speed at 40 MHz ---\n");
  const uint32_t total = 1024 * 1024;
  int64_t t = esp_timer_get_time();
  for (uint32_t s = 0; s < 2 * total / 512; s += BUF / 512) sdmmc_read_sectors(s_card, s_buf, AU + s, BUF / 512);
  printf("raw read 32 KB blocks: %lu KB/s\n", kb_per_s(2 * total, esp_timer_get_time() - t));
  memset(s_buf, 0xA5, BUF);
  FILE* f = fopen("/sd/SPEED.BIN", "wb");
  if (!f) return (void)printf("open for write failed\n");
  t = esp_timer_get_time();
  for (uint32_t n = 0; n < total; n += BUF) fwrite(s_buf, 1, BUF, f);
  fflush(f);
  fsync(fileno(f));
  fclose(f);
  printf("file write (stdio): %lu KB/s\n", kb_per_s(total, esp_timer_get_time() - t));
  f = fopen("/sd/SPEED.BIN", "rb");
  uint32_t got = 0;
  t = esp_timer_get_time();
  for (size_t n; f && (n = fread(s_buf, 1, BUF, f)) > 0;) got += n;
  if (f) fclose(f);
  printf("file read (stdio, 4 KB buffer): %lu KB/s\n", kb_per_s(got, esp_timer_get_time() - t));
  FIL fil;
  UINT br;
  got = 0;
  t = esp_timer_get_time();
  if (f_open(&fil, "0:/SPEED.BIN", FA_READ) == FR_OK) {
    while (f_read(&fil, s_buf, BUF, &br) == FR_OK && br) got += br;
    f_close(&fil);
  }
  printf("file read (FatFs f_read, 32 KB): %lu KB/s\n", kb_per_s(got, esp_timer_get_time() - t));
  unlink("/sd/SPEED.BIN");
}

static void long_names() {
  printf("\n--- 3. long file names ---\n");
  const char* dir = "/sd/apps";
  const char* path = "/sd/apps/io.github.tariqaq.morpheus.fxp";
  mkdir(dir, 0775);
  FILE* f = fopen(path, "wb");
  if (!f) return (void)printf("create %s: FAILED\n", path);
  for (int i = 0; i < 2; i++) fwrite(s_buf, 1, BUF, f);
  fclose(f);
  struct stat st;
  printf("stat: %s, %ld B\n", stat(path, &st) == 0 ? "ok" : "FAILED", (long)st.st_size);
  DIR* d = opendir(dir);
  for (dirent* de; d && (de = readdir(d));) printf("listed: %s\n", de->d_name);
  if (d) closedir(d);
  printf("delete: %s\n", unlink(path) == 0 && rmdir(dir) == 0 ? "ok" : "FAILED");
}

// ---- Touch on software SPI (mode 0, ~2 MHz; the XPT2046 allows 2.5 MHz) ----

static inline void wait_cycles(uint32_t n) {
  uint32_t s = esp_cpu_get_cycle_count();
  while (esp_cpu_get_cycle_count() - s < n) {
  }
}
static const uint32_t HALF = 60;  // 250 ns at 240 MHz

static uint8_t tp_byte(uint8_t out) {
  uint8_t in = 0;
  for (int b = 7; b >= 0; b--) {
    REG_WRITE(out >> b & 1 ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG, 1u << (TP_MOSI - 32));
    wait_cycles(HALF);
    REG_WRITE(GPIO_OUT_W1TS_REG, 1u << TP_CLK);
    wait_cycles(HALF);
    in = in << 1 | (REG_READ(GPIO_IN1_REG) >> (TP_MISO - 32) & 1);
    REG_WRITE(GPIO_OUT_W1TC_REG, 1u << TP_CLK);
  }
  return in;
}

static void tp_xfer(uint8_t* d, size_t n) {
  REG_WRITE(GPIO_OUT1_W1TC_REG, 1u << (TP_CS - 32));
  for (size_t i = 0; i < n; i++) d[i] = tp_byte(d[i]);
  REG_WRITE(GPIO_OUT1_W1TS_REG, 1u << (TP_CS - 32));
}

// One channel, 12 bits; ends with the converter powered down.
static int tp_channel(uint8_t cmd) {
  uint8_t d[5] = {cmd, 0, 0, 0x80, 0};
  tp_xfer(d, 5);
  return (d[1] << 8 | d[2]) >> 3;
}

static void tp_init() {
  gpio_config_t o = {};
  o.pin_bit_mask = 1ull << TP_CLK | 1ull << TP_MOSI | 1ull << TP_CS;
  o.mode = GPIO_MODE_OUTPUT;
  gpio_config(&o);
  gpio_config_t i = {};
  i.pin_bit_mask = 1ull << TP_MISO | 1ull << TP_IRQ;
  i.mode = GPIO_MODE_INPUT;
  gpio_config(&i);
  REG_WRITE(GPIO_OUT1_W1TS_REG, 1u << (TP_CS - 32));
  REG_WRITE(GPIO_OUT_W1TC_REG, 1u << TP_CLK);
}

static volatile bool s_sd_busy;
static uint32_t s_sd_kbps;

static void sd_load(void*) {  // 4 MB written and read back while touch is read on the other core
  const uint32_t total = 4 * 1024 * 1024;
  memset(s_buf, 0x5A, BUF);
  int64_t t = esp_timer_get_time();
  FILE* f = fopen("/sd/LOAD.BIN", "wb");
  for (uint32_t n = 0; f && n < total; n += BUF) fwrite(s_buf, 1, BUF, f);
  if (f) fclose(f);
  FIL fil;
  UINT br;
  uint32_t got = 0;
  bool ok = true;
  if (f_open(&fil, "0:/LOAD.BIN", FA_READ) == FR_OK) {
    while (f_read(&fil, s_buf, BUF, &br) == FR_OK && br) {
      got += br;
      for (UINT i = 0; i < br; i++) ok &= s_buf[i] == 0x5A;
    }
    f_close(&fil);
  }
  s_sd_kbps = kb_per_s(total + got, esp_timer_get_time() - t);
  unlink("/sd/LOAD.BIN");
  printf("sd under load: 4 MB written + read, %lu KB/s, %s\n", (unsigned long)s_sd_kbps,
         ok && got == total ? "verified" : "MISMATCH");
  s_sd_busy = false;
  vTaskDelete(nullptr);
}

static void touch_test() {
  printf("\n--- 4. touch on software SPI, SD busy on the other core ---\n");
  tp_init();
  vTaskDelay(pdMS_TO_TICKS(5));
  bool idle_irq = gpio_get_level((gpio_num_t)TP_IRQ);  // before any conversion, as LovyanGFX checks it
  printf("IRQ before conversions: %s\n", idle_irq ? "high, not pressed" : "low, pressed");
  printf("idle: temp %d, vbat %d, aux %d, z1 %d (12-bit raw; IRQ %s)\n", tp_channel(0x87), tp_channel(0xA7),
         tp_channel(0xE7), tp_channel(0xB1), gpio_get_level((gpio_num_t)TP_IRQ) ? "high, not pressed" : "low, pressed");
  s_sd_busy = true;
  xTaskCreatePinnedToCore(sd_load, "sd_load", 8192, nullptr, 5, nullptr, 0);
  uint32_t reads = 0, presses = 0, tmin = 0, tmax = 0, tsum = 0, temp_min = 4095, temp_max = 0;
  int64_t end = esp_timer_get_time() + 10 * 1000000;
  while (s_sd_busy || esp_timer_get_time() < end) {
    // The same 57-byte frame LovyanGFX sends for one touch read.
    uint8_t d[57] = {};
    d[0] = 0x91, d[2] = 0xB1, d[4] = 0xD1, d[6] = 0xC1, d[56] = 0x80;
    memcpy(d + 8, d, 8);
    memcpy(d + 16, d, 16);
    memcpy(d + 32, d, 24);
    bool pressed = !gpio_get_level((gpio_num_t)TP_IRQ);
    int64_t t = esp_timer_get_time();
    tp_xfer(d, 57);
    uint32_t us = esp_timer_get_time() - t;
    tsum += us;
    tmin = reads ? (us < tmin ? us : tmin) : us;
    tmax = us > tmax ? us : tmax;
    reads++;
    int temp = tp_channel(0x87);
    temp_min = temp < (int)temp_min ? temp : temp_min;
    temp_max = temp > (int)temp_max ? temp : temp_max;
    if (pressed) {
      int x = (d[5] << 8 | d[6]) >> 3, y = (d[1] << 8 | d[2]) >> 3;
      if (presses++ % 10 == 0) printf("press: raw x %d, y %d\n", x, y);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  printf("touch: %lu reads of 57 B, %lu us avg (min %lu, max %lu); temp channel %lu..%lu; %lu pressed samples\n",
         (unsigned long)reads, (unsigned long)(tsum / reads), (unsigned long)tmin, (unsigned long)tmax,
         (unsigned long)temp_min, (unsigned long)temp_max, (unsigned long)presses);
}

extern "C" void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  printf("\n=== fenixOS spike SD 2 ===\n");
  s_buf = (uint8_t*)heap_caps_malloc(BUF, MALLOC_CAP_DMA);
  spi_bus_config_t bus = {};
  bus.mosi_io_num = SD_MOSI;
  bus.miso_io_num = SD_MISO;
  bus.sclk_io_num = SD_CLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = BUF;
  if (spi_bus_initialize(SPI3_HOST, &bus, SDSPI_DEFAULT_DMA) != ESP_OK) return (void)printf("spi bus init failed\n");
  check_and_format();
  if (!mount()) return;
  speed();
  long_names();
  touch_test();
  esp_vfs_fat_sdcard_unmount(MNT, s_card);
  printf("\n=== done ===\n");
}
