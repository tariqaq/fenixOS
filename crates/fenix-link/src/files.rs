//! Files on the board's SD card, under `/sd/fenixos` (P53, `proto/spec.md` "Files (0x6_)").
//! Paths are relative to that folder, split by `/`. The board lists and reads anything there,
//! deletes in `fonts/ data/ wallpaper/ logs/ crash/ shots/`, and takes writes only into
//! `fonts/ data/ wallpaper/`.
//!
//! A write is `FILE_WRITE_BEGIN` (size and sha256), chunks as firmware updates send them
//! (`chunks.rs`), `FILE_END`; the board checks the sha256 and answers `FILE_RESULT`. A cut write
//! goes on from where it stopped when the same file is sent again.

use std::time::Duration;

use fenix_proto::{file, Msg};
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
