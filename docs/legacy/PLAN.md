# FenixDeck v1 plan

> History, frozen: the FenixDeck PoC, not current fenixOS facts. Current: `docs/board-info.md`, `docs/PLAN.md`, `proto/spec.md`.

PC hardware monitor: a **Rust + Tauri 2** PC app collects all stats and does all processing. It pushes compact data to the **ESP32-2432S028R** over USB serial. The board firmware (**C++, ESP-IDF, LovyanGFX**) only receives data and draws it efficiently.

Status: **v0.1 done (2026-10-28).** All phases executed; see `tasks.md` and README "Measured". This doc is updated to describe the system as built; §10 lists where the build departed from the original plan.

## 1. Decisions (from user, 2026-10-28)

| Topic | Decision |
|---|---|
| Transport | USB serial first. Protocol is transport-agnostic so Wi-Fi (TCP/WebSocket) can be added later |
| Firmware | C++ on ESP-IDF v5.5.x (`idf.py`), LovyanGFX as a component |
| Rendering | Hybrid: board draws widgets from data packets; PC can also push small RGB565 images (icons etc.) |
| Orientation | Configurable from the PC app (landscape 320x240 / portrait 240x320, plus 180° flips) |
| Metrics v1 | CPU, GPU (NVIDIA), RAM + disk, network + battery |
| PC UI | Tauri 2 + Svelte 5 + TypeScript |
| Touch v1 | Tap cycles stat pages on the board **and** every touch event is sent to the PC |
| CPU temps | LibreHardwareMonitor (LHM) web server `data.json`. The app may ask for UAC elevation to launch LHM |
| PC app behaviour | System tray (close to tray) + auto-detect/auto-reconnect board. No autostart in v1 |
| Board offline | "Waiting for PC" screen; backlight dims after a timeout |
| Default poll rate | 1 s, configurable in the PC app (250 ms to 10 s) |

## 2. Hardware facts that shape the design

Pin details are in `pics/ANALYSIS.md`; chip details are in `docs/board-info.md`.

1. **No hardware vsync.** The ILI9341 TE (tearing effect) pin is not wired on this board (LCM 18-pin connector: no TE). True vsync is impossible. Mitigations:
   - Render each widget into an off-screen sprite in RAM, then DMA-push only the changed rectangle. No flicker, and tearing is limited to tiny regions.
   - Cap frame rate (default 30 fps, max 60) and redraw only dirty widgets.
   - Full-screen redraws only on page or orientation change. At 55 MHz SPI a full frame (153,600 B) takes about 22 ms, so 40+ fps is possible.
2. **No PSRAM.** Planning estimate: about 300 KB of free heap, sprite budget **≤ 96 KB**. No full-screen framebuffer (150 KB); per-widget sprites only. As built: 2 × 40 KB sprite buffers, ~159 KB heap still free (measured).
3. **UART0 is the link and also the boot/log console.** Firmware must silence IDF console logs on UART0 (`CONFIG_ESP_CONSOLE_NONE` or the secondary console off, log level none). Debug logs go inside protocol `LOG` frames instead. The ROM bootloader still prints at reset; the PC side must ignore garbage until the handshake.
4. **DTR/RTS drive the auto-reset transistors (T1/T2).** The PC opens the port with DTR and RTS released (`fenix_link::open_port`); verified that this does not reset the board. Any reset is handled as reconnect + re-handshake.
5. **Link speed.** CH340C is rated 2 Mbps, but 921600 failed during a bulk flash read here, while 460800 was solid. Start at **460800** (about 46 KB/s). Metrics frames are about 200 B, and a 48x48 icon is 4.6 KB. Try 921600 later with CRC error counting.
6. **Touch is on its own SPI bus** (CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36). LovyanGFX `Touch_XPT2046` on `SPI3_HOST` (VSPI). The UI loop polls `getTouch()` at ~30 Hz; LovyanGFX checks the IRQ pin first, so an untouched poll costs no SPI traffic.
7. **Display bus:** HSPI SCK 14, MOSI 13, MISO 12, CS 15, DC 2, RST -1 (tied to EN), backlight PWM GPIO21.
8. **Flash 4 MB.** App partition 2 MB (firmware is 334 KB), no OTA in v1, a 1.9 MB `storage` partition reserved for fonts/icons later.

