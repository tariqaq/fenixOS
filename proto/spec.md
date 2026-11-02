# fenixOS link protocol

Version: **proto 1** (fenixOS v0.1). Source of truth for both sides:
- Rust: `crates/fenix-proto` (also generates the golden vectors)
- C++: `board/components/fenix_proto`

Golden vectors: `proto/vectors.txt`. Both implementations decode every vector and encode it byte for byte. Regenerate only after an intended change: a `PROTO_VERSION` bump, or a new optional message (TOUCH, SYSINFO), which adds vectors and changes none: `FENIX_REGEN=1 cargo test -p fenix-proto --test vectors`.

The framing is kept from the FenixDeck PoC (`docs/legacy/protocol.md`), where it ran without errors.

## Transport

- USB serial through the CH340C at **921600 baud, 8N1, no flow control** (`BAUD` in both codecs; chosen in spike S3, `docs/board-info.md`).
- The PC opens the port with **DTR and RTS released**. Asserting them can reset the board through its auto-reset circuit.
- After a reset the ROM bootloader prints text at 115200. Receivers drop bytes until a valid frame arrives.
- Only `fenix-link` resets the board on purpose (reboot request, flashing).
- Bluetooth (SPP, P33) was removed in P38; its values below stay reserved.
- Wi-Fi (P34): the same frames over TCP port **7350** (`TCP_PORT`), `TCP_NODELAY`, no TLS (user choice). The board announces `fenixos.local` by mDNS. One client at a time.
- One session at a time: the board answers a frame from another transport while a session is open with `ERROR` code 4 (busy). The session ends on its heartbeat timeout or when its TCP connection closes; the board also closes a TCP connection that sends nothing for 3 s (P46), so a PC that vanished never holds the slot.

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
| 0x06 | ERROR | both | code:u8 ‖ text:bytes. Codes: 1 proto mismatch, 2 unknown type, 3 bad body, 4 busy, 5 no session (PING before HELLO: the board restarted, so the PC must send HELLO again), 6 reserved (was "unavailable": STREAM_START over Bluetooth, firmware 0.1.10 only) |
| 0x07 | STATS | B→P | free_heap:u32 ‖ min_heap:u32 ‖ fps_x10:u16 (animation fps over the last 5 s: only frames ≤ 100 ms after the previous one count; 0 when nothing animated) ‖ rx_frames:u32 ‖ rx_errors:u32 ‖ stream_dropped:u32 |

