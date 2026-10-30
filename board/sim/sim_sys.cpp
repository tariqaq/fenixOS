// fx::sys for the simulator: settings in memory, a clock fixed at 2026-10-29 08:35 (plus the
// virtual time) so screenshots repeat, the app store as a folder of .fxp files, and a link whose
// events come from the script or the window (sim::post).
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>

#include "fenix_pkg.h"
#include "sim.h"

namespace fs = std::filesystem;

namespace sim {

static std::string s_store;
static bool s_connected;
static std::deque<sys::LinkEvent> s_events;
static std::deque<sys::BindValue> s_binds;

void store_dir(const std::string& dir) { s_store = dir; }

void post(sys::LinkEv type, int32_t value, const char* text) {
  sys::LinkEvent e{};
  e.type = type;
  e.value = value;
  strlcpy(e.text, text, sizeof e.text);
  s_events.push_back(e);
}

void set_connected(bool on) {
  if (on == s_connected) return;
  s_connected = on;
  post(on ? sys::LinkEv::CONNECTED : sys::LinkEv::DISCONNECTED);
}

static std::vector<uint8_t> read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static fs::path pkg_path(const std::string& id) { return fs::path(s_store) / (id + ".fxp"); }

bool set_binds(const std::string& id, const std::vector<std::string>& pairs, std::string* err) {
  std::vector<uint8_t> data = read_file(pkg_path(id));
  fpkg::Package pkg;
  if (data.empty() || pkg.parse(data.data(), data.size())) {
    *err = "no valid package for " + id + " in the store";
    return false;
  }
  for (const std::string& kv : pairs) {
    size_t eq = kv.find('=');
    std::string name = kv.substr(0, eq), val = eq == std::string::npos ? "" : kv.substr(eq + 1);
    int bind = -1;
    for (size_t i = 0; i < pkg.binds.size(); i++)
      if (pkg.strings[pkg.binds[i]] == name) bind = (int)i;
    if (bind < 0) {
      *err = id + " has no bind '" + name + "'";
      return false;
    }
    sys::BindValue v{};
    v.bind = (uint16_t)bind;
    strlcpy(v.id, id.c_str(), sizeof v.id);
    char* end = nullptr;
    long n = strtol(val.c_str(), &end, 10);
    if (val == "true" || val == "false") {
      v.kind = 1;
      v.i = val == "true";
    } else if (!val.empty() && end && *end == 0) {
      v.kind = 0;
      v.i = (int32_t)n;
    } else {
      v.kind = 2;
      strlcpy(v.text, val.c_str(), sizeof v.text);
    }
    s_binds.push_back(v);
  }
  return true;
}

bool install(const std::string& fxp, std::string* id, std::string* err) {
  std::vector<uint8_t> data = read_file(fxp);
  fpkg::Package pkg;
  const char* why = data.empty() ? "cannot read it" : pkg.parse(data.data(), data.size());
  if (why) {
    *err = fxp + ": " + why;
    return false;
  }
  *id = std::string(pkg.id);
  fs::create_directories(s_store);
  fs::copy_file(fxp, pkg_path(*id), fs::copy_options::overwrite_existing);
  post(sys::LinkEv::APPS_CHANGED, 0, id->c_str());
  return true;
}

bool remove(const std::string& id) {
  std::error_code ec;
  if (!fs::remove(pkg_path(id), ec)) return false;
  post(sys::LinkEv::APPS_CHANGED, 0, id.c_str());
  return true;
}

}  // namespace sim

