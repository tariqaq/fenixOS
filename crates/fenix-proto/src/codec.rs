use crate::MAX_PAYLOAD;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FrameError {
    Cobs,
    TooShort,
    TooLong,
    Crc { expected: u16, got: u16 },
    BadBody(&'static str),
    UnknownType(u8),
}

impl std::fmt::Display for FrameError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}

impl std::error::Error for FrameError {}

/// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
pub fn crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &b in data {
        crc ^= (b as u16) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 { (crc << 1) ^ 0x1021 } else { crc << 1 };
        }
    }
    crc
}

/// COBS encode. Output contains no zero bytes; delimiter not included.
pub fn cobs_encode(input: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(input.len() + input.len() / 254 + 2);
    let mut code_idx = 0;
    out.push(0);
    let mut code: u8 = 1;
    for &b in input {
        if b == 0 {
            out[code_idx] = code;
            code_idx = out.len();
            out.push(0);
            code = 1;
        } else {
            out.push(b);
            code += 1;
            if code == 0xFF {
                out[code_idx] = code;
                code_idx = out.len();
                out.push(0);
                code = 1;
            }
        }
    }
    out[code_idx] = code;
    out
}

/// COBS decode (input without delimiter).
pub fn cobs_decode(input: &[u8]) -> Result<Vec<u8>, FrameError> {
    let mut out = Vec::with_capacity(input.len());
    let mut i = 0;
    while i < input.len() {
        let code = input[i];
        if code == 0 {
            return Err(FrameError::Cobs);
        }
        i += 1;
        let n = code as usize - 1;
        if i + n > input.len() {
            return Err(FrameError::Cobs);
        }
        out.extend_from_slice(&input[i..i + n]);
        i += n;
        if code != 0xFF && i < input.len() {
            out.push(0);
        }
    }
    Ok(out)
}

/// payload -> wire bytes (including trailing 0x00).
pub fn encode_frame(payload: &[u8]) -> Vec<u8> {
    let mut buf = Vec::with_capacity(payload.len() + 2);
    buf.extend_from_slice(payload);
    buf.extend_from_slice(&crc16(payload).to_le_bytes());
    let mut out = cobs_encode(&buf);
    out.push(0);
    out
}

/// wire bytes of one frame (without the 0x00 delimiter) -> payload.
pub fn decode_frame(encoded: &[u8]) -> Result<Vec<u8>, FrameError> {
    let mut buf = cobs_decode(encoded)?;
    if buf.len() < 4 {
        return Err(FrameError::TooShort);
    }
    if buf.len() > MAX_PAYLOAD + 2 {
        return Err(FrameError::TooLong);
    }
    let n = buf.len() - 2;
    let got = u16::from_le_bytes([buf[n], buf[n + 1]]);
    let expected = crc16(&buf[..n]);
    if got != expected {
        return Err(FrameError::Crc { expected, got });
    }
    buf.truncate(n);
    Ok(buf)
}

/// Streaming splitter: feed raw bytes, get decoded payloads.
#[derive(Default)]
pub struct FrameDecoder {
    buf: Vec<u8>,
    overflow: bool,
    pub errors: u32,
}

impl FrameDecoder {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn push(&mut self, data: &[u8], mut on_payload: impl FnMut(Vec<u8>)) {
        for &b in data {
            if b == 0 {
                if !self.buf.is_empty() {
                    if self.overflow {
                        self.errors += 1;
                    } else {
                        match decode_frame(&self.buf) {
                            Ok(p) => on_payload(p),
                            Err(_) => self.errors += 1,
                        }
                    }
                }
                self.buf.clear();
                self.overflow = false;
            } else if self.buf.len() < (MAX_PAYLOAD + 2) * 2 {
                self.buf.push(b);
            } else {
                self.overflow = true;
            }
        }
    }
}