### System (0x1_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x10 | TIME_SET | P→B | epoch_s:u32 (UTC). The board shows MYT (UTC+8) |
| 0x11 | REBOOT | P→B | (empty). The board answers nothing and restarts within 100 ms |
| 0x12 | BOARD_STATE | B→P | screen:u8 (0 boot, 1 home, 2 settings, 3 session, 4 stream, 5 app) ‖ dimmed:u8 ‖ rotation:u8 ‖ app_id:str (empty unless screen 5) |
| 0x13 | TOUCH | P→B | x:u16 ‖ y:u16 (current screen coordinates) ‖ down:u8. Remote touch from the PC mirror and from tests (`fenix tap`, `fenix swipe`). The board treats it like the panel: a press lasts until `down = 0` or the link drops. Optional, so added in v0.1 without a proto bump |
| 0x14 | SYSINFO_REQ | P→B | empty. The PC's Task manager asks every second while it is open. Optional (P27, no proto bump): firmware before v0.1.5 answers ERROR unknown type |
| 0x15 | SYSINFO | B→P | time_us:u32 (FreeRTOS run-time clock) ‖ cpu_mhz:u16 ‖ heap_total:u32 ‖ heap_free:u32 ‖ heap_min:u32 ‖ heap_largest:u32 ‖ dma_free:u32 ‖ flash_size:u32 ‖ app_size:u32 (firmware image) ‖ app_slot:u32 ‖ apps_used:u32 ‖ apps_total:u32 (bytes) ‖ nvs_used:u16 ‖ nvs_total:u16 (entries) ‖ rx_bytes:u32 ‖ tx_bytes:u32 ‖ rx_frames:u32 ‖ rx_errors:u32 ‖ fps_x10:u16 ‖ backlight:u8 ‖ flags:u8 (1 dimmed, 2 panel resting, 4 burn-in protection, 8 streaming, 16 Wi-Fi running, 32 reserved (Bluetooth running, firmware 0.1.8 to 0.1.10); corrected P32: these meant "built in" and no firmware set them) ‖ n:u8 ‖ n × (id:u16 ‖ core:u8 (255 either) ‖ priority:u8 ‖ state:u8 (0 running, 1 ready, 2 blocked, 3 suspended, 4 deleted) ‖ runtime:u32 (µs on the time_us clock) ‖ stack_free:u16 (least free stack, bytes) ‖ name:str). Counters are cumulative and wrap at 2^32: the PC works out CPU load and rates from two answers |
| 0x16 | RADIO_STATE | B→P | mode:u8 (0 off, 2 Wi-Fi; 1 reserved: Bluetooth on firmware 0.1.8 to 0.1.10) ‖ state:u8 (0 off, 1 starting, 2 ready: Wi-Fi with no network set; 3 connecting, 4 connected, 5 failed) ‖ rssi:u8 (i8 dBm, Wi-Fi; 0 unknown) ‖ ip:4 (Wi-Fi IPv4, zero when none) ‖ name:str (Wi-Fi network). Sent after HELLO_ACK and on every change. Optional (P32, no proto bump) |
| 0x17 | RADIO_SET | P→B | mode:u8 (0 off, 2 Wi-Fi). The board saves the mode and restarts. Same mode or 1 (Bluetooth, removed in P38): nothing happens |
| 0x18 | WIFI_SCAN_REQ | P→B | empty. The board scans all channels (~2 to 3 s), then answers WIFI_SCAN |
| 0x19 | WIFI_SCAN | B→P | status:u8 (0 ok, 1 not in Wi-Fi mode, 2 failed) ‖ n:u8 (≤ 24) ‖ n × (ssid:str ‖ rssi:u8 (i8 dBm) ‖ channel:u8 ‖ auth:u8 (0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA/WPA2, 5 enterprise, 6 WPA3, 7 WPA2/WPA3, 8 other) ‖ bssid:6). Strongest first, one entry per SSID; hidden networks are left out |
| 0x1A | WIFI_JOIN | P→B | ssid:str (≤ 32 bytes) ‖ password:str (≤ 64 bytes, empty for open). The board saves both in plaintext (user choice, no encryption anywhere) and connects; progress comes as RADIO_STATE. An empty SSID forgets the network |
| 0x1B | WEATHER_SET | P→B | lat:i32 ‖ lon:i32 (degrees × 10000) ‖ name:str (≤ 47 bytes). The board saves the place and, in Wi-Fi mode, fetches the current weather from Open-Meteo over plain HTTP every 15 min (P34). An empty name turns weather off |
| 0x1C | SD_STATE | B→P | state:u8 (0 no card, 1 ready, 2 no FAT file system: exFAT, NTFS or blank; 3 failed) ‖ total_kb:u32 ‖ free_kb:u32 (0 unless ready) ‖ clock_khz:u16 (SPI clock the card runs at, 0 unless ready). Sent after HELLO_ACK and on every change; a card put in later is found when a PC connects or Settings › Storage opens (the slot has no card-detect pin). Optional (P50, no proto bump) |

