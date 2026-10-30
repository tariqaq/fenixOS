// Settings tree: Display (+ Rotation), Home screen, Storage, Host PC, Wireless, About, Debug.
// Artboards Settings, SettingsDisplay, SettingsHome, SettingsAbout. Wireless (P32) uses the
// Rotation list and the Session pop-up (no artboard of its own).
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "fenix_hal.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "shell.h"

namespace fx::apps {

using namespace fx::ui;

static const char* const ROT_NAMES[] = {"Portrait", "Landscape", "Portrait flipped", "Landscape flipped"};

static void apply_backlight() {
  const sys::Settings& s = sys::settings();
  backlight_config(s.brightness, s.dim_level, s.dim_after_s);
}

// ---- Display ----

static int bright_pct(uint8_t level) { return (level - 16) * 100 / (255 - 16); }
static uint8_t pct_bright(int pct) { return 16 + pct * (255 - 16) / 100; }

static void bright_cb(lv_event_t* e) {
  lv_obj_t* s = lv_event_get_target_obj(e);
  sys::settings().brightness = pct_bright(lv_slider_get_value(s));
  apply_backlight();
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) sys::settings_save();
}

static void dim_cb(lv_event_t* e) {
  lv_obj_t* s = lv_event_get_target_obj(e);
  int v = lv_slider_get_value(s);
  sys::settings().dim_level = v;
  lv_label_set_text_fmt(static_cast<lv_obj_t*>(lv_event_get_user_data(e)), "%d%%", v);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    apply_backlight();
    sys::settings_save();
  }
}

static const uint16_t DIM_AFTER[] = {30, 60, 300, 0};

static void dim_after_cb(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_param(e);
  sys::settings().dim_after_s = DIM_AFTER[i];
  apply_backlight();
  sys::settings_save();
}

static void rotation_screen(lv_obj_t* scr);
static void open_rotation(lv_event_t*) { nav_push(rotation_screen); }

static void calibrate_cb(lv_event_t*) {
  sys::Settings& s = sys::settings();
  hal::touch_calibrate(s.cal, 0xFFFF, 0x0000);
  s.cal_valid = 1;
  sys::settings_save();
  nav_rebuild();
  lv_obj_invalidate(lv_screen_active());
}

// Burn-in protection is locked on; Settings > Debug unlocks its switch (user 2026-10-29).
static void burn_cb(lv_event_t* e) {
  bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
  sys::settings().burn_guard = on;
  burn_guard(on);
  sys::settings_save();
}

static lv_obj_t* note(lv_obj_t* parent, const char* s) {
  lv_obj_t* n = text(parent, s, &fx_font_caption, FX_COLOR_LABEL_2);
  lv_obj_set_width(n, LV_PCT(100));
  lv_obj_set_style_pad_hor(n, 4, 0);
  lv_obj_set_style_text_line_space(n, 2, 0);
  return n;
}

static lv_obj_t* slider_block(lv_obj_t* g, const char* label, int pad_bottom) {
  lv_obj_t* b = column(g, 4);
  lv_obj_set_width(b, LV_PCT(100));
  lv_obj_set_style_pad_top(b, 8, 0);
  lv_obj_set_style_pad_bottom(b, pad_bottom, 0);
  text(b, label, &fx_font_label, FX_COLOR_LABEL_2);
  lv_obj_t* r = rowbox(b, 8);
  lv_obj_set_width(r, LV_PCT(100));
  lv_obj_set_height(r, 20);
  return r;
}

