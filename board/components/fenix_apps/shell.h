// Shared state and builders of the system screens (private to fenix_apps).
#pragma once
#include "fenix_apps.h"
#include "fenix_ui.h"

namespace fx::apps {

struct Shell {
  bool connected;
  bool streaming;
  uint8_t stream_fps;
};
Shell& shell();

void boot_screen(lv_obj_t* scr);
void home_screen(lv_obj_t* scr);
void home_tick();
void settings_screen(lv_obj_t* scr);
void stream_screen(lv_obj_t* scr);
void session_open();
void stream_stop_local();
uint8_t current_screen();  // fnx::screen value

// RGB LED status (PLAN §3: host connected = short green pulse, busy = dim blue, error = dim red).
void led_pulse_connected();
void led_busy(bool on);
void led_error(bool on);

// Rebuild the top screen if it is `b` (refresh a visible screen after a state change).
void refresh_if_top(fx::ui::Builder b);

}  // namespace fx::apps
