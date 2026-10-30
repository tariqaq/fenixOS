// LVGL glue: display (2 DMA bands, RGB565 swapped), touch, backlight dimming, burn-in
// protection, stream hook, fps.
// Choices from spike S1 and P4/P29 profiling (docs/board-info.md): 320x40 bands, no LVGL OS layer,
// 16 ms refresh.
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "fenix_hal.h"
#include "fenix_sys.h"
#include "fenix_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace fx::ui {

static constexpr int BAND_LINES = 40;  // 2 x 25.6 KB; 80 lines gave 1.6 fps more for 51 KB (P29)

static lv_display_t* s_disp;
static uint8_t s_rot;

// ---- Stream hook ----
// Only frames on the PC's rate are encoded (STREAM_START fps); areas of the frames in between,
// and bands the TX buffer had no room for, are marked stale and redrawn for the stream once the
// screen is idle (or after STALE_MAX_US at the latest).
static constexpr int64_t STALE_MAX_US = 1000000;
static bool s_stream;
static int64_t s_stream_period_us = 100000, s_last_stream_frame;
static bool s_stream_frame;  // the frame being rendered goes to the stream
static bool s_stale;
static int64_t s_stale_since;
static lv_area_t s_stale_area;

static void mark_stale(const lv_area_t* a) {
  if (!s_stale) {
    s_stale_area = *a;
    s_stale = true;
    s_stale_since = esp_timer_get_time();
  } else {
    s_stale_area.x1 = LV_MIN(s_stale_area.x1, a->x1);
    s_stale_area.y1 = LV_MIN(s_stale_area.y1, a->y1);
    s_stale_area.x2 = LV_MAX(s_stale_area.x2, a->x2);
    s_stale_area.y2 = LV_MAX(s_stale_area.y2, a->y2);
  }
}

// ---- fps ----
// Animation fps: only frames that follow another within 100 ms count, so idle time does not
// pull the number down. Kept in 1 s buckets; fps_x10() covers the last 5 (one STATS period).
static int64_t s_last_frame, s_fps_t0;
static uint32_t s_int_n, s_int_us;
static uint32_t s_hist_n[5], s_hist_us[5];
static int s_fps_i;

static void refr_start_cb(lv_event_t*) {
  int64_t now = esp_timer_get_time();
  s_stream_frame = s_stream && now - s_last_stream_frame >= s_stream_period_us;
  if (s_stream_frame) s_last_stream_frame = now;
}

// Pixel shift offset (burn-in protection): the panel shows the picture moved by this much.
static int s_dx, s_dy;

static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  int w = lv_area_get_width(a), h = lv_area_get_height(a);
  hal::display_push(a->x1 + s_dx, a->y1 + s_dy, w, h, px);  // LovyanGFX clips the edge
  // The stream gets the picture unshifted.
  if (s_stream && !(s_stream_frame && sys::stream_rect(a->x1, a->y1, w, h, px))) mark_stale(a);
  if (lv_display_flush_is_last(d)) {
    int64_t now = esp_timer_get_time();
    if (now - s_last_frame < 100000) {
      s_int_n++;
      s_int_us += now - s_last_frame;
    }
    s_last_frame = now;
  }
  lv_display_flush_ready(d);
}

static uint32_t tick_cb() { return esp_timer_get_time() / 1000; }

// ---- Backlight ----
static uint8_t s_level = 200, s_dim_pct = 20;
static uint16_t s_dim_after_s = 60;
static bool s_dimmed;
static int s_bl_now = -1;

static void bl_exec(void*, int32_t v) {
  s_bl_now = v;
  hal::backlight_set(v);
}

static void bl_fade(int to, uint32_t ms, lv_anim_completed_cb_t done = nullptr) {
  lv_anim_delete(&s_bl_now, bl_exec);
  if (s_bl_now < 0) s_bl_now = 0;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &s_bl_now);
  lv_anim_set_exec_cb(&a, bl_exec);
  lv_anim_set_values(&a, s_bl_now, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  if (done) lv_anim_set_completed_cb(&a, done);
  lv_anim_start(&a);
}

static int dim_target() {
  int v = s_level * s_dim_pct / 100;
  return v < 4 ? 4 : v;
}

