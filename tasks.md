# fenixOS tasks

New project: an "OS of sorts" for the ESP32-2432S028R (CYD). Hand-over from the FenixDeck PoC is in `docs/POC-WRITEUP.md`. No code yet: the user decides what to build.

Legend: `[x]` done, `[~]` in progress, `[!]` blocked, `[ ]` to do.

## Phase 0: repo setup (2026-10-29)

- [x] Read `AGENTS.md`, all docs, all `pics/`, vendor schematics, dimensions, vendor LovyanGFX demo
- [x] Add `.gitignore` for the local-only vendor files listed in `docs/vendor/README.md`
- [x] Add this `tasks.md`
- [x] `git init` on `main`, first commit
- [x] Create private GitHub repo `fenixOS`, push, confirm local = `origin/main` (https://github.com/tariqaq/fenixOS)

## Phase 1: verify new findings (2026-10-29)

Read-only on the board (no flashing). Results are in the docs named below.

- [x] Schematic file names swapped vs content: confirmed by viewing both sheets. `docs/vendor/README.md`, `POC-WRITEUP.md` §3
- [x] Vendor LovyanGFX demo touch range 0–239/0–319 is wrong: raw XPT2046 values are 12-bit, the demo never calibrates, and the board runs 300–3900/200–3900 with no `tcal` in NVS. `bus_shared = true` is not needed. `docs/vendor/README.md`, `POC-WRITEUP.md` §2/§3
- [x] CN1: R18 marked `103` (10 kΩ) by CN1, matching the schematic pull-up on IO27. New: R15/R19 `501`, R16/R17 `101`, R20 `201` differ from V0.1. `pics/ANALYSIS.md`
- [x] esptool chip re-read on COM9: matches `docs/board-info.md`
- [x] Current firmware identified (`fenixdeck_board` `74a62bb-dirty`, IDF 5.5.5, 337,008 B image) and backed up + verified: `firmware/fenixdeck/`. `docs/board-info.md`
- [x] Live protocol + touch check (5 taps, page cycling, 0 errors). `docs/board-info.md`
- [x] Docs updated; commit + push

## Phase 2: fenixOS v0.1 discovery and design (2026-10-29)

Goal: capture the user's ideas, settle every open choice (asked with the picker, pros/cons + recommendation), make wireframe/mock-up slides, then write a detailed plan. No firmware or app code in this phase.

- [x] Write this phase into `tasks.md`
- [x] Wipe the board's whole flash (`esptool erase-flash`); read-back all 0xFF (460800 read failed once, 230400 worked). `docs/board-info.md`, `docs/tooling.md`
- [x] Install hallmark 2.0.0 at project level (`.claude/skills/hallmark`), `puppeteer-core` global, edit-time hook in `.claude/settings.json`; LF fix + `.gitattributes`. `docs/tooling.md`
- [x] Write `ideas.md`: vision, constraints C1–C10 (time, streaming, app runtime, shutdown, vsync, fonts, private registry, flash, RAM, PC plugin trust), suggested gaps
- [x] Commit + push
- [x] Ask decision questions round by round (picker); record each answer in `ideas.md` + `docs/decisions.md`
  - [x] Round 1 board core (LVGL 9 + LovyanGFX, C++17/IDF 5.5.5, declarative app packages, orientation switchable)
  - [x] Round 2 time/stream/power/nav (PC time sync + RTC keep, compressed dirty-rect stream, deep sleep tap-wake, top nav bar)
  - [x] Round 3 PC side (Tauri 2 + Svelte 5, sidecar app processes, repo public for registry, PC app flashes firmware via serial)
  - [x] Round 4 design (Inter, Phosphor, iOS-like tuned motion; old commits left)
  - [x] Round 5 structure (custom dark title bar, board/app/crates layout, lockstep SemVer, Settings + Display + Storage/host)
  - [x] Round 6 PC behaviour + board extras (autostart setting off by default, Tauri updater, destroy webview on close, RGB LED status)
- [x] Go public (2026-10-29, verified PUBLIC, scrubbed paths 404): scrub history first (packing slip + order number, vendor package, CH340 driver, `firmware/` images; local copies kept + backed up in scratchpad bundle), force-push, set public, verify
- [x] Lock the design system in `DESIGN.md` (colour tokens, type sizes, radii, motion, icon map)
- [x] Wireframes / mock-up slides: 20 artboards (13 board, 7 PC) on a private canvas; list + open questions in `docs/mockups.md`
- [x] Iterate with user: review round 1 done (both home styles, sweep-line boot, per-action elevation, tray menu, 3D OOTB, single instance, permissions, logs, simulator); canvas approved as the spec. `docs/decisions.md` round 7, `docs/mockups.md`
- [x] Detailed, scoped build plan: `docs/PLAN.md`. FenixDeck docs moved to `docs/legacy/` (PLAN, protocol, metrics-validation, screens). Version pinning is web-checked in P0, not now
- [x] Standing by for the go-ahead to start P0

## fenixOS v0.1 build (plan: `docs/PLAN.md`; started 2026-10-29 06:15 MYT, unattended `/goal` run)

Each phase: acceptance check, docs, stale sweep, commit + pull --rebase + push.

Progress rule: update this file after every step (done / in progress / blocked + notes, current sub-step, last commit), so a cut-off session resumes exactly here.

Unattended-run rules (user, 2026-10-29; details in `docs/decisions.md`): one UAC prompt at the start for an elevated helper; keep COM9 enumerated (no physical replug needed); kill + relaunch the PC app to see changes; pick recommended options and log them.

- [x] P0 Repo foundation (CI first run pending check) (elevated helper running: scratchpad `elev/helper.ps1`, drop `*.ps1` in `elev/queue`, output in `elev/out`): MIT licence, README, layout from PLAN §2, `VERSION`, `/firmware/` anchored in `.gitignore`, pinned versions in `docs/toolchain.md`, `tools/version`, `tools/gen-tokens`, CI skeleton
- [~] P1 Spikes on the board (S1 done: QIO 80 MHz flash, LCD 80 MHz, no LVGL OS layer; bands later 80 lines (P4); S2 fonts generated; S3 done: 921600 baud; S4 done in P4: RLE16 kept, stream throttled to the PC rate, ~11× on slides, link-bound ~45 KB/s; next S5 deep sleep (needs a person to tap-wake; RTS reset works remotely), S6 LED levels (needs eyes), S7 espflash (in P8), S8 WebGL (in P9); results in `docs/board-info.md`): S1 LVGL+LovyanGFX fps/heap/rotation, S2 fonts, S3 baud, S4 stream codec, S5 deep sleep touch wake, S6 RGB LED, S7 espflash lib, S8 OOTB WebGL in WebView2
- [x] P2 Design round 2 (4 artboards + welcome row published; user review pending, choices logged in `docs/mockups.md`) (with P1): Settings › Home screen, tray menu, permissions sheet, log view, "Show welcome again" row; OOTB is Claude's call (optional storyboard for own reference, no review)
- [x] P3 Link protocol (done: `proto/spec.md`, fenix-proto Rust + 31 golden vectors in `proto/vectors.txt`, C++ `fenix_proto` + MSVC host test 31/31; fenix-link with pause + StreamCanvas, `fenix` CLI built; on-board link task done in P4; 1 h soak passed in P10: 0 RX errors, 0 disconnects): `proto/spec.md`, fenix-proto (Rust), fenix_proto (C++), golden vectors, fenix-link, fenix-cli
- [x] P4 Board core (accepted 2026-10-29: `fenix hello`/`time` work, STATS heap ~134 KB min ~121 KB, 0 RX errors, slides 34.5 fps / 29 fps while streaming): fenix_hal, fenix_ui, screenshots (sim built later, see P5), fenix_sys (settings, clock, link task, LittleFS), init order
  - [x] fenix_hal: LED, deep sleep, touch calibration, time-valid flag (fixed: EN resets report POWERON, so the flag is cleared at boot by `time_boot_check`)
  - [x] fenix_sys: NVS settings, clock (MYT), link task (UART0 921600, HELLO/PING/TIME_SET/REBOOT/STATS; APP_LIST was a stub until P6), LittleFS mount, stream encoder (RLE16 bands into the 16 KB UART TX buffer, stale re-send)
  - [x] fenix_ui: LVGL glue (display, touch, auto-dim, stream hook, fps), widgets, nav stack
  - [x] fenix_apps (P5 started early): boot sweep, Home Tiles/Dock, Settings tree, Session sheet, Stream, LED
  - [x] main: init order; built (796 KB), flashed COM9; `fenix hello`, `fenix time`, `fenix shot` all work; Home PNG matches HomeA
  - [x] Remote touch message (TOUCH 0x13, P→B; spec, Rust, C++, vector 32) + `fenix tap` / `fenix swipe`
  - [x] `tools/capture_screens.sh`: erases NVS, drives every system screen, saves PNGs; golden copies in `docs/screens/board/` (landscape, portrait, Tiles, Dock; P6 added the sample app)
  - [x] Fixed on the way: non-clickable layout boxes (taps were lost), clickable scroll containers (swipes did not scroll), flex track centring (Dock sat left), screen swap double delete, "20%" label wrap, portrait navbar overlap (back label dropped)
  - Screenshots come from the real board over the link while nobody is at it; the PC simulator came later (see P5)
  - [x] Perf pass: stale `board/sdkconfig` fixed (13 → 25 fps), 80-line bands (34.5 fps), stream throttled to the PC rate (29 fps while streaming), wake-from-dim activity bug fixed; `fenix bench`. Numbers in `docs/board-info.md` "Rendering speed"
  - [x] Stale sweep for P4 (docs, comments, CLI help)
  - Boot-to-screen ≤ 1 s not measured with a camera; HELLO answers ~2 s after reset
  - Note: the first `fenix hello` after one flash failed 4× ("no HELLO_ACK") then worked every time; not reproduced since. Watch for it.
- [~] P5 Board UI + system apps (artboards 1–5 exactly, 4 rotations): boot, Home Tiles/Dock, Settings tree, Session, Stream + encoder, motion, LED, auto-dim, golden screenshots
  - [x] Boot sweep line → fade to Home; Home Tiles + Dock (landscape + portrait); Settings (Display, Rotation, Home screen, Storage, Host PC, About); Session sheet; Stream (off / not streaming / on); auto-dim with first-touch wake; LED connect pulse; golden PNGs in `docs/screens/board/`
  - [x] Shut down → deep sleep: link goes silent; RTS reset wakes it (POWERON). Tap-wake: open item for the user
  - [x] Flipped rotations (2, 3): screenshots in `docs/screens/board/`
  - [x] Host app screens (HostWaiting, HostOff) and app tiles fade in/out: done in P6
  - [x] PC simulator (user asked in round 7; built 2026-10-29 after P9, PLAN §P4.2 corrected: Win32 + headless, no SDL2). `docs/toolchain.md` "Board simulator"
    - [x] a. `board/sim` CMake host build (MSVC + Ninja, `board/sim/build.ps1`): LVGL 9.5.0 (managed component, else fetched), `fenix_ui`, `fenix_apps`, `fenix_pkg`, `fenix_proto`; LVGL from the board's Kconfig values (`tools/sim_config.py`, `--check` in the board CI job). Fix found: `fenix_fonts.h` needed `extern "C"` for MSVC (gen_fonts.py updated, firmware still builds)
    - [x] b. Stubs: IDF headers (virtual clock, heap/chip/flash, VERSION, esp_restart), `fx::hal` on a framebuffer, `fx::sys` (settings in memory, clock fixed at 2026-10-29 08:35 MYT, store = folder of .fxp, scripted link events and binds)
    - [x] c. `fenix_sim run <script> --out <dir> [--store <dir>]`; `board/sim/screens.txt` = the capture_screens.sh walk + 6a "Waiting for host PC" (21 shots). vs `docs/screens/board/`: ≤ 0.6% pixels differ (clock minute, antialiasing)
    - [x] d. `fenix_sim window`: Win32 GDI at 2×, DPI-aware, mouse = touch (quick clicks latched), `C` toggles the link; ~0.6% CPU, 13 MB
    - [x] e. `tools/sim_shots.py` (exact compare, `--update`, `--no-build`, diff PNGs); goldens `board/sim/golden/` (148 KB); CI job `sim` on windows-latest
    - [x] f. Docs: toolchain, PLAN corrections, decisions, packages.md (6a now covered)
- [x] P6 App packages (accepted 2026-10-29: install 0.13 s for 1.6 KB, 0.51 s for 21 KB, ~1.6 s for the 64 KB cap; remove leaves the store at its empty 8 KB; reconcile itself is the PC engine's job in P7/P8): `.fxp` spec + compiler, board runtime, atomic install/remove/reconcile, host states, `tools/sample-app` fixture
  - [x] Format 1 spec `docs/packages.md`; `crates/fenix-pkg` compiler + reader (`fenix-pkg build`/`inspect`, limits, 2 tests); `tools/sample-app/board` builds to 1643 B, 15 elements, 6 bindings
  - [x] C++ reader `board/components/fenix_pkg` (same checks as Rust; app ids capped at 48 bytes for LittleFS names; host test passes, survives every single-byte flip)
  - [x] Board store + install in the link task: APP_INSTALL_BEGIN/CHUNK/END (sha256 via mbedtls, `.tmp` then rename, `.sha`), real APP_LIST, APP_REMOVE, cleanup at mount; UART ISR in IRAM, RX buffer 8 KB, link stack 8 KB. Tested on COM9: install 0.13 s (1.6 KB) / 0.51 s (21 KB), reinstall, remove back to 8 KB used, unknown remove = code 6. Package cap lowered to 64 KB (board holds it in RAM); RGB565 assets now little-endian
  - [x] Board runtime `app_pkg.cpp`: tiles (Tiles, Dock, portrait grid) with fade + scale in/out, app screen built from the package, bindings with a per-binding cache, events (tap, long press, switch, slider on release), APP_OPENED/APP_CLOSED, BOARD_STATE screen 5 + app id, HostWaiting (6a) / HostOff (6b), remove while open (6b for 2 s, then Home). Verified on COM9 with `fenix set`/`tap`/`swipe`/`host`
  - [x] Fixed on the way: store mounted after `apps::start` (tiles now load at the boot → Home hand-over); a rebuilt Home kept the old clock state (clock stuck at `--:--`); Settings › Storage shows the real app count and names
  - [x] CLI: `fenix install <file.fxp>` (id/version from the package, rewind on stalled acks), `fenix remove <id>`, `fenix list`
  - [x] CLI: `fenix set <file.fxp> <bind>=<value>...`, `fenix host <id> on|off`; `tap`/`swipe` print the board's replies
  - [x] Golden screenshots refreshed with the sample installed (19 + 3 flipped in `docs/screens/board/`, new: `storage`, `app`, `app_host_off`); flipped-rotation shots are from before P6 (no app tile)
  - [x] Sample app in portrait (`app_portrait.png`); fixed on the way: `grow` in a content-sized parent squeezed cards to 0 px, so `grow` now applies only when the parent has a set size along its flow (`docs/packages.md`)
  - [x] Stale sweep (PLAN layout, board-info heap/image, tasks)
- [x] P7 PC app shell (accepted 2026-10-29: window 64 MB / 4 processes, tray only 4 MB / 1 process, 0.01 % CPU, window 74 ms after launch; reboot over the link reconnects in about 1 s)
  - [x] 1. Scaffold `app/` (Svelte 5 + Vite + TS, `app/src-tauri` as a workspace member): local Inter + Phosphor, tokens CSS, custom title bar, sidebar (Apps, Board, Settings); Apps is an empty state and Settings has Start with Windows + About until P8
  - [x] 2. Engine (Rust, runs without a window): link manager on `fenix-link`, state store, time sync on connect + hourly, events to the UI only while a window exists
  - [x] 3. Tray (native menu: status, detail, Open fenixOS, Quit; left click opens), single instance (second launch opens the window, `--quit` quits, `--quit` with nothing running exits at once), window close destroys the webview, autostart (`--tray`, off by default)
  - [x] 4. Logs: rolling 3 × 1 MB in `%LOCALAPPDATA%\fenixOS\logs` incl. board LOG frames; log sheet (All / Board / Apps / Problems, Copy, Open log folder)
  - [x] 5. Finding-the-board screen (PcConnect, artboard 7); Board page (facts, firmware match, Reboot board, Logs)
  - [x] 6. Accept with `tools/measure_app.ps1`: window open 64.1 MB over 4 processes (target 120), tray only 4 MB over 1 process (target 35), CPU 0.01 %, window handle 74 ms after launch (target 1.5 s). Release exe 3.9 MB
  - [x] Fixed on the way: after "Reboot board" the board answered PINGs without a new HELLO, so the app stayed "connected" with stale facts. The board now answers a PING before HELLO with `ERROR` 5 (no session) and the PC handshakes again: request to reconnect about 1 s
- [x] P8 PC features (artboards 8–13 exactly; accepted 2026-10-29): dashboard, app settings, add app + registry + permissions, sidecars + fenix-sdk + elevation, board page + mirror, update flow + espflash, settings
  - [x] 1. Registry: manifest format (`registry/apps/<id>.json`, PLAN 3.7) documented in `docs/registry.md`; `tools/registry.py` builds `registry/index.json` and `--check`s it (CI); sample app manifest; `FENIX_REGISTRY=<folder>` for a local registry (local file paths allowed only there)
  - [x] 2. Sidecar protocol + `crates/fenix-sdk` (two one-way pipes `<base>.down` / `<base>.up`, see `docs/sidecars.md`; `tools/sample-app/build.ps1` builds package + PC half + local registry): named pipe `\\.\pipe\fenixos-<id>-<nonce>`, u32 length + JSON (`hello`, `settings`, `opened`, `closed`, `event`, `set`, `log`), documented in `docs/sidecars.md`; sample PC half in `tools/sample-app/pc` (CPU + memory to the binds, logs events)
  - [x] 3. Engine app manager (`app/src-tauri/src/{apps,config,registry,sidecar}.rs`). Verified on COM9 with the local registry: Add downloads and checks the files, starts the PC half, installs on the board in 0.1 s; CPU / memory values reach the board; board taps and switch changes reach the PC half; settings and the settings button reach it too: `%APPDATA%\fenixOS\config.json`, apps in `%LOCALAPPDATA%\fenixOS\apps\<id>\<version>\`, download + sha256 check, enable (push package, start sidecar, HOST_STATE on) / disable (stop sidecar, remove from board), reconcile on connect, route EVENT / APP_OPENED / APP_CLOSED to sidecars and `set` to BIND_SET, restart back-off (3 tries, then tile error), elevated start on a worker thread
  - [x] 4. UI: Apps dashboard (8, incl. empty state), App settings (9, rendered from the manifest schema), Add app (10) + permissions sheet (15)
  - [x] 5. Board page (11): live mirror from STREAM (binary frames over IPC, clicks and drags on the picture are board touches, stops 2 s after the page stops asking), facts, reboot, Reinstall firmware. Uptime now counts from a boot timestamp (`booted_at_ms`); it showed "-1 h -1 min" right after a reconnect and stale values after a page switch
  - [x] 6. Update flow (12): mismatch dialog on connect ("Later" hides it until the next connect), Reinstall firmware on the Board page, espflash 4.6 library with progress, reconnect and a "Board updated" check. Firmware bundled by `tools/bundle_firmware.py` into `app/src-tauri/resources/firmware` (bins + `firmware.json` with sha256), `FENIX_FIRMWARE` overrides. Verified on COM9: v0.0.9 board updated from the dialog in 23 s with live progress (0 → 20 → 64 → 100 %); Reinstall of the same version 10 s (unchanged regions skipped). Found: `board/CMakeLists.txt` did not re-run CMake on a VERSION change (fixed with CMAKE_CONFIGURE_DEPENDS)
  - [x] 7. Settings (13), as the PcSettings artboard:
    - [x] a. Connection port (verified: COM9 fixed, then Automatic, each reconnects in ~0.1 s, saved in config): `Link::set_port` switches at runtime (session ends with "port changed"); `list_ports` / `set_port` commands; config `port`; picker menu "Automatic (COMx)" + every USB serial port
    - [x] b. Updates (verified: start-up check and "Check for updates" both say up to date, no release exists yet): `tauri-plugin-updater` 2.14.1 now (signing key made locally, kept outside git, `%USERPROFILE%\.tauri\fenixos-updater.key`; pubkey in `tauri.conf.json`; endpoint `releases/latest/download/latest.json`); "Check for updates" row + status; check once at start when `check_updates`; install + restart. Release CI signing stays P10
    - [x] c. About (licenses: 7 board, 4 web incl. the Svelte runtime, 293 Rust crates): Source code link, "Open-source licenses" sheet from `tools/licenses.py` (Rust crates + npm packages + firmware libraries + fonts/icons, `--check` in CI); "Show welcome again" row lands with P9 (needs the welcome flow)
    - [x] d. Verify in the app, docs (toolchain, decisions, PLAN), commit
  - [x] 8. Accept (2026-10-29): full loop with the sample app from a local registry (add, permissions, download + check, board install 0.1 s, live CPU/memory, events both ways, settings, remove: steps 3–4); a v0.0.9 board updated from the dialog in 23 s, and the Sample app was still installed after both reflashes (the app store partition is not in the image); mirror 10 fps (the cap) while the Home dock scrolls, 9–10 sustained. Known limit: a fast-moving picture can show a torn frame, because the stream has no frame-end marker (would need proto 2; noted in `docs/decisions.md`)
  - [x] 9. P8 wrap-up: docs + stale sweep (PLAN flasher text corrected: separate bins, not a merged image; stale `dead_code` allowance removed)
- [x] P9 First-run experience: 3D board model, plug-in flow (3 s wait if already plugged), probe + first install, "Show welcome again"
  - [x] 1. (Done: GPU 165 fps / 1 % CPU / 89 MB vs software WebGL 122 fps / 40 % CPU / 184 MB, so the welcome window has its own GPU-on WebView2 folder; `docs/toolchain.md` "PC spike results") S8 spike: three.js 0.186.1 in WebView2 under the main window's lean args (`--disable-gpu`) vs GPU on: WebGL available? fps and memory at 1280×800. Decide the OOTB rendering mode (likely its own window with its own WebView2 data folder and GPU args, destroyed when done, so the main window stays lean); results in `docs/toolchain.md`, decision in `docs/decisions.md`
  - [x] 2. (Done: `app/src/ootb/scene.ts`, `screen.ts`) Board model (`app/src/ootb/`): procedural ESP32-2432S028R at real size (86 × 50 mm PCB, 1.6 mm, 2.8" panel with 57.6 × 43.2 mm active area, ESP32 module, SD slot, micro-USB, USB-C, JST, RESET/BOOT on the back as in `pics/`), dark PCB with gold pads (PLAN §3.9), glass with reflection, screen as a canvas texture (boot line, progress); USB-C cable that slides in; soft studio light; slow drift with the DESIGN.md ease
  - [x] 3. (Done; `flash::connect` retries the port for 4 s because the paused link lets go only after its handshake try) Engine: `probe_board(port)` (espflash connect: chip, MAC, flash size; resets into the bootloader, only for silent CH340 ports) and `install_board(port)` (full erase, then the bundled bins, progress in `State::update`); the link pauses around both
  - [x] 4. (Done: `app/src/ootb/Welcome.svelte`; old fenixOS gets an update offer) Flow (`OOTB` state machine): Welcome → board check (already plugged: wait 3 s; else the hover loop with "Plug in your board" + CH340 hint) → plugged animation → firmware check (fenixOS: sync; silent: probe → "Install fenixOS on this board? This erases what is on it." → flash with progress on the model screen) → done → dashboard (empty state). `prefers-reduced-motion`: cross-fades
  - [x] 5. Config: show the OOTB on first launch (`welcome_done` false), set it at the end (Skip also sets it); Settings > About "Show welcome again" (artboard 13 row)
  - [x] 6. Accepted 2026-10-29: erased board probed as "esp32 rev 3.1, 4MB", one confirm, erase + install 25 s with progress on the model screen, Home, dashboard; the Sample app went back on by itself (0.1 s); 165 fps throughout, 137 MB / 7 % CPU while playing, 68 MB after. Accept: erase the board's flash (blank like day one), launch with `welcome_done` false, plug-in to dashboard with only the install confirm; smooth at the S8 numbers; measure memory with the OOTB open and after it closes
  - [x] 7. Docs (PLAN, toolchain, decisions), stale sweep, commit
- [~] P10 Release v0.1.0: CI release, updater key (secret), updater test, README, sweep, hardware pass. Plan (2026-10-29):
  - [x] 1. (Written; a local signed `pnpm tauri build` makes `fenixOS_0.1.0_x64-setup.exe` 3.84 MiB + `.sig`) `.github/workflows/release.yml` on tag `v*`: check tag = `VERSION`; job `firmware` (idf v5.5.5 container) builds and uploads `board/build` bins + `flasher_args.json` + `project_description.json`; job `app` (windows-latest) downloads them, runs `tools/bundle_firmware.py`, builds the NSIS installer with `tauri-apps/tauri-action` (signed updater artifacts, `latest.json`), publishes a GitHub release. NSIS `installMode` set to `currentUser` explicitly (no UAC)
  - [x] 2. Done 2026-10-29: the user set `TAURI_SIGNING_PRIVATE_KEY` with `gh secret set` (stdin). Was blocked: the agent may not write GitHub secrets. Run `gh secret set TAURI_SIGNING_PRIVATE_KEY < %USERPROFILE%\.tauri\fenixos-updater.key` (cmd) once. Secret: `TAURI_SIGNING_PRIVATE_KEY` from `%USERPROFILE%\.tauri\fenixos-updater.key` via `gh secret set` (stdin, never echoed); the empty password is set in the workflow
  - [x] 3. Unsigned dry run passed 2026-10-29 (run 40829389825: firmware + app jobs green, `windows-x64-nsis` artifact 4.03 MB, `firmware` artifact; no release created; hand-started runs without the secret skip the updater signature). Signed dry run passed (run 40834914931: `.sig` made). Dry run: `workflow_dispatch` builds everything without publishing (artifact upload only), so the tag is pushed only once the pipeline is green
  - [x] 4. (Done: status v0.1.0, board screenshots, install, how apps work, build + simulator) README for v0.1.0: install, first run, how apps work, build from source (+ simulator), screenshots from `docs/screens/`
  - [x] 5. (Corrected 2026-10-29: the screen walk compares the LVGL picture, not the panel; the user then found 80 MHz LCD corruption and mirrored touch, both fixed, see Open) (Done: soak 60 min, 0 disconnects, 0 RX errors / 4578 frames, max RTT 22 ms, min heap 110 KB; board walk pixel-identical to `docs/screens/board/` except the live clock; `docs/board-info.md` "Release hardware pass") Hardware pass: 1 h `fenix soak`, all screens via the simulator goldens + a board capture walk, rotations; shut down / wake stays an open item (needs a finger)
  - [x] 6. (Done: toolchain P10 refs, PLAN P10.3 + updater note corrected, decisions logged, CLI help checked; repeat after the tag) Sweep: docs, comments, CLI help, `tasks.md`; "in development" status lines
  - [!] 7. (Blocked 2026-10-29 16:05: CI green on main, but the agent may not push a release tag (auto-mode "Create Public Surface"). User runs: `git tag -a v0.1.0 -m "fenixOS v0.1.0"` then `git push origin v0.1.0`) Tag `v0.1.0`, release CI publishes it; check the release assets (`fenixOS_0.1.0_x64-setup.exe`, `.sig`, `latest.json`)
  - [x] 8a. Local updater test passed 2026-10-29: a 0.0.9 client (`--config` override: endpoint http://127.0.0.1:8765, `dangerousInsecureTransportProtocol`) found v0.1.0 at start, Settings > Install fetched the signed installer, verified it, installed it passively, and the installed v0.1.0 started by itself (~1 s). A tampered signature is refused ("The signature verification failed", nothing installed). Found: an install failure read "could not check"; Settings now says "could not install vX"
  - [x] 9a. Local clean install passed 2026-10-29: config removed, signed installer `/S` (per user, no UAC) into `%LOCALAPPDATA%\fenixOS` (shared with the app's data folder), erased board probed as ESP32 rev 3.1 / 4MB / COM9, one confirm, firmware installed, dashboard with "Board connected, fenixOS v0.1.0". Uninstall `/S` removes only `fenixos.exe`, `firmware/`, `uninstall.exe` and its registry entry; apps, logs, WebView folders and `config.json` stay. Config restored and the test install removed afterwards
  - [ ] 8. Updater test (corrects PLAN P10.3: the endpoint is `releases/latest`, which skips pre-releases, so a `v0.1.1-test` pre-release would never be offered): a local build with VERSION 0.0.9 (not committed) must find v0.1.0, download, verify the signature and install it
  - [ ] 9. Accept: clean install from the GitHub release (uninstall first, fresh config), erased board, OOTB to a working dashboard

## Decisions

- 2026-10-29: **Keep current versions.** ESP-IDF v5.5.5, LovyanGFX 1.2.35, CH340 PC driver 3.3.2011.11. No upgrade to IDF v5.5.6 / v6.x or the 2024 CH340 driver for now (see `docs/board-info.md` "Version status").
- 2026-10-29: **Corrected: update the CH340 driver.** 3.3.2011.11 blue-screened the PC (bugcheck 0x48) when `fenix.exe` was force-killed with a COM9 read pending. The app now closes the port before exit (`Link::shutdown`).

## Open (needs user input or flashing)

- [x] Touch corner coverage (2026-10-29, user at the board): the default range was mirrored left to right; fixed (`y_min`/`y_max` reversed), confirmed by the user. `docs/board-info.md` "Hands-on review"
- [ ] User: install WCH CH341SER 3.9.2024.9 (needs admin; `docs/board-info.md` Version status). Until then: do not force-kill fenixOS programs holding COM9, quit the app before unplugging
- [ ] Physical USB replug with the PC app running (P7 reconnect was measured with a board reboot over the link)
- [~] Tap-to-wake from Shut down: works since the touch-bus pins are held in sleep (user confirmed); the follow-up "touch dead after wake" is fixed and flashed. LED-off-in-sleep fix flashed 15:23; the board then woke from sleep by a tap (app log 15:56: boot reason "wake from sleep"). Still to check by eye: the LED is off in sleep, and the time survives a wake
- [ ] RGB LED levels (S6): connect pulse looks right, not too bright
- [ ] V1.4 RGB LED and LDR (IO34) circuits: resistor values differ from the schematic; LED is tested in P1 S6, LDR is not in v0.1
