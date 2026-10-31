# Board info (read from hardware)

Chip info and the stock backup were read on 2026-10-28 with `esptool v5.6.0`, before anything was written. The board was then flashed with the FenixDeck firmware (FenixDeck repo `board/`; that code is not in fenixOS). Chip info was re-read on 2026-10-29 and matched.

**Current state (2026-11-01): fenixOS v0.1.15** (flashed with `fenix flash`).

Current numbers (v0.1.12 unless marked; the sections below are a dated log, so older numbers in them are history):

| What | Value |
|---|---|
| Firmware image | 1,117,296 B in the 2 MB factory slot (47% free, v0.1.15); IRAM 111.8 of 128 KB, static DRAM 50.8 KB (P38) |
| Heap at boot, Wi-Fi joined | 81.8 KB free of 281 KB (P40); 82.2 KB at v0.1.15 (`fenix hello`, 8 s after boot) |
| Heap at boot, radio off | 154.9 KB (P38, before the P40 trims) |
| Animation fps (`fenix bench 15 nostream`) | 25.5 (P38) |
| Stream over USB / Wi-Fi (`fenix bench 15`) | 25.0 fps, 43 KB/s / 25.8 fps, 53 KB/s; lowest heap in a Wi-Fi stream 23.9 KB (P40) |
| Install of a 32 KB package | 0.48 s over USB, 0.41-0.55 s over Wi-Fi; lowest heap 51.3 KB (P39) |
| App store | LittleFS, 1984 KB (8 KB used when empty) |

 Corrected: the board was fully erased on 2026-10-29 (`esptool erase-flash`, a 4 MB read-back all `0xFF`) before fenixOS went on it. Both earlier images are backed up and can be restored (below).

Since P23, every flash read, write and erase goes through `fenix` (`flash`, `write`, `read-flash`, `erase`), not esptool: esptool's read timeouts take the CH340 driver's crash path (below). The esptool commands further down are a record of what was run before that.

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
fenix write 0 firmware/stock/stock_flash_4MB.bin
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
fenix write 0 firmware/fenixdeck/fenixdeck_v0.1.0_flash_4MB.bin
```

(Corrected 2026-10-30: these restores used esptool `write-flash`; `fenix write` does the same at 460800 baud without the CH340 crash path. Quit the PC app first. A new backup: `fenix read-flash 0 0x400000 <file>`.)

### Version status (checked 2026-10-29)

On the ESP32, peripheral drivers (SPI, UART, LEDC, NVS, Wi-Fi) are compiled into the app and the 2nd-stage bootloader. There is no separate driver to update: updating means rebuilding with a newer ESP-IDF and reflashing. The 1st-stage ROM bootloader is in mask ROM and cannot be changed.

| Part | On the board / PC | Latest | Source |
|---|---|---|---|
| ESP-IDF (app + 2nd-stage bootloader; both carry `v5.5.5`) | v5.5.5 (2026-07-17) | v5.5.6 on the 5.5 line (2026-10-16); v6.0.3 (2026-09-02); **v6.1** stable (2026-08-27) | GitHub releases API (`espressif/esp-idf`) |
| LovyanGFX | 1.2.35 (FenixDeck submodule, per `docs/toolchain.md`; the version string is not in the binary) | 1.2.35 (2026-10-25) | GitHub releases API (`lovyan03/LovyanGFX`) |
| CH340 Windows driver (PC side) | 3.3.2011.11 (2011, installed `ch341ser.inf`); the vendor copy in `docs/vendor/CH340/` is older still, 3.1.2009.06 | 3.9.2024.9 per the Microsoft Update Catalog and oemdrivers.com (corrected 2026-10-29: WCH's own `CH341SER.EXE` now carries **4.0.2026.2**, dated 2026-02-11); WCH's own page renders by JS and was not read | web search |

v5.5.6 changes for the classic ESP32 are minor (no ESP32-specific fixes stood out in its notes; it has Wi-Fi supplicant security ports). ESP-IDF v5.5 is supported until January 2028. Moving to v6.x needs a check that LovyanGFX works on it (still unconfirmed; see `docs/POC-WRITEUP.md` §7.5). The 2011 CH340 driver works at 460800 baud, but it crashed Windows once (bugcheck 0x48, see below).

**Decision (user, 2026-10-29): stay on the current versions** (IDF v5.5.5, LovyanGFX 1.2.35, CH340 driver 3.3.2011.11).

Corrected (2026-10-29, after a PC crash): **update the CH340 driver** to WCH's current CH341SER. **Done 2026-10-29 16:22:** `CH341SER.EXE` from `https://www.wch-ic.com/download/file?id=65` (799,096 B, SHA-256 `458c37bd...12cad1`, Authenticode valid, signed by Nanjing Qinheng Microelectronics) staged driver **4.0.2026.2** (`oem164.inf`), but COM9 kept the 2011 driver until `pnputil /delete-driver oem160.inf /uninstall` removed it. COM9 then runs 4.0.2026.2 and the board works (handshake at 921600 baud, 10 s bench with 0 RX errors). Driver 3.3.2011.11 (`CH341S64.SYS`, `oem160.inf`) blue-screened the PC at 15:30: bugcheck `0x48 CANCEL_STATE_IN_COMPLETED_IRP`, `PROCESS_NAME: fenix.exe`, `CH341S64+4120`. The stack is `NtReadFile` then `IopCancelAlertedRequest` then `IoCancelIrp`: a synchronous read on COM9 was cancelled after the driver had completed it without clearing its cancel routine. This is a driver race, not a hardware fault. Ending a process with a read pending triggers it: here an agent force-killed `fenix logs` while it held COM9. Until the driver is updated, do not force-kill a fenixOS program that has the port open, and quit the app before unplugging the board. If Windows keeps the old driver, remove it from an elevated shell (`pnputil /delete-driver oem160.inf /uninstall /force`), then install the new one. The app now closes the port before it exits (`Link::shutdown` on Quit and before an app update), so a normal Quit cancels no read. The full write-up stays local (`docs/tempcrashlogs/`, git-ignored).

