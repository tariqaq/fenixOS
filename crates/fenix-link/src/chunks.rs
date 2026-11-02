//! Chunked uploads with acks, shared by firmware updates (P52, `FW_*`) and file writes (P53,
//! `FILE_*`). The board acks the offset it expects next after each chunk, so the PC keeps
//! `WINDOW` bytes in flight, goes back to the last acked offset when acks stop for a second, and
//! sends the end message once everything is acked.
//!
//! Flow: send `begin` once (built by the caller); the board's first ack gives the offset to start
//! from (more than 0 when it resumes a part already on its card). Then call `poll()` often and
//! `on_ack()` for each ack, until the caller sees the result message.

use std::time::{Duration, Instant};

use fenix_proto::{Msg, MAX_CHUNK};

/// Bytes in flight before an ack (the board's link task writes each chunk to the card or flash).
pub const WINDOW: usize = 4 * MAX_CHUNK;
/// No ack moved the offset for this long: the board went away or gave up (it closes after 10 s).
const STALL: Duration = Duration::from_secs(12);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    /// `FW_CHUNK` / `FW_END`.
    Firmware,
    /// `FILE_CHUNK` / `FILE_END`.
    File,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Stage {
    /// The begin message went out; no answer yet.
    Starting,
    Sending,
    /// The end message went out; the board checks (and installs a firmware).
    Finishing,
}

pub struct ChunkSend {
    kind: Kind,
    data: Vec<u8>,
    stage: Stage,
    sent: usize,
    acked: usize,
    /// Where the board resumed (more than 0 for a part already on its card).
    pub resumed_at: usize,
    /// The last ack that moved the offset (or the start of a stage).
    last_progress: Instant,
    last_resend: Instant,
    finish: Duration,
}

impl ChunkSend {
    /// `finish`: how long the board may take after the end message (a firmware is checked and
    /// copied into a slot; a file is read back for its sha256).
    pub fn new(kind: Kind, data: Vec<u8>, finish: Duration) -> Self {
        let now = Instant::now();
        ChunkSend {
            kind,
            data,
            stage: Stage::Starting,
            sent: 0,
            acked: 0,
            resumed_at: 0,
            last_progress: now,
            last_resend: now,
            finish,
        }
    }

    pub fn data(&self) -> &[u8] {
        &self.data
    }

    pub fn size(&self) -> usize {
        self.data.len()
    }

    pub fn stage(&self) -> Stage {
        self.stage
    }

    /// An ack from the board (`FW_ACK`, `FILE_ACK`).
    pub fn on_ack(&mut self, next: u32) {
        let next = (next as usize).min(self.data.len());
        let now = Instant::now();
        if self.stage == Stage::Starting {
            self.stage = Stage::Sending;
            self.resumed_at = next;
            self.sent = next;
            self.acked = next;
            self.last_progress = now;
            return;
        }
        if next > self.acked {
            self.acked = next;
            self.last_progress = now;
        }
    }

    fn chunk(&self, offset: usize, n: usize) -> Msg {
        let (offset, data) = (offset as u32, self.data[offset..offset + n].to_vec());
        match self.kind {
            Kind::Firmware => Msg::FwChunk { offset, data },
            Kind::File => Msg::FileChunk { offset, data },
        }
    }

    /// The messages to send now: chunks within the window, a resend after a quiet second, and
    /// the end message once all is acked. `Err` when the board stopped answering.
    pub fn poll(&mut self) -> Result<Vec<Msg>, String> {
        let now = Instant::now();
        let what = match self.kind {
            Kind::Firmware => "the firmware",
            Kind::File => "the file",
        };
        let mut out = Vec::new();
        match self.stage {
            Stage::Starting => {
                if now - self.last_progress > STALL {
                    return Err(format!("the board did not answer ({what})"));
                }
            }
            Stage::Sending => {
                if now - self.last_progress > STALL {
                    return Err(format!("the board stopped taking {what}"));
                }
                // A chunk was lost (the board acks the offset it still expects): after a quiet
                // second, send again from there.
                let quiet = now - self.last_progress.max(self.last_resend);
                if self.sent > self.acked && quiet > Duration::from_secs(1) {
                    self.sent = self.acked;
                    self.last_resend = now;
                }
                let len = self.data.len();
                while self.sent < len && self.sent < self.acked + WINDOW {
                    let n = (len - self.sent).min(MAX_CHUNK);
                    out.push(self.chunk(self.sent, n));
                    self.sent += n;
                }
                if self.acked == len {
                    out.push(match self.kind {
                        Kind::Firmware => Msg::FwEnd,
                        Kind::File => Msg::FileEnd,
                    });
                    self.stage = Stage::Finishing;
                    self.last_progress = now;
                }
            }
            Stage::Finishing => {
                if now - self.last_progress > self.finish {
                    return Err(format!("the board did not finish with {what}"));
                }
            }
        }
        Ok(out)
    }

    /// Bytes the board has, 0 to 100.
    pub fn percent(&self) -> u8 {
        (self.acked as u64 * 100 / self.data.len().max(1) as u64) as u8
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sends_window_resumes_and_ends() {
        let data = vec![0u8; 5 * MAX_CHUNK + 10];
        let mut s = ChunkSend::new(Kind::File, data.clone(), Duration::from_secs(5));
        assert!(s.poll().unwrap().is_empty()); // nothing before the board answers
        s.on_ack(MAX_CHUNK as u32); // resume after the first chunk
        assert_eq!(s.resumed_at, MAX_CHUNK);
        let m = s.poll().unwrap();
        assert_eq!(m.len(), 4);
        assert!(matches!(m[0], Msg::FileChunk { offset, .. } if offset as usize == MAX_CHUNK));
        s.on_ack(5 * MAX_CHUNK as u32);
        let m = s.poll().unwrap();
        assert_eq!(m.len(), 1); // the last 10 bytes
        s.on_ack(data.len() as u32);
        assert_eq!(s.percent(), 100);
        let m = s.poll().unwrap();
        assert!(matches!(m.last(), Some(Msg::FileEnd)));
        assert_eq!(s.stage(), Stage::Finishing);
    }

    #[test]
    fn empty_file_ends_at_once() {
        let mut s = ChunkSend::new(Kind::File, Vec::new(), Duration::from_secs(5));
        s.on_ack(0);
        let m = s.poll().unwrap();
        assert!(matches!(m.as_slice(), [Msg::FileEnd]));
    }
}
