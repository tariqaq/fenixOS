// Stream: link status and the screen stream to the PC. Artboards StreamOff, StreamOn.
#include <stdio.h>

#include "fenix_proto.h"
#include "shell.h"

namespace fx::apps {

using namespace fx::ui;

static void start_cb(lv_event_t*) { stream_start_local(); }
static void stop_cb(lv_event_t*) { stream_stop_local(); }

static lv_obj_t* centre_column(lv_obj_t* scr, int gap, int pad_hor, int pad_bottom) {
  lv_obj_t* c = column(scr, gap);
  lv_obj_set_width(c, LV_PCT(100));
  lv_obj_set_flex_grow(c, 1);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(c, pad_hor, 0);
  lv_obj_set_style_pad_bottom(c, pad_bottom, 0);
  return c;
}

static void message(lv_obj_t* c, const char* icon, const char* title, const char* body) {
  text(c, icon, &fx_icon_fill_40, FX_COLOR_LABEL_2);
  lv_obj_set_style_margin_top(text(c, title, &fx_font_title, FX_COLOR_LABEL), 4, 0);
  lv_obj_t* b = text(c, body, &fx_font_label, FX_COLOR_LABEL_2);
  lv_obj_set_width(b, LV_PCT(100));
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(b, 2, 0);
}

void stream_screen(lv_obj_t* scr) {
  navbar(scr, "Home", "Stream");
  const Shell& sh = shell();
  if (!sh.connected) {
    message(centre_column(scr, 6, 24, 16), FX_ICON_SCREENCAST, "No PC connection",
            "Connect the board by USB and open fenixOS on your PC to stream this screen.");
    return;
  }
  if (!sh.streaming) {
    lv_obj_t* c = centre_column(scr, 6, 24, 16);
    message(c, FX_ICON_SCREENCAST, "Not streaming",
            "Start the stream here or on the Board page in fenixOS on your PC.");
    lv_obj_t* b = capsule(c, FX_ICON_SCREENCAST, &fx_icon_fill_14, "Start streaming", FX_COLOR_ACCENT_FILL, 36, start_cb,
                          nullptr);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_style_margin_top(b, 8, 0);
    return;
  }
  lv_obj_t* c = centre_column(scr, 10, 16, 12);
  lv_obj_t* head = rowbox(c, 8);
  text(head, FX_ICON_SCREENCAST, &fx_icon_fill_28, FX_COLOR_ACCENT);
  text(head, "Streaming to your PC", &fx_font_title, FX_COLOR_LABEL);
  lv_obj_t* g = group(c);
  char v[32];
  snprintf(v, sizeof v, "%u fps", (unsigned)sh.stream_fps);
  info_row(g, "Rate", v);
  separator(g, 10);
  snprintf(v, sizeof v, "USB, %u baud", (unsigned)fnx::BAUD);
  info_row(g, "Link", v);
  lv_obj_t* b = capsule(c, FX_ICON_X_CIRCLE, &fx_icon_fill_14, "Stop streaming", FX_COLOR_SURFACE_2, 36, stop_cb,
                        nullptr);
  lv_obj_set_width(b, LV_PCT(100));
}

}  // namespace fx::apps
