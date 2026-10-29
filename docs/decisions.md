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

## 2026-10-29: round 6, PC behaviour and board extras

| Topic | Options | Picked |
|---|---|---|
| Start with Windows | Setting, on by default (Rec.) / setting, off by default / no autostart | **Setting, off by default**: start minimised to tray at login when turned on |
| PC app self-update | **Tauri updater + GitHub Releases (Rec.)** / notify only / manual | **Tauri updater + GitHub Releases** on this repo: signed updates (signing key kept secret, never in git); after updating, the app offers to flash the matching board firmware |
| Close window | **Destroy webview (Rec.)** / hide window | **Destroy webview** (PoC-proven: tray ~29 MB, 1 process); engine, board link and sidecar apps keep running |
| Board extras (multi-select) | RGB LED status light / auto-brightness (LDR) / UI sounds / none | **RGB LED status light** (subtle states such as host connected / waiting / error; V1.4 LED resistors unverified, so brightness needs tuning). Not in v0.1: LDR auto-brightness, sounds |

## 2026-10-29: round 7, mock-up review

| Topic | Options | Picked |
|---|---|---|
| Home layout | Tiles (2a) / icon dock (2b) | **Both**, chosen by the user in board Settings › Home screen (Tiles / Dock) |
| Boot animation | Spinner (first draft) / other | **Grey track with a white line sweeping left to right** |
| Tile colours, PC tile size, switch place | Keep / change | **Keep** as drawn |
| Elevation | "Run as administrator" setting / per action (Rec.) | **Per action**: no admin setting; an app's PC half marked `elevated` starts through a UAC prompt, the rest of fenixOS never runs elevated |
| Tray menu | Draw it | **Open fenixOS, board status line, Quit** |
| First run | Draw it | **OOTB with a 3D board model**: if a board is already plugged in, wait 3 s, then a smooth "plugged in" animation and continue; otherwise animate a plug-in prompt. Creative direction left to Claude, within the design language |
| Single instance | Yes / no | **Yes** |
| App permissions sheet before install | Yes / no | **Yes** |
| Log view | Yes / no | **Yes** |
| Board UI simulator on the PC for testing | Yes / no | **Yes** |
| The canvas | Review | **Approved: build it exactly as drawn** |

## 2026-10-29: before the build

| Topic | Options | Picked |
|---|---|---|
| Licence | **MIT (Rec.)** / Apache-2.0 / none | **MIT** |
| "Show welcome again" in PC Settings | Yes / no | **Yes** |
| OOTB 3D direction | Storyboard review / Claude's call | **Claude's call**, within the design language; no review needed. A storyboard may still be drawn as Claude's own reference |
| Targets | Hard, tight targets / realistic | **PC: realistic targets, no hyper-optimising. Board: squeeze for efficiency** |
| Unattended run (user asleep, `/goal` for all phases) | — | One UAC prompt at the start gives an elevated helper used for any later elevation. Keep the board's COM port enumerated (flashing and resets are fine; never anything that needs a physical replug). Kill and relaunch the PC app to see changes. Window resizing and UI interaction allowed. Web search allowed. Any further choice: pick the recommended option and log it here with the alternatives |

## 2026-10-29: during the build (choices made on the user's behalf, per the unattended-run rules)

