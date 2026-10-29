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
- [~] P3 Link protocol (done: `proto/spec.md`, fenix-proto Rust + 31 golden vectors in `proto/vectors.txt`, C++ `fenix_proto` + MSVC host test 31/31; fenix-link with pause + StreamCanvas, `fenix` CLI built; next: on-board link task in P4, then 1 h soak): `proto/spec.md`, fenix-proto (Rust), fenix_proto (C++), golden vectors, fenix-link, fenix-cli
- [x] P4 Board core (accepted 2026-10-29: `fenix hello`/`time` work, STATS heap ~134 KB min ~121 KB, 0 RX errors, slides 34.5 fps / 29 fps while streaming): fenix_hal, fenix_ui, SDL2 sim + screenshots, fenix_sys (settings, clock, link task, LittleFS), init order
  - [x] fenix_hal: LED, deep sleep, touch calibration, time-valid flag (fixed: EN resets report POWERON, so the flag is cleared at boot by `time_boot_check`)
  - [x] fenix_sys: NVS settings, clock (MYT), link task (UART0 921600, HELLO/PING/TIME_SET/REBOOT/STATS/APP_LIST stub), LittleFS mount, stream encoder (RLE16 bands into the 16 KB UART TX buffer, stale re-send)
  - [x] fenix_ui: LVGL glue (display, touch, auto-dim, stream hook, fps), widgets, nav stack
  - [x] fenix_apps (P5 started early): boot sweep, Home Tiles/Dock, Settings tree, Session sheet, Stream, LED
  - [x] main: init order; built (796 KB), flashed COM9; `fenix hello`, `fenix time`, `fenix shot` all work; Home PNG matches HomeA
  - [x] Remote touch message (TOUCH 0x13, P→B; spec, Rust, C++, vector 32) + `fenix tap` / `fenix swipe`
  - [x] `tools/capture_screens.sh`: erases NVS, drives every system screen, saves PNGs; golden copies in `docs/screens/board/` (16 screens: landscape, portrait, Tiles, Dock)
  - [x] Fixed on the way: non-clickable layout boxes (taps were lost), clickable scroll containers (swipes did not scroll), flex track centring (Dock sat left), screen swap double delete, "20%" label wrap, portrait navbar overlap (back label dropped)
  - Screenshots come from the real board over the link while nobody is at it; the PC simulator the user asked for is still planned (see P5)
  - [x] Perf pass: stale `board/sdkconfig` fixed (13 → 25 fps), 80-line bands (34.5 fps), stream throttled to the PC rate (29 fps while streaming), wake-from-dim activity bug fixed; `fenix bench`. Numbers in `docs/board-info.md` "Rendering speed"
  - [x] Stale sweep for P4 (docs, comments, CLI help)
  - Boot-to-screen ≤ 1 s not measured with a camera; HELLO answers ~2 s after reset
  - Note: the first `fenix hello` after one flash failed 4× ("no HELLO_ACK") then worked every time; not reproduced since. Watch for it.
- [~] P5 Board UI + system apps (artboards 1–5 exactly, 4 rotations): boot, Home Tiles/Dock, Settings tree, Session, Stream + encoder, motion, LED, auto-dim, golden screenshots
  - [x] Boot sweep line → fade to Home; Home Tiles + Dock (landscape + portrait); Settings (Display, Rotation, Home screen, Storage, Host PC, About); Session sheet; Stream (off / not streaming / on); auto-dim with first-touch wake; LED connect pulse; golden PNGs in `docs/screens/board/`
  - [x] Shut down → deep sleep: link goes silent; RTS reset wakes it (POWERON). Tap-wake: open item for the user
  - [x] Flipped rotations (2, 3): screenshots in `docs/screens/board/`
  - [ ] Host app screens (HostWaiting, HostOff) and app tiles fade in/out: come with P6
  - [ ] PC simulator (user asked in round 7): after P8, before release
- [~] P6 App packages: `.fxp` spec + compiler, board runtime, atomic install/remove/reconcile, host states, `tools/sample-app` fixture
  - [x] Format 1 spec `docs/packages.md`; `crates/fenix-pkg` compiler + reader (`fenix-pkg build`/`inspect`, limits, 2 tests); `tools/sample-app/board` builds to 1643 B, 15 elements, 6 bindings
  - [x] C++ reader `board/components/fenix_pkg` (same checks as Rust; app ids capped at 48 bytes for LittleFS names; host test passes, survives every single-byte flip)
  - [ ] Board store + install in the link task: APP_INSTALL_BEGIN/CHUNK/END (sha256 via mbedtls, `.tmp` then rename, `.sha`), real APP_LIST, APP_REMOVE, delete `*.tmp` at mount
  - [ ] Board reader + LVGL builder `app_pkg.cpp`: bindings (BIND_SET), events (EVENT), APP_OPENED/APP_CLOSED, app tiles with fade in/out, HostWaiting/HostOff
  - [ ] CLI: `fenix install <file.fxp>` (rewind on stalled acks), `fenix remove <id>`, `fenix apps`, `fenix set <bind> <value>`
  - [ ] Accept: install <= 5 s, remove leaves nothing, screenshots of the sample app in both orientations
- [ ] P7 PC app shell: Tauri 2 + Svelte 5 scaffold, engine, tray, single instance, destroy webview, autostart, logs, finding-the-board, time sync
- [ ] P8 PC features (artboards 8–13 exactly): dashboard, app settings, add app + registry + permissions, sidecars + fenix-sdk + elevation, board page + mirror, update flow + espflash, settings
- [ ] P9 First-run experience: 3D board model, plug-in flow (3 s wait if already plugged), probe + first install, "Show welcome again"
- [ ] P10 Release v0.1.0: CI release, updater key (secret), updater test, README, sweep, hardware pass

## Decisions

- 2026-10-29: **Keep current versions.** ESP-IDF v5.5.5, LovyanGFX 1.2.35, CH340 PC driver 3.3.2011.11. No upgrade to IDF v5.5.6 / v6.x or the 2024 CH340 driver for now (see `docs/board-info.md` "Version status").

## Open (needs user input or flashing)

- [ ] Touch corner coverage of the default raw range (tap the 4 corners while logging)
- [ ] Tap-to-wake from Shut down (deep sleep, ext0 on GPIO36), and the time surviving it
- [ ] RGB LED levels (S6): connect pulse looks right, not too bright
- [ ] V1.4 RGB LED and LDR (IO34) circuits: resistor values differ from the schematic; LED is tested in P1 S6, LDR is not in v0.1
