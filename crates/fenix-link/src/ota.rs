//! Firmware updates over the link (P52, `docs/ota.md`): the signed package (`.fwp`) and the
//! sender for `FW_BEGIN`..`FW_RESULT`. The board checks the hash and signature; the PC only moves
//! bytes, so a package that fails here is malformed, not untrusted.
//!
//! Flow: `begin()` goes out; the board answers `FW_ACK` with the offset to start from (more than 0
//! when it resumes a package staged on its SD card) or `FW_RESULT` with a refusal. Then `poll()`
//! sends the chunks (`chunks.rs`: a window of acks, resends, `FW_END`). `FW_RESULT` ends it; on
//! OK the board restarts into the new firmware.

use std::time::Duration;

use fenix_proto::Msg;

use crate::chunks::{ChunkSend, Kind};

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

/// After `FW_END`: the board checks the signature and copies the image into the spare slot.
const FINISH: Duration = Duration::from_secs(120);

/// `Starting`, `Sending`, then `Finishing` while the board checks and installs.
pub use crate::chunks::Stage as OtaStage;

pub struct OtaSend {
    version: String,
    sha256: [u8; 32],
    signature: [u8; 64],
    send: ChunkSend,
    /// Where the board resumed (more than 0 for a package already partly on its card).
    pub resumed_at: usize,
}

impl OtaSend {
    pub fn new(fwp: Fwp) -> Self {
        OtaSend {
            version: fwp.version,
            sha256: fwp.sha256,
            signature: fwp.signature,
            send: ChunkSend::new(Kind::Firmware, fwp.image, FINISH),
            resumed_at: 0,
        }
    }

    pub fn version(&self) -> &str {
        &self.version
    }

    pub fn size(&self) -> usize {
        self.send.size()
    }

    pub fn stage(&self) -> OtaStage {
        self.send.stage()
    }

    /// The first message; send it once.
    pub fn begin(&self) -> Msg {
        Msg::FwBegin {
            version: self.version.clone(),
            size: self.send.size() as u32,
            sha256: self.sha256,
            signature: self.signature,
        }
    }

    /// An `FW_ACK` from the board.
    pub fn on_ack(&mut self, next: u32) {
        self.send.on_ack(next);
        self.resumed_at = self.send.resumed_at;
    }

    /// The messages to send now (`chunks::ChunkSend::poll`). `Err` when the board stopped answering.
    pub fn poll(&mut self) -> Result<Vec<Msg>, String> {
        self.send.poll()
    }

    /// Bytes the board has, 0 to 100.
    pub fn percent(&self) -> u8 {
        self.send.percent()
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
    fn begin_and_resume() {
        use fenix_proto::MAX_CHUNK;
        let image = vec![0u8; 2 * MAX_CHUNK];
        let mut s = OtaSend::new(parse_fwp(&pkg(&image)).unwrap());
        assert!(matches!(s.begin(), Msg::FwBegin { size, .. } if size as usize == image.len()));
        s.on_ack(MAX_CHUNK as u32);
        assert_eq!(s.resumed_at, MAX_CHUNK);
        let m = s.poll().unwrap();
        assert!(matches!(m.as_slice(), [Msg::FwChunk { .. }]));
        s.on_ack(image.len() as u32);
        assert!(matches!(s.poll().unwrap().last(), Some(Msg::FwEnd)));
        assert_eq!(s.stage(), OtaStage::Finishing);
    }
}
