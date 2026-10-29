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
- Crash or early exit while switched on: fenixOS restarts it after 1 s, then 5 s, then 15 s. A fourth failure within 2 minutes stops the retries, and the tile shows "Stopped, see Logs" until the app is switched off and on again.
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
| `stop` | | Exit now |

App → fenixOS (`.up`):

| `type` | Fields | Effect |
|---|---|---|
| `set` | `bind` (name in `layout.json`), `value` (int, bool or string ≤ 255 bytes) | Sets a bound value on the board. Sent only while the app is open on the board; fenixOS drops it otherwise |
| `log` | `level` (`info`, `warn`, `error`), `text` | Adds a line to the fenixOS log (source "Apps"); `warn` and `error` show under Problems |

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

The sample PC half in `tools/sample-app/pc` is the full example: it sends CPU and memory use while the app is open and logs every event.