static void display_screen(lv_obj_t* scr) {
  const sys::Settings& st = sys::settings();
  navbar(scr, "Settings", "Display");
  lv_obj_t* c = content(scr, 12);

  lv_obj_t* g = group(c);
  lv_obj_set_style_pad_hor(g, 10, 0);
  lv_obj_t* r = slider_block(g, "Brightness", 6);
  text(r, FX_ICON_SUN, &fx_icon_fill_12, FX_COLOR_LABEL_2);
  lv_obj_t* s = slider(r, 0, 100, bright_pct(st.brightness));
  lv_obj_add_event_cb(s, bright_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s, bright_cb, LV_EVENT_RELEASED, nullptr);
  text(r, FX_ICON_SUN, &fx_icon_fill_18, FX_COLOR_ACCENT);
  separator(g, 0);

  r = slider_block(g, "Auto-dim level", 6);
  text(r, FX_ICON_MOON, &fx_icon_fill_12, FX_COLOR_LABEL_2);
  s = slider(r, 0, 100, st.dim_level);
  lv_obj_t* pct = text(r, "", &fx_font_label, FX_COLOR_LABEL_2);
  lv_obj_set_width(pct, 34);
  lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text_fmt(pct, "%d%%", st.dim_level);
  lv_obj_add_event_cb(s, dim_cb, LV_EVENT_VALUE_CHANGED, pct);
  lv_obj_add_event_cb(s, dim_cb, LV_EVENT_RELEASED, pct);
  separator(g, 0);

  lv_obj_t* b = column(g, 6);
  lv_obj_set_width(b, LV_PCT(100));
  lv_obj_set_style_pad_top(b, 8, 0);
  lv_obj_set_style_pad_bottom(b, 10, 0);
  text(b, "Dim after", &fx_font_label, FX_COLOR_LABEL_2);
  static const char* const opts[] = {"30 s", "1 min", "5 min", "Never"};
  int sel = 3;
  for (int i = 0; i < 4; i++)
    if (DIM_AFTER[i] == st.dim_after_s) sel = i;
  segmented(b, opts, 4, sel, dim_after_cb);

  g = group(c);
  lv_obj_t* row = list_row(g, nullptr, 0, "Rotation", ROT_NAMES[st.rotation], true, open_rotation, nullptr);
  lv_obj_move_to_index(text(row, FX_ICON_DEVICE_ROTATE, &fx_icon_fill_18, FX_COLOR_LABEL), 0);
  separator(g, 38);
  row = list_row(g, nullptr, 0, "Calibrate touch", nullptr, false, calibrate_cb, nullptr);
  lv_obj_set_style_text_color(lv_obj_get_child(row, 0), col(FX_COLOR_ACCENT), 0);
  lv_obj_move_to_index(text(row, FX_ICON_HAND_TAP, &fx_icon_fill_18, FX_COLOR_ACCENT), 0);

  g = group(c);
  lv_obj_t* sw = switch_row(g, nullptr, 0, "Burn-in protection", st.burn_guard, st.debug, burn_cb, nullptr);
  lv_obj_move_to_index(text(lv_obj_get_parent(sw), FX_ICON_MONITOR, &fx_icon_fill_18, FX_COLOR_LABEL), 0);
  note(c, st.debug ? "Moves the picture by a pixel every minute and turns the screen off after 10 minutes "
                     "without a touch."
                   : "Moves the picture by a pixel every minute and turns the screen off after 10 minutes "
                     "without a touch. Turn on Debug in Settings to change this.");
}

// ---- Rotation ----

static void rotation_cb(lv_event_t* e) {
  uint8_t r = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  sys::settings().rotation = r;
  sys::settings_save();
  set_rotation(r);  // rebuilds this screen in the new orientation
}

static void rotation_screen(lv_obj_t* scr) {
  navbar(scr, "Display", "Rotation");
  lv_obj_t* c = content(scr, 12);
  lv_obj_t* g = group(c);
  static const uint8_t order[] = {1, 0, 3, 2};
  for (int i = 0; i < 4; i++) {
    uint8_t r = order[i];
    if (i) separator(g, 10);
    lv_obj_t* row = list_row(g, nullptr, 0, ROT_NAMES[r], nullptr, false, rotation_cb, (void*)(intptr_t)r);
    if (r == rotation()) text(row, FX_ICON_CHECK, &fx_icon_bold_14, FX_COLOR_ACCENT);
  }
}

// ---- Home screen ----

static void style_cb(lv_event_t* e) {
  sys::settings().home_style = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  sys::settings_save();
  nav_rebuild();
}

