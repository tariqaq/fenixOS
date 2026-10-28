# fenixOS v0.1 mock-ups

Review canvas (private, owner only; share from the page's Share menu):
https://claude.ai/artifact/4iSeRZ1tZLgcRxiS42vYji

Built from the locked design system in `DESIGN.md` (Apple dark, Inter, Phosphor). Board screens are drawn at 2x (320×240 shown as 640×480). PC screens are 1280×800 with the custom dark title bar.

Status: first pass done 2026-10-29, waiting for the user's review.

## Artboards

| # | Board | What it shows |
|---|---|---|
| 1 | Main | Boot: fenixOS name, version, loading animation |
| 2a | HomeA | Home, landscape: clock + date, app tiles |
| 2b | HomeB | Home, landscape: clock + date, icon dock (alternative to 2a) |
| 2c | HomePortrait | Home, portrait 240×320 |
| 2d | HomeNoTime | Home before the first PC time sync (`--:--`) |
| 3 | Settings | Settings list: backlight, auto-dim, Display, Storage + host, About |
| 3a | SettingsDisplay | Rotation, touch calibration |
| 3b | SettingsAbout | Version, chip, flash, RAM, uptime |
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
| 13 | PcSettings | PC: start with Windows, run as administrator, port, updates, about |

## Open review questions

- Home layout: tiles (2a) or icon dock (2b)?
- Boot animation: is the loading style on board 1 right?
- Tile colours: keep the dark tile palette in `DESIGN.md`?
- PC dashboard: tile size and switch place OK?
- "Run as administrator" (board 13) is a proposal from the user's note that the PC app may run elevated. Keep it, or elevate per action only (UAC prompt when an app needs it)?
- Not drawn yet: tray menu, first-run onboarding, board log view.
