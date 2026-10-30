// Shell: boot screen, link events, board state reporting.
#include <string.h>

#include "esp_app_desc.h"
#include "esp_timer.h"
#include "fenix_proto.h"
#include "shell.h"

namespace fx::apps {

using namespace fx::ui;

static Shell s_shell;
Shell& shell() { return s_shell; }

void refresh_if_top(Builder b) {
  if (nav_top() == b) nav_rebuild();
}

uint8_t current_screen() {
  if (sheet_is_open()) return fnx::screen::SESSION;
  Builder t = nav_top();
  if (t == boot_screen) return fnx::screen::BOOT;
  if (t == home_screen) return fnx::screen::HOME;
  if (t == stream_screen) return fnx::screen::STREAM;
  if (t == app_screen) return fnx::screen::APP;
  return fnx::screen::SETTINGS;
}

// ---- Boot: wordmark, version, a white line sweeping over a grey track ----

static constexpr int TRACK_W = 120, LINE_W = 36;
static constexpr uint32_t BOOT_MS = 1500;
static int64_t s_boot_until;

static void sweep_exec(void* o, int32_t v) { lv_obj_set_x((lv_obj_t*)o, v); }

void boot_screen(lv_obj_t* scr) {
  lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(scr, "fenixOS", &fx_font_title_l, FX_COLOR_LABEL);
  char v[40];
  snprintf(v, sizeof v, "v%s", esp_app_get_description()->version);
  lv_obj_t* ver = text(scr, v, &fx_font_caption, FX_COLOR_LABEL_2);
  lv_obj_set_style_margin_top(ver, 4, 0);
  lv_obj_t* track = box(scr);
  lv_obj_set_size(track, TRACK_W, 3);
  lv_obj_set_style_margin_top(track, 18, 0);
  lv_obj_set_style_radius(track, 2, 0);
  lv_obj_set_style_bg_color(track, col(FX_COLOR_SURFACE_3), 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_clip_corner(track, true, 0);
  lv_obj_t* line = box(track);
  lv_obj_set_size(line, LINE_W, 3);
  lv_obj_set_style_radius(line, 2, 0);
  lv_obj_set_style_bg_color(line, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, line);
  lv_anim_set_exec_cb(&a, sweep_exec);
  lv_anim_set_values(&a, -LINE_W, TRACK_W);
  lv_anim_set_duration(&a, 900);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_repeat_delay(&a, 100);
  lv_anim_start(&a);
}

void start() {
  s_boot_until = esp_timer_get_time() + BOOT_MS * 1000;
  nav_root(boot_screen, false);
}

// ---- Link events ----

static void send_stream_info() {
  uint8_t b[5];
  fnx::Writer w(b, sizeof b);
  w.u16(width());
  w.u16(height());
  w.u8(rotation());
  sys::link_send(fnx::ty::STREAM_INFO, b, w.size());
}

// Tell the PC whether the board streams (P14): the same STREAM_START / STREAM_STOP the PC sends,
// so a start or stop on either side shows on both.
static void report_stream() {
  if (!s_shell.connected) return;
  uint8_t fps = s_shell.stream_fps;
  if (s_shell.streaming) sys::link_send(fnx::ty::STREAM_START, &fps, 1);
  else sys::link_send(fnx::ty::STREAM_STOP, &fps, 0);
}

static void stream_set(bool on, uint8_t fps) {
  s_shell.streaming = on;
  s_shell.stream_fps = fps;
  report_stream();
  if (on) send_stream_info();
  stream_enable(on, fps);
  refresh_if_top(stream_screen);
}

void on_link(const sys::LinkEvent& e) {
  switch (e.type) {
    case sys::LinkEv::CONNECTED:
      s_shell.connected = true;
      led_pulse_connected();
      break;
    case sys::LinkEv::DISCONNECTED:
      s_shell.connected = false;
      if (s_shell.streaming) stream_set(false, 0);
      break;
    case sys::LinkEv::TIME_SET:
      break;  // home_tick picks the new time up
    case sys::LinkEv::STREAM_START:
      stream_set(true, e.value);
      break;
    case sys::LinkEv::STREAM_STOP:
      stream_set(false, 0);
      break;
    case sys::LinkEv::STREAM_SYNC:
      report_stream();  // the PC asks at every connection whether the board streams (P15)
      if (s_shell.streaming) {
        send_stream_info();
        stream_full_refresh();
      }
      break;
    default:
      break;
  }
  app_on_link(e);
  if (e.type == sys::LinkEv::CONNECTED || e.type == sys::LinkEv::DISCONNECTED) {
    refresh_if_top(stream_screen);
    if (nav_top() != home_screen) refresh_if_top(settings_screen);
  }
}

// The stream screen's "Start streaming" and "Stop streaming" buttons.
void stream_start_local() {
  stream_set(true, STREAM_FPS);
  sys::link_log(3, "stream started on the board");
}

void stream_stop_local() {
  stream_set(false, 0);
  sys::link_log(3, "stream stopped on the board");
}

static uint8_t s_last_rot = 0xFF;

void tick() {
  if (nav_top() == boot_screen && esp_timer_get_time() >= s_boot_until) {
    apps_reload();  // the store is mounted while the boot line plays (main.cpp)
    nav_root(home_screen, true);
  }
  if (nav_top() == home_screen) home_tick();
  settings_tick();
  app_tick();
  if (s_shell.streaming && s_last_rot != rotation()) send_stream_info();
  s_last_rot = rotation();
  sys::BoardInfo b{};
  b.width = width();
  b.height = height();
  b.rotation = rotation();
  b.screen = current_screen();
  b.dimmed = dimmed();
  b.fps_x10 = fps_x10();
  strlcpy(b.app_id, open_app_id(), sizeof b.app_id);
  b.backlight = backlight_now();
  b.resting = resting();
  sys::board_info_set(b);
}

}  // namespace fx::apps