Corrected (2026-10-29, second crash): **the driver update was not the fix.** Driver 4.0.2026.2 blue-screened the PC at 20:39 while the installed app (v0.1.0, `fenixos.exe`) used COM9: bugcheck `0xD1 DRIVER_IRQL_NOT_LESS_OR_EQUAL`, read of address `0x44` at DISPATCH_LEVEL, `CH341S64+48e9` (`cmp byte ptr [rdi+44h],0`), and `0x800` (our 2048-byte read buffer) on the stack. No USB disconnect was logged. **Cause, from the driver's code** (capstone disassembly of `CH341S64.SYS` 4.0.2026.2):
- The function at `+0x4740` starts a read. It takes the current read request from the device extension (`+0x3a0`) and the port's COMMTIMEOUTS (`+0xd8` interval, `+0xdc` multiplier, `+0xe0` constant).
- When a read timeout is set, it takes the cancel spin lock and re-reads the current request (`+0x48e2`). It then checks `irp->Cancel` (IRP offset `0x44`), and from `+0x4a61` it sets a cancel routine and arms the timeout timers (`KeSetTimer`).
- Another path (the data-arrival or timer DPC) can complete the request and clear that pointer in between, so the check reads through NULL. Crash 1 (`0x48`, a cancel on a completed request) is the same family: requests that wait with timers and a cancel routine.
- Our link used serialport's timeouts: interval and multiplier `MAXDWORD`, constant 15 ms ("return at once if bytes are there, else wait for the first byte"). That takes this path on every read, about 66 times a second.

**Fix (fenix-link, P16):** COMMTIMEOUTS `MAXDWORD, 0, 0` set with `SetCommTimeouts` (`no_wait_reads`). In that mode the driver completes every read at once (`+0x486d` to `IofCompleteRequest`), with no timer, no cancel routine and no request left pending. The link sleeps 2 ms (bytes flowing) or 10 ms (idle) itself when a read finds nothing. Measured: stream bench 39.9–43.6 KB/s (41.8 with no sleep at all, so the stream is limited by the board), CLI 1.2 % of one core while streaming, app 0.5 %. **Until a release with this fix is installed, do not run fenixOS v0.1.0 with the board plugged in.** espflash (app install / probe) still opens the port with its own timeouts, only while flashing. Corrected (2026-10-30, P23): that left two gaps. espflash 4.6.0 takes serialport's `COMPort` type directly and calls `set_timeout` (interval and multiplier `MAXDWORD` plus a constant, the crash-2 pattern) around every command, so the app's firmware update and probe used the risky path while flashing; and esptool (`idf.py flash`, pyserial) sets a plain total timeout (`0, 0, constant`), which also arms the driver's timers. **Fix (P23):** serialport is patched (`third_party/serialport`, `[patch.crates-io]`): on Windows `set_timeout` always sets `MAXDWORD, 0, 0` and no write timeout, and `read` polls every 1 ms up to the timeout itself. Every Rust user gets it (fenix-link, espflash). The flasher moved to crate `fenix-flash`, shared by the app and `fenix flash`, which replaces `idf.ps1 flash` for this board. Tested: two full 832 KB writes with `fenix flash`, 22 s each at 460800 baud, board booted both times. Then (P26) `fenix flash <build dir>` flashes any ESP-IDF build from its `flasher_args.json` (spikes), `fenix write` writes a raw file such as a backup, and `fenix read-flash` reads flash (checked: the partition table and the first 256 KB of the app read back identical to the build). `tools/serial_read.py` reads without a timeout. Still outside our control: other serial programs (terminals, monitors, esptool) on this board, because the bug is in the driver; avoid them.

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
- **Touch works** with the default raw range (taps, page cycling and events verified with the stylus). Hold the screen at boot to calibrate. **Corrected 2026-10-29:** that check only cycled pages, so any tap counted. With the fenixOS landscape layout the default range was mirrored left to right; see "Hands-on review" below.
- **Render cost:** a full CPU page (gauge, KV, 16 core bars, sparkline, header) takes ~27 ms, Overview ~15–25 ms. Dirty-only redraw keeps a 1 Hz poll at ~1–2 frames/s.
- **UI task stack:** ~2.6 KB used of 8 KB.
- **`link` is a POSIX function name** in ESP-IDF's newlib, so the firmware namespace is `pclink`.
- **Unplug/replug recovery (2026-10-28, measured):** `tools/replug_watch.ps1` (FenixDeck-era script, not in this repo now) logs when COM9 appears or disappears (50 ms polling), and `fenix demo --wait` logs session events with timestamps. Across 3 physical replugs by the user, port reappearance → reconnected took **1311 / 1347 / 1336 ms**, including board boot (`boot reason 1, up 0 s`), handshake and the 1 s rescan. Unplug was detected within 30 ms of the port disappearing (`read: Access is denied (os error 5)`). The target was < 3 s.
- **Software unplug simulation is not possible on this PC right now:** `pnputil /disable-device` fails ("pending system reboot") and `/remove-device` defers until reboot. A physical replug brings the device back.

## fenixOS spikes (P1, 2026-10-29)

Spike projects live in `board/spikes/` (`sdkconfig.spike` turns the UART0 console on; results print at 115200, read with `python tools/serial_read.py 30 --reset`).

### S1: LVGL 9.5 + LovyanGFX rendering

Scene: full-screen panel with 6 rounded tiles + labels, LVGL partial mode, two DMA draw buffers, `LV_COLOR_FORMAT_RGB565_SWAPPED` pushed raw by `pushImageDMA`, refresh period 16 ms (62 fps cap).

| Setup | Fade (layer opacity) | Scrim fade (black rect over content) | Slide (whole panel) | Small 40×40 move | Full redraw |
|---|---|---|---|---|---|
| Flash DIO 40 MHz, LCD "55 MHz" (runs at 40), 24 lines | 19 fps | 20 fps | 38 fps | 62 fps | 49 ms |
| + LVGL FreeRTOS, 2 SW draw units | 10 fps | 10 fps | 23 fps | 60 fps | 103 ms |
| Flash **QIO 80 MHz**, LCD "55 MHz" (runs at 40), 24 lines | 28 fps | 31 fps | 50 fps | 62 fps | 33 ms |
| Flash QIO 80 MHz, **LCD 80 MHz, 32 lines** | **36 fps** | **42 fps** | **59 fps** | **62 fps** | **19 ms** |

