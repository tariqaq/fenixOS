# Board info (read from hardware)

Chip info and the stock backup were read on 2026-10-28 with `esptool v5.6.0`, before anything was written. The board has since been flashed with the FenixDeck firmware (`board/`); the stock image can be restored (below).

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

## Design-relevant findings (2026-10-28)

- **No TE pin wired** on the LCD connector, so there is no hardware vsync. Use sprites + dirty-rect DMA pushes (see `docs/PLAN.md` §2).
- **UART0 is shared** between the PC link and the ESP-IDF console. Firmware must turn off console logs on UART0.
- **DTR/RTS drive the auto-reset circuit.** The PC app opens the port with both released, which was verified not to reset the board (see below).
- **No PSRAM**: no room for a full 150 KB framebuffer.

## Host PC

- CPU: Intel Core i7-13620H (13th gen, 16 threads). GPUs: Intel UHD + NVIDIA GeForce RTX 4060 Laptop.
- Toolchain on 2026-10-28: rustc 1.99.1, node 24.13.1, Python 3.14.3, git 2.56, WebView2 149. ESP-IDF, cmake, ninja and pnpm were installed in Phase 0 (see `docs/toolchain.md`).

## Findings while building v0.1 (2026-10-28)

- **Port open does not reset the board.** Verified: `open_port` (DTR/RTS released) gives no boot log or reset (`esp_reset_reason` = 1, power-on, with uptime growing across sessions).
- **LCD read-back works** (MISO on IO12 is wired). `fenix shot` / `rawshot` capture exact screenshots, and `docs/screens/` holds the README ones.
- **Colours are correct** with `invert=false`, `rgb_order=false` on the ILI9341: no inversion fix needed.
- **Touch works** with the default raw range (taps, page cycling and events verified with the stylus). Hold the screen at boot to calibrate.
- **Render cost:** a full CPU page (gauge, KV, 16 core bars, sparkline, header) takes ~27 ms, Overview ~15–25 ms. Dirty-only redraw keeps a 1 Hz poll at ~1–2 frames/s.
- **UI task stack:** ~2.6 KB used of 8 KB.
- **`link` is a POSIX function name** in ESP-IDF's newlib, so the firmware namespace is `pclink`.
- **Unplug/replug recovery (2026-10-28, measured):** `tools/replug_watch.ps1` logs when COM9 appears or disappears (50 ms polling), and `fenix demo --wait` logs session events with timestamps. Across 3 physical replugs by the user, port reappearance → reconnected took **1311 / 1347 / 1336 ms**, including board boot (`boot reason 1, up 0 s`), handshake and the 1 s rescan. Unplug was detected within 30 ms of the port disappearing (`read: Access is denied (os error 5)`). The target was < 3 s.
- **Software unplug simulation is not possible on this PC right now:** `pnputil /disable-device` fails ("pending system reboot") and `/remove-device` defers until reboot. A physical replug brings the device back.
