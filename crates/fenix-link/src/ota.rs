//! Firmware updates over the link (P52, `docs/ota.md`): the signed package (`.fwp`) and the
//! sender for `FW_BEGIN`..`FW_RESULT`. The board checks the hash and signature; the PC only moves
//! bytes, so a package that fails here is malformed, not untrusted.
//!
//! Flow: `begin()` goes out; the board answers `FW_ACK` with the offset to start from (more than 0
//! when it resumes a package staged on its SD card) or `FW_RESULT` with a refusal. Then `poll()`
//! keeps `WINDOW` bytes of chunks in flight, goes back to the last acked offset when acks stop for
//! a second, and sends `FW_END` once everything is acked. `FW_RESULT` ends it; on OK the board
//! restarts into the new firmware.

use std::time::{Duration, Instant};

use fenix_proto::{Msg, MAX_CHUNK};

/// A parsed `.fwp`: `"FXFW" || format:u8 (1) || version:str || size:u32 || sha256:32 ||
/// signature:64 || image` (integers little-endian, str = len:u8 || bytes).
#[derive(Debug, Clone)]
pub struct Fwp {
    pub version: String,
    pub sha256: [u8; 32],
    pub signature: [u8; 64],
    pub image: Vec<u8>,
}

pub fn parse_fwp(data: &[u8]) -> Result<Fwp, String> {
    let bad = || "not a fenixOS firmware package".to_string();
    if data.len() < 6 || &data[..4] != b"FXFW" {
        return Err(bad());
    }
    if data[4] != 1 {
        return Err(format!(
            "firmware package format {} (this app reads 1)",
            data[4]
        ));
    }
    let vlen = data[5] as usize;
    let mut p = 6 + vlen;
    if vlen == 0 || data.len() < p + 4 + 32 + 64 {
        return Err(bad());
    }
    let version = std::str::from_utf8(&data[6..p])
        .map_err(|_| bad())?
        .to_string();
    let size = u32::from_le_bytes(data[p..p + 4].try_into().unwrap()) as usize;
    p += 4;
    let sha256: [u8; 32] = data[p..p + 32].try_into().unwrap();
    p += 32;
    let signature: [u8; 64] = data[p..p + 64].try_into().unwrap();
    p += 64;
    if data.len() - p != size || size == 0 {
        return Err("the firmware package is cut short".into());
    }
    Ok(Fwp {
        version,
        sha256,
        signature,
        image: data[p..].to_vec(),
    })
}

/// Bytes in flight before an ack (the board's link task writes each chunk to the card or flash).
pub const WINDOW: usize = 4 * MAX_CHUNK;
/// No ack moved the offset for this long: the board went away or gave up (it aborts after 10 s).
const STALL: Duration = Duration::from_secs(12);
/// After `FW_END`: the board checks the signature and copies the image into the spare slot.
const FINISH: Duration = Duration::from_secs(120);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OtaStage {
    /// `FW_BEGIN` sent; no answer yet.
    Starting,
    Sending,
    /// `FW_END` sent; the board checks and installs.
    Installing,
}

pub struct OtaSend {
    fwp: Fwp,
    stage: OtaStage,
    sent: usize,
    acked: usize,
    /// Where the board resumed (more than 0 for a package already partly on its card).
    pub resumed_at: usize,
    /// The last ack that moved the offset (or the start of a stage).
    last_progress: Instant,
    last_resend: Instant,
}

impl OtaSend {
    pub fn new(fwp: Fwp) -> Self {
        let now = Instant::now();
        OtaSend {
            fwp,
            stage: OtaStage::Starting,
            sent: 0,
            acked: 0,
            resumed_at: 0,
            last_progress: now,
            last_resend: now,
        }
    }

    pub fn version(&self) -> &str {
        &self.fwp.version
    }

    pub fn size(&self) -> usize {
        self.fwp.image.len()
    }

    pub fn stage(&self) -> OtaStage {
        self.stage
    }

    /// The first message; send it once.
    pub fn begin(&self) -> Msg {
        Msg::FwBegin {
            version: self.fwp.version.clone(),
            size: self.fwp.image.len() as u32,
            sha256: self.fwp.sha256,
            signature: self.fwp.signature,
        }
    }

