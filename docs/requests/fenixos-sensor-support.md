# Request: wired sensor support in fenixOS (DHT22 on the CYD)

Written for the fenixOS repo (`tariqaq/fenixOS`) and the agent working in it. It describes the firmware, package, protocol, SDK, PC app and tooling changes needed so an app can show readings from sensors wired to the board, and send those readings to its PC half. The requesting app is `io.github.tariqaq.sensors` (repo `tariqaq/sensors`).

The design below is a proposal. Where it conflicts with how fenixOS is built, keep the goals in section 1 and adapt the mechanism. Write down any changes in the docs so the app side can follow them.

> **Status: implemented in fenixOS v0.1.29 (P59), state rule fixed in v0.1.30 (P60), 2026-11-18.** Pin `fenix-sdk` and `fenix-pkg` to `v0.1.30` and set `min_fenixos = "0.1.29"`. The authoritative reference is now `docs/packages.md` "Sensors", `docs/sidecars.md` (`sensor` message), `proto/spec.md` "Sensors (0x7_)" and the template's `docs/fenixos/dev-guide.md` section 6 (re-copy the template docs). Tested on the real board with two DHT22s on IO22 and IO27. Where it differs from this request:
>
> - `app.toml` `[[sensor]]` (`kind`, `pin`, `bind`), the six bindings, the `sensors` permission (required; the PC app refuses the package without it) and `Message::Sensor` / `SensorState` / `app.sensors()` are as asked in sections 3.5, 4, 5 and 6.
> - `<bind>_state` also has `"Reading"` before the first result (a good read, or 3 failed ones).
> - `°` is written as UTF-8 (`\xC2\xB0`): the fonts hold U+00B0, and LVGL takes UTF-8.
> - Missing means the last 3 reads got no answer at all; any other 3 failures in a row are error (v0.1.30).
> - The board sends one `SENSOR` frame per pin, not per app: the PC app knows each app's pins from its package and gives every switched-on app with the permission its own `sensor` message.
> - PC away: the layout stays live, touchable and undimmed under "PC not connected" (PC-set values show their last value). "Turned off on host PC", "Starting" and "Not on this PC" keep their screens and show no readings (decision for 3.6).
> - `fenix sensors fake` lasts until `fenix sensors fake <pin> off` or a board restart (it has to outlive the CLI's session).
> - Cost: 3.8 KB of board heap with both pins declared, 0.1% CPU.
> - fenixOS's own test fixture `tools/sensor-app` (`io.github.tariqaq.sensortest`) is not the app; build the real one in `tariqaq/sensors`.

## 1. Goal

The user wires two DHT22 temperature and humidity sensors to the board. The app should then:

1. **Board:** show live temperature and humidity for both sensors while the app is open, updating every ~2 s. The readings should stay live when the PC is off or unplugged, because the board reads the sensors itself.
2. **PC:** while the app is switched on in the PC app and the board is connected, receive every reading in its PC half. That lets it log readings to a file, keep history, and show them.
3. **Errors:** show a clear per-sensor state when a sensor is missing, unplugged or failing its checksum, instead of a frozen value.

Today none of this is possible. App packages are declarative, and the docs say apps never touch pins (`docs/board-info.md`, dev guide section 2).

## 2. Hardware facts

| Item | Value |
|---|---|
| Board | ESP32-2432S028R, PCB V1.4 (Guition) |
| Connector used | **CN1** (4-pin JST 1.25 mm): `GND`, `IO22`, `IO27`, `3.3V` |
| Sensor 1 data | **GPIO22** |
| Sensor 2 data | **GPIO27** |
| Sensor power | 3.3 V from CN1 (DHT22 runs on 3.3–6 V) |
| Pull-up | 10 kΩ data-to-3.3V recommended. Many DHT22 modules have one on board. The firmware should also enable the ESP32 internal pull-up (~45 kΩ) as a fallback |

Pins to exclude:

- **GPIO35**: input only. It can't drive the DHT22 start pulse.
- **GPIO21**: backlight (also on connector P3).
- Every other pin is used by the LCD, touch, SD, LED, light sensor or speaker.
- GPIO22 also appears on P3. Treat it as one pin.

The **allowed sensor pins are therefore 22 and 27 only**.

DHT22 (AM2302) protocol notes:

- Single wire, not addressable: **one sensor per pin**.
- The host pulls the line low for at least 1 ms (1–10 ms), then releases it. The sensor answers with an 80 µs low and an 80 µs high, then 40 bits. Each bit is a 50 µs low followed by a high of ~26–28 µs (0) or ~70 µs (1).
- Data: 16-bit humidity ×10, 16-bit temperature ×10 (the top bit is the sign), 8-bit checksum.
- Minimum interval between reads of one sensor: **2 s**.
- Range: −40 to 80 °C, 0 to 100 % RH, resolution 0.1.

## 3. Firmware changes

### 3.1 Sensor driver

- Add a `sensors` module or task that owns the DHT22 reads.
- **Use the RMT peripheral** in RX mode, or an equivalent hardware-timed capture, to measure the pulse train. Bit-banging with busy-waits gets corrupted by Wi-Fi, SPI display and touch interrupts. Disabling interrupts for ~5 ms per read would stall the display stream and the radio.
- Read each active sensor every **2 s**. Stagger the sensors (sensor 2 reads ~1 s after sensor 1) so reads never overlap.
- Validate the checksum and the ranges. On failure, retry once on the next cycle.
- Per-sensor state machine:
  - `ok`: last read valid.
  - `error`: checksum or timing failures for 3 reads in a row.
  - `missing`: no response at all (no sensor) for 3 reads in a row.
  - Report `ok` again on the next valid read.
- Keep the latest `{temp_x10: i16, hum_x10: u16, state, age_ms}` per sensor.
- Run on the core and priority that keep the UI smooth. Report heap and CPU cost in `fenix sysinfo`.

### 3.2 Pin ownership

- The firmware configures a pin only while an installed app declares a sensor on it. Otherwise the pin stays as now (unconfigured, input, no pull).
- One driver instance per pin, shared by every app that declares the same pin with the same `kind`.
- Two apps declaring the same pin with **different** kinds: refuse the second install with a clear error, both in `fenix install` and in the PC app.
- Pins outside the allowed set (22, 27) are refused by `fenix-pkg build` and again by the board before install.

### 3.3 When to read

Read a declared sensor while either condition holds:

- the declaring app's screen is open on the board, **or**
- the declaring app's PC half is running and the PC link is up (the board already knows host state: see `fenix host <id> on|off|gone|starting`).

Stop reading when neither holds, to save CPU and power.

### 3.4 Board-side bindings

- The board sets the generated bindings (3.5) itself, from the driver, without a PC round trip.
- They behave like PC-set bindings: labels, bars and `show` work unchanged. Values for the screen shown in a `view` are applied when it shows.
- Update the bindings only when a value or state changes.
- Format text values on the board (3.5). Write `°` as Latin-1 0xB0, which the fonts already hold (fenixOS 0.1.26+).

### 3.5 Generated bindings

For each declared sensor with `bind = "<b>"`:

| Binding | Type | Value |
|---|---|---|
| `<b>_temp` | int | Temperature in tenths of °C (−400…800), for `bar` |
| `<b>_temp_text` | text | `"23.4 °C"`; `"--"` when not `ok` |
| `<b>_hum` | int | Humidity in tenths of % (0…1000), for `bar` |
| `<b>_hum_text` | text | `"45.2 %"`; `"--"` when not `ok` |
| `<b>_ok` | bool | true while state is `ok` (for `show`) |
| `<b>_state` | text | `"Live"`, `"Not connected"` or `"Read error"` |

These count toward the 64-binding limit. `fenix-pkg build` and `fenix-pkg inspect` must list them as board-sourced. Optional: have `fenix-pkg build` warn when a layout binding is never set by either side.

### 3.6 PC-away behaviour (important)

Today, with no PC link, an app with a PC half shows "Waiting for host PC", or with a card in, the last values dimmed under "PC not connected". For an app that declares sensors:

- PC link down, app switched on: show the **layout live**. Sensor-bound elements are at full brightness and keep updating. PC-bound elements follow the existing card behaviour (last value dimmed, or the placeholder). A small "PC not connected" note is fine, as long as it doesn't cover the readings.
- App switched off in the PC app ("Turned off on host PC"): keep the existing behaviour. Whether sensors should still show there is a fenixOS design call: decide it and document it.
- Board-only apps (no `pc` part) that declare sensors: always show the layout live.

### 3.7 Upstream to the PC

- Add a board-to-PC frame, for example `SENSOR {app_id, bind, temp_x10, hum_x10, state, uptime_ms}`. Send it after every read cycle for every sensor of an app whose PC half is running.
- When several apps declare the same pin, send one frame per app.
- Bandwidth is tiny (2 sensors / 2 s) but shares the link with the screen stream. Keep the frame compact.
- Over Wi-Fi (TCP 7350) it should work the same.

## 4. Package format (`.fxp`) and `fenix-pkg`

New optional table array in `app.toml`:

```toml
[[sensor]]
kind = "dht22"   # only "dht22" for now; keep the field so more kinds can follow (dht11, ds18b20, ...)
pin  = 22        # 22 or 27
bind = "s1"      # prefix of the generated bindings (section 3.5); letters, digits, _

[[sensor]]
kind = "dht22"
pin  = 27
bind = "s2"
```

Rules for `fenix-pkg build`:

- `kind` must be known, `pin` must be 22 or 27, and each pin appears once per app.
- `bind` prefixes must be unique and must not clash with other bindings.
- Any `[[sensor]]` requires `min_fenixos` ≥ the release that adds this. Give the same kind of error as for `view`/`show`.
- Store the declaration in the package so the board and the PC app can read it before install.
- `fenix-pkg inspect`: list the sensors, pins and generated bindings.
- Bump the package format version only if older boards would otherwise misread it. Older boards must refuse the package cleanly, not install it and show dead values.

## 5. Pipe protocol and `fenix-sdk`

New down message to the PC half:

```json
{ "type": "sensor", "bind": "s1", "kind": "dht22", "temp": 23.4, "hum": 45.2, "state": "ok", "at": 1731923400123 }
```

- `temp` is in °C and `hum` in %, as floats. Both are `null` when `state` is not `"ok"`.
- `state` is `"ok"`, `"missing"` or `"error"`.
- `at` is the PC's receive time in Unix ms. The board has no reliable clock when offline, so stamp on the PC side.

`fenix-sdk` (Rust):

```rust
Message::Sensor { bind: String, kind: String, temp: Option<f32>, hum: Option<f32>, state: SensorState, at: u64 }
pub enum SensorState { Ok, Missing, Error }
```

- Older PC halves ignore unknown message types (already the rule).
- Document it in `docs/sidecars.md`, including the "other languages" wire list.
- Optional: `app.sensors()` returns the last reading per `bind`, so a PC half that starts late gets current values without waiting for a cycle.

## 6. PC app, manifest and registry

- New permission **`sensors`**: "Reads sensors wired to the board". Add it to `PERMISSIONS`, with an entry in `PERMISSION_SINCE` for the new version, in `tools/registry.py`. The app repo copies the rule into `tools/app.py`.
- On install, the PC app checks that a package declaring `[[sensor]]` has the `sensors` permission in its manifest, and refuses a mismatch.
- Show the pins on the app page, for example "Sensors: DHT22 on IO22, DHT22 on IO27", so the user knows where to wire them.
- Route `SENSOR` frames to the matching app's PC half as `sensor` pipe messages.
- Optional: show the live readings in the Task manager or Board page for debugging.

## 7. `fenix` CLI

- `fenix sensors [s]`: print live readings and states of all active sensors for `s` seconds. This tests the wiring without any app logic.
- `fenix sensors fake <pin> <temp> <hum> | missing | error`: inject readings into the driver so layouts can be tested without hardware. Make it debug-only, or clearly marked.
- `fenix sysinfo`: include the sensor task.

## 8. Docs to update in fenixOS

- `docs/board-info.md`: CN1 pinout, allowed sensor pins, pull-up note, why 35 and 21 are excluded.
- `docs/packages.md`: `[[sensor]]`, generated bindings, PC-away behaviour, limits.
- `docs/sidecars.md`: `sensor` message, `Message::Sensor`.
- `docs/registry.md`: `sensors` permission.
- `DESIGN.md`, if a new pattern is needed (for example a sensor card with its state row).
- The app template copies under `templates/app/` (`dev-guide.md`, `app-format.md`, `pc-half.md`, `registry.md`). The app repo re-copies them; it doesn't patch them.

## 9. What the app will do with it (for context)

Board layout (both rotations): two value cards in a card row (a column in portrait), one per sensor:

- Label "Sensor 1" (`label_2`).
- `s1_temp_text` in `title_l`, with a `bar` on `s1_temp`.
- `s1_hum_text` with a `bar` on `s1_hum`.
- A state row (icon plus `s1_state`, shown in place of the values when `s1_ok` is false).
- The same for `s2`.

PC half:

- Appends every `Message::Sensor` to a CSV (time, sensor, temp, hum, state).
- Settings: log interval, a log on/off toggle, and a button that opens the log folder.
- Optional: min/max today, sent to the board through ordinary PC-set bindings.

`app.toml` will declare the two `[[sensor]]` blocks from section 4, and `manifest.json` will add `"permissions": ["sensors"]`.

## 10. Acceptance criteria

1. With two DHT22s on IO22 and IO27, opening the app shows both temperatures and humidities, updating about every 2 s, in landscape and portrait.
2. Unplug the USB data link (keep power) or quit the PC app: the readings keep updating on the board.
3. Unplug one sensor: within ~6 s that sensor shows "Not connected" and `--`. Plug it back in and it goes live again without reopening the app.
4. With the PC app running and the app switched on, the PC half receives a `sensor` message per sensor per cycle, including while the board is showing Home.
5. No visible stutter in the board UI or the screen stream while sensors are read, over both USB and Wi-Fi. Heap and CPU cost stay within what `fenix sysinfo` shows as safe.
6. `fenix-pkg build` refuses pin 35 or 21, duplicate pins, unknown kinds, and a `min_fenixos` that is too low. An older board refuses the package cleanly.
7. Installing a package with `[[sensor]]` but without the `sensors` permission is refused by the PC app.
8. `fenix sensors` and `fenix sensors fake` work.
9. Docs and template copies are updated, and a fenixOS release is tagged so the app can pin `fenix-sdk`/`fenix-pkg` to it.

## 11. Fallback (if section 3.4/3.6 is too much for one release)

A smaller first step:

- Ship only the driver (3.1–3.3), the upstream frame (3.7) and the SDK message (5).
- The PC half receives readings and `set`s them back onto ordinary bindings.

Cost: the board shows live readings only while the PC is connected (dimmed last values otherwise), and every reading takes a PC round trip. The `app.toml` declaration (4) is still needed so the board knows which pins to read. The full design can follow in a later release without breaking the app.
