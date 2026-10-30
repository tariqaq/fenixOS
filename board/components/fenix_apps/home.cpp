// Home: clock, date, PC status and the launcher (Tiles or Dock). Artboards HomeA, HomeB,
// HomePortrait, HomeNoTime.
#include <stdio.h>
#include <string.h>

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

// The old Home is deleted after a rebuilt one is made: only forget our own objects.
static void forget(lv_event_t* e) {
  if (lv_event_get_target(e) != s_clock) return;
  s_clock = s_date = s_radio_icon = s_status_icon = s_status_text = nullptr;
  s_shown_min = -2;
  s_shown_conn = -1;
  s_shown_radio = -1;
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
};

static void launch_cb(lv_event_t* e) { static_cast<const Launch*>(lv_event_get_user_data(e))->open(); }
static void app_cb(lv_event_t* e) {
  size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e);
  if (i < app_tiles().size()) app_open(app_tiles()[i].id);
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
  lv_obj_set_style_bg_color(t, lv_color_mix(lv_color_white(), col(s.color), LV_OPA_20), LV_STATE_PRESSED);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  tile_icon(t, s, &fx_icon_fill_20);
  lv_obj_t* name = text(t, s.name, &fx_font_label_semibold, FX_COLOR_LABEL);
  lv_obj_set_width(name, LV_PCT(100));
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  if (s.cb) lv_obj_add_event_cb(t, s.cb, LV_EVENT_CLICKED, s.ud);
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
  lv_obj_t* name = text(it, s.name, &fx_font_caption_medium, FX_COLOR_LABEL);
  lv_obj_set_width(name, 56);
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  if (s.cb) lv_obj_add_event_cb(it, s.cb, LV_EVENT_CLICKED, s.ud);
  return it;
}

// Tile install / remove (DESIGN.md): fade + scale 0.8 <-> 1.
static void tile_anim_exec(void* o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t*)o, v * 255 / 256, 0);
  lv_obj_set_style_transform_scale((lv_obj_t*)o, 205 + v * 51 / 256, 0);
}
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

// System apps, then installed apps (by id), then a removed app fading out.
static void launcher(lv_obj_t* parent, Mode mode) {
  std::vector<TileSpec> specs;
  for (const Launch& l : SYSTEM_APPS) specs.push_back({l.name, l.icon, nullptr, l.color, launch_cb, (void*)&l});
  const auto& apps = app_tiles();
  for (size_t i = 0; i < apps.size(); i++) {
    const AppTile& t = apps[i];
    specs.push_back({t.name, nullptr, mode == DOCK ? &t.large : &t.small, t.color, app_cb, (void*)(uintptr_t)i});
  }
  const AppTile* g = app_ghost();
  if (g) specs.push_back({g->name, nullptr, mode == DOCK ? &g->large : &g->small, g->color, nullptr, nullptr});
  for (size_t i = 0; i < specs.size(); i++) {
    lv_obj_t* o = mode == DOCK ? dock_item(parent, specs[i]) : tile(parent, specs[i], mode == GRID);
    if (mode == GRID) lv_obj_set_grid_cell(o, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
    bool is_app = i >= N_SYSTEM && i - N_SYSTEM < apps.size();
    if (is_app && app_take_fade_in(apps[i - N_SYSTEM].id)) tile_anim(o, true);
    if (g && i == specs.size() - 1) {
      lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
      tile_anim(o, false);
    }
  }
}

// A launcher row that scrolls sideways when its items do not fit.
static lv_obj_t* scroll_row(lv_obj_t* parent, int gap) {
  lv_obj_t* r = rowbox(parent, gap);
  lv_obj_set_width(r, LV_PCT(100));
  lv_obj_add_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);  // drags must reach the scroller
  lv_obj_set_scroll_dir(r, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
  return r;
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
  if (!centred) lv_obj_set_style_pad_right(scr, 0, 0);  // tiles run off the right edge
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

  int n_items = N_SYSTEM + app_tiles().size() + (app_ghost() ? 1 : 0);
  if (dock) {
    lv_obj_t* d = scroll_row(scr, 16);
    bool fits = n_items * 56 + (n_items - 1) * 16 <= width() - 2 * m;
    lv_obj_set_flex_align(d, fits ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    launcher(d, DOCK);
  } else if (port) {
    // Two columns; two rows show, more rows scroll. LVGL keeps a pointer to the row template.
    static int32_t rows[(sys::MAX_APPS + N_SYSTEM + 2) / 2 + 1];
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    int n_rows = (n_items + 1) / 2;
    for (int r = 0; r < n_rows; r++) rows[r] = 64;
    rows[n_rows] = LV_GRID_TEMPLATE_LAST;
    lv_obj_t* g = box(scr);
    lv_obj_set_size(g, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(g, 2 * 64 + 8, 0);
    lv_obj_set_layout(g, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(g, cols, rows);
    lv_obj_set_style_pad_row(g, 8, 0);
    lv_obj_set_style_pad_column(g, 8, 0);
    if (n_rows > 2) {
      lv_obj_add_flag(g, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(g, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_scroll_dir(g, LV_DIR_VER);
      lv_obj_set_scrollbar_mode(g, LV_SCROLLBAR_MODE_OFF);
    }
    launcher(g, GRID);
  } else {
    lv_obj_t* r = scroll_row(scr, 8);
    lv_obj_set_style_pad_right(r, FX_MARGIN_LANDSCAPE, 0);
    launcher(r, ROW);
  }
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
      lv_label_set_text(s_date, "Waiting for your PC to set the time");
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
