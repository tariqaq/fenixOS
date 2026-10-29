// Simulator internals shared by its sources (the board code sees only the fx::hal / fx::sys
// headers and the IDF stubs).
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

#include "fenix_sys.h"

namespace sim {
namespace sys = fx::sys;

// ---- Clock (sim_platform.cpp) ----
int64_t now_us();
void advance_us(int64_t us);
// Window mode: vTaskDelay also sleeps for real and pumps the window.
extern bool realtime;
extern void (*pump)();

// ---- Screen and touch (sim_platform.cpp) ----
int width();
int height();
// Current picture as RGB565 (native order), width() × height().
const std::vector<uint16_t>& pixels();
uint8_t backlight();
void touch(bool down, int x = 0, int y = 0);

// ---- Fake link and store (sim_sys.cpp) ----
void store_dir(const std::string& dir);
void post(sys::LinkEv type, int32_t value = 0, const char* text = "");
void set_connected(bool on);
// `name=value` pairs for app `id`'s binds, resolved through its package; false if unknown.
bool set_binds(const std::string& id, const std::vector<std::string>& pairs, std::string* err);
bool install(const std::string& fxp, std::string* id, std::string* err);
bool remove(const std::string& id);

// ---- PNG (sim_png.cpp) ----
bool write_png(const char* path, int w, int h, const std::vector<uint16_t>& rgb565);

}  // namespace sim
