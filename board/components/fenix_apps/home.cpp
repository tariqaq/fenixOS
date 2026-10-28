// Home: clock, date, PC status and the launcher (Tiles or Dock). Artboards HomeA, HomeB,
// HomePortrait, HomeNoTime.
#include <stdio.h>
#include <string.h>

#include "fenix_sys.h"
#include "shell.h"

namespace fx::apps {

using namespace fx::ui;

static lv_obj_t* s_clock;  // row of 5 cells: digits get equal widths (tabular figures)
static lv_obj_t* s_cells[5];
static lv_obj_t* s_date;
static lv_obj_t* s_status_icon;
static lv_obj_t* s_status_text;
static int s_shown_min = -2;  // minute of day on screen; -1 = "--:--"
static int s_shown_conn = -1;

static void forget(lv_event_t*) {
  s_clock = s_date = s_status_icon = s_status_text = nullptr;
  s_shown_min = -2;
  s_shown_conn = -1;
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

static void launch_cb(lv_event_t* e) { static_cast<const Launch*>(lv_event_get_user_data(e))->open(); }

static void tile(lv_obj_t* parent, const Launch& l, bool grid) {
  lv_obj_t* t = column(parent, 0);
  if (grid)
    lv_obj_set_size(t, LV_PCT(100), 64);
  else
    lv_obj_set_size(t, 92, 64);
  lv_obj_set_style_pad_ver(t, 9, 0);
  lv_obj_set_style_pad_hor(t, 10, 0);
  lv_obj_set_flex_align(t, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_radius(t, FX_RADIUS_BOARD_TILE, 0);
  lv_obj_set_style_bg_color(t, col(l.color), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t, lv_color_mix(lv_color_white(), col(l.color), LV_OPA_20), LV_STATE_PRESSED);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  text(t, l.icon, &fx_icon_fill_20, FX_COLOR_LABEL);
  text(t, l.name, &fx_font_label_semibold, FX_COLOR_LABEL);
  lv_obj_add_event_cb(t, launch_cb, LV_EVENT_CLICKED, (void*)&l);
}

static void dock_item(lv_obj_t* parent, const Launch& l) {
  lv_obj_t* it = column(parent, 5);
  lv_obj_set_width(it, 56);
  lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(it, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_t* sq = box(it);
  lv_obj_set_size(sq, 46, 46);
  lv_obj_set_style_radius(sq, 12, 0);
  lv_obj_set_style_bg_color(sq, col(l.color), 0);
  lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
  lv_obj_center(text(sq, l.icon, &fx_icon_fill_26, FX_COLOR_LABEL));
  text(it, l.name, &fx_font_caption_medium, FX_COLOR_LABEL);
  lv_obj_add_event_cb(it, launch_cb, LV_EVENT_CLICKED, (void*)&l);
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
  bool port = portrait();
  bool dock = sys::settings().home_style == sys::HOME_DOCK;
  int m = port ? FX_MARGIN_PORTRAIT : FX_MARGIN_LANDSCAPE;

  // Status (top right): PC link.
  lv_obj_t* st = rowbox(scr, 4);
  lv_obj_add_flag(st, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(st, LV_ALIGN_TOP_RIGHT, -m, port ? 8 : 10);
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

  if (dock) {
    lv_obj_t* d = rowbox(scr, 16);
    lv_obj_set_flex_align(d, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (const Launch& l : SYSTEM_APPS) dock_item(d, l);
  } else if (port) {
    lv_obj_t* g = box(scr);
    lv_obj_set_width(g, LV_PCT(100));
    lv_obj_set_layout(g, LV_LAYOUT_GRID);
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {64, 64, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(g, cols, rows);
    lv_obj_set_style_pad_row(g, 8, 0);
    lv_obj_set_style_pad_column(g, 8, 0);
    int i = 0;
    for (const Launch& l : SYSTEM_APPS) {
      tile(g, l, true);
      lv_obj_set_grid_cell(lv_obj_get_child(g, i), LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
      i++;
    }
  } else {
    lv_obj_t* r = rowbox(scr, 8);
    lv_obj_set_width(r, LV_PCT(100));
    lv_obj_add_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(r, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_right(r, FX_MARGIN_LANDSCAPE, 0);
    for (const Launch& l : SYSTEM_APPS) tile(r, l, false);
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
  if (min != s_shown_min) {
    s_shown_min = min;
    if (valid) {
      char c[8], d[48];
      snprintf(c, sizeof c, "%02d:%02d", t.tm_hour, t.tm_min);
      snprintf(d, sizeof d, "%s, %d %s", DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
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
}

}  // namespace fx::apps
