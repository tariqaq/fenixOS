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
- P21 Burn-in protection (one board check left, see Open)
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
- P34 Wi-Fi: PC link, internet, phone web page (one check left, see Open)
- P35 Design-language fixes
- P37 Known limits, open checks, docs pass (one check left, see Open)
- P38 Remove Bluetooth, BLE included
- P39 App packages from flash in pieces, not whole in RAM
- P40 Link buffer trims, measured

Releases: v0.1.0 (P10) to v0.1.13 (P42); tags on GitHub.

## Open (needs the user)

- [~] P21 step 5: the user checks pixel shift, screen rest and the protection lock on the board (all built and flashed)
- [~] P34 step 5 / P37 step 6: the PC app's Wi-Fi fallback with the board's USB unplugged (needs a second power source; the fixed `tcp:fenixos.local` port is tested)

## Now

- [x] P41 Docs audit 1 (user 2026-10-31, overnight goal): every `*.md` and reference file matches the code (rule 7), no stale numbers, versions, removed features or done TODOs, token-optimized. Method per file: read whole, check each fact that names code (paths, constants, commands, sizes) against the code, fix, cut repetition. History docs stay frozen (header note only). Decisions in `docs/decisions.md` "2026-11-01: overnight run"
  - [x] 1. Done: 88.7 KB to 6.7 KB. `tasks.md`: done phases P0-P40 (verbatim) to `docs/legacy/tasks-history.md`; here one line per done phase, open items kept whole (P21, P34 step 5 and 9, P37 steps 5 and 6), Decisions without the superseded entries
  - [x] 2. Done: README (Wi-Fi in the board line, images in the element list, "CH340 note above"), AGENTS (cores line, history pointer), legacy README (history file). CLAUDE.md is the one-line import. `README.md`, `AGENTS.md`, `CLAUDE.md`
  - [x] 3. Done: board type table matches `tools/gen_fonts.py` weights; icon sizes and the full icon map; unused `bluetooth` glyph dropped from the icon fonts (P38 leftover; firmware build in P42). `DESIGN.md` (tokens against `design/tokens.json`, `tools/gen_tokens.py` output, board `fenix_ui` and app CSS)
  - [x] 4. Done: every type ID matches `msg.rs` and `fenix_proto.h` (script check); reserved Bluetooth values right. `proto/spec.md` (message table against `fenix_proto.h` and `msg.rs`)
  - [x] 5. Done: marked as the v0.1.0 plan as built; Wi-Fi, NTP, Bluetooth, SNTP, element kinds, Wi-Fi fallback corrected. `docs/PLAN.md` (status lines, partitions, limits, task table against code)
  - [x] 6. Done: P30/P33 Bluetooth rows marked superseded; the overnight "keep Bluetooth glyph" row corrected (not app API: dropped); icon weights and bundled firmware size corrected. `docs/decisions.md` (Corrected notes still true; superseded rows marked)
  - [x] 7. Done: "Current numbers (v0.1.12)" table at the top; the rest is a dated log (history). `docs/board-info.md` (heap and image numbers: latest first, old ones marked history)
  - [x] 8. Done: fonts, icon sizes, limits, kinds, keys, registry rules (name 24, description 100, permissions, allowed URL), empty index all match the code. `docs/packages.md`, `docs/registry.md`, `docs/sidecars.md` (against `crates/fenix-pkg`, `tools/registry.py`, `app/src-tauri`)
  - [x] 9. Done: mdns pin, CLI commands and `tcp:` port, `sim_config.py`, cores line; tooling.md header and DESIGN.md line. `docs/toolchain.md`, `docs/tooling.md` (commands, versions, scripts that exist)
  - [x] 10. Done: 25 artboard files match the table; Settings row names Wireless. `docs/mockups.md`, `design/` references (artboard list against `design/mockups` and `canvas.json`)
  - [x] 11. Done: Wi-Fi/NTP/Bluetooth "later" lines corrected; firmware README named a `flash.rs` that never existed in the app (flashing is `crates/fenix-flash`); ANALYSIS board state. `ideas.md`, `pics/ANALYSIS.md`, `app/src-tauri/resources/firmware/README.md`
  - [x] 12. Done: "History, frozen" line on POC-WRITEUP and the three legacy docs. History docs (`docs/POC-WRITEUP.md`, `docs/legacy/*`): one-line "history, not current" header if missing; no rewrite
  - [x] 13. Done: CLI usage lists every command; tools/*.py docstrings match their flags. Reference text outside `*.md`: CLI usage/help (`fenix-cli`), `tools/*.py` help strings, `board/sim/screens.txt` comments
  - [x] 14. Done: version, tokens, registry, licenses, sim config all ok; CI green from 90e0b03. Checks (`version.py`, `gen_tokens.py`, `registry.py`, `licenses.py`, `sim_config.py` --check), commit, push
- [x] P42 File-by-file code audit (user: logic errors, design errors, inefficient or tangled code, stale code; not a security review; anti-cheat-sensitive behaviour; PC app page by page, board screen by screen; QoL; design language per `DESIGN.md`, hallmark locked-system rule: edits stay in the project's own system). Method: read each file whole, note findings in this list, fix the clear ones in place (small commits per group), log every choice made for the user in `docs/decisions.md` "2026-11-01: overnight run" with the options not taken. Bigger redesigns are proposed, not built
  - [x] A. Done: no hooks, injection, process lists, memory reads, raw input or own drivers. Fixed: mDNS lookups only for a board last seen in Wi-Fi mode (`Config.board_wifi`, `Link::set_net`); non-fenixOS CH340 retried every 10 s after 2 fails (was every 1 s, 2.5 s of 2 ms polls each); tray says "Wi-Fi" not `tcp:fenixos.local`; misplaced `mirror_set` doc comment. README "What fenixOS does on your PC". Anti-cheat review of the PC side (what runs on the user's PC while games run): `app/src-tauri/src/*.rs` (main, engine, tray, sidecar, update, wifi, weather, welcome, logs, config, apps, registry), `crates/fenix-link` (port polling), `crates/fenix-flash`, CPU cap job object. Check: process or window enumeration, global hooks, DLL injection, drivers, elevation, raw input, memory reads of other processes, polling rates, network endpoints. Write findings and verdict to `docs/decisions.md` and a short "What fenixOS does on your PC" section in README
  - [x] B. Done: engine files read whole (main, engine, apps, config, sidecar, update, wifi, weather, logs, tray, welcome, registry, fenix-link, fenix-flash, fenix-sdk wire); proto, pkg and cli covered by clippy (2 nits fixed) and their tests, not reread line by line. No logic errors found beyond A. Known limit kept: the PC Wi-Fi list parses English `netsh` output. PC engine and crates, file by file: `engine.rs`, `apps.rs`, `sidecar.rs`, `registry.rs`, `main.rs`, `update.rs`, `wifi.rs`, `weather.rs`, `logs.rs`, `config.rs`, `tray.rs`, `welcome.rs`; `fenix-link`, `fenix-proto` (lib, codec, msg), `fenix-pkg` (lib, main), `fenix-sdk`, `fenix-flash`, `fenix-cli`
  - [x] C. PC UI, page by page (run the app, screenshot each page and state): `App.svelte`, `Sidebar`, `TitleBar`, pages Apps, AddApp, AppSettings, Board, TaskManager, Settings, Connect; sheets and dialogs (Update, Logs, Licenses, Permission); `api.ts`, `motion.ts`, `icons.ts`, `ootb/*`. Check against DESIGN.md tokens, type, motion; QoL; Settings organisation. Done: literal colours to tokens, Enter joins Wi-Fi, failed add retryable, live slider value, scroll-to-top on page change, meter transform; Settings order kept (decisions.md)
  - [x] D. Board firmware, file by file: `main.cpp`, `fenix_hal`, `fenix_ui` (ui_core, widgets, nav), `fenix_sys` (link, net, radio, web, store, stream, sysinfo, settings, clock), `fenix_apps` (shell, home, settings_ui, session, stream_ui, app_pkg, led), `fenix_pkg`, `fenix_proto`. Done: colour tokens, Wi-Fi join/reconnect race (own ASSOC_LEAVE skipped, GOT_IP stops retry), About heap total, time-source texts, stale comments (bands 40 lines, LED S6, stream TX). Firmware builds; flash and on-board check in G
  - [x] E. Board screen by screen (sim shots and the real board): boot, Home tiles and dock, Settings and each sub-page, Session, Stream, an app (sample), host states. QoL and Settings organisation. Sim: 25 screens rendered and reviewed; goldens updated (about, host: P42 D texts); `docs/screens/board/` replaced by the sim set (old captures were v0.1.0). One `sim_shots.py` run hung after `app_portrait` with fenix_sim at full CPU; every rerun (bisected and full) finished in 0.3 s: not reproduced, watch it. Real board (v0.1.13): Settings, Wireless and the network list checked by `fenix shot`; tapping the saved network reconnects in ~3 s and stays connected (P42 D fix)
  - [x] F. Sim, tools, CI: `board/sim/*`, `tools/*.py|ps1|sh`, `.github/workflows/*`, spikes (keep or drop). Read all; nothing stale beyond the docs fixed in E. Spikes (`board/spikes/s1`, `s3`) and their tools kept: documented in board-info and toolchain
  - [x] G. Build and test everything once (cargo test, svelte-check, app build, firmware, sim shots, host tests), flash, install, release v0.1.13. Done: cargo test (12 pass), svelte-check 0/0, sim 25/25 after update, firmware and signed app v0.1.13 built, installed (/S), flashed with `fenix flash`, tagged
- [ ] P43 Docs audit 2: the same pass again after P42's changes
- [ ] P44 App repo template zip in the repo root (user: copy into a new app repo, open Claude, build a fenixOS app in the design language, installable by the PC app from the registry). Contents planned here before writing: AGENTS/CLAUDE instructions, package format and manifest, board UI rules and tokens, PC half rules, registry steps, a sample app skeleton, build and test commands
- [ ] Overnight decisions log: `docs/decisions.md`, section "Overnight 2026-11-01" (each choice made for the user and the options not taken)

- [ ] P36 Later
  - [ ] On-board Wi-Fi password entry (the scan list comes in P34; needs LVGL keyboard and textarea, which P31 turns off: more flash)
  - [x] Phone Web Bluetooth page: dropped, P38 removed Bluetooth (needs BLE on the board)
  - [ ] OTA over Wi-Fi after the SD card (user; 2026-10-31: not before the SD card, even if P38 makes two slots fit the 4 MB flash). A/B slots need ~2 x 1.6 MB; the 4 MB flash fits that only with the apps partition (1.94 MB) moved to the SD card
  - Not planned: both radios at once (P29: ~40 KB free), LCD SPI at 80 MHz (garbles the panel, P10), fps caps for RAM (no gain), A2DP and HID (user)

## Decisions (current; superseded ones in the history file)

- 2026-10-31: **Builds may use all cores** (user: "u can run builds using all available cores and threads"): `FENIX_CORES=16` with `tools/capped.ps1`, one build at a time, until the user says otherwise.
- 2026-10-30: **Releases are the agent's job** (user: "handle all the versioning changes, release pushes urself"; 2026-10-29: "i permit u at all times"). Flow: bump VERSION, build locally (firmware and app), install the app (`/S`) and flash the board with `fenix flash`, then push the tag; GitHub builds at its own pace.
- 2026-10-29: **Versions:** ESP-IDF v5.5.5, LovyanGFX 1.2.35; CH340 driver updated to 4.0.2026.2 (3.3.2011.11 blue-screened the PC). See `docs/board-info.md` "Version status".
