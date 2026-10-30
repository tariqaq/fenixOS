# fenixOS v0.1 mock-ups

Review canvas (private, owner only; share from the page's Share menu):
https://claude.ai/artifact/4iSeRZ1tZLgcRxiS42vYji

Source copies of every artboard: `design/mockups/` (`*.dc.html` + `canvas.json`; they render only inside the canvas). Built from the locked design system in `DESIGN.md` (Apple dark, Inter, Phosphor). Board screens are drawn at 2x (320×240 shown as 640×480). PC screens are 1280×800 with the custom dark title bar.

Status: approved by the user on 2026-10-29 ("follow it exactly as it is"), with the round 7 changes below applied. 25 artboards, all in `design/mockups/`. Screens added later without an artboard are noted in their rows.

## Artboards

| # | Board | What it shows |
|---|---|---|
| 1 | Main | Boot: fenixOS name, version, grey track with a white line sweeping left to right |
| 2a | HomeA | Home, landscape: clock + date, app tiles |
| 2b | HomeB | Home, landscape: clock + date, icon dock (second style, picked in Settings › Home screen) |
| 2c | HomePortrait | Home, portrait 240×320 |
| 2d | HomeNoTime | Home before the first PC time sync (`--:--`) |
| 3 | Settings | Settings list: Display, Home screen (Tiles / Dock), Storage, Host PC, About (added without artboards: Wireless, P32, a Wi-Fi / Off list with the scan list; Debug switch at the end, P21) |
| 3a | SettingsDisplay | Brightness, auto-dim level, dim after, rotation, calibrate touch (P21 added a Burn-in protection switch below them, no artboard) |
| 3b | SettingsAbout | Board, chip, CPU, memory free, flash, display |
| 4 | Session | Shut down / Restart pop-up |
| 5a | StreamOff | Stream app, no PC connected. Connected but not streaming: "Not streaming" with a "Start streaming" button (P14); the board is where the stream is started (P15) |
| 5b | StreamOn | Stream app, streaming at ~10 fps |
| 6a | HostWaiting | Host app installed, waiting for the PC half |
| 6b | HostOff | Host app switched off on the PC |
| 7 | PcConnect | PC: looking for the board (CH340 driver help). Since P11 drawn in 3D: the welcome's board with the cable hovering, the plug-in run on connect, then a fade to the dashboard |
| 8 | PcDashboard | PC: app tiles with on/off switches, "+" (toggle: apps / empty) |
| 9 | PcAppSettings | PC: one app's settings, back to Apps |
| 10 | PcAddApp | PC: registry list, search, Add buttons |
| 11 | PcBoard | PC: live mirror of the board screen, facts, reboot, reinstall firmware. Since P13 the mirror starts off; Start stream turns it on. Since P14 it follows the board. Since P15 it has no Start button: the off state says to start streaming in the board's Stream app; Stop stream shows while live. P27 added a Task manager page after it in the sidebar (artboard 17 since P35) |
| 12 | PcUpdate | PC: version mismatch dialog and the progress state |
| 13 | PcSettings | PC: start with Windows, port, updates, about (with "Show welcome again") |
| 3c | SettingsHome | Board: Settings › Home screen, Tiles / Dock picker with previews |
| 14 | PcTray | PC: tray menu (status, Open fenixOS, Quit) |
| 15 | PcPermissions | PC: what an app's PC part will do, shown before Add |
| 16 | PcLogs | PC: log sheet (filter All / Board / Apps / Problems, Copy, Open log folder), opened from the Board page |
| 17 | PcTaskManager | PC: Task manager (P27, drawn in P35 from the shipped page): CPU per core, memory, the link (USB; over Wi-Fi "Wi-Fi link" without line use), storage, display, wireless, threads. Series colours `chart-1` / `chart-2`. In `design/mockups/` only; the review canvas gets it at its next publish |

## Review round 1 (2026-10-29)

- Home layout: keep **both** styles; the user picks Tiles or Dock in Settings › Home screen. (Settings artboard updated.)
- Boot: the spinner became a grey track with a white line sweeping left to right. (Main artboard updated.)
- Tile colours, PC tile size and switch place: approved as drawn.
- "Run as administrator" setting dropped: elevation is per action (UAC when an app needs it). (PcSettings artboard updated.)

## Design round 2 (2026-10-29, unattended run)

Drawn during the overnight run, so the review is pending: the user said to pick the recommended option for any choice. Choices made:
- Home screen picker: two live mini previews, the selected one outlined in `accent` with a check.
- Tray menu: status header (icon + words), Open fenixOS, Quit fenixOS. Left-click opens the window.
- Permissions sheet: app tile, name, version and source repo, the declared permissions as rows, a plain note on how fenixOS checks apps, Add app / Cancel.
- Logs: a sheet from the Board page ("Logs" row), filter, copy, open folder; problems in `warning`.
- "Show welcome again": first row of PC Settings › About.
- First-run storyboard: not drawn; the scene is designed directly in P9 (`docs/PLAN.md` §3.9).
