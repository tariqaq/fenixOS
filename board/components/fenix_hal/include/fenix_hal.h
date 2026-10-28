// Board hardware layer: LCD (ILI9341, DMA), touch (XPT2046), backlight.
// Pins and bus facts: pics/ANALYSIS.md, docs/board-info.md.
#pragma once
#include <stdint.h>

namespace fx::hal {

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
// SPI write clock in Hz (default 80 MHz; set before display_init).
void display_set_spi_hz(uint32_t hz);
// Backlight 0..255.
void backlight_set(uint8_t level);
// Touch in rotated screen coordinates. False when not touched.
bool touch_read(int16_t* x, int16_t* y);

}  // namespace fx::hal
