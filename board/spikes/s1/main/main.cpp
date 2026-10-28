// Spike S1: LVGL 9 + LovyanGFX on the CYD. Measures fps of a full-screen fade,
// a small animation, the full-push time and heap, for each draw-buffer size and rotation.
// Results print on the UART0 console (115200).
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "fenix_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#ifndef SPIKE_SPI_HZ
#define SPIKE_SPI_HZ 80000000
#endif

static uint32_t flushes, frames;
static int64_t push_us;

static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  int64_t t = esp_timer_get_time();
  fx::hal::display_push(a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), px);
  push_us += esp_timer_get_time() - t;
  flushes++;
  if (lv_display_flush_is_last(d)) frames++;
  lv_display_flush_ready(d);
}

static uint32_t tick_cb() { return esp_timer_get_time() / 1000; }

static void run_for(uint32_t ms) {
  int64_t end = esp_timer_get_time() + ms * 1000LL;
  while (esp_timer_get_time() < end) {
    uint32_t w = lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(w < 1 ? 1 : (w > 5 ? 5 : w)));
  }
}

static void measure(const char* name, uint32_t ms) {
  flushes = frames = 0;
  push_us = 0;
  int64_t t0 = esp_timer_get_time();
  run_for(ms);
  float s = (esp_timer_get_time() - t0) / 1e6f;
  printf("S1 %-22s fps=%5.1f flushes/frame=%4.1f push_ms/frame=%5.2f heap=%u min=%u\n", name, frames / s,
         frames ? (float)flushes / frames : 0.f, frames ? push_us / 1000.f / frames : 0.f,
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

static void opa_cb(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, v, 0); }
static void x_cb(void* o, int32_t v) { lv_obj_set_x((lv_obj_t*)o, v); }

static void scene(lv_display_t* disp, int lines, int rot) {
  fx::hal::display_set_rotation(rot);
  lv_display_set_resolution(disp, fx::hal::display_width(), fx::hal::display_height());
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_screen_load(scr);
  lv_obj_t* panel = lv_obj_create(scr);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  for (int i = 0; i < 6; i++) {
    lv_obj_t* t = lv_obj_create(panel);
    lv_obj_set_size(t, 88, 64);
    lv_obj_set_pos(t, 8 + (i % 3) * 96, 70 + (i / 3) * 72);
    lv_obj_set_style_radius(t, 14, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x4B48C9 + i * 0x101010), 0);
    lv_obj_t* l = lv_label_create(t);
    lv_label_set_text(l, "Tile");
  }
  run_for(300);
  char name[32];
  // Full-screen fade (opacity on a full-screen object: every frame redraws everything).
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, panel);
  lv_anim_set_exec_cb(&a, opa_cb);
  lv_anim_set_values(&a, 255, 0);
  lv_anim_set_duration(&a, 1000);
  lv_anim_set_playback_duration(&a, 1000);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
  snprintf(name, sizeof name, "fade l%d r%d", lines, rot);
  measure(name, 3000);
  lv_anim_delete(panel, opa_cb);
  lv_obj_set_style_opa(panel, 255, 0);
  // Scrim fade: a black full-screen rect over the content (cheaper than layer opacity).
  lv_obj_t* scrim = lv_obj_create(scr);
  lv_obj_remove_style_all(scrim);
  lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(scrim, lv_color_hex(0), 0);
  lv_obj_set_style_bg_opa(scrim, 255, 0);
  lv_anim_set_var(&a, scrim);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t*)o, v, 0); });
  lv_anim_start(&a);
  snprintf(name, sizeof name, "scrim l%d r%d", lines, rot);
  measure(name, 3000);
  lv_obj_delete(scrim);
  // Slide: the whole panel moves sideways (full redraw, no blending).
  lv_anim_set_var(&a, panel);
  lv_anim_set_exec_cb(&a, x_cb);
  lv_anim_set_values(&a, 0, 320);
  lv_anim_start(&a);
  snprintf(name, sizeof name, "slide l%d r%d", lines, rot);
  measure(name, 3000);
  lv_anim_delete(panel, x_cb);
  lv_obj_set_x(panel, 0);
  // Small animation: a 40x40 square moving.
  lv_obj_t* sq = lv_obj_create(scr);
  lv_obj_set_size(sq, 40, 40);
  lv_obj_set_y(sq, 10);
  lv_obj_set_style_bg_color(sq, lv_color_hex(0x0A84FF), 0);
  lv_anim_set_var(&a, sq);
  lv_anim_set_exec_cb(&a, x_cb);
  lv_anim_set_values(&a, 0, 200);
  lv_anim_set_var(&a, sq);
  lv_anim_set_exec_cb(&a, x_cb);
  lv_anim_start(&a);
  snprintf(name, sizeof name, "small l%d r%d", lines, rot);
  measure(name, 3000);
  lv_anim_delete(sq, x_cb);
  // Full invalidate: one full-screen render + push.
  frames = 0;
  int64_t t = esp_timer_get_time();
  lv_obj_invalidate(scr);
  lv_refr_now(disp);
  fx::hal::display_wait();
  printf("S1 full redraw l%d r%d: %.2f ms\n", lines, rot, (esp_timer_get_time() - t) / 1000.f);
  lv_obj_delete(scr);
}

extern "C" void app_main(void) {
  printf("S1 start heap=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
  fx::hal::display_set_spi_hz(SPIKE_SPI_HZ);
  fx::hal::display_init();
  printf("S1 spi=%d selftest bad=%d\n", SPIKE_SPI_HZ, fx::hal::display_selftest());
  fx::hal::backlight_set(200);
  lv_init();
  lv_tick_set_cb(tick_cb);
  printf("S1 after init heap=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
  const int sizes[] = {32};
  for (int lines : sizes) {
    size_t bytes = 320 * lines * 2;
    void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    lv_display_t* disp = lv_display_create(320, 240);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    scene(disp, lines, 1);
    scene(disp, lines, 0);
    lv_display_delete(disp);
    heap_caps_free(b1);
    heap_caps_free(b2);
  }
  printf("S1 done\n");
}
