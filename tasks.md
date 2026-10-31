# fenixOS tasks

fenixOS: an "OS of sorts" for the ESP32-2432S028R (CYD) and its Windows app. Plan: `docs/PLAN.md`. Done phases in full: `docs/legacy/tasks-history.md`.

Legend: `[x]` done, `[~]` in progress, `[!]` blocked, `[ ]` to do.

Progress rule: update this file after every step (done / in progress / blocked + notes), so a cut-off session resumes exactly here. Each phase: acceptance check, docs, stale sweep, commit + pull --rebase + push.

## Done

Phase 0-2 (2026-10-29): repo setup, finding checks, v0.1 discovery and design (`ideas.md`, `DESIGN.md`, `docs/PLAN.md`).

- P0 Repo foundation
- P1 Spikes on the board
- P2 Design round 2
- P3 Link protocol
- P4 Board core
- P5 Board UI + system apps
- P6 App packages
- P7 PC app shell
- P8 PC features
- P9 First-run experience
- P10 Release v0.1.0
- P11 Connect screen in 3D
- P12 Build time
- P13 Board page mirror off by default
- P14 Stream state in sync between board and PC
- P15 The board's Stream app is the only stream switch
- P16 BSOD #2
- P17 PC app motion and polish per DESIGN.md Motion
- P18 Release v0.1.1
- P19 LED green while shut down
- P20 Release v0.1.2
- P21 Burn-in protection (board check by the user 2026-10-31: works)
- P22 Release v0.1.3
- P23 CH340 crash path while flashing
- P24 Full docs sweep
- P25 Release v0.1.4
- P26 Spikes and backups off esptool
- P27 Task manager
- P28 Release v0.1.5
- P29 RAM and flash headroom for the radios
- P30 Radios research
- P31 Flash, RAM and CPU trims, no radios yet; release v0.1.7
- P32 Radio manager
- P33 Bluetooth
- P34 Wi-Fi: PC link, internet, phone web page (Wi-Fi fallback checked in P46: board on a powerbank, the app found it on `tcp:fenixos.local`, 16:30 and 17:15 in the app log)
- P35 Design-language fixes
- P37 Known limits, open checks, docs pass
- P38 Remove Bluetooth, BLE included
- P39 App packages from flash in pieces, not whole in RAM
- P40 Link buffer trims, measured
- P41 Docs audit 1 (overnight run)
- P42 File-by-file code audit; release v0.1.13
- P43 Docs audit 2
- P44 App repo template (`fenixos-app-template.zip`); release v0.1.14

Releases: v0.1.0 (P10) to v0.1.19 (P42, P44 to P48); tags on GitHub.

## Open (needs the user)


## Now