Findings:
- **Rendering, not SPI, was the limit**, and most of it was code and font reads from flash: switching flash from DIO 40 MHz to **QIO 80 MHz** gave +50 %. The chip runs QIO fine (`flash io: qio`).
- **LCD SPI at 80 MHz works**: a colour-bar write at 80 MHz read back over MISO (20 MHz) with **0 wrong pixels**, and `display_selftest()` stays in `fenix_hal` for later checks. HSPI pins 14/13/12/15 are the IOMUX pins, so 80 MHz needs no GPIO matrix. **Corrected 2026-10-29:** the colour bars were a few large writes. Under LVGL's many small DMA writes, 80 MHz put bands at the wrong place on the real panel (ghost text, doubled widgets). The LCD now runs at 40 MHz, what the vendor demo and FenixDeck really ran (their "55 MHz" rounds down to 40). The S1 table's "LCD 55 MHz" rows likewise ran at 40 MHz.
- **LVGL's FreeRTOS OS layer with 2 draw units halves the frame rate** here (task hand-off cost outweighs the second core for these small areas). Not used; LVGL runs in one UI task.
- `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` overflows IRAM by ~30 KB. Not used.
- Draw buffers: **2 × 320 × 32 lines = 40 KB** (16→24→40 lines gave 25→28→31 fps on the fade; 32 is the balance). Heap after LVGL init and buffers: ~243 KB free of ~297 KB. (Corrected in P4: the real system screens render fastest with **80-line** bands, see "Rendering speed" below.)
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

