// System services: settings (NVS), clock (MYT), PC link (UART0), stream encoder, app storage.
// Threads: the link task owns UART RX. Everything else runs on the UI task.
// Link events reach the UI through link_poll(); nothing here calls LVGL.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "fenix_proto.h"

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
  // New fields go at the end: an older, shorter blob then loads with them at their defaults.
  uint8_t burn_guard = 1;  // burn-in protection (P21); can be turned off only with debug on
  uint8_t debug = 0;       // Settings > Debug: unlocks switches meant for development
  uint8_t radio = 0;       // fnx::radio mode (P32); applied at boot
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

// ---- App storage (LittleFS partition "apps" at /apps; rules in docs/packages.md) ----
constexpr int MAX_APPS = 10;  // APP_LIST fits in one frame; Home has room for them
struct AppMeta {
  char id[64], version[64];
  uint8_t sha256[32];
};
bool store_mount();  // also deletes leftovers of an interrupted install or remove
void store_usage(uint32_t* used_kb, uint32_t* total_kb);
int store_list(AppMeta* out, int max);           // sorted by id
uint8_t* store_load(const char* id, size_t* n);  // whole package (malloc, caller frees); nullptr if missing
// Install and remove. Link task only. Results are fnx::app codes.
uint8_t store_begin(const char* id, const char* version, uint32_t size, const uint8_t sha256[32]);
uint32_t store_chunk(uint32_t offset, const uint8_t* data, size_t n);  // next expected offset
uint8_t store_end(char* why, size_t cap);  // checks size, sha256 and the package, then commits
void store_abort();
bool store_installing(const char** id);
uint8_t store_remove(const char* id);

// ---- Shared board state (written by the UI, read by the link task) ----
struct BoardInfo {
  uint16_t width, height;
  uint8_t rotation;
  uint8_t screen;  // fnx::screen
  uint8_t dimmed;
  uint16_t fps_x10;
  char app_id[64];
  uint8_t backlight;  // level now, 0..255 (SYSINFO)
  uint8_t resting;    // burn-in protection put the panel to sleep
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
  APPS_CHANGED,  // text = app id installed or removed
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
// Binding values from the PC half (BIND_SET), in arrival order. UI task.
struct BindValue {
  uint16_t bind;
  uint8_t kind;  // 0 int, 1 bool, 2 text
  int32_t i;
  char id[64];
  char text[256];
};
bool bind_poll(BindValue* v);
uint32_t link_rx_frames();
uint32_t link_rx_errors();
// Remote transports (P33 SPP, P34 TCP): one radio runs per boot, so at most one exists. The
// transport moves bytes; the link task decodes and frames. A session belongs to the transport
// that opened it (HELLO); the other one gets ERROR busy until it ends.
struct RemoteLink {
  const char* name;                           // "Bluetooth", "Wi-Fi"
  bool (*write)(const uint8_t* d, size_t n);  // one whole frame or none; may block briefly
  size_t (*tx_free)();                        // room for frames without blocking
  bool stream;  // carries the screen stream (Bluetooth: no, too little heap)
};
void link_remote_start(const RemoteLink* t, size_t rx_buf);  // once, from radio_start
void link_remote_rx(const uint8_t* d, size_t n);             // transport task; waits while full
void link_remote_closed();                                   // the peer went away
const char* link_remote_name();  // the session's radio ("Wi-Fi", "Bluetooth"), or nullptr on USB
bool link_can_stream();          // false while the session runs over a transport without stream

// ---- Radios (P32): one at a time; switching saves the mode and restarts ----
struct RadioInfo {
  uint8_t mode, state;  // fnx::radio, fnx::radio_state
  int8_t rssi;
  uint8_t ip[4];
  char name[fnx::SSID_LEN];  // Wi-Fi network or Bluetooth peer
};
void radio_start();  // boot, after nvs_start: starts the saved radio, frees Bluetooth memory otherwise
RadioInfo radio_info();
void radio_report();              // RADIO_STATE to the PC
void radio_switch(uint8_t mode);  // saves and restarts (no return) unless the mode is unchanged
// Wi-Fi only: scan all channels; with for_pc the result goes out as WIFI_SCAN (NOT_WIFI otherwise).
bool radio_scan(bool for_pc);
bool radio_scanning();
int radio_scan_results(fnx::WifiAp* out, int max);  // last scan, strongest first
void radio_join(const char* ssid, const char* pass);  // saved in plaintext; empty ssid forgets
void radio_reconnect();                               // the saved network, now
bool radio_saved_network(char* out, size_t cap);
void net_start();  // Wi-Fi mode: PC link over TCP, mDNS, SNTP (net.cpp)
void net_up();     // the station got an address
void web_start();  // phone page on port 80, status only (web.cpp)
// The board's state as JSON (web.cpp): version, uptime, heap, apps, PC link, time, date,
// weather; `net` adds SSID, signal and address. Length, or 0 when `cap` is too small.
size_t status_json(char* out, size_t cap, bool net);
void ble_start();  // Bluetooth mode: the BLE status service (ble.cpp), once Bluedroid runs
// Weather (P34): Open-Meteo every 15 min in Wi-Fi mode, for the place set from the PC.
struct Weather {
  bool valid;
  int16_t temp_x10;  // degrees C x 10
  uint8_t code;      // WMO weather code
  char place[fnx::PLACE_LEN];
};
void weather_set(const fnx::WeatherSet& p);  // saved in NVS; any mode
bool weather(Weather* out);                  // false until the first reading
const char* weather_text(uint8_t code);      // one short word: "Clear", "Rain"...

// ---- Stream encoder (called from the LVGL flush callback) ----
bool stream_active();
// Encode one flushed area (RGB565 big-endian, w*h pixels) into STREAM_RECT frames.
// Returns false when the TX buffer had no room; the caller marks the area stale.
bool stream_rect(int x, int y, int w, int h, const uint8_t* px);
// Room for a full redraw of `bytes` raw pixels, roughly (used before re-invalidating stale areas).
bool stream_has_room(size_t bytes);
uint32_t stream_dropped();
// UI task only. Off frees the encode buffers (they are allocated at the first band).
void stream_set_active(bool on);

}  // namespace fx::sys
