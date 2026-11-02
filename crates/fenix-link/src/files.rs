//! Files on the board's SD card, under `/sd/fenixos` (P53, `proto/spec.md` "Files (0x6_)").
//! Paths are relative to that folder, split by `/`. The board lists and reads anything there,
//! deletes in `fonts/ data/ wallpaper/ logs/ crash/ shots/`, and takes writes only into
//! `fonts/ data/ wallpaper/`.
//!
//! A write is `FILE_WRITE_BEGIN` (size and sha256), chunks as firmware updates send them
//! (`chunks.rs`), `FILE_END`; the board checks the sha256 and answers `FILE_RESULT`. A cut write
//! goes on from where it stopped when the same file is sent again.
//!
//! `list` and `read` run one request-and-answer job each. They take `send` (put a message on
//! the link) and `recv` (the board's next file message, or `None` after the given time), so the
//! CLI and the PC app share them over their own links.

use std::time::{Duration, Instant};

use fenix_proto::{file, FileEntry, Msg};
use sha2::{Digest, Sha256};

use crate::chunks::{ChunkSend, Kind, Stage};

/// Bytes asked for per `FILE_READ_REQ` (one frame's worth).
pub const READ_LEN: u16 = fenix_proto::MAX_CHUNK as u16;
/// Read requests in flight; the board answers them in order.
pub const READ_WINDOW: usize = 4;

pub struct FileSend {
    path: String,
    sha256: [u8; 32],
    send: ChunkSend,
}

impl FileSend {
    pub fn new(path: &str, data: Vec<u8>) -> Self {
        let sha256 = Sha256::digest(&data).into();
        // The board reads the file back for its sha256: allow 30 s plus 1 s per 256 KB.
        let finish = Duration::from_secs(30 + data.len() as u64 / (256 * 1024));
        FileSend {
            path: path.to_string(),
            sha256,
            send: ChunkSend::new(Kind::File, data, finish),
        }
    }

    pub fn path(&self) -> &str {
        &self.path
    }

    pub fn size(&self) -> usize {
        self.send.size()
    }

    pub fn stage(&self) -> Stage {
        self.send.stage()
    }

    pub fn resumed_at(&self) -> usize {
        self.send.resumed_at
    }

    /// The first message; send it once.
    pub fn begin(&self) -> Msg {
        Msg::FileWriteBegin {
            size: self.send.size() as u32,
            sha256: self.sha256,
            path: self.path.clone(),
        }
    }

    /// A `FILE_ACK` from the board.
    pub fn on_ack(&mut self, next: u32) {
        self.send.on_ack(next);
    }

    pub fn poll(&mut self) -> Result<Vec<Msg>, String> {
        self.send.poll()
    }

    pub fn percent(&self) -> u8 {
        self.send.percent()
    }
}

/// A path the board would take: relative, `/`-separated, no empty, `.` or `..` part. The board
/// checks again; this gives a clear message before anything is sent.
pub fn path_ok(p: &str) -> bool {
    !p.is_empty()
        && p.len() < 128
        && p.split('/').all(|s| {
            !s.is_empty()
                && !s.ends_with('.')
                && !s.ends_with(' ')
                && !s.chars().any(|c| c < ' ' || "\\:*?\"<>|".contains(c))
        })
}

/// How long the board may stay silent during a list or a read.
const QUIET: Duration = Duration::from_secs(5);

fn refused(op: &str, path: &str, code: u8, text: &str) -> String {
    let why = if text.is_empty() {
        result_text(code)
    } else {
        text
    };
    format!("{op} {path}: {why}")
}

fn silent() -> String {
    "the board did not answer (firmware 0.1.23 or later has files)".into()
}

/// All entries of a folder (`""` is `/sd/fenixos` itself), page by page.
pub fn list(
    send: &dyn Fn(Msg),
    recv: &mut dyn FnMut(Duration) -> Option<Msg>,
    path: &str,
) -> Result<Vec<FileEntry>, String> {
    let mut all = Vec::new();
    loop {
        send(Msg::FileListReq {
            start: all.len() as u16,
            path: path.to_string(),
        });
        let end = Instant::now() + QUIET;
        let (total, entries) = loop {
            let left = end.saturating_duration_since(Instant::now());
            match recv(left).ok_or_else(silent)? {
                Msg::FileList { total, entries, .. } => break (total as usize, entries),
                Msg::FileResult {
                    op: file::OP_LIST,
                    code,
                    text,
                    ..
                } => return Err(refused("list", path, code, &text)),
                Msg::Error { code, text } => return Err(format!("board error {code}: {text}")),
                _ => continue,
            }
        };
        let none = entries.is_empty();
        all.extend(entries);
        if none || all.len() >= total {
            return Ok(all);
        }
    }
}

