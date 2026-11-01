# Plan: apps on the SD card, app on/off, OTA, SD extras (P51 to P54)

Status: **planned**, not built (2026-11-02). Ahead of the code on purpose (AGENTS.md rule 7). The checklists are in `tasks.md`; the user's picks are in `docs/decisions.md` "2026-11-02: SD, apps, OTA". The SD base (P50, v0.1.20) is done: card at `/sd`, `SD_STATE`, Storage rows.

Rules for every phase:

- **RAM:** the budget is the Wi-Fi-joined, no-stream case (70.6 KB free at v0.1.20 with the card mounted). Each phase measures heap at idle and at its peak (`fenix sysinfo`, `fenix bench 15 nostream`) and writes the numbers to `docs/board-info.md`. Nothing new stays resident unless the table below allows it. Buffers live only during the operation that needs them; file work runs in short-lived tasks or the link task, never in the LVGL task; reads use `open`/`read` or `f_read` into one shared buffer, not stdio.
- **Protocol:** new messages are optional (no proto bump), vectors are appended at the end of the list only.
- **Flash:** the image must stay under the 1.5 MB OTA slot (1,572,864 B) with ~200 KB to spare. Each phase writes its image size.

| Resident RAM added | Budget |
|---|---|
| SD mount (P50, measured) | 9.6 KB |
| App index for ~30 apps (P51) | ≤ 2 KB (compact entries; tiles built only for the Home page shown) |
| App on/off state (P51) | ≤ 0.3 KB (bit per app) |
| OTA (P52) | 0 at idle; ≤ 12 KB while an update runs (4 KB copy buffer, SHA-256, ECDSA verify) |
| Logs to SD (P53) | ≤ 1 KB (512 B line buffer, flushed every 5 s and on errors) |
| Fonts from SD (P54) | 0 until a screen uses one; glyph cache ≤ 12 KB while it shows (measured) |
| Sounds (P54) | 0 at idle; ≤ 3 KB while a sound plays |

## P51: apps on the SD card, the new on/off model (v0.1.21)

### Behaviour

| Action on the PC | PC half | Board half |
|---|---|---|
| **Add app** (registry or local) | Downloaded and kept | Installed at once, on the SD card; on flash if there is no card and the package is ≤ 256 KB |
| **Switch off** | Sidecar stopped | Kept. Any open screen of it closes; `HOST_STATE` off |
| **Switch on** | Sidecar started | Installed if missing; `HOST_STATE` on once the sidecar is ready |
| **Remove** | Deleted | Removed (`APP_REMOVE`) |

- Every app goes to the SD card by default (user: "global default apps should install on sdcard" too). Flash is only the fallback.
- **On the board:** an app switched off on the PC keeps its tile. Opening it shows "Turned off on host PC" and a **Turn on** button. The button sends `APP_TURN_ON` and the PC switches the app on without a click on the PC (it logs "Switched on from the board"). With no PC linked, the screen says "Waiting for host PC" (as today).
- **Card missing:** tiles of apps stored on the card show greyed. Opening one says "Insert the SD card". A card put back is found by Settings › Storage, a PC connecting, or opening the tile (probe).
- **Reconcile** (PC connects): apps on the board that this PC does not know are removed (as today: one PC owns the board). Apps that are switched off are no longer removed. Every known app gets a `HOST_STATE`.
- **Package cap:** 8 MB on the card, 256 KB on flash. Was 64 KB (`fenix_pkg::MAX_SIZE`). Install time is the real limit: ~42 KB/s over USB (~3 min for 8 MB), faster over Wi-Fi. The PC shows progress for installs over 256 KB. Reading stays in pieces (P39), so a big package costs no more RAM than a small one; RAM per screen keeps its own cap (40 KB estimate).
- **~30 apps:** `MAX_APPS` 10 to 32. `APP_LIST` (one frame, 1200 B) cannot hold 32 entries, so a paged index replaces it (below). Home gets pages of tiles (swipe, page dots); only the page shown has LVGL objects. Mock-up first (DESIGN.md, `docs/mockups.md`).

### Storage layout on the card

```
/sd/fenixos/apps/<id>.fxp     package
/sd/fenixos/apps/<id>.sha     sha256 (32 B), written last (commit marker, as on flash)
/sd/fenixos/apps/<id>.tmp     install in progress (deleted at mount if left over)
```

The flash store (LittleFS) keeps its layout. One app lives in one place: an install to the card deletes a flash copy, and the other way round.

