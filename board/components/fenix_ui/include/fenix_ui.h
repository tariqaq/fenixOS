// LVGL glue, theme widgets and the screen stack. Runs on the UI task only.
// Sizes and colours follow the approved artboards (design/mockups, DESIGN.md).
#pragma once
#include <stdint.h>

#include "fenix_fonts.h"
#include "fenix_tokens.h"
#include "lvgl.h"

namespace fx::ui {

// ---- Core ----
void init(uint8_t rotation);
// Run LVGL once; returns ms until the next call is due (capped).
uint32_t step();
void set_rotation(uint8_t rotation);  // rebuilds the screen stack
uint8_t rotation();
bool portrait();
int width();
int height();
uint16_t fps_x10();

// Backlight: brightness level 16..255; auto-dim after `dim_after_s` (0 = never) to `dim_pct`.
void backlight_config(uint8_t level, uint8_t dim_pct, uint16_t dim_after_s);
bool dimmed();
uint8_t backlight_now();  // the level right now, fades included
// Fade the backlight to 0 (blocking, ~FX_MOTION_SLOW_MS).
void backlight_fade_out();
// Burn-in protection (P21): the picture moves 1 px every minute within +-2 px, and the panel
// sleeps after 10 minutes without a touch (dimmed() is then true; a tap wakes it).
void burn_guard(bool on);
bool resting();  // the panel sleeps (burn-in protection)

// Stream hook: when on, frames are sent to the PC at up to `fps` (the PC's STREAM_START rate).
void stream_enable(bool on, uint8_t fps);
void stream_full_refresh();

// ---- Screen stack ----
// A builder fills a fresh screen. Screens are rebuilt on pop, so they keep no hidden state.
using Builder = void (*)(lv_obj_t* scr);
void nav_root(Builder b, bool animate);  // replace the whole stack
void nav_push(Builder b, bool fade = false);  // slide in, or fade in (app open)
void nav_pop();
void nav_rebuild();  // rebuild the top screen in place (rotation, setting changes)
Builder nav_top();
int nav_depth();

// ---- Widgets ----
lv_color_t col(uint32_t hex);
lv_obj_t* screen_new();
lv_obj_t* box(lv_obj_t* parent);  // plain container, no style, no scroll
lv_obj_t* column(lv_obj_t* parent, int gap);
lv_obj_t* rowbox(lv_obj_t* parent, int gap);
lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* f, uint32_t color);
// Top bar: "< back" (accent, 15 px) on the left and the title centred, 32 px tall.
lv_obj_t* navbar(lv_obj_t* scr, const char* back, const char* title);
// Scrolling content area under the navbar, with the side margin.
lv_obj_t* content(lv_obj_t* scr, int gap);
lv_obj_t* group(lv_obj_t* parent);  // grouped list surface, radius 12
void separator(lv_obj_t* group, int inset);
// 40 px list row: optional 24 px icon square, label, value, chevron.
lv_obj_t* list_row(lv_obj_t* group, const char* icon, uint32_t icon_bg, const char* label, const char* value,
                   bool chevron, lv_event_cb_t cb, void* ud);
// 30 px info row: label left, value right (13 px).
lv_obj_t* info_row(lv_obj_t* group, const char* label, const char* value);
// Capsule button: optional icon + label, filled.
lv_obj_t* capsule(lv_obj_t* parent, const char* icon, const lv_font_t* icon_font, const char* label, uint32_t bg,
                  int h, lv_event_cb_t cb, void* ud);
lv_obj_t* slider(lv_obj_t* parent, int min, int max, int value);
// On/off switch, 44 x 26.
lv_obj_t* toggle(lv_obj_t* parent, bool on);
// 40 px row: optional 24 px icon square, label, switch (returned; `cb` gets LV_EVENT_VALUE_CHANGED).
// A disabled row is greyed out and its switch ignores taps.
lv_obj_t* switch_row(lv_obj_t* group, const char* icon, uint32_t icon_bg, const char* label, bool on, bool enabled,
                     lv_event_cb_t cb, void* ud);
// Segmented control. `cb` gets LV_EVENT_VALUE_CHANGED with the new index as the event param
// (`(int)(intptr_t)lv_event_get_param(e)`).
lv_obj_t* segmented(lv_obj_t* parent, const char* const* opts, int n, int selected, lv_event_cb_t cb);
// Bottom sheet over a scrim on the top layer. Returns the sheet body (flex column).
lv_obj_t* sheet_open();
void sheet_close();
bool sheet_is_open();
// Spinner ring in the given colour.
lv_obj_t* spinner(lv_obj_t* parent, int size, uint32_t color);
// Tap feedback: darken on press.
void pressable(lv_obj_t* o);

}  // namespace fx::ui