void backlight_config(uint8_t level, uint8_t dim_pct, uint16_t dim_after_s) {
  s_level = level;
  s_dim_pct = dim_pct;
  s_dim_after_s = dim_after_s;
  bl_fade(s_dimmed ? dim_target() : s_level, FX_MOTION_FAST_MS);
}

// ---- Burn-in protection (P21) ----
// The panel is a TN LCD: a picture held for hours leaves a faint ghost (image retention), seen
// where the Home clock sits (2026-10-29). The picture walks 1 px every SHIFT_S over a 5 x 5
// square (+-2 px; there and back, so every step is 1 px), and after REST_S without a touch the
// backlight fades out and the panel sleeps, so it stops driving the picture at all.
static constexpr uint32_t SHIFT_S = 60, REST_S = 600;
static constexpr int WALK = 25, WALK_CENTRE = 12;
static bool s_guard, s_rest, s_panel_asleep;
static int s_walk = WALK_CENTRE;
static int64_t s_shift_at;

static void shift_set(int dx, int dy) {
  if (dx == s_dx && dy == s_dy) return;
  s_dx = dx;
  s_dy = dy;
  // The picture now covers [dx, W + dx) x [dy, H + dy): blank the strip it left.
  int W = width(), H = height();
  if (dx > 0) hal::display_fill(0, 0, dx, H);
  if (dx < 0) hal::display_fill(W + dx, 0, -dx, H);
  if (dy > 0) hal::display_fill(0, 0, W, dy);
  if (dy < 0) hal::display_fill(0, H + dy, W, -dy);
  lv_obj_invalidate(lv_screen_active());
}

// Step `i` of the walk, 0..2 * (WALK - 1): rows of 5 left to right, then right to left, and back.
static void shift_walk(int i) {
  int k = i < WALK ? i : 2 * (WALK - 1) - i;
  int row = k / 5, c = k % 5;
  shift_set((row & 1 ? 4 - c : c) - 2, row - 2);
}

static void panel_sleep(lv_anim_t*) {
  if (!s_rest || s_panel_asleep) return;
  hal::display_sleep(true);
  s_panel_asleep = true;
}

static void rest_set(bool on) {
  s_rest = on;
  if (on) {
    bl_fade(0, FX_MOTION_SLOW_MS, panel_sleep);  // sleep once dark, so the blank panel never shows
    return;
  }
  if (s_panel_asleep) {
    hal::display_sleep(false);
    s_panel_asleep = false;
    lv_obj_invalidate(lv_screen_active());
  }
}

void burn_guard(bool on) {
  s_guard = on;
  s_shift_at = esp_timer_get_time();
  if (on) return;
  if (s_rest) {
    rest_set(false);
    bl_fade(s_dimmed ? dim_target() : s_level, FX_MOTION_FAST_MS);
  }
  s_walk = WALK_CENTRE;
  shift_set(0, 0);
}

bool dimmed() { return s_dimmed || s_rest; }
bool resting() { return s_rest; }
uint8_t backlight_now() { return s_bl_now < 0 ? 0 : s_bl_now; }

