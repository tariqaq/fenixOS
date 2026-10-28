// Session sheet: Shut down (deep sleep, tap to wake) or Reboot. Artboard Session.
#include "esp_system.h"
#include "fenix_hal.h"
#include "shell.h"

namespace fx::apps {

using namespace fx::ui;

static void shutdown_cb(lv_event_t*) {
  sys::link_log(3, "shutting down (deep sleep, tap to wake)");
  sheet_close();
  backlight_fade_out();
  hal::led_set(0, 0, 0);
  hal::deep_sleep_until_touch();
}

static void reboot_cb(lv_event_t*) {
  sys::link_log(3, "rebooting");
  backlight_fade_out();
  esp_restart();
}

static void cancel_cb(lv_event_t*) { sheet_close(); }

void session_open() {
  lv_obj_t* sh = sheet_open();
  lv_obj_t* head = column(sh, 2);
  lv_obj_set_width(head, LV_PCT(100));
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(head, "Session", &fx_font_title, FX_COLOR_LABEL);
  lv_obj_t* sub = text(head, "Shut down sleeps the board. Tap the screen to wake it.", &fx_font_caption,
                       FX_COLOR_LABEL_2);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(sub, LV_PCT(100));

  lv_obj_t* r = rowbox(sh, 8);
  lv_obj_set_width(r, LV_PCT(100));
  lv_obj_set_style_margin_top(r, 4, 0);
  lv_obj_set_flex_grow(
      capsule(r, FX_ICON_POWER, &fx_icon_fill_16, "Shut down", FX_COLOR_DANGER_FILL, 40, shutdown_cb, nullptr), 1);
  lv_obj_set_flex_grow(
      capsule(r, FX_ICON_ARROW_CLOCKWISE, &fx_icon_fill_16, "Reboot", FX_COLOR_SURFACE_2, 40, reboot_cb, nullptr), 1);

  lv_obj_t* cancel = box(sh);
  lv_obj_set_size(cancel, LV_PCT(100), 32);
  lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(cancel, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_center(text(cancel, "Cancel", &fx_font_body, FX_COLOR_ACCENT));
  lv_obj_add_event_cb(cancel, cancel_cb, LV_EVENT_CLICKED, nullptr);
}

}  // namespace fx::apps
