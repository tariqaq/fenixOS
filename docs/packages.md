# fenixOS app packages (`.fxp`)

Version: **package format 1** (fenixOS v0.1). Plan: `docs/PLAN.md` §3.4. Sources of truth:
- Compiler and reader (Rust): `crates/fenix-pkg` (`fenix-pkg build <dir>`, `fenix-pkg inspect <file>`)
- Board reader (C++): `board/components/fenix_pkg` (same checks as the Rust reader; host test `test/run.ps1` parses the sample and survives every single-byte flip)
- Board builder: `board/components/fenix_apps/app_pkg.cpp`; store: `board/components/fenix_sys/store.cpp`
- Test fixture: `tools/sample-app/board` (`fenix-pkg build tools/sample-app/board`, then `fenix install tools/sample-app/board/app.fxp`)

A host app has two halves. The **board half** is this package: a declarative screen (no code runs on the board). The **PC half** is a sidecar process (P8) that sets bound values and gets events.

## Source

A folder with:

- `app.toml`:

  ```toml
  id = "io.github.tariqaq.sample"   # reverse-DNS; letters, digits, . - _; ≤ 48 bytes (it names files on the board)
  name = "Sample"                   # tile label, ≤ 24 bytes
  version = "0.1.0"
  min_fenixos = "0.1.0"
  color = "teal"                    # tile colour: a tile token (gray indigo blue teal green orange red purple pink) or "#RRGGBB"
  icon = "icon.png"                 # any size, alpha channel used; scaled to 20 px (tile) and 26 px (dock)
  ```

- `layout.json`: one root element (below). Every element is an object with a `type`, optional props and `children`.
- Image files named by `image` elements (PNG).

### Elements

| `type` | What it is | Own props |
|---|---|---|
| `box` | Flex container | `flow` (`column` default, `row`, `row_wrap`), `gap`, `pad`, `main`, `cross` (`start` `center` `end` `space_between`), `bg`, `radius` |
| `label` | Text. With `bind`, a value label | `text`, `font`, `color`, `align` (`left` `center` `right`) |
| `button` | Capsule button; with `icon` and no text, an icon button | `text`, `icon`, `bg`, `color`, `event` |
| `switch` | On/off switch | `bind`, `event`, `value` |
| `slider` | Horizontal slider | `min`, `max`, `value`, `bind`, `event` |
| `bar` | Progress / meter bar | `min`, `max`, `value`, `bind`, `color` |
| `image` | A8 (tinted with `color`) or RGB565 image | `src`, `color` (A8 only) |
| `icon` | Built-in Phosphor glyph | `icon`, `size` (12 14 15 16 18 20 26 28 40), `color` |

Props every element takes: `w`, `h` (pixels, `"50%"`, or `"content"`), `grow`, `bind` (a binding name), `event` (an event name), `hidden`, and `portrait` (an object with any of these props, used when the board is in portrait).

- **Fonts:** `body` (15), `body_semibold`, `label` (13), `label_medium`, `label_semibold`, `caption` (12), `caption_medium`, `title` (17), `title_l` (22), `clock_56`, `clock_64`. Default `body`. Inter only, digits and Basic Latin; see `tools/gen_fonts.py` for the character set.
- **Colours:** a token name from `design/tokens.json` (`label`, `label_2`, `accent`, `success`, `warning`, `danger`, `surface_1` … `tile_*`) or `"#RRGGBB"`.
- **Icons:** the board's built-in set (`gear-six`, `power`, `screencast`, `arrow-clockwise`, `plugs-connected`, `plugs`, `sun`, `moon`, `timer`, `device-rotate`, `hand-tap`, `hard-drives`, `desktop-tower`, `info`, `squares-four`, `warning`, `puzzle-piece`, `check-circle`, `x-circle`, `monitor`). Other artwork goes in as `image`.

### Bindings and events

- `bind: "cpu"` connects the element to binding `cpu`. The PC half sets it (`BIND_SET`, `proto/spec.md`): a label shows text or an integer, a switch takes a bool, a slider or bar an integer.
- `event: "mute"` makes the element report: a button sends a tap, a switch or slider sends its new value (sliders on release). The board sends `EVENT` with the element's index; the PC maps the index back to the name through the package.
- The board keeps no values while an app is closed. When an app opens the board sends `APP_OPENED`, and the PC half sends its current values.

## Limits

Checked by the compiler and again by the board before it commits an install. Both readers also check that every index (strings, bindings, assets, icons, fonts) is in range, that each asset's size matches `w × h × bytes per pixel`, and that the tree depth is ≤ 8.

