// Theme widgets. Every object starts from lv_obj_remove_style_all, so only the styles
// set here apply (the LVGL default theme never shows through).
#include "fenix_ui.h"

namespace fx::ui {

lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  // Not clickable: presses fall through to the nearest clickable parent (rows, tiles, cards).
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  return o;
}

static lv_obj_t* flexbox(lv_obj_t* parent, lv_flex_flow_t flow, int gap) {
  lv_obj_t* o = box(parent);
  lv_obj_set_layout(o, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(o, flow);
  lv_obj_set_style_pad_row(o, gap, 0);
  lv_obj_set_style_pad_column(o, gap, 0);
  return o;
}

lv_obj_t* column(lv_obj_t* parent, int gap) { return flexbox(parent, LV_FLEX_FLOW_COLUMN, gap); }
lv_obj_t* rowbox(lv_obj_t* parent, int gap) {
  lv_obj_t* o = flexbox(parent, LV_FLEX_FLOW_ROW, gap);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return o;
}

lv_obj_t* screen_new() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s);
  lv_obj_set_style_bg_color(s, col(FX_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s, col(FX_COLOR_LABEL), 0);
  lv_obj_set_style_text_font(s, &fx_font_body, 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(s, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
  return s;
}

lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* f, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, col(color), 0);
  return l;
}

void pressable(lv_obj_t* o) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(o, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(o, LV_OPA_20, LV_STATE_PRESSED);
}

static void back_cb(lv_event_t*) { nav_pop(); }

lv_obj_t* navbar(lv_obj_t* scr, const char* back, const char* title) {
  lv_obj_t* bar = box(scr);
  lv_obj_set_size(bar, LV_PCT(100), FX_NAVBAR);
  lv_obj_set_style_pad_hor(bar, 8, 0);
  lv_obj_set_flex_grow(bar, 0);
  lv_obj_t* t = text(bar, title, &fx_font_title, FX_COLOR_LABEL);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
  if (back) {
    lv_obj_t* b = rowbox(bar, 2);
    lv_obj_set_height(b, FX_NAVBAR);
    lv_obj_set_style_pad_hor(b, 4, 0);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_set_style_opa(b, LV_OPA_50, LV_STATE_PRESSED);
    text(b, FX_ICON_CARET_LEFT, &fx_icon_bold_14, FX_COLOR_ACCENT);
    // Narrow screens: drop the back label when it would run into the centred title.
    lv_point_t tw, bw;
    lv_text_get_size(&tw, title, &fx_font_title, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&bw, back, &fx_font_body, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int title_w = tw.x, back_w = 8 + 4 + 14 + 2 + bw.x;
    if (back_w + 8 <= (width() - title_w) / 2) text(b, back, &fx_font_body, FX_COLOR_ACCENT);
    lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, nullptr);
  }
  return bar;
}

lv_obj_t* content(lv_obj_t* scr, int gap) {
  lv_obj_t* c = column(scr, gap);
  lv_obj_set_width(c, LV_PCT(100));
  lv_obj_set_flex_grow(c, 1);
  int m = portrait() ? FX_MARGIN_PORTRAIT : FX_MARGIN_LANDSCAPE;
  lv_obj_set_style_pad_top(c, 8, 0);
  lv_obj_set_style_pad_hor(c, m, 0);
  lv_obj_set_style_pad_bottom(c, m, 0);
  // Scrolling starts on the pressed object or its parents, and only clickable objects take presses.
  lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scroll_dir(c, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
  return c;
}

lv_obj_t* group(lv_obj_t* parent) {
  lv_obj_t* g = column(parent, 0);
  lv_obj_set_width(g, LV_PCT(100));
  lv_obj_set_style_bg_color(g, col(FX_COLOR_SURFACE_1), 0);
  lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g, FX_RADIUS_BOARD_GROUP, 0);
  lv_obj_set_style_clip_corner(g, true, 0);
  return g;
}

