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
| CH340 Windows driver (PC side) | 3.3.2011.11 (2011, installed `ch341ser.inf`); the vendor copy in `docs/vendor/CH340/` is older still, 3.1.2009.06 | 3.9.2024.9 per the Microsoft Update Catalog and oemdrivers.com (corrected 2026-10-29: WCH's own `CH341SER.EXE` now carries **4.0.2026.2**, dated 2026-02-11); WCH's own page renders by JS and was not read | web search |

v5.5.6 changes for the classic ESP32 are minor (no ESP32-specific fixes stood out in its notes; it has Wi-Fi supplicant security ports). ESP-IDF v5.5 is supported until January 2028. Moving to v6.x needs a check that LovyanGFX works on it (still unconfirmed; see `docs/POC-WRITEUP.md` §7.5). The 2011 CH340 driver works at 460800 baud, but it crashed Windows once (bugcheck 0x48, see below).

**Decision (user, 2026-10-29): stay on the current versions** (IDF v5.5.5, LovyanGFX 1.2.35, CH340 driver 3.3.2011.11).

Corrected (2026-10-29, after a PC crash): **update the CH340 driver** to WCH's current CH341SER. **Done 2026-10-29 16:22:** `CH341SER.EXE` from `https://www.wch-ic.com/download/file?id=65` (799,096 B, SHA-256 `458c37bd...12cad1`, Authenticode valid, signed by Nanjing Qinheng Microelectronics) staged driver **4.0.2026.2** (`oem164.inf`), but COM9 kept the 2011 driver until `pnputil /delete-driver oem160.inf /uninstall` removed it. COM9 then runs 4.0.2026.2 and the board works (handshake at 921600 baud, 10 s bench with 0 RX errors). Driver 3.3.2011.11 (`CH341S64.SYS`, `oem160.inf`) blue-screened the PC at 15:30: bugcheck `0x48 CANCEL_STATE_IN_COMPLETED_IRP`, `PROCESS_NAME: fenix.exe`, `CH341S64+4120`. The stack is `NtReadFile` then `IopCancelAlertedRequest` then `IoCancelIrp`: a synchronous read on COM9 was cancelled after the driver had completed it without clearing its cancel routine. This is a driver race, not a hardware fault. Ending a process with a read pending triggers it: here an agent force-killed `fenix logs` while it held COM9. Until the driver is updated, do not force-kill a fenixOS program that has the port open, and quit the app before unplugging the board. If Windows keeps the old driver, remove it from an elevated shell (`pnputil /delete-driver oem160.inf /uninstall /force`), then install the new one. The app now closes the port before it exits (`Link::shutdown` on Quit and before an app update), so a normal Quit cancels no read. The full write-up stays local (`docs/tempcrashlogs/`, git-ignored).

Corrected (2026-10-29, second crash): **the driver update was not the fix.** Driver 4.0.2026.2 blue-screened the PC at 20:39 while the installed app (v0.1.0, `fenixos.exe`) used COM9: bugcheck `0xD1 DRIVER_IRQL_NOT_LESS_OR_EQUAL`, read of address `0x44` at DISPATCH_LEVEL, `CH341S64+48e9` (`cmp byte ptr [rdi+44h],0`), and `0x800` (our 2048-byte read buffer) on the stack. No USB disconnect was logged. **Cause, from the driver's code** (capstone disassembly of `CH341S64.SYS` 4.0.2026.2):
- The function at `+0x4740` starts a read. It takes the current read request from the device extension (`+0x3a0`) and the port's COMMTIMEOUTS (`+0xd8` interval, `+0xdc` multiplier, `+0xe0` constant).
- When a read timeout is set, it takes the cancel spin lock and re-reads the current request (`+0x48e2`). It then checks `irp->Cancel` (IRP offset `0x44`), and from `+0x4a61` it sets a cancel routine and arms the timeout timers (`KeSetTimer`).
- Another path (the data-arrival or timer DPC) can complete the request and clear that pointer in between, so the check reads through NULL. Crash 1 (`0x48`, a cancel on a completed request) is the same family: requests that wait with timers and a cancel routine.
- Our link used serialport's timeouts: interval and multiplier `MAXDWORD`, constant 15 ms ("return at once if bytes are there, else wait for the first byte"). That takes this path on every read, about 66 times a second.

**Fix (fenix-link, P16):** COMMTIMEOUTS `MAXDWORD, 0, 0` set with `SetCommTimeouts` (`no_wait_reads`). In that mode the driver completes every read at once (`+0x486d` to `IofCompleteRequest`), with no timer, no cancel routine and no request left pending. The link sleeps 2 ms (bytes flowing) or 10 ms (idle) itself when a read finds nothing. Measured: stream bench 39.9–43.6 KB/s (41.8 with no sleep at all, so the stream is limited by the board), CLI 1.2 % of one core while streaming, app 0.5 %. **Until a release with this fix is installed, do not run fenixOS v0.1.0 with the board plugged in.** espflash (app install / probe) still opens the port with its own timeouts, only while flashing. Gaps (checked 2026-10-30): espflash 4.6.0 takes serialport's `COMPort` type directly and calls `set_timeout` (interval and multiplier `MAXDWORD` plus a constant, the crash-2 pattern) around every command, so the app's firmware update and probe use the risky path for the length of the flash. esptool (`idf.py flash`, pyserial) sets a plain total timeout (`0, 0, constant`), which also arms the driver's timers. Any other serial tool (monitors, terminals) is outside our control. Nothing has crashed while flashing so far; both crashes came from hours of the link's timed reads.

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
- **Unplug/replug recovery (2026-10-28, measured):** `tools/replug_watch.ps1` logs when COM9 appears or disappears (50 ms polling), and `fenix demo --wait` logs session events with timestamps. Across 3 physical replugs by the user, port reappearance → reconnected took **1311 / 1347 / 1336 ms**, including board boot (`boot reason 1, up 0 s`), handshake and the 1 s rescan. Unplug was detected within 30 ms of the port disappearing (`read: Access is denied (os error 5)`). The target was < 3 s.
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
- **Link task:** UART0 at 921600 with a 4 KB RX and a 16 KB TX buffer. The TX buffer is also the stream ring: `stream_rect` checks `uart_get_tx_buffer_free_size` and drops a band (marking it stale) instead of blocking the UI.
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
- **Bands:** fewer, taller bands mean fewer passes over the object tree per frame. 80 lines = 3 bands in landscape, 4 in portrait.
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

## Image retention ("burn-in"), 2026-10-29

- **Seen:** after long sessions on Home, a faint ghost of the clock stays on the panel (user: "a moderate level of screen burn in right where the clock is").
- **What it is:** the ILI9341 panel is a TN LCD, not an OLED, so nothing wears out. A picture held for hours leaves a DC bias on the liquid crystal (image retention). It usually fades after some hours with the panel off or showing changing content.
- **Guard (P21, on by default):** the picture walks 1 px every 60 s over a 5 x 5 square (+-2 px; the flush offsets each push, LovyanGFX clips the edge, the uncovered strip is cleared, local touches are shifted back, the PC stream stays unshifted). After 10 min without a touch the backlight fades out and the panel sleeps (ILI9341 SLPIN: it stops driving the picture; its memory is kept); a tap wakes it and is swallowed, like the dim wake. Settings > Display shows the switch greyed out and locked on; Settings > Debug unlocks it, and turning Debug off locks it on again.
