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

Props every element takes: `w`, `h` (pixels, `"50%"`, or `"content"`; boxes default to their content size), `grow` (share of the parent's free space along its flow; only applies when the parent has a set `h` for a column or `w` for a row, so a row that turns into a column in portrait keeps its children's natural height), `bind` (a binding name), `event` (an event name), `hidden`, and `portrait` (an object with any of these props, used when the board is in portrait).

- **Fonts:** `body` (15), `body_semibold`, `label` (13), `label_medium`, `label_semibold`, `caption` (12), `caption_medium`, `title` (17), `title_l` (22), `clock_56`, `clock_64`. Default `body`. Inter only: printable ASCII plus `° · × – — ’ • … ‹ ›` (`tools/gen_fonts.py`); the clock fonts hold digits, `:` `-` and space. Corrected (P45): was "digits and Basic Latin".
- **Colours:** a token name from `design/tokens.json` (`label`, `label_2`, `accent`, `success`, `warning`, `danger`, `surface_1` … `tile_*`) or `"#RRGGBB"`.
- **Icons:** the board's built-in set (`gear-six`, `power`, `screencast`, `arrow-clockwise`, `plugs-connected`, `plugs`, `sun`, `moon`, `timer`, `device-rotate`, `hand-tap`, `hard-drives`, `desktop-tower`, `info`, `squares-four`, `warning`, `puzzle-piece`, `check-circle`, `x-circle`, `monitor`). Other artwork goes in as `image`.

### Bindings and events

- `bind: "cpu"` connects the element to binding `cpu`. The PC half sets it (`BIND_SET`, `proto/spec.md`): a label shows text, an integer, or "On" / "Off" for a bool; a switch takes a bool, a slider or bar an integer. Corrected: the bool case on a label was missing.
- The board queues at most 16 values between the link and the UI and drops the rest (`link.cpp`); a dropped value shows only when the PC sends it again. A burst of more than 16 (an app sending every value on `opened`) can lose values; fix planned in P50-M (`tasks.md`).
- A bound label without `w` grows with its text and is cut off at the screen edge; with `w` it wraps onto as many lines as needed.
- `event: "mute"` makes the element report: a button sends a tap (and a long press), a switch or slider sends its new value (sliders on release). The board sends `EVENT` with the element's index; the PC maps the index back to the name through the package.
- The board keeps no values while an app is closed. When an app opens the board sends `APP_OPENED`, and the PC half sends its current values.

## Limits

Checked by the compiler and again by the board before it commits an install. Both readers also check that every index (strings, bindings, assets, icons, fonts) is in range, that each asset's size matches `w × h × bytes per pixel`, and that the tree depth is ≤ 8.

| What | Limit |
|---|---|
| Package size | none (P51; was 64 KB). On the board's flash: 128 KB |
| Elements | 200 |
| Strings (all, bytes) | 8 KB |
| Bindings | 64 |
| Image data (every asset except the two tile icons) | 24 KB |
| RAM estimate (`elements × 160 B + strings + images`) | 40 KB |
| Tree depth | 8 |

### Board memory (P39)

The board never holds a whole package. `fpkg::Package::parse` reads from a `FileSource` (LittleFS) or a `MemSource`, copies strings (each NUL-terminated) and element props into the package's own arena, and records each asset's offset instead of reading its pixels.

- **Install check** (`store_end`): parses the temp file in place; RAM is the arena only. Hash is computed as chunks arrive.
- **Home tiles** (`apps_reload`): parse, then read the two tile icons by offset.
- **Open app:** keeps the arena while open; reads only the assets an `image` element shows. App labels point into the arena (`lv_label_set_text_static`), so their text is not copied again; the arena therefore lives until the app screen is deleted, after its slide out (P45).
- Heap before and after P39: `docs/board-info.md` "App packages read in pieces".

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

## Runtime on the board

`board/components/fenix_apps/app_pkg.cpp`, artboards HostWaiting (6a) and HostOff (6b).