void separator(lv_obj_t* g, int inset) {
  lv_obj_t* b = box(g);
  lv_obj_set_size(b, LV_PCT(100), 1);
  lv_obj_set_style_pad_left(b, inset, 0);
  lv_obj_t* line = box(b);
  lv_obj_set_size(line, LV_PCT(100), 1);
  lv_obj_set_style_bg_color(line, col(FX_COLOR_SEPARATOR), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

lv_obj_t* list_row(lv_obj_t* g, const char* icon, uint32_t icon_bg, const char* label, const char* value,
                   bool chevron, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = rowbox(g, 10);
  lv_obj_set_size(r, LV_PCT(100), FX_ROW);
  lv_obj_set_style_pad_hor(r, 10, 0);
  pressable(r);
  if (icon) {
    lv_obj_t* sq = box(r);
    lv_obj_set_size(sq, 24, 24);
    lv_obj_set_style_radius(sq, 6, 0);
    lv_obj_set_style_bg_color(sq, col(icon_bg), 0);
    lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
    lv_obj_center(text(sq, icon, &fx_icon_fill_15, FX_COLOR_LABEL));
  }
  lv_obj_t* l = text(r, label, &fx_font_body, FX_COLOR_LABEL);
  lv_obj_set_flex_grow(l, 1);
  if (value) text(r, value, &fx_font_label, FX_COLOR_LABEL_2);
  if (chevron) text(r, FX_ICON_CARET_RIGHT, &fx_icon_bold_10, FX_COLOR_LABEL_2);
  if (cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
  return r;
}

lv_obj_t* info_row(lv_obj_t* g, const char* label, const char* value) {
  lv_obj_t* r = rowbox(g, 8);
  lv_obj_set_size(r, LV_PCT(100), 30);
  lv_obj_set_style_pad_hor(r, 10, 0);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(r, label, &fx_font_label, FX_COLOR_LABEL);
  return text(r, value, &fx_font_label, FX_COLOR_LABEL_2);
}

lv_obj_t* capsule(lv_obj_t* parent, const char* icon, const lv_font_t* icon_font, const char* label, uint32_t bg,
                  int h, lv_event_cb_t cb, void* ud) {
  lv_obj_t* b = rowbox(parent, 6);
  lv_obj_set_height(b, h);
  lv_obj_set_style_pad_hor(b, 16, 0);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_bg_color(b, col(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(b, lv_color_mix(lv_color_white(), col(bg), LV_OPA_20), LV_STATE_PRESSED);
  if (icon) text(b, icon, icon_font, FX_COLOR_LABEL);
  text(b, label, &fx_font_body_semibold, FX_COLOR_LABEL);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

lv_obj_t* slider(lv_obj_t* parent, int min, int max, int value) {
  lv_obj_t* s = lv_slider_create(parent);
  lv_obj_remove_style_all(s);
  lv_slider_set_range(s, min, max);
  lv_slider_set_value(s, value, LV_ANIM_OFF);
  lv_obj_set_height(s, 4);
  lv_obj_set_flex_grow(s, 1);
  lv_obj_set_style_bg_color(s, col(FX_COLOR_SURFACE_3), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s, col(FX_COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s, 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, 7, LV_PART_KNOB);  // 4 + 2*7 = 18 px knob
  lv_obj_set_ext_click_area(s, 14);
  return s;
}

// ---- Segmented ----

struct SegState {
  lv_obj_t* items[6];
  int n;
};

static void seg_paint(SegState* st, int sel) {
  for (int i = 0; i < st->n; i++) {
    lv_obj_t* it = st->items[i];
    lv_obj_set_style_bg_opa(it, i == sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(it, 0), i == sel ? &fx_font_label_semibold : &fx_font_label, 0);
  }
}

static void seg_click(lv_event_t* e) {
  lv_obj_t* it = lv_event_get_current_target_obj(e);
  lv_obj_t* bar = lv_obj_get_parent(it);
  auto* st = static_cast<SegState*>(lv_obj_get_user_data(bar));
  int idx = lv_obj_get_index(it);
  seg_paint(st, idx);
  lv_obj_send_event(bar, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)idx);
}

static void seg_free(lv_event_t* e) { delete static_cast<SegState*>(lv_event_get_user_data(e)); }

lv_obj_t* segmented(lv_obj_t* parent, const char* const* opts, int n, int selected, lv_event_cb_t cb) {
  lv_obj_t* bar = rowbox(parent, 2);
  lv_obj_set_width(bar, LV_PCT(100));
  lv_obj_set_style_pad_all(bar, 2, 0);
  lv_obj_set_style_radius(bar, 8, 0);
  lv_obj_set_style_bg_color(bar, col(FX_COLOR_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  auto* st = new SegState{};
  st->n = n > 6 ? 6 : n;
  for (int i = 0; i < st->n; i++) {
    lv_obj_t* it = box(bar);
    lv_obj_set_height(it, 26);
    lv_obj_set_flex_grow(it, 1);
    lv_obj_set_style_radius(it, 6, 0);
    lv_obj_set_style_bg_color(it, col(FX_COLOR_TILE_GRAY), 0);
    lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(text(it, opts[i], &fx_font_label, FX_COLOR_LABEL));
    lv_obj_add_event_cb(it, seg_click, LV_EVENT_CLICKED, nullptr);
    st->items[i] = it;
  }
  lv_obj_set_user_data(bar, st);
  lv_obj_add_event_cb(bar, seg_free, LV_EVENT_DELETE, st);
  if (cb) lv_obj_add_event_cb(bar, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  seg_paint(st, selected);
  return bar;
}

// ---- Sheet ----

static lv_obj_t* s_scrim;
static lv_obj_t* s_sheet;

static void scrim_opa(void* o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t*)o, v, 0); }
static void sheet_y(void* o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); }

static void scrim_click(lv_event_t* e) {
  if (lv_event_get_target_obj(e) == s_scrim) sheet_close();
}

bool sheet_is_open() { return s_scrim != nullptr; }

lv_obj_t* sheet_open() {
  sheet_close();
  s_scrim = box(lv_layer_top());
  lv_obj_set_size(s_scrim, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_scrim, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_scrim, 0, 0);
  lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_scrim, scrim_click, LV_EVENT_CLICKED, nullptr);
  s_sheet = column(s_scrim, 8);
  lv_obj_set_width(s_sheet, LV_PCT(100));
  lv_obj_align(s_sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(s_sheet, col(FX_COLOR_SURFACE_1), 0);
  lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_sheet, FX_RADIUS_BOARD_TILE, 0);
  lv_obj_set_style_pad_top(s_sheet, 12, 0);
  lv_obj_set_style_pad_hor(s_sheet, 12, 0);
  // Radius only on the top corners: the extra bottom padding hides the lower ones off screen.
  lv_obj_set_style_pad_bottom(s_sheet, 10 + FX_RADIUS_BOARD_TILE, 0);
  lv_obj_set_style_translate_y(s_sheet, FX_RADIUS_BOARD_TILE, 0);
  lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_CLICKABLE);  // taps on the sheet do not close it

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_scrim);
  lv_anim_set_exec_cb(&a, scrim_opa);
  lv_anim_set_values(&a, 0, FX_SCRIM_OPA);
  lv_anim_set_duration(&a, FX_MOTION_BASE_MS);
  lv_anim_start(&a);
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_sheet);
  lv_anim_set_exec_cb(&a, sheet_y);
  lv_anim_set_values(&a, 160, FX_RADIUS_BOARD_TILE);
  lv_anim_set_duration(&a, FX_MOTION_BASE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
  return s_sheet;
}

static void sheet_deleted(lv_anim_t* a) { lv_obj_delete((lv_obj_t*)a->var); }

void sheet_close() {
  if (!s_scrim) return;
  lv_obj_t* scrim = s_scrim;
  lv_obj_t* sh = s_sheet;
  s_scrim = s_sheet = nullptr;
  lv_obj_remove_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, sh);
  lv_anim_set_exec_cb(&a, sheet_y);
  lv_anim_set_values(&a, lv_obj_get_style_translate_y(sh, LV_PART_MAIN), lv_obj_get_height(sh) + 20);
  lv_anim_set_duration(&a, FX_MOTION_FAST_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_start(&a);
  lv_anim_init(&a);
  lv_anim_set_var(&a, scrim);
  lv_anim_set_exec_cb(&a, scrim_opa);
  lv_anim_set_values(&a, lv_obj_get_style_bg_opa(scrim, LV_PART_MAIN), 0);
  lv_anim_set_duration(&a, FX_MOTION_FAST_MS);
  lv_anim_set_completed_cb(&a, sheet_deleted);
  lv_anim_start(&a);
}

// ---- Spinner: a turning arc (stands in for the HostWaiting artboard's spoke spinner) ----

lv_obj_t* spinner(lv_obj_t* parent, int size, uint32_t color) {
  lv_obj_t* s = lv_spinner_create(parent);
  lv_obj_remove_style_all(s);
  lv_obj_set_size(s, size, size);
  lv_spinner_set_anim_params(s, 1000, 270);
  lv_obj_set_style_arc_width(s, 2, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s, col(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(s, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s, col(color), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s, LV_OPA_20, LV_PART_MAIN);
  return s;
}

}  // namespace fx::ui