## 3. Architecture

```
┌──────────── PC (Windows) ────────────┐        USB serial 460800        ┌──────── ESP32 board ────────┐
│ Svelte UI  ◄─events/commands─►  Rust │ ◄── COBS frames + CRC16 ──────► │ link task (core 0)          │
│ (dashboard, settings, board status)  │                                  │   RX/TX, decode, dispatch   │
│                                      │                                  │ state store (latest values) │
│ fenix-metrics: PDH, sysinfo, LHM,    │                                  │ render task (core 1)        │
│   (NVML fallback), battery → Sample  │                                  │   pages → widgets → sprites │
│ fenix-link: find CH340, handshake,   │                                  │   dirty rects → DMA push    │
│   heartbeat, reconnect               │                                  │ touch polled in render task │
│ fenix-proto: shared frame format     │                                  │   tap → next page + event   │
└──────────────────────────────────────┘                                  └─────────────────────────────┘
```

### Repo layout

```
Cargo.toml              # Rust workspace
crates/fenix-proto/     # protocol types, encode/decode, COBS, CRC, golden test vectors
crates/fenix-metrics/   # collectors (CPU, GPU, mem, disk, net, battery, LHM)
crates/fenix-link/      # serial discovery, handshake, heartbeat, reconnect
crates/fenix-cli/       # `fenix` dev tool: ports, ping, demo/live data, sample, logo, shot/rawshot
app/                    # Tauri 2 app (src-tauri + Svelte frontend)
board/                  # ESP-IDF project (main/, components/fenix_proto, components/LovyanGFX, partitions.csv)
tools/                  # validate_metrics.py, measure_app.ps1, replug_watch.ps1
docs/legacy/protocol.md        # wire spec (source of truth for both sides)
firmware/stock/         # stock flash backup (exists)
```

## 4. Protocol v1 (summary; full spec in `docs/legacy/protocol.md`)

- **Framing:** `COBS(payload + CRC16-CCITT)` terminated by `0x00`. Easy to resync after boot garbage.
- **Payload:** `[type u8][seq u8][body]`, little-endian, fixed-point integers (no floats on the wire). No length field: COBS framing gives it.
- **Versioning:** `HELLO` carries `proto_version`. A mismatch shows an error on both sides.

| Type | Dir | Purpose |
|---|---|---|
| `HELLO` / `HELLO_ACK` | PC→B / B→PC | magic `FNXD`, proto ver; ack returns fw ver, screen size, free heap, MAC |
| `PING` / `PONG` | both | heartbeat every 1 s; 3 missed = disconnected |
| `CONFIG` | PC→B | rotation, brightness, dim timeout, enabled pages, fps cap, units |
| `METRICS` | PC→B | one snapshot: fixed struct with a field-present bitmask |
| `CORES` | PC→B | per-core usage array (16 threads on i7-13620H), 1 byte each |
| `DISKS` / `INFO` | PC→B | per-drive used/total; CPU/GPU/host names |
| `IMAGE_BEGIN/CHUNK/END` | PC→B | RGB565 image into a slot (≤ 64×64), chunked ≤ 1 KB; each frame has its own CRC |
| `TOUCH` | B→PC | x, y, kind (down/up/tap/long), current page |
| `PAGE` | both | board reports page change; PC can force a page |
| `LOG` | B→PC | firmware debug text |
| `ERROR` | both | code + text |
| `STATS` | B→PC | heap, fps, render time, rx frames/errors every 5 s |
| `SHOT_REQ` / `SHOT_ROW` | PC→B / B→PC | LCD read-back screenshot |

