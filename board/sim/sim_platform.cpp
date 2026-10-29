// Simulator platform: the virtual clock, IDF stubs that need a body, and fx::hal on a
// framebuffer. Rotation follows LovyanGFX on the board: odd = landscape 320 × 240.
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "esp_system.h"
#include "esp_timer.h"
#include "fenix_hal.h"
#include "freertos/task.h"
#include "sim.h"

namespace sim {

static int64_t s_now;
bool realtime;
void (*pump)();

int64_t now_us() { return s_now; }
void advance_us(int64_t us) { s_now += us; }

static uint8_t s_rot = 1;
static std::vector<uint16_t> s_px(320 * 240);
static uint8_t s_bl;
static bool s_down, s_seen, s_up_pending;
static int16_t s_tx, s_ty;

int width() { return (s_rot & 1) ? 320 : 240; }
int height() { return (s_rot & 1) ? 240 : 320; }
const std::vector<uint16_t>& pixels() { return s_px; }
uint8_t backlight() { return s_bl; }

// A quick click can press and release between two LVGL reads; the press is then kept until
// LVGL has read it once, so no tap is lost.
void touch(bool down, int x, int y) {
  if (down) {
    if (!s_down) s_seen = false;
    s_down = true;
    s_up_pending = false;
    s_tx = (int16_t)x;
    s_ty = (int16_t)y;
  } else if (s_down && !s_seen) {
    s_up_pending = true;
  } else {
    s_down = false;
  }
}

}  // namespace sim

extern "C" int64_t esp_timer_get_time(void) { return sim::now_us(); }

extern "C" void vTaskDelay(TickType_t ticks) {
  sim::advance_us((int64_t)ticks * 1000);
#ifdef _WIN32
  if (sim::realtime) Sleep(ticks);
#endif
  if (sim::pump) sim::pump();
}

extern "C" void esp_restart(void) {
  printf("board restarted (Session > Restart); the simulator stops here\n");
  exit(0);
}

namespace fx::hal {

void display_init() {}
void display_set_rotation(uint8_t r) {
  sim::s_rot = r & 3;
  sim::s_px.assign((size_t)sim::width() * sim::height(), 0);
}
int display_width() { return sim::width(); }
int display_height() { return sim::height(); }

// LVGL renders RGB565 byte-swapped for the panel's SPI order; store it native.
void display_push(int x, int y, int w, int h, const void* px) {
  const uint8_t* p = static_cast<const uint8_t*>(px);
  int W = sim::width(), H = sim::height();
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++, p += 2)
      if (x + i < W && y + j < H) sim::s_px[(size_t)(y + j) * W + x + i] = (uint16_t)(p[0] << 8 | p[1]);
}
void display_wait() {}
int display_selftest() { return 0; }
void display_set_spi_hz(uint32_t) {}
void backlight_set(uint8_t level) { sim::s_bl = level; }

bool touch_read(int16_t* x, int16_t* y) {
  if (!sim::s_down) return false;
  *x = sim::s_tx;
  *y = sim::s_ty;
  sim::s_seen = true;
  if (sim::s_up_pending) sim::s_down = sim::s_up_pending = false;
  return true;
}
void touch_set_calibration(const uint16_t[8]) {}
void touch_calibrate(uint16_t cal[8], uint16_t, uint16_t) {
  for (int i = 0; i < 8; i++) cal[i] = 0;
}

void led_init() {}
void led_set(uint8_t, uint8_t, uint8_t) {}

[[noreturn]] void deep_sleep_until_touch() {
  printf("board shut down (Session > Shut down); the simulator stops here\n");
  exit(0);
}
uint8_t reset_reason() { return 1; }
bool woke_from_sleep() { return false; }
bool time_valid() { return true; }
void time_mark_valid() {}
void time_boot_check() {}

}  // namespace fx::hal
