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
    /// The largest side `set_image` sends (P50-M); fenixOS scales it to the element's size.
    pub const MAX_IMAGE: usize = 120;

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
        /// Pixels for the layout's bound image (P50-M, fenixOS 0.1.25): `data` is base64 RGB888,
        /// `w` x `h` at most [`MAX_IMAGE`] each.
        #[serde(rename = "set_image")]
        SetImage {
            bind: String,
            w: u16,
            h: u16,
            data: String,
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

    /// Show a picture in the layout's bound `image` (album art; fenixOS 0.1.25). `rgba` is
    /// `w` x `h` RGBA8 (transparent parts go black). fenixOS scales it to fill the element and
    /// crops the middle; send it once per change, not per frame. False if the sizes do not match.
    pub fn set_image(&self, bind: &str, w: usize, h: usize, rgba: &[u8]) -> bool {
        if w == 0 || h == 0 || rgba.len() != w * h * 4 {
            return false;
        }
        let rgb: Vec<u8> = rgba
            .chunks_exact(4)
            .flat_map(|p| {
                let a = p[3] as u32;
                [0, 1, 2].map(|c| (p[c] as u32 * a / 255) as u8)
            })
            .collect();
        let m = wire::MAX_IMAGE;
        let (rgb, w, h) = if w > m || h > m {
            let s = (m as f64 / w as f64).min(m as f64 / h as f64);
            let (tw, th) = (
                ((w as f64 * s) as usize).max(1),
                ((h as f64 * s) as usize).max(1),
            );
            (image::fit(&rgb, w, h, tw, th), tw, th)
        } else {
            (rgb, w, h)
        };
        self.send(&wire::Up::SetImage {
            bind: bind.into(),
            w: w as u16,
            h: h as u16,
            data: wire::encode(&rgb),
        });
        true
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

/// Picture helpers for the bound image (P50-M), shared with the fenixOS PC app.
pub mod image {
    /// Scale RGB888 `w` x `h` to cover `tw` x `th`, cropping the middle; each target pixel is
    /// the average of the source pixels under it (so it works up and down).
    pub fn fit(rgb: &[u8], w: usize, h: usize, tw: usize, th: usize) -> Vec<u8> {
        let s = (tw as f64 / w as f64).max(th as f64 / h as f64);
        let (cw, ch) = (tw as f64 / s, th as f64 / s);
        let (x0, y0) = ((w as f64 - cw) / 2.0, (h as f64 - ch) / 2.0);
        let span = |o: f64, step: f64, i: usize, n: usize| {
            let a = ((o + i as f64 * step).floor() as usize).min(n - 1);
            let b = ((o + (i + 1) as f64 * step).ceil() as usize).clamp(a + 1, n);
            (a, b)
        };
        let mut out = Vec::with_capacity(tw * th * 3);
        for ty in 0..th {
            let (ya, yb) = span(y0, ch / th as f64, ty, h);
            for tx in 0..tw {
                let (xa, xb) = span(x0, cw / tw as f64, tx, w);
                let mut sum = [0u32; 3];
                for y in ya..yb {
                    for x in xa..xb {
                        let p = (y * w + x) * 3;
                        for c in 0..3 {
                            sum[c] += rgb[p + c] as u32;
                        }
                    }
                }
                let n = ((yb - ya) * (xb - xa)) as u32;
                out.extend(sum.map(|v| ((v + n / 2) / n) as u8));
            }
        }
        out
    }

    /// RGB888 to RGB565 little-endian (the board's image format).
    pub fn rgb565_le(rgb: &[u8]) -> Vec<u8> {
        rgb.chunks_exact(3)
            .flat_map(|p| {
                let v = (p[0] as u16 >> 3) << 11 | (p[1] as u16 >> 2) << 5 | p[2] as u16 >> 3;
                v.to_le_bytes()
            })
            .collect()
    }
}

/// Text the board can draw (P51-M). The text fonts hold printable ASCII, Latin-1 (U+00A0 to
/// U+00FF, fenixOS 0.1.26) and `– — ’ • … ‹ ›`; the CJK font on the SD card, when installed, adds
/// Chinese, Japanese and Korean. Anything else draws nothing, so pass titles through [`text::fit`].
pub mod text {
    /// The marks outside Latin-1 that the text fonts hold.
    pub const MARKS: &str = "–—’•…‹›";

    /// True when the board's built-in text fonts draw `c` (fenixOS 0.1.26 and newer).
    pub fn builtin(c: char) -> bool {
        matches!(c, ' '..='~' | '\u{A0}'..='\u{AC}' | '\u{AE}'..='\u{FF}') || MARKS.contains(c)
    }

    /// True when the CJK font on the card draws `c` (`docs/fonts.md` ranges), if it is installed.
    pub fn cjk(c: char) -> bool {
        matches!(c as u32, 0x3000..=0x30FF | 0x3130..=0x318F | 0x31F0..=0x31FF | 0x4E00..=0x9FFF
            | 0xAC00..=0xD7A3 | 0xFF00..=0xFFEF)
    }

    // A base letter plus a combining mark that Latin-1 holds composed ("e" + U+0301 is "é").
    const COMPOSE: [(char, &str); 7] = [
        ('\u{300}', "AÀEÈIÌOÒUÙaàeèiìoòuù"),
        ('\u{301}', "AÁEÉIÍOÓUÚYÝaáeéiíoóuúyý"),
        ('\u{302}', "AÂEÊIÎOÔUÛaâeêiîoôuû"),
        ('\u{303}', "AÃNÑOÕaãnñoõ"),
        ('\u{308}', "AÄEËIÏOÖUÜaäeëiïoöuüyÿ"),
        ('\u{30A}', "AÅaå"),
        ('\u{327}', "CÇcç"),
    ];

    fn compose(base: char, mark: char) -> Option<char> {
        let (_, pairs) = COMPOSE.iter().find(|(m, _)| *m == mark)?;
        let v: Vec<char> = pairs.chars().collect();
        v.chunks(2).find(|p| p[0] == base).map(|p| p[1])
    }

    fn emoji(c: char) -> bool {
        matches!(c as u32, 0x1F000..=0x1FAFF | 0x2600..=0x27BF | 0x2B00..=0x2BFF | 0x2300..=0x23FF)
    }

    // Draws nothing and means nothing on its own: zero-width marks, variation selectors, skin
    // tones, tag characters, the soft hyphen, other control characters.
    fn invisible(c: char) -> bool {
        matches!(c as u32, 0xAD | 0x200B..=0x200F | 0x2060..=0x2064 | 0xFE00..=0xFE0F | 0xFEFF
            | 0x1F3FB..=0x1F3FF | 0xE0000..=0xE007F)
            || (c.is_control() && c != '\n')
    }

    /// `s` in characters the board draws: kept when it can (Latin-1, and CJK when `cjk_font`),
    /// else transliterated (`Ł` to `L`, `Привет` to `Privet`, `北京` to `Bei Jing` without the CJK
    /// font, `“x”` to `"x"`), else `?` (an emoji, with its joined parts, is one `?`). A combining
    /// accent is composed when Latin-1 has the result, else dropped. Tabs become spaces; newlines
    /// stay; runs of spaces made by the transliteration are folded and the ends trimmed.
    pub fn fit(s: &str, cjk_font: bool) -> String {
        let mut out = String::with_capacity(s.len());
        let mut joined = false; // after a zero-width joiner: the next emoji is part of the last one
        for c in s.chars() {
            if c == '\u{200D}' {
                joined = true;
                continue;
            }
            let join = std::mem::take(&mut joined);
            if c == '\t' {
                out.push(' ');
            } else if builtin(c) || c == '\n' || (cjk_font && cjk(c)) {
                out.push(c);
            } else if invisible(c) {
            } else if ('\u{300}'..='\u{36F}').contains(&c) {
                if let Some(k) = out.pop().and_then(|b| compose(b, c).or(Some(b))) {
                    out.push(k);
                }
            } else if emoji(c) {
                if !join {
                    out.push('?');
                }
            } else {
                match deunicode::deunicode_char(c) {
                    // Ideographs come with a space each ("Bei Jing "); the fold below keeps one.
                    Some(t) if !t.is_empty() && t.chars().all(builtin) => out.push_str(t),
                    _ => out.push('?'),
                }
            }
        }
        let mut folded = String::with_capacity(out.len());
        for c in out.chars() {
            if c == ' ' && folded.ends_with(' ') {
                continue;
            }
            folded.push(c);
        }
        folded.trim_matches(' ').to_string()
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
    fn text_fit() {
        use super::text::{builtin, fit};
        // Latin-1 and the marks stay; so does a newline.
        assert_eq!(
            fit("Café · Ñandú – 20 °C\nÅ ß", false),
            "Café · Ñandú – 20 °C\nÅ ß"
        );
        // Transliterated: Latin Extended, Cyrillic, Greek, quotes.
        assert_eq!(fit("Łódź Dvořák", false), "Lódz Dvorák");
        assert_eq!(fit("Привет", false), "Privet");
        assert_eq!(fit("“Ωmega”", false), "\"Omega\"");
        // CJK: kept with the card font, transliterated without it.
        assert_eq!(fit("北京 東京", true), "北京 東京");
        assert_eq!(fit("北京", false), "Bei Jing");
        // A combining accent composes when Latin-1 has it, else it goes.
        assert_eq!(fit("Cafe\u{301} n\u{303}o s\u{30C}", false), "Café ño s");
        // An emoji is one '?', with its skin tone, selector and joined parts; zero-width marks go.
        assert_eq!(
            fit("Hi 👋🏽 👨\u{200D}👩\u{200D}👧 ❤\u{FE0F}\u{200B}!", false),
            "Hi ? ? ?!"
        );
        // Tabs, soft hyphens, runs of spaces, the ends.
        assert_eq!(fit("\ta\u{AD}b\t\tc ", false), "ab c");
        assert!(fit("Ölçek 日本語 Ελληνικά ✓ 🎵", false)
            .chars()
            .all(|c| builtin(c) || c == '\n'));
    }

    #[test]
    fn image_fit_and_pack() {
        use super::image::{fit, rgb565_le};
        // 2 x 2 down to 1 x 1: the average.
        let px = [0, 0, 0, 255, 255, 255, 0, 0, 0, 255, 255, 255];
        assert_eq!(fit(&px, 2, 2, 1, 1), vec![128, 128, 128]);
        // 4 x 1 (red, green, green, blue) covering 2 x 1... keeps all; 1 x 1 crops to the middle.
        let row = [255, 0, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255];
        assert_eq!(fit(&row, 4, 1, 1, 1), vec![0, 255, 0]);
        // Up: 1 x 1 to 2 x 2 repeats it.
        assert_eq!(fit(&[9, 8, 7], 1, 1, 2, 2), [9, 8, 7].repeat(4));
        assert_eq!(
            rgb565_le(&[255, 0, 0, 0, 0, 255]),
            vec![0x00, 0xF8, 0x1F, 0x00]
        );
        // The largest picture fits in one message.
        let max = super::wire::MAX_IMAGE;
        assert!((max * max * 3).div_ceil(3) * 4 < super::wire::MAX_MSG - 256);
    }

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
