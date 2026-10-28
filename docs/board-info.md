# Board info (read from hardware)

Chip info and the stock backup were read on 2026-10-28 with `esptool v5.6.0`, before anything was written. The board was then flashed with the FenixDeck firmware (FenixDeck repo `board/`; that code is not in fenixOS). Chip info was re-read on 2026-10-29 and matched.

**Current state (2026-10-29): flash fully erased** (`esptool erase-flash`). A full 4 MB read-back was all `0xFF` (0 non-blank bytes). The board has no firmware now: the ROM bootloader runs and finds no valid image. Both earlier images are backed up and can be restored (below).

**Backups are local only** (`firmware/` is in `.gitignore`): the repo is public, and both images contain vendor code (the stock LVGL demo; the FenixDeck dump still holds 172 blocks of it). Keep `firmware/` safe outside git (for example, a private cloud folder). The SHA-256 values below identify the files.

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

## FenixDeck firmware (on the board until the 2026-10-29 wipe)

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

- **No TE pin wired** on the LCD connector, so there is no hardware vsync. Use sprites + dirty-rect DMA pushes (see `docs/legacy/PLAN.md` §2).
- **UART0 is shared** between the PC link and the ESP-IDF console. Firmware must turn off console logs on UART0.
- **DTR/RTS drive the auto-reset circuit.** The PC app opens the port with both released, which was verified not to reset the board (see below).
- **No PSRAM**: no room for a full 150 KB framebuffer.

## Host PC

- CPU: Intel Core i7-13620H (13th gen, 16 threads). GPUs: Intel UHD + NVIDIA GeForce RTX 4060 Laptop.
- Toolchain on 2026-10-28: rustc 1.99.1, node 24.13.1, Python 3.14.3, git 2.56, WebView2 149. ESP-IDF, cmake, ninja and pnpm were installed in Phase 0 (see `docs/toolchain.md`).

## Findings while building v0.1 (2026-10-28)

- **Port open does not reset the board.** Verified: `open_port` (DTR/RTS released) gives no boot log or reset (`esp_reset_reason` = 1, power-on, with uptime growing across sessions). Note (2026-10-29): an EN-pin reset also reports reason 1 on the ESP32. After an `esptool` hard reset (RTS pulls EN), the board logged `boot reason 1, up 39 s`. So the proof is the uptime that keeps growing, not the reason code.
- **LCD read-back works** (MISO on IO12 is wired). `fenix shot` / `rawshot` capture exact screenshots, and `docs/legacy/screens/` holds the README ones.
- **Colours are correct** with `invert=false`, `rgb_order=false` on the ILI9341: no inversion fix needed.
- **Touch works** with the default raw range (taps, page cycling and events verified with the stylus). Hold the screen at boot to calibrate.
- **Render cost:** a full CPU page (gauge, KV, 16 core bars, sparkline, header) takes ~27 ms, Overview ~15–25 ms. Dirty-only redraw keeps a 1 Hz poll at ~1–2 frames/s.
- **UI task stack:** ~2.6 KB used of 8 KB.
- **`link` is a POSIX function name** in ESP-IDF's newlib, so the firmware namespace is `pclink`.
- **Unplug/replug recovery (2026-10-28, measured):** `tools/replug_watch.ps1` logs when COM9 appears or disappears (50 ms polling), and `fenix demo --wait` logs session events with timestamps. Across 3 physical replugs by the user, port reappearance → reconnected took **1311 / 1347 / 1336 ms**, including board boot (`boot reason 1, up 0 s`), handshake and the 1 s rescan. Unplug was detected within 30 ms of the port disappearing (`read: Access is denied (os error 5)`). The target was < 3 s.
- **Software unplug simulation is not possible on this PC right now:** `pnputil /disable-device` fails ("pending system reboot") and `/remove-device` defers until reboot. A physical replug brings the device back.

## fenixOS spikes (P1, 2026-10-29)

Spike projects live in `board/spikes/` (`sdkconfig.spike` turns the UART0 console on; results print at 115200, read with `python tools/serial_read.py 30 --reset`).

### S1: LVGL 9.5 + LovyanGFX rendering

Scene: full-screen panel with 6 rounded tiles + labels, LVGL partial mode, two DMA draw buffers, `LV_COLOR_FORMAT_RGB565_SWAPPED` pushed raw by `pushImageDMA`, refresh period 16 ms (62 fps cap).

| Setup | Fade (layer opacity) | Scrim fade (black rect over content) | Slide (whole panel) | Small 40×40 move | Full redraw |
|---|---|---|---|---|---|
| Flash DIO 40 MHz, LCD 55 MHz, 24 lines | 19 fps | 20 fps | 38 fps | 62 fps | 49 ms |
| + LVGL FreeRTOS, 2 SW draw units | 10 fps | 10 fps | 23 fps | 60 fps | 103 ms |
| Flash **QIO 80 MHz**, LCD 55 MHz, 24 lines | 28 fps | 31 fps | 50 fps | 62 fps | 33 ms |
| Flash QIO 80 MHz, **LCD 80 MHz, 32 lines** | **36 fps** | **42 fps** | **59 fps** | **62 fps** | **19 ms** |

