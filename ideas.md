# fenixOS ideas

The user's vision for fenixOS, written down on 2026-10-29 and kept current as decisions are made. Choices are logged in `docs/decisions.md`; this file holds the idea itself, the constraints that shape it, and what is still open.

## 1. Background

- The FenixDeck PoC (PC hardware monitor on the board) proved the board, the link and the toolchain. Its code is legacy and experimental: local copy `C:\Users\trqboi\Documents\fenixdeck-legacy`, GitHub `tariqaq/fenixdeck`. What it taught is in `docs/POC-WRITEUP.md`.
- fenixOS is the "proper" build: a clean, modular system with an OS on the board and a companion app on the PC.
- The board flash was wiped clean on 2026-10-29 (both earlier images are backed up in `firmware/`, local only).

## 2. What this repo holds

- **Only** the fenixOS board firmware, the fenixOS PC app, and what they share (protocol, design system, app registry manifests).
- Extra apps (for example a stream-deck app) each live in **their own GitHub repo**. This repo only keeps the central registry of their manifests.

## 3. Names and versions

- Board firmware: **fenixOS**, first release **fenixOS_v0.1**.
- PC app: also **fenixOS**.
- Both always share the **same version number**.

## 4. Board: fenixOS firmware

### 4.1 Goals

- Extremely optimized, efficient and clean.
- Smooth animations and transitions at the highest frame rate the board can render without visible tearing.
- Apple dark design language (section 7).

### 4.2 Boot

1. Show the OS name and version with a very simple loading animation.
2. While it plays, set up the home screen.
3. Smoothly transition into the home screen.

### 4.3 Home screen

- Current **time and date** at the top.
- Time zone: **Kuala Lumpur, Malaysia** (MYT, UTC+8, no daylight saving).
- Keep time on the board ("let the onboard crystal keep time"; see constraint C1).
- Below the time and date: the app tiles. First the 3 system apps, then any installed apps, which fade in and out smoothly when installed or removed.

### 4.4 System apps (built in, always installed)

These 3 are part of the firmware. They have **no tile and no switch** in the PC app.

| App | Kind | Does |
|---|---|---|
| **Settings** | Full app, extremely light | Backlight brightness; auto-dim level; time before auto-dim; system info / specs |
| **Session** | Pop-up | **Shut down** and **Reboot** buttons that do just that (see constraint C4) |
| **Stream** | Full app, extremely light | Shows whether a PC connection is found. When connected, streams the board's exact screen to the PC at a steady rate, target ~10 fps (see constraint C2) |

### 4.5 Installed apps (from the PC)

- Behaviour **for now (no SD card, 4 MB flash):**
  - Switching an app **on** in the PC app immediately loads its board part onto the board's flash. It fades into the home screen next to the system apps, ready to launch.
  - Switching it **off** makes it fade out of the home screen and deletes it from the board's flash.
- An app that needs the host PC, launched on the board:
  - App switched on in the PC app and PC connected: it works normally. Data flows from the PC; events and actions go back to the PC.
  - PC not connected: "Waiting for host PC connection".
  - App switched off on the PC: "This app is turned off on the host PC" (or "host PC has disabled this app").
- **Later, with an SD card:** the board detects the card, and apps can be installed onto it and stay there. Host-dependent apps follow the same three states above.

Example (an illustration only; not part of v0.1): a stream-deck app is switched on in the PC app. On the board it shows a mute button and a volume slider. Touching them sends the event straight to the PC part of the app, which controls the PC.

## 5. PC app: fenixOS

### 5.1 Goals