- **Image:** ~826 KB app (`fenixos.bin`, 60% of the 2 MB factory slot free, P6 app store and runtime included). Free heap with Home shown and one app installed: ~121 KB (was ~134 KB before P6: +4 KB UART RX buffer, +2 KB link stack, ~5 KB bind queue, tile icons), minimum ~117 KB (the gate is ≥ 60 KB). An open app adds its package (a few KB) and its LVGL objects.
- **Link task:** UART0 at 921600 with a 5 KB RX (`RX_BUF` in `link.cpp`; 4 KB until P6, 8 KB until P40) and a 16 KB TX buffer (corrected, P29: 8 KB; streaming drops about as many bands as before, +130 vs +116 in a 15 s bench). The TX buffer is also the stream ring: `stream_rect` checks `uart_get_tx_buffer_free_size` and drops a band (marking it stale) instead of blocking the UI.
- **Stream:** each flushed area is cut into bands of ≤ 3 KB raw and sent RLE16 (raw when RLE is not smaller). A static Home screen streams at ~3–4 KB/s.
- **Reset reason:** a reset through the EN pin (the PC's RTS line, esptool, `serial_read.py --reset`) reports `ESP_RST_POWERON`, the same as a cold power-on, and the RTC clock restarts. So the "time valid" flag is cleared at boot on POWERON (`hal::time_boot_check`); it survives `esp_restart` and deep sleep. (Corrected: the first version also required reset reason ≠ POWERON when reading the flag, so the time never became valid after a PC-triggered reset.)
- **LVGL input gotchas:**
  - Only clickable objects take presses, and a drag scrolls the pressed object or its scrollable parents. Plain layout boxes are non-clickable (so taps reach the row, tile or card), and scroll containers are made clickable on purpose. Before this, taps on icons inside rows were lost and swipes did not scroll.
  - A flex column centres items inside its track, which is only as wide as the widest item. `track_cross_place` must be CENTER too, or centred layouts (Dock home) sit to the left.
  - Instant screen swaps go through `lv_screen_load_anim(..., 0, 0, true)`, never `lv_screen_load` plus a manual delete: that can delete the old screen twice while a slide is still running.
- **First `fenix hello` after one flash** failed 4 times with "no HELLO_ACK", then always worked (a raw Python HELLO got answers at the same time). Not reproduced in ~20 later flashes. Watch for it.

### Rendering speed (P4, 2026-10-29)

Measured with `fenix bench` (opens and closes Settings every second: two full-screen slides) and the animation fps in `STATS` (frames ≤ 100 ms apart only).

| Change | Slide fps | Render per frame | Free heap |
|---|---|---|---|
| Start (stale `sdkconfig`, 32-line bands) | 13 | ~46 ms | 143 KB |
| Fresh `sdkconfig` from `sdkconfig.defaults` | 25 | ~26 ms | 196 KB |
| + LVGL circle cache 16, style cache | 25 | ~26 ms | 196 KB |
| + 48-line bands | 29 | | 175 KB |
| **+ 80-line bands (kept)** | **34.5** | ~21 ms | **134 KB** (min 121) |
| 80-line bands, streaming every frame | 21.7 | | 129 KB |
| **80-line bands, stream throttled to the PC rate (kept)** | **29** | | 134 KB |

- **Stale `board/sdkconfig`:** it was made before `sdkconfig.defaults` had its final values, and IDF never re-applies defaults to an existing `sdkconfig`. The firmware ran DIO 40 MHz flash, LVGL's own 64 KB pool and a 33 ms refresh. Fix: delete `board/sdkconfig` after editing `sdkconfig.defaults` (`docs/toolchain.md`). CI always builds from the defaults.
- Rounded-corner clipping (`clip_corner` on grouped lists) cost nothing measurable here; it is off anyway, and pressed rows round their own highlight.
- **Bands:** fewer, taller bands mean fewer passes over the object tree per frame. 80 lines = 3 bands in landscape, 4 in portrait. Corrected (P29, 2026-10-30): remeasured the same way (`fenix bench 15 nostream`), 80 lines now gives 25.6 fps, 48 lines 24.9, 40 lines 24.0, 20 lines 20.7. The P4 numbers above no longer hold (later features slowed the slide; not looked into). The bands are now 40 lines: 51 KB more free heap for 1.6 fps.
- **Stream throttle:** only frames on the PC's `STREAM_START` rate are encoded. Areas drawn in between, and bands that did not fit the TX buffer, are marked stale and redrawn for the stream when the screen is idle (or after 1 s). During slides the stream is link-bound (~45 KB/s, ~3.5 screens of pixels per s, RLE ~11×).
- **Shut down (deep sleep at first, light sleep since P19), 2026-10-29:** Session › Shut down fades the backlight, holds the backlight and LED pins and sleeps; the link goes silent at once. A PC-side reset through RTS (EN pin) wakes it as a normal boot (reset reason POWERON, so the time is unset again). Tap-to-wake (ext0 on GPIO36) still needs a check by hand. The PC app can offer "Wake board" through the same RTS pulse. **Update:** tap-to-wake failed at first and works since the touch-bus fix in "Hands-on review" below.
- **Rotations:** all four work, including touch through `fenix tap` (LovyanGFX rotates touch with the panel). The stream sends logical coordinates, so PC screenshots are always upright. **Note:** `fenix tap` injects logical coordinates and skips the touch panel, so it could not catch the mirrored default range.

## Release hardware pass (P10, 2026-10-29)

- **1 h link soak** (`fenix soak 60`, v0.1.0, PC app closed): 1 connect, 0 disconnects, 0 RX errors over 4578 frames, max round trip 22 ms, free heap 120016 B at the end, minimum 110052 B, 0 stream drops.
- **Screen walk** (`tools/capture_screens.sh`, all 20 screens in landscape and portrait, with the sample app): pixel-identical to `docs/screens/board/` except the live clock on Home (Tiles, Dock, portrait) and Session, and the Host PC navbar, which the older reference caught mid-fade. **Corrected:** `fenix shot` captures what LVGL rendered, not the glass, so this walk could not see the 80 MHz panel corruption the user filmed the same day.
- Not covered remotely: tap-to-wake after Shut down, LED brightness, touch corners, physical replug (open items in `tasks.md`).

## Hands-on review (2026-10-29, user at the board)

Two phone videos (`docs/recordedvids/`, not in git) and live tests with the user showed bugs that remote testing had missed.

- **Touch mirrored left to right** with the default range. Corner taps in landscape (USB left), raw → mapped with the old range: top-left 3760, 3679 → 299, 10; top-right 3738, 241 → 3, 11; bottom-right 512, 184 → 0, 225; bottom-left 405, 3583 → 291, 233. Raw y runs right to left (~3630 at the left edge, ~210 at the right), so `y_min`/`y_max` are now 3900/200 (`fenix_lgfx.hpp`). LovyanGFX builds the touch affine from the four corners, so a reversed range simply mirrors that axis. Confirmed by the user; they also ran Settings › Display › Calibrate touch, so this board now uses its NVS calibration.
- **Garbled drawing at 80 MHz LCD SPI:** text and widgets drawn twice at offsets, smeared rows. LCD write clock lowered. The ESP32 SPI clock is 80 MHz / (n + 1) (LovyanGFX `FreqToClockDiv`), so anything between 41 and 79 MHz runs at 40 MHz: the vendor demo's and FenixDeck's "55 MHz" was 40 MHz. The config now says 40 MHz. Cost: slides 26.1 fps without streaming (34.5 at 80 MHz).
- **Tap-to-wake did not wake.** The touch bus pins (CS 33, CLK 25, MOSI 32) floated in deep sleep; they are now held (CS high, CLK and MOSI low) so the XPT2046 keeps PENIRQ armed. Wake then worked. **Follow-up bug:** the holds outlived the wake-up and locked touch out; `display_init` now releases them (`gpio_deep_sleep_hold_dis` + `gpio_hold_dis`).
- **RGB LED glowed green in deep sleep** (and faintly while "off"). The faint glow while on was LEDC's 8-bit duty 255 still pulling low 1/256 of the time: fixed by inverting the LEDC output (duty 0 = steady off). Corrected (2026-10-29, user: still green while shut down after that fix): the first explanation (LEDC kept driving the held pads) was not the cause, and `esp_sleep_pd_config(VDDSDIO, ON)` did nothing, because ESP-IDF's deep sleep always powers VDD_SDIO down on the ESP32 (`RTC_SLEEP_PD_VDDSDIO` in `deep_sleep_start`'s forced flags, `esp_hw_support/sleep_modes.c`). GPIO16 (green) and GPIO17 (blue) are VDD_SDIO pads; with the rail off, LED current flows from 3.3 V through the LED into GPIO16's protection diode and the dead rail (which also feeds the flash), so green lights. Blue needs more voltage than is left; red (GPIO4) is an RTC pad and stays dark. **Fix (P19):** Shut down uses light sleep, which keeps VDD_SDIO up, with the same ext0 wake on GPIO36, then restarts; an RTC flag keeps "wake from sleep" as the reported boot reason. Light sleep draws about 1 mA more than deep sleep, nothing next to the always-powered CH340.
- A purple flash on wake is the LED pins' reset state during boot, before `led_init`.

### USB replug (2026-10-29)

With the PC app running and CH340 driver 4.0.2026.2, unplugging the board ends the session with "Board disconnected (read: Access is denied. (os error 5))": Windows reports a vanished COM port to an open handle as access denied. Plugging it back in, the app finds the board on COM9 again by itself (boot reason power on). No crash. The clock is still right after a tap wake from Shut down (user, 2026-10-29). The LED checks (off in sleep, connect pulse level) are deferred by the user.

## Battery and SD card (planning notes, 2026-10-29)

- **No battery circuit on the board.** Power is 5 V (USB or the 5 V connector) into an AMS1117-3.3. A LiPo needs a charger plus a 5 V boost (or a one-chip "power bank" module) feeding that 5 V input; 3.7 V straight into 5 V leaves the AMS1117 below 3.3 V.
- **Draw (estimates, not measured here):** ~115 mA at 5 V with the backlight full, about 40 % less dimmed (secondary spec pages); Wi-Fi adds ~80–120 mA on average with TX peaks near 240 mA (ESP32 datasheet), BLE ~30–50 mA. At the cell, with a ~85 % boost, multiply the 5 V current by ~1.6: UI only ~185 mA, BLE ~240 mA, Wi-Fi ~350 mA. Shut down (light sleep since P19) plus regulator and boost quiescent current is a few mA, so a hard power switch is worth having. Measure with the real firmware before choosing.
- **SD card:** ESP-IDF v5.5.5's FatFs has exFAT compiled out (`FF_FS_EXFAT 0` in `components/fatfs/src/ffconf.h`, no Kconfig option), so cards must be FAT32 (FAT12/16 also work). FAT32 itself is not limited to 32 GB: larger SDXC cards work once reformatted to FAT32 (Windows' own format tool refuses FAT32 above 32 GB; use another formatter). Files are limited to 4 GB.

## Task manager numbers (P27, 2026-10-30)

From `fenix sysinfo` (two SYSINFO answers 1 s apart), v0.1.4 firmware with run-time stats on, Home shown, PC app closed:

- **Tasks:** 6 in all (corrected, P29: `main` moved to core 1): `main` (the UI, core 0, priority 1, 3.6 KB stack free), `link` (core 0, priority 6, 2.2 KB free), `IDLE0`, `IDLE1` (~950 B free), `ipc0`, `ipc1` (priority 24, ~500 B free). No `esp_timer` or `Tmr Svc` task is listed. Corrected: `docs/PLAN.md` put the UI task on core 1; `main` is pinned to core 0, so both busy tasks share core 0 and core 1 stays idle.
- **CPU:** core 0 about 2% busy, core 1 about 0.1% (idle Home). Per-core load = 1 − (idle task run time ÷ elapsed time); run times count in esp_timer µs (`CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER`, 32-bit counters that wrap after ~71 minutes, so the PC subtracts with wrap-around).
- **Memory:** 8-bit heap 302,848 B in all, 117,720 B free, minimum 113,904 B, largest block 94,208 B; all of it DMA-capable.
- **Flash:** 4 MB chip; firmware image 837,584 B in the 2 MB app slot; apps 8 KB of 1984 KB; NVS 4 of 756 entries.
- **Cost of the stats:** `CONFIG_FREERTOS_USE_TRACE_FACILITY` and `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` add a few hundred bytes of code (image 837,584 B vs ~830 KB before) and one counter update per context switch.
- **Link while the page is open:** ~16 B/s to the board, ~200 to 600 B/s from it (one SYSINFO a second, ~230 B with 6 tasks).

## RAM for Wi-Fi and Bluetooth (P29, 2026-10-30)

Measured on the board with a probe build (removed in P32): `board/sdkconfig.radio` on top of the defaults (Bluetooth on, NimBLE host, BLE-only controller), built in `board/build-radio`; `fx::sys::radio_probe()` starts Wi-Fi (station, no network) and BLE advertising once the PC connects and logs the heap after each step.

- **IRAM:** with Wi-Fi's IRAM speed options on (`ESP_WIFI_IRAM_OPT`, `ESP_WIFI_RX_IRAM_OPT`, the defaults) the image does not link: `iram0_0_seg` overflowed by 10,968 B. With both off: IRAM 116 of 128 KB. They only matter for peak Wi-Fi throughput.
- **Fixed cost at boot:** the BT controller reserves 56 KB of DRAM (`BTDM_RESERVE_DRAM` 0xdb5c) and Wi-Fi/lwIP add ~31 KB static. With the v0.1.5 firmware the heap free at boot fell from 118 KB to 46 KB; Wi-Fi then took the rest and BLE init panicked (boot loop, reset reason 4).
- **Releasing the Classic BT part:** `esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)` before NimBLE starts gives back 15 KB.
- **Runtime cost, buffers trimmed** (`sdkconfig.radio`: Wi-Fi static RX 4, dynamic RX/TX 16, no AMPDU; NimBLE 1 connection, 1 bond, smaller msys pools): Wi-Fi start 37 KB, BLE host and advertising 39.5 KB.
- **Result with the P29 cuts** (draw bands 40 lines, TX ring 8 KB): 101 KB free at boot, 116 KB after the release, 79 KB with Wi-Fi up, **39.7 KB with Wi-Fi and BLE both up** (largest block 38.9 KB). Stable, but under the 60 KB gate, and an app package (up to 64 KB, read whole) would not fit.
- **New tasks with both radios:** `wifi` (core 0, priority 23), `tiT` (lwIP), `sys_evt`, `esp_timer`, `Tmr Svc`, `nimble_host`, `btController`. CPU stays under 2% on core 0 when idle.
- **Flash:** the image grows from 0.84 MB to 1.60 MB, 76% of the 2 MB app slot.
- **Core 1 for the UI:** `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1` (P29) moves `main` off core 0, where the link and the radio tasks run. No fps change without radios (24.2 vs 24.0): slides are bound by the SPI display (~31 ms for a full 320 × 240 frame at 40 MHz), not by CPU.

## Radios one at a time, flash and CPU (P30, 2026-10-30)

Measured with a Bluedroid probe (removed in P32; its settings are now in `sdkconfig.defaults`): `board/sdkconfig.bluedroid` on top of the defaults (Bluetooth Classic + BLE, Wi-Fi speed options on, size trims), built in `board/build-bluedroid`. The probe in `radio.cpp` runs one radio per boot and alternates (`fenix reboot`): Bluetooth (SPP server and BLE advertising), then Wi-Fi with all Bluetooth memory released.

- **IRAM:** Bluedroid plus Wi-Fi's IRAM speed options plus lwIP IRAM overflow IRAM by 25,156 B. Moving FreeRTOS, heap and ringbuf functions to flash and turning `LOG_IN_IRAM` off leaves 4,108 B over. `CONFIG_ESP_SYSTEM_ESP32_SRAM1_REGION_AS_IRAM` then fits: IRAM 135 of 172 KB. The extra IRAM comes out of the heap only as far as it is used (~4 KB). It needs a bootloader from ESP-IDF 5.x; ours is v5.5.5.
- **Image:** 1.76 MB with Bluedroid's default sizing, 1.60 MB trimmed (76% of the 2 MB slot). Both include the size trims below.
- **Bluetooth boot, Bluedroid defaults:** 83 KB free at boot. The controller takes 21 KB, Bluedroid 22 KB, SPP 1.7 KB: 38.6 KB free, later 34.7 KB. `BTC_TASK` ran out of stack (32 B left; heap blocks corrupted, a panic followed): the probe logs from SPP and GAP callbacks, which run on `BTC_TASK`. `CONFIG_BT_BTC_TASK_STACK_SIZE=4096` fixed it (908 B left).
- **Bluetooth boot, trimmed** (1 BLE and 1 BR/EDR connection, no GATT client, dynamic env memory, 4 bonds, no Bluetooth logs): static DRAM 82.6 to 57.6 KB. 108 KB free at boot; controller -17.6 KB, Bluedroid -40 KB, SPP -7.9 KB: 42.4 KB free with SPP and BLE advertising, settling at 38.7 KB (min 35.5 KB, largest block 36.8 KB). New tasks on core 0: `BTU_TASK`, `BTC_TASK`, `hciT`, `btController`. Idle load ~1% per core.
- **Wi-Fi boot, same image:** 104 KB free at boot. `esp_bt_mem_release(ESP_BT_MODE_BTDM)` gives back 44 KB (148 KB free); it is one-way until a reboot, hence restart-to-switch. Wi-Fi station start with default buffers, AMPDU and IRAM options: -45 KB, 102.7 KB free (min 99 KB). Not joined to a network yet; traffic will use more (dynamic buffers).
- **Static cost of radio code with no radio running:** ~148 KB free after the release, against 187 KB for the trimmed normal firmware.
- **Size trims, normal firmware** (trial A): unused LVGL widgets, themes and observer off, `LOG_DEFAULT_LEVEL_NONE`, `ESP_ERR_TO_NAME_LOOKUP` off, newlib nano printf, silent assertions. Image 837 to 664 KB (-21%), IRAM 68 to 61.6 KB, heap free at boot 179 to 187 KB. LVGL's examples and demos must be off with the widgets, or the build fails. With `-Os` too (`CONFIG_COMPILER_OPTIMIZATION_SIZE`): 562 KB (-33%), IRAM 58 KB, slides 24.4 fps against 24.5 (`fenix bench 15 nostream`): no loss, because the slide is SPI-bound.
- **Largest flash users (trial A):** LVGL 263 KB; fenix_ui 110 KB, of which fonts and icons are ~103 KB. The biggest are LVGL's Montserrat 14 (13.6 KB), `fx_icon_fill_40` (11.4 KB) and `fx_font_title_l` (10 KB). Montserrat 14 is linked only as LVGL's default font; no fenixOS code uses it. Then LovyanGFX 37 KB, newlib nano 29 KB, LittleFS 24 KB. Corrected: a P30 note had `esp_app_format` at 47 KB; it is 384 B.
- **Frame-rate caps do not free RAM:** the draw bands are allocated once at their size; a lower frame rate saves CPU time, not memory.
- **Applied in P31 (v0.1.7):** the trims, `-Os` and UNSCII 8 as LVGL's unused default font (Montserrat 14 out). Image 837 to 549 KB, heap free at boot 179 to 187 KB, about 192 KB with the stream off (stream buffers now exist only while streaming). Under install + stream + Task manager load the stacks keep `link` 992 B and `main` 2064 B free, so they stay 8 KB. CPU: left as is. The link task already blocks on the UART (20 ms timeout); a 100 Hz tick would round LVGL's 16 ms refresh to 10 ms steps; streaming costs ~1 fps, not worth a band copy to core 0; DFS waits for the battery (nothing to measure on USB).
- **Cores:** the UI (`main`) is on core 1. The link, Wi-Fi and Bluedroid tasks are on core 0; Bluedroid is pinned to core 0 by default. Idle load is 0.4 to 2% per core. Not CPU-bound: a full 320 × 240 frame takes ~31 ms on the 40 MHz SPI bus.

## Radio manager (P32, 2026-10-30)

Corrected (P38): Bluetooth is gone; the radio is Wi-Fi or off. Bluetooth numbers below are history.

The normal firmware now carries both radios; Settings > Wireless (`Settings.radio` in NVS: 0 off, 1 Bluetooth, 2 Wi-Fi) picks the one that starts at boot. Switching saves the mode and restarts, because `esp_bt_mem_release` cannot be undone without a reboot.

- **Image:** 1.38 MB (69% of the 2 MB slot), IRAM 130 of 172 KB. Up from 549 KB in v0.1.7: Bluedroid, Wi-Fi, lwIP.
- **Heap at boot:** off 158 KB free (min 118.6 KB); Bluetooth 48.5 KB (SPP server "fenixOS" and BLE advertising up; 23 to 24 KB since P33, see "Bluetooth link"); Wi-Fi 110 to 116 KB (station, scanning); joined to a network (Windows hotspot, -40 dBm) 108.7 KB, min 105.9 KB. The saved network is rejoined within ~8 s of boot. Corrected: the P30 figure of ~148 KB with no radio running was for the probe; the P32 build with the P31 trims leaves 158 KB.
- **Wi-Fi:** station with power save off (`WIFI_PS_NONE`) for full speed; network in NVS (`wifi_ssid`, `wifi_pass`, plaintext by user choice) and `WIFI_STORAGE_RAM` so the Wi-Fi driver keeps no second copy. Reconnect backoff 1, 2, 4... up to 30 s; after 5 failures the state shows "failed" and retries continue at 30 s. Scans are asynchronous; results are merged per SSID (strongest kept), sorted by signal, at most 24.
- **Event task stack:** Wi-Fi events build PC frames. With a ~1.2 KB buffer on the stack the default 2304 B overflowed after a scan (panic, reset reason 4); 4096 left 744 B; 5120 then. Corrected (P37): frames are built in static buffers since P34, so the task uses ~1.7 KB after scans: 3072.
- **PC messages:** RADIO_STATE after every HELLO and on each change, WIFI_SCAN only when asked. Nothing is sent while no PC is connected.
- **Frames from small stacks (P34):** `build_frame` kept a 1200 B payload buffer on the caller's stack. The SNTP callback (lwIP task, 3 KB stack) logged through it and panicked the board (reset reason 4) at the first time sync. Now `link.cpp` frames into a static buffer under the TX lock (`build_frame_in`), so any task can send; callbacks on lwIP still do not log.

## Wi-Fi link, mDNS, SNTP (P34, 2026-10-30)

Measured with the board on a Windows Mobile Hotspot (2.4 GHz, -39 dBm), PC on the same hotspot.

- **TCP link:** port 7350, one client, `TCP_NODELAY`, 500 ms send timeout (a stalled frame ends the connection rather than leaving half a frame). Since P37 frames go into a 6 KB send ring that task `tcp_tx` (2.5 KB stack) drains into the socket; a frame that finds no room for 500 ms ends the connection. `fenix --port tcp:fenixos.local ...` works for every link command; a USB session still open answers the TCP HELLO with ERROR busy until it times out (3 s).
- **Stream over TCP** (`fenix bench 8`, P37): 46 to 54 KB/s, 0 dropped, 0 RX errors; board fps 20 to 22 while streaming (USB 23). Corrected (P37): P34 measured 47.3 KB/s with board fps 14.4, because the UI task blocked in `send()` while `tcp_tx_free` reported a fixed 8 KB. The send ring reports its real room, so the stream skips a band instead of waiting.
- **Heap under a Wi-Fi stream (P37):** with the ring the UI stopped waiting, and the first stream frame took the heap to 0.5 KB; LVGL's failed malloc then hung the UI in its default `while(1)` assert (core 1 at 100%, no events, stream never stopped). Consumers and caps: each small `send()` held a whole-MSS pbuf (1.5 KB, 16 queued; `TCP_OVERSIZE` now quarter MSS), Wi-Fi dynamic buffers (32 each way, now 10), LVGL's opacity layer (24 KB, now 8 KB), the stream encoder (5.8 KB, only while streaming). Static Wi-Fi RX buffers 10 to 8. An LVGL assert now aborts, so the board restarts instead of freezing (`board/CMakeLists.txt`). Minimum over three 8 s Wi-Fi streams: 10.8 to 16.7 KB (the same bench over USB: 32 KB).
- **Heap, Wi-Fi joined, TCP server, mDNS, SNTP:** 80 to 90 KB free (108 KB before the services: 8 KB link RX buffer (2 KB since P40), 2.4 KB decoder, task stacks, mDNS). With weather, the web page and the send ring (P37): 63 KB free at idle.
- **mDNS:** `fenixos.local` resolves on Windows 11 (`Resolve-DnsName`, getaddrinfo) within a few seconds of the board joining; service `_fenixos._tcp`. No UPnP: nothing is opened on the router; the link is reachable only from the same network.
- **SNTP:** synced in about 10 s after boot. One cold boot did not sync within 74 s while DNS through the hotspot worked; a second server (`time.cloudflare.com`, `LWIP_SNTP_MAX_SERVERS=2`) covers a pool address that does not answer. SNTP starts on each new address (`net_up`); before an address its DNS query fails and backs off.
- **Open-Meteo:** answers plain HTTP (`http://api.open-meteo.com/v1/forecast?...&current=temperature_2m,weather_code`, 200, no redirect, checked 2026-10-30), so weather needs no TLS.
- **Weather (P34):** task `weather` (3 KB stack since P37: it peaks at ~1.3 KB; core 0, priority 2) on the board: a plain-socket HTTP/1.0 GET (never chunked) to Open-Meteo every 15 min, 60 s after a failure, and at once on a new address or place. The answer (~400 B) is read into a 2 KB static buffer and parsed with `strstr`/`strtof`; no JSON library, no float printf (newlib nano: coordinates are printed from integers). Place in NVS (`wx_lat`, `wx_lon` × 10000, `wx_name`), set by WEATHER_SET from the PC (Settings > Weather, Open-Meteo geocoding on the PC over HTTPS). Home shows "Friday, 30 October · 25° Partly cloudy" (portrait: second line). Heap with weather running: ~75 KB free in Wi-Fi mode.
- **Phone page (P34):** `esp_http_server` on port 80 (`web.cpp`), 3 sockets, task on core 0 (3 KB stack). Cost (P37): `httpd_start` takes 7 KB of heap with a 4 KB stack; the handler peaks at 1.3 KB, so the stack is now 3 KB (~6 KB in all). 0.0% CPU when idle; each `/status.json` is one ~300 B reply. `http://fenixos.local/` (mDNS also announces `_http._tcp`) serves one 2.4 KB page in the design tokens that reads `/status.json` every 2 s: time, date, weather, version, uptime, PC link, free memory, apps, network, signal, address. Status only, because anyone on the network can open it without a password. Heap 67 KB free 10 s after boot with everything running (TCP link, mDNS, SNTP, weather, web server).
- **Board network list (P34):** Settings > Wireless in Wi-Fi mode lists the board's own scan (12 strongest, merged per SSID) with a check on the joined network, a lock on secured ones and signal bars (-60 / -75 dBm steps). A tap rejoins the saved network or joins an open one; a secured network asks for its password from the PC (no keyboard on the board, P36). The first visit scans by itself.
- **Remote taps over TCP from the CLI:** each `fenix --port tcp:... tap` opens a session just before its tap and ends it when the socket closes (USB keeps it 3 s). Corrected (P37): the lost taps were not the dim wake. CONNECTED rebuilds Settings, and the rebuilt screen had no layout until the next refresh, so a press read in between hit-tested rows still at 0,0 with no size and landed on the scroll area. `nav.cpp` now lays a screen out as soon as it is built, and a rebuild during a held press waits for the release. Every tap over TCP lands. A dimmed board still takes the first tap as a wake, over any link.
- **Heartbeat RTT** (`fenix soak 1`, max of 60 pings): TCP over the hotspot 138 ms, USB 11 ms. Fine for the link (timeout 3 s); Wi-Fi latency spikes come from the PC's hotspot side, the board has power save off.
- **STATS counters:** fixed in P34 to count frames from both transports (they counted the UART only, so a TCP session showed 0 frames).
- **Heap under stream + scans over TCP:** minimum 24 KB seen after several streamed screenshots and scans (free 64 KB). Watch this before adding more Wi-Fi services.

## Bluetooth link and BLE status (P33, 2026-10-31; removed in P38)

History: P38 removed Bluetooth from the firmware for the reasons below.

Measured with the board paired to this PC (Intel adapter, Windows 11). Pairing makes an outgoing Bluetooth COM port (here COM10, `BTHENUM\DEV_B4A64AA0BE56`, Microsoft's RFCOMM driver, not CH340).

- **SPP link:** `fenix --port COM10 hello|sysinfo|time|list|install` work. Sample app install 1643 B in 0.15 s. Heartbeat RTT max 182 ms (`fenix soak`).
- **Opening the COM port is slow and unreliable:** each open makes Windows page the board and set up RFCOMM. Opens took 2 to 46 s, and many failed after ~40 s with "The semaphore timeout period has expired" or "The network location cannot be reached". A reopen within a few seconds of a close is fast (2 s). The CLI waits only 6 s for a board, so a cold run often says "no board"; run it again. The app keeps its port open, so it pays this once. No failed malloc on the board during these attempts.
- **Stream refused over Bluetooth:** a stream over SPP gave ~1 KB/s and took the heap from ~21 KB to 7.9 KB. The board then panicked (reset reason 4). The board now answers STREAM_START over Bluetooth with `ERROR` 6 (unavailable), and its Stream screen says "Not over Bluetooth". Screenshots (`fenix shot`) stream too, so they need USB or Wi-Fi.
- **Two panics after a Bluetooth bench session** (reset reason 4, `fenix bench` with its taps between Home and Settings). One had the stream refused, so the stream is not the only cause. They did not repeat in a third run. No console and no core dump exist, so `link.cpp` now keeps the last failed malloc (size, function, free heap) in RTC memory. The next session logs "out of memory before this session: ...". Check that log first when a Bluetooth restart comes back.
- **RX buffer 6 KB:** the PC keeps 4 install chunks (4 KB) in flight; with framing that overflowed a 4 KB buffer. A full buffer blocks `BTC_TASK`, which also confirms the board's own writes, so its ACKs waited out their 500 ms and were lost.
- **Heap in Bluetooth mode:** 23 to 24 KB free at idle with the BLE service and the 6 KB RX buffer; 19 KB with an SPP session; minimum seen 14 to 16 KB. Corrected: P32's 48.5 KB was before the remote link layer, the BLE service and the P37 caps were in.
- **BLE status service** (`ble.cpp`): found by `bleak` as "fenixOS" (`B4:A6:4A:A0:BE:56`), MTU 517, the status JSON (98 B without Wi-Fi fields) read in one go and notified every 2 s. 25.4 KB free with a BLE client.

## Bluetooth removed (P38, 2026-11-01)

Same board, Wi-Fi joined to the hotspot, measured before (v0.1.10) and after.

| | v0.1.10 | P38 |
|---|---|---|
| Image | 1,526,331 B | 1,115,815 B (-27%) |
| IRAM used | 131,231 B (FreeRTOS, heap, ringbuf code in flash; SRAM1 as IRAM) | 111,827 of 131,072 B (IDF defaults again) |
| Static DRAM | 62,280 B | 50,844 B |
| Heap at boot, Wi-Fi | 63.9 KB (min 59.5) | 71.8 KB (min 67.4), of 281 KB |
| Heap at boot, radio off | 147.5 KB | 154.9 KB (largest block 94 to 78 KB: the IRAM move changes the layout) |
| `fenix bench 8 nostream` (USB) | 24.3 fps | 25.5 fps |
| Stream over Wi-Fi (`bench 8`) | 46-54 KB/s, board 20-22 fps, heap min 10.8-16.7 KB | 50.8 KB/s, board 24.7 fps, heap min 17.2 KB |
| Stream over USB (`bench 8`) | 23 fps | 24.2 fps |

- Gone: `ble.cpp`, the SPP code in `radio.cpp`, Bluedroid, the BT controller and coexistence (`ESP_COEX_SW_COEXIST_ENABLE`), tasks `BTC_TASK`, `BTU_TASK`, `hciT`, `btController`. `esp_timer` stack 3072 to 2560 (1840 B free after the benches).
- A saved radio mode 1 boots as Wi-Fi when a network is saved, else off, and is saved back. RADIO_SET 1 does nothing.

## Link buffers trimmed (P40, 2026-11-01)

Peaks measured with temporary counters (removed after), 32 KB package installs and 15-20 s `fenix bench` streams, over USB and Wi-Fi:

| Buffer | Size before | Peak | Now |
|---|---|---|---|
| UART RX (`RX_BUF`) | 8 KB | 2.1 KB in an install; the PC keeps at most 4 KB of chunks unacked | 5 KB |
| Wi-Fi link RX (stream buffer fed by `tcp_link`) | 8 KB | small; when full, `tcp_link` waits and TCP holds the rest | 2 KB |
| UART TX (`TX_BUF`, the USB stream ring) | 8 KB | full while streaming | 8 KB: a stream band needs up to ~4.6 KB of room |
| TCP send ring (`TX_RING`) | 6 KB | full while streaming | 6 KB |
| `tcp_link` stack | 3072 B | 1960 B free | 2560 B |

Result, Wi-Fi mode: heap at boot 71.8 to 81.8 KB; install of 32 KB 0.48 s over USB, 0.41-0.55 s over Wi-Fi (unchanged); stream fps USB 25.0, Wi-Fi 25.8; lowest heap in a Wi-Fi stream 15.8 to 23.9 KB; 0 RX errors. Kept: `link` 8 KB stack (1984 B free), `tcp_tx` 2560 (1728 B free), mDNS 4 KB (1560 B free).

## App packages read in pieces (P39, 2026-11-01)

Before P39 `store_load` malloc'd the whole package (up to 64 KB) to check an install, draw tiles and open an app. Now `fpkg::Package` parses from a `FileSource` into its own arena (strings with a NUL each, element props) and keeps only asset offsets; `app_pkg.cpp` reads the tile icons and the shown images by offset. Details: `docs/packages.md` "Board memory (P39)".

Measured in Wi-Fi mode with a 32 KB worst-case package (110x110 RGB565 image, 7 KB text):

| | Before | After |
|---|---|---|
| Lowest heap during install | 30.7 KB | 51.3 KB |
| Free with the app open | 32.9 KB | 40.0 KB |
| Lowest heap with the app open | 21.7 KB | 26.3 KB |

The 64 KB package cap is not reachable in practice: the image (24 KB) and string (8 KB) caps bound a package near 33 KB plus tile icons.

**Use after free on app close (found P45, 2026-11-01).** App labels show the package's strings in place (`lv_label_set_text_static`) and images point at the package's descriptors. `close_app` freed the package as soon as the app left the top of the stack, but the app screen still draws for its 250 ms slide out, so it read freed memory: on the board, garbage text or a crash; in the simulator, a label that ran on through freed bytes and stalled `sim_shots.py` about once in 30 runs (after `app_portrait`, tap Home). Found with a temporary DbgHelp watchdog in the simulator. Fix: `close_app` moves the package, descriptors and pixels into `s_retired`, freed when the app screen is deleted (`LV_EVENT_DELETE`); 150 simulator runs, no stall. `fpkg::Package::parse` also leaves the Package empty on failure (a failed parse could leave elements with null props). `sim_shots.py` now kills a run after 60 s.

## Image retention ("burn-in"), 2026-10-29

- **Seen:** after long sessions on Home, a faint ghost of the clock stays on the panel (user: "a moderate level of screen burn in right where the clock is").
- **What it is:** the ILI9341 panel is a TN LCD, not an OLED, so nothing wears out. A picture held for hours leaves a DC bias on the liquid crystal (image retention). It usually fades after some hours with the panel off or showing changing content.
- **Guard (P21, on by default):** the picture walks 1 px every 60 s over a 5 x 5 square (+-2 px; the flush offsets each push, LovyanGFX clips the edge, the uncovered strip is cleared, local touches are shifted back, the PC stream stays unshifted). After 10 min without a touch the backlight fades out and the panel sleeps (ILI9341 SLPIN: it stops driving the picture; its memory is kept); a tap wakes it and is swallowed, like the dim wake. Settings > Display shows the switch greyed out and locked on; Settings > Debug unlocks it, and turning Debug off locks it on again.