- **Tiles:** Home shows the system apps, then installed apps sorted by id: tile colour, the 20 px icon (Tiles) or 26 px icon (Dock), tinted white. Since P51 the launcher is paged: 3 tiles a page in landscape, 2 × 2 in portrait, as many Dock items as fit (4 in landscape); swipe left or right anywhere on Home, dots under the tiles show the page. Only the shown tiles' icons are read (and the previous page's, kept one rebuild so the old screen can still draw). A new tile turns Home to its page and fades in (250 ms); a removed one fades out where it was. Corrected (P51): the tiles no longer scale (0.8 → 1), which needed a heap-hungry transform layer.
- **Faded tiles (P51):** 40% opacity when the app's PC half is switched off (`HOST_STATE` off) or not on this PC (not here), or when the app is on an SD card that is out (with a card glyph in the corner). A faded tile still opens.
- **Open:** tapping a tile reads the package from the SD card or flash (strings, element props and the images its elements show; tile icons stay in storage), fades the app screen in (navbar "Home" + app name, the layout below it, scrolling if taller than the screen) and sends `APP_OPENED`. It is sent again when the link comes back while the app is open.
- **Close:** back, or any other screen change, sends `APP_CLOSED`; what was read is freed when the app screen is deleted (its slide out still draws it). `BOARD_STATE` reports screen 5 and the app id while it is open.
- **States:** card out: "Insert the SD card" (the card is looked for at once; the app opens when it comes back). No PC link: 6a "Waiting for host PC". `HOST_STATE` (P51): off, 6b "Turned off on host PC" with a **Turn on** button (sends `APP_TURN_ON`; "Turning on" with a spinner until the PC answers); not here, "Not on this PC"; starting, "Starting on host PC" with a spinner. Running (or no state from this PC yet): the layout.
- **Bindings:** `BIND_SET` for the open app updates every element bound to it and is cached per binding while the app is open, so rebuilds (rotation, state changes) keep the values. Values for other apps, or with no app open, are dropped.
- **Events:** buttons send tap (1) and long press (2); switches send change (3) with 0/1; sliders send change (3) with the value on release. The board updates its cache for a bound control the user changed.
- **Install / remove while open:** an update reloads the package in place. A remove returns Home at once, or after 2 s when 6b is showing (Remove app on the PC sends `HOST_STATE` off, then `APP_REMOVE`). Pulling the card under an open SD app returns Home.
- **Defaults:** buttons 40 px tall (icon buttons 40 × 40, DESIGN.md's touch target), button background `accent`, label colour `label`, bar colour `accent`, switch on colour `success`, slider and bar track `surface_3`. Built-in widgets match the system screens (slider, capsule button).
- Not testable over the link: 6a (connecting the CLI ends it). The PC simulator shows it (`board/sim/golden/app_waiting.png`, the `disconnect` step in `board/sim/screens.txt`).

## Store on the board

### Where apps live (P51)

- **SD card first:** with a FAT32 card in, packages go to `/sd/fenixos/apps/<id>.fxp` and `.sha`. No size cap: the card's free space (64 KB kept spare) and FAT32's 4 GB per file. **No card:** LittleFS at `/apps`, 128 KB per package (`sys::FLASH_PKG_MAX`), else `APP_RESULT` 7 "too big for flash". One app is in one place: an install drops the copy in the other store.
- **Index:** `sys::store_*` keep one list in RAM (`AppMeta`, 120 B: id, version, name, tile colour, where, state), sorted by id, up to 32 apps (`MAX_APPS`). The sha is read from the `.sha` file when asked. Home, Settings › Storage and the link read it; nobody keeps a copy.
- **Card out:** `/apps/sd.idx` on flash lists the card's apps (`id\tversion\tcolour\tname` per line), so their tiles stay (faded, card glyph) with the card out; they are "card missing" until the card is back and scanned. A flash copy wins over a card copy of the same app at a scan (the card's is deleted). An app whose card is out cannot be removed.
- **Link:** `APP_INDEX_REQ` / `APP_INDEX` page the index (8 apps a page, where and state per app, sha zero while the card is out). `APP_LIST` still answers with the first 10 apps for older PC apps.

### Install and remove

- Install: `APP_INSTALL_BEGIN` opens `<dir>/<id>.tmp` in the store chosen above; each `APP_CHUNK` at the expected offset is written and acked with the next offset (others are acked with the expected offset, so the PC rewinds); `APP_INSTALL_END` checks size, sha256 and the package header and limits, then removes the old files and renames the temp file. Leftover `*.tmp` files are deleted at boot, so a power cut never leaves a half app. The UART RX ISR runs from IRAM (`CONFIG_UART_ISR_IN_IRAM`) and the RX buffer is 5 KB (the PC keeps at most 4 KB of chunks unacked; peak 2.1 KB, P40), so flash writes do not drop chunk bytes.
- A failed check leaves the old version in place. A `.fxp` or `.sha` without its partner is deleted at boot (flash) or at the card's mount (SD) too (a cut between the two steps of a commit or remove); Sync with board offers it again.
- Measured (2026-10-29, COM9): 1.6 KB sample installs in 0.13 s, a 21 KB package with a 110×90 RGB565 image in 0.51 s (~42 KB/s over USB). Remove brings the flash store back to its empty size (8 KB of LittleFS metadata).
- Package size in practice: the content limits above (24 KB of images, 8 KB of strings) bound a package near 33 KB plus tile icons, so dropping the 64 KB cap (P51) changes no real package today; it leaves room for later formats that stream images from the card.

## On, off and sync (P51)

| Action on the PC | PC half | Board half |
|---|---|---|
| Add app (registry) | Downloaded, switched on | Installed (SD card, else flash ≤ 128 KB) |
| Switch off | Stopped | Kept; `HOST_STATE` off: tile fades, the app screen offers Turn on |
| Switch on (here, or Turn on on the board) | Started (`HOST_STATE` starting, then running) | Installed if missing or different |
| Remove app | Deleted with its settings | Removed |

- **Sync with board:** when the board connects, the PC reads its index and lists what differs on the Apps page; nothing changes until the user clicks Sync. Per app: on this PC only, or older or a different copy on the board: put this PC's version on the board. Newer on the board: update this PC from the registry. On the board only: add the PC half from the registry (switched off), keep it on the board alone (remembered in `config.json` `kept_on_board`; the board shows "Not on this PC"), or remove it. Each row can also be left for later.
- Every board app gets its `HOST_STATE` at connect: running, starting, off, or not here.
- Installs over 128 KB show a percentage; one that would take over ~2 minutes at USB speed is announced in the log first. An install gives up after 30 s without progress.
