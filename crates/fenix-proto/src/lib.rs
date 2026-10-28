//! fenixOS link protocol. Spec: `proto/spec.md`.
//!
//! Frame on the wire: `COBS(payload ‖ crc16_le) ‖ 0x00`,
//! payload: `type:u8 ‖ seq:u8 ‖ body`.

mod codec;

pub use codec::{cobs_decode, cobs_encode, crc16, decode_frame, encode_frame, FrameDecoder, FrameError};

pub const MAX_PAYLOAD: usize = 1200;
