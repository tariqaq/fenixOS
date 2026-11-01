// Home: clock, date, PC status and the launcher (Tiles or Dock). Artboards HomeA, HomeB,
// HomePortrait, HomeNoTime. Since P51 the launcher shows one page of tiles at a time (swipe
// sideways, dots below): up to 32 apps fit, and only the shown tiles' icons are in RAM.
#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "fenix_sys.h"
#include "shell.h"

namespace fx::apps {

using namespace fx::ui;

static lv_obj_t* s_clock;  // row of 5 cells: digits get equal widths (tabular figures)
static lv_obj_t* s_cells[5];
static lv_obj_t* s_date;
static lv_obj_t* s_radio_icon;
static lv_obj_t* s_status_icon;
static lv_obj_t* s_status_text;
static int s_shown_min = -2;  // minute of day on screen; -1 = "--:--"
static int s_shown_conn = -1;
static int s_shown_radio = -1;
static int s_shown_wx = -1;
static lv_obj_t* s_launch;  // the launcher's container, rebuilt alone on a page swipe
static lv_obj_t* s_dots;    // page dots under it (hidden for one page)
static int s_page;          // launcher page, kept across rebuilds (rotation, app changes)

// The old Home is deleted after a rebuilt one is made: only forget our own objects.
static void forget(lv_event_t* e) {
  if (lv_event_get_target(e) != s_clock) return;
  s_clock = s_date = s_radio_icon = s_status_icon = s_status_text = s_launch = s_dots = nullptr;
  s_shown_min = -2;
  s_shown_conn = -1;
  s_shown_radio = -1;
  s_shown_wx = -1;
}

struct Launch {
  const char* name;
  const char* icon;
  uint32_t color;
  void (*open)();
};

static void open_settings() { nav_push(settings_screen); }
static void open_stream() { nav_push(stream_screen); }

static const Launch SYSTEM_APPS[] = {
    {"Settings", FX_ICON_GEAR_SIX, FX_COLOR_TILE_GRAY, open_settings},
    {"Session", FX_ICON_POWER, FX_COLOR_TILE_INDIGO, session_open},
    {"Stream", FX_ICON_SCREENCAST, FX_COLOR_TILE_BLUE, open_stream},
};
static constexpr size_t N_SYSTEM = sizeof SYSTEM_APPS / sizeof SYSTEM_APPS[0];

// One launcher entry: a system app (glyph) or an installed host app (A8 icon from its package).
struct TileSpec {
  const char* name;
  const char* glyph;
  const lv_image_dsc_t* img;  // used when it has data
  uint32_t color;
  lv_event_cb_t cb;
  void* ud;
  bool faded;  // PC half off or not on this PC, or its SD card is out (P51)
  bool card;   // SD card badge: stored on the card, which is out
};

// A swipe also ends in a click on the tile under the finger: ignore that one.
static bool swiped() { return lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_NONE; }

static void launch_cb(lv_event_t* e) {
  if (!swiped()) static_cast<const Launch*>(lv_event_get_user_data(e))->open();
}

// Shown installed apps: their index entry and icons, at fixed addresses (images point at the
// descriptors). The previous page's set is kept one build longer: the old screen can still draw
// while the new one replaces it.
struct Shown {
  sys::AppMeta m;
  AppIcons ic;
};
static std::vector<std::unique_ptr<Shown>> s_shown, s_prev;

static void app_cb(lv_event_t* e) {
  if (swiped()) return;
  const Shown* a = static_cast<const Shown*>(lv_event_get_user_data(e));
  app_open(a->m.id);
}

// The cached entry for `id` (moved out of the previous set when there), or a new one.
static Shown* shown_for(const sys::AppMeta& m, std::vector<std::unique_ptr<Shown>>& old) {
  for (auto& p : old)
    if (p && strcmp(p->m.id, m.id) == 0) {
      p->m = m;  // name, colour or state may have changed; the icons were read for this id
      s_shown.push_back(std::move(p));
      return s_shown.back().get();
    }
  auto n = std::make_unique<Shown>();
  n->m = m;
  if (m.state == fnx::app_state::READY) app_icons(m.id, n->ic);
  s_shown.push_back(std::move(n));
  return s_shown.back().get();
}

// Tile glyph or package icon, tinted white.
static lv_obj_t* tile_icon(lv_obj_t* parent, const TileSpec& s, const lv_font_t* f) {
  if (!s.img || !s.img->data) return text(parent, s.glyph ? s.glyph : FX_ICON_PUZZLE_PIECE, f, FX_COLOR_LABEL);
  lv_obj_t* im = lv_image_create(parent);
  lv_image_set_src(im, s.img);
  lv_obj_set_style_image_recolor(im, col(FX_COLOR_LABEL), 0);
  lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
  return im;
}

// SD card glyph in a tile's top-right corner, `inset` px from its edges: the app is on the card,
// which is out (P51). Alignment is to the content box, so the padding is taken back out.
static void badge(lv_obj_t* parent, int inset) {
  lv_obj_t* b = text(parent, FX_ICON_SIM_CARD, &fx_icon_fill_14, FX_COLOR_LABEL);
  lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(b, LV_ALIGN_TOP_RIGHT, lv_obj_get_style_pad_right(parent, LV_PART_MAIN) - inset,
               inset - lv_obj_get_style_pad_top(parent, LV_PART_MAIN));
}

static lv_obj_t* tile(lv_obj_t* parent, const TileSpec& s, bool grid) {
  lv_obj_t* t = column(parent, 0);
  if (grid)
    lv_obj_set_size(t, LV_PCT(100), 64);
  else
    lv_obj_set_size(t, 92, 64);
  lv_obj_set_style_pad_ver(t, 9, 0);
  lv_obj_set_style_pad_hor(t, 10, 0);
  lv_obj_set_flex_align(t, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_radius(t, FX_RADIUS_BOARD_TILE, 0);
  lv_obj_set_style_bg_color(t, col(s.color), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t, lv_color_mix(col(FX_COLOR_LABEL), col(s.color), LV_OPA_20), LV_STATE_PRESSED);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  tile_icon(t, s, &fx_icon_fill_20);
  lv_obj_t* name = text(t, s.name, &fx_font_label_semibold, FX_COLOR_LABEL);
  lv_obj_set_width(name, LV_PCT(100));
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  if (s.cb) lv_obj_add_event_cb(t, s.cb, LV_EVENT_CLICKED, s.ud);
  if (s.card) badge(t, 8);
  if (s.faded) lv_obj_set_style_opa(t, LV_OPA_40, 0);
  return t;
}

static lv_obj_t* dock_item(lv_obj_t* parent, const TileSpec& s) {
  lv_obj_t* it = column(parent, 5);
  lv_obj_set_width(it, 56);
  lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(it, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_t* sq = box(it);
  lv_obj_set_size(sq, 46, 46);
  lv_obj_set_style_radius(sq, 12, 0);
  lv_obj_set_style_bg_color(sq, col(s.color), 0);
  lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
  lv_obj_center(tile_icon(sq, s, &fx_icon_fill_26));
  if (s.card) badge(sq, 3);
  lv_obj_t* name = text(it, s.name, &fx_font_caption_medium, FX_COLOR_LABEL);
  lv_obj_set_width(name, 56);
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  if (s.cb) lv_obj_add_event_cb(it, s.cb, LV_EVENT_CLICKED, s.ud);
  if (s.faded) lv_obj_set_style_opa(it, LV_OPA_40, 0);
  return it;
}

// Tile install / remove (DESIGN.md): fade + scale 0.8 <-> 1.
static void tile_anim_exec(void* o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t*)o, v * 255 / 256, 0);
  lv_obj_set_style_transform_scale((lv_obj_t*)o, 205 + v * 51 / 256, 0);
}
static char s_fade_id[sizeof(sys::AppMeta::id)];  // tile fading in on the page being built

static void ghost_done(lv_anim_t* a) {
  app_ghost_done();
  lv_obj_delete_async((lv_obj_t*)a->var);
}
static void tile_anim(lv_obj_t* o, bool in) {
  lv_obj_set_style_transform_pivot_x(o, LV_PCT(50), 0);
  lv_obj_set_style_transform_pivot_y(o, LV_PCT(50), 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, tile_anim_exec);
  lv_anim_set_values(&a, in ? 0 : 256, in ? 256 : 0);
  lv_anim_set_duration(&a, FX_MOTION_BASE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  if (!in) lv_anim_set_completed_cb(&a, ghost_done);
  tile_anim_exec(o, in ? 0 : 256);
  lv_anim_start(&a);
}

enum Mode { ROW, GRID, DOCK };
static Mode s_mode;

// Items per launcher page. Tiles: 3 across in landscape (92 px + 8 gap), 2x2 in portrait; Dock:
// as many 56 px items with 16 px gaps as fit the width.
static int per_page(Mode mode) {
  if (mode == ROW) return 3;
  if (mode == GRID) return 4;
  int m = portrait() ? FX_MARGIN_PORTRAIT : FX_MARGIN_LANDSCAPE;
  return LV_MAX(1, (width() - 2 * m + 16) / 72);
}

static int item_count() { return (int)N_SYSTEM + app_count(); }
static int page_count(Mode mode) { return LV_MAX(1, (item_count() + per_page(mode) - 1) / per_page(mode)); }

static void show_dots(Mode mode) {
  if (!s_dots) return;
  lv_obj_clean(s_dots);
  int n = page_count(mode);
  if (n < 2) {
    lv_obj_add_flag(s_dots, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(s_dots, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < n; i++) {
    lv_obj_t* d = box(s_dots);
    lv_obj_set_size(d, 6, 6);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, col(i == s_page ? FX_COLOR_LABEL : FX_COLOR_SURFACE_3), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  }
}

// One page: system apps, then installed apps (by id), then a removed app fading out when its
// tile was on this page. A newly installed app turns to its page.
static void launcher(lv_obj_t* parent, Mode mode) {
  int per = per_page(mode), n = item_count();
  sys::AppMeta m;
  for (int i = 0; app_at(i, &m); i++)
    if (app_take_fade_in(m.id)) {
      strlcpy(s_fade_id, m.id, sizeof s_fade_id);
      s_page = ((int)N_SYSTEM + i) / per;
    }
  s_page = LV_CLAMP(0, s_page, page_count(mode) - 1);
  s_prev.swap(s_shown);
  s_shown.clear();
  std::vector<TileSpec> specs;
  std::vector<bool> fade_in;
  for (int i = s_page * per; i < n && i < (s_page + 1) * per; i++) {
    if (i < (int)N_SYSTEM) {
      const Launch& l = SYSTEM_APPS[i];
      specs.push_back({l.name, l.icon, nullptr, l.color, launch_cb, (void*)&l, false, false});
      fade_in.push_back(false);
      continue;
    }
    if (!app_at(i - (int)N_SYSTEM, &m)) break;
    Shown* a = shown_for(m, s_prev);
    bool missing = m.state == fnx::app_state::CARD_MISSING;
    uint8_t h = app_host(m.id);
    bool faded = missing || h == fnx::host::OFF || h == fnx::host::NOT_HERE;
    specs.push_back({a->m.name, nullptr, mode == DOCK ? &a->ic.large : &a->ic.small, a->m.color, app_cb, a, faded,
                     missing});
    fade_in.push_back(strcmp(s_fade_id, m.id) == 0);
  }
  s_fade_id[0] = 0;
  // The removed tile fades out where it was, if that page shows and there is room for it.
  const char* gid = app_ghost();
  const Shown* g = nullptr;
  if (gid)
    for (auto& p : s_prev)
      if (p && strcmp(p->m.id, gid) == 0) g = p.get();
  if (g && (int)specs.size() < per)
    specs.push_back({g->m.name, nullptr, mode == DOCK ? &g->ic.large : &g->ic.small, g->m.color, nullptr, nullptr,
                     false, false});
  else if (gid)
    app_ghost_done();
  for (size_t i = 0; i < specs.size(); i++) {
    lv_obj_t* o = mode == DOCK ? dock_item(parent, specs[i]) : tile(parent, specs[i], mode == GRID);
    if (mode == GRID) lv_obj_set_grid_cell(o, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
    if (i < fade_in.size() && fade_in[i]) tile_anim(o, true);
    if (i >= fade_in.size()) {  // the ghost
      lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
      tile_anim(o, false);
    }
  }
  show_dots(mode);
}

static void relaunch(void*) {
  if (!s_launch) return;
  lv_obj_clean(s_launch);
  launcher(s_launch, s_mode);
}

// Swipe left or right anywhere on Home turns the page; the launcher is rebuilt after the event.
static void swipe_cb(lv_event_t*) {
  lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
  int to = s_page + (d == LV_DIR_LEFT ? 1 : d == LV_DIR_RIGHT ? -1 : 0);
  if (to == s_page || to < 0 || to >= page_count(s_mode)) return;
  s_page = to;
  lv_async_call(relaunch, nullptr);
}

// The artboards set line-height = font size; LVGL labels are one font line tall,
// so the row's top margin is pulled up by half the difference.
static lv_obj_t* clock_row(lv_obj_t* parent, const lv_font_t* f, int size, int margin_top) {
  lv_obj_t* r = rowbox(parent, -1);  // letter-spacing -0.02em
  int lh = lv_font_get_line_height(f);
  lv_obj_set_style_margin_top(r, margin_top - (lh - size) / 2, 0);
  lv_obj_set_style_margin_bottom(r, -(lh - size) / 2, 0);
  int digit_w = 0;
  for (char c = '0'; c <= '9'; c++) digit_w = LV_MAX(digit_w, (int)lv_font_get_glyph_width(f, c, 0));
  for (int i = 0; i < 5; i++) {
    s_cells[i] = text(r, i == 2 ? ":" : "-", f, FX_COLOR_LABEL);
    if (i != 2) {
      lv_obj_set_width(s_cells[i], digit_w);
      lv_obj_set_style_text_align(s_cells[i], LV_TEXT_ALIGN_CENTER, 0);
    }
  }
  return r;
}

static void clock_show(const char* hhmm, uint32_t color) {
  char cell[2] = {0, 0};
  for (int i = 0; i < 5; i++) {
    cell[0] = hhmm[i];
    lv_label_set_text(s_cells[i], cell);
    lv_obj_set_style_text_color(s_cells[i], col(color), 0);
  }
}

void home_screen(lv_obj_t* scr) {
  s_shown_min = -2;  // a rebuilt Home has fresh labels: redraw them on the first tick
  s_shown_conn = -1;
  s_shown_radio = -1;
  s_shown_wx = -1;
  bool port = portrait();
  bool dock = sys::settings().home_style == sys::HOME_DOCK;
  int m = port ? FX_MARGIN_PORTRAIT : FX_MARGIN_LANDSCAPE;

  // Status (top right): radio (P32), PC link.
  lv_obj_t* st = rowbox(scr, 4);
  lv_obj_add_flag(st, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(st, LV_ALIGN_TOP_RIGHT, -m, port ? 8 : 10);
  s_radio_icon = text(st, "", &fx_icon_fill_14, FX_COLOR_LABEL_2);
  lv_obj_set_style_margin_right(s_radio_icon, 4, 0);
  s_status_icon = text(st, "", &fx_icon_fill_14, FX_COLOR_SUCCESS);
  s_status_text = text(st, "", &fx_font_caption, FX_COLOR_LABEL_2);

  bool centred = port || dock;
  lv_obj_set_style_pad_all(scr, m, 0);
  // Track place too: a column's track is only as wide as its widest item.
  lv_flex_align_t cross = centred ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START;
  lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, cross, cross);

  s_clock = clock_row(scr, port ? &fx_font_clock_56 : &fx_font_clock_64, port ? 56 : 64, port ? 40 : (dock ? 18 : 6));
  s_date = text(scr, "", &fx_font_label_medium, FX_COLOR_LABEL_2);
  lv_obj_set_style_margin_top(s_date, 6, 0);
  if (!centred) lv_obj_set_style_margin_left(s_date, 2, 0);
  lv_obj_add_event_cb(s_clock, forget, LV_EVENT_DELETE, nullptr);

  lv_obj_t* spacer = box(scr);
  lv_obj_set_flex_grow(spacer, 1);

  if (dock) {
    s_mode = DOCK;
    s_launch = rowbox(scr, 16);
    lv_obj_set_width(s_launch, LV_PCT(100));
    lv_obj_set_flex_align(s_launch, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  } else if (port) {
    // Two columns, two rows. LVGL keeps a pointer to the templates.
    static const int32_t rows[] = {64, 64, LV_GRID_TEMPLATE_LAST};
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    s_mode = GRID;
    s_launch = box(scr);
    lv_obj_set_size(s_launch, LV_PCT(100), 2 * 64 + 8);
    lv_obj_set_layout(s_launch, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(s_launch, cols, rows);
    lv_obj_set_style_pad_row(s_launch, 8, 0);
    lv_obj_set_style_pad_column(s_launch, 8, 0);
  } else {
    s_mode = ROW;
    s_launch = rowbox(scr, 8);
    lv_obj_set_width(s_launch, LV_PCT(100));
  }
  s_dots = rowbox(scr, 6);
  lv_obj_set_style_margin_top(s_dots, 8, 0);
  if (!centred) lv_obj_set_width(s_dots, LV_PCT(100));
  lv_obj_set_flex_align(s_dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  launcher(s_launch, s_mode);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_GESTURE, nullptr);
  home_tick();
}

static const char* const DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* const MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};

void home_tick() {
  if (!s_clock) return;
  struct tm t;
  bool valid = sys::clock_local(&t);
  int min = valid ? t.tm_hour * 60 + t.tm_min : -1;
  sys::Weather wx;
  int wxk = sys::weather(&wx) ? (wx.temp_x10 << 8 | wx.code) : -1;
  if (min != s_shown_min || wxk != s_shown_wx) {
    s_shown_min = min;
    s_shown_wx = wxk;
    if (valid) {
      char c[8], d[80];
      snprintf(c, sizeof c, "%02d:%02d", t.tm_hour, t.tm_min);
      int n = snprintf(d, sizeof d, "%s, %d %s", DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
      // Weather (P34): rounded degrees and a word, after the date (its own line in portrait).
      if (wxk >= 0) {
        int deg = (wx.temp_x10 + (wx.temp_x10 < 0 ? -5 : 5)) / 10;
        snprintf(d + n, sizeof d - n, portrait() ? "\n%d\xC2\xB0 %s" : " \xC2\xB7 %d\xC2\xB0 %s", deg,
                 sys::weather_text(wx.code));
      }
      clock_show(c, FX_COLOR_LABEL);
      lv_label_set_text(s_date, d);
    } else {
      clock_show("--:--", FX_COLOR_LABEL_2);
      // In Wi-Fi mode SNTP sets it too (P34); the mode only changes with a restart.
      bool wifi = sys::radio_info().mode == fnx::radio::WIFI;
      lv_label_set_text(s_date, wifi ? "Getting the time over Wi-Fi" : "Waiting for your PC to set the time");
    }
  }
  int conn = shell().connected;
  if (conn != s_shown_conn) {
    s_shown_conn = conn;
    lv_label_set_text(s_status_icon, conn ? FX_ICON_PLUGS_CONNECTED : FX_ICON_PLUGS);
    lv_obj_set_style_text_color(s_status_icon, col(conn ? FX_COLOR_SUCCESS : FX_COLOR_WARNING), 0);
    lv_label_set_text(s_status_text, conn ? "PC" : "No PC");
  }
  sys::RadioInfo ri = sys::radio_info();
  int radio = ri.mode << 16 | ri.state << 8 | (uint8_t)ri.rssi;
  if (radio != s_shown_radio) {
    s_shown_radio = radio;
    bool on = ri.mode == fnx::radio::WIFI;
    bool up = ri.state == fnx::radio_state::CONNECTED;
    const char* icon = !up              ? FX_ICON_WIFI_NONE
                       : ri.rssi >= -60 ? FX_ICON_WIFI_HIGH
                       : ri.rssi >= -75 ? FX_ICON_WIFI_MEDIUM
                                        : FX_ICON_WIFI_LOW;
    lv_label_set_text(s_radio_icon, on ? icon : "");
    lv_obj_set_style_text_color(s_radio_icon, col(up ? FX_COLOR_LABEL : FX_COLOR_LABEL_2), 0);
    if (on) lv_obj_remove_flag(s_radio_icon, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_radio_icon, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace fx::apps