| What | Limit |
|---|---|
| Package size | 64 KB (the board holds the whole package in RAM to check it and while the app is open) |
| Elements | 200 |
| Strings (all, bytes) | 8 KB |
| Bindings | 64 |
| Image data (every asset except the two tile icons) | 24 KB |
| RAM estimate (`elements × 160 B + strings + images`) | 40 KB |
| Tree depth | 8 |

## Binary format 1

Little-endian. All offsets are implied by order (the file is read front to back).

```
header (32 bytes)
  0  magic        "FXP1"
  4  format       u8 = 1
  5  reserved     u8 = 0
  6  min_fenixos  u8 major, u8 minor, u8 patch
  9  reserved     u8 = 0
  10 n_strings    u16
  12 n_binds      u16
  14 n_elems      u16
  16 n_assets     u16
  18 id           u16 string index
  20 name         u16 string index
  22 version      u16 string index
  24 tile_color   u32 0x00RRGGBB
  28 icon_small   u16 asset index (20 px A8), 0xFFFF = none
  30 icon_large   u16 asset index (26 px A8), 0xFFFF = none
strings   n_strings × (len:u16 ‖ utf8)
binds     n_binds × name:u16 (string index); the bind id is the position
elements  n_elems × (kind:u8 ‖ parent:u16 (0xFFFF = root) ‖ nprops:u8 ‖ nprops × (key:u8 ‖ value:i32))
assets    n_assets × (format:u8 (1 = A8, 2 = RGB565 little-endian, LVGL's native order) ‖ w:u16 ‖ h:u16 ‖ len:u32 ‖ data)
```

- Elements are in tree order: a parent always comes before its children. The element index (position) is the `elem` in `EVENT`.
- **Kinds:** 1 box, 2 label, 3 button, 4 switch, 5 slider, 6 bar, 7 image, 8 icon.
- **Prop keys** (`| 0x80` = portrait-only value):

  | Key | Prop | Value |
  |---|---|---|
  | 1 | text | string index |
  | 2 | font | 0 body, 1 body_semibold, 2 label, 3 label_medium, 4 label_semibold, 5 caption, 6 caption_medium, 7 title, 8 title_l, 9 clock_56, 10 clock_64 |
  | 3 | color | 0x00RRGGBB |
  | 4 | bg | 0x00RRGGBB |
  | 5 | bind | bind id |
  | 6 | w | size (below) |
  | 7 | h | size |
  | 8 | grow | flex grow |
  | 9 | gap | px |
  | 10 | pad | px |
  | 11 | flow | 0 column, 1 row, 2 row_wrap |
  | 12 | main | 0 start, 1 center, 2 end, 3 space_between |
  | 13 | cross | same as main |
  | 14 | radius | px |
  | 15 | min | int |
  | 16 | max | int |
  | 17 | value | int |
  | 18 | icon | built-in icon index (list order above) |
  | 19 | size | icon px |
  | 20 | src | asset index |
  | 21 | event | string index of the event name |
  | 22 | hidden | 0 / 1 |
  | 23 | align | 0 left, 1 center, 2 right |

- **Sizes:** `≥ 0` pixels, `-1` content, `0x40000000 | pct` percent.
- Assets are stored raw in format 1 (no compression). Compression is a later format version.

## Store on the board

- LittleFS partition `apps` (~1.9 MB) at `/apps`: `<id>.fxp` and `<id>.sha` (32-byte sha256).
- Install: `APP_INSTALL_BEGIN` opens `/apps/<id>.tmp`; each `APP_CHUNK` at the expected offset is written and acked with the next offset (others are acked with the expected offset, so the PC rewinds); `APP_INSTALL_END` checks size, sha256 and the package header and limits, then removes the old files and renames the temp file. Leftover `*.tmp` files are deleted at boot, so a power cut never leaves a half app. The UART RX ISR runs from IRAM (`CONFIG_UART_ISR_IN_IRAM`) and the RX buffer is 8 KB, so flash writes do not drop chunk bytes.
- `APP_LIST` reads each package header (id, version) and its `.sha`. Up to 10 apps (`MAX_APPS`).
- A failed check leaves the old version in place. A `.fxp` or `.sha` without its partner is deleted at boot too (a cut between the two steps of a commit or remove); the PC reinstalls it on the next reconcile.
- Measured (2026-10-29, COM9): 1.6 KB sample installs in 0.13 s, a 21 KB package with a 110×90 RGB565 image in 0.51 s (~42 KB/s, so a 64 KB package takes ~1.6 s). Remove brings the store back to its empty size (8 KB of LittleFS metadata).