    /// An `FW_ACK` from the board.
    pub fn on_ack(&mut self, next: u32) {
        let next = (next as usize).min(self.fwp.image.len());
        let now = Instant::now();
        if self.stage == OtaStage::Starting {
            self.stage = OtaStage::Sending;
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

    /// The messages to send now: chunks within the window, a resend after a quiet second, and
    /// `FW_END` once all is acked. `Err` when the board stopped answering.
    pub fn poll(&mut self) -> Result<Vec<Msg>, String> {
        let now = Instant::now();
        let mut out = Vec::new();
        match self.stage {
            OtaStage::Starting => {
                if now - self.last_progress > STALL {
                    return Err("the board did not answer the update".into());
                }
            }
            OtaStage::Sending => {
                if now - self.last_progress > STALL {
                    return Err("the board stopped taking the firmware".into());
                }
                // A chunk was lost (the board acks the offset it still expects): after a quiet
                // second, send again from there.
                let quiet = now - self.last_progress.max(self.last_resend);
                if self.sent > self.acked && quiet > Duration::from_secs(1) {
                    self.sent = self.acked;
                    self.last_resend = now;
                }
                let len = self.fwp.image.len();
                while self.sent < len && self.sent < self.acked + WINDOW {
                    let n = (len - self.sent).min(MAX_CHUNK);
                    out.push(Msg::FwChunk {
                        offset: self.sent as u32,
                        data: self.fwp.image[self.sent..self.sent + n].to_vec(),
                    });
                    self.sent += n;
                }
                if self.acked == len {
                    out.push(Msg::FwEnd);
                    self.stage = OtaStage::Installing;
                    self.last_progress = now;
                }
            }
            OtaStage::Installing => {
                if now - self.last_progress > FINISH {
                    return Err("the board did not finish installing".into());
                }
            }
        }
        Ok(out)
    }

    /// Bytes the board has, 0 to 100.
    pub fn percent(&self) -> u8 {
        (self.acked as u64 * 100 / self.fwp.image.len().max(1) as u64) as u8
    }
}

/// The text for an `FW_RESULT` code, when the board sent none.
pub fn result_text(code: u8) -> &'static str {
    use fenix_proto::fw;
    match code {
        fw::OK => "installed",
        fw::NO_SPACE => "not enough room on the board",
        fw::BAD_HASH => "the firmware was damaged on the way",
        fw::BAD_SIGNATURE => "the package is not signed by fenixOS",
        fw::BUSY => "the board is busy with another update",
        fw::STORAGE => "the board could not write the firmware",
        fw::OLDER => "the board runs a newer version",
        _ => "the board refused the update",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pkg(image: &[u8]) -> Vec<u8> {
        let mut v = b"FXFW\x01\x061.2.30".to_vec();
        v.extend((image.len() as u32).to_le_bytes());
        v.extend([7u8; 32]);
        v.extend([9u8; 64]);
        v.extend(image);
        v
    }

    #[test]
    fn parse_ok_and_bad() {
        let f = parse_fwp(&pkg(&[1, 2, 3])).unwrap();
        assert_eq!(f.version, "1.2.30");
        assert_eq!(f.image, [1, 2, 3]);
        assert_eq!(f.sha256, [7; 32]);
        let mut short = pkg(&[1, 2, 3]);
        short.pop();
        assert!(parse_fwp(&short).is_err());
        assert!(parse_fwp(b"FXFW").is_err());
        let mut fmt = pkg(&[1]);
        fmt[4] = 2;
        assert!(parse_fwp(&fmt).is_err());
    }

    #[test]
    fn sends_window_resumes_and_ends() {
        let image = vec![0u8; 5 * MAX_CHUNK + 10];
        let mut s = OtaSend::new(parse_fwp(&pkg(&image)).unwrap());
        assert!(s.poll().unwrap().is_empty()); // nothing before the board answers
        s.on_ack(MAX_CHUNK as u32); // resume after the first chunk
        assert_eq!(s.resumed_at, MAX_CHUNK);
        let m = s.poll().unwrap();
        assert_eq!(m.len(), 4);
        assert!(matches!(m[0], Msg::FwChunk { offset, .. } if offset as usize == MAX_CHUNK));
        s.on_ack(5 * MAX_CHUNK as u32);
        let m = s.poll().unwrap();
        assert_eq!(m.len(), 1); // the last 10 bytes
        s.on_ack(image.len() as u32);
        assert_eq!(s.percent(), 100);
        let m = s.poll().unwrap();
        assert!(matches!(m.last(), Some(Msg::FwEnd)));
        assert_eq!(s.stage(), OtaStage::Installing);
    }
}
