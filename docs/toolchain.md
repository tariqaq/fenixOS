# Toolchain

The installed versions below still apply to fenixOS. The "Build commands" and "Debug knobs" sections are the FenixDeck PoC's (legacy) and are replaced in `docs/PLAN.md` P0.

Set up on 2026-10-28 (Windows 11).

| Tool | Version | Notes |
|---|---|---|
| ESP-IDF | **v5.5.5** | Installed by EIM CLI 0.20.1 (`winget install Espressif.EIM-CLI`) at `C:\Espressif`, target esp32 only |
| xtensa-esp-elf GCC | esp-14.2.0_20260121 | from IDF tools |
| CMake / Ninja | 3.30.2 / 1.12.1 | from IDF tools (not on global PATH) |
| LovyanGFX | **1.2.35** | git submodule `board/components/LovyanGFX`; it is not in the ESP component registry |
| Rust | 1.99.1 | rustup, MSVC target |
| Node / pnpm | 24.13.1 / 12.15.0 | pnpm via `npm i -g pnpm` |
| MSVC | VS 2022 Build Tools | used for the C++ protocol host test |
| esptool | 5.6.0 | pip (system Python 3.14), for reads/backups |

## Build commands

- Firmware: `pwsh -NoProfile -File board/idf.ps1 build` (also `flash`, `monitor`, etc.)
- C++ protocol test: `pwsh -NoProfile -File board/components/fenix_proto/test/run.ps1`
- Rust tests: `cargo test --workspace --exclude fenixdeck` (the app crate needs `app/dist`; build it with `pnpm build` first to include it)
- PC app: `cd app; pnpm install; pnpm tauri build`
- Metrics check: `python tools/validate_metrics.py 15`; footprint: `pwsh -NoProfile -File tools/measure_app.ps1 [-Tray]`

## Debug knobs

| Variable | Effect |
|---|---|
| `FENIX_REGEN=1` | `cargo test -p fenix-proto` rewrites `vectors/vectors.txt` (only after an intended wire change + `PROTO_VERSION` bump) |
| `FENIX_PROFILE=1` | `fenix-metrics` prints per-section sample timings to stderr |
| `FENIX_NO_NVML=1` | never load NVML (memory measurement / diagnostics) |

## Gotchas found

- EIM's `Microsoft.v5.5.5.PowerShell_profile.ps1` fails to parse in pwsh 7. `board/idf.ps1` sets the IDF env itself.
- `idf.py` refuses to run when `MSYSTEM` is set (launched from Git Bash). `board/idf.ps1` clears it.
- `vcvars64.bat` breaks on PATH entries with parentheses (`\Nmap was unexpected at this time`). The test script starts from a clean PATH.
- Clone with submodules: `git clone --recursive`, or `git submodule update --init`.
- **Windows serial: no reader thread on a cloned handle.** serialport-rs uses blocking I/O, and a read and a write on the same port serialize. A reader thread with a 20 ms timeout stalled every write and starved the link above ~4 Hz. `fenix-link` now runs one thread: batch write, then read with a 15 ms timeout (it returns as soon as bytes arrive).
- **WebView2 cost:** with default settings, WebView2 runs separate GPU and renderer processes, and CSS animations kept them busy. Fixes: lean `additionalBrowserArgs` (software compositing in-process, one renderer), no CSS transitions, no UI updates while the window is hidden, and the webview is destroyed when closed to the tray. Window open: 96–97 MB, ~0.17% CPU. (Corrected: an earlier "~200 MB" figure included another app's WebView2 processes.)
- **starship-battery** takes ~115 ms per query. Use `GetSystemPowerStatus` per sample and poll starship on a background thread for power (W) only.
- **LHM unreachable** makes each HTTP attempt wait for the timeout, so LHM is polled on its own thread with a 5 s back-off.
- `tauri icon` accepts the SVG at `app/src-tauri/icons/icon.svg`.
- **Never block Tauri `setup` or sync commands on UAC.** `ShellExecuteW("runas")` waits for the prompt. Called synchronously in `setup` (LHM autostart), it hung app startup and stacked UAC prompts. It now runs on a thread, skipped if LHM already runs; `launch_lhm` is an async command.
- **Config windows can race managed state:** the webview could call `get_settings` before `app.manage()`, which left the settings panel empty. The window is now created at the end of `setup`, and the UI retries its initial loads.
- **Measuring WebView2 apps:** other apps (Widgets, Teams...) run `msedgewebview2.exe` too. Count only processes with `--webview-exe-name=fenixdeck.exe` (see `tools/measure_app.ps1`).
- **Forced kills leave ghost tray icons.** Quit with `fenixdeck --quit`.
- **NVML in-process costs ~19 MB private.** Prefer LHM's sensors when LHM runs.
