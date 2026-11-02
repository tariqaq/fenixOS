# Plan: apps on the SD card, sync, OTA, SD extras, fonts, then Morpheus (P51 to P54, P50-M, P51-M)

Status: P51 done (v0.1.21); P52 built and tested on the board (`docs/ota.md`, v0.1.22); P53 on: **planned**, not built (2026-11-03). Ahead of the code on purpose (AGENTS.md rule 7). The checklists are in `tasks.md`; the user's picks are in `docs/decisions.md` "2026-11-02: SD, apps, OTA" and "2026-11-02 (evening): plan changes before the overnight run". The SD base (P50, v0.1.20) is done: card at `/sd`, `SD_STATE`, Storage rows.

**Overnight run (user: "/goal the entire plan"):** the phases run one after another without waiting for a go: P51, P52, P53, P54, P50-M, P51-M. Each ends with docs, all tests, a release, a flash over USB and a tag. A choice the plan does not settle is made by the agent and logged in `docs/decisions.md` (section "2026-11-03: overnight run") with the options not taken. A step that cannot be done (needs the user's hands or eyes, or keeps failing) is marked blocked in `tasks.md` with notes, and the run goes on with the next step that does not depend on it.

Rules for every phase:

- **RAM:** the budget is the Wi-Fi-joined, no-stream case (70.6 KB free at v0.1.20 with the card mounted). Each phase measures heap at idle and at its peak (`fenix sysinfo`, `fenix bench 15 nostream`) and writes the numbers to `docs/board-info.md`. Nothing new stays resident unless the table below allows it. Buffers live only during the operation that needs them; file work runs in short-lived tasks or the link task, never in the LVGL task; reads use `open`/`read` or `f_read` into one shared buffer, not stdio.
- **Protocol:** new messages are optional (no proto bump); vectors are appended at the end of the list only.
- **Flash:** the image must fit a 1,900,544 B OTA slot with ≥ 300 KB to spare (v0.1.20: 1,155,568 B). Each phase writes its image size.

| Resident RAM added | Budget |
|---|---|
| SD mount (P50, measured) | 9.6 KB |
| App index for 32 apps (P51) | ≤ 2 KB (compact entries; tiles built only for the Home page shown). Corrected (P51): 3.8 KB (120 B an app, `docs/decisions.md`), offset by Home keeping icons for the shown page only |
| App on/off and where-stored flags (P51) | ≤ 0.3 KB |
| OTA (P52) | 0 at idle; ≤ 12 KB while an update runs (4 KB copy buffer, SHA-256, ECDSA verify) |
| Logs to SD (P53) | ≤ 1 KB (512 B line buffer, flushed every 5 s and on errors) |
| Wallpaper (P53) | 0: drawn from the card in rows, never held whole (a full screen is 150 KB) |
| Fonts from SD (P54) | 0 until a screen uses one; glyph cache ≤ 12 KB while it shows (measured) |

## P51: apps on the SD card, on/off, sync (v0.1.21)

### Behaviour

| Action on the PC | PC half | Board half |
|---|---|---|
| **Add app** (registry or local) | Downloaded and kept | Installed at once, on the SD card; on flash if there is no card and the package is ≤ 128 KB |
| **Switch off** | Sidecar stopped | Kept, tile faded. An open screen of it shows "Turned off on host PC"; `HOST_STATE` off |
| **Switch on** | Sidecar started | Installed if missing; `HOST_STATE` on once the sidecar is ready |
| **Remove** | Deleted | Removed (`APP_REMOVE`) |

- Every app goes to the SD card by default, the global default apps too. Flash is only the fallback.
- **Board tiles:** an app switched off on the PC has a **faded** tile. Tapping it opens the app screen anyway, with "Turned off on host PC" and a **Turn on** button. The button sends `APP_TURN_ON`; the PC switches the app on without a click on the PC (it logs "Switched on from the board"), and the screen comes up once the sidecar is ready. With no PC linked the screen says "Waiting for host PC" (as today).
- **Card missing:** tiles of apps stored on the card are faded with a small SD card badge. Tapping one says "Insert the SD card" and probes the card. A card put back is also found by Settings › Storage and a PC connecting.
- **Sync** (replaces reconcile's silent removal): when the PC connects it compares its apps with the board's index. Differences go to a **"Sync with board"** card on the PC's Apps page, one checkbox each, and one **Sync** button does them:
  - On the board, not on this PC: install the PC half (fetched from the registry by id and version). If the registry does not have it (a local dev app from another PC), the choices are "Keep on the board only" (tile faded, "Not on this PC") or "Remove from the board".
  - On this PC, not on the board: install the board half.
  - Same id, different version: update the side that is older.
  - Nothing changes without the click, except **Add** (installs both halves at once) and **Remove** (removes both).
- **Package cap on the card: none fixed.** Limited by free card space and FAT32's 4 GB per file (`size` is a u32 on the wire). Install time is the real limit: ~42 KB/s over USB, ~80 KB/s over Wi-Fi. The PC shows progress for every board install over 128 KB and says how long one will take when it is over ~2 min. Reading stays in pieces (P39), so a big package costs no more RAM than a small one; the RAM estimate per screen keeps its own cap. **On flash: 128 KB per package** (the user has a reason, to come), 256 KB in all.
- **32 apps:** `MAX_APPS` 10 to 32. `APP_LIST` (one frame, max payload 1200 B) cannot hold 32 entries, so a paged index replaces it. Home gets pages of tiles: swipe left and right, page dots, same tile size as today; only the page shown has LVGL objects. Designed by the agent under DESIGN.md (no mock-up review overnight); sim screenshots in `docs/screens/board/` for the user to check.

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

- `APP_RESULT` gains code 7 "too big for flash and no SD card".
- `APP_LIST` stays (first 10 apps) for an old PC app during an update.

### Code

- Board: `store.cpp` gets two back ends (SD under `/sd/fenixos/apps`, LittleFS as now) behind the current `store_*` API; compact `AppMeta` (id 48 = `fpkg::MAX_ID`, version 16, sha 32, where, state: ~100 B, was 160). `fenix_pkg` `MAX_SIZE` split: none for SD, 128 KB for flash. `app_pkg.cpp`: faded tile, SD badge, Turn on button, card-missing screen, "Not on this PC" state, Home pages. `link.cpp`: `APP_INDEX`, `APP_TURN_ON`. Host tests.
- PC: `apps.rs`: `added()` queues the board install whether on or off; `set_enabled(false)` no longer queues `Op::Remove`; `Cmd::Remove` does; `reconcile()` becomes a diff (`SyncItem` list) shown in the UI and applied on `Cmd::Sync`; `AppTurnOn` handled; index pages read; registry lookup by id and version. UI: Sync card, where each app lives ("SD card", "flash", "card missing"), install progress. Cargo tests for every case.
- Docs: `docs/packages.md` (caps, where apps live, on/off table, sync), spec, `docs/sidecars.md` (switch off no longer removes), README "How apps work", mockups, board-info numbers.

### Tests

Host tests (store index codec, flash cap), cargo (sync diff: off app kept, board-only app, PC-only app, version skew, unknown not in registry; turn-on request), sim screens (faded tile, SD badge, Turn on screen, card missing, Home page 2), on the board: installs with and without the card, card pulled with an app open, Turn on from the board, a 2 MB install timed over USB and Wi-Fi, heap with 32 apps.

## P52: OTA with two slots, signed (v0.1.22)

### Flash layout (one USB flash to move to it)

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data nvs | 0x9000 | 24 KB (kept: settings survive) |
| otadata | data ota | 0xF000 | 8 KB |
| phy_init | data phy | 0x11000 | 4 KB |
| coredump | data coredump | 0x12000 | 56 KB (P53; if a dump does not fit, the gap stays free) |
| ota_0 | app ota_0 | 0x20000 | 0x1D0000 (1,900,544 B, 1.81 MB) |
| ota_1 | app ota_1 | 0x1F0000 | 0x1D0000 |
| apps | data littlefs | 0x3C0000 | 256 KB (flash app store, the fallback; kept even if the card stays in for good, user) |

App slots must start on 64 KB boundaries; this layout fills the 4 MB exactly.

- **Migration:** v0.1.22 goes on over USB (the board is plugged in): the PC app's update dialog and `fenix flash` write the bootloader, the partition table, `ota_data_initial` and the app (all in `firmware.json` from `bundle_firmware.py`). NVS stays. The flash app store moves and starts empty; the Sync card then offers the flash-stored apps back (apps on the card are untouched). A board reached only over Wi-Fi with old firmware gets "Plug in USB once to update".
- A USB flash after that always writes `ota_data_initial`, so the board boots `ota_0`; "Reinstall firmware" over USB stays the rescue path.
- Bootloader: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. A new image boots as pending; fenixOS marks it valid (`esp_ota_mark_app_valid_cancel_rollback`) once Home has shown and 30 s passed. A crash or reset before that boots the old slot, and Settings › About and the PC say "Update to vX failed; back on vY".

### Firmware package (`.fwp`)

`magic "FXFW" ‖ format:u8 ‖ version:str ‖ size:u32 ‖ sha256:32 ‖ signature:64 (ECDSA P-256 over the sha256, raw r‖s) ‖ image`.

- Key: generated by the agent into `%USERPROFILE%\.fenixos\fw-signing.pem`, set as the `FENIX_FW_SIGNING_KEY` Actions secret with `gh secret set` (user, 2026-11-02). Never in git. The agent backs it up with the updater key to `%USERPROFILE%\Documents\fenixOS-keys\` (user, 2026-11-02) and tells the user; that copy is on the same disk, so the user may copy the folder somewhere else too. Losing the key means a new key and one USB update.
- `tools/sign_fw.py` signs; the release workflow and local releases run it; the release publishes `fenixOS_<v>.fwp`; the PC app bundles it. The public key is compiled into the firmware.
- Step 1 compares IDF's signed-update option (`CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`, its scheme on ESP32 rev 3.1) with this own check and records the choice; the own check is the default because it needs no bootloader or eFuse change.

### Three ways in

1. **PC over USB or Wi-Fi:** the PC sends the `.fwp`. With a card it is staged to `/sd/fenixos/firmware/<v>.fwp` (resumable: `FW_BEGIN` for the same sha answers with the bytes already there), verified, then copied into the spare slot. Without a card it is written straight into the spare slot and verified after.
2. **SD package:** copy `fenixOS_<v>.fwp` to the card's root on a PC. Settings › About finds it (also checked at boot) and offers "Update to vX".
3. **Rollback:** the last two `.fwp` stay in `/sd/fenixos/firmware/` (also the rescue copy). Settings › About › "Go back to vY" installs one (signature checked again; a downgrade only from this menu).

Each way shows a progress screen on the board and restarts into the new slot.

### Protocol (optional, no bump)

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x50 | FW_BEGIN | P→B | version:str ‖ size:u32 ‖ sha256:32 ‖ signature:64 |
| 0x51 | FW_CHUNK | P→B | offset:u32 ‖ data:bytes (≤ 1024) |
| 0x52 | FW_ACK | B→P | next_offset:u32 (same flow control as APP_ACK) |
| 0x53 | FW_END | P→B | (empty). The board checks sha and signature |
| 0x54 | FW_RESULT | B→P | code:u8 (0 ok, restarting; 1 no space; 2 bad hash; 3 bad signature; 4 busy; 5 storage error; 6 older than this firmware) ‖ text:bytes |

### Code

Board: `fenix_sys/ota.cpp` (stage, verify with mbedTLS, copy SD to slot with one 4 KB buffer, `esp_ota_*`, mark valid), Settings › About rows, progress screen; `partitions.csv`, `sdkconfig.defaults`. PC: `update.rs` uses OTA over the current link for firmware ≥ 0.1.22, USB flashing otherwise. `fenix ota <file.fwp>`. `tools/sign_fw.py`, `bundle_firmware.py` (adds the `.fwp`), release workflow. Docs: spec, board-info (partition table, timings), toolchain (key handling), README, decisions.

### Tests

Signature host test (good, bad signature, bad hash, wrong key), updates over USB, Wi-Fi and from an SD package, a pulled cable mid-transfer (resume), a forced crash in the new image (rollback), a downgrade refused outside the menu, heap at the peak.

## P53: SD extras (v0.1.23)

- **Files over the link:** `FILE_LIST_REQ`, `FILE_LIST`, `FILE_READ_REQ`, `FILE_DATA`, `FILE_DELETE`, `FILE_WRITE_BEGIN`/`FILE_CHUNK`/`FILE_END` (0x60 to 0x67), only under `/sd/fenixos`; writes only to `fonts/`, `data/`, `wallpaper/`. Chunked and resumable like app installs. `fenix files ls|get|put|rm`. Corrected (built): 0x60 to 0x6A, with `FILE_ACK`, `FILE_RESULT` and `SHOT_SAVE` (`proto/spec.md` "Files (0x6_)").
- **Logs and crash records:** board log lines (the `link_log` text, boot reason, heap) also go to `/sd/fenixos/logs/<boot>.txt` (512 B buffer, 10 newest kept, 256 KB each). The core dump partition (56 KB gap) is copied to `/sd/fenixos/crash/` at the next boot, if a dump of this firmware fits it (measured). The PC's log sheet and `fenix logs --board` read them.
- **Screenshots, from the PC only** (user): a button on the PC's Board page and `fenix shot --sd` make the board save `/sd/fenixos/shots/<time>.bmp` (RGB565, read from the LCD in 16-row bands; `cfg.readable` is on). The PC lists and downloads them.
- **Last values while the PC is off:** the board saves an app's last bound values to `/sd/fenixos/state/<id>.bin` when it closes (and every 60 s while open). With no PC linked, the app opens on them, dimmed, with "PC not connected", instead of the empty "Waiting for host PC".
- **Settings backup:** every settings change also writes `/sd/fenixos/backup/settings.bin` (all NVS settings, **Wi-Fi password included, plaintext**, user). A board with empty settings and a card holding a backup offers "Restore settings from the SD card" at boot.
- **Status page from the card:** `web.cpp` serves `/sd/fenixos/web/` when present (a richer page without flash cost; the built-in page stays the fallback).
- **Home wallpaper:** an image in `/sd/fenixos/wallpaper/` (LVGL binary RGB565, 320×240), set from the PC; drawn from the card in rows (LVGL file image decoder), never held whole. Measured first: draw time of a full redraw and the burn-in shift with it. Dropped if it slows Home.
- **Per-app data folder:** `/sd/fenixos/data/<id>/`, written and read by the app's PC half through the SDK (`app.put_file(name, bytes)`, `app.get_file(name)`), for history and offline data. Removed with the app.

## P54: fonts from the card (v0.1.24)

- **Scripts:** Chinese, Japanese, Korean (user). One CJK font on the card: Noto Sans CJK (OFL), its language OTF (~16 MB). Step 1 checks LVGL 9.5 Tiny TTF and its `stb_truetype` with a CFF-outline OTF; if CFF does not work, a TrueType-outline CJK font (OFL) instead. If one big font is too slow, it is split per script.
- **Engine:** LVGL Tiny TTF (`LV_USE_TINY_TTF`, `LV_TINY_TTF_FILE_SUPPORT`) with an LVGL file system driver on `/sd`; `tools/sim_config.py` after the LVGL change. Measured first: flash added, glyph cache RAM, time to draw a 20-character title.
- **Delivery:** the PC app downloads the font (pinned URL and sha256) and installs it with `FILE_WRITE` to `/sd/fenixos/fonts/`; Settings › Storage shows it.
- **Use:** labels fall back to the CJK font for characters the built-in fonts lack (no change for app makers); with no card or no font they show as now. Covers the Morpheus "wider text" request beyond Latin-1.
- **UI sounds: dropped** (user, 2026-11-02: no speaker planned).

## P50-M and P51-M: Morpheus (v0.1.25, v0.1.26)

Only the fenixOS features in `docs/requests/morpheus.md`; the Morpheus app itself lives in its own repo and is not built here (user, 2026-11-02). As in `tasks.md`, with the user's answers (2026-11-02): **both** a `view` element and a bindable `show` (bool) on every element; a **`secret`** setting type (masked in the PC app, DPAPI user scope, never logged; the sidecar gets the plain value); **two releases**. The answer in `docs/requests/morpheus.md` is updated when each ships.

## Order and releases

P51 (v0.1.21), P52 (v0.1.22, the USB migration), P53 (v0.1.23), P54 (v0.1.24), P50-M (v0.1.25), P51-M (v0.1.26), in one run.
