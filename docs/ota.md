# Firmware updates over the link and from the SD card (P52, v0.1.22)

The board has two app slots. A new firmware goes into the spare slot while the old one keeps running. The board restarts into it only after the board has checked the package's hash and signature. A new image runs on trial: if it crashes or resets before it has shown Home for 30 s, the bootloader goes back to the old slot.

Design and the user's picks: `docs/plan-sd-ota.md` (P52) and `docs/decisions.md` ("2026-11-02: SD, apps, OTA", "2026-11-02: overnight run"). Messages: `proto/spec.md` "Firmware (0x5_)".

## Three ways in

| Way | How | Needs |
|---|---|---|
| PC app | Update dialog or Board › Reinstall firmware. A board on 0.1.22 or later gets the bundled `.fwp` over the current link, USB or Wi-Fi | The bundle has a `.fwp`, and the bundle is not older than the board |
| CLI | `fenix ota <file.fwp> [--port COMx\|tcp:<host>]` | Any signed package |
| SD card | Copy `fenixOS_<v>.fwp` (a release file) to the card's root. Settings shows "Update" on the About row; About › Update to vX installs it | A newer version than the running one |

USB flashing (`fenix flash`, the app's first install and its fallback) still works and always writes the bootloader, the partition table and `ota_data_initial`, so the board then boots `ota_0`. An old board (before 0.1.22) reached only over Wi-Fi gets "plug the board in by USB once to update it". A bundle older than the board's firmware goes over USB, because the board refuses an older version over the link.

## Flash layout

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data nvs | 0x9000 | 24 KB |
| otadata | data ota | 0xF000 | 8 KB |
| phy_init | data phy | 0x11000 | 4 KB |
| coredump | data coredump | 0x12000 | 56 KB (P53) |
| ota_0 | app | 0x20000 | 0x1D0000 (1,900,544 B) |
| ota_1 | app | 0x1F0000 | 0x1D0000 |
| apps | data littlefs | 0x3C0000 | 256 KB (flash app store) |

Corrected (P52): until v0.1.21 there was one 2 MB `factory` slot and a 1984 KB flash app store. The first boot on the new layout erases the `apps` partition once (NVS `ota`/`layout` = 2), because the old store's data no longer lines up with it. Apps on the SD card are not touched, and the PC app's Sync card offers the flash apps back. The v0.1.22 image is 1,177,600 B, 38% of a slot free; v0.1.27 is 1,290,192 B, 32% free.

## The package (`.fwp`)

```
"FXFW" || format:u8 (1) || version:str || size:u32 || sha256:32 || signature:64 || image
```

Integers are little-endian; a str is len:u8 then bytes. The signature is ECDSA P-256 over sha256(image), raw r‖s. The board takes the version from the header, then checks that it matches the version inside the image (`esp_app_desc_t`). So a header cannot relabel an older image as a newer one.

`tools/sign_fw.py`:

- `keygen`: makes the key once.
- `sign <fenixos.bin> <out.fwp>`: refuses a key that does not match `fw_pubkey.h`.
- `verify <file.fwp>`.
- `pubkey`: rewrites `board/components/fenix_sys/fw_pubkey.h`.
- `--self-test`: runs in CI. It checks a good package, a bad signature, a bad hash, a wrong key and a relabelled version.

`tools/bundle_firmware.py` writes `fenixOS_<v>.fwp` next to the bins and names it in `firmware.json` (`"fwp"`). Without the key it warns, and that bundle updates over USB only. A tag build passes `--require-key`.

### The signing key

- **Key file:** `%USERPROFILE%\.fenixos\fw-signing.pem`. It is also the `FENIX_FW_SIGNING_KEY` Actions secret. `*.pem` is git-ignored, and the key is never in git.
- **Backup:** `%USERPROFILE%\Documents\fenixOS-keys\` holds the key, the updater key and a README. That backup is on the same disk, so also keep a copy offline.
- **Who reads it:** `sign_fw.py` reads the `FENIX_FW_SIGNING_KEY` variable first (the PEM text, as in the release workflow), then the file.
- **Losing the key:** make a new key, rebuild with the new `fw_pubkey.h`, and update every board over USB once.

## On the board (`board/components/fenix_sys/ota.cpp`)

The link task receives; an `ota` task (6 KB stack) checks and installs. The UI reads `ota_status()` and `ota_offer()`. `LinkEv::OTA` opens the progress screen (`update_screen`) when an update from the PC starts, and keeps it current until it fails or the board restarts.

- **Staging (card in).** `FW_BEGIN` opens `/sd/fenixos/firmware/<v>.part` with the package header. If a `.part` with the same size and sha256 is there, the board answers `FW_ACK` with the offset it has, so a cut transfer goes on from there. `FW_CHUNK`s are appended. After `FW_END` the ota task checks the file, copies it into the spare slot with a 4 KB buffer, keeps it as `<v>.fwp`, and restarts.
- **Direct (no card).** The chunks go straight into the spare slot (`esp_ota_write`), and the sha256 is computed on the way. A cut transfer starts over.
- **Checks, in order:**
  1. Not older than the running version (`FW_BEGIN`; an equal version is allowed, for Reinstall).
  2. Fits the slot.
  3. Room on the card.
  4. Size and sha256.
  5. Version inside the image.
  6. ECDSA signature (mbedTLS, `fw_pubkey.h`).
  7. `esp_ota_end`'s own image check.

  A package refused for its hash or signature is deleted from the card.
- **Busy rules.** A new `FW_BEGIN` replaces an unfinished receive, because the PC reconnected and goes on from the staged part. `BUSY` comes only while a check or install runs, or an app install is open. A receive with no message for 10 s is dropped, and its `.part` stays for a resume.
- **Kept packages.** After an install the board keeps three: the new one, the running one, and the newest older one. Any other `.fwp` and leftover `.part` are deleted. About › Go back to vY installs the newest kept package older than the running one. Going back is the only way to install an older version.
- **Trial and rollback.** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. `ota_home_shown()` starts a 30 s timer, then `esp_ota_mark_app_valid_cancel_rollback`. If the board restarts before that, the bootloader boots the old slot. At boot, `esp_ota_get_last_invalid_partition` names the failed version when it is newer than the running one. About shows "Last update vX failed", and the link logs "Update to vX failed; back on vY" once per boot. An update started during a trial first keeps the running image, because `esp_ota_begin` refuses while the running image is pending (`ESP_ERR_OTA_ROLLBACK_INVALID_STATE`).
- **SD offers.** `ota_scan_sd()` runs after a mount and after an install. It finds the newest `fenixOS_*.fwp` on the card's root that is newer than the running version, and the newest kept older one. Only headers are read. Installing a root package first moves it into `/sd/fenixos/firmware/`.

## On the PC

- **Shared sender.** `crates/fenix-link/src/ota.rs` holds `parse_fwp` and `OtaSend`, on the chunk sender in `chunks.rs` that file writes share (P53). The CLI and the app both use it.
  - Up to 4 chunks (4 KB) are in flight.
  - When no ack moves the offset for a second, it sends again from the last acked offset.
  - It gives up after 12 s without progress, or 120 s after `FW_END` with no `FW_RESULT`.
- **App.** `app/src-tauri/src/engine.rs` `start_update` picks over the link or USB. The engine thread polls the sender every 5 ms while it runs.
  - Stages: `writing` (sending), `installing` (the board checks and copies), `restarting`, then `done` once the board reconnects with the bundled version.
  - A link drop fails the update. Try again goes on from what the board staged.

## Measured (2026-11-02, COM9, 8 GB card, test builds labelled 0.1.22 to 0.1.28, 1.15 MB image)

| Case | Sent | Install | Notes |
|---|---|---|---|
| USB, staged on the card | 15.2 s | 9.0 s | about 77 KB/s over the link |
| Wi-Fi, staged | 7.7 s | 9.0 s | about 150 KB/s |
| Resume after a killed transfer | 7.2 s (from 621,568 B) | 9.0 s | |
| No card, direct to the slot | 15.6 s | 0.7 s | a test build that ignored the card |
| About › Go back (from the card) | | ~6 s | tap to restart |

- **Heap.**
  - Idle: 68 to 70 KB free.
  - While the staged copy runs: 59 to 60 KB, so the update uses 8 to 11 KB, inside the 12 KB budget.
  - Direct receive: about 70 KB.
  - Wi-Fi receive at full speed: low point 50.4 KB (lwIP buffers).
- **Refusals.**
  - A flipped signature byte gives `BAD_SIGNATURE`.
  - A flipped image byte gives `BAD_HASH`.
  - An older version over the link gives `OLDER`.
  - In each case the board stayed on its version.
- **Rollback.** An image that aborted at boot rolled back to the previous slot. The board logged "Update to v0.1.24 failed; back on v0.1.23", and About showed "Last update v0.1.24 failed".

Not tried on the board yet:

- "Update to vX" from a package on the card's root. The PC's file writes (P53) reach only `fonts/ data/ web/`, so this needs a `.fwp` copied to the card on a PC.
- A power cut during the copy. The old slot stays the boot slot until `esp_ota_set_boot_partition`, so a cut copy only leaves a half-written spare slot.
