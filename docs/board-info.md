# Board info (read from hardware)

Chip info and the stock backup were read on 2026-10-28 with `esptool v5.6.0`, before anything was written. The board has since been flashed with the FenixDeck firmware (FenixDeck repo `board/`; that code is not in fenixOS). Both the stock image and the FenixDeck image can be restored (below). Chip info was re-read on 2026-10-29 and matched.

## Connection

| Item | Value |
|---|---|
| USB-UART | CH340, `USB\VID_1A86&PID_7523` |
| Driver | vendor `CH341SER.EXE` (`docs/vendor/CH340/`) |
| Port | `COM9` (can change if plugged into another USB port) |
| Reliable baud for reads | 460800 (921600 gave `Corrupt data`). CH340C datasheet max is 2 Mbps; a distributor spec lists 460,800 bps continuous with flow control |

## Chip

| Item | Value |
|---|---|
| Chip | **ESP32-D0WD-V3, revision v3.1** |
| Features | Wi-Fi, BT, dual core, 240 MHz |
| Crystal | 40 MHz |
| MAC | `b4:a6:4a:a0:be:54` |
| Flash | 4 MB, JEDEC manufacturer `0xc4`, device `0x6016` |
| Flash voltage | 3.3 V (strapping pin) |

The store listing says ESP32-D0WDQ6. The real chip is the newer **D0WD-V3** (rev 3.1), which is still dual core.

## Stock firmware

Backup: `firmware/stock/stock_flash_4MB.bin` (full 4 MB dump, offset 0x0).

- SHA-256: `db0256aad5a235cc19584cb6050421da27d08730e27da49347e0c52bdef75c22`
- Checked against the board with `esptool verify-flash`: digest matched.

| Partition | Type | Offset | Size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 24K |
| phy_init | data/phy | 0xF000 | 4K |
| factory | app | 0x10000 | 1024K |

- App: `lvgl-demo` v1, built `Nov 1 2025 11:46:16` with ESP-IDF v4.3. This is the vendor LVGL demo.
- Data ends at about 0xFBE00. The rest of the flash is empty (0xFF).

## Restore stock firmware

Do not run this unless you intend to restore. It overwrites the whole flash.

```
python -m esptool --port COM9 -b 460800 write-flash 0 firmware/stock/stock_flash_4MB.bin
```

## FenixDeck firmware (on the board now)

Backup: `firmware/fenixdeck/fenixdeck_v0.1.0_flash_4MB.bin` (full 4 MB dump, offset 0x0, read 2026-10-29).

- SHA-256: `3434e84f6cd7ffec7306868f682d95a63194859a7192e4a79da02effd9b33945`
- Checked against the board with `esptool verify-flash`: digest matched.
- App: project `fenixdeck_board`, version `74a62bb-dirty` (git describe of the FenixDeck repo), built `Oct 28 2026 23:33:04` with ESP-IDF v5.5.5. Protocol `HELLO_ACK` reports fw 0.1.0, proto 1.
- App image: 337,008 B (329 KiB), 6 segments, checksum and SHA-256 valid (`esptool image-info`).

| Partition | Type | Offset | Size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 24K |
| phy_init | data/phy | 0xF000 | 4K |
| factory | app | 0x10000 | 2048K |
| storage | data/littlefs | 0x210000 | 1984K |

- **Leftover stock bytes:** after the app image, 172 of the 174 4 KB blocks up to the old stock app end (~0xFBE00) are identical to the stock image. `idf.py flash` erases only the sectors it writes. This is harmless (the bootloader reads only the image length), but a raw dump is not "app + 0xFF". Run `esptool erase-flash` first when a clean image matters.
- **NVS** holds the `fenix` namespace but no `tcal` key: touch was never calibrated on this board, so the default raw range (x 300–3900, y 200–3900) is what runs.
- **Live check (2026-10-29),** using a throwaway protocol script (not in the repo): `HELLO_ACK` proto 1, fw 0.1.0, 240×320, MAC matches, free heap 163,908 B (min 156,496 B). Then 5 stylus taps in landscape (x 68–162, y 171–190), each giving down/up/tap events, with pages cycling 0→1→2→3→4→0. Render 26–29 ms per page change, 0 rx errors over 93 frames. Uptime grew across two port opens (39 s, then 60 s). Corner coverage of the default range was **not** tested.

Restore it (overwrites the whole flash):

```
python -m esptool --port COM9 -b 460800 write-flash 0 firmware/fenixdeck/fenixdeck_v0.1.0_flash_4MB.bin
```

### Version status (checked 2026-10-29)

