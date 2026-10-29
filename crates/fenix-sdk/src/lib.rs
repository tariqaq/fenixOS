//! Write the PC half of a fenixOS app. fenixOS starts the program with `--fenix-pipe <base>`;
//! [`App::connect`] opens the two pipes, and the app then reads [`Message`]s and sends values to
//! the board with [`App::set`]. Protocol: `docs/sidecars.md`.

use std::fs::{File, OpenOptions};
use std::io;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

pub use serde_json::{Map, Value as Json};

/// Wire format shared with fenixOS: a little-endian u32 length, then one JSON object.
pub mod wire {
    use serde::{Deserialize, Serialize};
    use std::io::{self, Read, Write};

    pub const PROTOCOL: u32 = 1;
    pub const MAX_MSG: usize = 64 * 1024;

    /// fenixOS → app.
    #[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
    #[serde(tag = "type", rename_all = "lowercase")]
    pub enum Down {
        Hello {
            app: String,
            version: String,
            fenixos: String,
            protocol: u32,
        },
        Settings {
            values: serde_json::Map<String, serde_json::Value>,
        },
        Board {
            connected: bool,
        },
        Opened,
        Closed,
        Event {
            name: String,
            kind: String,
            value: i32,
        },
        Button {
            key: String,
        },
        Stop,
        /// A type this version does not know (the protocol only grows).
        #[serde(other)]
        Unknown,
    }

    /// App → fenixOS.
    #[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
    #[serde(tag = "type", rename_all = "lowercase")]
    pub enum Up {
        Set {
            bind: String,
            value: serde_json::Value,
        },
        Log {
            level: String,
            text: String,
        },
        #[serde(other)]
        Unknown,
    }

    pub fn write<T: Serialize>(w: &mut impl Write, m: &T) -> io::Result<()> {
        let body = serde_json::to_vec(m).map_err(io::Error::other)?;
        if body.len() > MAX_MSG {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "message over 64 KB",
            ));
        }
        let mut buf = Vec::with_capacity(4 + body.len());
        buf.extend_from_slice(&(body.len() as u32).to_le_bytes());
        buf.extend_from_slice(&body);
        w.write_all(&buf)?;
        w.flush()
    }

    /// Next message. A malformed body is skipped as `None` inside `Ok`; a broken pipe is `Err`.
    pub fn read<T: for<'de> Deserialize<'de>>(r: &mut impl Read) -> io::Result<Option<T>> {
        let mut len = [0u8; 4];
        r.read_exact(&mut len)?;
        let n = u32::from_le_bytes(len) as usize;
        if n > MAX_MSG {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "message over 64 KB",
            ));
        }
        let mut body = vec![0u8; n];
        r.read_exact(&mut body)?;
        Ok(serde_json::from_slice(&body).ok())
    }
}

/// A value for a board binding.
#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    Int(i32),
    Bool(bool),
    Text(String),
}