static lv_obj_t* bar(lv_obj_t* p, int w, int h, int r, uint32_t c) {
  lv_obj_t* o = box(p);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_set_style_bg_color(o, col(c), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

static const uint32_t PREVIEW_COLORS[] = {FX_COLOR_TILE_GRAY, FX_COLOR_TILE_INDIGO, FX_COLOR_TILE_BLUE,
                                          FX_COLOR_TILE_ORANGE};

static void style_card(lv_obj_t* parent, uint8_t style, bool selected) {
  lv_obj_t* btn = column(parent, 6);
  lv_obj_set_flex_grow(btn, 1);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(btn, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn, style_cb, LV_EVENT_CLICKED, (void*)(intptr_t)style);

  lv_obj_t* card = column(btn, 5);
  lv_obj_set_size(card, 140, 105);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, col(selected ? FX_COLOR_ACCENT : FX_COLOR_SURFACE_2), 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  if (style == sys::HOME_TILES) {
    bar(card, 54, 16, 3, FX_COLOR_LABEL);
    bar(card, 40, 5, 2, FX_COLOR_LABEL_2);
    lv_obj_set_flex_grow(box(card), 1);
    lv_obj_t* row = rowbox(card, 4);
    lv_obj_set_width(row, LV_PCT(100));
    for (uint32_t c : PREVIEW_COLORS) lv_obj_set_flex_grow(bar(row, 10, 30, 5, c), 1);
  } else {
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_top(bar(card, 64, 20, 3, FX_COLOR_LABEL), 6, 0);
    bar(card, 44, 5, 2, FX_COLOR_LABEL_2);
    lv_obj_set_flex_grow(box(card), 1);
    lv_obj_t* row = rowbox(card, 7);
    for (uint32_t c : PREVIEW_COLORS) bar(row, 20, 20, 6, c);
  }
  lv_obj_t* lbl = rowbox(btn, 4);
  if (selected) text(lbl, FX_ICON_CHECK, &fx_icon_bold_10, FX_COLOR_ACCENT);
  text(lbl, style == sys::HOME_TILES ? "Tiles" : "Dock", selected ? &fx_font_label_semibold : &fx_font_label,
       selected ? FX_COLOR_LABEL : FX_COLOR_LABEL_2);
}

static void home_style_screen(lv_obj_t* scr) {
  navbar(scr, "Settings", "Home screen");
  lv_obj_t* c = content(scr, 12);
  lv_obj_set_style_pad_top(c, 10, 0);
  lv_obj_t* r = rowbox(c, 10);
  lv_obj_set_width(r, LV_PCT(100));
  if (portrait()) lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
  uint8_t cur = sys::settings().home_style;
  style_card(r, sys::HOME_TILES, cur == sys::HOME_TILES);
  style_card(r, sys::HOME_DOCK, cur == sys::HOME_DOCK);
  lv_obj_t* n = note(c, "Tiles show big app tiles under the clock. Dock keeps the clock large with small icons below.");
  lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
}

// ---- Storage ----

static const char* apps_count_text() {
  static char t[16];
  size_t n = app_tiles().size();
  if (n == 0) return "No apps";
  snprintf(t, sizeof t, n == 1 ? "%u app" : "%u apps", (unsigned)n);
  return t;
}

static void storage_screen(lv_obj_t* scr) {
  navbar(scr, "Settings", "Storage");
  lv_obj_t* c = content(scr, 12);
  lv_obj_t* g = group(c);
  uint32_t used = 0, total = 0;
  sys::store_usage(&used, &total);
  char v[40];
  snprintf(v, sizeof v, "%u KB of %u KB", (unsigned)used, (unsigned)total);
  info_row(g, "App storage", v);
  separator(g, 10);
  info_row(g, "Installed apps", apps_count_text());
  if (app_tiles().empty()) return;
  lv_obj_t* list = group(c);
  for (size_t i = 0; i < app_tiles().size(); i++) {
    if (i) separator(list, 10);
    info_row(list, app_tiles()[i].name, "");
  }
}

// ---- Host PC ----

static void host_screen(lv_obj_t* scr) {
  navbar(scr, "Settings", "Host PC");
  lv_obj_t* c = content(scr, 12);
  lv_obj_t* g = group(c);
  bool conn = shell().connected;
  info_row(g, "Status", conn ? "Connected" : "Not connected");
  separator(g, 10);
  char v[32];
  link_text(v, sizeof v);
  info_row(g, "Link", v);
  separator(g, 10);
  info_row(g, "Time", sys::clock_valid() ? "Set by your PC" : "Not set");
  lv_obj_t* note = text(c, "Open fenixOS on your PC to manage apps, update the board and see its screen.",
                        &fx_font_caption, FX_COLOR_LABEL_2);
  lv_obj_set_width(note, LV_PCT(100));
  lv_obj_set_style_pad_hor(note, 4, 0);
}

// ---- Wireless (P32; Bluetooth removed in P38): Wi-Fi or off ----

static const char* radio_status(const sys::RadioInfo& r) {
  switch (r.state) {
    case fnx::radio_state::OFF:
      return "Off";
    case fnx::radio_state::STARTING:
      return "Starting";
    case fnx::radio_state::READY:
      return "No network set";
    case fnx::radio_state::CONNECTING:
      return "Connecting";
    case fnx::radio_state::CONNECTED:
      return "Connected";
    default:
      return "Not working";
  }
}

static uint8_t s_radio_pick;

static void radio_restart_cb(lv_event_t*) {
  backlight_fade_out();
  sys::radio_switch(s_radio_pick);
  // On the board radio_switch restarts; only the simulator gets here.
  sheet_close();
  apply_backlight();
  nav_rebuild();
}

static void radio_cancel_cb(lv_event_t*) { sheet_close(); }

// Switching restarts: confirm first (user 2026-10-30: warning popup).
static void radio_pick_cb(lv_event_t* e) {
  uint8_t m = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  uint8_t cur = sys::radio_info().mode;
  if (m == cur) return;
  s_radio_pick = m;
  bool on = m == fnx::radio::WIFI;
  const char* title = on ? "Turn Wi-Fi on?" : "Turn Wi-Fi off?";
  const char* sub = on ? "fenixOS restarts to turn Wi-Fi on." : "fenixOS restarts to turn Wi-Fi off.";
  lv_obj_t* sh = sheet_open();
  lv_obj_t* head = column(sh, 2);
  lv_obj_set_width(head, LV_PCT(100));
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(head, title, &fx_font_title, FX_COLOR_LABEL);
  lv_obj_t* s = text(head, sub, &fx_font_caption, FX_COLOR_LABEL_2);
  lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s, LV_PCT(100));
  lv_obj_t* r = rowbox(sh, 8);
  lv_obj_set_width(r, LV_PCT(100));
  lv_obj_set_style_margin_top(r, 4, 0);
  lv_obj_set_flex_grow(capsule(r, FX_ICON_ARROW_CLOCKWISE, &fx_icon_fill_16, "Restart", FX_COLOR_ACCENT_FILL, 40,
                               radio_restart_cb, nullptr),
                       1);
  lv_obj_t* cancel = box(sh);
  lv_obj_set_size(cancel, LV_PCT(100), 32);
  lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(cancel, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_center(text(cancel, "Cancel", &fx_font_body, FX_COLOR_ACCENT));
  lv_obj_add_event_cb(cancel, radio_cancel_cb, LV_EVENT_CLICKED, nullptr);
}

// ---- Wi-Fi networks (P34): the board's own scan; passwords come from the PC (no keyboard, P36) ----

static void wireless_screen(lv_obj_t* scr);
static constexpr int MAX_LIST = 12;
static fnx::WifiAp s_aps[MAX_LIST];
static int s_naps;

static void ok_cb(lv_event_t*) { sheet_close(); }

static void net_info(const char* title, const char* sub) {
  lv_obj_t* sh = sheet_open();
  lv_obj_t* head = column(sh, 2);
  lv_obj_set_width(head, LV_PCT(100));
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(head, title, &fx_font_title, FX_COLOR_LABEL);
  lv_obj_t* s = text(head, sub, &fx_font_caption, FX_COLOR_LABEL_2);
  lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s, LV_PCT(100));
  lv_obj_t* ok = box(sh);
  lv_obj_set_size(ok, LV_PCT(100), 32);
  lv_obj_add_flag(ok, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(ok, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_center(text(ok, "OK", &fx_font_body, FX_COLOR_ACCENT));
  lv_obj_add_event_cb(ok, ok_cb, LV_EVENT_CLICKED, nullptr);
}

// Saved and open networks join with a tap; others need their password from the PC.
static void ap_cb(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= s_naps) return;
  const fnx::WifiAp& a = s_aps[i];
  char saved[fnx::SSID_LEN];
  bool is_saved = sys::radio_saved_network(saved, sizeof saved) && strcmp(saved, a.ssid) == 0;
  if (is_saved) {
    sys::radio_reconnect();
  } else if (a.auth == fnx::wifi_auth::OPEN) {
    sys::radio_join(a.ssid, "");
  } else if (a.auth == fnx::wifi_auth::ENTERPRISE) {
    net_info("Can't join", "Networks that need a user name are not supported.");
  } else {
    net_info(a.ssid, "Enter its password in fenixOS on your PC: Settings, Wireless.");
  }
}

static void scan_cb(lv_event_t*) {
  sys::radio_scan(false);
  refresh_if_top(wireless_screen);
}

static void networks(lv_obj_t* c, const sys::RadioInfo& ri) {
  bool scanning = sys::radio_scanning();
  s_naps = sys::radio_scan_results(s_aps, MAX_LIST);
  lv_obj_t* g = group(c);
  lv_obj_t* head = list_row(g, nullptr, 0, "Networks", nullptr, false, scanning ? nullptr : scan_cb, nullptr);
  text(head, scanning ? "Scanning\xE2\x80\xA6" : "Scan", &fx_font_label, scanning ? FX_COLOR_LABEL_2 : FX_COLOR_ACCENT);
  for (int i = 0; i < s_naps; i++) {
    const fnx::WifiAp& a = s_aps[i];
    separator(g, 10);
    lv_obj_t* r = list_row(g, nullptr, 0, a.ssid, nullptr, false, ap_cb, (void*)(intptr_t)i);
    bool here = ri.state == fnx::radio_state::CONNECTED && strcmp(ri.name, a.ssid) == 0;
    if (here) text(r, FX_ICON_CHECK, &fx_icon_bold_14, FX_COLOR_ACCENT);
    if (a.auth != fnx::wifi_auth::OPEN) text(r, FX_ICON_LOCK, &fx_icon_fill_14, FX_COLOR_LABEL_2);
    const char* bars = a.rssi >= -60 ? FX_ICON_WIFI_HIGH : a.rssi >= -75 ? FX_ICON_WIFI_MEDIUM : FX_ICON_WIFI_LOW;
    text(r, bars, &fx_icon_fill_14, FX_COLOR_LABEL);
  }
}

static void wireless_screen(lv_obj_t* scr) {
  sys::RadioInfo ri = sys::radio_info();
  navbar(scr, "Settings", "Wireless");
  lv_obj_t* c = content(scr, 12);
  lv_obj_t* g = group(c);
  static const uint8_t modes[] = {fnx::radio::OFF, fnx::radio::WIFI};
  static const char* const names[] = {"Off", "Wi-Fi"};
  static const char* const icons[] = {FX_ICON_WIFI_NONE, FX_ICON_WIFI_HIGH};
  static const uint32_t bgs[] = {FX_COLOR_TILE_GRAY, FX_COLOR_TILE_BLUE};
  for (int i = 0; i < 2; i++) {
    if (i) separator(g, 44);
    lv_obj_t* row = list_row(g, icons[i], bgs[i], names[i], nullptr, false, radio_pick_cb, (void*)(intptr_t)modes[i]);
    if (modes[i] == ri.mode) text(row, FX_ICON_CHECK, &fx_icon_bold_14, FX_COLOR_ACCENT);
  }
  const char* tip = "Switching restarts fenixOS.";
  if (ri.mode == fnx::radio::WIFI) {
    g = group(c);
    info_row(g, "Status", radio_status(ri));
    char v[40];
    char net[fnx::SSID_LEN];
    bool saved = sys::radio_saved_network(net, sizeof net);
    separator(g, 10);
    info_row(g, "Network", saved ? net : "None");
    if (ri.state == fnx::radio_state::CONNECTED) {
      separator(g, 10);
      snprintf(v, sizeof v, "%u.%u.%u.%u", ri.ip[0], ri.ip[1], ri.ip[2], ri.ip[3]);
      info_row(g, "IP address", v);
      separator(g, 10);
      snprintf(v, sizeof v, "%d dBm", ri.rssi);
      info_row(g, "Signal", v);
    }
    if (!saved) tip = "Add a network in fenixOS on your PC: Settings, Wireless. Switching restarts fenixOS.";
  }
  note(c, tip);
  if (ri.mode == fnx::radio::WIFI) {
    // The first visit scans by itself; the list then stays until Scan.
    static bool scanned;
    if (!scanned && !sys::radio_scanning() && sys::radio_scan_results(s_aps, 1) == 0) sys::radio_scan(false);
    scanned = true;
    networks(c, ri);
  }
}

// Rebuild Wireless when the radio's state changes.
void settings_tick() {
  static int shown = -1;
  sys::RadioInfo ri = sys::radio_info();
  fnx::WifiAp one;
  int key = ri.mode << 8 | ri.state | sys::radio_scanning() << 16 | sys::radio_scan_results(&one, 1) << 17;
  if (key == shown) return;
  shown = key;
  refresh_if_top(wireless_screen);
}

// ---- About ----

static void about_screen(lv_obj_t* scr) {
  navbar(scr, "Settings", "About");
  lv_obj_t* c = content(scr, 0);
  lv_obj_t* g = group(c);
  char v[48];
  snprintf(v, sizeof v, "v%s", esp_app_get_description()->version);
  info_row(g, "fenixOS", v);
  separator(g, 10);
  info_row(g, "Board", "ESP32-2432S028R");
  separator(g, 10);
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  snprintf(v, sizeof v, "ESP32-D0WD-V3 rev %d.%d", ci.revision / 100, ci.revision % 100);
  info_row(g, "Chip", v);
  separator(g, 10);
  snprintf(v, sizeof v, "%d cores, 240 MHz", ci.cores);
  info_row(g, "CPU", v);
  separator(g, 10);
  snprintf(v, sizeof v, "%u KB of 520 KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
  info_row(g, "Memory free", v);
  separator(g, 10);
  uint32_t flash = 0;
  esp_flash_get_size(nullptr, &flash);
  snprintf(v, sizeof v, "%u MB", (unsigned)(flash >> 20));
  info_row(g, "Flash", v);
  separator(g, 10);
  info_row(g, "Display", "2.8 in, 320 \xC3\x97 240, ILI9341");
}

// ---- Settings list ----

static void push_cb(lv_event_t* e) { nav_push((Builder)lv_event_get_user_data(e)); }

// Debug off locks burn-in protection on again.
static void debug_cb(lv_event_t* e) {
  sys::Settings& s = sys::settings();
  s.debug = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
  if (!s.debug && !s.burn_guard) {
    s.burn_guard = 1;
    burn_guard(true);
  }
  sys::settings_save();
}

void settings_screen(lv_obj_t* scr) {
  const sys::Settings& st = sys::settings();
  navbar(scr, "Home", "Settings");
  lv_obj_t* c = content(scr, 16);
  lv_obj_t* g = group(c);
  list_row(g, FX_ICON_SUN, FX_COLOR_TILE_BLUE, "Display", (st.rotation & 1) ? "Landscape" : "Portrait", true, push_cb,
           (void*)display_screen);
  separator(g, 44);
  list_row(g, FX_ICON_SQUARES_FOUR, FX_COLOR_TILE_INDIGO, "Home screen",
           st.home_style == sys::HOME_DOCK ? "Dock" : "Tiles", true, push_cb, (void*)home_style_screen);
  separator(g, 44);
  list_row(g, FX_ICON_HARD_DRIVES, FX_COLOR_TILE_GRAY, "Storage", apps_count_text(), true, push_cb, (void*)storage_screen);
  separator(g, 44);
  list_row(g, FX_ICON_DESKTOP_TOWER, FX_COLOR_TILE_TEAL, "Host PC", shell().connected ? "Connected" : "Not connected",
           true, push_cb, (void*)host_screen);
  separator(g, 44);
  const char* radio = sys::radio_info().mode == fnx::radio::WIFI ? "Wi-Fi" : "Off";
  list_row(g, FX_ICON_WIFI_HIGH, FX_COLOR_TILE_BLUE, "Wireless", radio, true, push_cb, (void*)wireless_screen);
  g = group(c);
  char v[40];
  snprintf(v, sizeof v, "v%s", esp_app_get_description()->version);
  list_row(g, FX_ICON_INFO, FX_COLOR_TILE_GRAY, "About", v, true, push_cb, (void*)about_screen);
  g = group(c);
  switch_row(g, FX_ICON_WARNING, FX_COLOR_TILE_ORANGE, "Debug", st.debug, true, debug_cb, nullptr);
}

}  // namespace fx::apps