On the ESP32, peripheral drivers (SPI, UART, LEDC, NVS, Wi-Fi) are compiled into the app and the 2nd-stage bootloader. There is no separate driver to update: updating means rebuilding with a newer ESP-IDF and reflashing. The 1st-stage ROM bootloader is in mask ROM and cannot be changed.

| Part | On the board / PC | Latest | Source |
|---|---|---|---|
| ESP-IDF (app + 2nd-stage bootloader; both carry `v5.5.5`) | v5.5.5 (2026-07-17) | v5.5.6 on the 5.5 line (2026-10-16); v6.0.3 (2026-09-02); **v6.1** stable (2026-08-27) | GitHub releases API (`espressif/esp-idf`) |
| LovyanGFX | 1.2.35 (FenixDeck submodule, per `docs/toolchain.md`; the version string is not in the binary) | 1.2.35 (2026-10-25) | GitHub releases API (`lovyan03/LovyanGFX`) |
| CH340 Windows driver (PC side) | 3.3.2011.11 (2011, installed `ch341ser.inf`); the vendor copy in `docs/vendor/CH340/` is older still, 3.1.2009.06 | 3.9.2024.9 per the Microsoft Update Catalog and oemdrivers.com; WCH's own page renders by JS and was not read | web search |

v5.5.6 changes for the classic ESP32 are minor (no ESP32-specific fixes stood out in its notes; it has Wi-Fi supplicant security ports). ESP-IDF v5.5 is supported until January 2028. Moving to v6.x needs a check that LovyanGFX works on it (still unconfirmed; see `docs/POC-WRITEUP.md` §7.5). The 2011 CH340 driver has worked here without errors at 460800 baud.

**Decision (user, 2026-10-29): stay on the current versions** (IDF v5.5.5, LovyanGFX 1.2.35, CH340 driver 3.3.2011.11).

## Design-relevant findings (2026-10-28)

- **No TE pin wired** on the LCD connector, so there is no hardware vsync. Use sprites + dirty-rect DMA pushes (see `docs/PLAN.md` §2).
- **UART0 is shared** between the PC link and the ESP-IDF console. Firmware must turn off console logs on UART0.
- **DTR/RTS drive the auto-reset circuit.** The PC app opens the port with both released, which was verified not to reset the board (see below).
- **No PSRAM**: no room for a full 150 KB framebuffer.

## Host PC

- CPU: Intel Core i7-13620H (13th gen, 16 threads). GPUs: Intel UHD + NVIDIA GeForce RTX 4060 Laptop.
- Toolchain on 2026-10-28: rustc 1.99.1, node 24.13.1, Python 3.14.3, git 2.56, WebView2 149. ESP-IDF, cmake, ninja and pnpm were installed in Phase 0 (see `docs/toolchain.md`).

## Findings while building v0.1 (2026-10-28)

- **Port open does not reset the board.** Verified: `open_port` (DTR/RTS released) gives no boot log or reset (`esp_reset_reason` = 1, power-on, with uptime growing across sessions). Note (2026-10-29): an EN-pin reset also reports reason 1 on the ESP32. After an `esptool` hard reset (RTS pulls EN), the board logged `boot reason 1, up 39 s`. So the proof is the uptime that keeps growing, not the reason code.
- **LCD read-back works** (MISO on IO12 is wired). `fenix shot` / `rawshot` capture exact screenshots, and `docs/screens/` holds the README ones.
- **Colours are correct** with `invert=false`, `rgb_order=false` on the ILI9341: no inversion fix needed.
- **Touch works** with the default raw range (taps, page cycling and events verified with the stylus). Hold the screen at boot to calibrate.
- **Render cost:** a full CPU page (gauge, KV, 16 core bars, sparkline, header) takes ~27 ms, Overview ~15–25 ms. Dirty-only redraw keeps a 1 Hz poll at ~1–2 frames/s.
- **UI task stack:** ~2.6 KB used of 8 KB.
- **`link` is a POSIX function name** in ESP-IDF's newlib, so the firmware namespace is `pclink`.
- **Unplug/replug recovery (2026-10-28, measured):** `tools/replug_watch.ps1` logs when COM9 appears or disappears (50 ms polling), and `fenix demo --wait` logs session events with timestamps. Across 3 physical replugs by the user, port reappearance → reconnected took **1311 / 1347 / 1336 ms**, including board boot (`boot reason 1, up 0 s`), handshake and the 1 s rescan. Unplug was detected within 30 ms of the port disappearing (`read: Access is denied (os error 5)`). The target was < 3 s.
- **Software unplug simulation is not possible on this PC right now:** `pnputil /disable-device` fails ("pending system reboot") and `/remove-device` defers until reboot. A physical replug brings the device back.
