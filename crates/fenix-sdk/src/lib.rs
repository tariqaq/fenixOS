//! Write the PC half of a fenixOS app. fenixOS starts the program with `--fenix-pipe <base>`;
//! [`App::connect`] opens the two pipes, and the app then reads [`Message`]s and sends values to
//! the board with [`App::set`]. [`App::put_file`] and [`App::get_file`] keep the app's files on
//! the board's SD card. Protocol: `docs/sidecars.md`.

use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::io;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
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
    /// The largest file `file_put` and `file_get` carry (base64 in one message).
    pub const MAX_FILE: usize = 32 * 1024;

    /// File bytes in a message: standard base64.
    pub fn encode(data: &[u8]) -> String {
        use base64::Engine;
        base64::engine::general_purpose::STANDARD.encode(data)
    }

    pub fn decode(text: &str) -> Option<Vec<u8>> {
        use base64::Engine;
        base64::engine::general_purpose::STANDARD.decode(text).ok()
    }

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
        /// The answer to a `file_put`, `file_get` or `file_delete` (P53). `data` (base64) only
        /// for a get that worked; `error` when `ok` is false.
        File {
            req: u32,
            ok: bool,
            #[serde(default, skip_serializing_if = "String::is_empty")]
            data: String,
            #[serde(default, skip_serializing_if = "String::is_empty")]
            error: String,
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
        /// Files in the app's folder on the board's SD card, `/sd/fenixos/data/<id>/` (P53).
        /// `name` is relative to it; `data` is base64, at most [`MAX_FILE`] bytes decoded.
        #[serde(rename = "file_put")]
        FilePut {
            req: u32,
            name: String,
            data: String,
        },
        #[serde(rename = "file_get")]
        FileGet {
            req: u32,
            name: String,
        },
        #[serde(rename = "file_delete")]
        FileDelete {
            req: u32,
            name: String,
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

type Answer = Result<Vec<u8>, String>;

struct Shared {
    open: AtomicBool,
    board: AtomicBool,
    stopped: AtomicBool,
    settings: Mutex<Map<String, Json>>,
    /// File requests waiting for fenixOS's answer, by `req`.
    files: Mutex<HashMap<u32, Sender<Answer>>>,
}

/// How long a file request waits: a 32 KB write over Wi-Fi takes a few seconds.
const FILE_WAIT: Duration = Duration::from_secs(30);

pub struct App {
    pub id: String,
    pub version: String,
    pub fenixos: String,
    up: Mutex<File>,
    rx: Receiver<Message>,
    shared: Arc<Shared>,
    next_req: AtomicU32,
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
            files: Mutex::new(HashMap::new()),
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
                        wire::Down::File {
                            req,
                            ok,
                            data,
                            error,
                        } => {
                            if let Some(w) = sh.files.lock().unwrap().remove(&req) {
                                let _ = w.send(if !ok {
                                    Err(error)
                                } else {
                                    wire::decode(&data).ok_or_else(|| "bad file data".into())
                                });
                            }
                            continue;
                        }
                        wire::Down::Stop => break,
                        wire::Down::Hello { .. } | wire::Down::Unknown => continue,
                    };
                    if tx.send(out).is_err() {
                        break;
                    }
                }
                sh.stopped.store(true, Ordering::Relaxed);
                sh.files.lock().unwrap().clear(); // waiting file requests fail at once
                let _ = tx.send(Message::Stop);
            })?;
        let app = App {
            id,
            version,
            fenixos,
            up: Mutex::new(up),
            rx,
            shared,
            next_req: AtomicU32::new(1),
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

    /// Show a built-in icon (`"pause"`, list in [`ICONS`]) on an `icon` element or a button bound
    /// to `bind` (fenixOS 0.1.25). Returns false, and sends nothing, for an unknown name.
    pub fn set_icon(&self, bind: &str, name: &str) -> bool {
        match ICONS.iter().position(|n| *n == name) {
            Some(i) => {
                self.set(bind, i as i32);
                true
            }
            None => false,
        }
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

    /// Save a file in this app's folder on the board's SD card (`/sd/fenixos/data/<id>/<name>`),
    /// replacing one of that name. At most 32 KB. Blocks until the board has it (a few seconds at
    /// most); fails when the board is not connected or has no card. `name` may hold `/` for
    /// subfolders. The folder goes when the app is removed from the board.
    pub fn put_file(&self, name: &str, data: &[u8]) -> Result<(), String> {
        if data.len() > wire::MAX_FILE {
            return Err(format!("{name}: over {} KB", wire::MAX_FILE / 1024));
        }
        self.file_req(|req| wire::Up::FilePut {
            req,
            name: name.into(),
            data: wire::encode(data),
        })
        .map(|_| ())
    }

    /// A file this app saved with [`App::put_file`]. Blocks like it.
    pub fn get_file(&self, name: &str) -> Result<Vec<u8>, String> {
        self.file_req(|req| wire::Up::FileGet {
            req,
            name: name.into(),
        })
    }

    /// Delete a file (or an empty subfolder) in this app's folder on the card.
    pub fn delete_file(&self, name: &str) -> Result<(), String> {
        self.file_req(|req| wire::Up::FileDelete {
            req,
            name: name.into(),
        })
        .map(|_| ())
    }

    fn file_req(&self, m: impl FnOnce(u32) -> wire::Up) -> Answer {
        let req = self.next_req.fetch_add(1, Ordering::Relaxed);
        let (tx, rx) = mpsc::channel();
        self.shared.files.lock().unwrap().insert(req, tx);
        self.send(&m(req));
        let r = rx.recv_timeout(FILE_WAIT);
        self.shared.files.lock().unwrap().remove(&req);
        match r {
            Ok(a) => a,
            Err(RecvTimeoutError::Disconnected) => Err("fenixOS stopped the app".into()),
            Err(RecvTimeoutError::Timeout) => {
                Err("no answer from fenixOS (0.1.23 or later keeps app files)".into())
            }
        }
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

/// The board's built-in icons, in its order (a copy of `fenix-pkg` ICONS, checked by a test). The
/// last 14 (from `play`) need fenixOS 0.1.25.
pub const ICONS: [&str; 34] = [
    "gear-six",
    "power",
    "screencast",
    "arrow-clockwise",
    "plugs-connected",
    "plugs",
    "sun",
    "moon",
    "timer",
    "device-rotate",
    "hand-tap",
    "hard-drives",
    "desktop-tower",
    "info",
    "squares-four",
    "warning",
    "puzzle-piece",
    "check-circle",
    "x-circle",
    "monitor",
    "play",
    "pause",
    "skip-forward",
    "skip-back",
    "speaker-high",
    "speaker-x",
    "microphone",
    "microphone-slash",
    "headphones",
    "game-controller",
    "music-notes",
    "video",
    "phone",
    "sparkle",
];

#[cfg(test)]
mod tests {
    #[test]
    fn icons_match_the_package_list() {
        assert_eq!(super::ICONS, fenix_pkg::ICONS);
    }

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
    fn file_messages() {
        let up = Up::FilePut {
            req: 7,
            name: "notes/a.txt".into(),
            data: wire::encode(b"hi"),
        };
        assert_eq!(
            serde_json::to_string(&up).unwrap(),
            r#"{"type":"file_put","req":7,"name":"notes/a.txt","data":"aGk="}"#
        );
        let down: Down =
            serde_json::from_str(r#"{"type":"file","req":7,"ok":false,"error":"no card"}"#)
                .unwrap();
        assert_eq!(
            down,
            Down::File {
                req: 7,
                ok: false,
                data: String::new(),
                error: "no card".into()
            }
        );
        assert_eq!(wire::decode("aGk=").unwrap(), b"hi");
        // A full-size file still fits one message with its fields.
        assert!(wire::MAX_FILE.div_ceil(3) * 4 < wire::MAX_MSG - 256);
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
