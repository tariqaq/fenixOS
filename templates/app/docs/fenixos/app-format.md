# Board half: app package (`.fxp`)

Copied from fenixOS `docs/packages.md` (package format 1), cut to what an app author needs. Compiler: `fenix-pkg build app/board -o dist/app.fxp`; `fenix-pkg inspect dist/app.fxp` lists a package.

No code runs on the board. The package is a declarative screen: elements, bindings (values the PC half sets, or the board itself for wired sensors) and events (touches the PC half gets).

## Source folder (`app/board/`)

`app.toml`:

```toml
id = "io.github.tariqaq.myapp"   # letters, digits, . - _; <= 48 bytes; equals manifest.json id
name = "My app"                  # tile label, <= 24 bytes
version = "0.1.0"                # equals manifest.json version
min_fenixos = "0.1.0"            # lowest fenixOS that may install it
color = "teal"                   # tile token (gray indigo blue teal green orange red purple pink) or "#RRGGBB"
icon = "icon.png"                # any size; alpha channel used, drawn white; scaled to 20 px (Tiles) and 26 px (Dock)

[[sensor]]                       # optional, fenixOS 0.1.29: a DHT22 the board reads itself
kind = "dht22"                   # the only kind so far
pin = 22                         # 22 or 27 (CN1); one sensor per pin
bind = "s1"                      # prefix of the six bindings the board sets (letters, digits, _; <= 16)
```

`layout.json`: one root element. `image` elements name PNG files in the same folder.

## Screen area

The board shows the app under a 32 px nav bar ("< Home" and the app name). The area below scrolls vertically when the layout is taller.

| Rotation | Area (w × h) | Side margin to use |
|---|---|---|
| Landscape | 320 × 208 | 12 px |
| Portrait | 240 × 288 | 10 px |

The area has no padding of its own: give the root box `"w": "100%", "pad": 12` and `"portrait": { "pad": 10 }`. Rotation is the user's choice, so every layout must work in both.

## Elements

Every element is an object with `type`, optional props and `children` (boxes and views only).

| `type` | What it is | Own props |
|---|---|---|
| `box` | Flex container | `flow` (`column` default, `row`, `row_wrap`), `gap`, `pad`, `main`, `cross` (`start` `center` `end` `space_between`), `bg`, `radius` |
| `label` | Text; with `bind`, a value label | `text`, `font`, `color`, `align` (`left` `center` `right`), `overflow`, `max_lines` |
| `button` | Capsule button; with `icon` and no text, an icon button | `text`, `icon`, `bg`, `color`, `event` |
| `switch` | On/off switch | `bind`, `event`, `value` |
| `slider` | Horizontal slider | `min`, `max`, `value`, `bind`, `event` |
| `bar` | Progress or meter bar | `min`, `max`, `value`, `bind`, `color` |
| `image` | A8 (tinted with `color`) or RGB565 image. Since v0.1.25 one `image` may have a `bind`: the PC half sends its pixels (album art); it needs `w` and `h` in pixels (1 to 96, the same in portrait), shows `src` or a `surface_2` square until the first picture arrives, and its buffer (w × h × 2 B) counts in the RAM estimate | `src`, `color` (A8 only), `bind` |
| `icon` | Built-in glyph | `icon`, `size` (12 14 15 16 18 20 26 28 40), `color` |
| `view` | Since v0.1.25: a container that shows one child, picked by its int `bind` (0 = first child; out of range shows none), or by `value` until the PC sets it. Lays out like a `box` | `bind`, `value`, and the `box` props |

Props every element takes:

- `w`, `h`: pixels, `"50%"` or `"content"`. Boxes default to their content size.
- `grow`: share of the parent's free space along its flow. Applies only when the parent has a set `h` (column) or `w` (row).
- `bind`: a binding name. `event`: an event name. `hidden`: true hides it.
- `show` (fenixOS v0.1.25): a binding name; the element shows while that value is true or non-zero, and follows `hidden` until the PC half sets it.
- `overflow` (labels, fenixOS v0.1.26; older boards wrap): `wrap` (default), `ellipsis` or `clip` after `max_lines` lines (1 to 8, default 1), or `scroll` (one line scrolling round, for titles). Needs a width: `w` (not `"content"`) or `grow`.
- `portrait`: an object with any of these props, used instead when the board is in portrait (for example `{ "flow": "column", "w": "100%" }`).

