// fenixOS board simulator: the real fenix_ui / fenix_apps code on a PC (docs/toolchain.md).
//   fenix_sim run <script> --out <dir> [--store <dir>]   headless; writes the script's shots
//   fenix_sim window [--store <dir>] [--scale <n>]       a window; the mouse is the finger
// Scripts use the verbs of tools/capture_screens.sh (see board/sim/screens.txt).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fenix_apps.h"
#include "fenix_hal.h"
#include "fenix_sys.h"
#include "fenix_ui.h"
#include "freertos/task.h"
#include "sim.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace fx;

// Same init order as board/main/main.cpp.
static void boot() {
  hal::display_init();
  sys::nvs_start();
  const sys::Settings& st = sys::settings();
  ui::init(st.rotation);
  apps::start();
  ui::step();
  ui::backlight_config(st.brightness, st.dim_level, st.dim_after_s);
  ui::burn_guard(false);  // the pixel shift would move the golden screens (the setting shows on)
  hal::led_init();
  sys::clock_init();
  sys::store_mount();
  sys::link_start();
}

// One pass of the board's main loop; returns the ms it asked to wait.
static uint32_t pass() {
  uint32_t wait = ui::step();
  sys::LinkEvent e;
  while (sys::link_poll(&e)) apps::on_link(e);
  apps::tick();
  return wait ? wait : 1;
}

// Run the main loop for `ms` of virtual time.
static void run_for(int64_t ms) {
  int64_t end = sim::now_us() + ms * 1000;
  while (sim::now_us() < end) {
    int64_t left = (end - sim::now_us() + 999) / 1000;
    uint32_t wait = pass();
    vTaskDelay(pdMS_TO_TICKS(wait < left ? wait : (uint32_t)left));
  }
}

// ---- Scripts ----

static std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> w;
  std::string cur;
  bool quoted = false;
  for (char c : line) {
    if (c == '"') {
      quoted = !quoted;
    } else if (c == ' ' && !quoted) {
      if (!cur.empty()) w.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) w.push_back(cur);
  return w;
}

static int fail(const std::string& where, const std::string& why) {
  fprintf(stderr, "%s: %s\n", where.c_str(), why.c_str());
  return 1;
}

static int run_script(const std::string& path, const std::string& out) {
  std::ifstream f(path);
  if (!f) return fail(path, "cannot read it");
  fs::create_directories(out);
  fs::path base = fs::path(path).parent_path();
  std::string line;
  int n = 0, shots = 0;
  while (std::getline(f, line)) {
    n++;
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::vector<std::string> w = split(line);
    if (w.empty()) continue;
    std::string where = path + ":" + std::to_string(n);
    const std::string& v = w[0];
    auto num = [&](size_t i) { return i < w.size() ? atoi(w[i].c_str()) : 0; };
    if (v == "wait" && w.size() == 2) {
      run_for(num(1));
    } else if (v == "tap" && w.size() == 3) {
      sim::touch(true, num(1), num(2));
      run_for(120);
      sim::touch(false);
      run_for(1000);
    } else if (v == "swipe" && w.size() == 5) {
      // Like `fenix swipe`: 13 points 20 ms apart, then release.
      for (int i = 0; i <= 12; i++) {
        sim::touch(true, num(1) + (num(3) - num(1)) * i / 12, num(2) + (num(4) - num(2)) * i / 12);
        run_for(20);
      }
      sim::touch(false);
      run_for(1200);
    } else if (v == "shot" && w.size() == 2) {
      std::string file = (fs::path(out) / (w[1] + ".png")).string();
      if (!sim::write_png(file.c_str(), sim::width(), sim::height(), sim::pixels()))
        return fail(where, "cannot write " + file);
      printf("%s\n", w[1].c_str());
      shots++;
    } else if (v == "connect" || v == "disconnect") {
      sim::set_connected(v == "connect");
      run_for(300);
    } else if (v == "install" && w.size() == 2) {
      std::string id, err;
      fs::path p = fs::path(w[1]).is_absolute() ? fs::path(w[1]) : base / w[1];
      if (!sim::install(p.string(), &id, &err)) return fail(where, err);
      run_for(1000);
    } else if (v == "remove" && w.size() == 2) {
      if (!sim::remove(w[1])) return fail(where, w[1] + " is not installed");
      run_for(600);
    } else if (v == "set" && w.size() >= 3) {
      std::string err;
      if (!sim::set_binds(w[1], std::vector<std::string>(w.begin() + 2, w.end()), &err)) return fail(where, err);
      run_for(300);
    } else if (v == "stream" && (w.size() == 3 ? w[1] == "on" : w.size() == 2 && w[1] == "off")) {
      // `fenix shot` streams from the board, so the Stream screen there shows "Streaming".
      if (w[1] == "on") sim::post(sys::LinkEv::STREAM_START, num(2));
      else sim::post(sys::LinkEv::STREAM_STOP);
      run_for(300);
    } else if (v == "host" && w.size() == 3 && (w[2] == "on" || w[2] == "off")) {
      sim::post(sys::LinkEv::HOST_STATE, w[2] == "on", w[1].c_str());
      run_for(300);
    } else {
      return fail(where, "unknown or malformed line: " + line);
    }
  }
  printf("%d shots in %s\n", shots, out.c_str());
  return 0;
}

