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
- P45 Close the gaps in P42 and P43 (every file read, docs pass 2, Hallmark pass, sim hang)
- P46 Wi-Fi link on a powerbank, stream rate per link (10 fps USB, 25 Wi-Fi)
- P47 Connect screen: the Wi-Fi run in 3D
- P48 Wi-Fi link drops during a busy stream
- P49 Template zip check
- SD card checks (spike, run 2: SD Association layout, touch on software SPI)
- P50 SD card base; release v0.1.20
- P51 Apps on the SD card, on/off, Sync with board; release v0.1.21
- P52 OTA with two slots, signed firmware; v0.1.22 (its release workflow failed, P53 step 1: no GitHub release for v0.1.22)
- P53 SD extras (files, logs, crash records, screenshots, last values, settings backup, web page, app data); release v0.1.23
- P54 Fonts from the card (CJK bitmap font); release v0.1.24
- P50-M App runtime for Morpheus (views, `show`, bindable text and icons, bound image, `screen` permission); release v0.1.25
- P51-M Text and labels for Morpheus (Latin-1, `text::fit`, label overflow, `secret` settings); release v0.1.26
- P55 Full audit (`docs/audit-2026-11.md`); release v0.1.27

Releases: v0.1.0 (P10) to v0.1.27 (P55); tags on GitHub (v0.1.22 has a tag but no release). Detail of P45 to P55: the history file.

## Open (needs the user)

Tests and checks the overnight run could not do (2026-11-02); none blocks use:

