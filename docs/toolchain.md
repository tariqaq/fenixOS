# Toolchain

Tools installed on 2026-10-28 (Windows 11); libraries pinned for fenixOS on 2026-10-29 (`docs/PLAN.md` P0), checked against crates.io, npm and the ESP component registry.

## Tools

| Tool | Version | Notes |
|---|---|---|
| ESP-IDF | **v5.5.5** | Installed by EIM CLI 0.20.1 (`winget install Espressif.EIM-CLI`) at `C:\Espressif`, target esp32 only. CI uses the `espressif/idf:v5.5.5` image |
| xtensa-esp-elf GCC | esp-14.2.0_20260121 | from IDF tools |
| CMake / Ninja | 3.30.2 / 1.12.1 | from IDF tools (not on global PATH); the board simulator uses the copies that come with VS 2022 |
| Rust | 1.99.1 | rustup, MSVC target |
| Node / pnpm | 24.13.1 / 12.15.0 | pnpm via `npm i -g pnpm` |
| MSVC | VS 2022 Build Tools | C++ protocol host test, board simulator |
| esptool | 5.6.0 | pip (system Python 3.14). Not used on the board any more: its read timeouts take the CH340 driver's crash path (P23). `fenix flash` / `write` / `read-flash` / `erase` replace it |
| Python | 3.14 | `tools/*.py` |

## Pinned libraries

| Library | Version | Where | Why this one |
|---|---|---|---|
| LovyanGFX | **1.2.35** | git submodule `board/components/LovyanGFX` | Decided (keep current). Not in the ESP component registry |
| LVGL | **9.5.0** | `board/main/idf_component.yml` | Newest 9.x with months of field use (9.6.0 is from 2026-09-16) |
| joltwallet/littlefs | 1.23.1 | `board/main/idf_component.yml` | Latest |
| tauri / tauri-build | 2.12.5 / 2.7.4 | `app/src-tauri/Cargo.toml` | Latest 2.x |
| tauri-plugin-single-instance / autostart / updater | 2.5.5 / 2.7.0 / 2.14.1 | same | Latest. No process plugin: the updater restarts through `AppHandle::restart` (Corrected 2026-10-29: the process plugin was listed but is not needed) |
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

