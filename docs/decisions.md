# fenixOS decisions

Every design choice, in order, with the options offered. "(Rec.)" marks the option recommended at the time. The vision and constraints are in `ideas.md`.

## 2026-10-29: versions

| Topic | Options | Picked |
|---|---|---|
| Toolchain versions | Upgrade (IDF v5.5.6 / v6.x, CH340 driver 3.9) / keep current | **Keep current:** ESP-IDF v5.5.5, LovyanGFX 1.2.35, CH340 driver 3.3.2011.11. Corrected 2026-10-29: the CH340 driver should be updated (it crashed Windows, see below) |

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
| Session "Shut down" | **Deep sleep, tap to wake (Rec.)** / deep sleep, RST to wake / screen off only | **Deep sleep, tap to wake**: fade out, backlight off, deep sleep; touch IRQ (GPIO36) or RST wakes it and fenixOS boots. Corrected (P19): light sleep, then a restart on the tap, because deep sleep powers down the LED's GPIO16 rail and the LED glows green (`docs/board-info.md`) |
| Board navigation | **Top bar back + home (Rec.)** / edge swipe back / both | **Top bar**: iOS-style nav bar in apps, `< Home` at top left, title centred |

## 2026-10-29: round 3, PC side

| Topic | Options | Picked |
|---|---|---|
| PC UI stack | **Tauri 2 + Svelte 5 (Rec.)** / Tauri 2 + vanilla TS / Slint | **Rust + Tauri 2 + Svelte 5** |
| PC half of host apps | **Sidecar process (Rec.)** / WASM plugin (Extism) / script (Rhai/Lua) | **Sidecar process**: each app ships a small exe (any language), started by fenixOS only while the app is switched on, talking to fenixOS over a local pipe with a typed API. No sandbox: trust comes from the registry (only the user's own repos), pinned versions and checksums |
| Registry access (repo was private) | GitHub token, optional (Rec.) / public registry repo / make fenixOS public / bundle in PC app | **Make this repo public**. The registry lives in this repo; the PC app reads it without auth |
| Firmware install/update | **PC app flashes via serial (Rec.)** / on-board OTA (A/B) / manual only | **PC app flashes via serial**: the firmware image is bundled with each PC app release and flashed through the ROM bootloader (Rust `espflash` library, DTR/RTS auto-reset). No OTA slots, so ~1.9 MB stays free for apps. Corrected (P52, v0.1.22): two signed OTA slots and a 256 KB flash app store now (apps live on the SD card); serial flashing stays for the first install and old boards (`docs/ota.md`) |

## 2026-10-29: going public

| Topic | Options | Picked |
|---|---|---|
| How to go public | **Scrub, then public (Rec.)** / scrub personal only / public as-is / stay private | **Scrub, then public**. Removed from all git history: the packing-slip photo (order number, forwarder account code) and the order number in `pics/ANALYSIS.md`, the whole vendor package (`docs/vendor/ESP32-2432S028R/`, `docs/vendor/CH340/`), and the flash images (`firmware/`; both contain vendor code). They stay on the local disk, gitignored. Force-pushed, then the repo was set to public |
| Old pre-scrub commits on GitHub (still reachable by exact hash) | Delete + recreate repo (Rec.) / ask GitHub Support / leave it | **Leave it**: the hashes are known only locally (the repo was private until the scrub) |

## 2026-10-29: round 4, design

| Topic | Options | Picked |
|---|---|---|
| Typeface (SF Pro not licensable off Apple platforms) | **Inter (Rec.)** / Geist / Manrope | **Inter** (OFL) on board and PC. Board: anti-aliased bitmap sizes generated with `lv_font_conv`; Inter Display cut for large sizes; tabular figures for the clock |
| Icons (SF Symbols not licensable off Apple platforms) | **Phosphor (Rec.)** / Lucide / Material Symbols Rounded | **Phosphor** (MIT): fill weight on tiles, regular in nav bars and settings rows. Corrected: built as fill for tiles, rows and status, bold for chevrons, check, plus and close (`DESIGN.md` Icons) |
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
| Board "Stop streaming" button (StreamOn artboard) | Ask the PC to stop / stop locally | **Stop locally** and send a LOG line; the PC sees the stream go quiet. No new message in proto 1. Corrected (P14): the board now also reports the stop with STREAM_STOP, so the PC shows Off |
| Stream screen when the PC is connected but not streaming (no artboard) | Reuse StreamOff / new state | **New state** in the StreamOff layout: "Not streaming". Corrected (P14, P15): "Show this screen on the Board page in fenixOS on your PC." with a "Start streaming" button (sim screen `stream_idle`) |
| Rotation setting UI (artboard shows a Rotation row with a chevron) | Cycle on tap / sub-page | **Sub-page** with the 4 orientations and a check mark |
| Navbar on narrow (portrait) screens when back label and title collide | Truncate / drop the back label | **Drop the back label**, keep the chevron (iOS does the same) |
| Settings › Storage and Host PC pages (no artboards) | — | Grouped info rows in the About style |
| Package size cap (PLAN said 256 KB) | 256 KB, read in pieces / **64 KB, whole package in RAM** | **64 KB.** The board has ~125 KB free heap, and the package is read whole to check it and while the app is open. Assets other than the two tile icons count toward the 24 KB image budget, so real packages are a few KB. Corrected (P39): the board reads packages in pieces now; the 64 KB cap stays (no format change) |
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
| Mirror fps chip | Board rects per second / **pictures shown** | **New pictures the PC showed per second** (max 10; 25 since P46): the stream has no frame marker, and a frame is many rects |
| Clicks on the mirror | View only / **touches** | **Touches** (`TOUCH` messages): a press is held ≥ 100 ms because the board keeps only the latest touch state |
| Firmware writes | Full image / **only changed regions** | **Only changed regions** (espflash `skip`, MD5 per region): a same-version Reinstall takes ~10 s instead of ~23 s; every written region is verified |
| Flash baud | 115200 / 921600 / **460800** | **460800**, the speed `board/idf.ps1` uses and proven on the CH340C; a full update takes ~23 s |
| Firmware in the PC app | Download on demand / **bundled resource** | **Bundled** (`resources/firmware`, ~0.85 MB then, 1.1 MB in v0.1.12): the board and the app always match without a network, as PLAN §3.8 lockstep needs |
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
| Simulator version string (after the 0.1.1 bump broke CI) | `VERSION` / **fixed 0.0.0** | **Fixed 0.0.0** in `board/sim/CMakeLists.txt`: About and Settings print the version, so every release changed three goldens |
| Simulator golden images | Reuse `docs/screens/board/` / **own set** | **Own set** in `board/sim/golden/`, compared pixel for pixel in CI; the board captures stay as the reference for the real panel (they differ by ≤ 0.6%: clock minute, antialiasing) |
| Simulator stream screen | Not streaming / **streaming** | **Streaming** (`stream on 10` in the script): `fenix shot` streams from the board, so the real capture shows it that way |
| Release build | Hand-written upload steps / **tauri-action v1** | **`tauri-apps/tauri-action@v1`**: builds the NSIS bundle, signs the updater files and uploads them with `latest.json` in one step; the firmware is built in the IDF container job and handed over as an artifact |
| Updater test | `v0.1.1-test` pre-release / **old local build against the real release** | **Local build with VERSION 0.0.9** (not committed): the endpoint `releases/latest/download/latest.json` skips pre-releases, so a pre-release could never be offered; the real v0.1.0 tests the whole path |
| Installer scope | Per machine (UAC) / **per user** | **Per user** (`nsis.installMode: currentUser`, also Tauri's default, now explicit): no admin prompt, and the updater can replace it silently |
| Install folder | Move the data elsewhere / **share `%LOCALAPPDATA%\fenixOS`** | **Share**: Tauri's per-user NSIS default; uninstall and update were tested to leave the data alone, and moving the data would need a migration for no gain |
| LCD write clock | 80 MHz (S1 colour bars) / **40 MHz** | **40 MHz**: 80 MHz garbled the real panel under LVGL's small DMA writes (user video, 2026-10-29). The ESP32 SPI clock is 80 MHz / (n + 1), so the only step below 80 is 40, which is also what the vendor demo's and FenixDeck's "55 MHz" became. Slides: 26.1 fps (was 34.5) |
| CH340 PC driver (after a crash) | Keep 3.3.2011.11 / **update to WCH's current CH341SER** | **Update** (installed 4.0.2026.2): the 2011 driver bugchecked Windows (0x48) when a pending COM9 read was cancelled at process exit (`docs/board-info.md`, Version status). The app also closes the port before it exits (`Link::shutdown`). Corrected: the 2026 driver crashed too (0xD1); the trigger was our read timeouts, see the next row |
| Serial read timeouts (P16, after crash 2) | serialport's "wait for the first byte" timeouts / overlapped I/O / **reads that never wait, polled** | **Never wait** (`MAXDWORD, 0, 0`): the CH340 driver's timeout path is where both crashes were; in this mode it never arms a timer or a cancel routine. Polling at 2 ms / 10 ms costs no throughput (bench within noise of no sleep) and 0.5 % of a core in the app |
| Board screen in 3D (P11, user request) | Keep the flat artboard / **3D board in the main window** | **3D**: the welcome's scene, the cable hovering while no board answers, the plug-in run on connect, a 600 ms fade to the dashboard; a board already connected at start opens the dashboard directly. three.js loads only when the screen shows (its own 582 KB chunk) |
| Main window rendering (P11) | GPU off, scene capped (30 fps, 1×) / **GPU on** | **GPU on**, user asked for it: software WebGL took ~8.5 cores even at 30 fps (`--in-process-gpu` puts it in the browser process). With the GPU: dashboard 78.8 MB / 0.02 % CPU (64.1 MB GPU-off), board screen 141.7 MB |
| GPU choice (P11, user request) | Default / **NVIDIA** | **High-performance GPU**: `--force_high_performance_gpu` in both windows' args and `powerPreference: "high-performance"`; WebGL reports the RTX 4060 Laptop GPU |
| Scene frame rate (P11) | Display rate / **60 fps, paused when minimized** | **60 fps**: at the panel's 165 Hz the GPU process took 1.2 cores (8.9 % of all CPUs); paused while minimized: 0.57 % |
| CPU cap (P11/P12, user request) | None / affinity to 4 CPUs / **job CPU rate cap** | **Hard rate cap of 4 cores' worth** (25 % on 16 logical CPUs) at normal priority, for the app and its children (`cap_cpu`) and for builds (`tools/capped.ps1`): threads still use any core, unlike an affinity mask that would crowd 4 CPUs |
| Release build profile (P12) | Fat LTO + 1 codegen unit always / thin LTO, fat only for tags / thin LTO always / **no LTO always** | **No cross-crate LTO (`lto = false`) for every build** (in `.cargo/config.toml` since v0.1.1, for the CI cache key): a one-file rebuild went from ~10 min (fat) to 45 s (4-core cap), a full one 3 min 47 s; the exe is 9.13 MB instead of 8.1 MB (the user accepts a few MB for faster builds). Corrected twice the same day: fat LTO for tags was dropped, because a different profile per build kind gives each its own CI Rust cache, so every release would rebuild all crates; then thin LTO (1 min 31 s one-file, 5 min 12 s full, 9.14 MB) gave way to no LTO, which was faster and no bigger |
| CI firmware cache (P12) | None / **ccache** | **ccache** in the ci `board` and release `firmware` jobs (`actions/cache`, 75 MB for 1768 objects) |
| Board page mirror (P13, user request) | Stream while the page is open / **off until started** | **Off until Start stream**: opening the Board page no longer makes the board stream; the screen area says the stream is off. Corrected (P14): the engine no longer stops the stream when polling ends |
| Stream on/off between board and PC (P14, user request) | PC only / **either side, kept in sync** | **Either side** (corrected by P15: started on the board only): the board's Stream app has Start and Stop, the Board page has Start and Stop, and the board reports every change with STREAM_START / STREAM_STOP sent back to the PC (no protocol version bump; older apps ignore them). The stream runs until stopped on either side or the link drops; every new connection starts with it off. Engine `State.streaming` drives the mirror |
| PC app motion (P17, user: "so abrupt in animation and transitions") | CSS only / **Svelte transitions with the DESIGN.md curve** | **Svelte transitions** (`lib/motion.ts`: the `--ease` curve in JS, 200 / 250 / 350 ms, `backOut` for the tile spring) plus CSS for hover and press. Pages enter only (no out transition), so two pages never stack. Corrected: reduced motion used to switch every transition and animation off; DESIGN.md asks for a plain fade, which it now is |
| Mirror off state (P17, user request) | Text over the paused picture / **blur and dim** | **`backdrop-filter: blur(14px)` with a 45 % black fill** behind "Streaming is off", fading in and out (250 ms) |
| Where the stream is started (P15, user: "the board section should ask user to enable streaming on board") | Both sides / **the board** | **The board's Stream app**: the Board page has no Start button, its off state says "On the board, open Stream and tap Start streaming.", and Stop stream shows while live. At each connection the PC sends STREAM_SYNC and the board answers with its state (replaces P14's STREAM_STOP at connect). The board stops its stream when the link has been silent for 3 s, so after an app restart the stream is normally off; the answer matters for quicker reconnects |
| Burn-in protection (P21, user: "screen burn in right where the clock is") | Pixel shift only / **pixel shift + screen rest** / periodic white refresh | **Pixel shift (1 px a minute, +-2 px) and a panel rest (sleep after 10 min without a touch)**, on by default. The user asked for the switch to be greyed out and locked on, unlockable only through a Debug switch at the end of Settings; Debug off locks it on again |
| Crash-safe flashing (P23, user: "please fix it all") | Own ROM-loader protocol / **patch serialport** / leave flashing as is | **Patch serialport** (`third_party/serialport`, `[patch.crates-io]`): Windows `set_timeout` never arms a driver read timeout and `read` polls; espflash and fenix-link both get it. The flasher became crate `fenix-flash`, and `fenix flash` replaces `idf.ps1 flash` (esptool) for the board |
| Release build cache (v0.1.3: tag caches are per tag) | Accept cold releases / **warm a shared cache on main** | **ci.yml `rust` job runs the release build on main under `shared-key: app`; release.yml restores it (`save-if: false`)** |
| Task manager data (P27, user: "make it real-time and keep polling to 1 second") | Board computes load and rates / **board sends raw counters, PC computes** | **Raw counters** (FreeRTOS run times, link bytes) in one SYSINFO per second, asked only while the page is open, so the board keeps no history and the link stays quiet otherwise. An optional message pair, no proto bump; older firmware answers ERROR unknown type and the page says so |
| RAM for Wi-Fi and Bluetooth (P29, user: "optimizing ram usage ... so that we can enable wifi and bluetooth") | Keep 80-line bands / **40-line bands** / 20-line bands | **40 lines** (2 x 25.6 KB): +51 KB heap for 1.6 fps (24.0 vs 25.6, remeasured); 20 lines drops to 20.7 fps. Also UART TX 16 to 8 KB and the UI on core 1. Wi-Fi and BLE together still leave only ~40 KB (probe build); see `docs/board-info.md` |
| Radios at once (P30, user: "only one radio at a time") | Both at once / one at a time, live switch / **one at a time, restart to switch** | **Restart to switch**, with a warning popup first. Releasing Bluetooth memory (`esp_bt_mem_release`) is one-way until a reboot |
| Bluetooth uses (P30) | SPP serial / BLE phone app / A2DP audio / HID | Superseded by P38 (Bluetooth removed). Was: **SPP serial and BLE for a phone**; no A2DP, no HID. The phone side is deferred: later a Web Bluetooth page (user: "skip this for now ... later we can do smtg like this") |
| Stream over Bluetooth (P33) | Allow it / cap its rate / **refuse it** | Superseded by P38 (Bluetooth removed; code 6 reserved). Was: **Refuse:** STREAM_START over SPP gets `ERROR` 6 (unavailable), an additive code, so proto stays 1. A stream gave ~1 KB/s and took the heap to 7.9 KB before a panic; Bluetooth mode has ~20 KB free. USB and Wi-Fi stream |
| Bluetooth at all (P38, user 2026-10-31: "nuke bluetooth incl BLE all togther") | Keep SPP and BLE / BLE-only link (NimBLE, passkey) / **remove Bluetooth** | **Removed**: Bluetooth mode left 19-24 KB free, Windows took 2-46 s to open its COM port and often failed, a stream ran at ~1 KB/s. Without Bluedroid: image 1.53 to 1.12 MB, Wi-Fi heap 63.9 to 71.8 KB at boot, IRAM 131 to 112 KB with FreeRTOS and heap code back in IRAM (fps 24.3 to 25.5). Mode 1, SYSINFO flag 32 and ERROR 6 stay reserved (no proto bump); a board that saved mode 1 boots as Wi-Fi with a saved network, else off |
| Wi-Fi uses (P30) | PC link / internet (time, weather) / phone web page / OTA updates | **All four.** OTA only after the SD card arrives (user: "this particular ota feature will only be done later when i can get an sdcard in") |
| Wi-Fi credentials (P30) | PC app / on-board keyboard / both | **Both**: PC app first, on-board keyboard later. Stored in plaintext, no encryption anywhere (user: "i dont want any enceyption to be used wjth overhead on perf") |
| Weather source (P30) | **Open-Meteo** / skip / other | **Open-Meteo**: no account or key |
| Frame rate (P30, user asked about 24 / 10 fps caps to free RAM) | Caps / **keep the UI smooth** | **Smooth.** Caps free no RAM: the draw bands have a fixed size |

## 2026-11-01: overnight run (choices made on the user's behalf; the user was asleep)

| Question | Options | Choice |
|---|---|---|
| Hallmark skill on edits to existing screens | Run a Hallmark build / **follow DESIGN.md** | **DESIGN.md**: Hallmark's own pre-flight treats a root `DESIGN.md` as a locked system and says changes to an existing design are done in the project's system, not as a Hallmark build |
| Non-USB COM ports in the port menu after P38 (P37 had listed them for the Bluetooth link) | Keep listing them / **USB only again** | **USB only**: with no Bluetooth link, the 6 other COM ports on this PC (paired devices) could never answer, so they were noise. The CLI's 60 s wait for them went too |
| IRAM placement after P38 | Keep FreeRTOS, heap and ringbuf code in flash and SRAM1 as IRAM / **IDF defaults** | **Defaults**: Bluedroid was why they moved (P30). Back in IRAM: 112 of 128 KB used, heap unchanged, fps 24.3 to 25.5 |
| `esp_timer` stack after P38 | 3072 / **2560** | **2560**: 3072 was for the BLE status JSON; 1840 B free at 2560 after Wi-Fi and stream benches |
| Link buffer sizes (P40) | Keep / trim all to their peak / **trim RX only** | **RX only**: UART RX 8 to 5 KB (bounded by the PC's 4 KB install window), Wi-Fi RX 8 to 2 KB (TCP flow control), `tcp_link` stack 3072 to 2560. TX buffers fill while streaming and a band needs ~4.6 KB, so smaller ones would drop bands. +10 KB heap, fps and install time not lower |
| Bluetooth glyph in the board icon font | **Drop** / keep | **Dropped** (P41). Corrected: it was kept on the belief that every icon is app API, but apps get only the 20 icons in `docs/packages.md`, and `bluetooth` was not one; nothing used it |
| PC Settings with a board on old firmware reporting mode 1 | Hide it / **show no tick, keep its name for messages** | **No tick**: the choices are Off and Wi-Fi; "Bluetooth" still names mode 1 in the status and confirm text |
| `tasks.md` size (P41, 88.7 KB) | Keep / summarise in place / **move done phases verbatim to `docs/legacy/tasks-history.md`** | **Move**: nothing is lost, and every session's first read drops to 6.7 KB. Open items stay in `tasks.md` |
| `docs/PLAN.md` after the build (P41) | Rewrite as current architecture / **keep as the v0.1.0 plan as built** | **As built**, with Corrected notes where later phases changed it and a pointer to the measured numbers; a rewrite would duplicate `board-info`, `packages` and `spec` |
| `docs/board-info.md` (51 KB dated log, P41) | Rewrite / **current-numbers table on top, log kept** | **Table on top**: readers get today's numbers in 10 lines; the log keeps why each changed |
| FenixDeck history docs (P41) | Trim / **freeze with a header line** | **Freeze**: `POC-WRITEUP` and `docs/legacy/*` are records of a finished project; agents skip them by the header |
| Red CI board job since P37 (found P40) | **Regenerate `lv_kconfig.h`** / drop the check | **Regenerate** (90e0b03); 25 sim screens still match. AGENTS.md and toolchain.md now say to run `sim_config.py` after LVGL changes |
| Anti-cheat review (P42, user: "so that ... valorant, their anticheat doesnt false flag") | Leave as is / **cut what looks like polling the user did not ask for** / add a "game mode" switch | **Cut the noise**: no `fenixos.local` mDNS lookups unless the board was last seen in Wi-Fi mode (`board_wifi` in config); a CH340 that is not fenixOS is tried every 10 s after 2 failures, not every second. Nothing in the app hooks, injects, lists processes or reads other memory, so no game mode; README "What fenixOS does on your PC" says what it does. CH340 driver vs Vanguard: no reports found, unverified |
| Tray text over Wi-Fi (P42) | `tcp:fenixos.local` / **"Wi-Fi"** | **"Wi-Fi"**: the port name is the link's internal spelling |
| PC literal colours (P42 C; DESIGN.md: "All colours go through these tokens") | Leave / **`#fff`, `#000` to `--label`, `--bg`** | **Tokens**. Corrected (P45): shadows, the tile switch tracks, the hover and press washes and the mirror dim are now named variables in `app.css` too, so no literal colour is left in a component |
| PC Settings organisation (P42 C) | Regroup into Board / Network / App pages / **keep one page, order General, Board, Wireless, Wi-Fi network, Weather, Updates, About** | **Keep**: seven short groups fit one scroll, each with its own note; a split would add a sub-page level DESIGN.md has no artboard for |
| PC QoL (P42 C) | Each as found | Enter joins a Wi-Fi network; a failed app add offers Add again (was stuck on "Failed"); a slider shows its value while dragged; a new page or sub-page opens at its top; Settings confirm buttons use the 10 px PC button radius; Task manager meters scale (transform) instead of animating width, ends now square inside the rounded track |
| Board literal colours (P42 D) | Leave `lv_color_white()` / `lv_color_black()` / **`col(FX_COLOR_LABEL)`, `col(FX_COLOR_BG)`** | **Tokens**, same pixels; DESIGN.md routes every colour through a token on the board too |
| Board Settings organisation (P42 D) | Move Wireless next to Host PC, split groups / **keep Display, Home screen, Storage, Host PC, Wireless; About; Debug** | **Keep**: matches the Settings artboard; Wireless was added at the end of the first group and Host PC sits next to it already |
| Board text fixes (P42 D) | Each as found | About "Memory free" now "of" the heap total (was "of 520 KB", the chip's whole SRAM, unlike the PC's Task manager); CPU MHz from sdkconfig; Host PC "Time: Set" (SNTP can set it too); Home "Getting the time over Wi-Fi" in Wi-Fi mode; Stream "by USB or Wi-Fi" |
| Wi-Fi join race (P42 D, found by reading `radio.cpp`) | Leave / **ignore our own disconnect, stop the retry timer on an address** | **Fix**: `radio_join` and `radio_reconnect` disconnect then connect; the disconnect event counted as a failure and armed a 1 s retry that could restart the new attempt, or fire after GOT_IP and show "Connecting" on a connected board. Now an `ASSOC_LEAVE` we caused is skipped and GOT_IP stops the timer |
| Time zone fixed to MYT in firmware (P42 D) | **Keep** / send the PC's offset in TIME_SET | **Keep**: the user chose Kuala Lumpur (`ideas.md`) and PLAN lists other time zones as out of scope; a PC-sent offset would need a protocol change |
| Stale board screenshots in `docs/screens/board/` (P42 E: real-board captures from v0.1.0, no Wireless, "of 520 KB") | Recapture on the board (`capture_screens.sh` erases settings, Wi-Fi network included, which nobody could re-enter overnight) / **copy the simulator golden set** | **Simulator set**: the same LVGL code, current screens, deterministic; it shows v0.0.0. README's three shots come from it. Recapture on the board when the user is there |
| App template form (P44) | Zip only / **source in `templates/app/` + zip built by `tools/app_template.py`, checked in CI** / separate template repo | **Source + zip**: the user asked for a zip in the root; keeping the source beside it lets docs changes reach it, and CI catches a stale zip. A separate repo would need the user to create it |
| Template docs (P44) | Copy fenixOS docs whole / **cut-down copies for app authors** (`app-format`, `design`, `pc-half`, `registry`) | **Cut-down**: drops the binary format, store and protocol internals an author never touches; each says which fenixOS file it comes from and that fenixOS wins |
| Template tooling (P44) | Ship fenixOS `registry.py` / **own `tools/app.py`** with the same manifest rules (`check`, `local`, `release TAG`) | **Own**: one command each for checking, local testing (`FENIX_REGISTRY`) and the filled registry manifest; rules copied verbatim, verified against fenixOS `registry.check` |
| Template SDK and compiler source (P44) | Publish crates to crates.io / **git dependency and `cargo install --git` pinned to tag v0.1.13** | **Git tag**: no publishing account or secrets needed; tested from a scratch copy |
| Starter app (P44) | Copy the sample (needs `windows-sys`) / **a counter with a switch, a button and two settings** | **Counter**: shows bind, events and settings with only `fenix-sdk`; Phosphor `timer` icon on both the board tile and the PC tile |
| PC sidebar re-click (found in P44 testing) | Leave / **clicking the current item rebuilds its page** | **Rebuild**: Apps stayed on Add app or an app's settings when "Apps" was clicked again |
| Section titles in PC Settings and Licenses (P45; `app.css` had uppercase with 0.02em tracking) | Keep iOS-style caps / **sentence case, untracked** | **Sentence case**: DESIGN.md Type says "no letter-spaced capitals", and Hallmark's floor says the same |
| Accent text on `surface-2` (P45; 3.8:1, DESIGN.md lists accent only on `bg` 5.8 and `surface-1` 4.7) | Keep / **`label` text** on Logs Copy/Open log folder, Add app Get, sheet Cancel/Later/Close | **Label**: passes 4.5:1, same as the Board page's Stop stream button; accent stays on `bg` and `surface-1` |
| PC button radius in sheets and the welcome (P45) | Keep 12 px / capsules / **`--radius-pc-button` (10 px)** | **10 px** per DESIGN.md Shape; the board keeps capsules |
| Connect "Board found" (P45) | Keep "Settings > About > Show welcome again" / **a "Set up this board" button** | **Button**: Settings is not reachable while Connect shows (no board connected), so the old hint led nowhere |
| Connect and welcome 3D scene while hidden (P45) | Pause on minimize only / **also pause while the page is hidden** | **Also hidden**: no GPU frames while the window is covered or in the tray; matters next to games |
| Hallmark on an existing locked design (P45) | Full Hallmark redesign / **check stamp in `app.css`, sloplint `--scope app`, Floor fixes; waivers where DESIGN.md decides (pure black and white, artboard spacing, the mirror's blur)** | **Check stamp**: the user asked to follow the design language strictly, so DESIGN.md wins; sloplint on `app.css`: 0 FAIL |
| Meta strings joined with ` · ` (P45: Task manager subtitle, welcome probe line) | Keep / **commas and words** | **Words**: Hallmark floor 10, and easier to read |
| Closed app freed while its screen slides out (P45, root cause of the simulator hang) | Copy label text (`lv_label_set_text`, more RAM per label) / delay close by a timer / **keep the package until the app screen is deleted** | **Until deleted**: no extra RAM while open, no timing guess; the screen's delete event frees it |
| App buttons 36 px tall (P45; DESIGN.md: board touch targets at least 40 px) | Keep / **40 px, icon buttons 40 × 40** | **40 px**: the system screens and the template's design rules already say 40; three goldens updated |
| Board segmented control, 26 px segments in a 30 px bar (P45) | Grow to 40 px / **keep** | **Keep**: matches the Display artboard; the rows around it are 40 px. Growing it changes every Settings layout. Confirmed by the user 2026-10-31 ("keep the control height") |
| `tasks.md` after P41 to P44 (P45) | Keep the long done blocks / **move them verbatim to the history file** | **Move**, as P41 did for P0 to P40; one line each stays in Done |
| Docs pass 2 depth (P45) | Re-read changed files only / **every current `.md` except history and vendor drops, facts checked against code** | **Every file**: stale facts fixed with Corrected notes (ideas push time and codec, toolchain GPU and PoC gotchas, PLAN pipes and screens, packages font set, registry limits, mockups status, vendor SPI clock); long Corrected chains cut where history adds nothing |

## 2026-10-31: P46, Wi-Fi link on a powerbank

| Question | Options | Choice |
|---|---|---|
| A silent TCP peer held the board's one link slot (found when the app quit on a powerbank) | Accept new connections and drop the old (`select`) / TCP keepalive tuned / **3 s receive timeout on the client socket** | **Receive timeout**: the PC pings every 1 s and the session already ends after 3 s silent, so the socket now ends with it; one `setsockopt`, no new task logic. Tested: a silent client is closed and `fenix hello` gets in after 3.1 s |
| Stream rate (user: "r u sure u cant get higher fps", 10 fixed) | Keep 10 / 25 on both links / **10 over USB, 25 over Wi-Fi** | **Per link** (`fenix bench 15 <fps>`, Settings opened and closed each second): USB is full at ~45 KB/s, so 25 only doubles dropped bands and costs board fps (25.2 to 19.9); Wi-Fi at 25 sends 64 KB/s, 4.4 screens/s against 3.0 (+47%), board fps 26.6 to 21.0 while streaming, lowest heap the same (27.3 KB). The PC mirror polls every 40 ms (was 100) |
| Connect screen for a board found over Wi-Fi (P47, user asked for a 3D animation) | Same plug-in run / no 3D run / **own run: cable pulls away, the antenna edge turns in, `accent` arcs pulse out from the ESP32 antenna, the screen boots** | **Own run**: a cable seating for a wireless link would be false. Arcs (not full rings: those crossed the screen and hid behind the board in a test render) face the camera and open away from the board, in `accent` like the USB port glow; the board sits nearer the centre to leave them room; reduced motion keeps only the antenna light. 300 ms longer than the USB run, for the cable to leave. The welcome ("Show welcome again") plays the same run for a board on Wi-Fi |
| Wi-Fi link closed on one 500 ms send stall (P48, user: "app lose connection temp after stopping stream") | Lower Wi-Fi to 15 fps / bigger send ring (RAM) / **drop the frame and retry `send()` up to 3 s** | **Drop and retry**: no RAM, the 25 fps stays; stream bands are redrawn when dropped; a dead PC is still found in 3 s (P46 receive timeout, heartbeat). A dropped small frame (a log line, stats) costs nothing; a dropped `STREAM_STOP` report would leave the PC's mirror on "Live" until the next start, stop or reconnect (needs a 500 ms stall at that moment; the ring empties once the stream stops) |

## 2026-11-01: Morpheus requests (`docs/requests/morpheus.md`)

| Question | Options | Choice |
|---|---|---|
| RAM case an app must fit | Worst case (Wi-Fi stream on while an app runs, ~24 KB low) / **Wi-Fi on and linked, no stream** | **No stream** (user: "im never gonna do always wifi screen stream and also run apps"). Apps are used with Wi-Fi on and the PC linked, or Wi-Fi off over USB; the Wi-Fi case (~82 KB free at boot) sets the budget, USB-only (~155 KB) has room to spare |
| Image binding size | 80×80 under today's 40 KB estimate cap / **96×96 to 120×120, cap raised to ~56 KB after a board measurement** | **Measure first** (P50): a Morpheus-sized layout with 120×120 art in both modes; below ~20 KB lowest free heap with Wi-Fi on means 96×96 |
| CJK and wide text | Fonts in flash now / **after the SD card** | **SD card** (user: "as soon as sdcard arrives then we can discuss further"): ~400 KB per CJK size and weight does not fit the 4 MB flash. Latin-1 and a transliteration helper come first (P51) |

## 2026-11-02: SD card (`docs/board-info.md` "SD card check")

| Question | Options | Choice |
|---|---|---|
| File system | exFAT (patch IDF FatFs) / **FAT32** | **FAT32** (user: "nvm about exfat"): exFAT is compiled out of IDF v5.5.5 and would not be faster over 1-bit SPI |
| Card layout (user: "reformat ... with appropriate cluster sizes") | Keep (partition at sector 32) / IDF `f_mkfs` (partition at 63) / **SD Association layout** | **SD Association layout**: partition and data area on 4 MB boundaries, 32 KB clusters, 2 FATs; formatted once by the spike, which never formats a card already in that layout |
| SD clock | 20 MHz (IDF default) / **40 MHz** | **40 MHz**: the highest ESP32 step (80 / 2) under the card's 50 MHz limit; worked on this card in both runs |
| Long file names | Off (8.3) / **on, buffers on the heap** | **On** (user: "a no brainer"): app ids name files |
| SPI clash (LCD, touch and SD on three pin sets, two SPI controllers) | Switch SPI3's pins between touch and SD / **touch on software SPI** | **Software SPI for touch**: 346 µs per read, only while pressed (~2 % of core 1), the SD card keeps hardware SPI at 40 MHz; tested together without effect on either |

## 2026-11-02: SD, apps, OTA (user picks; plan in `docs/plan-sd-ota.md`, `tasks.md` P51 to P54)

Corrected: several rows below were changed by the user the same evening (flash layout, package caps, tiles, sounds, unknown apps); the changes are in the next section, and the rows say so.

The user's request: an app added in the PC app installs both halves, the board half on the SD card; switching an app off on the PC stops it on both sides but keeps it on the board; OTA updates from an SD package, USB or Wi-Fi with two slots; RAM kept low throughout; ideas for the 8 GB card.

| Question | Options | Choice |
|---|---|---|
| Flash layout | Keep one 2 MB slot + 1.94 MB app store / **two 1.5 MB OTA slots + 896 KB app store** / two 1.9 MB slots, no flash app store | Changed (evening): **two 1.81 MB slots + 256 KB LittleFS**, next section. Was: **Two 1.5 MB slots + 896 KB LittleFS** (nvs 0x9000 24 KB, otadata 0xF000 8 KB, phy_init 0x11000 4 KB, ota_0 0x20000, ota_1 0x1A0000, apps 0x320000): the image is 1,155,568 B (v0.1.20), so a 1.5 MB slot (1,572,864 B) leaves ~407 KB to grow; the flash store stays as the fallback with no card. Moving to it needs one serial flash (new partition table and bootloader) |
| OTA trust | Hash only / **signed firmware** | **Signed (ECDSA P-256)**: the release signs the image; the board checks the signature before it switches slots. The private key is kept like the updater key, never in git |
| App switched off on the PC | Remove it from the board (today) / **keep it, stop both halves** | **Keep it.** Changed (evening): the tile is faded; opening it shows the message and button. Was: the tile stays; opening it shows "Turned off on your PC" with a **Turn on** button that asks the PC, which switches the app on without a click on the PC |
| Where the board half goes | Flash / **SD card, flash when there is no card** | **SD card first**, global default apps too |
| No card | Refuse installs / **fall back to flash** | **Flash fallback** (128 KB per package, evening). Tiles of apps on a missing card are faded with an SD badge (evening): "Insert the SD card" |
| OTA path | Write the spare slot while receiving / **stage on the card, then install** | **Stage on SD** (PC to card, resumable), check the signature, then write the spare slot. With no card, write the slot directly (USB or Wi-Fi) |
| Package size cap | 1 MB / **8 MB on SD, 256 KB on flash** | Changed (evening): **no fixed cap on SD, 128 KB on flash**. Was: **8 MB on SD** (user: "can be way higher than 1 mb surely"): install time (~42 KB/s over USB, ~3 min for 8 MB) and RAM per screen are the real limits, not the card. 256 KB on the flash fallback |
| What else the card brings | (all picked) | Logs and crash records on SD; keep the previous firmware (roll back from Settings); bigger packages; ~30 apps (paged APP_LIST); last values while the PC is off; fonts from SD (LVGL Tiny TTF, `LV_TINY_TTF_FILE_SUPPORT`); screenshots to SD; UI sounds (speaker on IO26). Changed (evening): sounds dropped; settings backup, status page from the card, Home wallpaper, per-app data folder and a rescue firmware copy added |
| Order | Plan only / **build the SD base now, plan the rest** | **SD base now** (P50, v0.1.20), the rest planned, then standby |
| SD mount RAM | `max_files` 2 / **4** | **4** (P50): each costs 552 B; fonts from SD will hold a file open while an install, a log or a screenshot opens others. Corrected (P53): **6**, for a PC file read and write next to a firmware part, an app package, the log and the wallpaper (+1.1 KB). Corrected (step 7): the wallpaper was dropped; the sixth is a font from the card (P54) |

## 2026-11-02 (evening): plan changes before the overnight run

The user reviewed the plan, asked for these changes and set the whole plan (P51 to P54, then P50-M and P51-M) as one overnight run ("imma jus do /goal the entire plan and go to sleep").

| Question | Options | Choice |
|---|---|---|
| Flash layout | Two 1.5 MB slots + 896 KB app store / **two slots + 256 KB app store** | **ota_0 0x20000 and ota_1 0x1F0000 at 0x1D0000 (1.81 MB) each, apps 0x3C0000 256 KB**, coredump in the 56 KB gap at 0x12000 (user: "keep the on flash app space to be just 256kb and give the rest for OTA"). The 256 KB stays even if the card stays in for good (user: "keep the 256kb on flash space for incase") |
| Package cap on flash | 256 KB / **128 KB** | **128 KB** (user: "ill tell later on why") |
| Package cap on the card | 8 MB / 32 MB / 64 MB / **none fixed** | **None fixed**: free space and FAT32's 4 GB per file; the PC shows progress and warns when an install takes over ~2 min. A package today is 2 to 100 KB (layout, ≤ 8 KB text, ≤ 24 KB per image) |
| Tile of an app off on the PC | Message on the tile / **faded tile, message on open** | **Faded tile**; opening it shows "Turned off on host PC" and Turn on (user) |
| Tile of an app on a missing card | Same fade / **fade + SD badge** / hidden | **Fade + SD badge** |
| Apps on one side only | Remove unknown board apps (today) / keep, faded / **a sync system** | **Sync** (user's own idea): board-only apps get their PC half, PC-only apps get their board half, version skew updates the older side. Shown as a "Sync with board" card with a checkbox each; **one click** applies them, nothing changes silently except Add and Remove |
| Board app whose PC half is not in the registry | **Registry, else keep board-only or remove** / store PC halves on the card | **Registry, else keep or remove** (no programs taken off the board) |
| Screenshot trigger | BOOT button / long press on the clock / **from the PC only** | **PC only**: Board page button and `fenix shot --sd` |
| Screenshot file (P53) | 24-bit BMP / PNG / **16-bit BMP** | **16-bit BMP** (RGB565 bitfields, top-down): the panel's own format, no conversion or compression on the board. 8-row bands, not the planned 16: 16 took the heap low point to 43 KB |
| Settings backup: when to offer (P53) | Settings blob missing / **blob and Wi-Fi network both missing** | **Both missing**: a board that joined Wi-Fi but never changed a setting has no blob and is not an erased one. An empty board with no backup saves its blob at the mount, so the next boot does not offer its own defaults back |
| Settings backup: calibration (P53) | Always restore / **keep this board's when it has one** | **Keep**: touch calibration belongs to the panel; a card moved to another board must not break its touch |
| Settings backup: dismissing the sheet (P53) | A tap outside closes it / **asked until answered** | **Until answered**: the sheet comes back on Home after any rebuild; "Start fresh" saves the settings, so it is a real answer |
| Status page from the card (P53) | Replace the built-in page only / **serve the whole `web/` folder** | **Whole folder**, GET only, `/status.json` kept built in (a card page reads it). No percent-decoding: `%` and `..` are refused. The PC may write `web/` |
| Where the PC app keeps shot copies (P53) | Pictures / Downloads / **`%LOCALAPPDATA%\fenixOS\shots`** | **Local app data**: never a synced or redirected folder (the user keeps cloud sync off). "Show" opens it |
| OTA signing key | **Agent makes it and sets the Actions secret with gh** / agent makes it, user sets the secret | **Agent does both**; `%USERPROFILE%\.fenixos\fw-signing.pem`, never in git. Backup: the agent copies it with the updater key to `%USERPROFILE%\Documents\fenixOS-keys\` and tells the user (corrected: said the user backs it up) |
| Wi-Fi password in the settings backup | **Yes, plaintext** / no | **Yes, plaintext** (matches the earlier no-encryption choice; anyone with the card can read it) |
| Fonts from the card | **Chinese, Japanese, Korean** / plus more scripts / Japanese only | **CJK**: one Noto Sans CJK font (OFL) installed by the PC app; split per script if one font is too slow |
| UI sounds | Plan them / **drop** | **Dropped** (user: "i hav no idea of adding speaker for now") |
| More SD ideas | (all) | **All in P53**: settings backup, status page from the card, Home wallpaper (measured first), per-app data folder, rescue firmware copy (the kept `.fwp`). Corrected: the wallpaper was dropped after the measurement (next row) |
| Home wallpaper from the card (P53) | Keep it / **drop it** | **Drop**: drawn from the card by LVGL's file decoder (RGB565, a row at a time), a full Home redraw took 321 ms against 30 ms. The card gives about 1 MB/s through FAT and LVGL's reads; a bigger read cache (8 KB) was slower (361 ms). Home is redrawn in full every minute by the burn-in shift, and the board has no RAM to hold 150 KB. `wallpaper/` is no longer a folder the PC may write |
| Home with 32 apps | Wait for a mock-up review / **agent designs it** | **Agent designs it** under DESIGN.md (swipe pages, page dots); sim screenshots in `docs/screens/board/` for the user |
| Morpheus in the run | **Yes** / stop after P54 | **Yes**: P50-M (v0.1.25) and P51-M (v0.1.26) after P54 |
| Morpheus: `view` or `show` | **Both** / view only | **Both**: `view` for screens, a bindable bool `show` on every element |
| Morpheus: `secret` setting | **Yes** / no | **Yes**: masked, DPAPI user scope, never logged |
| Morpheus: releases | **Two** / one | **Two** |

## 2026-11-02: overnight run (choices made on the user's behalf; the user was asleep)

Plan: `docs/plan-sd-ota.md`. Each row: the question, the options, what was chosen and why.

| Question | Options | Choice |
|---|---|---|
| App index entry (P51) | Keep the sha in RAM (152 B a app) / **read the `.sha` file when asked** (120 B) | **120 B**: id 49, version 32, name 32, colour, where, state. 32 apps = 3.8 KB, over the plan's 2 KB row, but Home no longer keeps icon copies of every app (v0.1.20: ~1 KB each for 10 apps); only the page shown, and the previous one for one rebuild. Plan row corrected |
| Card apps while the card is out | Hide them / **keep their tiles** | **Keep**: `/apps/sd.idx` on flash lists them; faded tile, card glyph, "Insert the SD card" on open |
| Same app on flash and card | Card wins / **flash wins** | **Flash wins** at a scan (the card copy is deleted): flash is there at every boot |
| Removing an app whose card is out | Forget it / **refuse** | **Refuse** (`APP_RESULT` storage): the files are on the card; Sync offers it again later |
| Disabled-look opacity | New token / **`LV_OPA_40`** | **LV_OPA_40** in code: no `disabled` opacity token exists; one value, used twice |
| SD glyph | Draw one / **Phosphor `sim-card`** | **sim-card**: Phosphor has no sd-card glyph; the card shape reads the same at 14 and 40 px |
| Landscape Home page size | Narrower tiles to fit 4 / **3 tiles of 92 px** | **3 a page**: same tile size as before; the system apps fill page 1, installed apps follow |
| A new app installed | Stay on the page / **turn to its page** | **Turn to its page**, so the fade-in is seen |
| `HOST_STATE` for a crashed PC half | Off / **starting while it restarts, off when it gave up** | As chosen: off offers Turn on, which clears the error and starts it again |
| Board installs at connect | Automatic (as before) / **only on Sync** | **Only on Sync** (user: sync sheet, one click). Add, update, switch on and Remove on this PC still act at once |
| Tile install animation (P51, found on the board) | Fade + scale (DESIGN.md) / **fade only** | **Fade only**: the scale drew the tile through an LVGL transform layer and took the heap low point from 47 KB to 16 KB on an install with Home on top (`docs/board-info.md`) |
| Firmware signature check (P52 step 1) | IDF's `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT` / **own ECDSA P-256 check** | **Own check** (mbedTLS, already in the image for Wi-Fi). IDF's option on a rev 3.1 ESP32 uses the Secure Boot v2 scheme: RSA-3072 signature blocks appended by `espsecure`, checked only at `esp_ota_end`, i.e. after the slot is written. The own check verifies a staged `.fwp` on the card before the slot is touched, checks a kept package again for "Go back", and the PC can check the same format; no bootloader or eFuse change either way |
| What the signature covers | Image only / image + header | **sha256 of the image**; the board also checks that the header's version is the one inside the image (`esp_app_desc_t`), so a header cannot relabel an old signed image as new |
| Signature host test | C++ with mbedTLS on the PC / **Python self-test + board tests** | **`tools/sign_fw.py --self-test`** (good, bad signature, bad hash, wrong key, relabelled version) for the format; the board's own check is tested on the board with the same bad packages (`fenix ota`), since mbedTLS is not built for the PC here |
| Same version over the link (P52) | Refuse as `OLDER` / **allow** | **Allow**: Board › Reinstall firmware goes over the link too; only an older version is refused (About › Go back is the way down) |
| Bundle older than the board (P52) | Send it, the board refuses / **USB flash** | **USB flash**, as before; over Wi-Fi the app says to connect USB |
| Firmware packages kept on the card (P52) | Two (new + running) / **three** | **Three**: new, running and the newest older one. Found on the board: with two, a rolled-back try left nothing to go back to |
| A second `FW_BEGIN` while a transfer is open (P52) | `BUSY` until the 10 s timeout / **replace it** | **Replace**: one PC session at a time, so it is the same PC reconnecting; it goes on from the staged part |
| An update during the 30 s trial (P52) | Refuse / **keep the running image, then update** | **Keep and update**: it ran well enough to take an update; `esp_ota_begin` refuses otherwise |
| File message types (P53) | Plan's 0x60 to 0x67 / **0x60 to 0x6A** | **0x60 to 0x6A**: acks, results and `SHOT_SAVE` need their own types (`FILE_ACK`, `FILE_RESULT`); plan corrected |
| Checking a written file (P53) | Hash while receiving / **read it back after `FILE_END`, in a short task** | **Read back**: a resumed write would need the hash state of the part already on the card; the read-back covers it and costs a 4 KB stack plus a 4 KB buffer for a few seconds |
| Partial file writes on the card | Write in place / **`<path>.part` + `<path>.pin`** | **Part + pin**, as firmware staging: the old file stays whole until the new one is checked |
| What the PC may read | Only its own folders / **anything under `/sd/fenixos`** | **Anything**: logs, crash records, shots, packages and the settings backup are what the PC reads; writes and deletes keep to their folders |
| `mkdir`'s 32 KB FatFs buffer (P53, found on the board) | Make every folder once at mount / **cap FatFs's buffers at 4 KB** | **Cap** (`__wrap_ff_memalloc`): app data folders are made at any time, so a one-off pass would not cover them |
| Package cap | 8 MB / **none** | **None** (user: no fixed SD cap). The content limits (24 KB images, 8 KB strings, 40 KB RAM) still bound real packages near 33 KB, so a 2 MB timing test needs a format that streams images (not in P51); registry download cap 64 MB |
