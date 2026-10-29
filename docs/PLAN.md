# fenixOS v0.1 build plan

Status: **plan written 2026-10-29, not started.** Progress is tracked in `tasks.md`, which mirrors the phases below.

Sources of truth, in order:
1. The mock-up canvas (https://claude.ai/artifact/4iSeRZ1tZLgcRxiS42vYji, list in `docs/mockups.md`). The user approved it on 2026-10-29: "follow it exactly as it is". Every screen is built to match its artboard.
2. `DESIGN.md`: tokens, type, icons, motion, components.
3. `docs/decisions.md`: every choice and its options.
4. `ideas.md`: the vision and the hard constraints C1–C10.

The FenixDeck PoC plan is in `docs/legacy/PLAN.md`; what it taught is in `docs/POC-WRITEUP.md`.

## 1. Scope

### 1.1 In v0.1

Board firmware (fenixOS v0.1.0):
- Boot screen: name, version, a grey track with a white line sweeping left to right. Home is built while it plays, then a smooth transition to Home.
- Home: time and date (MYT, UTC+8), `--:--` until the first PC time sync. Two styles, **Tiles** and **Dock**, chosen in Settings › Home screen. Works in all 4 rotations.
- System apps:
  - **Settings:** Display (rotation, brightness, auto-dim level, dim after, touch calibration), Home screen (Tiles / Dock), Storage, Host PC, About.
  - **Session:** pop-up with Shut down (deep sleep, tap to wake) and Restart.
  - **Stream:** link status; streams the screen to the PC at ~10 fps.
- Installed host apps: declarative packages on LittleFS; tiles fade in and out on install and remove. Host states: working, waiting for PC, turned off on PC.
- Link to the PC over USB serial, time sync, RGB LED status light, backlight dimming.

PC app (fenixOS v0.1.0, Windows):
- Tray app with a single instance. Closing the window destroys the webview. Tray menu: Open fenixOS, board status line, Quit.
- First-run experience (OOTB) with a 3D board model (§3.9).
- Screens per the canvas:
  - Finding the board.
  - Apps dashboard (tiles with switches, "+", empty state).
  - App settings.
  - Add app, with a permissions sheet.
  - Board page (live mirror, facts, reboot, reinstall firmware).
  - Update dialog with progress.
  - Settings.
- Logs: a rolling log file plus a log view.
- Host-app runtime:
  - Registry read from this repo; downloads checked against checksums.
  - Board packages pushed to the board.
  - PC halves run as sidecar processes.
  - Per-action UAC elevation.
- Flashing the bundled firmware with `espflash`. Lockstep versions; a mismatch offers an update.
- Self-update (Tauri updater + GitHub Releases), start with Windows (off by default).

### 1.2 Not in v0.1

SD card, Wi-Fi, NTP, Bluetooth, LDR auto-brightness, sounds, developer overlay, factory reset, manual time set, on-board OTA, other time zones, macOS or Linux PC app, enforced sandboxing of sidecars (C10: trust comes from the registry), Authenticode code signing, any real extra app (extra apps live in their own repos; this repo only has a test fixture app).

## 2. Repo layout

From round 5 (`docs/decisions.md`):

```
VERSION                  0.1.0, the only version source
board/                   ESP-IDF project (C++17, IDF v5.5.5)
  main/                  app_main: init order only
  components/
    LovyanGFX/           git submodule, 1.2.35
    fenix_proto/         C++ codec (shared golden vectors)
    fenix_hal/           display, touch, backlight, RGB LED, power, RTC time
    fenix_ui/            LVGL glue, theme from tokens, fonts, nav, widgets
    fenix_sys/           settings (NVS), clock, link task, app store (LittleFS), stream encoder
    fenix_pkg/           package reader (no IDF dependency, host-tested)
    fenix_apps/          boot, home, settings, session, stream, host-app runtime (app_pkg.cpp: LVGL builder, bindings; corrected 2026-10-29)
  sim/                   SDL2 simulator of fenix_ui + fenix_apps (stub HAL)
  partitions.csv, sdkconfig.defaults, idf.ps1
app/                     Tauri 2 + Svelte 5 PC app (src/, src-tauri/)
crates/
  fenix-proto/           Rust codec + golden vectors (source of the vectors)
  fenix-link/            port discovery, handshake, reconnect, single-thread serial I/O
  fenix-pkg/             package format, compiler library + `fenix-pkg` CLI
  fenix-sdk/             sidecar SDK (pipe API) for app authors
  fenix-cli/             `fenix` dev CLI: handshake, logs, stream to PNG, install, soak
proto/                   link protocol spec (spec.md) + vectors copy used by C++ tests
registry/                app manifests (apps/<id>.json) + generated index.json
design/                  tokens.json, fonts (Inter OFL), icon subset list (Phosphor MIT)
tools/                   gen-tokens, gen-fonts, version sync, sample-app (test fixture)
docs/, pics/
```

`.gitignore` must anchor the local backup folder as `/firmware/`. Today's unanchored `firmware/` would also hide `app/src-tauri/resources/firmware/`.

## 3. Architecture

### 3.1 Board runtime

- **Tasks:**
  - **UI task** on core 1: LVGL timer handler, touch read, flush.
  - **Link task** on core 0: UART RX/TX, frame decode, dispatch through queues to the UI task. LVGL is touched only by the UI task.
  - **Background worker** on core 0: LittleFS writes during installs, so the UI never blocks.
- **Console:** UART0 console logs off (PoC finding). Logs go out as `LOG` frames.
- **Display:**
  - LovyanGFX drives the ILI9341 (HSPI, DMA) and the XPT2046 (VSPI, `bus_shared = false`, raw range 300–3900 / 200–3900 until calibrated).
  - LVGL 9 runs in partial render mode with two draw buffers. The flush callback DMA-pushes each area through LovyanGFX.
  - The draw buffer size is picked in spike S1.
- **Frame rate:** capped at the measured full-push limit (about 45 fps, C5).
  - Small animations run at the cap.
  - Full-screen transitions are held to what keeps ~30 fps.
- **Rotation:** `lv_display_set_rotation` plus LovyanGFX rotation. All layouts use flex or grid, so every screen reflows in all 4 rotations.
- **Theme and fonts:**
  - The theme is generated from `design/tokens.json`.
  - Inter at the `DESIGN.md` sizes is built with `lv_font_conv` at 4 bpp. The 64/56 px clock face holds digits and the colon only.
  - Phosphor glyph subsets, fill and regular, are built as LVGL fonts.
- **Navigation:**
  - Top nav bar (`< Home`, centred title).
  - App open = scale and fade from the tile; push and pop = slide.
  - Session is a sheet over the current screen.
- **Settings store:** NVS. Rotation, brightness, dim level, dim delay, touch calibration, home style.
- **Clock:**
  - Sources: `TIME_SET` from the PC, on connect and hourly.
  - `TZ=MYT-8`.
  - System time survives `esp_restart` and deep sleep. A "time valid" magic word in RTC no-init memory tells a real power-on (show `--:--`) from a soft reboot.
- **Backlight:**
  - LEDC PWM on GPIO21.
  - After the dim delay with no touch, it fades to the dim level.
  - The first touch while dimmed only wakes the screen.
- **Power:** Shut down fades out, turns the backlight off, sets ext0 wake on GPIO36 (touch IRQ, low) and enters deep sleep. Wake is a normal boot.
- **RGB LED:**
  - Pins IO4/16/17, active low, LEDC.
  - States: host connected (short green pulse, then off), installing or flashing (dim blue), error (dim red), otherwise off.
  - Brightness is tuned in S6.

### 3.2 Link protocol (proto v1 of fenixOS)

- **Framing:** kept from the PoC, already proven: `COBS(payload ‖ crc16_le) ‖ 0x00`, CRC-16/CCITT-FALSE, little-endian, no floats.
- **Rules:**
  - The PC opens the port with DTR and RTS released.
  - Bytes are dropped until a valid frame arrives.
  - A disconnect is declared after 3 s without a valid frame.
- **Baud:** starts at 460800. Spike S3 tries 921600–2M with CRC error counting and the fastest clean rate wins.
- **New magic:** `"FNXO"`. Full spec in `proto/spec.md`, written in P3.

| Group | Messages |
|---|---|
| Session | `HELLO` (P→B: magic, proto ver), `HELLO_ACK` (B→P: magic, proto ver, fw version, panel size, rotation, free heap, app storage used/free, MAC), `PING`/`PONG`, `LOG`, `ERROR`, `STATS` |
| System | `TIME_SET` (UTC epoch s), `REBOOT`, `BOARD_STATE` (B→P: screen, open app, dimmed, uptime) |
| Apps | `APP_LIST_REQ`/`APP_LIST` (id, version, sha256 per installed app), `APP_INSTALL_BEGIN` (id, version, size, sha256), `APP_CHUNK` (offset, data), `APP_INSTALL_END` → `APP_RESULT`, `APP_REMOVE`, `APP_OPENED`/`APP_CLOSED` (B→P), `HOST_STATE` (P→B: per app, PC half running or not) |
| Bindings | `BIND_SET` (P→B: app, binding id, typed value: int, bool, string, image ref), `EVENT` (B→P: app, element id, kind tap/long/change, value) |
| Stream | `STREAM_START` (P→B: target fps), `STREAM_STOP`, `STREAM_RECT` (B→P: frame no, x, y, w, h, codec, chunk), `STREAM_FRAME_END` |

- The max payload stays about 1200 B. Larger items are chunked.
- The proto version is bumped on any layout change.
- Golden vectors are produced by Rust and checked byte for byte by the C++ host test, as in the PoC.

### 3.3 Stream encoder

- **Encoding:**
  - When streaming, the flush callback encodes each flushed area (RGB565) into a TX ring with the codec picked in S4 (RLE of 16-bit runs vs LZ4-class).
  - DMA to the LCD continues in parallel, so board animation is never slowed for the stream.
- **Backlog handling:**
  - If the ring is full, the area is dropped and added to a "stale" region list.
  - When the UI is idle, stale regions are invalidated, so LVGL redraws them and they flow out again.
  - Static screens therefore converge to an exact copy. Busy transitions show fewer frames on the PC ("~10 fps normal, lower in full-screen animations", round 2).
- **PC side:** `fenix-link` rebuilds a 320×240 (or 240×320) canvas. While the Board page is visible, the UI gets rect updates over a Tauri binary channel.

### 3.4 App packages (board half)

- **File (`.fxp`):**
  - Sections: header (magic, format ver, app id, version, min fenixOS), string table, element tree, bindings table, assets.
  - Element kinds: container (flex or grid, with separate landscape and portrait props), label, value label, button, icon button, switch, slider, image, icon glyph.
  - Assets are A8 or RGB565 images, raw in format 1 (corrected 2026-10-29: no compression; see `docs/packages.md`).
  - Fonts are the built-in Inter sizes only.
- **Source and limits:**
  - Source format: `app.toml` + `layout.json`, compiled by `fenix-pkg build`.
  - Limits checked at compile time and again on the board: 64 KB per package (corrected 2026-10-29 from 256 KB: the board holds the whole package in RAM), element count, and an RAM estimate.
- **Board runtime:**
  - One host app is open at a time.
  - Its LVGL objects are built from the package when it opens and freed when it closes.
  - Bindings update objects; touches on interactive elements send `EVENT`.
- **Store:** LittleFS on the `apps` partition (~1.9 MB), `/apps/<id>.fxp` plus `/apps/<id>.sha` (corrected 2026-10-29: no separate index). An install writes to a temp file, checks sha256 and renames it, so a broken install never leaves a half app.
- **Host states:**
  - PC half running: the app works.
  - No PC link: "Waiting for host PC" (artboard 6a).
  - `HOST_STATE` off: "Turned off on host PC" (6b).
- **Switch-off in v0.1:**
  - Switching an app off on the PC sends `HOST_STATE` off, then `APP_REMOVE`.
  - If the app is open on the board, the board shows 6b for 2 s, returns Home and fades the tile out.
  - If the board was unplugged, the next `APP_LIST` reconcile removes the app. The PC is the source of truth.

### 3.5 PC engine (Rust, always running while fenixOS runs)

- **Link manager:**
  - Scans CH340 ports (VID 0x1A86, PID 0x7523) and does the `HELLO` handshake.
  - Reconnects, reconciles apps, sends time sync on connect and hourly.
  - Single-thread serial I/O: batch write, then read with a 15 ms timeout (PoC gotcha).
  - Background scanning never resets a board.
- **App manager:**
  - Installed apps in `%LOCALAPPDATA%\fenixOS\apps\<id>\<version>\`.
  - State and settings in `%APPDATA%\fenixOS\config.json`.
  - Enable or disable, push packages, run sidecars.
- **Sidecars:**
  - Started only while their app is switched on.
  - One named pipe per app (`\\.\pipe\fenixos-<id>-<nonce>`) carries length-prefixed JSON messages: `hello`, `settings`, `opened`, `closed`, `event`, `set`, `log`.
  - Crash means restart with back-off (3 tries, then the tile shows an error).
  - `fenix-sdk` wraps this for Rust; the protocol is documented for other languages.
- **Elevation:**
  - A manifest may mark the PC half `elevated`. fenixOS then starts it with `ShellExecuteW("runas")` on a worker thread (PoC gotcha: never block `setup` on UAC), so Windows asks each time the app starts.
  - The rest of fenixOS never runs elevated.
- **Flasher:**
  - `espflash` library. It resets into the bootloader through DTR/RTS and writes the merged image bundled in the app resources.
  - Used for the version-mismatch update, "Reinstall firmware" and the OOTB first install.
- **Updater:**
  - `tauri-plugin-updater` reads `latest.json` from this repo's GitHub Releases, signed.
  - The private key lives outside git and as a GitHub Actions secret.
  - After an update, the app offers to flash the matching firmware.
- **Logs:** `%LOCALAPPDATA%\fenixOS\logs\`, rolling 3 × 1 MB, including board `LOG` frames.
- **Plugins:** `tauri-plugin-single-instance` (a second launch opens the window; `--quit` quits the running one), `tauri-plugin-autostart` (off by default).

### 3.6 PC UI (Svelte 5)

- **Lifecycle:** created when the window opens and destroyed on close. It holds no state the engine does not have.
- **Design:**
  - CSS variables are generated from `design/tokens.json`.
  - Inter and Phosphor are bundled locally (no CDN).
  - The custom title bar and the sidebar (Apps, Board, Settings) follow the canvas pixel for pixel.
- **App settings page:** rendered by fenixOS from the manifest's settings schema (toggle, slider, choice, text, button). Every app's settings therefore follow the fenixOS design, and values go to the sidecar.
- **WebView2:** lean `additionalBrowserArgs` (PoC). No UI updates while hidden. The OOTB window's GPU needs are settled in S8.
- **hallmark:** its edit-time lint hook stays on for all UI work. `DESIGN.md` stays locked and overrides hallmark's own themes.

### 3.7 Registry

- **Location:** `registry/apps/<id>.json`, one manifest per app.
- **Manifest fields:**
  - Identity: id (`io.github.<owner>.<name>`), name, one-line description, version, fenixOS range.
  - Look: tile colour token, Phosphor icon name.
  - Downloads: board package URL + sha256, sidecar URL (win-x64) + sha256.
  - Behaviour: permissions, `elevated` flag, settings schema.
- **Index:** `registry/index.json` is generated and checked in CI. The PC app reads it from `raw.githubusercontent.com/tariqaq/fenixOS/main/registry/index.json`.
- **Allowed sources:** downloads come only from GitHub repos owned by `tariqaq`.
- **Local testing:** a dev setting (`FENIX_REGISTRY=<folder>`) points at a local registry, so the test fixture app can go through Add app.

### 3.8 Versions

- `VERSION` feeds everything. `tools/version` writes it into `Cargo.toml` files, `tauri.conf.json`, `package.json` and the board's `PROJECT_VER`. CI fails if any differs.
- The board shows "fenixOS v0.1".
- On connect, a mismatch opens the update dialog (artboard 12). Host apps stay off until the versions match.

### 3.9 First-run experience (OOTB)

Shown on the first launch, and again from PC Settings › Show welcome again. Creative direction (user: free hand, must fit the design language):

- **Look:**
  - Black stage with soft studio light.
  - A 3D model of the ESP32-2432S028R, built from its real size: module 86 × 50 mm, 2.8" screen, USB-C and micro-USB ports.
  - Dark PCB, glass screen with subtle reflection.
  - Real-time with Three.js. The model is procedural, so no third-party asset licences apply.
- **Motion:**
  - Slow camera drift, with the same ease curve as `DESIGN.md`.
  - A USB-C cable slides in, the port glows faintly, and the board screen lights up with the fenixOS boot line.
  - `prefers-reduced-motion` swaps the motion for cross-fades.
- **Flow:**
  1. Welcome: the "fenixOS" wordmark fades in while the board turns into view.
  2. Board check.
     - If a board is already plugged in, wait 3 s, then play the "plugged in" animation and continue.
     - If not, the cable hovers near the port in a gentle loop with "Plug in your board" and the CH340 driver hint. It plays the "plugged in" animation as soon as the board is found.
  3. Firmware check.
     - If the board answers as fenixOS, it is synced and the flow continues.
     - If it does not answer (blank or other firmware), fenixOS probes it with `espflash`. That is the only place, besides explicit updates, where a board is reset into the bootloader.
     - fenixOS shows "Install fenixOS on this board? This erases what is on it."
     - On confirm, it flashes while the model's screen shows the progress line.
  4. Done: the board boots fenixOS, time syncs, and the dashboard opens (empty state).

## 4. Budgets and targets

All numbers are targets to measure, not facts. Each result goes into `docs/board-info.md` or `docs/toolchain.md`. Policy (user, 2026-10-29): PC targets are realistic guides, not a reason to hyper-optimise; on the board, squeeze for efficiency.

### 4.1 Board RAM (~260 KB heap at start, no PSRAM)

| Item | Estimate |
|---|---|
| LVGL draw buffers (2 × 320 × 24 × 2 B) | 31 KB (S1 picks the real size) |
| LVGL objects and styles (system screens) | 40–50 KB |
| Open host app objects + asset cache | ≤ 40 KB |
| Link buffers + UART driver | 10 KB |
| Stream TX ring | 16 KB |
| Task stacks | 20 KB |
| Kept free for Wi-Fi later | 45 KB |

Gate: free heap ≥ 60 KB with Home shown and ≥ 40 KB with a host app open and streaming.

### 4.2 Board flash (4 MB)

`nvs` 24 KB, `phy_init` 4 KB, `factory` 2 MB (image target ≤ 1.5 MB: LVGL + fonts + icons), `apps` LittleFS ~1.9 MB. There are no OTA slots (C8).

### 4.3 Board feel

| What | Target |
|---|---|
| Reset to boot screen | ≤ 1 s |
| Reset to Home, interactive | ≤ 2.5 s |
| Tap to visual feedback | ≤ 50 ms |
| Small animations | ≥ 40 fps |
| Full-screen transitions | ≥ 25 fps, no visible tearing band |
| Stream on Home / Settings | ≥ 10 fps on the PC |

### 4.4 PC

| What | Target |
|---|---|
| Tray only | ≤ 35 MB private, ~0% CPU idle (PoC: 29 MB) |
| Window open, idle | ≤ 120 MB incl. WebView2 (PoC: 96 MB) |
| Plug in to connected | ≤ 2 s |
| Install a 100 KB package | ≤ 5 s |
| Full firmware flash | ≤ 60 s |
| Cold start to window | ≤ 1.5 s |

## 5. Phases

Each phase ends with:
- Its acceptance check.
- Docs updated.
- A stale sweep.
- `tasks.md` updated.
- A commit, `git pull --rebase`, then `git push` (AGENTS.md).

### P0. Repo foundation

1. Add `LICENSE` (MIT) and a new `README.md` (what fenixOS is, status, layout, build).
2. Create the layout from §2 with stub build files and `VERSION` = `0.1.0`. Anchor `/firmware/` in `.gitignore`.
3. Pin exact versions (latest stable, checked on the web, recorded in `docs/toolchain.md`): LVGL 9.x, `joltwallet/littlefs`, Tauri 2.x and its plugins, Svelte 5.x, Vite, Three.js, `espflash`, `serialport`.
   - Keep IDF v5.5.5 and LovyanGFX 1.2.35 (decided).
   - Rewrite the legacy build-command part of `docs/toolchain.md`.
4. Add `tools/version` (sync + check) and `tools/gen-tokens` (`design/tokens.json` → C header + CSS), with tokens taken from `DESIGN.md`.
5. Set up a CI skeleton (GitHub Actions):
   - Board build in the `espressif/idf:v5.5.5` image.
   - Rust tests and the PC build on `windows-latest`.
   - Version check.

Accept: CI green on an empty build; `tools/version check` passes.

### P1. Spikes (de-risk on the real board; results documented, code kept only if clean)

| # | Question | Done when |
|---|---|---|
| S1 | LVGL 9 + LovyanGFX on IDF 5.5.5: draw buffer size, SPI clock, DMA flush, fps for a full-screen fade and a small animation, heap after init, 4 rotations, touch mapping | Numbers in `docs/board-info.md`; buffer size and fps cap chosen |
| S2 | Inter + Phosphor via `lv_font_conv`: flash per size, render quality on the panel | Font set and sizes fixed; flash total known |
| S3 | Link speed: 460800 / 921600 / 1.5M / 2M, 10 min each, CRC error count | Baud chosen and documented |
| S4 | Stream codec: RLE vs LZ4-class on Home, Settings and a transition; encode cost per flush; fps at the chosen baud | Codec chosen; fps numbers |
| S5 | Deep sleep with ext0 wake on GPIO36 from the XPT2046 IRQ; current-state check; time kept across sleep and restart | Wake works by tap; time kept |
| S6 | RGB LED via LEDC (V1.4 resistors unverified): usable dim levels per colour | Levels for the 3 states |
| S7 | `espflash` as a library from Rust on COM9: chip probe, merged-image write, time | Flash time and API notes |
| S8 | OOTB 3D: Three.js model in WebView2 with the lean args vs GPU on; fps and memory; window-scoped args possible? | Rendering mode chosen for the OOTB window |

### P2. Design round 2 (runs alongside P1; needs the user's review)

Add these artboards to the canvas in the same style, then have the user review them:
- Board: Settings › Home screen (Tiles / Dock picker with previews).
- PC: tray menu.
- PC: permissions sheet in Add app.
- PC: log view (proposal: "Logs" row on the Board page opening a sheet with filter, copy and "Open log folder").
- PC: "Show welcome again" row in Settings.

The OOTB 3D scene is Claude's creative call (user, 2026-10-29). A keyframe storyboard may be drawn on the canvas as Claude's own reference; it needs no review.

Accept: the user approves (during an unattended run: recommended choices, logged in `docs/decisions.md`); `docs/mockups.md` updated.

### P3. Link protocol

1. Write `proto/spec.md` (§3.2) with every message, field and size.
2. Write `crates/fenix-proto`: types, encode/decode, COBS, CRC. Golden vectors are generated with `FENIX_REGEN=1` only after an intended change.
3. Write `board/components/fenix_proto` (C++) and its host test (MSVC, PoC script pattern), checked against the same vectors.
4. Write `crates/fenix-link`: discovery, handshake, reconnect, keepalive, reconcile hooks, stream canvas.
5. Write `crates/fenix-cli` (`fenix hello|logs|time|stream --png|install|remove|soak`).

Accept: Rust and C++ agree on all vectors. `fenix soak` runs 1 h with 0 CRC errors once the board link task from P4 exists.

### P4. Board core

1. Write `fenix_hal` (display, touch with NVS calibration, backlight fade, LED, power) and `fenix_ui` (LVGL glue, theme, fonts, widgets: top bar, tile, dock icon, list row, switch, slider, sheet, progress line).
2. Write `board/sim`: an SDL2 build of `fenix_ui` + `fenix_apps` with a stub HAL and a fake link. Screens can then be tried on the PC without flashing. The sim saves PNG screenshots per screen and rotation.
3. Write `fenix_sys`: settings store, clock (`TIME_SET`, RTC no-init flag, MYT), link task, `LOG` frames, `STATS`, partition table with LittleFS.
4. Set the init order: display and boot screen first, then the rest while the boot line plays.

Accept: the board boots to the boot screen ≤ 1 s, then talks to `fenix hello`; the time sets from `fenix time`; heap and fps are logged.

### P5. Board UI and system apps (match artboards 1–5 exactly)

1. Boot: wordmark, version, sweep line, smooth transition to Home.
2. Home Tiles (2a), Dock (2b), portrait (2c), no time (2d); style switch live from Settings.
3. Settings tree: Display, Home screen, Storage, Host PC, About (3, 3a, 3b + the P2 page).
4. Session sheet (4): Shut down (fade, deep sleep, tap wake), Restart.
5. Stream (5a, 5b) and the stream encoder (§3.3).
6. Motion per `DESIGN.md`: app open scale and fade, nav slide, sheet, tile fade.
7. LED states; backlight auto-dim.
8. Screenshot tests: sim PNGs vs approved golden PNGs (checked in), per screen and rotation.

Accept: every screen matches its artboard in all 4 rotations (side-by-side review with the user), plus the §4.3 targets and the §4.1 heap gate.

### P6. App packages

1. Write the `.fxp` format spec in `docs/packages.md`, plus the `fenix-pkg` compiler and CLI with limits.
2. Write the board `fenix_pkg` reader and LVGL builder, bindings and events; the LittleFS store with atomic install; tile fade in and out.
3. Implement the install, remove and reconcile messages end to end with `fenix install/remove`.
4. Build the host states 6a and 6b, and the switch-off sequence (§3.4).
5. Add a test fixture `tools/sample-app` (board package + tiny Rust sidecar using `fenix-sdk`), used only for tests.

Accept: the sample app installs ≤ 5 s, shows bound values, sends events and removes cleanly. A power cut during an install leaves no broken app.

### P7. PC app shell

1. Scaffold Tauri 2 + Svelte 5 in `app/`: tokens CSS, local fonts and icons, custom title bar, sidebar, routing.
2. Engine in Rust: link manager on `fenix-link`, state store, events to the UI. The engine runs without a window.
3. Tray with menu (P2 design), single instance, `--quit`, destroy webview on close, autostart setting. (Corrected: v0.1 uses the native tray menu; see `docs/decisions.md`.)
4. Logs: rolling file plus the log view (P2 design).
5. Finding-the-board screen (7) and time sync.

Accept: the §4.4 tray and window numbers (measured with a `tools/measure_app.ps1` port); plug and unplug are detected ≤ 2 s.

### P8. PC features (match artboards 8–13 exactly)

1. Apps dashboard (8): tiles, switches, "+", empty state. Switch on: download (if needed), push the package, start the sidecar. Switch off: stop the sidecar, remove from the board.
2. App settings (9) from the manifest schema.
3. Add app (10): registry fetch, search, permissions sheet, download, sha256 check, install. Registry tooling: index generator + CI check.
4. Sidecar runtime + `fenix-sdk`: pipe, lifecycle, restart back-off, elevated start.
5. Board page (11): live mirror, facts, reboot, reinstall firmware.
6. Update flow (12): version check on connect, dialog, `espflash` flashing with progress, reconnect.
7. Settings (13): start with Windows, connection port, updates, about.

Accept: the full loop with the sample app from a local registry works. A mismatched firmware gets updated from the dialog. The mirror shows ≥ 10 fps on Home.

### P9. First-run experience

1. Build the 3D board model and scene (Three.js, procedural, real dimensions), lighting and materials in the design language.
2. Build the flow from §3.9, including the 3 s wait for an already-plugged board, the plug-in loop, the probe and first install, and reduced motion.
3. Add the "Show welcome again" entry in PC Settings.

Accept: a smooth run on this laptop at the S8 numbers. A blank board (like today's) goes from plug-in to the fenixOS dashboard with no manual step except the install confirm.

### P10. Release v0.1.0

1. CI release on tag `v0.1.0`: board build, merged image into app resources, signed Tauri NSIS installer (per-user, no UAC), `latest.json`.
2. Generate the updater key: private key outside the repo and in an Actions secret, public key in `tauri.conf.json`.
3. Test the updater with a throwaway `v0.1.1-test` pre-release, then delete it.
4. README: install, first run, build from source, how apps work. Docs and stale sweep across the repo.
5. Do a final hardware pass: all screens, all rotations, a 1 h soak, the shut down / wake cycle.

Accept: a clean install from the GitHub release on this PC goes through OOTB on the blank board to a working dashboard.

## 6. Testing

- **Protocol:** golden vectors, Rust ↔ C++, byte for byte.
- **Board UI:** sim screenshot tests per screen and rotation. Hardware fps and heap logs come from `STATS`.
- **Rust:** unit tests per crate; `fenix-pkg` round-trip and limit tests; registry manifest validation.
- **End to end:** the `fenix` CLI and the sample app on the real board; `fenix soak` 1 h.
- **PC UI:** checked by eye against the canvas, with screenshots kept in `docs/screens/` for the README.

## 7. Risks

| Risk | Plan |
|---|---|
| RAM too tight for LVGL + package + stream (C9) | S1 numbers early; smaller draw buffers; stream ring shrinks; one app open at a time |
| Full-screen transitions tear or stutter (C5) | Cap fps; prefer motion that redraws small areas; S1 picks the limits |
| Touch IRQ cannot wake from deep sleep | S5; fallback: Shut down = backlight off + light sleep, wake on touch poll |
| Faster baud unstable on CH340C | S3; stay at 460800, the stream drops to fewer fps |
| WebGL slow under lean WebView2 args | S8; fallback: pre-rendered video clips of the same scene for the OOTB |
| SmartScreen warns on the unsigned installer | Accepted for v0.1 (no Authenticode certificate); noted in the README |
| Sidecars are full-trust native code (C10) | Registry allowlist (own repos only), pinned versions, sha256; permissions shown before install |

## 8. Open questions

None. Licence: MIT. "Show welcome again": yes (both 2026-10-29). Later choices during an unattended run take the recommended option and are logged in `docs/decisions.md`.