- On this laptop, run builds through `pwsh -NoProfile -File tools/capped.ps1 <command>` (user request, P12): at most 4 cores' worth of CPU at normal priority for the command and every process it starts, and `CARGO_BUILD_JOBS=4`. `FENIX_CORES=8` raises both while the user allows it (2026-10-30, until further notice). Example: `pwsh -NoProfile -File tools/capped.ps1 pnpm -C app tauri build --no-bundle` (a one-file change rebuilds in about 45 s; the release profile has no LTO).
- Firmware: `pwsh -NoProfile -File board/idf.ps1 build` (also `monitor`, `size`; not `flash`, see below). First time: `idf.ps1 set-target esp32`.
- Rust tests: `cargo test --workspace`
- C++ protocol test (MSVC): `pwsh -NoProfile -File board/components/fenix_proto/test/run.ps1` (checks `proto/vectors.txt`)
- Radio probe (P29, not for release): `pwsh -NoProfile -File board/idf.ps1 -B build-radio -D SDKCONFIG=build-radio/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.radio" build`, then `fenix flash board/build-radio` and `fenix logs 10` (heap per step). Flash the normal firmware back after. Bluedroid probe (P30): the same with `build-bluedroid` and `sdkconfig.bluedroid`; each boot runs one radio, `fenix reboot` switches
- Board load: `fenix sysinfo` (quit the PC app first) prints what the Task manager shows: per-core load, heap, flash, tasks.
- Spikes: `pwsh -NoProfile -File board/idf.ps1 -C spikes/<name> -B spikes/<name>/build build`, then `fenix flash board/spikes/<name>/build` (reads its `flasher_args.json`), then `python tools/serial_read.py 30 --reset` (reads without a timeout since P23). Corrected (P26): spikes were flashed with `idf.ps1 flash` (esptool)
- Board fonts: `python tools/gen_fonts.py` (Inter + Phosphor via `lv_font_conv`)
- Version check / sync: `python tools/version.py --check` / `python tools/version.py`
- Design tokens: `python tools/gen_tokens.py` (writes `board/components/fenix_ui/include/fenix_tokens.h` and `app/src/lib/tokens.css`; `--check` in CI)
- Firmware to the board: build, then `python tools/bundle_firmware.py`, quit the app, then `cargo run -q -p fenix-cli --release -- flash` (or `target/release/fenix.exe flash`). Corrected (P23): `idf.ps1 flash` runs esptool, whose read timeouts take the CH340 driver's crash path (`docs/board-info.md`); `fenix flash` uses the app's flasher (espflash on the patched serialport), writes only changed regions and resets the board. `--erase` wipes the whole flash first (settings and apps too). Spikes still flash with `idf.ps1` (their own images)
- Dev CLI (`cargo build -p fenix-cli`, then `target/debug/fenix.exe`): `ports`, `hello`, `logs [s]`, `time`, `reboot`, `tap <x> <y>`, `swipe <x1> <y1> <x2> <y2>`, `shot <png> [s]`, `bench [s] [nostream]` (slide fps, stream throughput), `list`, `install`, `remove`, `soak <min>`; `--port COMx`
- Board screenshots: `bash tools/capture_screens.sh <dir>` (PC app closed) erases the board's settings (NVS, with `fenix erase`; touch calibration goes too), installs `tools/sample-app`, taps through every system screen and the sample app, saves PNGs, and removes the sample again. Golden copies: `docs/screens/board/`
- Board simulator (`board/sim`, no board needed): the real `fenix_ui` + `fenix_apps` + `fenix_pkg` code and LVGL 9.5.0 built for Windows with MSVC, on stub IDF headers, `fx::hal` on a framebuffer and `fx::sys` with settings in memory, a clock fixed at Thursday 2026-10-29 08:35 (plus virtual time) and an app store in a folder of `.fxp` files. LVGL gets the board's own Kconfig values from `board/sim/lv_kconfig.h` (`python tools/sim_config.py` after a firmware build; `--check` runs in the board CI job). Time is virtual: it moves only when the script waits or the code calls `vTaskDelay`, so every run gives the same pixels.
  - Build: `pwsh board/sim/build.ps1` (vcvars64 + CMake + Ninja; output `board/sim/build/fenix_sim.exe`). LVGL comes from `board/managed_components` after a firmware build, else CMake fetches tag v9.5.0
  - Headless: `fenix_sim run <script> --out <dir> [--store <dir>]`. Script verbs (one per line, `#` comments): `tap x y`, `swipe x1 y1 x2 y2`, `wait ms`, `shot name`, `install file.fxp`, `remove id`, `set id name=value...`, `host id on|off`, `stream on fps|off`, `connect`, `disconnect`. Taps and swipes match `fenix tap`/`swipe` timing. It starts connected, with default settings and an empty store
  - Window: `fenix_sim window [--store <dir>] [--scale <n>]` (default 2×). The mouse is the finger; `C` toggles the PC link. The backlight level dims the picture as on the panel. Idle cost: ~0.6% CPU, 13 MB
  - Golden screens: `python tools/sim_shots.py` builds it, runs `board/sim/screens.txt` (the same walk as `tools/capture_screens.sh`, plus 6a "Waiting for host PC") and compares every pixel with `board/sim/golden/` (23 PNGs; corrected: 21 before `stream_idle`, 22 before `settings_debug`). The simulator shows version 0.0.0, not `VERSION`, so a release bump does not change the goldens; failures write `<name>.diff.png` in `board/sim/build/shots`. `--update` rewrites the goldens after an intended UI change; `--no-build` reuses the exe. CI job `sim` (windows-latest) runs it
  - Against the real board's `docs/screens/board/` captures: at most 0.6% of pixels differ (the clock minute and antialiasing edges)
