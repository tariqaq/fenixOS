# FenixDeck wire protocol v1

Source of truth for both sides:
- Rust: `crates/fenix-proto`
- C++: `board/components/fenix_proto`

Golden test vectors live in `crates/fenix-proto/vectors/vectors.txt`. Both implementations must decode them, and must encode them byte-identically.

## Transport

- USB serial (CH340), **460800 baud, 8N1**, no flow control. The protocol does not depend on the transport, so the same frames can later run over TCP/WebSocket.
- The PC opens the port with **DTR = false and RTS = false**. Any other DTR/RTS state can trigger the board's auto-reset circuit.
- After a board reset, the ROM bootloader prints text at 115200 baud. Receivers drop bytes until a valid frame arrives.

## Framing

```
wire  = COBS( payload ‖ crc16_le ) ‖ 0x00
payload = type:u8 ‖ seq:u8 ‖ body
```

- **COBS:** standard Consistent Overhead Byte Stuffing. The encoded data contains no `0x00`; `0x00` is the frame delimiter.
- **CRC:** CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`, no reflection, no xorout) over `payload`, appended little-endian.
- **Max payload:** 1200 bytes. Bigger frames are dropped and counted as errors.
- **`seq`:** a per-sender counter (wraps at 255). Used only for diagnostics.
- **Byte order:** all integers are little-endian. There are no floats on the wire; fixed-point scales are noted per field.
- **Strings:** `len:u8 ‖ utf8 bytes`.
- **Errors:** a bad CRC or COBS error drops the frame and increments the receiver's error counter.

## Session

1. PC → `HELLO` (repeated every 300 ms for up to 2.5 s). Board → `HELLO_ACK`, then a `LOG` with its reset reason and uptime. If the proto version differs, the board answers `ERROR` and the PC treats the port as "no board" and keeps scanning.
2. PC → `INFO`, `CONFIG`, then `METRICS` / `CORES` / `DISKS` at the poll rate.
3. PC → `PING` every 1 s; the board answers `PONG` (the PC also answers a board `PING`, which the firmware does not send today). Either side treats **3 s** without a valid frame as disconnected. On the board, `SHOT_REQ` alone does not count.
4. Board → `STATS` every 5 s, and `TOUCH` / `PAGE` on events.

## Messages

`P→B` = PC to board, `B→P` = board to PC.

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x01 | HELLO | P→B | magic `"FNXD"` (4) ‖ proto_ver:u8 |
| 0x02 | HELLO_ACK | B→P | magic `"FNXD"` ‖ proto_ver:u8 ‖ fw_major:u8 ‖ fw_minor:u8 ‖ fw_patch:u8 ‖ width:u16 ‖ height:u16 (native panel 240×320) ‖ free_heap:u32 ‖ mac:6 |
| 0x03 | PING | both | t_ms:u32 |
| 0x04 | PONG | both | t_ms:u32 (echo) |
| 0x10 | CONFIG | P→B | rotation:u8 (0–3) ‖ brightness:u8 ‖ dim_brightness:u8 ‖ dim_timeout_s:u16 ‖ fps_cap:u8 ‖ page_mask:u8 ‖ flags:u8 |
| 0x11 | METRICS | P→B | see below |
| 0x12 | CORES | P→B | count:u8 ‖ util[count]:u8 (0–100 %) |
| 0x13 | DISKS | P→B | count:u8 ‖ count × (letter:u8 ‖ used_mb:u32 ‖ total_mb:u32) |
| 0x14 | INFO | P→B | cpu_name:str ‖ gpu_name:str ‖ host:str |
| 0x20 | IMAGE_BEGIN | P→B | slot:u8 (0–1) ‖ w:u16 ‖ h:u16 (each ≤ 64). Slot 0 is drawn as the logo on the waiting screen |
| 0x21 | IMAGE_CHUNK | P→B | slot:u8 ‖ offset:u32 ‖ data (RGB565 LE, ≤ 1024 B) |
| 0x22 | IMAGE_END | P→B | slot:u8. Board replies with a `LOG` "image slot N ready WxH" |
| 0x30 | TOUCH | B→P | kind:u8 (1 down, 2 up, 3 tap, 4 long) ‖ x:u16 ‖ y:u16 ‖ page:u8 |
| 0x31 | PAGE | both | page:u8 (B→P: changed; P→B: go to) |
| 0x40 | LOG | B→P | level:u8 (1 err, 2 warn, 3 info, 4 debug) ‖ text (rest of body) |
| 0x41 | ERROR | both | code:u8 ‖ text (rest of body) |
| 0x42 | STATS | B→P | free_heap:u32 ‖ min_heap:u32 ‖ fps_x10:u16 ‖ render_us:u32 ‖ rx_frames:u32 ‖ rx_errors:u32 |
| 0x50 | SHOT_REQ | P→B | (empty). Board reads its LCD back and streams it; does not count as "connected" traffic |
| 0x51 | SHOT_ROW | B→P | y:u16 ‖ w:u16 ‖ h:u16 ‖ RGB565 LE pixels (one or more full rows) |

### CONFIG

| Field | Meaning |
|---|---|
| `rotation` | LovyanGFX rotation: 0 = portrait, 1 = landscape, 2 = portrait flipped, 3 = landscape flipped |
| `brightness`, `dim_brightness` | 0–255 backlight PWM |
| `dim_timeout_s` | Seconds without data before dimming; 0 = never |
| `fps_cap` | 1–60 |
| `page_mask` | bit0 Overview, bit1 CPU, bit2 GPU, bit3 Memory/Disk, bit4 Network/Battery |
| `flags` | bit0 temperatures in °F, bit1 show free-heap overlay in the header |

### METRICS (0x11)

The body is a fixed **58-byte** struct. `present:u32` says which groups are valid. Fields of absent groups are zero and are drawn as `–`.

| Bit | Group | Fields (in order) |
|---|---|---|
| — | header | present:u32 |
| 0 | CPU load | cpu_util_x10:u16 (‰), cpu_clock_mhz:u16 |
| 1 | CPU sensors | cpu_temp_x10:i16 (°C), cpu_power_x10:u16 (W) |
| 2 | GPU | gpu_util_x10:u16, gpu_temp_x10:i16, gpu_power_x10:u16, gpu_clock_mhz:u16, vram_used_mb:u16, vram_total_mb:u16 |
| 3 | RAM | ram_used_mb:u32, ram_total_mb:u32 |
| 4 | Disk I/O | disk_read_kbs:u32, disk_write_kbs:u32 |
| 5 | Network | net_up_kbs:u32, net_down_kbs:u32 |
| 6 | Battery | bat_pct:u8, bat_state:u8 (0 unknown, 1 charging, 2 discharging, 3 full, 4 no battery), bat_time_min:u16, bat_power_x10:i16 (W; negative = discharging) |
| 7 | GPU idle | (no fields) dGPU is asleep; GPU group absent on purpose |
| — | always | uptime_s:u32 |

The fields always appear in this order, whether or not their bit is set.

Body size: 4 + 4 + 4 + 12 + 8 + 8 + 8 + 6 + 4 = 58 bytes.

## Versioning

`PROTO_VERSION = 1`. Any change to field layout bumps it.
