# PC half (optional Windows helper)

Copied from fenixOS `docs/sidecars.md`, cut for app authors. Skip this file if the app needs nothing from the PC: delete `app/pc` and the `pc` block in `manifest.json`.

## Lifecycle

- fenixOS starts `<exe> --fenix-pipe <base>` while the app is switched on, and talks to it over two named pipes (`<base>.down` in, `<base>.up` out). `fenix-sdk` does all of this.
- fenixOS sends `hello`, then the current settings. It sends `stop` to end the app and ends the process 3 s later; the app must also exit when the pipe closes.
- Crash or early exit: restarted after 1 s, 5 s, 15 s; a fourth failure within 2 minutes stops it and the tile shows "Stopped, see Logs".
- Non-elevated helpers run in a job object that ends them with fenixOS. `"elevated": true` starts it through UAC each time (Windows asks the user); a declined prompt is not retried.
- Build it as a GUI-subsystem exe (`#![windows_subsystem = "windows"]`) so no console window opens.

## fenix-sdk (Rust)

`app/pc/Cargo.toml` takes it from the fenixOS repo at a release tag:

```toml
fenix-sdk = { git = "https://github.com/tariqaq/fenixOS", tag = "v0.1.13" }
```

| API | What it does |
|---|---|
| `App::connect()` | Connects using `--fenix-pipe`; waits for `hello` and the settings. Errors when started by hand |
| `app.next(timeout) -> Option<Message>` | Next message, or `None` after `timeout`. Returns `Stop` forever once stopped |
| `app.is_open()` | The app's screen is open on the board: only then are `set` values shown |
| `app.board_connected()` | The board link is up |
| `app.settings()` | Latest settings: key → `Json` (bool, number, string; see `registry.md`) |
| `app.set(bind, value)` | Set a binding: `i32`, `bool` or `&str`/`String` (≤ 255 bytes). Dropped while closed |
| `app.log(Level::Warn, text)`, `app.log_info(text)` | A line in the fenixOS log; `Warn` and `Error` show under Problems |
| `app.id`, `app.version`, `app.fenixos` | From `hello` |

`Message`: `Settings(map)`, `Board { connected }`, `Opened`, `Closed`, `Event { name, kind, value }` (`kind`: `Tap`, `Long`, `Change`; `value`: slider value or switch 0/1), `Button { key }` (a `button` setting was pressed), `Stop`.

## Pattern

```rust
#![windows_subsystem = "windows"]
use fenix_sdk::{App, Message};
use std::time::Duration;

fn main() {
    let Ok(app) = App::connect() else { std::process::exit(2) };
    loop {
        match app.next(Duration::from_secs(1)) {
            Some(Message::Stop) => break,
            Some(Message::Opened) => push(&app),           // the board holds no values: send them all
            Some(Message::Event { name, .. }) if name == "refresh" => push(&app),
            _ => {}
        }
        if app.is_open() {
            push(&app); // once a second while open
        }
    }
}

fn push(app: &App) {
    app.set("cpu", 42);
    app.set("cpu_text", "42 %");
}
```

Send only what changed when values are large or frequent: every `set` crosses the board link (USB at 921,600 baud, or Wi-Fi), shared with the screen stream.

## Other languages

Any Windows program can speak the protocol: each message is a little-endian `u32` length, then UTF-8 JSON with a `type`, at most 64 KB. Down: `hello {app, version, fenixos, protocol: 1}`, `settings {values}`, `board {connected}`, `opened`, `closed`, `event {name, kind, value}`, `button {key}`, `stop`. Up: `set {bind, value}`, `log {level, text}`. Open `.down` first, then `.up`; fenixOS waits 10 s for both. Unknown types and fields are ignored.

## Permissions

Declare in `manifest.json` what the helper does: `keys` (press keys and shortcuts), `media` (volume and media), `system-stats` (CPU, memory and other stats), `network`, `files`, `programs` (start other programs). The PC app shows them before Add. Do no more than declared, and nothing that looks like cheating software: no global hooks, DLL injection, other processes' memory or raw input capture.
