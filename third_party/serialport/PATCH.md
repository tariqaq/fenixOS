# serialport 4.10.1, patched for fenixOS

Upstream: https://github.com/serialport/serialport-rs (crates.io `serialport` 4.10.1, MPL-2.0, see `LICENSE.txt`).
Used through `[patch.crates-io]` in the workspace `Cargo.toml`, so every crate that links serialport (fenix-link, espflash in fenix-flash) gets it.

Only `src/windows/com.rs` changes (marked "fenixOS patch"):

- `set_timeout` sets COMMTIMEOUTS `MAXDWORD, 0, 0` for reads and no write timeout. Upstream's `MAXDWORD, MAXDWORD, <timeout>` takes the WCH CH340 driver's read-timeout code, which blue-screened the PC twice (`docs/board-info.md`, P16, P23).
- `read` polls every 1 ms until data arrives or the port's timeout passes, then returns `TimedOut` as upstream does.

Behaviour on other platforms is unchanged. When updating serialport, copy the new release here and apply the same two changes.
