// Shared state and builders of the system screens (private to fenix_apps).
#pragma once
#include <vector>

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
void settings_tick();  // refreshes Wireless when the radio's state changes
void stream_screen(lv_obj_t* scr);
void session_open();
// Stream rate when started on the board (the PC asks for the same).
constexpr uint8_t STREAM_FPS = 10;
void stream_start_local();
void stream_stop_local();
uint8_t current_screen();  // fnx::screen value

// RGB LED status (PLAN §3: host connected = short green pulse, busy = dim blue, error = dim red).
void led_pulse_connected();
void led_busy(bool on);
void led_error(bool on);

// Host apps (app_pkg.cpp).
struct AppTile {
  char id[49];
  char name[32];
  uint32_t color;
  std::vector<uint8_t> px;     // icon pixels (small then large, A8)
  lv_image_dsc_t small, large;  // data_size 0 = no icon
};
void apps_reload();  // read the store (boot, install, remove)
const std::vector<AppTile>& app_tiles();
const AppTile* app_ghost();  // a removed tile still to fade out on Home, or nullptr
void app_ghost_done();
bool app_take_fade_in(const char* id);  // true once, for a newly installed tile
void app_open(const char* id);
void app_screen(lv_obj_t* scr);
const char* open_app_id();  // "" when no app is open
void app_on_link(const sys::LinkEvent& e);
void app_tick();

// Rebuild the top screen if it is `b` (refresh a visible screen after a state change).
void refresh_if_top(fx::ui::Builder b);

}  // namespace fx::apps