### Apps (0x2_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x20 | APP_LIST_REQ | P→B | (empty) |
| 0x21 | APP_LIST | B→P | count:u8 ‖ count × (id:str ‖ version:str ‖ sha256:32). Since P51 (firmware 0.1.21) the first 10 apps only, for an older PC app; the PC uses APP_INDEX |
| 0x22 | APP_INSTALL_BEGIN | P→B | id:str ‖ version:str ‖ size:u32 ‖ sha256:32 |
| 0x23 | APP_CHUNK | P→B | offset:u32 ‖ data:bytes (≤ 1024) |
| 0x24 | APP_ACK | B→P | next_offset:u32. Sent with 0 when APP_INSTALL_BEGIN is accepted, then after each chunk is written. A chunk at any other offset is not written and is acked with the expected offset. The PC keeps at most 4 chunks in flight and goes back to the last acked offset when acks stop for 1 s. The board drops an install after 10 s without install messages, or when the link drops |
| 0x25 | APP_INSTALL_END | P→B | (empty). The board checks size and sha256, then commits |
| 0x26 | APP_RESULT | B→P | op:u8 (1 install, 2 remove) ‖ code:u8 ‖ id:str ‖ text:bytes. Codes: 0 ok, 1 no space, 2 bad hash, 3 bad package, 4 busy, 5 storage error, 6 unknown app, 7 too big for flash and no SD card (P51) |
| 0x27 | APP_REMOVE | P→B | id:str |
| 0x28 | APP_OPENED | B→P | id:str |
| 0x29 | APP_CLOSED | B→P | id:str |
| 0x2A | HOST_STATE | P→B | state:u8 ‖ id:str. States: 0 off (switched off on the PC, or its PC half stopped for good), 1 running, 2 not on this PC (kept on the board only), 3 starting (switched on, PC half not ready yet). Before P51 the byte was `running` (0 or 1); boards before 0.1.21 read 2 and 3 as running |
| 0x2B | APP_INDEX_REQ | P→B | page:u8. Optional (P51) |
| 0x2C | APP_INDEX | B→P | page:u8 ‖ pages:u8 ‖ count:u8 ‖ count × (id:str ‖ version:str ‖ sha256:32 ‖ where:u8 (0 flash, 1 SD card) ‖ state:u8 (0 ready, 1 card missing)). Up to 32 apps, sorted by id, 8 per page; `pages` is at least 1. The PC asks for page 0, then each next page. Optional (P51) |
| 0x2D | APP_TURN_ON | B→P | id:str. The Turn on button of an app switched off on the PC; the PC switches it on as if clicked there. Optional (P51) |

### Bindings (0x3_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x30 | BIND_SET | P→B | bind:u16 ‖ kind:u8 ‖ id:str ‖ value. Kinds: 0 int (`i32`), 1 bool (`u8`), 2 text (`str`). Applies to the open app only; ignored otherwise |
| 0x31 | EVENT | B→P | elem:u16 ‖ kind:u8 (1 tap, 2 long press, 3 value change) ‖ value:i32 ‖ id:str |
| 0x32 | BIND_IMAGE | P→B | bind:u16 ‖ w:u16 ‖ h:u16 ‖ total:u32 ‖ offset:u32 ‖ id:str ‖ data:bytes (≤ 1024). Pixels of the open app's bound `image` (P50-M, firmware 0.1.25; optional, an older board answers ERROR unknown type): RGB565 **little-endian** (the package's image order, not the stream's), `w` × `h` as the package declares, `total` = w × h × 2, chunks in order from offset 0. The board copies them into the element's buffer and redraws it when `offset + len(data) == total`; a chunk for another app, binding or size is ignored, and a gap drops the rest until the next offset 0. No ack: the PC sends at most 3 chunks per 50 ms |

### Stream (0x4_)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x40 | STREAM_START | both | fps:u8 (target, 1–30). P→B: start streaming. B→P (P14, after v0.1.0): the board reports that its stream is on, after a PC request or its own "Start streaming" button; the PC shows the stream as on |
| 0x41 | STREAM_STOP | both | (empty). P→B: stop streaming. B→P (P14): the board reports that its stream is off (PC request, its own "Stop streaming" button, or a disconnect) |
| 0x42 | STREAM_RECT | B→P | rect_id:u16 ‖ x:u16 ‖ y:u16 ‖ w:u16 ‖ h:u16 ‖ codec:u8 ‖ total:u32 ‖ offset:u32 ‖ data:bytes. One encoded rectangle may span several frames; the PC applies it when `offset + len(data) == total` |
| 0x43 | STREAM_SYNC | P→B | (empty). The board redraws the whole screen, so the next rects cover everything. Since P15 the board also answers with its stream state (STREAM_START or STREAM_STOP); the PC sends it at every new connection |
| 0x44 | STREAM_INFO | B→P | width:u16 ‖ height:u16 ‖ rotation:u8. Sent at stream start and on rotation change; the PC resizes its canvas |