- [x] P45 Close the gaps in P42 and P43 (stop-hook review: not every file read, docs pass 2 not full, Hallmark not applied, sim hang open)
  - [x] 1. PC UI files not yet read whole: `api.ts`, `motion.ts`, `icons.ts`, `Icon`, `AppIcon`, `Switch`, `TitleBar`, `Mirror`, `LineChart`, `LogSheet`, `LicensesSheet`, `PermissionSheet`, `UpdateDialog`, `Connect` (rest), `ootb/Welcome.svelte`, `ootb/scene.ts`, `ootb/screen.ts`, `welcome.ts`, `main.ts`, `app.css`. Done: Connect "Board found" got a Set up button (its Settings hint was unreachable); 3D scene pauses while hidden; Mirror reuses its ImageData; update bar scales instead of animating width; sheet buttons 10 px radius; accent text off `surface-2`; `permissionText` unexported
  - [x] 2. Firmware files not yet read whole: `fenix_hal` (h, cpp), `fenix_ui` (`widgets.cpp`, `fenix_ui.h`), `fenix_pkg` (h, cpp, test), `fenix_proto` (h, cpp), `app_pkg.cpp`, `fenix_sys.h`. Done: use after free of a closed app's package during the slide out (fixed, `docs/board-info.md`); `Package::parse` empties itself on failure (+ host test); app buttons 40 px; app help text without "by USB"; stale comments in `fenix_hal.h`, `fenix_ui.h`
  - [x] 3. Hallmark pass (locked system: DESIGN.md wins; Hallmark's Floor used as a checklist where it does not contradict it): run `sloplint` on the built PC UI (`--scope app`), check focus-visible, reduced motion, literal colours, `transition: all`, width/height animation; fix what DESIGN.md allows, log the rest. Done: check stamp and waivers atop `app.css` (sloplint on it: 0 FAIL); every literal colour and shadow now an `app.css` variable; section titles sentence case; `overflow: clip`; `--font-ui`; `text-wrap: balance` on heads. Built chunks lose the stamp comment in minification (gate 20 there dismissed)
  - [x] 4. Sim hang after `app_portrait`: rerun `sim_shots.py` in a loop to reproduce; find the cause or bound it (timeout in `sim_shots.py`). Done: reproduced (1 in ~30), caught with a temporary DbgHelp watchdog: a label drawing freed package text; fixed (item 2), 150 runs clean; `sim_shots.py` kills a run after 60 s
  - [x] 5. Docs pass 2, complete: read every current `.md` (root, `docs/` except `legacy/` and `vendor/`, `proto/`, `board/`, `app/`, `tools/`, `registry/`, `templates/`), fix stale facts, cut repetition and filler (token optimisation), keep Corrected notes short. Done so far: firmware README, tooling, sidecars (numbers checked in `apps.rs`, `sidecar.rs`), mockups (status, PC Settings row), registry (setting limits), README, ANALYSIS, DESIGN (accent on `surface-2`, chrome variables, switch sizes), proto spec, packages (font set, app lifetime), template docs (zip rebuilt), tasks (P41 to P44 moved to the history file), ideas (push time, link rate, codec, icon weights), toolchain (main window GPU, PoC gotchas, cores line), PLAN (pipes, app lifetime, welcome entry, PC UI testing), vendor README (SPI clock, touch range), board-info current state, decisions rows
  - [x] 6. Build, test, release if code changed; decisions log; commit. Done: version, tokens, registry, template, licenses, sim config checks ok; cargo test all pass; proto host test 40/40; pkg host test pass; svelte-check 0/0; sim 25/25; v0.1.15 firmware and signed app built, installed (/S), flashed (`fenix hello`: v0.1.15, heap 82.2 KB), tagged
- [x] P46 Wi-Fi link test on a powerbank (user, 2026-10-31) and stream fps
  - [x] 1. Board on a powerbank, USB unplugged: the PC app found it on `tcp:fenixos.local`, streamed (user tested). Ping, web page, `/status.json` ok
  - [x] 2. Bug: after the app quit, port 7350 accepted nothing (one connection slot; the board's `tcp_link` waited in `recv` on a peer gone silent; the session ended on the 3 s heartbeat, the socket stayed). Fix: `SO_RCVTIMEO` 3 s on the client socket in `net.cpp`, so a silent peer frees the slot. Spec and board-info notes. Tested: a silent client is closed, `fenix hello` gets in after 3.1 s
  - [x] 3. `fenix bench` takes a stream fps (was fixed at 10); measured 10 / 15 / 20 / 25 over USB and Wi-Fi (table in `docs/board-info.md` P46): USB full at ~45 KB/s; Wi-Fi 25 fps +47% pictures
  - [x] 4. Stream rate: 10 over USB, 25 over Wi-Fi (`STREAM_FPS_WIFI` board, `MIRROR_FPS_WIFI` PC); the PC mirror polls every 40 ms (was 100, its own 10 fps ceiling); docs and decisions rows
  - [x] 5. Build, test, release, flash, recheck Wi-Fi on the powerbank (user: "stream over wifi is fine"). Done: checks ok, cargo test pass, svelte-check 0/0, sim 25/25; v0.1.16 built, installed (/S), flashed (`fenix hello`: v0.1.16), tagged. Left: the user streams over Wi-Fi on the powerbank
- [x] P47 Connect screen: a Wi-Fi run in 3D (user, 2026-10-31: "add 3d model animations if board was found thru wifi")
  - [x] 1. `scene.ts`: pose `wireless` (cable pulls away, the antenna edge turns in, board nearer the centre), three `accent` arcs pulsing out from the ESP32 module's antenna, an antenna glow; reduced motion: no arcs, the glow only. Checked in a Chrome render (full rings crossed the screen: arcs instead)
  - [x] 2. `Connect.svelte`: `wireless` prop (board port `tcp:`), the Wi-Fi run (rings, screen boots, dashboard), copy "Connected over Wi-Fi"; waiting copy says a board on Wi-Fi connects on its own. `App.svelte` passes it
  - [x] 3. Docs: DESIGN.md Motion exceptions, mockups row 7, decisions row
  - [x] 4. svelte-check, build, release v0.1.17 (user saw it live: "looks nice"). Done: svelte-check 0/0, version check ok; v0.1.17 built, installed (/S), flashed (`fenix hello`: v0.1.17), tagged. Left: the user sees it live with the board on the powerbank
  - [x] 5. v0.1.18 built, installed (/S), flashed (`fenix hello`: v0.1.18). "Show welcome again" (user: it did not play the new run): the welcome uses the Wi-Fi run for a board on `tcp:` (cable stays away, arcs go on over the final front view), copy "A board is on your Wi-Fi"
- [x] P48 Wi-Fi link drops during a busy stream (user, 2026-10-31: "app lose connection temp after stopping stream then reconnects")
  - [x] 1. Reproduced (v0.1.18, board on the powerbank): `fenix bench 8 25` over Wi-Fi with taps, the board reset the connection mid-stream in 3 of 4 runs (os error 10054); stops on a calm screen were clean. `fenix bench` now prints link drops and watches the link 4 s after the stream stops
  - [x] 2. Cause: since P37 one 500 ms stall ended the link (a frame with no ring room, or one `send()` timeout); at 25 fps over Wi-Fi such stalls happen. Fix in `net.cpp`: the frame is dropped instead (a stream band is redrawn later), `tcp_tx` retries a timed-out send and ends the link only on an error or 3 s without progress. Firmware built
  - [x] 3. Flashed (the CH340 port failed to open with two power sources on the board; it worked after the user restarted the PC and plugged the micro-USB alone), busy test with the stop pressed on the board: USB 4/4 and Wi-Fi 4/4 without a drop, two PC-stopped Wi-Fi benches clean, Wi-Fi 72-88 KB/s. v0.1.19: checks, cargo test, svelte-check 0/0, sim 25/25; built, installed (/S), flashed (`fenix hello`: v0.1.19), the app connects on COM9 without an update dialog; tagged. Docs: board-info P34 and P48, decisions
- [x] Overnight decisions log: `docs/decisions.md`, section "2026-11-01: overnight run" (each choice made for the user and the options not taken), kept up to date through P41 to P44. v0.1.14 (P44 sidebar fix) built, installed, flashed and tagged

- [ ] P36 Later
  - [ ] On-board Wi-Fi password entry (the scan list comes in P34; needs LVGL keyboard and textarea, which P31 turns off: more flash)
  - [x] Phone Web Bluetooth page: dropped, P38 removed Bluetooth (needs BLE on the board)
  - [ ] OTA over Wi-Fi after the SD card (user; 2026-10-31: not before the SD card, even if P38 makes two slots fit the 4 MB flash). A/B slots need ~2 x 1.6 MB; the 4 MB flash fits that only with the apps partition (1.94 MB) moved to the SD card
  - Not planned: both radios at once (P29: ~40 KB free), LCD SPI at 80 MHz (garbles the panel, P10), fps caps for RAM (no gain), A2DP and HID (user)

## Decisions (current; superseded ones in the history file)

- 2026-10-31: **Builds may use all cores** (user, after gaming: "u can go back to using all cores threads and testing pc app normally grabbing focus"): `FENIX_CORES=16` with `tools/capped.ps1`, one build at a time; the PC app may be opened and focused for tests. Supersedes the 4-core limit the same evening.
- 2026-10-30: **Releases are the agent's job** (user: "handle all the versioning changes, release pushes urself"; 2026-10-29: "i permit u at all times"). Flow: bump VERSION, build locally (firmware and app), install the app (`/S`) and flash the board with `fenix flash`, then push the tag; GitHub builds at its own pace.
- 2026-10-29: **Versions:** ESP-IDF v5.5.5, LovyanGFX 1.2.35; CH340 driver updated to 4.0.2026.2 (3.3.2011.11 blue-screened the PC). See `docs/board-info.md` "Version status".
