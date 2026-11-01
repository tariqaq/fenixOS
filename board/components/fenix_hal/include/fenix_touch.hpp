// XPT2046 touch on software (bit-banged) SPI, P50. The board wires LCD, touch and the microSD
// slot to three different SPI pin sets and the ESP32 has two SPI controllers for them: the LCD
// keeps SPI2, the SD card gets SPI3 (its own IO-MUX pins), and touch is clocked by hand.
// Measured in spike `board/spikes/sd`: one touch read ~350 us at ~2 MHz (the XPT2046 allows
// 2.5 MHz), done only while the panel is pressed (IRQ low). docs/board-info.md "SD card check".
//
// The frame and the median filter are LovyanGFX's Touch_XPT2046 (FreeBSD licence,
// https://github.com/lovyan03/LovyanGFX), which only drives hardware SPI.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <algorithm>
#include <string.h>

#include "driver/gpio.h"
#include "esp_cpu.h"
#include "soc/gpio_reg.h"

class TouchSoftXPT2046 : public lgfx::ITouch {
 public:
  TouchSoftXPT2046() {
    _cfg.x_min = 300;
    _cfg.x_max = 3900;
    _cfg.y_min = 400;
    _cfg.y_max = 3900;
  }

  bool init() override {
    _inited = false;
    gpio_config_t o = {};
    o.pin_bit_mask = 1ull << _cfg.pin_sclk | 1ull << _cfg.pin_mosi | 1ull << _cfg.pin_cs;
    o.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&o) != ESP_OK) return false;
    gpio_config_t i = {};
    i.pin_bit_mask = 1ull << _cfg.pin_miso;
    if (_cfg.pin_int >= 0) i.pin_bit_mask |= 1ull << _cfg.pin_int;
    i.mode = GPIO_MODE_INPUT;
    if (gpio_config(&i) != ESP_OK) return false;
    set(_cfg.pin_cs, true);
    set(_cfg.pin_sclk, false);
    _inited = true;
    return true;
  }
  void wakeup() override {}
  void sleep() override {}

  uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
    if (!_inited || count == 0) return 0;
    tp->size = 0;
    if (_cfg.pin_int >= 0 && gpio_get_level((gpio_num_t)_cfg.pin_int)) return 0;  // not pressed
    // 7 samples of Y, Z1, X, Z2; the last command powers the converter down (PENIRQ on).
    uint8_t d[57];
    memset(d, 0, 8);
    d[0] = 0x91;
    d[2] = 0xB1;
    d[4] = 0xD1;
    d[6] = 0xC1;
    d[56] = 0x80;
    memcpy(&d[8], d, 8);
    memcpy(&d[16], d, 16);
    memcpy(&d[32], d, 24);
    set(_cfg.pin_cs, false);
    for (uint8_t& b : d) b = xfer(b);
    set(_cfg.pin_cs, true);

    size_t ix = 0, iy = 0, iz = 0;
    uint_fast16_t xt[7], yt[7], zt[7];
    for (size_t j = 0; j < 7; ++j) {
      const uint8_t* s = &d[j * 8];
      int x = (s[5] << 8 | s[6]) >> 3;
      int y = (s[1] << 8 | s[2]) >> 3;
      int z = 0x3200 + y - x + (((s[3] << 8 | s[4]) - (s[7] << 8 | s[8])) >> 1);
      if (x > 128 && x <= 3968) xt[ix++] = x;
      if (y > 128 && y <= 3968) yt[iy++] = y;
      if (z > 0) zt[iz++] = z;
    }
    if (ix < 3 || iy < 3 || iz < 3) return 0;
    std::sort(xt, xt + ix);
    std::sort(yt, yt + iy);
    std::sort(zt, zt + iz);
    tp->x = xt[ix >> 1];
    tp->y = yt[iy >> 1];
    tp->size = zt[iz >> 1] >> 8;
    return tp->size ? 1 : 0;
  }

 private:
  static constexpr uint32_t HALF_CYCLES = 60;  // 250 ns at 240 MHz: ~2 MHz clock

  static void set(int pin, bool hi) {
    if (pin < 32)
      REG_WRITE(hi ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, 1u << pin);
    else
      REG_WRITE(hi ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG, 1u << (pin - 32));
  }
  static bool get(int pin) {
    return pin < 32 ? (REG_READ(GPIO_IN_REG) >> pin) & 1 : (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1;
  }
  static void wait() {
    uint32_t s = esp_cpu_get_cycle_count();
    while (esp_cpu_get_cycle_count() - s < HALF_CYCLES) {
    }
  }
  // SPI mode 0, MSB first: data out before the rising edge, the XPT2046 shifts on the falling one.
  uint8_t xfer(uint8_t out) const {
    uint8_t in = 0;
    for (int b = 7; b >= 0; b--) {
      set(_cfg.pin_mosi, (out >> b) & 1);
      wait();
      set(_cfg.pin_sclk, true);
      wait();
      in = (uint8_t)(in << 1 | get(_cfg.pin_miso));
      set(_cfg.pin_sclk, false);
    }
    return in;
  }
};
