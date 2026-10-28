// System services: settings (NVS), clock (MYT), PC link (UART0), stream encoder, app storage.
// Threads: the link task owns UART RX. Everything else runs on the UI task.
// Link events reach the UI through link_poll(); nothing here calls LVGL.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace fx::sys {

// ---- Settings (NVS namespace "fenix", one blob) ----
enum HomeStyle : uint8_t { HOME_TILES = 0, HOME_DOCK = 1 };

struct Settings {
  uint8_t rotation = 1;       // LovyanGFX 0..3; 1 = landscape (USB on the left)
  uint8_t brightness = 200;   // backlight 16..255
  uint8_t dim_level = 20;     // percent of brightness when dimmed
  uint16_t dim_after_s = 60;  // 0 = never
  uint8_t home_style = HOME_TILES;
  uint8_t cal_valid = 0;
  uint16_t cal[8] = {};
};
void nvs_start();
Settings& settings();
void settings_save();

// ---- Clock ----
// Local time is MYT (UTC+8, no DST).
void clock_init();
void clock_set_utc(uint32_t epoch_s);
bool clock_valid();
bool clock_local(struct tm* out);  // false while invalid

// ---- App storage (LittleFS partition "apps" at /apps) ----
bool store_mount();
void store_usage(uint32_t* used_kb, uint32_t* total_kb);

// ---- Shared board state (written by the UI, read by the link task) ----
struct BoardInfo {
  uint16_t width, height;
  uint8_t rotation;
  uint8_t screen;  // fnx::screen
  uint8_t dimmed;
  uint16_t fps_x10;
  char app_id[64];
};
void board_info_set(const BoardInfo& b);  // sends BOARD_STATE when screen/dimmed/rotation/app change
BoardInfo board_info();

// ---- PC link ----
enum class LinkEv : uint8_t {
  CONNECTED,
  DISCONNECTED,
  TIME_SET,
  STREAM_START,  // value = fps
  STREAM_STOP,
  STREAM_SYNC,
  HOST_STATE,  // value = running, text = app id
  APPS_CHANGED,
};
struct LinkEvent {
  LinkEv type;
  int32_t value;
  char text[64];
};
void link_start();
bool link_connected();
bool link_poll(LinkEvent* e);  // UI task: next pending event
void link_log(uint8_t level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Send one frame (type + body). Blocks while the TX buffer is full. Thread-safe.
void link_send(uint8_t type, const uint8_t* body, size_t n);
// Remote touch from the PC (TOUCH message). True while the PC holds a press.
bool remote_touch(int16_t* x, int16_t* y);
uint32_t link_rx_frames();
uint32_t link_rx_errors();

// ---- Stream encoder (called from the LVGL flush callback) ----
bool stream_active();
// Encode one flushed area (RGB565 big-endian, w*h pixels) into STREAM_RECT frames.
// Returns false when the TX buffer had no room; the caller marks the area stale.
bool stream_rect(int x, int y, int w, int h, const uint8_t* px);
// Room for a full redraw of `bytes` raw pixels, roughly (used before re-invalidating stale areas).
bool stream_has_room(size_t bytes);
uint32_t stream_dropped();
void stream_set_active(bool on);

}  // namespace fx::sys