## 5. Metrics v1

| Group | Fields | Source (Rust) |
|---|---|---|
| CPU | total %, per-core %, live clock, package temp °C, power W | PDH `Processor Information` (utility, per-core, performance × frequency); temp/power via LHM `data.json` |
| GPU | util %, temp °C, VRAM used/total, power W, core clock | Awake check via SetupAPI power state (D0), so FenixDeck never wakes the dGPU; asleep shows "idle". VRAM used from PDH `GPU Adapter Memory`. Temp/power/clock/util/VRAM total from LHM when it runs, else `nvml-wrapper` (only while D0), else util from PDH `GPU Engine` |
| RAM | used/total GB, % | `sysinfo` |
| Disk | per-drive used/total, read/write MB/s | `sysinfo` disks (space, refreshed every 30 s); PDH `PhysicalDisk(_Total)` (I/O) |
| Network | up/down KB/s summed over real adapters | `sysinfo` networks; skips zero-MAC filter-driver interfaces and virtual/loopback adapters |
| Battery | %, charging/discharging, time left, power W | `GetSystemPowerStatus` each sample; power W from `starship-battery` on a background thread (it takes ~115 ms) |

The LHM collector is optional and polled on its own thread. When LHM is not running, CPU temp/power are absent (the board shows "–") and GPU sensors fall back to NVML. The app has a button (and an optional autostart) to launch LHM elevated (UAC prompt), off the main thread. Accuracy is checked by `tools/validate_metrics.py` (`docs/legacy/metrics-validation.md`).

## 6. Board firmware design

- **Tasks:** `link_rx` (core 0, prio 10) decodes frames and answers PING/HELLO; `ui` (core 1, prio 5) renders and polls touch. Latest-value state store guarded by a mutex; the UI copies a snapshot per frame. A 1 s timer logs if the UI loop stalls.
- **Pages v1:** Overview, CPU (per-core bars + temp), GPU, Memory/Disk, Network/Battery. Each page has a landscape and a portrait layout.
- **Widgets:** header, tile, arc gauge, key/value rows, per-core bars, sparkline (60-sample rings), bar rows. Each widget is rendered into one of two shared 40 KB ping-pong sprite buffers and redrawn only when a hash of its displayed content changes.
- **Frame loop:** wake on new data or after ≤ 33 ms, render dirty widgets, `pushImageDMA`, never faster than the fps cap.
- **Offline:** waiting screen with logo and status; backlight PWM fades to the dim level after the timeout (default 5 min); restored when data arrives.
- **Touch:** default raw range; hold the screen at boot to calibrate (stored in NVS). Tap: next page + `TOUCH` event; down/up/long are reported too. Long-press is reserved for the PC.
- **Size targets:** firmware ≤ 1 MB (built: 334 KB), heap free ≥ 100 KB (built: ~159 KB).

## 7. PC app design

- **Rust backend:**
  - Engine thread (plain std thread, no async runtime) polls the collector at the configured rate.
  - Each sample is sent to the board, and emitted to the UI only while the window is open and not minimised.
- **Link manager:**
  - Enumerate ports and match VID:PID `1A86:7523`, open at 460800 with DTR/RTS safe, then `HELLO`.
  - One thread per session: batch writes, then reads with a 15 ms timeout.
  - On failure or unplug, rescan every 1 s (measured replug recovery ~1.3 s).
- **UI (Svelte):**
  - Live dashboard mirroring the board, connection/board info panel.
  - Settings: poll rate, orientation, brightness, dim level and timeout, fps cap, pages, °F, overlay, serial port, LHM toggles; plus a logo upload.
  - Touch event log.