- [ ] Card pulled, or no card, with an SD app open (P51; the simulator covers both: `home_card_out`, `app_card_missing`)
- [ ] Board Settings › About "Update to vX" from a `.fwp` copied to the card's root on a PC (P52; the PC app can write only `fonts/ data/ web/`)
- [ ] Power cut during a firmware copy (P52; the old slot stays the boot slot until the copy is checked)
- [ ] Status light during a firmware update: dim blue, and dim red after a failed one (P55 B4; built, not seen on the board yet)
- [ ] Apps › Sync with board clicked through in the installed app (P51; cargo tests cover the diff), and a `secret` setting typed, saved and cleared (P51-M)
- [ ] App RAM with the radio off (P50-M step 5 measured Wi-Fi mode only; USB-only has ~100 KB more)
- [ ] Copy `%USERPROFILE%\Documents\fenixOS-keys\` (signing and updater keys) to offline storage such as a USB stick; never a cloud folder

## Now

- Overnight run (user /goal, 2026-11-02 night): P51 to P55 done (detail in the history file), then P56 below. Builds may use all cores (`FENIX_CORES=16`). Choices made for the user: `docs/decisions.md` "2026-11-02: overnight run"
- [x] P56 Docs sweep: every current `.md` (root, `docs/` except `legacy/`, `vendor/`, `tempcrashlogs/`; `proto/`, `board/`, `app/`, `tools/`, `registry/`, `templates/`) checked against the code; stale facts fixed with Corrected notes; tasks.md Done list trimmed into the history file. History docs (`docs/POC-WRITEUP.md` if marked, `docs/legacy/`) stay frozen
  - [x] 1. Scripted checks over every current `.md`: paths that do not exist, message ids in `proto/spec.md` against `fenix-proto`, constants named in docs (sizes, baud, caps) against the code, old version strings
    - Done: spec ids match Rust and C++; no missing path (the few hits are marked history or card paths); stale: PLAN status v0.1.19, board-info current state v0.1.22 and v0.1.12 numbers, README "next" pointer (fixed in steps 2, 3)
  - [x] 2. Root and small docs read whole: `README.md`, `DESIGN.md`, `AGENTS.md`, `ideas.md`, `pics/ANALYSIS.md`, `app/src-tauri/resources/firmware/README.md`, `third_party/serialport/NOTES.md` and `PATCH.md`
    - Done: README status line and a missing comma, DESIGN.md built-in icons (34, not 20), ideas.md on/off and SD flow (Corrected P51), pics/ANALYSIS.md touch on software SPI; the rest current
  - [x] 3. `docs/` read whole: `PLAN.md`, `board-info.md`, `decisions.md`, `packages.md`, `ota.md`, `plan-sd-ota.md`, `sd-card.md`, `fonts.md`, `sidecars.md`, `registry.md`, `toolchain.md`, `tooling.md`, `mockups.md`, `requests/morpheus.md`, `audit-2026-11.md`
    - Done: PLAN (status v0.1.27, P50 to P55 in the follow-ups, Corrected notes: touch on software SPI, app open, baud 921600, BIND_IMAGE, package caps, switch off and Sync, heap), board-info current state and numbers (v0.1.27: 1,290,192 B image, 52.9 KB free on Home in Wi-Fi mode), decisions (Corrected notes on reconcile and motion, rows for P50-M, P51-M and the P55 kept findings), packages (offline values; template copy too), ota (v0.1.27 size, shared sender), plan-sd-ota (status all done, three kept packages, P50-M status), toolchain (CLI commands, sim verbs, 42 goldens, template pins, rustfmt gotcha), mockups (screens added without artboards), Morpheus (v0.1.27 note). Found: `docs/screens/board/` 5 screens behind the goldens; synced, and `tools/sim_shots.py --update` now writes both (audit T1). sd-card, fonts, sidecars, registry, tooling current
  - [x] 4. `proto/spec.md` and `templates/app` docs (AGENTS, README, `docs/fenixos/*`) read whole against the code; zip regenerated if the template changes
    - Done: message ids and bodies match both codecs; spec session step 2 said `APP_LIST_REQ` and an automatic reconcile (now `APP_INDEX` and Sync). Template: offline values and card-out states in app-format, `screen` permission in pc-half, zip regenerated (corrected: the step 3 commit b800e3e changed app-format.md without the zip, so CI's template check failed on b800e3e and 0c1f9bf; green again from 08e8b08). `apps.rs` image pacing comment said 120 x 120 (cap 96)
  - [x] 5. `tasks.md`: P45 to P55 detail moved verbatim into `docs/legacy/tasks-history.md`, one line each left under Done; Releases line; Open list (tests that need the user)
    - Done: P45 to P55 blocks moved verbatim to the history file (header updated), one line each under Done, Releases line to v0.1.27, Open list of the hardware and hand checks left, P36 Later items done marked; pointers in plan-sd-ota, decisions and Morpheus doc now name the history file
  - [x] 6. All checks, commit, push
    - Done (2026-11-02): version, tokens, registry, template, licenses checks ok; `cargo test --workspace` 0 failed; `sim_shots.py --no-build --update` rewrote identical goldens and the docs copy, then 42 screens match

- [ ] P36 Later
  - [ ] On-board Wi-Fi password entry (the scan list comes in P34; needs LVGL keyboard and textarea, which P31 turns off: more flash)
  - [x] Phone Web Bluetooth page: dropped, P38 removed Bluetooth (needs BLE on the board)
  - [x] OTA over Wi-Fi after the SD card: done in P52 (two 1.81 MB slots, 256 KB flash app store)
  - [x] With the SD card (user, 2026-11-01): CJK fonts (P54), text beyond Latin-1 (P51-M), app screens built only when shown (P50-M)
  - Not planned: both radios at once (P29: ~40 KB free), LCD SPI at 80 MHz (garbles the panel, P10), fps caps for RAM (no gain), A2DP and HID (user)

## Decisions (current; superseded ones in the history file)

- 2026-11-02: **No OneDrive, no cloud folders** (user: "DONT WANT ONEDRIVE AGAIN"). Never check, use, enable or suggest OneDrive or any cloud sync for backups or files. Backups stay local (`%USERPROFILE%\Documents\fenixOS-keys\`, a USB stick).

- 2026-10-31: **Builds may use all cores** (user, after gaming: "u can go back to using all cores threads and testing pc app normally grabbing focus"): `FENIX_CORES=16` with `tools/capped.ps1`, one build at a time; the PC app may be opened and focused for tests. Supersedes the 4-core limit the same evening.
- 2026-10-30: **Releases are the agent's job** (user: "handle all the versioning changes, release pushes urself"; 2026-10-29: "i permit u at all times"). Flow: bump VERSION, build locally (firmware and app), install the app (`/S`) and flash the board with `fenix flash`, then push the tag; GitHub builds at its own pace.
- 2026-10-29: **Versions:** ESP-IDF v5.5.5, LovyanGFX 1.2.35; CH340 driver updated to 4.0.2026.2 (3.3.2011.11 blue-screened the PC). See `docs/board-info.md` "Version status".