Findings:
- **Rendering, not SPI, was the limit**, and most of it was code and font reads from flash: switching flash from DIO 40 MHz to **QIO 80 MHz** gave +50 %. The chip runs QIO fine (`flash io: qio`).
- **LCD SPI at 80 MHz works**: a colour-bar write at 80 MHz read back over MISO (20 MHz) with **0 wrong pixels**, and `display_selftest()` stays in `fenix_hal` for later checks. HSPI pins 14/13/12/15 are the IOMUX pins, so 80 MHz needs no GPIO matrix.
- **LVGL's FreeRTOS OS layer with 2 draw units halves the frame rate** here (task hand-off cost outweighs the second core for these small areas). Not used; LVGL runs in one UI task.
- `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` overflows IRAM by ~30 KB. Not used.
- Draw buffers: **2 × 320 × 32 lines = 40 KB** (16→24→40 lines gave 25→28→31 fps on the fade; 32 is the balance). Heap after LVGL init and buffers: ~243 KB free of ~297 KB.
- Portrait (rotation 0) and landscape (1) perform the same.
- Motion rule that follows: transitions use **slides and scrims** (≥ 40 fps) rather than layer opacity on big areas (~36 fps).
- Touch mapping per rotation needs a person at the board; it is checked in the morning review (LovyanGFX rotates touch with the panel).

### S2: board fonts

`tools/gen_fonts.py` builds 11 Inter text fonts (4 bpp, uncompressed for render speed: 64/56 px clock digits, 22, 17, 15, 15 semibold, 13 ×3 weights, 12 ×2) and 6 Phosphor icon fonts (fill 16/20/26/40, bold 10/14) with `lv_font_conv` 1.5.3: 642 KB of C source. Flash cost is measured in the first full firmware build (P4). Tabular clock digits: `lv_font_conv` cannot apply Inter's `tnum` feature, so the clock widget places each digit in a fixed-width cell.

### S3: link speed (UART0 ↔ CH340C)

`board/spikes/s3` echoes UART0; `tools/spike_s3_baud.py` sends random 1000-byte blocks with a CRC32 and checks the echo, 90 s per rate (ping-pong, so throughput is latency-bound, not the line rate).

| Baud | Blocks ok | Bad | Echo throughput |
|---|---|---|---|
| 460800 | 3175 | 0 | 34.4 KiB/s each way |
| **921600** | **4860** | **0** | **52.7 KiB/s each way** |
| 1500000 | 4842 | 5 | 52.5 KiB/s |
| 2000000 | 899 | 135 | 9.8 KiB/s |

**Decision: 921600 baud** for the fenixOS link (raw line rate ~90 KiB/s, double the PoC). 1.5 Mbaud gives no gain and drops blocks; 2 Mbaud fails. (The PoC's 921600 failure was an `esptool` bulk flash read, a different path.) With the console off, the app must route UART0 to GPIO1/3 itself (`uart_set_pin`); without it nothing is received.

## fenixOS board core (P4, 2026-10-29)

- **Image:** 796 KB app (`fenixos.bin`, 62% of the 2 MB factory slot free). Free heap with Home shown: ~140 KB (`HELLO_ACK` 143,668 B; the gate is ≥ 60 KB).
- **Link task:** UART0 at 921600 with a 4 KB RX and a 16 KB TX buffer. The TX buffer is also the stream ring: `stream_rect` checks `uart_get_tx_buffer_free_size` and drops a band (marking it stale) instead of blocking the UI.
- **Stream:** each flushed area is cut into bands of ≤ 3 KB raw and sent RLE16 (raw when RLE is not smaller). A static Home screen streams at ~3–4 KB/s.
- **Reset reason:** a reset through the EN pin (the PC's RTS line, esptool, `serial_read.py --reset`) reports `ESP_RST_POWERON`, the same as a cold power-on, and the RTC clock restarts. So the "time valid" flag is cleared at boot on POWERON (`hal::time_boot_check`); it survives `esp_restart` and deep sleep. (Corrected: the first version also required reset reason ≠ POWERON when reading the flag, so the time never became valid after a PC-triggered reset.)
- **LVGL input gotchas:**
  - Only clickable objects take presses, and a drag scrolls the pressed object or its scrollable parents. Plain layout boxes are non-clickable (so taps reach the row, tile or card), and scroll containers are made clickable on purpose. Before this, taps on icons inside rows were lost and swipes did not scroll.
  - A flex column centres items inside its track, which is only as wide as the widest item. `track_cross_place` must be CENTER too, or centred layouts (Dock home) sit to the left.
  - Instant screen swaps go through `lv_screen_load_anim(..., 0, 0, true)`, never `lv_screen_load` plus a manual delete: that can delete the old screen twice while a slide is still running.
- **First `fenix hello` after one flash** failed 4 times with "no HELLO_ACK", then always worked (a raw Python HELLO got answers at the same time). Not reproduced in ~20 later flashes. Watch for it.
