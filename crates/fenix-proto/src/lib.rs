//! fenixOS link protocol. Spec: `proto/spec.md`.
//!
//! Frame on the wire: `COBS(payload ‖ crc16_le) ‖ 0x00`,
//! payload: `type:u8 ‖ seq:u8 ‖ body`.

mod codec;
mod msg;

pub use codec::{cobs_decode, cobs_encode, crc16, decode_frame, encode_frame, FrameDecoder, FrameError};
pub use msg::*;

pub const PROTO_VERSION: u8 = 1;
pub const MAGIC: [u8; 4] = *b"FNXO";
pub const MAX_PAYLOAD: usize = 1200;
/// Max data bytes in one `APP_CHUNK` or `STREAM_RECT`.
pub const MAX_CHUNK: usize = 1024;
/// Link baud rate (spike S3, `docs/board-info.md`).
pub const BAUD: u32 = 460_800;
/// CH340 USB ids of the ESP32-2432S028R.
pub const USB_VID: u16 = 0x1A86;
pub const USB_PID: u16 = 0x7523;