Defaults: buttons 40 px tall (icon buttons 40 × 40), button background `accent`, label colour `label`, bar colour `accent`, switch on colour `success`, slider and bar track `surface_3`.

### Fonts

`body` (15, default), `body_semibold`, `label` (13), `label_medium`, `label_semibold`, `caption` (12), `caption_medium`, `title` (17), `title_l` (22), `clock_56`, `clock_64`. Inter only. Text fonts hold printable ASCII, Latin-1 (accented letters such as `é ñ ü Å ß`, `° · ×`; fenixOS 0.1.26 and newer, older boards show them as blanks) and `– — ’ • … ‹ ›`; Chinese, Japanese and Korean show when the user has put the CJK font on the board's SD card. Anything else is a blank: transliterate it in the PC half (`fenix_sdk::text::fit`). Clock fonts hold digits, `:` `-` and space.

### Colours

A token from `design/tokens.json` with `_` for `-`: `label`, `label_2`, `accent`, `accent_fill`, `success`, `warning`, `danger`, `danger_fill`, `surface_1`, `surface_2`, `surface_3`, `separator`, `bg`, `tile_gray` … `tile_pink`. `"#RRGGBB"` is accepted but breaks the design language: use tokens.

### Icons

The built-in glyphs (the last 14, from `play`, need fenixOS v0.1.25): `gear-six`, `power`, `screencast`, `arrow-clockwise`, `plugs-connected`, `plugs`, `sun`, `moon`, `timer`, `device-rotate`, `hand-tap`, `hard-drives`, `desktop-tower`, `info`, `squares-four`, `warning`, `puzzle-piece`, `check-circle`, `x-circle`, `monitor`, `play`, `pause`, `skip-forward`, `skip-back`, `speaker-high`, `speaker-x`, `microphone`, `microphone-slash`, `headphones`, `game-controller`, `music-notes`, `video`, `phone`, `sparkle`. Other artwork goes in as an `image` (draw it from Phosphor, fill weight, to match).

## Bindings and events

- `"bind": "cpu"` connects the element to binding `cpu`. The PC half sets it: a label shows text, an integer, or "On" / "Off" for a bool; a switch takes a bool, a slider or bar an integer. Since v0.1.25 (P50-M) a `button` takes text (its text) or an integer (its glyph, an index in the icon list; out of range: none), and an `icon` takes an integer (its glyph); `fenix-sdk` `set_icon(bind, "pause")` sends the index. Use one kind per button: after a rebuild only the last value is applied.
- Sending every value on `opened` (~50) is fine from fenixOS v0.1.25: the PC app sends the latest value per binding, paced, and the board keeps them in order. Before v0.1.25 the board kept only 16 at a time.
- **Several screens in one app** (fenixOS v0.1.25): put each screen in a `view` and set its binding to the index of the one to show (`app.set("mode", 2)`). Only the screen shown is built (switching builds the next one), so the RAM estimate counts the largest screen; values for the other screens are kept and applied when they show. What appears fades in; no motion props. `view` or `show` needs `min_fenixos = "0.1.25"` in `app.toml` (`fenix-pkg build` says so).
- **Pictures from the PC half (fenixOS v0.1.25):** one `image` per app may have a `bind`. The PC half calls `set_image(bind, w, h, &rgba)` with any picture (the SDK scales it to at most 120 × 120); fenixOS scales it to fill the element, crops the middle, converts it to RGB565 and sends it in `BIND_IMAGE` chunks, about 3 KB per 50 ms (a 96 × 96 picture takes ~0.25 s over USB). The board writes them into the element's one buffer and redraws it once the picture is whole, so a redraw during the transfer can show the old and new picture mixed for that moment. Send it once per change (a new track), not per frame; a newer picture replaces one still on its way. Not kept on the card: after a restart or offline the element shows `src` again.
- A bound label without `w` grows with its text and is cut off at the screen edge; with `w` it wraps onto as many lines as needed.
- `"event": "mute"` makes the element report: a button sends `tap` (and `long` on a long press), a switch sends `change` with 0/1, a slider sends `change` with its value on release. The board updates its own copy of a bound control the user changed.
- The board keeps no live values while the app is closed (fenixOS v0.1.23 and later keep the last ones on the SD card and show them dimmed while the PC is away). When the app opens, the PC half gets `opened` and must send its current values.
- A label's `text` is what shows until the first value arrives: use a placeholder such as `"--"`.