| Topic | Options | Picked |
|---|---|---|
| Screenshots of board screens while nobody is at the board | SDL2 simulator first / the real board over the link | **Real board over the link** (`fenix tap` / `fenix swipe` drive it, `fenix shot` saves the stream as PNG; `tools/capture_screens.sh`). Exact pixels, no stub HAL. The PC simulator the user asked for in round 7 was built after P9 (`board/sim`, see the simulator rows below) |
| Driving the board from the PC | No remote input / a TOUCH message | **TOUCH (0x13, P→B)**, optional, no proto bump. Also gives the PC app's board mirror click-to-tap later |
| Board "Stop streaming" button (StreamOn artboard) | Ask the PC to stop / stop locally | **Stop locally** and send a LOG line; the PC sees the stream go quiet. No new message in proto 1 |
| Stream screen when the PC is connected but not streaming (no artboard) | Reuse StreamOff / new state | **New state** in the StreamOff layout: "Not streaming", "Open the board page in fenixOS on your PC to see this screen there." |
| Rotation setting UI (artboard shows a Rotation row with a chevron) | Cycle on tap / sub-page | **Sub-page** with the 4 orientations and a check mark |
| Navbar on narrow (portrait) screens when back label and title collide | Truncate / drop the back label | **Drop the back label**, keep the chevron (iOS does the same) |
| Settings › Storage and Host PC pages (no artboards) | — | Grouped info rows in the About style |
| Package size cap (PLAN said 256 KB) | 256 KB, read in pieces / **64 KB, whole package in RAM** | **64 KB.** The board has ~125 KB free heap, and the package is read whole to check it and while the app is open. Assets other than the two tile icons count toward the 24 KB image budget, so real packages are a few KB |
| Asset compression (PLAN said compressed) | Compress now / **raw in format 1** | **Raw.** With the 64 KB cap compression buys little; a later format version can add it |
| RGB565 byte order in packages | Big-endian (screen order) / **little-endian** | **Little-endian**, LVGL's native order, so images draw straight from the package buffer |
| App id length | 63 bytes / **48 bytes** | **48 bytes** of letters, digits, `.`, `-`, `_`: ids name files on LittleFS, whose names are ≤ 63 bytes |
| Number of installed apps | Unlimited / **10** | **10** (`MAX_APPS`): APP_LIST fits one frame, and Home stays usable |
| App open motion (DESIGN.md: scale 0.9 → 1 + fade from the tile) | Full-screen scale / **fade** | **Fade in.** A full-screen scale is a transformed layer every frame, far over the board budget; tile install / remove keep the scale + fade (small area) |
| Store index (PLAN: "a small index") | Index file / **`.sha` next to each package** | **`<id>.sha`**; id and version are read from the package header for APP_LIST |
| Bound values while an app is open | Only what is on screen / **cache per binding** | **Cache per binding** while the app is open (cleared on close), so rotation and host-state rebuilds keep the values |
| `APP_ACK` after `APP_INSTALL_BEGIN` | Silent / **ack offset 0** | **Ack 0**: the PC knows the install started; a refused BEGIN gets `APP_RESULT` instead |
| Tray menu (P7) | Custom webview popup / **native menu** | **Native menu** (status line, detail line, Open fenixOS, Quit): no webview in tray-only mode keeps it at about 4 MB; the styled popup can come later without changing the engine |
| P7 Apps and Settings pages | Build P8 content early / **placeholders** | **Placeholders**: Apps shows the empty state, Settings has Start with Windows and About. P8 fills them from artboards 8–13 |
| Spotting a board restart while the PC still holds the session | Wait for the 3 s heartbeat timeout / **board says "no session"** | **`ERROR` 5 (no session)** in reply to a PING before HELLO; the PC handshakes again at once. A restarting board answers PINGs within about 1 s, so the timeout alone never fired. Additive code, so proto stays 1 |
| Reboot log line | Problem / **normal** | **Normal**: "Board disconnected (board restarted)" is not marked as a problem; the next boot line gives the reset reason, which shows a crash |
| Sidecar pipe (PLAN: one named pipe per app) | One duplex pipe / **two one-way pipes** | **`<base>.down` + `<base>.up`**: a blocking read and a write on one synchronous handle wait for each other on Windows (the serial-port gotcha), so each direction gets its own pipe. Any language can still use plain file I/O |
| Sidecar start argument | Environment variable / **command-line argument** | **`--fenix-pipe <base>`**: an elevated start through UAC does not inherit the caller's environment |
| Public registry content in v0.1 | Ship the fixture / **empty** | **Empty** (`registry/index.json` has no apps): the test fixture is served from a local registry (`FENIX_REGISTRY`), per PLAN §1.2 |
| After Add app | Leave off / **switch on** | **Switch on**: adding is the intent to use it; the dashboard switch turns it off again |
| Apps on the board that the PC does not have switched on | Keep / **remove** | **Remove** at reconcile: the PC is the source of truth (PLAN §3.5). Apps put on by `fenix install` during development are removed when the app connects |
| Mirror fps chip | Board rects per second / **pictures shown** | **New pictures the PC showed per second** (max 10): the stream has no frame marker, and a frame is many rects |
| Clicks on the mirror | View only / **touches** | **Touches** (`TOUCH` messages): a press is held ≥ 100 ms because the board keeps only the latest touch state |
| Firmware writes | Full image / **only changed regions** | **Only changed regions** (espflash `skip`, MD5 per region): a same-version Reinstall takes ~10 s instead of ~23 s; every written region is verified |
| Flash baud | 115200 / 921600 / **460800** | **460800**, the speed `board/idf.ps1` uses and proven on the CH340C; a full update takes ~23 s |
| Firmware in the PC app | Download on demand / **bundled resource** | **Bundled** (`resources/firmware`, ~0.85 MB): the board and the app always match without a network, as PLAN §3.8 lockstep needs |
| Mismatch "Later" | Ask again on every state change / **until next connect** | **Until the board connects again**: no nagging while it stays plugged in; the Board page still offers Reinstall |
| Updater timing | P10 / **P8 with Settings** | **P8**: the Updates rows in artboard 13 need a real check; the key was made locally then, and P10 only adds CI signing and a release test |
| "No release yet" on update check | Error / **up to date** | **Up to date**: the updater's `ReleaseNotFound` (404 on `latest.json`) means nothing newer exists, so it is not shown as a failure |
| Update check at start | Never / **once, quiet** | **Once, quiet** when `check_updates` (default on): only a found update is logged; Settings shows the result |
| Connection port picker | Text field / native select / **menu** | **Menu** under the row ("Automatic" + each USB serial port, CH340s marked): matches the up-down caret in the artboard; a saved port that is unplugged stays listed |
| Port change | Restart the link / **retarget** | **Retarget**: `Link::set_port` ends the session ("port changed") and the next scan uses the new choice, about 0.1 s |
| Open-source licenses | Full texts / **name, version, license list** | **List** generated by `tools/licenses.py` (CI-checked); the full texts ship with each project's source |
| "Show welcome again" | Placeholder now / **with P9** | **With P9**: the row only makes sense once the welcome flow exists |
| Mirror tearing | Frame marker now (proto 2) / **accept for v0.1** | **Accept**: the board streams dirty rects with no frame-end marker, so a poll can catch half a frame during fast motion. Static and slow screens are clean. A STREAM frame-end message is the fix, for a later protocol version |
| OOTB rendering (S8) | Lean args (software WebGL) / GPU for every window / **GPU only in the welcome window** | **Own window, own WebView2 folder, GPU on**: software WebGL cost 40 % of all CPU and 184 MB; with the GPU, 1 % and 89 MB. The window closes when the welcome ends, so the GPU process ends with it and the main window stays lean |
| First install | Write over / **erase the whole flash first** | **Erase first**, as the confirm says ("This erases what is on it"): another firmware's NVS and files must not meet fenixOS. ~25 s in all on this board |
| Board on old fenixOS during the welcome | Skip / **offer the update there** | **Offer it** ("Update fenixOS on this board?", apps and settings stay), the same flash as the update dialog |
| End of the welcome | Wait for a click / **auto after 3.5 s** | **Auto-open the main window after 3.5 s**, with "Open fenixOS" for the impatient. Skip opens the main window; closing the welcome window marks it done and leaves fenixOS in the tray |
| Board model screen | Static pictures / **drawn live** | **Drawn live** on a canvas from the firmware's own layout: the boot line, the install progress line, and Home with the PC clock, so it never shows a stale time |
| Board model colour | Real yellow PCB / **dark PCB** | **Dark PCB with gold pads**, as PLAN §3.9 says; sizes and part positions follow the real board |
| Simulator window (`board/sim`) | SDL2 / **plain Win32** | **Win32 GDI**: one `StretchDIBits` per frame, no library to fetch or license, nothing beyond MSVC; the simulator is a Windows dev tool like the PC app |
| Simulator time | Wall clock / **virtual** | **Virtual**: time moves only on script waits and `vTaskDelay`, and the clock reads Thursday 2026-10-29 08:35, so every run gives identical pixels and golden images can be compared exactly |
| Simulator golden images | Reuse `docs/screens/board/` / **own set** | **Own set** in `board/sim/golden/`, compared pixel for pixel in CI; the board captures stay as the reference for the real panel (they differ by ≤ 0.6%: clock minute, antialiasing) |
| Simulator stream screen | Not streaming / **streaming** | **Streaming** (`stream on 10` in the script): `fenix shot` streams from the board, so the real capture shows it that way |
| Release build | Hand-written upload steps / **tauri-action v1** | **`tauri-apps/tauri-action@v1`**: builds the NSIS bundle, signs the updater files and uploads them with `latest.json` in one step; the firmware is built in the IDF container job and handed over as an artifact |
| Updater test | `v0.1.1-test` pre-release / **old local build against the real release** | **Local build with VERSION 0.0.9** (not committed): the endpoint `releases/latest/download/latest.json` skips pre-releases, so a pre-release could never be offered; the real v0.1.0 tests the whole path |
| Installer scope | Per machine (UAC) / **per user** | **Per user** (`nsis.installMode: currentUser`, also Tauri's default, now explicit): no admin prompt, and the updater can replace it silently |
