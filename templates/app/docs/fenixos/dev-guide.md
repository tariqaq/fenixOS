# fenixOS app developer guide

One file with what you need to build an app for fenixOS: the board, the PC app, the `fenix` CLI, the app format, the PC half and the release steps. It ships in `fenixos-app-template.zip` as `docs/fenixos/dev-guide.md` (source in the fenixOS repo: `templates/app/`). Written for fenixOS **v0.1.31** (2026-11-18).

The other `docs/fenixos/*.md` files beside this one are the full reference for the app format, design, PC half and registry. This guide sums them up and adds the board, the PC app and the CLI. Where they differ, the fenixOS repo (`tariqaq/fenixOS`) wins: `docs/packages.md`, `docs/sidecars.md`, `docs/registry.md`, `DESIGN.md`.

## 1. The pieces

| Piece | What it is |
|---|---|
| Board | ESP32-2432S028R ("Cheap Yellow Display", CYD) running the fenixOS firmware |
| PC app | `fenixos.exe` (Tauri), in the tray. Finds the board over USB or Wi-Fi, installs apps, starts their PC halves |
| An app | **Board half** (required): a declarative screen, compiled to a `.fxp` package. No code runs on the board. **PC half** (optional): a Windows `.exe` the PC app starts while the app is on; it sets values on the board and gets taps |
| Registry | `registry/` in `tariqaq/fenixOS`: one manifest per app. The files live in each app's own GitHub releases |
| `fenix-pkg` | Package compiler: `layout.json` + `app.toml` + PNGs to `.fxp` |
| `fenix` | Developer CLI: talks to the board directly (install, set values, screenshots, logs) |

Data flow: PC half `set("cpu", 42)` → named pipe → PC app → USB (921,600 baud) or Wi-Fi (TCP 7350) → board redraws the bound element. A tap goes back the same way as an event.

## 2. The board

### Hardware

