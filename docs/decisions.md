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
| Mirror fps chip | Board rects per second / **pictures shown** | **New pictures the PC showed per second** (max 10): the stream has no frame marker, and a frame is many rects |
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
| PC literal colours (P42 C; DESIGN.md: "All colours go through these tokens") | Leave / **`#fff`, `#000` to `--label`, `--bg`** | **Tokens**. Kept: shadows, the switch's off track `#48484A` and hover wash (artboard values with no token) |
| PC Settings organisation (P42 C) | Regroup into Board / Network / App pages / **keep one page, order General, Board, Wireless, Wi-Fi network, Weather, Updates, About** | **Keep**: seven short groups fit one scroll, each with its own note; a split would add a sub-page level DESIGN.md has no artboard for |
| PC QoL (P42 C) | Each as found | Enter joins a Wi-Fi network; a failed app add offers Add again (was stuck on "Failed"); a slider shows its value while dragged; a new page or sub-page opens at its top; Settings confirm buttons use the 10 px PC button radius; Task manager meters scale (transform) instead of animating width, ends now square inside the rounded track |
| Board literal colours (P42 D) | Leave `lv_color_white()` / `lv_color_black()` / **`col(FX_COLOR_LABEL)`, `col(FX_COLOR_BG)`** | **Tokens**, same pixels; DESIGN.md routes every colour through a token on the board too |
| Board Settings organisation (P42 D) | Move Wireless next to Host PC, split groups / **keep Display, Home screen, Storage, Host PC, Wireless; About; Debug** | **Keep**: matches the Settings artboard; Wireless was added at the end of the first group and Host PC sits next to it already |
| Board text fixes (P42 D) | Each as found | About "Memory free" now "of" the heap total (was "of 520 KB", the chip's whole SRAM, unlike the PC's Task manager); CPU MHz from sdkconfig; Host PC "Time: Set" (SNTP can set it too); Home "Getting the time over Wi-Fi" in Wi-Fi mode; Stream "by USB or Wi-Fi" |
| Wi-Fi join race (P42 D, found by reading `radio.cpp`) | Leave / **ignore our own disconnect, stop the retry timer on an address** | **Fix**: `radio_join` and `radio_reconnect` disconnect then connect; the disconnect event counted as a failure and armed a 1 s retry that could restart the new attempt, or fire after GOT_IP and show "Connecting" on a connected board. Now an `ASSOC_LEAVE` we caused is skipped and GOT_IP stops the timer |
| Time zone fixed to MYT in firmware (P42 D) | **Keep** / send the PC's offset in TIME_SET | **Keep**: one user, one zone; a PC-sent offset needs a protocol change. Proposed for later in `ideas.md` terms, not built |