namespace fx::sys {

static Settings s_settings;
void nvs_start() {}
Settings& settings() { return s_settings; }
void settings_save() {}

// 2026-10-29 08:35 MYT (UTC+8) as UTC seconds; MYT has no DST, so gmtime of (utc + 8 h) is local.
static const time_t BASE_UTC = 1793234100;
void clock_init() {}
void clock_set_utc(uint32_t) {}
bool clock_valid() { return true; }
bool clock_local(struct tm* out) {
  time_t t = BASE_UTC + 8 * 3600 + (time_t)(sim::now_us() / 1000000);
#ifdef _WIN32
  return gmtime_s(out, &t) == 0;
#else
  return gmtime_r(&t, out) != nullptr;
#endif
}

bool store_mount() { return true; }
void store_usage(uint32_t* used_kb, uint32_t* total_kb) {
  uint64_t used = 8 * 1024;  // an empty LittleFS store on the board
  std::error_code ec;
  if (!sim::s_store.empty())
    for (const auto& f : fs::directory_iterator(sim::s_store, ec))
      if (f.path().extension() == ".fxp") used += f.file_size();
  *used_kb = (uint32_t)(used / 1024);
  *total_kb = 1984;
}
int store_list(AppMeta* out, int max) {
  std::vector<AppMeta> v;
  std::error_code ec;
  if (!sim::s_store.empty())
    for (const auto& f : fs::directory_iterator(sim::s_store, ec)) {
      if (f.path().extension() != ".fxp") continue;
      std::vector<uint8_t> d = sim::read_file(f.path());
      std::string_view id, ver;
      if (d.empty() || !fpkg::peek(d.data(), d.size(), &id, &ver)) continue;
      AppMeta m{};
      strlcpy(m.id, std::string(id).c_str(), sizeof m.id);
      strlcpy(m.version, std::string(ver).c_str(), sizeof m.version);
      v.push_back(m);
    }
  std::sort(v.begin(), v.end(), [](const AppMeta& a, const AppMeta& b) { return strcmp(a.id, b.id) < 0; });
  int n = 0;
  for (; n < (int)v.size() && n < max; n++) out[n] = v[n];
  return n;
}
uint8_t* store_load(const char* id, size_t* n) {
  std::vector<uint8_t> d = sim::read_file(sim::pkg_path(id));
  if (d.empty()) return nullptr;
  uint8_t* p = (uint8_t*)malloc(d.size());
  memcpy(p, d.data(), d.size());
  *n = d.size();
  return p;
}
// Installs over the link do not happen in the simulator (`install` in a script copies the file).
uint8_t store_begin(const char*, const char*, uint32_t, const uint8_t[32]) { return 1; }
uint32_t store_chunk(uint32_t, const uint8_t*, size_t) { return 0; }
uint8_t store_end(char* why, size_t cap) {
  strlcpy(why, "not in the simulator", cap);
  return 1;
}
void store_abort() {}
bool store_installing(const char**) { return false; }
uint8_t store_remove(const char* id) { return sim::remove(id) ? 0 : 1; }

static BoardInfo s_info;
void board_info_set(const BoardInfo& b) { s_info = b; }
BoardInfo board_info() { return s_info; }

void link_start() {}
bool link_connected() { return sim::s_connected; }
bool link_poll(LinkEvent* e) {
  if (sim::s_events.empty()) return false;
  *e = sim::s_events.front();
  sim::s_events.pop_front();
  return true;
}
void link_log(uint8_t, const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  printf("board: ");
  vprintf(fmt, a);
  printf("\n");
  va_end(a);
}
void link_send(uint8_t, const uint8_t*, size_t) {}
bool remote_touch(int16_t*, int16_t*) { return false; }
bool bind_poll(BindValue* v) {
  if (sim::s_binds.empty()) return false;
  *v = sim::s_binds.front();
  sim::s_binds.pop_front();
  return true;
}
uint32_t link_rx_frames() { return 0; }
uint32_t link_rx_errors() { return 0; }
const char* link_remote_name() { return nullptr; }
bool link_can_stream() { return true; }
static bool s_stream;
bool stream_active() { return s_stream; }
bool stream_rect(int, int, int, int, const uint8_t*) { return true; }
bool stream_has_room(size_t) { return true; }
uint32_t stream_dropped() { return 0; }
void stream_set_active(bool on) { s_stream = on; }

// Radios (P32): no stacks; a switch takes effect at once instead of after a restart.
static RadioInfo s_radio{};
void radio_start() {}
RadioInfo radio_info() { return s_radio; }
void radio_report() {}
void radio_switch(uint8_t mode) {
  s_settings.radio = s_radio.mode = mode;
  s_radio.state = mode == fnx::radio::OFF ? fnx::radio_state::OFF : fnx::radio_state::READY;
}
bool radio_scan(bool) { return false; }
bool radio_scanning() { return false; }
int radio_scan_results(fnx::WifiAp*, int) { return 0; }
void radio_join(const char*, const char*) {}
void radio_reconnect() {}
bool radio_saved_network(char* out, size_t cap) {
  if (cap) out[0] = 0;
  return false;
}

// Weather (P34): none in the simulator; the Home date line stays as in the golden shots.
void weather_set(const fnx::WeatherSet&) {}
bool weather(Weather* out) {
  *out = {};
  return false;
}
const char* weather_text(uint8_t) { return ""; }

}  // namespace fx::sys
