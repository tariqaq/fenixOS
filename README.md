# fenixOS

A small operating system for the ESP32-2432S028R ("Cheap Yellow Display", 2.8" 320×240 touch screen), plus its companion app for Windows. Both share one version number.

- **Board:** C++17 on ESP-IDF v5.5.5, LVGL 9 + LovyanGFX. Boot screen, a home screen with time and date, three system apps (Settings, Session, Stream), and apps installed from the PC.
- **PC app:** Rust + Tauri 2 + Svelte 5. Lives in the tray, finds the board over USB, installs and switches apps, mirrors the board's screen, keeps the board's firmware and time in step.
- **Design:** Apple-style dark theme (`DESIGN.md`), Inter and Phosphor icons.

Status: **v0.1 in development.** Plan: `docs/PLAN.md`; progress: `tasks.md`.

## Repo layout

| Path | What |
|---|---|
| `VERSION` | The only version source (lockstep SemVer) |
| `board/` | ESP-IDF firmware project |
| `app/` | Tauri + Svelte PC app |
| `crates/` | Shared Rust crates (protocol, link, packages, SDK, dev CLI) |
| `proto/` | Link protocol spec and test vectors |
| `registry/` | Manifests of installable apps (each app lives in its own repo) |
| `design/` | Design tokens and font sources |
| `tools/` | Generators and helper scripts |
| `docs/` | Plan, decisions, hardware facts, toolchain; `docs/legacy/` holds the FenixDeck PoC docs |
| `pics/` | Board photos and their analysis |

## Build

See `docs/toolchain.md` for versions and commands. Clone with submodules: `git clone --recursive https://github.com/tariqaq/fenixOS`.

## Licence

MIT (`LICENSE`). Inter is under the SIL Open Font Licence; Phosphor icons are MIT.