- **Tray:** menu (Show, Reconnect, Quit). Closing the window destroys the webview while the engine keeps running. Single instance; `fenixdeck --quit` exits cleanly.
- **Settings** are persisted as JSON at `%APPDATA%\dev.fenixdeck.app\settings.json` (no store plugin).
- **Lightweight targets:**
  - PC app: < 1% CPU at a 1 s poll and < 100 MB RAM (built: window open ~0.17% CPU / 96–97 MB; tray 0.07% / 29 MB, measured with `tools/measure_app.ps1`).
  - Release build uses `opt-level="s"`, LTO, strip.

## 8. Phases (each ends with docs + commit + push)

| # | Phase | Done when |
|---|---|---|
| 0 | **Toolchain**: install ESP-IDF v5.5.x (EIM, brings cmake/ninja/python), pnpm, Tauri CLI; build an empty ESP-IDF + LovyanGFX project for ESP32 | LovyanGFX compiles on IDF 5.5; `cargo`/`pnpm tauri info` OK |
| 1 | **Protocol**: `docs/legacy/protocol.md`, `fenix-proto` crate + C++ `proto` component, shared golden test vectors | Rust and C++ tests decode each other's vectors |
| 2 | **Board skeleton**: display init, rotation, backlight PWM, waiting screen, UART link, HELLO/PING, LOG. 🔒 **flash needs user go-ahead** | Board shows waiting screen; `fenix-cli ping` gets PONG; no reset on port open |
| 3 | **PC link + CLI**: discovery, handshake, heartbeat, reconnect; `fenix-cli` sends fake metrics | Unplug/replug recovers in < 3 s |
| 4 | **Metrics**: all collectors + snapshot struct; CLI prints live values | Values match Task Manager/LHM within tolerance |
| 5 | **Board pages + widgets**: 5 pages × 2 orientations, sprites, dirty rects, fps cap | Live data at 1 s, no flicker; heap/fps logged |
| 6 | **Touch**: calibration, tap = next page, events to PC | Taps reliable with stylus; events visible on PC |
| 7 | **Tauri app**: UI, settings, tray, LHM launcher, image push (icons) | Full flow from the app; settings persist |
| 8 | **Polish + perf check**: measure PC CPU/RAM, board heap/fps, bundle size; README | Targets in §6–7 met and documented |

Later (not v1): Wi-Fi transport, OTA updates, PC-defined layouts, stream-deck actions on touch, SD card, autostart.

## 9. Open points

- Tap conflict: v1 = tap anywhere cycles pages *and* is reported. Later the PC may claim regions as buttons.
- IDF v6.0: not tried yet (v5.5.5 + LovyanGFX 1.2.35 works).
- Settled: EIM uses its own Python (`C:\Espressif\tools\python\v5.5.5\venv`).

## 10. Deviations from this plan (as built)

- **Payload has no length field:** `type ‖ seq ‖ body`. COBS framing already gives the frame length.
- **Screenshot messages added:** `SHOT_REQ`/`SHOT_ROW` read the LCD back (debug/verification).
- **IDF version:** v5.5.5, pinned. LovyanGFX is a git submodule (it is not in the component registry).
- **CPU clock source:** PDH `% Processor Performance` × base frequency (live), not sysinfo (static on Windows).
- **Close to tray destroys the webview:** the window is rebuilt when reopened from the tray. In the tray this saves ~65 MB and all WebView2 processes. (Not in the original plan; approved by user 2026-10-28.)
- **WebView2 runs lean:** `additionalBrowserArgs` in `tauri.conf.json` put GPU work in-process with software compositing, keep the network service in-process, and allow one renderer. With them the window-open footprint is 96–97 MB. (Corrected: an earlier "~215 MB before" figure wrongly included another app's WebView2 processes, so there is no reliable before number.) Any `--js-flags` value brought back an extra process (+60 MB), so none is used.
- **GPU sensors come from LHM when it runs**, to avoid loading NVML in-process (-19 MB); NVML is the fallback.
- **The window is created in `setup`** after the app state exists (config `create: false`), avoiding a startup race.
- **Touch calibration:** runs only when the screen is held at boot. Otherwise the default raw range is used.