### Protocol (optional, no bump)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x2B | APP_INDEX_REQ | P→B | page:u8 |
| 0x2C | APP_INDEX | B→P | page:u8 ‖ pages:u8 ‖ n:u8 ‖ n × (id:str ‖ version:str ‖ sha256:32 ‖ where:u8 (0 flash, 1 SD) ‖ state:u8 (0 ready, 1 card missing)) |
| 0x2D | APP_TURN_ON | B→P | id:str. The board's Turn on button |

- `APP_RESULT` gains code 7 "too big without an SD card" (old PCs show the number).
- `APP_LIST` stays for the old PC app during an update (first 10 apps).

### Code

- Board: `store.cpp` gets two back ends (SD with stdio paths under `/sd/fenixos/apps`, LittleFS as now) behind the current `store_*` API, plus `where`/`state` in a compact `AppMeta` (id 64, version 16, sha 32, 2 flags: 114 B, was 160). `app_pkg.cpp`: greyed tile, Turn on button, card-missing screen, Home pages. `link.cpp`: `APP_INDEX`, `APP_TURN_ON`. Host test for the index codec.
- PC: `apps.rs` `added()` queues the board install whether on or off; `set_enabled(false)` drops the `Op::Remove`; `Cmd::Remove` sends it; `reconcile()` removes only unknown apps and sends `HOST_STATE` for all; handles `AppTurnOn`; reads `APP_INDEX` pages. UI: an app card shows "On the board (SD card)", "On the board (flash)" or "Card missing on the board"; progress bar for big installs.
- Docs: `docs/packages.md` (caps, where apps live, the on/off table), spec, sidecars (switch off no longer removes), README "How apps work", mockups, board-info numbers.

### Tests

Host tests (store index codec, package cap), cargo (apps reconcile cases: off app kept, unknown removed, turn-on request), sim screens (greyed tile, Turn on screen, Home page 2), on the board: install 3 apps with and without the card, pull the card with an app open, Turn on from the board, a 2 MB package install timed over USB and Wi-Fi, heap with 30 apps installed.

## P52: OTA with two slots, signed (v0.1.22)

### Flash layout (one USB flash to move to it)

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data nvs | 0x9000 | 24 KB (kept: settings survive) |
| otadata | data ota | 0xF000 | 8 KB |
| phy_init | data phy | 0x11000 | 4 KB |
| (free) | | 0x12000 | 56 KB: room for a core dump partition (P53) |
| ota_0 | app ota_0 | 0x20000 | 1.5 MB |
| ota_1 | app ota_1 | 0x1A0000 | 1.5 MB |
| apps | data littlefs | 0x320000 | 896 KB (flash app store, the fallback) |

