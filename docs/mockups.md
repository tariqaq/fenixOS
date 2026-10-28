# fenixOS v0.1 mock-ups

Review canvas (private, owner only; share from the page's Share menu):
https://claude.ai/artifact/4iSeRZ1tZLgcRxiS42vYji

Built from the locked design system in `DESIGN.md` (Apple dark, Inter, Phosphor). Board screens are drawn at 2x (320×240 shown as 640×480). PC screens are 1280×800 with the custom dark title bar.

Status: approved by the user on 2026-10-29 ("follow it exactly as it is"), with the round 7 changes below applied. New screens come in design round 2 (`docs/PLAN.md` P2).

## Artboards

| # | Board | What it shows |
|---|---|---|
| 1 | Main | Boot: fenixOS name, version, grey track with a white line sweeping left to right |
| 2a | HomeA | Home, landscape: clock + date, app tiles |
| 2b | HomeB | Home, landscape: clock + date, icon dock (second style, picked in Settings › Home screen) |
| 2c | HomePortrait | Home, portrait 240×320 |
| 2d | HomeNoTime | Home before the first PC time sync (`--:--`) |
| 3 | Settings | Settings list: Display, Home screen (Tiles / Dock), Storage, Host PC, About |
| 3a | SettingsDisplay | Brightness, auto-dim level, dim after, rotation, calibrate touch |
| 3b | SettingsAbout | Board, chip, CPU, memory free, flash, display |
| 4 | Session | Shut down / Restart pop-up |
| 5a | StreamOff | Stream app, no PC connected |
| 5b | StreamOn | Stream app, streaming at ~10 fps |
| 6a | HostWaiting | Host app installed, waiting for the PC half |
| 6b | HostOff | Host app switched off on the PC |
| 7 | PcConnect | PC: looking for the board (CH340 driver help) |
| 8 | PcDashboard | PC: app tiles with on/off switches, "+" (toggle: apps / empty) |
| 9 | PcAppSettings | PC: one app's settings, back to Apps |
| 10 | PcAddApp | PC: registry list, search, Add buttons |
| 11 | PcBoard | PC: live mirror of the board screen, facts, reboot, reinstall firmware |
| 12 | PcUpdate | PC: version mismatch dialog and the progress state |
| 13 | PcSettings | PC: start with Windows, port, updates, about |

## Review round 1 (2026-10-29)

- Home layout: keep **both** styles; the user picks Tiles or Dock in Settings › Home screen. (Settings artboard updated.)
- Boot: the spinner became a grey track with a white line sweeping left to right. (Main artboard updated.)
- Tile colours, PC tile size and switch place: approved as drawn.
- "Run as administrator" setting dropped: elevation is per action (UAC when an app needs it). (PcSettings artboard updated.)

## Still to draw (design round 2)

Board Settings › Home screen page, PC tray menu, permissions sheet, log view, PC "Show welcome again" row. Optional: a first-run (OOTB) keyframe storyboard, as Claude's own reference for P9 (no review needed). See `docs/PLAN.md` P2.