## Wired sensors (`[[sensor]]`, fenixOS 0.1.29)

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
- The PC half gets every reading as `Message::Sensor` (`pc-half.md`), whatever the board shows.
- Test without hardware: `fenix sensors fake 22 23.4 45.2` (or `missing`, `error`, `off`); test the wiring: `fenix sensors`.
- Example: `tools/sensor-app` in the fenixOS repo.

## App web page (fenixOS 0.1.31)

In Wi-Fi mode, with the SD card in, the board serves the files your PC half writes under `web/` as a page any device on the same network can open: `put_file("web/index.html", ...)` serves at `http://fenixos.local/apps/<id>/`, `web/app.js` at `/apps/<id>/app.js`. Only `web/` is served; the rest of `data/<id>/` stays private. 32 KB per file: split a bigger page. The page goes when the app is removed.

- **Public:** anyone on the network can read it, without a password. Put only public things in `web/`: no settings, tokens or personal data.
- **Live sensors:** the page can poll `GET /sensors.json` (once a second is fine): `{"time": "14:03:28", "pc": true, "sensors": [{"pin": 22, "kind": "dht22", "state": "ok", "label": "Live", "temp": 23.4, "hum": 45.2, "read": "14:03:27", "age": 1, "apps": [{"id": "<id>", "bind": "s1"}]}]}`. One entry per pin some installed app declares; `state` is `reading`, `ok`, `missing` or `error` (`label` is the `<bind>_state` text); `temp` and `hum` are the last good values (`null` before one); `read` is the last good read's local time (`""` without a clock); `age` the seconds since it (right without a clock). Each request keeps the sensors read for 10 s, with or without the app open or a PC; after a pause the first answers say `reading` for up to 2 s.
- `GET /apps.json` lists the installed apps with a page (`[{"id", "name", "color"}]`); the board's own page at `http://fenixos.local/` links them. `GET /status.json` has the board's state.
- Test without a PC half: `fenix files put index.html data/<id>/web/index.html`.
- Not in USB-only mode, and not without a card.

## States the board shows for you

- No PC link: "Waiting for host PC", or with an SD card in (fenixOS v0.1.23) the last values, dimmed, under "PC not connected". An app with wired sensors (0.1.29) shows its layout live instead.
- The app is on an SD card that is out: "Insert the SD card".
- The app switched off in the PC app: "Turned off on host PC", with a Turn on button. Starting: "Starting on host PC". Added on another PC only: "Not on this PC".
- Otherwise the layout. An update reloads it in place; a removed app returns to Home.

## Limits

Checked by `fenix-pkg build` and again by the board before it installs.

| What | Limit |
|---|---|
| Package size | none (on the board's flash without an SD card: 128 KB) |
| Elements | 200 |
| Strings (all, bytes) | 8 KB |
| Bindings (a wired sensor adds 6, 7 from `min_fenixos` 0.1.31) | 64 |
| Image data (all images except the two tile icons) | 24 KB |
| RAM estimate (`elements built at once × 160 B + elements × 32 B + strings + images + the bound image's w × h × 2`; a `view` counts its largest child) | 32 KB (40 KB before fenixOS v0.1.25) |
| Tree depth | 8 |

Images are stored raw: a 100 × 60 RGB565 image is 12 KB. Prefer A8 (1 byte a pixel, tinted with `color`) for icons and line art. The board holds up to 32 apps, on its SD card when one is in.

## Example

```json
{
  "type": "box", "w": "100%", "pad": 12, "gap": 10, "portrait": { "pad": 10 },
  "children": [
    { "type": "box", "w": "100%", "bg": "surface_1", "radius": 12, "pad": 10, "gap": 4,
      "children": [
        { "type": "label", "text": "CPU", "font": "label", "color": "label_2" },
        { "type": "label", "text": "--", "font": "title_l", "bind": "cpu_text" },
        { "type": "bar", "min": 0, "max": 100, "bind": "cpu", "w": "100%", "h": 6 }
      ] },
    { "type": "button", "text": "Refresh", "icon": "arrow-clockwise", "bg": "surface_2", "w": "100%", "event": "refresh" }
  ]
}
```
