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

- Back up `%USERPROFILE%\.fenixos\fw-signing.pem` once P52 makes it (the user)

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
- [x] P49 Template zip check (user, 2026-11-01: "is the zip file uptodate and ready"): zip matched its source; SDK, compiler and protocol unchanged since the v0.1.13 pin, so the pin stays. Added the `accent`-on-`surface_2` rule to the template's `design.md`; `tools/build.ps1` now finds cargo's target folder (`cargo metadata`), it failed with `CARGO_TARGET_DIR` set. Tested from a fresh unzip: `fenix-pkg` installed from tag v0.1.13, `build.ps1` made `app.fxp` (1468 B) and `app.exe`, `app.py check` and `local` ok
- [x] Overnight decisions log: `docs/decisions.md`, section "2026-11-01: overnight run" (each choice made for the user and the options not taken), kept up to date through P41 to P44. v0.1.14 (P44 sidebar fix) built, installed, flashed and tagged
- [x] Morpheus requests discussed (user, 2026-11-01): doc moved to `docs/requests/morpheus.md` with the answer; RAM sized for Wi-Fi on, no stream (decisions); stale docs fixed (board-info current state v0.1.19, PLAN image ref, packages bool on a label, the 16-value bind queue). Standby for the open questions above
- [x] SD card check (user, 2026-11-02: card inserted; Morpheus work set aside)
  - [x] 1. Spike `board/spikes/sd`: SPI3 (CS 5, MOSI 23, CLK 18, MISO 19), card info, sector 0, mount (never formats), free space, raw and file speed at 20 and 40 MHz, a 1 MB temp file written, read and deleted
  - [x] 2. Flashed the spike (`fenix flash`), read UART0, flashed fenixOS v0.1.19 back (`fenix hello`: v0.1.19, settings kept)
  - [x] 3. Results in `docs/board-info.md` "SD card check": 8 GB SDHC, FAT32, 40 MHz works (2.7 MB/s raw reads); FAT32 over exFAT
