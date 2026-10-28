# Toolchain

Tools installed on 2026-10-28 (Windows 11); libraries pinned for fenixOS on 2026-10-29 (`docs/PLAN.md` P0), checked against crates.io, npm and the ESP component registry.

## Tools

| Tool | Version | Notes |
|---|---|---|
| ESP-IDF | **v5.5.5** | Installed by EIM CLI 0.20.1 (`winget install Espressif.EIM-CLI`) at `C:\Espressif`, target esp32 only. CI uses the `espressif/idf:v5.5.5` image |
| xtensa-esp-elf GCC | esp-14.2.0_20260121 | from IDF tools |
| CMake / Ninja | 3.30.2 / 1.12.1 | from IDF tools (not on global PATH) |
| Rust | 1.99.1 | rustup, MSVC target |
| Node / pnpm | 24.13.1 / 12.15.0 | pnpm via `npm i -g pnpm` |
| MSVC | VS 2022 Build Tools | C++ protocol host test, board simulator |
| esptool | 5.6.0 | pip (system Python 3.14), for reads/backups |
| Python | 3.14 | `tools/*.py` |

## Pinned libraries

| Library | Version | Where | Why this one |
|---|---|---|---|
| LovyanGFX | **1.2.35** | git submodule `board/components/LovyanGFX` | Decided (keep current). Not in the ESP component registry |
| LVGL | **9.5.0** | `board/main/idf_component.yml` | Newest 9.x with months of field use (9.6.0 is from 2026-09-16) |
| joltwallet/littlefs | 1.23.1 | `board/main/idf_component.yml` | Latest |
| tauri / tauri-build | 2.12.5 / 2.7.4 | `app/src-tauri/Cargo.toml` | Latest 2.x |
| tauri-plugin-single-instance / autostart / updater / process | 2.5.5 / 2.7.0 / 2.14.1 / 2.4.0 | same | Latest |
| espflash (library) | 4.6.0 | PC engine | Latest |
| serialport | 4.10.1 | `crates/fenix-link` | Latest |
| svelte / vite / @sveltejs/vite-plugin-svelte | 5.57.3 / 8.3.5 / 7.3.1 | `app/package.json` | Latest; same set as the PoC |
| @tauri-apps/cli / api | 2.12.4 | same | Latest |
| typescript | 5.9.x | same | TypeScript 7 (native port) skipped: Svelte tooling support unproven |
| three | 0.186.1 | same (first-run scene) | Latest |
| @fontsource-variable/inter | 5.3.0 | same | Inter, bundled locally |
| @phosphor-icons/core | 2.1.1 | same + board icon font | Matches `DESIGN.md` |
| lv_font_conv | 1.5.3 | `tools/gen_fonts` (npx) | Latest |

## Build commands

- Firmware: `pwsh -NoProfile -File board/idf.ps1 build` (also `flash`, `monitor`, `size`). First time: `idf.ps1 set-target esp32`.
- Rust tests: `cargo test --workspace`
- C++ protocol test (MSVC): `pwsh -NoProfile -File board/components/fenix_proto/test/run.ps1` (checks `proto/vectors.txt`)
- Spikes: `pwsh -NoProfile -File board/idf.ps1 -C spikes/<name> -B spikes/<name>/build -p COM9 -b 460800 build flash`, then `python tools/serial_read.py 30 --reset`
- Board fonts: `python tools/gen_fonts.py` (Inter + Phosphor via `lv_font_conv`)
- Version check / sync: `python tools/version.py --check` / `python tools/version.py`
- Design tokens: `python tools/gen_tokens.py` (writes `board/components/fenix_ui/include/fenix_tokens.h` and `app/src/lib/tokens.css`; `--check` in CI)

More commands are added here as each phase lands.

## Debug knobs

| Variable | Effect |
|---|---|
| `FENIX_REGEN=1` | `cargo test -p fenix-proto` rewrites the golden vectors (only after an intended wire change + proto version bump) |

## Gotchas found

From the FenixDeck PoC unless dated; still valid for fenixOS.


- EIM's `Microsoft.v5.5.5.PowerShell_profile.ps1` fails to parse in pwsh 7. `board/idf.ps1` sets the IDF env itself.
- `idf.py` refuses to run when `MSYSTEM` is set (launched from Git Bash). `board/idf.ps1` clears it.
- `vcvars64.bat` breaks on PATH entries with parentheses (`\Nmap was unexpected at this time`). The test script starts from a clean PATH.
- Clone with submodules: `git clone --recursive`, or `git submodule update --init`.
- **Windows serial: no reader thread on a cloned handle.** serialport-rs uses blocking I/O, and a read and a write on the same port serialize. A reader thread with a 20 ms timeout stalled every write and starved the link above ~4 Hz. `fenix-link` now runs one thread: batch write, then read with a 15 ms timeout (it returns as soon as bytes arrive).
- **WebView2 cost:** with default settings, WebView2 runs separate GPU and renderer processes, and CSS animations kept them busy. Fixes: lean `additionalBrowserArgs` (software compositing in-process, one renderer), no CSS transitions, no UI updates while the window is hidden, and the webview is destroyed when closed to the tray. Window open: 96–97 MB, ~0.17% CPU. (Corrected: an earlier "~200 MB" figure included another app's WebView2 processes.)
- `tauri icon` accepts the SVG at `app/src-tauri/icons/icon.svg`.
- **Never block Tauri `setup` or sync commands on UAC.** `ShellExecuteW("runas")` waits for the prompt. Called synchronously in `setup` (LHM autostart), it hung app startup and stacked UAC prompts. It now runs on a thread, skipped if LHM already runs; `launch_lhm` is an async command.
- **Config windows can race managed state:** the webview could call `get_settings` before `app.manage()`, which left the settings panel empty. The window is now created at the end of `setup`, and the UI retries its initial loads.
- **Measuring WebView2 apps:** other apps (Widgets, Teams...) run `msedgewebview2.exe` too. Count only processes with `--webview-exe-name=fenixdeck.exe` (see `tools/measure_app.ps1`).
- **Forced kills leave ghost tray icons.** Quit with `fenixdeck --quit`.