/// A whole file. Up to `READ_WINDOW` requests stay in flight; a lost answer is asked again.
/// `progress(bytes, total)` after each piece.
pub fn read(
    send: &dyn Fn(Msg),
    recv: &mut dyn FnMut(Duration) -> Option<Msg>,
    path: &str,
    progress: &mut dyn FnMut(usize, usize),
) -> Result<Vec<u8>, String> {
    let step = READ_LEN as usize;
    let req = |offset: usize| Msg::FileReadReq {
        offset: offset as u32,
        len: READ_LEN,
        path: path.to_string(),
    };
    send(req(0));
    let mut data: Vec<u8> = Vec::new();
    let mut asked = step;
    let mut last = Instant::now();
    loop {
        match recv(Duration::from_millis(500)) {
            Some(Msg::FileData {
                offset,
                total,
                data: d,
            }) => {
                last = Instant::now();
                let total = total as usize;
                if offset as usize == data.len() {
                    data.extend_from_slice(&d);
                    progress(data.len(), total);
                }
                if data.len() >= total {
                    data.truncate(total);
                    return Ok(data);
                }
                if offset as usize > data.len() {
                    asked = data.len(); // one went missing: from what we have
                }
                while asked < total && asked < data.len() + READ_WINDOW * step {
                    send(req(asked));
                    asked += step;
                }
            }
            Some(Msg::FileResult {
                op: file::OP_READ,
                code,
                text,
                ..
            }) => return Err(refused("read", path, code, &text)),
            Some(Msg::Error { code, text }) => return Err(format!("board error {code}: {text}")),
            _ => {
                if last.elapsed() > QUIET * 2 {
                    return Err(silent());
                }
                if last.elapsed() > Duration::from_secs(2) {
                    asked = data.len();
                    send(req(asked));
                    asked += step;
                    last = Instant::now() - Duration::from_secs(1);
                }
            }
        }
    }
}

/// Ask the board to save its screen to `shots/` on the card (`SHOT_SAVE`); the saved path.
pub fn shot(
    send: &dyn Fn(Msg),
    recv: &mut dyn FnMut(Duration) -> Option<Msg>,
) -> Result<String, String> {
    send(Msg::ShotSave);
    let end = Instant::now() + QUIET * 2; // the UI task reads the screen back and writes it
    loop {
        let left = end.saturating_duration_since(Instant::now());
        match recv(left.max(Duration::from_millis(1))) {
            Some(Msg::FileResult {
                op: file::OP_SHOT,
                code,
                path,
                text,
            }) => {
                return if code == file::OK {
                    Ok(path)
                } else {
                    Err(format!(
                        "screenshot: {}",
                        if text.is_empty() {
                            result_text(code)
                        } else {
                            &text
                        }
                    ))
                }
            }
            Some(Msg::Error { code, text }) => return Err(format!("board error {code}: {text}")),
            Some(_) if Instant::now() < end => continue,
            _ => return Err(silent()),
        }
    }
}

/// The text for a `FILE_RESULT` code, when the board sent none.
pub fn result_text(code: u8) -> &'static str {
    match code {
        file::OK => "done",
        file::NOT_FOUND => "not found",
        file::NOT_ALLOWED => "not allowed there",
        file::NO_CARD => "no SD card in the board",
        file::NO_SPACE => "not enough room on the card",
        file::BAD_HASH => "the file was damaged on the way",
        file::STORAGE => "the card could not be read or written",
        file::BUSY => "the board is busy with another file",
        _ => "the board refused",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn paths() {
        assert!(path_ok("fonts/cjk.ttf"));
        assert!(path_ok("data/com.x/history.bin"));
        assert!(!path_ok(""));
        assert!(!path_ok("/fonts/a"));
        assert!(!path_ok("fonts/../apps/x"));
        assert!(!path_ok("fonts/a."));
        assert!(!path_ok("fonts\\a"));
        assert!(!path_ok("fonts//a"));
    }

    #[test]
    fn list_pages_and_read_windows() {
        use std::cell::RefCell;
        let sent = RefCell::new(Vec::new());
        let entry = |n: &str| FileEntry {
            dir: false,
            size: 1,
            name: n.into(),
        };
        let mut answers = vec![
            Msg::FileList {
                start: 0,
                total: 3,
                path: "logs".into(),
                entries: vec![entry("1.txt"), entry("2.txt")],
            },
            Msg::FileList {
                start: 2,
                total: 3,
                path: "logs".into(),
                entries: vec![entry("3.txt")],
            },
        ]
        .into_iter();
        let got = list(
            &|m| sent.borrow_mut().push(m),
            &mut |_| answers.next(),
            "logs",
        )
        .unwrap();
        assert_eq!(got.len(), 3);
        assert!(matches!(
            sent.borrow()[1],
            Msg::FileListReq { start: 2, .. }
        ));

        let size = READ_LEN as usize * 2 + 5;
        let file: Vec<u8> = (0..size).map(|i| i as u8).collect();
        let sent = RefCell::new(Vec::<Msg>::new());
        let mut i = 0;
        let mut recv = |_| {
            let off = match sent.borrow().get(i) {
                Some(Msg::FileReadReq { offset, .. }) => *offset as usize,
                _ => return None,
            };
            i += 1;
            let end = (off + READ_LEN as usize).min(size);
            Some(Msg::FileData {
                offset: off as u32,
                total: size as u32,
                data: file[off..end].to_vec(),
            })
        };
        let back = read(
            &|m| sent.borrow_mut().push(m),
            &mut recv,
            "x",
            &mut |_, _| {},
        )
        .unwrap();
        assert_eq!(back, file);
    }

    #[test]
    fn list_refused() {
        let mut a = Some(Msg::FileResult {
            op: file::OP_LIST,
            code: file::NO_CARD,
            path: "logs".into(),
            text: String::new(),
        });
        let e = list(&|_| {}, &mut |_| a.take(), "logs").unwrap_err();
        assert!(e.contains("no SD card"), "{e}");
    }

    #[test]
    fn begin_has_sha() {
        let s = FileSend::new("fonts/a", b"abc".to_vec());
        match s.begin() {
            Msg::FileWriteBegin { size, sha256, path } => {
                assert_eq!(size, 3);
                assert_eq!(path, "fonts/a");
                assert_eq!(sha256[..4], [0xba, 0x78, 0x16, 0xbf]);
            }
            m => panic!("{m:?}"),
        }
    }
}
