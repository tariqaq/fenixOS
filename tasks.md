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

Releases: v0.1.0 (P10) to v0.1.12 (P39, P40); tags on GitHub.

## Open (needs the user)

- [~] P21 step 5: the user checks pixel shift, screen rest and the protection lock on the board (all built and flashed)
- [~] P34 step 5 / P37 step 6: the PC app's Wi-Fi fallback with the board's USB unplugged (needs a second power source; the fixed `tcp:fenixos.local` port is tested)

## Now

- [~] P41 Docs audit 1 (user 2026-10-31, overnight goal): every `*.md` and reference file matches the code (rule 7), no stale numbers, versions, removed features or done TODOs, token-optimized. Method per file: read whole, check each fact that names code (paths, constants, commands, sizes) against the code, fix, cut repetition. History docs stay frozen (header note only). Decisions in `docs/decisions.md` "2026-11-01: overnight run"
  - [x] 1. Done: 88.7 KB to 6.7 KB. `tasks.md`: done phases P0-P40 (verbatim) to `docs/legacy/tasks-history.md`; here one line per done phase, open items kept whole (P21, P34 step 5 and 9, P37 steps 5 and 6), Decisions without the superseded entries
  - [x] 2. Done: README (Wi-Fi in the board line, images in the element list, "CH340 note above"), AGENTS (cores line, history pointer), legacy README (history file). CLAUDE.md is the one-line import. `README.md`, `AGENTS.md`, `CLAUDE.md`
  - [x] 3. Done: board type table matches `tools/gen_fonts.py` weights; icon sizes and the full icon map; unused `bluetooth` glyph dropped from the icon fonts (P38 leftover; firmware build in P42). `DESIGN.md` (tokens against `design/tokens.json`, `tools/gen_tokens.py` output, board `fenix_ui` and app CSS)
  - [x] 4. Done: every type ID matches `msg.rs` and `fenix_proto.h` (script check); reserved Bluetooth values right. `proto/spec.md` (message table against `fenix_proto.h` and `msg.rs`)
  - [x] 5. Done: marked as the v0.1.0 plan as built; Wi-Fi, NTP, Bluetooth, SNTP, element kinds, Wi-Fi fallback corrected. `docs/PLAN.md` (status lines, partitions, limits, task table against code)
  - [x] 6. Done: P30/P33 Bluetooth rows marked superseded; the overnight "keep Bluetooth glyph" row corrected (not app API: dropped); icon weights and bundled firmware size corrected. `docs/decisions.md` (Corrected notes still true; superseded rows marked)
  - [x] 7. Done: "Current numbers (v0.1.12)" table at the top; the rest is a dated log (history). `docs/board-info.md` (heap and image numbers: latest first, old ones marked history)
  - [x] 8. Done: fonts, icon sizes, limits, kinds, keys, registry rules (name 24, description 100, permissions, allowed URL), empty index all match the code. `docs/packages.md`, `docs/registry.md`, `docs/sidecars.md` (against `crates/fenix-pkg`, `tools/registry.py`, `app/src-tauri`)
  - [x] 9. Done: mdns pin, CLI commands and `tcp:` port, `sim_config.py`, cores line; tooling.md header and DESIGN.md line. `docs/toolchain.md`, `docs/tooling.md` (commands, versions, scripts that exist)
  - [ ] 10. `docs/mockups.md`, `design/` references (artboard list against `design/mockups` and `canvas.json`)
  - [ ] 11. `ideas.md`, `pics/ANALYSIS.md`, `app/src-tauri/resources/firmware/README.md`
  - [ ] 12. History docs (`docs/POC-WRITEUP.md`, `docs/legacy/*`): one-line "history, not current" header if missing; no rewrite
  - [ ] 13. Reference text outside `*.md`: CLI usage/help (`fenix-cli`), `tools/*.py` help strings, `board/sim/screens.txt` comments
  - [ ] 14. Checks (`version.py`, `gen_tokens.py`, `registry.py`, `licenses.py`, `sim_config.py` --check), commit, push
- [ ] P42 File-by-file code audit (user: logic errors, design errors, inefficient or tangled code, stale code; not a security review). Detailed per-file checklist written here before starting. Includes
  - [ ] PC to board traffic that could look odd to anti-cheat (Vanguard and similar): what the app does at runtime (serial port polling, process and hardware queries, LibreHardwareMonitor sidecar, sockets, injected or elevated anything, global hooks); findings and changes in `docs/decisions.md`
  - [ ] PC app page by page (run it, screenshots), board screen by screen (sim and the board): design language (DESIGN.md, hallmark skill), simple QoL, layout of the app and of board Settings
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
