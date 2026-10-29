// LovyanGFX config for the ESP32-2432S028R (CYD, PCB V1.4).
// Pins: pics/ANALYSIS.md. LCD on HSPI (SPI2), touch XPT2046 on its own VSPI (SPI3) bus.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_XPT2046 touch_;

 public:
  void set_spi_hz(uint32_t hz) {
    auto cfg = bus_.config();
    cfg.freq_write = hz;
    bus_.config(cfg);
  }

  LGFX() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      // 40 MHz. 80 MHz passed S1's colour-bar check but put bands at the wrong place on the real
      // panel under LVGL's small DMA writes (2026-10-29). The SPI clock is 80 MHz / (n + 1), so
      // the vendor demo's and FenixDeck's "55 MHz" was 40 MHz too.
      cfg.freq_write = 40000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;  // tied to EN
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    {
      auto cfg = touch_.config();
      // Default raw XPT2046 range. Replaced by the NVS calibration when present
      // (Settings > Display > Calibrate touch).
      cfg.x_min = 300;
      cfg.x_max = 3900;
      // Raw y runs right to left in landscape (measured 2026-10-29: ~3630 at the left edge,
      // ~210 at the right), so the range is reversed; with 200..3900 left and right were mirrored.
      cfg.y_min = 3900;
      cfg.y_max = 200;
      cfg.pin_int = 36;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI3_HOST;
      cfg.freq = 2500000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};