- Sample app: `pwsh tools/sample-app/build.ps1` (board package, PC half `fenix-sample.exe`, local registry in `target/sample-registry`)
- Registry: `python tools/registry.py` (writes `registry/index.json`), `--check` (CI), `--local <out> <manifest> --board <fxp> [--pc <exe>]` (a local registry for `FENIX_REGISTRY`); see `docs/registry.md`
- App packages: `fenix-pkg build <dir> [-o out.fxp]`, `fenix-pkg inspect <file>`; on the board: `fenix install <file.fxp>`, `fenix list`, `fenix remove <id>`, `fenix set <file.fxp> <bind>=<value>...` (acts as the app's PC half), `fenix host <id> on|off`. `fenix tap` and `fenix swipe` print what the board sends back (EVENT, BOARD_STATE). C++ reader test: `pwsh board/components/fenix_pkg/test/run.ps1`

- PC app: `cd app; pnpm install; pnpm tauri dev` (hot reload) or `pnpm tauri build --no-bundle` (release exe at `target/release/fenixos.exe`). Flags: `--tray` starts without a window (autostart uses it), `--quit` quits a running instance. Logs: `%LOCALAPPDATA%\fenixOS\logs\fenixos.log`
- Bundle the board firmware into the PC app: `python tools/bundle_firmware.py` after `board/idf.ps1 build` (copies the bins named in `board/build/flasher_args.json` to `app/src-tauri/resources/firmware/` with `firmware.json`: version + offset/size/sha256 per file; refuses a build whose version differs from `VERSION`). The bins are git-ignored; the release workflow runs this before `tauri build`. The update dialog and Reinstall firmware flash exactly these
- Open-source licenses list (Settings > About): `python tools/licenses.py` writes `app/src/lib/licenses.json` from `cargo tree` (Windows target, normal deps), the UI's runtime npm packages + the Svelte runtime, and the firmware libraries; `--check` runs in CI. Run it after any dependency change
- Updater key: made with `pnpm tauri signer generate --ci -w %USERPROFILE%\.tauri\fenixos-updater.key` (no password). The private key stays there and in the `TAURI_SIGNING_PRIVATE_KEY` Actions secret (the release workflow signs with it); never in git. The public key is in `app/src-tauri/tauri.conf.json` (`plugins.updater.pubkey`). `bundle.createUpdaterArtifacts` is on, so a bundled `pnpm tauri build` needs `TAURI_SIGNING_PRIVATE_KEY` (and an empty `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`) set; `--no-bundle` does not
- Release: push tag `v<VERSION>` and `.github/workflows/release.yml` builds the firmware (IDF container), bundles it, builds the NSIS installer (per user, no UAC) with `tauri-apps/tauri-action@v1` and publishes a GitHub release with `fenixOS_<v>_x64-setup.exe`, its `.sig` and `latest.json` (the updater's endpoint). Run it by hand (Actions > release > Run workflow) to build without publishing; the files stay as workflow artifacts (without the secret, a hand-started run skips the updater signature; first run 2026-10-29 passed). Needs the `TAURI_SIGNING_PRIVATE_KEY` Actions secret. Local equivalent: `python tools/bundle_firmware.py`, set `TAURI_SIGNING_PRIVATE_KEY` to the key file's text, then `cd app; pnpm tauri build` (output in `target/release/bundle/nsis/`)
- Updater test without a release: build an old client with `pnpm tauri build --no-bundle --config <json>`, the JSON setting `"version": "0.0.9"` and `plugins.updater` to `"endpoints": ["http://127.0.0.1:8765/latest.json"], "dangerousInsecureTransportProtocol": true`. Serve the signed `fenixOS_<v>_x64-setup.exe` and a `latest.json` (`version`, `platforms.windows-x86_64.{signature, url}`; the signature is the `.sig` file's text) with `python -m http.server 8765 --bind 127.0.0.1`. Rebuild the normal exe afterwards: the test build overwrites `target/release/fenixos.exe`. The old client's Settings still shows v0.1.0 as its own version, because that text comes from the crate version, not the config override
- Updater test against the real release (P10, 2026-10-29): build the old client with only `--config '{"version":"0.0.9"}'` (and `CARGO_TARGET_DIR`, so the normal exe is not overwritten; a relative path resolves from `app/`). It finds the newest GitHub release at start.
- Driving the app's UI from a script: start it with `FENIX_WINDOW=background` (minimized, no focus) and `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS="<the args from tauri.conf.json> --remote-debugging-port=9333"` (the variable replaces the app's args), read `http://127.0.0.1:9333/json` for the page, then send CDP `Runtime.evaluate` over its WebSocket (Node 24 has `WebSocket` built in), e.g. `window.__TAURI_INTERNALS__.invoke('install_app_update')` or a button `.click()`. Only the first WebView2 environment gets the port: with the welcome window open, the main window's environment has none. Test builds only; never ship the variable.
- PC app footprint: `pwsh tools/measure_app.ps1 [-Seconds 10]` (private working set + CPU over `fenixos.exe` and its own WebView2 processes)


## PC spike results

### S8: WebGL for the first-run window (2026-10-29)

three.js 0.186.1, a lit PCB + glass test scene, 1280 × 800 at 1.25× scale, this laptop (RTX 4060 Laptop GPU), release build, no main window open. Memory is the private working set of `fenixos.exe` and its WebView2 processes (`tools/measure_app.ps1`).

| WebView2 args | Renderer | fps | CPU (of 16 threads) | Memory |
|---|---|---|---|---|
| Main window's lean args (`--disable-gpu --in-process-gpu`) | WARP (Microsoft Basic Render Driver, software) | 122 | 40 % | 184 MB |
| Same without `--disable-gpu --in-process-gpu` | ANGLE D3D11 on the GPU | 165 (display rate) | 1.1 % | 89 MB |

WebGL still works under `--disable-gpu`, but on the CPU. WebView2 shares one browser process, and so one set of args, per user-data folder, so the welcome window gets its own folder (`%LOCALAPPDATA%\fenixOS\webview-welcome`) with the GPU on; the main window keeps its lean args. Debug switch: `FENIX_OOTB_ARGS=lean` runs the welcome window with the lean args.

P9 acceptance run (the full first-run flow, the real scene): 165 fps (the display rate) the whole way; 137 MB and 7 % CPU for the app while the welcome window plays; 68 MB once it closes and the main window opens (its WebView2 processes end with it).

## Debug knobs

| Variable | Effect |
|---|---|
| `FENIX_REGEN=1` | `cargo test -p fenix-proto` rewrites the golden vectors (only after an intended wire change + proto version bump) |
| `FENIX_REGISTRY=<folder>` | The PC app reads the app registry from a local folder (`tools/sample-app/build.ps1` makes one in `target/sample-registry`) |
| `FENIX_FIRMWARE=<folder>` | The PC app flashes the firmware in this folder instead of its bundled resources |
| `FENIX_OOTB_FPS=1` | The first-run window shows its frame rate in the bottom-right corner |
| `FENIX_OOTB_ARGS=lean` | The first-run window uses the old GPU-off args (S8 comparison; the main window had them until P11) |
| `FENIX_WINDOW=background` | The main window opens minimized and does not take focus (scripted tests while someone works on the PC) |
| `--welcome` (flag) | Open the first-run window instead of the main one, whatever `welcome_done` says |
| `FENIX_DEBUG=1` | `fenix-link` prints the raw bytes it reads during the handshake |

## Gotchas found

- **Swatinem/rust-cache ignores Cargo.toml's `[profile]`** (2026-10-29): its key hashes the dependencies, so after a profile change it still reports "full match", restores artifacts built under the old profile, cargo compiles every crate again, and the stale cache is never saved over ("Cache up-to-date"). Its pinned v2 hashes member manifests with the version set to 0.0.0 and skips the virtual root manifest. Corrected (v0.1.1 release): keying on the whole `Cargo.toml` made every version bump a cold build, so the release profile moved to `.cargo/config.toml` and the workflows key on `hashFiles('.cargo/config.toml')`. Corrected (v0.1.3 release, still cold): a cache saved by a tag run is readable only by that tag, so the release job never found the `app` cache the v0.1.2 tag had saved. Now ci.yml's `rust` job on main also runs the release build (`pnpm -C app tauri build --no-bundle`) under `shared-key: app`, and release.yml restores that key with `save-if: false`.
- **`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` replaces the app's own WebView2 args** (2026-10-29), it does not add to them. A test that sets it for `--remote-debugging-port` must repeat the args from `tauri.conf.json`, or it measures a different setup (here the GPU came back on in a "GPU off" test).
- **WebView2 keeps a minimized window "visible"** (2026-10-29): `document.visibilityState` stays `visible` and `requestAnimationFrame` runs at the display rate (165 Hz here), so a WebGL scene in a minimized window burned a core. `BoardScene` pauses while Tauri reports the window minimized. A window moved off-screen is the opposite: Chromium counts it occluded and stops `requestAnimationFrame`, so screenshots of an off-screen window show a frozen scene.
- **A new app instance can attach to the old WebView2 browser process** (2026-10-29) when it starts while the old one is still shutting down, keeping the old args. Wait until no `msedgewebview2.exe` with `--webview-exe-name=fenixos.exe` is left before measuring.
- **Software WebGL runs in the browser process with `--in-process-gpu`** (2026-10-29): the 3D board screen there took ~8.5 cores in `msedgewebview2.exe` (browser), while the renderer showed 15 %.
- **Git Bash turns an installer's `/S` into a path** (2026-10-29): `./fenixOS_..._x64-setup.exe /S` from Git Bash reached NSIS as a Windows path, so the setup ran with its dialogs (welcome, uninstall of the old version, finish). Run installers from PowerShell (`& .\setup.exe /S`), or set `MSYS_NO_PATHCONV=1`.
- **The CH340 driver crashes Windows in its read-timeout code** (corrected 2026-10-29: the 2026 driver 4.0.2026.2 did it too, bugcheck `0xD1`, `CH341S64+48e9`). Never give a COM port a read timeout on this PC: serialport's `set_timeout` (and its `timeout()` on open) uses interval and multiplier `MAXDWORD` with a constant, the path that crashes. fenix-link sets `MAXDWORD, 0, 0` and polls (`no_wait_reads`, `docs/board-info.md`).
- **The 2011 CH340 driver crashes Windows when a pending read is cancelled** (2026-10-29, bugcheck `0x48`, `CH341S64+4120`, process `fenix.exe`). Force-killing a program that is blocked in `ReadFile` on COM9 does that. Updated to WCH CH341SER 4.0.2026.2 (installing it does not replace the old driver on the device: remove `oem160.inf` with `pnputil /delete-driver ... /uninstall`) (`docs/board-info.md`). Never `Stop-Process -Force` a fenixOS program that holds the port; the app and the CLI close the port before they exit (`Link::shutdown`, also run by Quit and before an app update installs).
- **The per-user NSIS installer installs into `%LOCALAPPDATA%\fenixOS`, which is also the app's data folder** (2026-10-29). Checked: uninstall removes only its own files (`fenixos.exe`, `firmware/`, `uninstall.exe`), so apps, logs and WebView data survive, and the updater installs over it in place.
- **`cargo tree` colours its output when `CARGO_TERM_COLOR=always`** (2026-10-29; set on GitHub runners by the Rust toolchain action), even into a pipe. ANSI codes reached the generated `licenses.json` and failed CI's `--check`. `tools/licenses.py` passes `--color never`.
- **MSVC mangles C++ names of global variables; GCC does not** (2026-10-29). `fenix_fonts.h` declared the C font objects without `extern "C"`, which links on the board (Itanium ABI leaves globals unmangled) but not in the MSVC simulator. `tools/gen_fonts.py` now wraps the declarations in `extern "C"`.
- **A fast mouse click can press and release between two LVGL reads** (simulator window). The simulator holds a press until LVGL has read it once.
- **`board/sdkconfig` does not pick up later edits to `sdkconfig.defaults`** (2026-10-29). IDF applies defaults only when `sdkconfig` is created. After changing `sdkconfig.defaults`, delete `board/sdkconfig` (it is git-ignored) and build again. A stale one cost half the UI frame rate (`docs/board-info.md`, Rendering speed).

- **Tauri IPC needs `connect-src ipc: http://ipc.localhost` in a custom CSP** (2026-10-29). Without it the webview cannot reach the IPC custom protocol and Tauri silently falls back to `postMessage`: plain commands still work, but a binary `tauri::ipc::Response` arrives as a plain array instead of an `ArrayBuffer` (the board mirror stayed black). Fixed in `app/src-tauri/tauri.conf.json`.
- **A VERSION bump did not reach the firmware** (2026-10-29). `board/CMakeLists.txt` reads `VERSION` at configure time only, so `idf.py build` kept the old `PROJECT_VER`. Fixed: `VERSION` is now in `CMAKE_CONFIGURE_DEPENDS`, so a change re-runs CMake.
- **A webview window created from a sync Tauri command stays black** (2026-10-29). On Windows, `finish_welcome` built the main window inside a sync command and WebView2 never painted it. Commands that create windows are `async` now.
- **Flashing a port the link is probing: "Access is denied"** (2026-10-29). `Link::pause` takes effect after the current handshake try, up to 2.5 s on a board that does not answer, so the 500 ms wait was too short for a blank board. `flash::connect` retries opening the port for 4 s.
- **Phosphor SVGs in a canvas** (2026-10-29). They already carry `fill="currentColor"` and no size: adding a second `fill` makes invalid XML (a broken image, and `drawImage` throws every frame), and without a size the image has no natural size. Swap `currentColor` and add `width`/`height`.
- **Python on Windows writes CRLF in text mode** (2026-10-29). Edit scripts must open files with `newline="\n"`, or git warns about CRLF. A path like `\fenixOS` in a non-raw Python string turns `\f` into a form feed: use raw strings.

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
