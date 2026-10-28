# fenixOS link protocol

Version: **proto 1** (fenixOS v0.1). Source of truth for both sides:
- Rust: `crates/fenix-proto` (also generates the golden vectors)
- C++: `board/components/fenix_proto`

Golden vectors: `proto/vectors.txt`. Both implementations decode every vector and encode it byte for byte. Regenerate only after an intended change and a `PROTO_VERSION` bump: `FENIX_REGEN=1 cargo test -p fenix-proto`.

The framing is kept from the FenixDeck PoC (`docs/legacy/protocol.md`), where it ran without errors.

## Transport

- USB serial through the CH340C at **921600 baud, 8N1, no flow control** (`BAUD` in both codecs; chosen in spike S3, `docs/board-info.md`).
- The PC opens the port with **DTR and RTS released**. Asserting them can reset the board through its auto-reset circuit.
- After a reset the ROM bootloader prints text at 115200. Receivers drop bytes until a valid frame arrives.
- Only `fenix-link` resets the board on purpose (reboot request, flashing).

## Framing

```
wire    = COBS( payload ‖ crc16_le ) ‖ 0x00
payload = type:u8 ‖ seq:u8 ‖ body
```

- COBS (Consistent Overhead Byte Stuffing); `0x00` ends a frame.
- CRC-16/CCITT-FALSE: poly `0x1021`, init `0xFFFF`, no reflection, no xorout, over `payload`, appended little-endian.
- **Max payload 1200 bytes.** Larger frames are dropped and counted as errors.
- `seq` counts per sender and wraps at 255. Diagnostics only.
- Integers are little-endian. No floats.
- `str` = `len:u8 ‖ utf8` (max 255 bytes). `bytes` = the rest of the body.
- A bad CRC or COBS error drops the frame and increments the receiver's error counter.

## Session

1. The PC sends `HELLO` every 300 ms for up to 2.5 s. The board answers `HELLO_ACK`.
   - If `proto` differs, the board answers `ERROR` (code 1). The PC reports "board firmware too old or too new" and offers to flash.
   - If the fenixOS versions differ, the session continues for `REBOOT` and flashing only. The PC shows the update dialog, and host apps stay off.
2. The PC sends `TIME_SET`, then `APP_LIST_REQ`, and reconciles apps (install missing, remove extra).
3. The PC sends `PING` every 1 s; the board answers `PONG`. Either side declares the link down after **3 s** without a valid frame.
4. The board sends `STATS` every 5 s, `BOARD_STATE` on every change, and `LOG` lines when it has something to say.

## Messages

`P→B` = PC to board, `B→P` = board to PC.

### Session (0x0_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x01 | HELLO | P→B | magic `"FNXO"` ‖ proto:u8 |
| 0x02 | HELLO_ACK | B→P | magic `"FNXO"` ‖ proto:u8 ‖ fw_major:u8 ‖ fw_minor:u8 ‖ fw_patch:u8 ‖ width:u16 ‖ height:u16 (current rotation) ‖ rotation:u8 ‖ free_heap:u32 ‖ apps_used_kb:u16 ‖ apps_total_kb:u16 ‖ reset_reason:u8 ‖ uptime_s:u32 ‖ time_valid:u8 ‖ mac:6 |
| 0x03 | PING | both | t_ms:u32 |
| 0x04 | PONG | both | t_ms:u32 (echo) |
| 0x05 | LOG | B→P | level:u8 (1 error, 2 warn, 3 info, 4 debug) ‖ text:bytes |
| 0x06 | ERROR | both | code:u8 ‖ text:bytes. Codes: 1 proto mismatch, 2 unknown type, 3 bad body, 4 busy |
| 0x07 | STATS | B→P | free_heap:u32 ‖ min_heap:u32 ‖ fps_x10:u16 ‖ rx_frames:u32 ‖ rx_errors:u32 ‖ stream_dropped:u32 |

### System (0x1_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x10 | TIME_SET | P→B | epoch_s:u32 (UTC). The board shows MYT (UTC+8) |
| 0x11 | REBOOT | P→B | (empty). The board answers nothing and restarts within 100 ms |
| 0x12 | BOARD_STATE | B→P | screen:u8 (0 boot, 1 home, 2 settings, 3 session, 4 stream, 5 app) ‖ dimmed:u8 ‖ rotation:u8 ‖ app_id:str (empty unless screen 5) |

### Apps (0x2_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x20 | APP_LIST_REQ | P→B | (empty) |
| 0x21 | APP_LIST | B→P | count:u8 ‖ count × (id:str ‖ version:str ‖ sha256:32) |
| 0x22 | APP_INSTALL_BEGIN | P→B | id:str ‖ version:str ‖ size:u32 ‖ sha256:32 |
| 0x23 | APP_CHUNK | P→B | offset:u32 ‖ data:bytes (≤ 1024) |
| 0x24 | APP_ACK | B→P | next_offset:u32. Sent after each chunk is written. The PC keeps at most 4 chunks in flight |
| 0x25 | APP_INSTALL_END | P→B | (empty). The board checks size and sha256, then commits |
| 0x26 | APP_RESULT | B→P | op:u8 (1 install, 2 remove) ‖ code:u8 ‖ id:str ‖ text:bytes. Codes: 0 ok, 1 no space, 2 bad hash, 3 bad package, 4 busy, 5 storage error, 6 unknown app |
| 0x27 | APP_REMOVE | P→B | id:str |
| 0x28 | APP_OPENED | B→P | id:str |
| 0x29 | APP_CLOSED | B→P | id:str |
| 0x2A | HOST_STATE | P→B | running:u8 ‖ id:str. `running` = the app's PC half is up |

### Bindings (0x3_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x30 | BIND_SET | P→B | bind:u16 ‖ kind:u8 ‖ id:str ‖ value. Kinds: 0 int (`i32`), 1 bool (`u8`), 2 text (`str`). Applies to the open app only; ignored otherwise |
| 0x31 | EVENT | B→P | elem:u16 ‖ kind:u8 (1 tap, 2 long press, 3 value change) ‖ value:i32 ‖ id:str |

### Stream (0x4_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x40 | STREAM_START | P→B | fps:u8 (target, 1–30) |
| 0x41 | STREAM_STOP | P→B | (empty) |
| 0x42 | STREAM_RECT | B→P | rect_id:u16 ‖ x:u16 ‖ y:u16 ‖ w:u16 ‖ h:u16 ‖ codec:u8 ‖ total:u32 ‖ offset:u32 ‖ data:bytes. One encoded rectangle may span several frames; the PC applies it when `offset + len(data) == total` |
| 0x43 | STREAM_SYNC | P→B | (empty). The board redraws the whole screen, so the next rects cover everything |
| 0x44 | STREAM_INFO | B→P | width:u16 ‖ height:u16 ‖ rotation:u8. Sent at stream start and on rotation change; the PC resizes its canvas |

Pixel data is RGB565 **big-endian** (the LCD's byte order). Codecs:
- `0` raw.
- `1` RLE16: a sequence of `count:u8 ‖ pixel:u16be`, where `count` is 1–255 repeats of the pixel.

## Versioning

`PROTO_VERSION = 1`. Any change to a body layout or a new required message bumps it.
