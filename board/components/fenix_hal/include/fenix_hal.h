// Board hardware layer: LCD (ILI9341, DMA), touch (XPT2046), backlight, RGB LED, power.
// Pins and bus facts: pics/ANALYSIS.md, docs/board-info.md.
#pragma once
#include <stdint.h>

namespace fx::hal {

// ---- Display ----
void display_init();
// Rotation 0..3 as LovyanGFX: 0 portrait, 1 landscape, 2 portrait flipped, 3 landscape flipped.
void display_set_rotation(uint8_t r);
int display_width();
int display_height();
// Push an RGB565 big-endian (byte-swapped) block by DMA. Waits for the previous transfer first.
void display_push(int x, int y, int w, int h, const void* px);
void display_wait();
// Write colour bars, read them back over MISO and count wrong pixels (bus check).
int display_selftest();
// SPI write clock in Hz (default 40 MHz; set before display_init). A dev knob: nothing calls it.
void display_set_spi_hz(uint32_t hz);

// ---- Backlight (GPIO21, LEDC through LovyanGFX) ----
void backlight_set(uint8_t level);  // 0..255

// ---- Touch ----
// Touch in rotated screen coordinates. False when not touched.
bool touch_read(int16_t* x, int16_t* y);
// LovyanGFX 8-value calibration (from touch_calibrate). Applied at once.
void touch_set_calibration(const uint16_t cal[8]);
// Run LovyanGFX's 4-corner calibration on the LCD (blocks until done). Fills cal[8].
void touch_calibrate(uint16_t cal[8], uint16_t fg, uint16_t bg);

// ---- RGB LED (IO4 red, IO16 green, IO17 blue, active low) ----
void led_init();
void led_set(uint8_t r, uint8_t g, uint8_t b);  // 0..255 each

// ---- Power ----
// Backlight off, LED off, hold pins, light sleep until the touch IRQ (GPIO36) goes low, then
// restart (fenix_hal.cpp explains why not deep sleep).
[[noreturn]] void sleep_until_touch();
// Reset reason of this boot (esp_reset_reason_t as uint8); ESP_RST_DEEPSLEEP after a sleep wake.
uint8_t reset_reason();

// ---- Time kept across soft reset and sleep ----
// The system clock survives esp_restart and sleep, but not a power cut.
// A magic word in RTC no-init memory says whether it was ever set since power-on.
bool time_valid();
void time_mark_valid();
// Call once at boot: forgets the time after a power-on or EN reset.
void time_boot_check();

}  // namespace fx::hal