- **Migration:** v0.1.22 goes on over USB, like every firmware so far: the PC app's update dialog flashes the bootloader, the partition table, `ota_data_initial` and the app (all listed in `firmware.json` by `bundle_firmware.py`). The flash app store moves and starts empty; the PC installs the flash-stored apps again (apps on the card are untouched). A board on Wi-Fi only with old firmware gets "Plug in USB once to update" (an old board cannot OTA).
- A USB flash after that always writes `ota_data_initial`, so the board boots `ota_0` (the rescue path stays "Reinstall firmware" over USB).
- The bootloader gets `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. A new image boots as pending; fenixOS marks it valid (`esp_ota_mark_app_valid_cancel_rollback`) once Home has shown and 30 s passed without a reset. A crash or reset before that boots the old slot, and Settings › About and the PC say "Update to vX failed; back on vY".

### Firmware package (`.fwp`)

`magic "FXFW" ‖ format:u8 ‖ version:str ‖ size:u32 ‖ sha256:32 ‖ signature:64 (ECDSA P-256 over the sha256, raw r‖s) ‖ image`. The release signs it (CI secret `FENIX_FW_SIGNING_KEY`; local key in `%USERPROFILE%\.fenixos\fw-signing.pem`, never in git) with `tools/sign_fw.py`. The public key is compiled into the firmware. The release publishes `fenixOS_<v>.fwp` and the PC app bundles it. Step 1 checks IDF's own signed-update option (`CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`) against this own check and records the choice; own check is the default because it needs no bootloader or eFuse change.

### Three ways in

1. **PC over USB or Wi-Fi:** the PC sends the `.fwp`. With a card: staged to `/sd/fenixos/firmware/<v>.fwp` (resumable: `FW_BEGIN` for the same sha answers with the bytes already there), then verified, then copied into the spare slot. Without a card: written straight into the spare slot and verified after.
2. **SD package:** copy `fenixOS_<v>.fwp` to the card's root on a PC. Settings › About finds it (also checked at boot) and offers "Update to vX".
3. **Rollback:** the last two `.fwp` stay in `/sd/fenixos/firmware/`. Settings › About › "Go back to vY" installs one (signature checked again; a downgrade only from this menu).

Either way the board shows a progress screen and restarts into the new slot.

### Protocol (optional, no bump)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x50 | FW_BEGIN | P→B | version:str ‖ size:u32 ‖ sha256:32 ‖ signature:64 |
| 0x51 | FW_CHUNK | P→B | offset:u32 ‖ data:bytes (≤ 1024) |
| 0x52 | FW_ACK | B→P | next_offset:u32 (same flow control as APP_ACK) |
| 0x53 | FW_END | P→B | (empty). Board verifies sha and signature |
| 0x54 | FW_RESULT | B→P | code:u8 (0 ok, restarting; 1 no space; 2 bad hash; 3 bad signature; 4 busy; 5 storage error; 6 older than this firmware) ‖ text:bytes |

### Code

Board: new `fenix_sys/ota.cpp` (stage, verify with mbedTLS, copy SD to slot with one 4 KB buffer, `esp_ota_*`), Settings › About rows, progress screen. `partitions.csv`, `sdkconfig.defaults`. PC: `update.rs` picks OTA when firmware is new enough (≥ 0.1.22), over the current link (USB or Wi-Fi), USB flashing otherwise. `fenix ota <file.fwp>`. `tools/sign_fw.py`, `bundle_firmware.py` (adds the `.fwp`), release workflow (signs, publishes). Docs: spec, board-info (partition table, timings), toolchain (key handling), README, decisions.

### Tests

Signature check host test (good, bad sig, bad hash, wrong key), an update over USB, over Wi-Fi, from an SD package, a pulled cable mid-transfer (resume), a forced crash in the new image (rollback), a downgrade refused outside the menu, heap at the peak.

## P53: SD extras (v0.1.23)

- **Logs and crash records:** board log lines (the `link_log` text, boot reason, heap) also go to `/sd/fenixos/logs/<boot>.txt` (512 B buffer, 10 newest kept, 256 KB each). A core dump partition in the 56 KB gap (if a dump of this firmware fits; measured) is copied to `/sd/fenixos/crash/` at the next boot. The PC's log sheet and `fenix logs --board` read them.
- **Files over the link:** `FILE_LIST_REQ`/`FILE_LIST`/`FILE_READ_REQ`/`FILE_DATA`/`FILE_DELETE` (0x60 to 0x64), read and delete only, under `/sd/fenixos`. Used by logs, crash records and screenshots.
- **Screenshots to SD:** a long press on the clock (or Settings › About) saves `/sd/fenixos/shots/<time>.bmp` (RGB565 BMP, read from the LCD in 16-row bands; `cfg.readable` is already on). The PC lists and downloads them.
- **Last values while the PC is off:** the board saves an app's last bound values to `/sd/fenixos/state/<id>.bin` when it closes (and every 60 s while open). With no PC linked, the app opens on them (dimmed, "PC not connected") instead of the empty "Waiting for host PC".

## P54: fonts from the card, UI sounds (v0.1.24)

- **Fonts:** LVGL Tiny TTF (`LV_USE_TINY_TTF`, `LV_TINY_TTF_FILE_SUPPORT`, an LVGL file system driver on `/sd`). TTF files in `/sd/fenixos/fonts/` (a CJK subset font, ~2 to 8 MB). Apps ask for `font: "cjk"` on a label; the board falls back to the built-in font when the card is missing. Measure flash (stb_truetype), glyph cache RAM and draw speed first; `tools/sim_config.py` after the LVGL change. Covers the Morpheus "wider text" request beyond Latin-1.
- **Sounds:** short UI sounds (tap, toggle, notification) through the speaker header (IO26, DAC, SC8002B amp). **The board has no speaker:** one must be plugged into the header (a small 8 Ω, 0.5 to 1 W). Sounds as WAV on the card, played by a short-lived task with the DAC; Settings › Sounds (off by default). Not built until a speaker is on the board.

## Order and releases

P51 (v0.1.21) then P52 (v0.1.22, the USB migration) then P53 then P54, then the Morpheus phases P50-M and P51-M. Each phase ends with docs, all tests, a release, a flash and a tag, and waits for the user's go to start the next one.