| Item | Value |
|---|---|
| Board | ESP32-2432S028R, PCB V1.4 (Guition), micro-USB and USB-C (both flash) |
| Chip | ESP32-D0WD-V3 rev 3.1, dual core Xtensa, 240 MHz, 40 MHz crystal (the store listing's "D0WDQ6" is wrong) |
| RAM | 520 KB SRAM. fenixOS heap: ~53 KB free on Home with Wi-Fi on (v0.1.28), ~155 KB with the radio off (measured at P38, before later features) |
| Flash | 4 MB (JEDEC `0xc4`/`0x6016`) |
| Display | 2.8" TFT, ILI9341V, 320 × 240, RGB565 (65K colours), SPI2 at 40 MHz. No TE pin (no vsync) |
| Touch | XPT2046 resistive (a stylus works best; a finger needs a firm press), on its own pins, software SPI |
| USB-UART | CH340C (`USB\VID_1A86&PID_7523`), DTR/RTS auto-reset. Shows as a COM port (COM9 on the dev PC) |
| SD card | microSD on SPI3 at 40 MHz, FAT32, ~2.7 MB/s reads. No card-detect pin |
| Wireless | Wi-Fi 2.4 GHz (Bluetooth is removed from fenixOS) |
| Other | RGB LED (status light), light sensor, speaker amp, BOOT button: fenixOS uses them; apps cannot |
| CN1 | 4-pin JST 1.25 mm socket on the underside: `GND`, `IO22`, `IO27`, `3.3V`. Takes up to two DHT22 sensors (fenixOS 0.1.29, section 6) |
| Power | 5 V USB, ~115 mA |

Pin map (for reference only; apps never drive pins, fenixOS reads wired sensors for them):

| Function | Pins |
|---|---|
| LCD SPI | SCK 14, MOSI 13, MISO 12, CS 15, DC 2, RST = EN |
| Backlight | GPIO21 (PWM, active high) |
| Touch | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| microSD | CS 5, MOSI 23, CLK 18, MISO 19 |
| RGB LED (active low) | 4, 16, 17 |
| Light sensor | 34 (ADC) |
| Speaker | 26 (DAC) |
| Sensors (CN1) | 22, 27: one DHT22 each, read by fenixOS for an app's `[[sensor]]` (IO27 has a 10 kΩ pull-up on the board) |
| Free | 35 (input only, on P3 next to 21 and 22) |

Sources (fenixOS repo): `docs/board-info.md`, `pics/ANALYSIS.md`.

### Flash layout

| Partition | Size | Holds |
|---|---|---|
| nvs | 24 KB | Settings, Wi-Fi network, touch calibration |
| otadata, phy_init, coredump | 8 + 4 + 56 KB | Boot slot choice, radio calibration, last crash |
| ota_0, ota_1 | 1,856 KB each | Firmware (two slots; v0.1.31 uses 1,302,032 B, 31% free) |
| apps | 256 KB | Flash app store (LittleFS), used when no SD card is in |

### What fenixOS runs

- Boot screen, Home (time, date, weather, app tiles), system apps Settings, Session and Stream, and installed apps.
- Up to 32 installed apps, on the SD card when one is in, else in flash (128 KB package cap there).
- Settings › Display › Rotation: landscape or portrait. **Every app layout must work in both.**
- Burn-in protection, network time, a web status page at `http://fenixos.local/` (Wi-Fi mode), and app pages at `/apps/<id>/` (0.1.31, section 6).
- Firmware updates from the PC app, from `fenix ota`, or from a `.fwp` on the card (signed; unsigned is refused).

### SD card (`/sd/fenixos/`)

| Folder | What |
|---|---|
| `apps/` | Installed app packages |
| `data/<id>/` | Your app's own files (PC half `put_file`, max 32 KB each). Deleted with the app. Its `web/` subfolder is served on the network as the app's page (0.1.31) |
| `state/` | Last bound values per app: shown dimmed while the PC is away |
| `fonts/cjk.fxf` | Chinese, Japanese and Korean font (the PC app's Board page puts it there) |
| `logs/`, `crash/`, `shots/` | Board log per boot, crash records, screenshots |

A pulled card is noticed within ~2 s (an open card app returns Home); a card put back is mounted within ~3 s.

## 3. The PC app

- **Install:** `fenixOS_<version>_x64-setup.exe` from the GitHub releases (per user, no admin). Updates itself (Settings › Updates).
- **Location:** `%LOCALAPPDATA%\fenixOS\fenixos.exe`.
- **Pages:** Apps (installed apps, Add app, each app's settings page, Sync with board), Board (mirror, firmware, SD card, screenshot), Task manager (board CPU per core, memory, storage, link, threads), Settings.
- **Logs:** `%LOCALAPPDATA%\fenixOS\logs\fenixos.log`. Your PC half's `log` lines land here; warnings and errors also show under Problems.
- **Settings file:** `%APPDATA%\fenixOS\config.json` (app settings; `secret` values DPAPI-encrypted).
- **Flags:** `--tray` (start without a window), `--quit` (quit the running one).
- **Env vars:** `FENIX_REGISTRY=<folder>` makes Add app list a local registry (for testing your app).
- **Link:** USB first. With no USB board and the board last seen on Wi-Fi, it connects to `fenixos.local` (TCP 7350).

**Only one program can talk to the board at a time.** Quit the PC app before using `fenix` over USB:

```powershell
& "$env:LOCALAPPDATA\fenixOS\fenixos.exe" --quit
# ... fenix commands ...
Start-Process "$env:LOCALAPPDATA\fenixOS\fenixos.exe"
```

## 4. The `fenix` CLI

Not shipped in the installer. Build it from the fenixOS repo (the repo patches `serialport`, see the warning below):

```powershell
git clone --recursive https://github.com/tariqaq/fenixOS
cd fenixOS
git checkout v0.1.31
cargo build --release -p fenix-cli     # target\release\fenix.exe
```

It finds the board's COM port itself. `--port COM9` picks a port; `--port tcp:fenixos.local` goes over Wi-Fi.

| Command | What it does |
|---|---|
| `fenix ports` | List serial ports, marks the board |
| `fenix hello` | Connect, print version, screen size, rotation, heap, apps |
| `fenix logs [s]` | Print the board's log lines live for `s` seconds (default 30) |
| `fenix logs --board [n]` | Read a saved board log from the card |
| `fenix sysinfo` | CPU per core, heap, flash, tasks |
| `fenix list` | Installed apps |
| `fenix install <file.fxp>` | Install or update an app package directly |
| `fenix remove <id>` | Remove an app |
| `fenix set <file.fxp> <bind>=<value>...` | Act as the app's PC half: set bindings (`42`, `true`, `text`; `art=@cover.png` for the bound image). Open the app on the board first |
| `fenix host <id> on\|off\|gone\|starting` | Fake the PC half's state (shows "Turned off on host PC" and so on) |
| `fenix tap <x> <y>`, `fenix swipe <x1> <y1> <x2> <y2>` | Touch the screen; prints the events the board sends back |
| `fenix shot <png> [s]` | Screenshot from the stream |
| `fenix shot --sd [out.bmp]` | Screenshot of the glass, saved on the card |
| `fenix bench [s] [nostream\|fps]` | Animation fps and stream speed |
| `fenix files ls [path]`, `get <path> [out]`, `put <local> <path>`, `rm <path>` | Files under `/sd/fenixos` (write only in `fonts/ data/ web/`) |
| `fenix sd` | SD card state |
| `fenix sensors [s]` | Read IO22 and IO27 as DHT22 for `s` seconds (default 20) and print each reading: tests the wiring, no app needed (0.1.29) |
| `fenix sensors fake <22\|27> <temp> <hum>\|missing\|error\|off` | The board uses these readings for that pin until `off` or a restart: test a layout without hardware |
| `fenix time`, `fenix reboot` | Set the board clock, restart it |
| `fenix radio [off\|wifi]` | Show or switch the radio (restarts the board) |
| `fenix wifi scan\|join <ssid> [pass]\|forget` | Wi-Fi network |
| `fenix ota <file.fwp>` | Firmware update over the link |
| `fenix flash [dir] [--erase]` | Flash a firmware build over USB |

**Warning (CH340):** do not use other serial tools on this board (esptool, serial monitors, terminals). WCH's CH340 driver can crash Windows when a program reads the port with a timeout. `fenix` and the PC app read without one.

## 5. App repo (template)

Unzip `fenixos-app-template.zip` into a new repo under `github.com/tariqaq/` (the registry only accepts downloads from there).

| Path | What |
|---|---|
| `AGENTS.md`, `CLAUDE.md` | Rules for AI agents |
| `docs/fenixos/` | `dev-guide.md` (this file), `app-format.md`, `design.md`, `pc-half.md`, `registry.md` |
| `design/tokens.json` | Design tokens |
| `manifest.json` | Registry manifest: name, colour, permissions, settings |
| `app/board/` | `app.toml`, `layout.json`, `icon.png` |
| `app/pc/` | PC half (Rust, `fenix-sdk`). Delete it, the root `Cargo.toml` and `"pc"` in the manifest if you need no PC half |
| `tools/build.ps1`, `tools/app.py` | Build, check, local registry, release |
| `.github/workflows/release.yml` | Tag to GitHub release |

Tools: Rust (stable), Python 3.11+, and `fenix-pkg`:

```powershell
cargo install --git https://github.com/tariqaq/fenixOS --tag v0.1.31 fenix-pkg
```

## 6. Board half

### `app.toml`

```toml
id = "io.github.tariqaq.myapp"   # letters, digits, . - _; <= 48 bytes; equals manifest.json id
name = "My app"                  # tile label, <= 24 bytes
version = "0.1.0"                # equals manifest.json version
min_fenixos = "0.1.0"            # lowest fenixOS that may install it (0.1.25 for view/show, 0.1.26 for overflow and Latin-1, 0.1.29 for [[sensor]], 0.1.31 for <bind>_time)
color = "teal"                   # gray indigo blue teal green orange red purple pink
icon = "icon.png"                # white glyph on transparent; scaled to 20 px and 26 px

[[sensor]]                       # optional, fenixOS 0.1.29: a DHT22 the board reads itself
kind = "dht22"                   # the only kind so far
pin = 22                         # 22 or 27 (CN1); one sensor per pin
bind = "s1"                      # prefix of the six bindings the board sets (letters, digits, _; <= 16)
```

### Wired sensors (`[[sensor]]`, fenixOS 0.1.29)

A DHT22 temperature and humidity sensor on CN1 (the 4-pin JST 1.25 mm socket: `GND`, `IO22`, `IO27`, `3.3V`), one per pin. Declare it in `app.toml` (above) with `min_fenixos = "0.1.29"`, and add `"permissions": ["sensors"]` to `manifest.json` (the PC app refuses the package otherwise). The board reads it every 2 s while the app is open, or while its PC half runs with the board connected, and sets six bindings itself:

| Binding | Kind | Value |
|---|---|---|
| `<bind>_temp` | int | Tenths of °C (-400 to 800), for a `bar` |
| `<bind>_temp_text` | text | `23.4 °C`; `--` unless live |
| `<bind>_hum` | int | Tenths of % (0 to 1000), for a `bar` |
| `<bind>_hum_text` | text | `45.2 %`; `--` unless live |
| `<bind>_ok` | bool | True while live: use it as `show` on the values |
| `<bind>_state` | text | `Reading` (no result yet), `Live`, `Not connected` (no answer 3 reads in a row, ~6 s), `Read error` (bad answers 3 reads in a row) |
| `<bind>_time` | text | fenixOS 0.1.31: local time of the last good read, `14:03:27`, set on every good read (every 2 s while live); held while not live; `--:--:--` before the first good read or while the board's clock is not set. Generated only with `min_fenixos = "0.1.31"` or newer (an older `min_fenixos` refuses it in the layout) |

- They count toward the 64 bindings. A layout binding under the prefix that is not one of the six is refused (`s1_temperature`), and `fenix-pkg inspect` lists them as set by the board. Do not `set` them from the PC half.
- Without a PC link the layout stays live (no dimming), under a "PC not connected" line; PC-set values show their last value. "Turned off on host PC", "Starting" and "Not on this PC" show as for any app (no readings there).
- The PC half gets every reading as `Message::Sensor` (section 7), whatever the board shows.
- Test without hardware: `fenix sensors fake 22 23.4 45.2` (or `missing`, `error`, `off`); test the wiring: `fenix sensors`.
- Example: `tools/sensor-app` in the fenixOS repo.

### App web page (fenixOS 0.1.31)

In Wi-Fi mode, with the SD card in, the board serves the files your PC half writes under `web/` as a page any device on the same network can open: `put_file("web/index.html", ...)` serves at `http://fenixos.local/apps/<id>/`, `web/app.js` at `/apps/<id>/app.js`. Only `web/` is served; the rest of `data/<id>/` stays private. 32 KB per file: split a bigger page. The page goes when the app is removed.

- **Public:** anyone on the network can read it, without a password. Put only public things in `web/`: no settings, tokens or personal data.
- **Live sensors:** the page can poll `GET /sensors.json` (once a second is fine): `{"time": "14:03:28", "pc": true, "sensors": [{"pin": 22, "kind": "dht22", "state": "ok", "label": "Live", "temp": 23.4, "hum": 45.2, "read": "14:03:27", "age": 1, "apps": [{"id": "<id>", "bind": "s1"}]}]}`. One entry per pin some installed app declares; `state` is `reading`, `ok`, `missing` or `error` (`label` is the `<bind>_state` text); `temp` and `hum` are the last good values (`null` before one); `read` is the last good read's local time (`""` without a clock); `age` the seconds since it (right without a clock). Each request keeps the sensors read for 10 s, with or without the app open or a PC; after a pause the first answers say `reading` for up to 2 s.
- `GET /apps.json` lists the installed apps with a page (`[{"id", "name", "color"}]`); the board's own page at `http://fenixos.local/` links them. `GET /status.json` has the board's state.
- Test without a PC half: `fenix files put index.html data/<id>/web/index.html`.
- Not in USB-only mode, and not without a card.

### Screen area

The app sits under a 32 px nav bar ("< Home" and the app name). The area scrolls when the layout is taller.

| Rotation | Area (w × h) | Side margin |
|---|---|---|
| Landscape | 320 × 208 | 12 px |
| Portrait | 240 × 288 | 10 px |

Root box: `"w": "100%", "pad": 12, "portrait": { "pad": 10 }`.

### Elements (`layout.json`, one root)

| `type` | What | Own props |
|---|---|---|
| `box` | Flex container | `flow` (`column`, `row`, `row_wrap`), `gap`, `pad`, `main`, `cross` (`start` `center` `end` `space_between`), `bg`, `radius` |
| `label` | Text, or a bound value | `text`, `font`, `color`, `align`, `overflow` (`wrap` `ellipsis` `clip` `scroll`), `max_lines` (1 to 8) |
| `button` | Capsule button (icon-only with `icon` and no text) | `text`, `icon`, `bg`, `color`, `event` |
| `switch` | On/off | `bind`, `event`, `value` |
| `slider` | Horizontal slider | `min`, `max`, `value`, `bind`, `event` |
| `bar` | Progress or level bar | `min`, `max`, `value`, `bind`, `color` |
| `image` | PNG as A8 (tinted) or RGB565. One per app may be bound (the PC sends pixels, 1 to 96 px per side) | `src`, `color`, `bind` |
| `icon` | Built-in glyph | `icon`, `size` (12 14 15 16 18 20 26 28 40), `color` |
| `view` | Shows one child, picked by its int `bind` (several screens in one app) | `bind`, `value`, and the `box` props |

Props on every element: `w`, `h` (px, `"50%"`, `"content"`), `grow`, `bind`, `event`, `hidden`, `show` (a binding: shown while true or non-zero), `portrait` (prop overrides in portrait).

**Fonts:** `body` (15, default), `body_semibold`, `label` (13), `label_medium`, `label_semibold`, `caption` (12), `caption_medium`, `title` (17), `title_l` (22), `clock_56`, `clock_64`. Inter: printable ASCII, Latin-1, `– — ’ • … ‹ ›`; CJK when the card font is in. Anything else (emoji) shows blank: use `fenix_sdk::text::fit`. Clock fonts: digits, `:` `-` and space only.

**Colours (tokens):** `bg` #000000, `surface_1` #1C1C1E (cards), `surface_2` #2C2C2E (secondary buttons), `surface_3` #3A3A3C (tracks), `separator` #38383A, `label` #FFFFFF, `label_2` #98989F, `accent` #0A84FF, `accent_fill` #0071E3, `success` #30D158, `warning` #FF9F0A, `danger` #FF453A, `danger_fill` #C9302C, `tile_*`.

**Icons:** `gear-six`, `power`, `screencast`, `arrow-clockwise`, `plugs-connected`, `plugs`, `sun`, `moon`, `timer`, `device-rotate`, `hand-tap`, `hard-drives`, `desktop-tower`, `info`, `squares-four`, `warning`, `puzzle-piece`, `check-circle`, `x-circle`, `monitor`; from 0.1.25: `play`, `pause`, `skip-forward`, `skip-back`, `speaker-high`, `speaker-x`, `microphone`, `microphone-slash`, `headphones`, `game-controller`, `music-notes`, `video`, `phone`, `sparkle`.

### Bindings and events

- `"bind": "cpu"`: the PC half sets it. A label shows text, an int, or On/Off; a switch takes a bool; slider and bar an int; a button text or an icon index; an `icon` an icon index; a `view` the child index.
- `"event": "mute"`: a button sends `tap` and `long`; a switch `change` 0/1; a slider `change` with its value on release.
- The board keeps no live values while the app is closed. On `opened`, the PC half sends all its values (~50 at once is fine).
- A label's `text` shows until the first value arrives: use `"--"`.

### Limits (`fenix-pkg build` checks them)

| What | Limit |
|---|---|
| Elements | 200 |
| Strings | 8 KB |
| Bindings | 64 (a sensor adds 6, 7 from `min_fenixos` 0.1.31) |
| Image data | 24 KB (A8 = 1 B/px, RGB565 = 2 B/px) |
| RAM estimate | 32 KB (elements built at once × 160 B + elements × 32 B + strings + images + bound image w × h × 2) |
| Tree depth | 8 |
| Package on flash (no card) | 128 KB |

The app slides open like Settings when the heap allows; a big layout fades in from black instead.

### States the board draws for you

"Waiting for host PC" (or the last values dimmed under "PC not connected" with a card; an app with sensors stays live), "Insert the SD card", "Turned off on host PC" with Turn on, "Starting on host PC", "Not on this PC". Do not build these yourself.

### Design rules (locked)

Dark only. Cards: `surface_1`, radius 12, pad 10, never nested. Buttons: primary `accent_fill` (one per screen), secondary `surface_2`, destructive `danger_fill`. Touch targets at least 40 px tall. Sentence case, few words. No shadows, gradients, custom fonts or new colours. Full rules: `docs/fenixos/design.md`.

## 7. PC half (optional)

`app/pc/Cargo.toml`:

```toml
fenix-sdk = { git = "https://github.com/tariqaq/fenixOS", tag = "v0.1.31" }
```

| API | What it does |
|---|---|
| `App::connect()` | Connects over `--fenix-pipe`; waits for `hello` and settings. Fails when started by hand |
| `app.next(timeout)` | Next `Message` or `None` |
| `app.is_open()` / `app.board_connected()` | The app's screen is open / the board link is up |
| `app.settings()` | Current settings (key → JSON) |
| `app.set(bind, value)` | `i32`, `bool` or text (≤ 255 B). Dropped while closed |
| `app.set_icon(bind, "pause")` | Built-in icon on a bound `icon` or button |
| `app.set_image(bind, w, h, &rgba)` | Picture for the bound `image` (~0.25 s for 96 × 96 over USB). Once per change |
| `fenix_sdk::text::fit(s, cjk)` | Make outside text drawable (transliterates the rest) |
| `app.log(Level::Warn, text)`, `app.log_info(text)` | Line in the PC app log |
| `app.put_file`, `get_file`, `delete_file` | Files in `/sd/fenixos/data/<id>/`, ≤ 32 KB, block until the board answers. Files under `web/` are public: the app's page (0.1.31, section 6) |

`Message`: `Settings`, `Board { connected }`, `Opened`, `Closed`, `Event { name, kind, value }` (`Tap`, `Long`, `Change`), `Button { key }`, `Sensor { bind, kind, temp, hum, state, at }` (0.1.29: every 2 s per sensor; `temp` °C and `hum` % are `None` unless `state` is `SensorState::Ok`; `at` Unix ms on the PC; `app.sensors()` keeps the last per `bind`), `Stop`.

Lifecycle: started as `<exe> --fenix-pipe <base>` while the app is on; exit on `Stop` or a closed pipe. Crashes restart after 1, 5, 15 s, then it stops. Build as a GUI exe (`#![windows_subsystem = "windows"]`). Pattern and the wire protocol for other languages: `docs/fenixos/pc-half.md`.

Permissions (`manifest.json`): `keys`, `media`, `system-stats`, `network`, `files`, `programs`, `screen`, `sensors` (0.1.29: required with `[[sensor]]`). Declare only what it uses. No hooks, injection, other processes' memory or input capture (users run anti-cheat games).

## 8. Manifest and settings

`manifest.json`: `id`, `name`, `description` (≤ 100 B), `version`, `min_fenixos`, `color`, `icon` (Phosphor name for the PC tile), `repo`, `board: {}`, `pc: { "elevated": false }`, `permissions`, `settings`. `board` and `pc` are filled by `tools/app.py`.

Settings the PC app draws on the app's page: `toggle`, `slider` (`min`, `max`, `step`, `unit`), `choice` (`options`), `text` (`max_len`), `secret` (masked, encrypted, needs `min_fenixos` 0.1.26), `button` (sends `Button { key }`). Never build a settings UI yourself.

## 9. Develop, test, release

Fast loop, board half only (PC app quit):

```powershell
fenix-pkg build app/board -o dist/app.fxp
fenix install dist/app.fxp                # open the app on the board
fenix set dist/app.fxp cpu=42 cpu_text="42 %"
fenix tap 160 180                         # prints the event
fenix shot shot.png
```

Rotate the board (Settings › Display › Rotation) and check both layouts.

Full loop through the PC app, with the PC half:

```powershell
pwsh tools/build.ps1                      # dist/app.fxp, dist/app.exe
python tools/app.py check
python tools/app.py local                 # prints dist/registry
& "$env:LOCALAPPDATA\fenixOS\fenixos.exe" --quit
$env:FENIX_REGISTRY = "$PWD\dist\registry"; & "$env:LOCALAPPDATA\fenixOS\fenixos.exe"
```

Then Apps › Add app, add it, switch it on. Watch `%LOCALAPPDATA%\fenixOS\logs\fenixos.log`.

Release:

1. Bump `version` in `manifest.json` and `app/board/app.toml`.
2. Commit, tag `v<version>`, push the tag. The workflow attaches `app.fxp` and `app.exe` and uploads the filled manifest.
3. PR on `tariqaq/fenixOS`: add it as `registry/apps/<id>.json`, run `python tools/registry.py`, commit both.

## 10. Gotchas

- Quit the PC app before `fenix` over USB; only one program can hold the port.
- `fenix-pkg inspect dist/app.fxp` shows bindings and the RAM estimate.
- A bound label without `w` grows and gets cut at the screen edge; give it a width.
- `grow` works only when the parent has a set size along its flow.
- `accent` text on `surface_2` is too low contrast; use `label`.
- Wi-Fi mode leaves ~53 KB heap: keep layouts lean, prefer A8 images.
- No PC half runs while the PC is off; with a card in, the board shows the last values dimmed.
