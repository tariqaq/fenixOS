#include "fenix_hal.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "fenix_lgfx.hpp"

namespace fx::hal {

static LGFX lcd;

// ---- Display ----

void display_init() {
  gpio_hold_dis(GPIO_NUM_21);  // released after a deep-sleep hold
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

// ---- Backlight ----

void backlight_set(uint8_t level) { lcd.setBrightness(level); }

// ---- Touch ----

bool touch_read(int16_t* x, int16_t* y) {
  lgfx::touch_point_t tp;
  if (lcd.getTouch(&tp, 1) == 0) return false;
  *x = tp.x;
  *y = tp.y;
  return true;
}

void touch_set_calibration(const uint16_t cal[8]) {
  uint16_t c[8];
  for (int i = 0; i < 8; i++) c[i] = cal[i];
  lcd.setTouchCalibrate(c);
}

void touch_calibrate(uint16_t cal[8], uint16_t fg, uint16_t bg) {
  lcd.waitDMA();
  lcd.endWrite();
  lcd.calibrateTouch(cal, fg, bg, 12);
  lcd.startWrite();
}

// ---- RGB LED ----

static const gpio_num_t LED_PINS[3] = {GPIO_NUM_4, GPIO_NUM_16, GPIO_NUM_17};

void led_init() {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_HIGH_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_8_BIT;
  t.timer_num = LEDC_TIMER_0;
  t.freq_hz = 5000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&t);
  for (int i = 0; i < 3; i++) {
    gpio_hold_dis(LED_PINS[i]);
    ledc_channel_config_t c = {};
    c.gpio_num = LED_PINS[i];
    c.speed_mode = LEDC_HIGH_SPEED_MODE;
    c.channel = static_cast<ledc_channel_t>(i);
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 255;  // active low: 255 = off
    c.flags.output_invert = 0;
    ledc_channel_config(&c);
  }
}

void led_set(uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t v[3] = {r, g, b};
  for (int i = 0; i < 3; i++) {
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, static_cast<ledc_channel_t>(i), 255 - v[i]);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, static_cast<ledc_channel_t>(i));
  }
}

// ---- Power ----

void deep_sleep_until_touch() {
  lcd.waitDMA();
  lcd.setBrightness(0);
  lcd.sleep();
  // Backlight and LED pins: drive "off" and hold through deep sleep.
  gpio_reset_pin(GPIO_NUM_21);
  gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_21, 0);
  gpio_hold_en(GPIO_NUM_21);
  for (gpio_num_t p : LED_PINS) {
    gpio_reset_pin(p);
    gpio_set_direction(p, GPIO_MODE_OUTPUT);
    gpio_set_level(p, 1);
    gpio_hold_en(p);
  }
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);  // XPT2046 PENIRQ, low while touched
  esp_deep_sleep_start();
}

uint8_t reset_reason() { return static_cast<uint8_t>(esp_reset_reason()); }

bool woke_from_sleep() { return esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED; }

// ---- Time flag ----

static RTC_NOINIT_ATTR uint32_t s_time_magic;
static constexpr uint32_t TIME_MAGIC = 0xF3E1C0DE;

bool time_valid() { return s_time_magic == TIME_MAGIC; }

void time_mark_valid() { s_time_magic = TIME_MAGIC; }

void time_boot_check() {
  // Power-on and EN-pin resets (the PC's RTS line) both report ESP_RST_POWERON;
  // the RTC clock restarts from 0 then, so the stored time is gone.
  if (esp_reset_reason() == ESP_RST_POWERON) s_time_magic = 0;
}

}  // namespace fx::hal