Pixel data is RGB565 **big-endian** (the LCD's byte order). Codecs:
- `0` raw.
- `1` RLE16: a sequence of `count:u8 ‖ pixel:u16be`, where `count` is 1–255 repeats of the pixel.

### Firmware (0x5_)

Firmware updates over the link (P52, firmware 0.1.22 and later). Optional: an older board answers ERROR unknown type, and the PC flashes it over USB instead. The image is the app image of a signed `.fwp` package (`docs/ota.md`).

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x50 | FW_BEGIN | P→B | version:str ‖ size:u32 ‖ sha256:32 ‖ signature:64 (ECDSA P-256 over the sha256, r ‖ s). Answered with FW_ACK (the offset to send from) or FW_RESULT (refused). With an SD card the image is staged on the card, and a FW_BEGIN for the same sha256 resumes: the ACK gives the bytes already there. A FW_BEGIN while a transfer is still open replaces it (the PC reconnected) |
| 0x51 | FW_CHUNK | P→B | offset:u32 ‖ data:bytes (≤ 1024) |
| 0x52 | FW_ACK | B→P | next_offset:u32. Flow control as APP_ACK. The board drops an update after 10 s without firmware messages, or when the link drops (a staged image stays on the card) |
| 0x53 | FW_END | P→B | (empty). The board checks size, sha256 and signature, copies the image to the spare slot and answers; this can take 20 s |
| 0x54 | FW_RESULT | B→P | code:u8 ‖ text:bytes. Codes: 0 ok (the board restarts into the new firmware), 1 no space, 2 bad hash, 3 bad signature, 4 busy, 5 storage error, 6 older than the running firmware (a downgrade is only offered on the board) |

### Files (0x6_)

Files on the board's SD card under `/sd/fenixos` (P53, firmware 0.1.23 and later). Optional: an older board answers ERROR unknown type. A path is relative to `/sd/fenixos`, split by `/`, shorter than 128 bytes; no part may be empty, `.`, `..`, or end in a dot or a space, and `\ : * ? " < > |` are refused. `""` is the folder itself (list only).

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x60 | FILE_LIST_REQ | P→B | start:u16 ‖ path:str. Answered with FILE_LIST, or FILE_RESULT op list |
| 0x61 | FILE_LIST | B→P | start:u16 ‖ total:u16 ‖ path:str ‖ entries to the end of the body, each dir:u8 ‖ size:u32 ‖ name:str. One frame's worth from entry `start`, in the card's order; ask again from `start` + the count for the rest |
| 0x62 | FILE_READ_REQ | P→B | offset:u32 ‖ len:u16 (≤ 1024) ‖ path:str. Answered with FILE_DATA, or FILE_RESULT op read. Several may be in flight; they are answered in order |
| 0x63 | FILE_DATA | B→P | offset:u32 ‖ total:u32 (the file's size) ‖ data:bytes (shorter at the end; empty past it) |
| 0x64 | FILE_DELETE | P→B | path:str. Only inside `fonts/ data/ web/ logs/ crash/ shots/`; a folder only when empty. Answered with FILE_RESULT op delete |
| 0x65 | FILE_WRITE_BEGIN | P→B | size:u32 ‖ sha256:32 ‖ path:str. Only inside `fonts/ data/ web/`; folders on the way are made; a name ending in `.part` or `.pin` is refused. Answered with FILE_ACK (the offset to send from) or FILE_RESULT op write. The board writes `<path>.part` and keeps `<path>.pin` (size and sha256) beside it, so a FILE_WRITE_BEGIN for the same size and sha256 resumes: the ACK gives the bytes already there. A FILE_WRITE_BEGIN while a write is open replaces it |
| 0x66 | FILE_CHUNK | P→B | offset:u32 ‖ data:bytes (≤ 1024). Flow control as APP_ACK. The board closes a write after 10 s without file messages, or when the link drops (the part stays) |
| 0x67 | FILE_END | P→B | (empty). The board reads the part back, checks the sha256, then puts it in place of any old file. Answered with FILE_RESULT op write |
| 0x68 | FILE_ACK | B→P | next_offset:u32 |
| 0x69 | FILE_RESULT | B→P | op:u8 ‖ code:u8 ‖ path:str ‖ text:bytes. Ops: 1 list, 2 read, 3 delete, 4 write, 5 shot. Codes: 0 ok, 1 not found, 2 not allowed, 3 no card, 4 no space, 5 bad hash, 6 storage error, 7 busy. A list or read that works answers with FILE_LIST or FILE_DATA instead |
| 0x6A | SHOT_SAVE | P→B | (empty). Save the screen to `/sd/fenixos/shots/<time>.bmp` (P53). Answered with FILE_RESULT op shot; its path is the saved file. An older board answers ERROR unknown type |

## Versioning

`PROTO_VERSION = 1`. Any change to a body layout or a new required message bumps it.
