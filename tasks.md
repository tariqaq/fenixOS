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
- [~] Commit + push
- [ ] Ask decision questions round by round (picker); record each answer in `ideas.md` + `docs/decisions.md`
- [ ] Research where needed (web), cross-check sources, document findings
- [ ] Wireframes / mock-up slides (board screens + PC app screens, per phase), published for review
- [ ] Iterate with user
- [ ] Detailed, scoped build plan (`docs/PLAN.md` for fenixOS; the FenixDeck plan moves to `docs/legacy/`)

## Decisions

- 2026-10-29: **Keep current versions.** ESP-IDF v5.5.5, LovyanGFX 1.2.35, CH340 PC driver 3.3.2011.11. No upgrade to IDF v5.5.6 / v6.x or the 2024 CH340 driver for now (see `docs/board-info.md` "Version status").

## Open (needs user input or flashing)

- [ ] Touch corner coverage of the default raw range (tap the 4 corners while logging)
- [ ] V1.4 RGB LED and LDR (IO34) circuits: resistor values differ from the schematic; test in firmware or measure
- [ ] Next: user decides the fenixOS direction and code layout
