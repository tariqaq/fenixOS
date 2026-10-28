# fenixOS tasks

New project: an "OS of sorts" for the ESP32-2432S028R (CYD). Hand-over from the FenixDeck PoC is in `docs/POC-WRITEUP.md`. No code yet: the user decides what to build.

Legend: `[x]` done, `[~]` in progress, `[!]` blocked, `[ ]` to do.

## Phase 0: repo setup (2026-10-29)

- [x] Read `AGENTS.md`, all docs, all `pics/`, vendor schematics, dimensions, vendor LovyanGFX demo
- [x] Add `.gitignore` for the local-only vendor files listed in `docs/vendor/README.md`
- [x] Add this `tasks.md`
- [ ] `git init` on `main`, first commit
- [ ] Create private GitHub repo `fenixOS`, push, confirm local = `origin/main`

## Phase 1: verify new findings (2026-10-29)

Findings from the read-through, to check (read-only on the board: no flashing without user go-ahead) and then document.

- [ ] Schematic file names: `ESP32-2432S028-MCU.jpg` holds the power/USB/ADC/LED/SD/flash sheet; `ESP32-2432S028-LCM.jpg` holds the ESP32/touch/LCM/audio sheet
- [ ] Vendor LovyanGFX demo touch config: `x_min/x_max` 0–239, `y_min/y_max` 0–319, `bus_shared = true` vs PoC raw range 300–3900 / 200–3900 on a separate bus
- [ ] CN1: IO27 pull-up R18 10 kΩ (schematic V0.1); check against V1.4 PCB photos
- [ ] Board on COM9: read chip info with esptool (read-only) and confirm it matches `docs/board-info.md`
- [ ] Identify the firmware now on the board; back up its flash
- [ ] Update docs with the results (rule 5), commit, push
