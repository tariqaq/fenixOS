#include "fenix_hal.h"

#include "fenix_lgfx.hpp"

namespace fx::hal {

static LGFX lcd;

void display_init() {
  lcd.init();
  lcd.setColorDepth(16);
  lcd.fillScreen(0);
  lcd.startWrite();  // keep the SPI bus claimed; LVGL is the only writer
}

void display_set_rotation(uint8_t r) {
  lcd.waitDMA();
  lcd.setRotation(r & 3);
}

int display_width() { return lcd.width(); }
int display_height() { return lcd.height(); }

void display_push(int x, int y, int w, int h, const void* px) {
  lcd.waitDMA();
  lcd.pushImageDMA(x, y, w, h, static_cast<const lgfx::swap565_t*>(px));
}

void display_wait() { lcd.waitDMA(); }

void display_set_spi_hz(uint32_t hz) { lcd.set_spi_hz(hz); }

int display_selftest() {
  static const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xA554, 0x1234, 0x8410};
  lcd.waitDMA();
  int w = lcd.width(), h = lcd.height(), bad = 0;
  for (int i = 0; i < 8; i++) lcd.fillRect(i * w / 8, 0, w / 8, h, colors[i]);
  for (int i = 0; i < 8; i++) {
    for (int y = 0; y < h; y += 37) {
      uint16_t c = lcd.readPixel(i * w / 8 + 3, y);
      if (c != colors[i]) bad++;
    }
  }
  return bad;
}

void backlight_set(uint8_t level) { lcd.setBrightness(level); }

bool touch_read(int16_t* x, int16_t* y) {
  lgfx::touch_point_t tp;
  if (lcd.getTouch(&tp, 1) == 0) return false;
  *x = tp.x;
  *y = tp.y;
  return true;
}

}  // namespace fx::hal
