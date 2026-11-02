# App PC halves (sidecars)

An app's PC half is a Windows program that fenixOS starts while the app is switched on. It talks to fenixOS over named pipes; fenixOS talks to the board. Plan: `docs/PLAN.md` §3.5. Rust apps use `crates/fenix-sdk`; any other language can speak the protocol below.

## Start and stop

- fenixOS starts the program as `<exe> --fenix-pipe \\.\pipe\fenixos-<id>-<nonce>`. The nonce is 128 random bits, new for every start.
- Two one-way pipes carry the messages:
  - `<base>.down`: fenixOS → app. The app opens it for reading.
  - `<base>.up`: app → fenixOS. The app opens it for writing.
  - Open `.down` first, then `.up`. Each pipe takes one client only.
  - Why two pipes: on Windows, a blocking read and a write on the same synchronous handle wait for each other (the serial port showed the same in the PoC). One pipe per direction lets an app block on reads in one thread and write from another.
- fenixOS waits 10 s for both connections. Then it sends `hello` and the current `settings`.
- To stop the app, fenixOS sends `stop` and closes the pipes. The app must exit when it gets `stop` or when a read or write fails (the pipe closed). After 3 s fenixOS ends the process. An elevated process cannot be ended by fenixOS, so it must exit on its own; `fenix-sdk` does this for you.
- Programs started without elevation run in a job object that ends them if fenixOS itself ends.
- Crash or early exit while switched on: fenixOS restarts it after 1 s, then 5 s, then 15 s (the board shows "Starting on host PC" meanwhile). A fourth failure within 2 minutes stops the retries, and the tile shows "Stopped, see Logs" until the app is switched off and on again, or Turn on is pressed on the board (which shows "Turned off on host PC" then).
- Switching an app off stops this program only; the board keeps its half (P51, `docs/packages.md` "On, off and sync").
- `pc.elevated: true` in the manifest: fenixOS starts the program through UAC (`runas`), so Windows asks each time the app starts. A declined prompt counts as a failed start, and fenixOS does not retry it.

## Messages

Each message is a little-endian `u32` byte length, then that many bytes of UTF-8 JSON: one object with a `type`. A message is at most 64 KB. Unknown types and unknown fields are ignored, so either side can add to the protocol.

fenixOS → app (`.down`):

| `type` | Fields | When |
|---|---|---|
| `hello` | `app` (id), `version` (app version), `fenixos` (fenixOS version), `protocol` (1) | First message |
| `settings` | `values`: object, key → value (`docs/registry.md` Settings) | After `hello`, then every time the user changes a setting |
| `board` | `connected`: bool | The board link came up or went down |
| `opened` / `closed` | | The app's screen opened / closed on the board. Values the app `set`s show only while it is open |
| `event` | `name` (the element's `event` name in `layout.json`), `kind` (`tap`, `long`, `change`), `value` (int: slider value, switch 0/1, else 0) | The user touched an element on the board |
| `button` | `key` | A `button` setting was pressed on the App settings page |
| `file` | `req`, `ok` (bool), `data` (base64, a get that worked), `error` (text, when not `ok`) | The answer to a `file_put`, `file_get` or `file_delete` with that `req` |
| `stop` | | Exit now |

App → fenixOS (`.up`):

| `type` | Fields | Effect |
|---|---|---|
| `set` | `bind` (name in `layout.json`), `value` (int, bool or string ≤ 255 bytes) | Sets a bound value on the board. Sent only while the app is open on the board; fenixOS drops it otherwise |
| `set_image` | `bind`, `w`, `h` (each 1 to 120), `data` (base64 RGB888, w × h × 3 bytes) | fenixOS 0.1.25 (P50-M): pixels for the layout's bound `image`. fenixOS scales them to fill the element (centre crop), converts to RGB565 and sends `BIND_IMAGE` paced (~3 KB per 50 ms); a newer picture replaces one on its way. Only while the app is open; a wrong name or size is logged under Problems. SDK: `set_image(bind, w, h, &rgba)` (scales larger pictures down), and `set_icon(bind, name)` sends an icon's index with `set` |
| `log` | `level` (`info`, `warn`, `error`), `text` | Adds a line to the fenixOS log (source "Apps"); `warn` and `error` show under Problems |
| `file_put` | `req` (u32, the app's own number), `name`, `data` (base64, at most 32 KB decoded) | Writes `/sd/fenixos/data/<id>/<name>` on the board's SD card (P53), replacing a file of that name |
| `file_get` | `req`, `name` | Reads it back (at most 32 KB) |
| `file_delete` | `req`, `name` | Deletes a file, or an empty subfolder |

App files (P53): `name` is relative to the app's folder and may hold `/` for subfolders (no `..`, the board's path rules in `docs/sd-card.md`). fenixOS sends them over its board link as file writes (`proto/spec.md` "Files (0x6_)") and answers `file` once the board has, so a request takes a few seconds at most. It answers `ok: false` with the reason when the board is not connected, has no card, or refuses. The board deletes the folder when the app is removed from it (from the board's card; a card that is out then keeps it). fenixOS before 0.1.23 ignores these types, so the SDK gives up after 30 s.

## fenix-sdk (Rust)

```rust
use fenix_sdk::{App, Message};
use std::time::Duration;

fn main() {
    let app = App::connect().expect("start me from fenixOS");
    loop {
        match app.next(Duration::from_secs(1)) {
            Some(Message::Stop) => break,
            Some(Message::Event { name, kind, value }) => app.log_info(format!("{name} {kind:?} {value}")),
            _ => {}
        }
        if app.is_open() {
            app.set("cpu", 42);
            app.set("cpu_text", "42 %");
        }
    }
}
```

`next` returns `Stop` once fenixOS sends `stop` or the pipe closes, and keeps returning it after that. `settings()` holds the latest values; `set` and `log` never block for long and are dropped once the pipe is closed.

`put_file(name, &bytes)`, `get_file(name)` and `delete_file(name)` keep files in the app's folder on the board's card. They block until fenixOS answers (seconds; 30 s at most) and return `Err(text)` when there is no board, no card, or an older fenixOS. Call them from the loop; each holds it for the time the request takes.

`fenix_sdk::text::fit(s, cjk_font)` (P51-M) turns any text into characters the board draws: Latin-1 and `– — ’ • … ‹ ›` stay (fenixOS 0.1.26), Chinese, Japanese and Korean stay when `cjk_font` is true (the user put the CJK font on the card; an app can ask in a setting), the rest is transliterated (`Łódź` to `Lódz`, `Привет` to `Privet`, `北京` to `Bei Jing`) or becomes `?` (one per emoji). `text::builtin(c)` and `text::cjk(c)` test one character. Use it for titles from outside (songs, windows, chats); text the app writes itself needs nothing.

The sample PC half in `tools/sample-app/pc` is the full example: it sends CPU and memory use while the app is open, logs every event, and writes `hello.txt` to its card folder and reads it back each time the board connects.