// ---- Window ----

#ifdef _WIN32
static HWND s_wnd;
static int s_scale = 2;
static bool s_quit;

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_LBUTTONDOWN:
      SetCapture(h);
      [[fallthrough]];
    case WM_MOUSEMOVE:
      if (m == WM_LBUTTONDOWN || (wp & MK_LBUTTON))
        sim::touch(true, (short)LOWORD(lp) / s_scale, (short)HIWORD(lp) / s_scale);
      return 0;
    case WM_LBUTTONUP:
      ReleaseCapture();
      sim::touch(false);
      return 0;
    case WM_KEYDOWN:
      if (wp == 'C') sim::set_connected(!sys::link_connected());
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      int w = sim::width(), hh = sim::height();
      BITMAPINFO bi{};
      bi.bmiHeader.biSize = sizeof bi.bmiHeader;
      bi.bmiHeader.biWidth = w;
      bi.bmiHeader.biHeight = -hh;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      static std::vector<uint32_t> rgb;
      rgb.resize((size_t)w * hh);
      // The backlight level dims the picture, as on the panel.
      uint32_t bl = sim::backlight();
      const std::vector<uint16_t>& px = sim::pixels();
      for (size_t i = 0; i < rgb.size(); i++) {
        uint16_t c = px[i];
        uint32_t r = (c >> 11) * 255 / 31 * bl / 255, g = ((c >> 5) & 63) * 255 / 63 * bl / 255,
                 b = (c & 31) * 255 / 31 * bl / 255;
        rgb[i] = r << 16 | g << 8 | b;
      }
      StretchDIBits(dc, 0, 0, w * s_scale, hh * s_scale, 0, 0, w, hh, rgb.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_DESTROY:
      s_quit = true;
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

static void fit_window() {
  RECT r{0, 0, sim::width() * s_scale, sim::height() * s_scale};
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&r, style, FALSE);
  SetWindowPos(s_wnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

static void pump() {
  static int w, h;
  if (sim::width() != w || sim::height() != h) {
    w = sim::width();
    h = sim::height();
    fit_window();
  }
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  InvalidateRect(s_wnd, nullptr, FALSE);
  if (s_quit) exit(0);
}

static int run_window() {
  SetProcessDPIAware();  // whole pixels: the scale is ours, not the desktop's
  WNDCLASSW wc{};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_HAND);
  wc.lpszClassName = L"fenix_sim";
  RegisterClassW(&wc);
  s_wnd = CreateWindowW(L"fenix_sim", L"fenixOS simulator (C toggles the PC link)",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                        640, 480, nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(s_wnd, SW_SHOW);
  sim::realtime = true;
  sim::pump = pump;
  pump();
  for (;;) vTaskDelay(pdMS_TO_TICKS(pass()));
}
#endif

int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "", script, out = "sim_shots", store;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) {
      out = argv[++i];
    } else if (a == "--store" && i + 1 < argc) {
      store = argv[++i];
#ifdef _WIN32
    } else if (a == "--scale" && i + 1 < argc) {
      s_scale = atoi(argv[++i]) > 0 ? atoi(argv[i]) : 2;
#endif
    } else if (mode == "run" && script.empty()) {
      script = a;
    } else {
      mode.clear();
    }
  }
  if (!(mode == "run" && !script.empty()) && mode != "window") {
    fprintf(stderr,
            "usage: fenix_sim run <script> --out <dir> [--store <dir>]\n"
            "       fenix_sim window [--store <dir>] [--scale <n>]\n");
    return 2;
  }
  // A fresh store per run unless one is given, so runs repeat.
  if (store.empty()) {
    store = (fs::temp_directory_path() / "fenix_sim_store").string();
    std::error_code ec;
    fs::remove_all(store, ec);
  }
  sim::store_dir(store);
  setvbuf(stdout, nullptr, _IONBF, 0);
  boot();
  sim::set_connected(true);  // the screens show a board on the PC link, as on the bench
  if (mode == "run") return run_script(script, out);
#ifdef _WIN32
  return run_window();
#else
  fprintf(stderr, "window mode needs Windows\n");
  return 2;
#endif
}