void backlight_fade_out() {
  bl_fade(0, FX_MOTION_SLOW_MS);
  int64_t end = esp_timer_get_time() + (FX_MOTION_SLOW_MS + 40) * 1000;
  while (esp_timer_get_time() < end) {
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  hal::backlight_set(0);
}

// ---- Touch ----
// The first touch while dimmed (or resting) only wakes the screen: it is swallowed until released.
static bool s_swallow;
static bool s_pressed;  // the last read was a press (touch or the PC's)

static void touch_cb(lv_indev_t*, lv_indev_data_t* d) {
  int16_t x, y;
  bool down = hal::touch_read(&x, &y);
  if (down) {
    // The panel shows the picture shifted; the PC's touches (below) are in picture coordinates.
    x = LV_CLAMP(0, x - s_dx, width() - 1);
    y = LV_CLAMP(0, y - s_dy, height() - 1);
  } else {
    down = sys::remote_touch(&x, &y);
  }
  if (down && (s_dimmed || s_rest)) {
    s_dimmed = false;
    if (s_rest) rest_set(false);
    s_swallow = true;
    lv_display_trigger_activity(s_disp);  // a swallowed press does not count as activity
    bl_fade(s_level, FX_MOTION_FAST_MS);
  }
  s_pressed = down;
  if (s_swallow) {
    if (!down) s_swallow = false;
    d->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (down) {
    d->point.x = x;
    d->point.y = y;
  }
  d->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool pressed() { return s_pressed; }

// ---- Core ----

void init(uint8_t rotation) {
  lv_init();
  lv_tick_set_cb(tick_cb);
  size_t bytes = 320 * BAND_LINES * 2;
  void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
  void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
  s_rot = rotation & 3;
  hal::display_set_rotation(s_rot);
  s_disp = lv_display_create(hal::display_width(), hal::display_height());
  lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_flush_cb(s_disp, flush_cb);
  lv_display_set_buffers(s_disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(s_disp, refr_start_cb, LV_EVENT_REFR_START, nullptr);
  lv_indev_t* in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, touch_cb);
  lv_obj_set_style_bg_color(lv_screen_active(), col(FX_COLOR_BG), 0);
  s_fps_t0 = esp_timer_get_time();
  s_bl_now = 0;
}

uint8_t rotation() { return s_rot; }
bool portrait() { return (s_rot & 1) == 0; }
int width() { return lv_display_get_horizontal_resolution(s_disp); }
int height() { return lv_display_get_vertical_resolution(s_disp); }
uint16_t fps_x10() {
  uint32_t n = 0, us = 0;
  for (int i = 0; i < 5; i++) {
    n += s_hist_n[i];
    us += s_hist_us[i];
  }
  return us ? (uint64_t)n * 10000000ULL / us : 0;
}

void set_rotation(uint8_t r) {
  r &= 3;
  if (r == s_rot) return;
  s_rot = r;
  hal::display_wait();
  hal::display_set_rotation(r);
  s_dx = s_dy = 0;  // the new picture covers the whole panel; the walk goes on from the centre
  s_walk = WALK_CENTRE;
  lv_display_set_resolution(s_disp, hal::display_width(), hal::display_height());
  nav_rebuild();
  lv_obj_invalidate(lv_screen_active());
}

void stream_enable(bool on, uint8_t fps) {
  s_stream = on;
  s_stream_period_us = 1000000 / (fps ? fps : 10);
  sys::stream_set_active(on);
  s_stale = false;
  if (on) stream_full_refresh();
}

void stream_full_refresh() {
  lv_area_t a{0, 0, (int32_t)width() - 1, (int32_t)height() - 1};
  mark_stale(&a);
}

uint32_t step() {
  uint32_t wait = lv_timer_handler();
  int64_t now = esp_timer_get_time();
  // Redraw stale areas for the stream when the screen is idle (or they waited too long),
  // and the TX buffer has drained enough. That redraw is always a stream frame.
  if (s_stream && s_stale) {
    bool idle = now - s_last_frame > s_stream_period_us;
    size_t bytes = (size_t)lv_area_get_width(&s_stale_area) * lv_area_get_height(&s_stale_area) * 2;
    if ((idle || now - s_stale_since > STALE_MAX_US) && sys::stream_has_room(bytes < 8192 ? bytes : 8192)) {
      lv_area_t a = s_stale_area;
      s_stale = false;
      s_last_stream_frame = 0;
      lv_obj_invalidate_area(lv_screen_active(), &a);
    }
  }
  // Auto-dim.
  uint32_t idle_ms = lv_display_get_inactive_time(s_disp);
  if (!s_dimmed && s_dim_after_s && idle_ms > s_dim_after_s * 1000u) {
    s_dimmed = true;
    bl_fade(dim_target(), FX_MOTION_SLOW_MS);
  }
  // Burn-in protection: rest after REST_S idle; otherwise shift every SHIFT_S.
  if (s_guard && !s_rest && idle_ms > REST_S * 1000u) rest_set(true);
  if (s_guard && !s_rest && now - s_shift_at >= SHIFT_S * 1000000LL) {
    s_shift_at = now;
    s_walk = (s_walk + 1) % (2 * (WALK - 1));
    shift_walk(s_walk);
  }
  if (now - s_fps_t0 >= 1000000) {
    s_hist_n[s_fps_i] = s_int_n;
    s_hist_us[s_fps_i] = s_int_us;
    s_fps_i = (s_fps_i + 1) % 5;
    s_int_n = s_int_us = 0;
    s_fps_t0 = now;
  }
  return wait < 1 ? 1 : (wait > 10 ? 10 : wait);
}

}  // namespace fx::ui