impl From<i32> for Value {
    fn from(v: i32) -> Self {
        Value::Int(v)
    }
}
impl From<bool> for Value {
    fn from(v: bool) -> Self {
        Value::Bool(v)
    }
}
impl From<&str> for Value {
    fn from(v: &str) -> Self {
        Value::Text(v.into())
    }
}
impl From<String> for Value {
    fn from(v: String) -> Self {
        Value::Text(v)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventKind {
    Tap,
    Long,
    Change,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Message {
    /// New settings (also kept in [`App::settings`]).
    Settings(Map<String, Json>),
    Board {
        connected: bool,
    },
    Opened,
    Closed,
    Event {
        name: String,
        kind: EventKind,
        value: i32,
    },
    Button {
        key: String,
    },
    /// fenixOS asked the app to exit, or the pipe closed. Exit soon: fenixOS ends the process after 3 s.
    Stop,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Level {
    Info,
    Warn,
    Error,
}

struct Shared {
    open: AtomicBool,
    board: AtomicBool,
    stopped: AtomicBool,
    settings: Mutex<Map<String, Json>>,
}

pub struct App {
    pub id: String,
    pub version: String,
    pub fenixos: String,
    up: Mutex<File>,
    rx: Receiver<Message>,
    shared: Arc<Shared>,
}

impl App {
    /// Connect to fenixOS using the `--fenix-pipe` argument. Waits for `hello` and the settings.
    pub fn connect() -> io::Result<App> {
        let mut args = std::env::args();
        let base = loop {
            match args.next() {
                Some(a) if a == "--fenix-pipe" => break args.next(),
                Some(_) => continue,
                None => break None,
            }
        };
        let base = base.ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::NotFound,
                "no --fenix-pipe: start this app from fenixOS",
            )
        })?;
        Self::connect_to(&base)
    }

    /// Connect to a pipe base name (`\\.\pipe\fenixos-<id>-<nonce>`).
    pub fn connect_to(base: &str) -> io::Result<App> {
        let mut down = open_retry(&format!("{base}.down"), true)?;
        let up = open_retry(&format!("{base}.up"), false)?;
        let (id, version, fenixos) = loop {
            match wire::read::<wire::Down>(&mut down)? {
                Some(wire::Down::Hello {
                    app,
                    version,
                    fenixos,
                    ..
                }) => break (app, version, fenixos),
                _ => continue,
            }
        };
        let shared = Arc::new(Shared {
            open: AtomicBool::new(false),
            board: AtomicBool::new(false),
            stopped: AtomicBool::new(false),
            settings: Mutex::new(Map::new()),
        });
        let (tx, rx) = mpsc::channel();
        let sh = shared.clone();
        thread::Builder::new()
            .name("fenix-sdk".into())
            .spawn(move || {
                loop {
                    let m = match wire::read::<wire::Down>(&mut down) {
                        Ok(Some(m)) => m,
                        Ok(None) => continue,
                        Err(_) => break,
                    };
                    let out = match m {
                        wire::Down::Settings { values } => {
                            *sh.settings.lock().unwrap() = values.clone();
                            Message::Settings(values)
                        }
                        wire::Down::Board { connected } => {
                            sh.board.store(connected, Ordering::Relaxed);
                            if !connected {
                                sh.open.store(false, Ordering::Relaxed);
                            }
                            Message::Board { connected }
                        }
                        wire::Down::Opened => {
                            sh.open.store(true, Ordering::Relaxed);
                            Message::Opened
                        }
                        wire::Down::Closed => {
                            sh.open.store(false, Ordering::Relaxed);
                            Message::Closed
                        }
                        wire::Down::Event { name, kind, value } => Message::Event {
                            name,
                            kind: match kind.as_str() {
                                "long" => EventKind::Long,
                                "change" => EventKind::Change,
                                _ => EventKind::Tap,
                            },
                            value,
                        },
                        wire::Down::Button { key } => Message::Button { key },
                        wire::Down::Stop => break,
                        wire::Down::Hello { .. } | wire::Down::Unknown => continue,
                    };
                    if tx.send(out).is_err() {
                        break;
                    }
                }
                sh.stopped.store(true, Ordering::Relaxed);
                let _ = tx.send(Message::Stop);
            })?;
        let app = App {
            id,
            version,
            fenixos,
            up: Mutex::new(up),
            rx,
            shared,
        };
        Ok(app)
    }

    /// Next message, waiting up to `timeout`. `None` on timeout. After [`Message::Stop`] this
    /// returns `Stop` again at once, so a loop that misses it still ends.
    pub fn next(&self, timeout: Duration) -> Option<Message> {
        match self.rx.recv_timeout(timeout) {
            Ok(m) => Some(m),
            Err(RecvTimeoutError::Timeout) => None,
            Err(RecvTimeoutError::Disconnected) => Some(Message::Stop),
        }
    }

    /// True while the app's screen is open on the board (values sent now are shown).
    pub fn is_open(&self) -> bool {
        self.shared.open.load(Ordering::Relaxed)
    }

    pub fn board_connected(&self) -> bool {
        self.shared.board.load(Ordering::Relaxed)
    }

    pub fn stopped(&self) -> bool {
        self.shared.stopped.load(Ordering::Relaxed)
    }

    /// The latest settings values (`docs/registry.md` Settings).
    pub fn settings(&self) -> Map<String, Json> {
        self.shared.settings.lock().unwrap().clone()
    }

    /// Set a bound value on the board (`bind` is the name in `layout.json`).
    pub fn set(&self, bind: &str, value: impl Into<Value>) {
        let value = match value.into() {
            Value::Int(v) => Json::from(v),
            Value::Bool(v) => Json::from(v),
            Value::Text(v) => Json::from(v),
        };
        self.send(&wire::Up::Set {
            bind: bind.into(),
            value,
        });
    }

    pub fn log(&self, level: Level, text: impl Into<String>) {
        let level = match level {
            Level::Info => "info",
            Level::Warn => "warn",
            Level::Error => "error",
        };
        self.send(&wire::Up::Log {
            level: level.into(),
            text: text.into(),
        });
    }

    pub fn log_info(&self, text: impl Into<String>) {
        self.log(Level::Info, text)
    }

    fn send(&self, m: &wire::Up) {
        if wire::write(&mut *self.up.lock().unwrap(), m).is_err() {
            // The pipe closed: fenixOS is gone or stopped us.
            self.shared.stopped.store(true, Ordering::Relaxed);
        }
    }
}

// The server may not have created the pipe yet when the app starts; retry for 5 s.
fn open_retry(path: &str, read: bool) -> io::Result<File> {
    let end = Instant::now() + Duration::from_secs(5);
    loop {
        let r = if read {
            OpenOptions::new().read(true).open(path)
        } else {
            OpenOptions::new().write(true).open(path)
        };
        match r {
            Ok(f) => return Ok(f),
            Err(e) if Instant::now() >= end => return Err(e),
            Err(_) => thread::sleep(Duration::from_millis(50)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::wire::{self, Down, Up};

    #[test]
    fn wire_round_trip() {
        let mut buf = Vec::new();
        wire::write(
            &mut buf,
            &Down::Event {
                name: "hello".into(),
                kind: "tap".into(),
                value: 0,
            },
        )
        .unwrap();
        wire::write(
            &mut buf,
            &Up::Set {
                bind: "cpu".into(),
                value: 42.into(),
            },
        )
        .unwrap();
        let mut r = &buf[..];
        assert_eq!(
            wire::read::<Down>(&mut r).unwrap(),
            Some(Down::Event {
                name: "hello".into(),
                kind: "tap".into(),
                value: 0
            })
        );
        assert_eq!(
            wire::read::<Up>(&mut r).unwrap(),
            Some(Up::Set {
                bind: "cpu".into(),
                value: 42.into()
            })
        );
        assert!(wire::read::<Down>(&mut r).is_err());
    }

    #[test]
    fn unknown_types_are_skipped() {
        let body = br#"{"type":"future","x":1}"#;
        let mut buf = (body.len() as u32).to_le_bytes().to_vec();
        buf.extend_from_slice(body);
        assert_eq!(
            wire::read::<Down>(&mut &buf[..]).unwrap(),
            Some(Down::Unknown)
        );
    }
}