- [x] SD card run 2 (user, 2026-11-02: "reformat ... enable long file names ... find a fix" for the bus clash): spike formats only a card not in the SD Association layout (done once: partition and data area on 4 MB boundaries, 32 KB clusters), 40 MHz, LFN on, 4 KB stdio buffer, touch on bit-banged SPI while the card streams on the other core (346 µs per touch read, no effect on SD). fenixOS v0.1.19 flashed back. Results: `docs/board-info.md` "Run 2"
- [x] P50 SD card base, release v0.1.20 (user, 2026-11-02: "implement it"; picks in `docs/decisions.md` "2026-11-02: SD, apps, OTA")
  - [x] 1. Touch on software SPI: `fenix_hal/include/fenix_touch.hpp` (XPT2046 bit-banged, LovyanGFX's frame and median filter), `fenix_lgfx.hpp` uses it; SPI3 left to the SD card
  - [x] 2. `fenix_sys/sd.cpp`: SPI3 at 40 MHz, FAT32 at `/sd`, long names on the heap, 512 B sectors, `max_files` 4; one-shot mount task; probe again from Settings › Storage and on HELLO; a card that stops answering is marked gone. Found: SPI DMA AUTO fails next to the LCD (falls back to channel 2); the local `sdkconfig` kept 8.3 names and 4 KB sectors (mount cost 28 KB, now 9.6 KB). Image 1,155,568 B (+38 KB); heap with Wi-Fi and card 70.6 KB (v0.1.19: 79.9)
  - [x] 3. `SD_STATE` 0x1C (B→P, optional): Rust and C++ codecs, vector appended, spec row
  - [x] 4. Board Settings › Storage: SD card group; simulator stub and golden
  - [x] 5. PC: engine state, Board page row, Task manager meter, `fenix sd`
  - [x] 6. Docs (spec, board-info, PLAN, README, decisions, Morpheus refs), tests (checks ok, cargo 0 failed, proto host 41/41, svelte-check 0/0, sim 25/25), v0.1.20 built, installed (/S), flashed (`fenix hello`: v0.1.20; `fenix sd`: ready, 7671 MB free, 40 MHz), tagged. Touch on software SPI checked on the panel by the user ("it works")
- [ ] P51 Apps on the SD card, on/off, sync, v0.1.21 (design: `docs/plan-sd-ota.md`; overnight run, no go needed between phases)
  - [ ] 1. Board store with two back ends: SD (`/sd/fenixos/apps/<id>.fxp|.sha|.tmp`, leftovers cleaned at mount) and LittleFS; SD first, flash only with no card and ≤ 128 KB; one app in one place. No fixed cap on the card (free space, 4 GB per file); 128 KB per package on flash (was 64 KB everywhere). Compact `AppMeta` (id 48, version 16, sha, where, state). Host tests
  - [ ] 2. `MAX_APPS` 32; `APP_INDEX_REQ`/`APP_INDEX` 0x2B/0x2C (paged), `APP_TURN_ON` 0x2D, `APP_RESULT` code 7; Rust and C++ codecs, vectors appended, spec. `APP_LIST` kept (first 10) for the update path
  - [ ] 3. PC `apps.rs`: Add installs both halves; switch off stops the sidecar and sends `HOST_STATE` off, no remove; Remove removes both halves; `reconcile()` becomes a sync diff (board-only: PC half from the registry by id and version, else keep board-only or remove; PC-only: board half; version skew: update the older side) applied only on the user's Sync click; `AppTurnOn` switches on and logs it; index pages read. Cargo tests per case
  - [ ] 4. Board UI: faded tile for an app off on the PC (tap opens "Turned off on host PC" + Turn on); faded tile with an SD badge for a missing card (tap: "Insert the SD card", probes); "Not on this PC" state; Home pages (swipe, page dots, only the shown page built), designed under DESIGN.md. Sim screens and goldens, copies in `docs/screens/board/`
  - [ ] 5. PC UI: "Sync with board" card on Apps (checkbox per difference, one Sync button); where each app lives ("SD card", "flash", "card missing"); progress for board installs over 128 KB, time warning over ~2 min
  - [ ] 6. On the board: installs with and without the card, card pulled with an app open, Turn on from the board, sync both ways, a 2 MB install timed over USB and Wi-Fi, heap with 32 apps (budget ≤ 2 KB more than v0.1.20)
  - [ ] 7. Docs (packages, spec, sidecars, README, mockups, board-info, decisions), all tests, release v0.1.21
- [ ] P52 OTA, two slots, signed firmware, v0.1.22 (design: `docs/plan-sd-ota.md`)
  - [ ] 1. Compare IDF v5.5.5's signed update option (`CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`, its ESP32 rev 3.1 scheme) with an own ECDSA P-256 check; record the choice (default: own check, no bootloader or eFuse change)
  - [ ] 2. `partitions.csv`: nvs 0x9000 24 KB, otadata 0xF000, phy_init 0x11000, coredump 0x12000 56 KB, ota_0 0x20000 and ota_1 0x1F0000 at 0x1D0000 each, apps 0x3C0000 256 KB; `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`; `bundle_firmware.py` and `fenix flash` write `ota_data_initial`. Check the USB update path writes bootloader and table, NVS kept, the Sync card offers flash apps back
  - [ ] 3. Signing key into `%USERPROFILE%\.fenixos\fw-signing.pem` (never in git), `gh secret set FENIX_FW_SIGNING_KEY`; `.fwp` format, `tools/sign_fw.py`, public key in the firmware, release workflow signs and publishes the `.fwp`, the PC app bundles it. Remind the user to back up the `.pem`
  - [ ] 4. Board `ota.cpp`: stage on SD (resumable), verify sha and signature, copy to the spare slot (4 KB buffer); no card: write the slot directly; mark valid after Home plus 30 s; rollback report. `FW_BEGIN`..`FW_RESULT` 0x50 to 0x54 (codecs, vectors, spec). Host test for the check
  - [ ] 5. Settings › About: version, "Update to vX" from an SD package (checked at boot too), "Go back to vY" (last two `.fwp` kept), progress screen. PC `update.rs`: OTA over the current link for firmware ≥ 0.1.22, USB flash otherwise, "Plug in USB once" for an old board on Wi-Fi. `fenix ota <file.fwp>`
  - [ ] 6. On the board: migration over USB, OTA over USB, Wi-Fi and SD package, cable pulled mid-transfer (resume), forced crash in the new image (rollback), downgrade refused outside the menu, heap at the peak (≤ 12 KB while updating)
  - [ ] 7. Docs (spec, board-info partition table and timings, toolchain key handling, README, decisions), all tests, release v0.1.22 (flashed over USB: the migration)
- [ ] P53 SD extras, v0.1.23 (design: `docs/plan-sd-ota.md`)
  - [ ] 1. `FILE_*` 0x60 to 0x67: list, read, delete under `/sd/fenixos`; chunked resumable write to `fonts/`, `data/`, `wallpaper/` only; codecs, vectors, spec; `fenix files ls|get|put|rm`
  - [ ] 2. Logs to `/sd/fenixos/logs/<boot>.txt` (512 B buffer, 10 kept, 256 KB each); core dump to the 56 KB partition (measure a dump; drop if it does not fit), copied to `/sd/fenixos/crash/` at boot; PC log sheet and `fenix logs --board` read them
  - [ ] 3. Screenshots from the PC only: Board page button and `fenix shot --sd` save `/sd/fenixos/shots/<time>.bmp` (LCD read in 16-row bands); PC lists and downloads
  - [ ] 4. Last bound values per app in `/sd/fenixos/state/<id>.bin` (on close, every 60 s open); with no PC the app opens on them, dimmed, "PC not connected"
  - [ ] 5. Settings backup `/sd/fenixos/backup/settings.bin` on every change (Wi-Fi password included, plaintext); restore offered at boot when settings are empty
  - [ ] 6. Status page served from `/sd/fenixos/web/` when present (built-in page as fallback)
  - [ ] 7. Home wallpaper from `/sd/fenixos/wallpaper/` drawn in rows, set from the PC; measure redraw time and burn-in shift first, drop it if Home slows
  - [ ] 8. Per-app data folder `/sd/fenixos/data/<id>/` through the SDK (`put_file`, `get_file`), removed with the app; template and SDK docs
  - [ ] 9. Heap per feature, docs, all tests, release v0.1.23
- [ ] P54 Fonts from the card, v0.1.24 (design: `docs/plan-sd-ota.md`; UI sounds dropped, user)
  - [ ] 1. Check Tiny TTF (`stb_truetype`) with Noto Sans CJK's CFF OTF; else a TrueType-outline CJK font (OFL). Measure flash, glyph cache RAM, draw time of a 20-character title; split per script if too slow
  - [ ] 2. `LV_USE_TINY_TTF`, `LV_TINY_TTF_FILE_SUPPORT`, LVGL file system driver on `/sd`; `tools/sim_config.py`; labels fall back to the CJK font for missing characters; no card or no font: as now
  - [ ] 3. PC app downloads the font (pinned URL and sha256) and installs it with `FILE_WRITE`; Settings › Storage shows it; licence in `tools/licenses.py` output
  - [ ] 4. Sim screen with CJK text, docs (packages fonts, board-info, README, Morpheus answer row 5), all tests, release v0.1.24
- [ ] P50-M App runtime for Morpheus, v0.1.25 (after P54 in the same run; user answers 2026-11-02: `view` and `show`, `secret`, two releases). Only the fenixOS features Morpheus asked for; the Morpheus app itself is not built here (user)
  - [ ] 1. Reproduce the `opened` burst: 50 `BIND_SET` right after `APP_OPENED`, count what the board applies. Fix: PC keeps the latest value per binding and paces the burst; board queue with compact entries (text in a small ring), deeper than 16
  - [ ] 2. `view` element (kind 9): int `bind` picks the one child shown (`LV_OBJ_FLAG_HIDDEN` on the rest); bindable `show` (bool) on every element; compiler, both readers, host tests, sim screen
  - [ ] 3. Bindable button text (string) and icon glyph (int index on `icon` and icon buttons); 14 glyphs added to the fill icon fonts (`play`, `pause`, `skip-forward`, `skip-back`, `speaker-high`, `speaker-x`, `microphone`, `microphone-slash`, `headphones`, `game-controller`, `music-notes`, `video`, `phone`, `sparkle`); flash measured
  - [ ] 4. Image binding: `image` element with `bind` and declared `w` × `h`, one per app, RGB565; `BIND_IMAGE` 0x32 chunked (spec, both codecs, vectors); buffer counted in the RAM estimate; pipe message `set_image`, SDK `set_image` (resize, convert) and `set_icon`
  - [ ] 5. Measure on the board: Morpheus-sized layout with 120×120 art, Wi-Fi on (PC over Wi-Fi and over USB) and radio off; lowest heap with `fenix bench` (no stream). Set the RAM estimate cap (~56 KB) and the art cap (96 or 120) from it
  - [ ] 6. `screen` permission: `tools/registry.py`, `templates/app/tools/app.py`, `app/src/lib/api.ts`, `docs/registry.md`, template `registry.md`
  - [ ] 7. Docs (packages, spec, sidecars, registry, template copies), template pin to v0.1.25, zip; tests; release v0.1.25; answer in `docs/requests/morpheus.md`
- [ ] P51-M Text and labels for Morpheus, v0.1.26 (after P50-M)
  - [ ] 1. Latin-1 Supplement (0xA0–0xFF) in the text fonts (`tools/gen_fonts.py`); flash measured
  - [ ] 2. SDK helper: transliterate to the board's glyph set, `?` for unknown
  - [ ] 3. Label `overflow` (`wrap`, `clip`, `ellipsis`, `scroll`) and `max_lines`
  - [ ] 4. `secret` setting type (user: yes): masked in App settings, DPAPI user scope in `config.json`, never logged, plain value to the sidecar; registry and template checks
  - [ ] 5. Docs (packages, registry, template copies, Morpheus answer), tests, release v0.1.26

- [ ] P36 Later
  - [ ] On-board Wi-Fi password entry (the scan list comes in P34; needs LVGL keyboard and textarea, which P31 turns off: more flash)
  - [x] Phone Web Bluetooth page: dropped, P38 removed Bluetooth (needs BLE on the board)
  - [x] OTA over Wi-Fi after the SD card: planned as P52 (two 1.81 MB slots, 256 KB flash app store)
  - [ ] With the SD card (user, 2026-11-01): CJK fonts and text beyond Latin-1 (fonts from SD planned in P54), app screens built only when shown (Morpheus requests)
  - Not planned: both radios at once (P29: ~40 KB free), LCD SPI at 80 MHz (garbles the panel, P10), fps caps for RAM (no gain), A2DP and HID (user)

## Decisions (current; superseded ones in the history file)

- 2026-10-31: **Builds may use all cores** (user, after gaming: "u can go back to using all cores threads and testing pc app normally grabbing focus"): `FENIX_CORES=16` with `tools/capped.ps1`, one build at a time; the PC app may be opened and focused for tests. Supersedes the 4-core limit the same evening.
- 2026-10-30: **Releases are the agent's job** (user: "handle all the versioning changes, release pushes urself"; 2026-10-29: "i permit u at all times"). Flow: bump VERSION, build locally (firmware and app), install the app (`/S`) and flash the board with `fenix flash`, then push the tag; GitHub builds at its own pace.
- 2026-10-29: **Versions:** ESP-IDF v5.5.5, LovyanGFX 1.2.35; CH340 driver updated to 4.0.2026.2 (3.3.2011.11 blue-screened the PC). See `docs/board-info.md` "Version status".
