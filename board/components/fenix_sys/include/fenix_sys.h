// System services: settings (NVS), clock (MYT), PC link (UART0), stream encoder, app storage.
// Threads: the link task owns UART RX. Everything else runs on the UI task.
// Link events reach the UI through link_poll(); nothing here calls LVGL.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

// ---- App storage (P51; rules in docs/packages.md "Where apps live") ----
// Packages go to the SD card (/sd/fenixos/apps) when one is in, else to flash (LittleFS
// partition "apps" at /apps, FLASH_PKG_MAX per package). An index in RAM lists them all.
constexpr int MAX_APPS = 32;  // APP_INDEX pages them; Home pages its tiles
constexpr uint32_t FLASH_PKG_MAX = 128 * 1024;
struct AppMeta {  // 120 B
  char id[49];  // fpkg::MAX_ID + 1
  char version[32];
  char name[32];
  uint32_t color;  // tile colour
  uint8_t where;   // fnx::app_where
  uint8_t state;   // fnx::app_state: CARD_MISSING while its SD card is out
};
bool store_mount();  // boot: flash apps, plus the card's apps from /apps/sd.idx (card missing until scanned)
void store_usage(uint32_t* used_kb, uint32_t* total_kb);  // the flash store
int store_count();
bool store_at(int i, AppMeta* out);               // sorted by id; false past the end
bool store_find(const char* id, AppMeta* out);
bool store_sha(const char* id, uint8_t out[32]);  // false if missing or its card is out
FILE* store_open(const char* id, size_t* n);  // the package file and its size; caller closes. nullptr if missing
// sd.cpp, from its task: the card was mounted (scan its apps) or went away (mark them missing).
// Posts APPS_CHANGED with an empty id.
void store_sd_changed(bool ready);
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
  HOST_STATE,  // value = fnx::host state, text = app id
  APPS_CHANGED,  // text = app id installed or removed; empty: several (the SD card came or went)
  SD_CHANGED,    // the microSD card came, went or changed (sd_info)
  OTA,           // a firmware update started or failed (ota_status)
  SHOT,          // the PC asked for a screenshot on the card (SHOT_SAVE): the UI task calls shot_save
};
struct LinkEvent {
  LinkEv type;
  int32_t value;
  char text[64];
};
void link_start();
bool link_connected();
bool link_poll(LinkEvent* e);  // UI task: next pending event
void link_post(LinkEv t, int32_t v = 0, const char* text = nullptr);  // any task: queue an event for the UI
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
// Remote transport (P34 TCP; SPP until P38): at most one exists. The transport moves bytes; the
// link task decodes and frames. A session belongs to the transport that opened it (HELLO); USB
// gets ERROR busy while a Wi-Fi session is open, and the other way round.
struct RemoteLink {
  const char* name;                           // "Wi-Fi"
  bool (*write)(const uint8_t* d, size_t n);  // one whole frame or none; may block briefly
  size_t (*tx_free)();                        // room for frames without blocking
};
void link_remote_start(const RemoteLink* t, size_t rx_buf);  // once, from radio_start
void link_remote_rx(const uint8_t* d, size_t n);             // transport task; waits while full
void link_remote_closed();                                   // the peer went away
const char* link_remote_name();  // the session's radio ("Wi-Fi"), or nullptr on USB

// ---- Radio (P32): Wi-Fi or off; switching saves the mode and restarts ----
struct RadioInfo {
  uint8_t mode, state;  // fnx::radio, fnx::radio_state
  int8_t rssi;
  uint8_t ip[4];
  char name[fnx::SSID_LEN];  // Wi-Fi network
};
void radio_start();  // boot, after nvs_start: starts Wi-Fi when that is the saved mode
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
// ---- microSD (P50, sd.cpp): FAT32 at /sd, SPI3 at 40 MHz, long file names ----
constexpr const char* SD_PATH = "/sd";
using SdInfo = fnx::SdState;
void sd_start();   // boot: mounts in a short-lived background task
void sd_probe();   // checks a mounted card is still there, or mounts one; async, SD_CHANGED on a change
SdInfo sd_info();
void sd_report();  // SD_STATE to the PC; without a ready card it also probes again
void sd_update_free();  // after a write to the card: new free space, SD_STATE on a change

// ---- Firmware updates (P52, ota.cpp; docs/ota.md) ----
enum OtaPhase : uint8_t { OTA_IDLE, OTA_RECEIVING, OTA_CHECKING, OTA_WRITING, OTA_RESTARTING, OTA_FAILED };
struct OtaStatus {
  uint8_t phase;  // OtaPhase
  uint8_t pct;
  char version[32];
  char text[64];  // what it does, or why it failed
};
struct OtaOffer {
  char update[32];        // a newer package on the card's root, or ""
  char update_file[64];   // its file name
  char back[32];          // the newest kept older package ("Go back"), or ""
};
void ota_boot();        // before store_mount: layout change, trial and rollback state
void ota_home_shown();  // UI: Home is up; a new image on trial is kept 30 s later
void ota_report();      // link: says once per boot that an update was rolled back
const char* ota_failed_version();  // the version that was rolled back, or nullptr
OtaStatus ota_status();
void ota_dismiss();     // UI: back to idle after a failure was shown
OtaOffer ota_offer();
void ota_scan_sd();     // sd task after a mount (and after an install): fills ota_offer
bool ota_install_offer(bool back);  // UI: install the card's update, or go back; runs in a task
int ota_vercmp(const char* a, const char* b);
// Link task. Codes are fnx::fw; ota_end returns 0xFF when the check runs (FW_RESULT follows).
uint8_t ota_begin(const fnx::FwBegin& b, uint32_t* next);
uint32_t ota_chunk(uint32_t offset, const uint8_t* data, size_t n);  // next expected offset; 0 on failure
uint8_t ota_end();
void ota_abort();
bool ota_receiving();
bool ota_busy();

// ---- Files under /sd/fenixos over the link (P53, files.cpp; proto/spec.md "Files (0x6_)") ----
void files_frame(uint8_t type, fnx::Reader& r);  // link task: FILE_* from the PC
void files_tick(int64_t now_us);                 // link task: closes a write or read idle for 10 s
void files_abort();                              // the session ended (a part stays for a resume)
void shot_save();  // UI task: the screen to /sd/fenixos/shots/<time>.bmp, answered with FILE_RESULT

// ---- Board log and crash records on the card (P53, logs.cpp) ----
void logs_line(uint8_t level, const char* text, size_t n);  // any task (link_log calls it)
// A line for the card only, for what the PC logs on its side anyway (sessions, installs).
void logs_note(uint8_t level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void logs_tick(int64_t now_us);  // link task: writes waiting lines to /sd/fenixos/logs/<boot>.txt
void logs_mounted();             // sd task, after a mount: this boot's file; copies a core dump

void net_start();  // Wi-Fi mode: PC link over TCP, mDNS, SNTP (net.cpp)
void net_up();     // the station got an address
void web_start();  // phone page on port 80, status only (web.cpp)
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