- Rust, with a Tauri UI in Svelte (user's first thought; to be confirmed).
- Extremely light, efficient and clean, since it often runs in the background from the tray icon.
- Can offer an elevated (UAC) option where a feature needs it.

### 5.2 Flow

1. Find and connect to the board. USB first; Wi-Fi and Bluetooth later.
2. Once connected: the **home dashboard** shows every installed app as a tile, in the style of Apple Shortcuts tiles but bigger (colour tile, icon top left, name bottom left). Each tile has an **on/off switch at its bottom right**.
3. **Switch on** means the app's board part may run when it is opened on the board, and it gets installed to the board (section 4.5). **Switch off** means the board part says it is turned off on the host, and it is removed from the board.
4. **Click a tile** to open that app's settings and customization. **Back** returns to the dashboard.
5. A **plus (+) button** opens a list of available apps, read from the manifests kept centrally in this repo. Picking one makes the PC app use the manifest to fetch the app from its own GitHub repo and install it into the PC app. It then shows as a tile, ready to switch on, and switching it on installs it on the board.
6. Later, when the board has its own SD card and may already have an app installed, this flow changes.

## 6. App model (modular)

- **System apps:** in the firmware, always present, not managed from the PC (Settings, Session, Stream).
- **Host apps:** two halves. The board half is UI plus input; the PC half is logic, data and PC control. They talk over the fenixOS link through the PC app.
- The PC app is the source of truth for which apps are installed and switched on.
- A **central registry of manifests** lives in this repo and points to each app's repo.

## 7. Design language

- **Apple dark theme**, following Apple's Human Interface Guidelines (https://developer.apple.com/design/human-interface-guidelines), on **both** the board and the PC app, and in every extra app on both sides.
- **Fonts: Apple-like.** If Apple's fonts can't be used, use the closest Google font. (They can't: see constraint C6.)
- Animations and transitions: smooth everywhere; on the board, the highest frame rate it can render without tearing.
- Design tooling: the frontend-design skill and **hallmark** (installed in this repo, see `docs/tooling.md`), used for everything design-related in this project.

## 8. Process

- Discuss and ask everything first (picker questions with pros, cons and a recommendation), research as needed.
- Then wireframes / mock-ups, phase by phase like Figma design stages, to review slide by slide.
- Then back-and-forth, then a detailed, well-scoped build plan.
- Throughout: keep every doc and `tasks.md` current, and commit, push and sync often (`AGENTS.md`).

## 9. Hard constraints found (2026-10-29)

These come from the hardware facts in `docs/POC-WRITEUP.md` and from research. They change parts of the idea, so each one is a question in section 11.

- **C1. The board cannot keep time across power loss.** The 40 MHz crystal keeps time only while powered, at about ±1–2 s per day (typical ±10–20 ppm). There is no RTC battery and no 32 kHz crystal. Unplug or power loss means the time is gone; a soft reboot or deep sleep keeps it. A time source is needed: sync from the PC on connect (no Wi-Fi needed), Wi-Fi NTP (later), or set it by hand in Settings.
- **C2. Raw 10 fps screen streaming does not fit over USB serial.**
  - A full frame is 240 × 320 × 2 = 153,600 B. At 460800 baud (~46 KB/s) that is ~0.3 fps. Even at the CH340C's 2 Mbps peak (~200 KB/s, untested here) it is ~1.3 fps.
  - Ways to get ~10 fps:
    - Send only changed regions (the UI library's flush rectangles), compressed. A flat dark UI compresses very well.
    - Or have the PC rebuild the screen from UI state.
    - Or Wi-Fi later.
  - The board has no RAM for a full framebuffer (no PSRAM), so streaming must hook into the drawing path or read the LCD back (proven, but slow).
- **C3. The board cannot run arbitrary native code downloaded at run time the easy way.** Options for the board half of an app:
  - Declarative packages: layout, assets and bindings that the firmware's UI engine renders, with the logic on the PC. Smallest and safest.
  - A small script VM: Berry starts at ~4–10 KB RAM and is used on the ESP32 by Tasmota. Lua and wasm3 are heavier, MicroPython heavier still.
  - Native ELF loading: Espressif's `elf_loader` component. The code runs from scarce internal RAM, so it is the most complex option.
- **C4. "Shut down" can't cut power.** The board has no power switch, and USB keeps it powered. Shut down = backlight off + deep sleep, woken by a touch (touch IRQ on GPIO36 can wake from deep sleep) or by the RST button. Reboot = software restart.
- **C5. No vsync.** The LCD's TE pin is not wired, so tearing can only be minimized: partial redraws by DMA and capped fps. A full-screen push takes ~22 ms (about 45 fps max). Full-screen effects such as cross-fades redraw the whole screen, so they run slower than small animations.
- **C6. Apple's fonts and SF Symbols can't be used.** Apple's license limits SF fonts to Apple platforms. It does not allow embedding them in a non-Apple app or firmware, nor even mock-ups of software for non-Apple systems. Use **Inter** (Google Fonts, OFL, designed in a similar spirit to SF Pro) or a similar open font. Icons need an open set too (for example Lucide, Phosphor, Material Symbols Rounded).
- **C7. Private repo vs. central registry** (resolved: the repo is now public). This repo was private, so the PC app cannot read the registry from it without a GitHub token. Options: a token in the PC app, a separate public registry repo, or making this repo public.
- **C8. Flash budget.** 4 MB total. The current plan has room for the firmware plus about 1.9 MB of app storage (LittleFS). Firmware updates through the PC app (by serial bootloader) need no OTA partitions. On-board OTA slots would take ~1.5 MB more.
- **C9. RAM budget.** ~260 KB of heap before our own buffers, no PSRAM. The UI library choice (LVGL 9 vs a custom renderer), render buffers, an app runtime and later Wi-Fi (~45 KB+) all share it.
- **C10. Installing apps from GitHub runs code on the PC.** Each app's PC half needs a runtime (WASM plugin, separate process, or script) and a trust model: pinned versions, checksums, and only the user's own repos.

## 10. Things that may be missing (suggested)

- **Firmware update from the PC app:** flash or update the board from the PC app (for example with the `espflash` Rust library), so versions stay in step.
- **Version mismatch handling:** a PC app and board on different versions. The PC app offers to update the board.
- **Board ↔ PC app sync on connect:** reconcile installed apps (the PC is the source of truth), queue installs made while the board was unplugged, and close a running app cleanly when it is switched off.
- **Time sync** on connect (C1), and a manual time set in Settings.
- **Screen orientation:** landscape 320×240 or portrait 240×320 for the home screen.
- **Navigation on the board:** how to go back or home (back button, edge swipe, home gesture) on a resistive screen.
- **More Settings items:** about/version, storage used, touch calibration, orientation, time and date, factory reset.
- **Light sensor** (IO34) for auto-brightness later; its V1.4 circuit is unverified.
- **RGB LED** as a status light (connected, busy, error).
- **App permissions:** what a PC-side app may do (keys, volume, files, network), shown when installing.
- **PC app:** start with Windows (the PoC had none), single instance, a clean quit, a window style (custom dark title bar or Windows native), and updates of the PC app itself.
- **Logs and diagnostics:** a board log view, a heap/fps overlay (the PoC had these).
- **Testing:** golden vectors for the protocol, a host build of the board UI to iterate without flashing, and screenshot checks.
- **Repo layout:** `firmware/` today holds flash backups and would clash with a firmware source folder.

## 11. Decided so far

Details and the options offered: `docs/decisions.md`.

- Board: **C++17 on ESP-IDF 5.5.5**, UI with **LVGL 9 + LovyanGFX**.
- Board half of host apps: **declarative packages** (layout + assets + bindings); logic in the PC half (answers C3).
- Orientation: **switchable in Settings**, all 4 rotations; every layout adapts.
- Versions: stay on IDF 5.5.5, LovyanGFX 1.2.35, CH340 driver 3.3.2011.11.
- Navigation: iOS-style **top nav bar** (`< Home`, centred title).
- Time (C1): **PC sync on connect** + hourly, **kept in RTC memory across soft reboot / deep sleep**, Wi-Fi NTP later; no manual set. `--:--` until the first sync after power-up.
- Stream (C2): **changed regions, compressed** (LVGL flush rects, RLE/LZ); ~10 fps in normal use.
- Shut down (C4): **deep sleep, tap to wake** (touch IRQ GPIO36) or RST.
- PC app: **Rust + Tauri 2 + Svelte 5**.
- PC half of host apps (C10): **sidecar process** per app, started only while switched on, local pipe + typed API; trust via registry, pinned versions, checksums.
- Registry (C7): **this repo is public**; the PC app reads the registry with no auth. Vendor files, flash images and the packing slip were scrubbed from history first.
- Firmware updates (C8): **the PC app flashes the board over serial** (`espflash`), firmware bundled with each PC app release. No OTA slots.

- Fonts (C6): **Inter** everywhere. Icons: **Phosphor** (fill on tiles, regular in bars and rows).
- Motion: **iOS-like, tuned** (ease-out/spring 200–350 ms; full-screen steps held to ~30 fps).
- PC window: **custom dark title bar** with Windows min/max/close.
- Repo layout: `VERSION`, `board/`, `app/`, `crates/`, `proto/`, `registry/`, `design/`, `docs/`, `pics/`, `tools/`.
- Versions: **lockstep SemVer** from one `VERSION` file; a mismatch makes the PC app offer to flash the board.
- Settings app adds: **Display** (rotation, touch calibration) and **Storage + host status** (app space, link, last sync, MYT).
- The old pre-scrub commits stay reachable on GitHub by hash only (accepted).
- PC app: **start with Windows is a setting, off by default**; **self-update with the Tauri updater from GitHub Releases** (signed), then offer to flash the matching firmware; **closing the window destroys the webview** (tray only).
- Board extra: **RGB LED status light**. Not in v0.1: LDR auto-brightness, sounds, developer overlay, factory reset.

## 12. Open questions

Settled through the mock-up review rather than the picker: home-screen layout per orientation, tile style on the board, the boot animation, app tile colours, how the PC dashboard looks, and "Run as administrator" vs per-action elevation. Canvas and question list: `docs/mockups.md`.
