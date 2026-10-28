# fenixOS decisions

Every design choice, in order, with the options offered. "(Rec.)" marks the option recommended at the time. The vision and constraints are in `ideas.md`.

## 2026-10-29: versions

| Topic | Options | Picked |
|---|---|---|
| Toolchain versions | Upgrade (IDF v5.5.6 / v6.x, CH340 driver 3.9) / keep current | **Keep current:** ESP-IDF v5.5.5, LovyanGFX 1.2.35, CH340 driver 3.3.2011.11 |

## 2026-10-29: round 1, board core

| Topic | Options | Picked |
|---|---|---|
| Board UI library | **LVGL 9 + LovyanGFX (Rec.)** / custom widgets on LovyanGFX (PoC style) / LVGL 8.3 + LovyanGFX | **LVGL 9 + LovyanGFX**: LVGL for widgets, layouts, easing animations, opacity, styles; LovyanGFX as the SPI/DMA LCD + XPT2046 touch driver |
| Board language | **C++17 on ESP-IDF 5.5.5 (Rec.)** / C only / Rust (esp-idf-svc) | **C++17 on ESP-IDF 5.5.5** |
| Board half of host apps | **Declarative packages (Rec.)** / declarative + Berry scripts / native ELF (`elf_loader`) / WASM (wasm3) | **Declarative packages**: layout + assets + bindings in a compact binary, drawn by the firmware UI engine; all logic in the PC half |
| Orientation | Landscape 320×240 (Rec.) / portrait 240×320 / switchable in Settings | **Switchable in Settings** (all 4 rotations). Every screen and every app layout must adapt to landscape and portrait (LVGL flex/grid layouts) |

Consequences:
- RAM: LVGL 9 + partial draw buffers + LovyanGFX within ~260 KB heap. Budget to be set in the plan; measure from the first build.
- Declarative packages mean the package format must describe layouts that reflow for both orientations.

## 2026-10-29: round 2, time, streaming, power, navigation

| Topic | Options | Picked |
|---|---|---|
| Time source (multi-select) | **PC sync on connect (Rec.)** / manual set in Settings / Wi-Fi NTP (later) / keep across soft reboot | **PC sync on connect** (on every connect + hourly), **keep across soft reboot** (time kept in RTC memory through Reboot and deep-sleep "shut down"), **Wi-Fi NTP later**. No manual set. Until the first sync after power-up the clock shows a placeholder (`--:--`) |
| Stream app transport | **Changed regions, compressed (Rec.)** / changed regions raw / wait for Wi-Fi | **Changed regions, compressed**: LVGL flush rectangles, RLE/LZ-compressed; the PC keeps the full canvas. ~10 fps in normal use, lower during full-screen animations |
| Session "Shut down" | **Deep sleep, tap to wake (Rec.)** / deep sleep, RST to wake / screen off only | **Deep sleep, tap to wake**: fade out, backlight off, deep sleep; touch IRQ (GPIO36) or RST wakes it and fenixOS boots |
| Board navigation | **Top bar back + home (Rec.)** / edge swipe back / both | **Top bar**: iOS-style nav bar in apps, `< Home` at top left, title centred |

## 2026-10-29: round 3, PC side

| Topic | Options | Picked |
|---|---|---|
| PC UI stack | **Tauri 2 + Svelte 5 (Rec.)** / Tauri 2 + vanilla TS / Slint | **Rust + Tauri 2 + Svelte 5** |
| PC half of host apps | **Sidecar process (Rec.)** / WASM plugin (Extism) / script (Rhai/Lua) | **Sidecar process**: each app ships a small exe (any language), started by fenixOS only while the app is switched on, talking to fenixOS over a local pipe with a typed API. No sandbox: trust comes from the registry (only the user's own repos), pinned versions and checksums |
| Registry access (repo was private) | GitHub token, optional (Rec.) / public registry repo / make fenixOS public / bundle in PC app | **Make this repo public**. The registry lives in this repo; the PC app reads it without auth |
| Firmware install/update | **PC app flashes via serial (Rec.)** / on-board OTA (A/B) / manual only | **PC app flashes via serial**: the firmware image is bundled with each PC app release and flashed through the ROM bootloader (Rust `espflash` library, DTR/RTS auto-reset). No OTA slots, so ~1.9 MB stays free for apps |

## 2026-10-29: going public

| Topic | Options | Picked |
|---|---|---|
| How to go public | **Scrub, then public (Rec.)** / scrub personal only / public as-is / stay private | **Scrub, then public**. Removed from all git history: the packing-slip photo (order number, forwarder account code) and the order number in `pics/ANALYSIS.md`, the whole vendor package (`docs/vendor/ESP32-2432S028R/`, `docs/vendor/CH340/`), and the flash images (`firmware/`; both contain vendor code). They stay on the local disk, gitignored. Force-pushed, then the repo was set to public |
| Old pre-scrub commits on GitHub (still reachable by exact hash) | Delete + recreate repo (Rec.) / ask GitHub Support / leave it | **Leave it**: the hashes are known only locally (the repo was private until the scrub) |

## 2026-10-29: round 4, design

| Topic | Options | Picked |
|---|---|---|
| Typeface (SF Pro not licensable off Apple platforms) | **Inter (Rec.)** / Geist / Manrope | **Inter** (OFL) on board and PC. Board: anti-aliased bitmap sizes generated with `lv_font_conv`; Inter Display cut for large sizes; tabular figures for the clock |
| Icons (SF Symbols not licensable off Apple platforms) | **Phosphor (Rec.)** / Lucide / Material Symbols Rounded | **Phosphor** (MIT): fill weight on tiles, regular in nav bars and settings rows |
| Board motion | **iOS-like, tuned (Rec.)** / minimal / rich | **iOS-like, tuned**: ease-out/spring curves, 200–350 ms; app open = quick scale+fade from its tile; nav push slides; tiles fade in/out on install/remove. Full-screen steps capped to what holds ~30 fps; small elements up to the fps cap |

## 2026-10-29: round 5, structure

| Topic | Options | Picked |
|---|---|---|
| PC window chrome | **Custom dark bar, Windows buttons (Rec.)** / macOS traffic lights / native Windows bar | **Custom dark title bar** (frameless, centred title, translucent) with Windows-style min/max/close on the right |
| Repo layout | **`board/` + `app/` + `crates/` (Rec.)** / `os/` + `desktop/` + `shared/` / `firmware/` + `pc/` + `libs/` | **`VERSION`, `board/`** (ESP-IDF, `components/fenix_proto`, `components/LovyanGFX` submodule), **`app/`** (Tauri + Svelte), **`crates/`** (fenix-proto, fenix-link, fenix-pkg, fenix-sdk), **`proto/`** (spec + golden vectors), **`registry/`**, **`design/`** (tokens), `docs/`, `pics/`, `tools/`. Local backups stay in ignored `firmware/` |
| Version policy | **Lockstep SemVer (Rec.)** / SemVer + protocol compat / CalVer | **Lockstep SemVer**: one `VERSION` file (0.1.0) feeds both builds; the board shows "fenixOS v0.1"; any mismatch on connect makes the PC app offer to flash its bundled firmware, and host apps stay off until matched |
| Extra board Settings items (multi-select) | Display: rotation + touch calibration / storage + host status / developer overlay / factory reset | **Display** (rotation picker, 4-point touch calibration) and **Storage + host status** (app flash used/free, PC link status, last time sync, time zone MYT). Not in v0.1: developer overlay, factory reset |
