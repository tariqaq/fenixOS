# FenixDeck PoC write-up: everything learned

> History, frozen: the FenixDeck PoC, not current fenixOS facts. Current: `docs/board-info.md`, `docs/PLAN.md`, `proto/spec.md`.

Written 2026-10-29 at the end of the FenixDeck proof of concept (v0.1, 2026-10-23 to 2026-10-28).

This is the hand-over document for the next project: a new repo that builds "an OS of sorts" for the same board. It is meant to be read on its own, without the FenixDeck code next to it. Every number here was measured on the real hardware or checked against a named source, unless it is marked as an estimate or as unverified.

**The toolchain will change.** FenixDeck used C++ on ESP-IDF 5.5.5 with LovyanGFX on the board, and Rust + Tauri 2 + Svelte 5 on the PC. Those choices were right for a PC-monitor PoC, not necessarily for an OS. Section 7 lists every option that was considered or that exists. Hardware facts (sections 2–4) and lessons (section 12) hold whatever toolchain is picked.

## Contents

1. [What the PoC was, and the result](#1-what-the-poc-was-and-the-result)
2. [The board: verified hardware facts](#2-the-board-verified-hardware-facts)
3. [Vendor material, stock firmware, recovery](#3-vendor-material-stock-firmware-recovery)
4. [Design constraints that come from the hardware](#4-design-constraints-that-come-from-the-hardware)
5. [Decisions asked and answered (all options)](#5-decisions-asked-and-answered-all-options)
6. [Toolchain used (replaceable)](#6-toolchain-used-replaceable)
7. [Option catalogue for the next project](#7-option-catalogue-for-the-next-project)
8. [Wire protocol v1](#8-wire-protocol-v1)
9. [Board firmware as built](#9-board-firmware-as-built)
10. [PC side as built](#10-pc-side-as-built)
11. [Measurements](#11-measurements)
12. [Gotchas and lessons](#12-gotchas-and-lessons)
13. [Corrections log](#13-corrections-log)
14. [Not done, open questions, advice for the OS project](#14-not-done-open-questions-advice-for-the-os-project)
15. [Transfer kit: what to take to the new repo](#15-transfer-kit-what-to-take-to-the-new-repo)
16. [Sources](#16-sources)
17. [Glossary](#17-glossary)
18. [Verification record](#18-verification-record)

---

## 1. What the PoC was, and the result

### Goal

A PC hardware monitor on a cheap ESP32 touch display, with all the work on the PC:

- **PC app** (Rust, Tauri 2 UI) collects all stats and does all processing. It pushes compact binary data at a configurable poll rate (default 1 s).
- **Board firmware** stays tiny and efficient. It only stays connected, receives data and draws it at native resolution, with a capped frame rate and as little tearing as the hardware allows.
- Keep both sides lightweight: low CPU and RAM on the laptop, low heap and flash on the board.
- No SD card (the user has none).

### Result

All 9 phases (0–8) are done and verified on the hardware.

- The board shows 5 pages (Overview, CPU, GPU, Memory, Network), each in landscape and portrait. Only changed widgets are redrawn, by DMA.
- The PC app sits in the tray, finds the board by USB ID, reconnects after unplug in about 1.3 s, and pushes stats, config and a logo.
- Touch works: a tap cycles pages on the board, and every touch event (down, up, tap, long) also goes to the PC.
- Stats were validated against independent sources: 19 checks pass, 0 fail.
- Footprints: firmware 334 KB, board heap ~159 KB free, PC app 0.07 % CPU / 29 MB in the tray.

Screenshots of the live board (exact LCD read-back): `docs/legacy/screens/v01-live-overview.png`, `docs/legacy/screens/v01-live-cpu.png`.

### Timeline

| Date | Step |
|---|---|
| 2026-10-23 | Board bought on Pinduoduo (two listings, see `pics/ANALYSIS.md`) |
| 2026-10-28 | Repo set up, photos and store screenshots analysed, vendor package downloaded |
| 2026-10-28 | CH340 driver installed, chip read with esptool, stock flash backed up (nothing written) |
| 2026-10-28 | Design discussion, 3 batches of decisions (section 5), plan written |
| 2026-10-28 | Phases 0–8 built, flashed, measured; follow-up fixes (network 2x, footprint, replug timing) |
| 2026-10-28 | Stale-content sweep of all docs and comments |
| 2026-10-29 | This write-up |

---

## 2. The board: verified hardware facts

### Identity

| Item | Value | How we know |
|---|---|---|
| Board | **ESP32-2432S028R**, "Cheap Yellow Display" (CYD), PCB **V1.4**, dual USB (micro-USB + USB-C) | silkscreen, box label, listing |
| Maker | Guition (logo on PCB), sold by SCVSTEM and a flagship store | PCB, listings |
| Box SKU | `10101001 (2641)` | box label photo |
| Module | Shield marked **"ESP-32S"** (WROOM-32-class module, PCB antenna). The listing art labels it ESP-WROOM-32 | PCB photo `20261028_190048.jpg`, listing |
| Case | Clear acrylic, with cutouts for P4, RST/BOOT, P3, CN1 | photos |
| Bundle | Board, acrylic case, USB cable, 4-pin JST 1.25 cable, stylus | listing B |

### Chip (read with `esptool v5.6.0`)

| Item | Value |
|---|---|
| Chip | **ESP32-D0WD-V3, revision v3.1** (the listing says D0WDQ6, which is outdated) |
| Cores | **2 × Xtensa LX6, 240 MHz** (the vendor manual's "single core" is wrong) |
| ULP coprocessor | Yes ("LP Core" in esptool). Runs during deep sleep and uses the 8 KB RTC slow memory. On this chip it is the ULP-FSM, programmed in assembly (the C-programmable ULP-RISC-V exists only on newer chips such as S2/S3) |
| Crystal | 40 MHz |
| Wireless | Wi-Fi 2.4 GHz b/g/n, Bluetooth Classic + BLE 4.2, on-module PCB antenna |
| MAC | `b4:a6:4a:a0:be:54` |
| SRAM | 520 KB, plus 16 KB RTC SRAM (8 KB fast, 8 KB slow); 448 KB ROM (Espressif ESP32 datasheet) |
| PSRAM | **None** |
| Flash | **4 MB**, JEDEC manufacturer `0xc4`, device `0x6016`, 3.3 V |

What the crystal and ULP core are for (from the design discussion): the crystal is the reference clock that the PLL multiplies to 240 MHz CPU / 80 MHz APB, and it keeps Wi-Fi/BT on frequency and UART baud rates accurate. The ULP core can watch pins, the ADC (for example the light sensor on IO34) and simple sensors while the main cores sleep, then wake them. On this board it matters little: the regulators, CH340 and backlight keep drawing power, and there is no battery support.

### Display

| Item | Value |
|---|---|
| Panel | 2.8" TFT, **240 × 320** native (portrait), 65K colours (RGB565), viewable area 43.2 × 57.6 mm, viewing angle > 60° |
| Controller | **ILI9341V** (panel spec `JC2432A028N.pdf`). The "maybe ST7789" worry from community posts does not apply to this panel |
| Bus | SPI2 / HSPI: SCK **14**, MOSI **13**, MISO **12**, CS **15**, DC **2**, RST **-1** (tied to EN) |
| Write clock | **55 MHz** works (vendor default and our setting). Corrected 2026-10-29: the ESP32 SPI clock is 80 MHz / (n + 1), so 55 MHz really ran at 40 MHz |
| Read-back | **Works** (MISO wired). Read clock 20 MHz, `dummy_read_pixel = 8`, `dummy_read_bits = 1`, `readable = true` |
| Colour settings | `invert = false`, `rgb_order = false` give correct colours and orientation (verified) |
| TE (tearing effect) pin | **Not wired** on the 18-pin LCM connector. No hardware vsync |
| Backlight | **GPIO21**, through an AO3402 MOSFET, active HIGH, PWM works (we use 44.1 kHz on LEDC channel 7) |

A full frame is 240 × 320 × 2 = 153,600 B, which takes about 22 ms at 55 MHz (bandwidth-limited theoretical figure). Corrected 2026-10-29: the ESP32 SPI clock is 80 MHz / (n + 1), so a 55 MHz request runs at 40 MHz, about 31 ms a frame.

### Touch

| Item | Value |
|---|---|
| Type | **Resistive**. Stylus recommended; a finger works poorly |
| Controller | **XPT2046** (U3, TSSOP-16 next to BOOT) |
| Bus | **Its own SPI bus**, SPI3 / VSPI: CLK **25**, MOSI **32**, MISO **39**, CS **33**, IRQ **36** |
| Clock | 2.5 MHz |
| Default raw range | x 300–3900, y 200–3900 (works without calibration, verified with the stylus; NVS on the board has no stored calibration, checked 2026-10-29). The vendor LovyanGFX demo's 0–239 / 0–319 is wrong (pixel sizes used as raw values) |

Because touch is on a separate bus, TFT_eSPI's built-in touch (`TOUCH_CS`) does not work on this board. LovyanGFX `Touch_XPT2046` on `SPI3_HOST`, or the `XPT2046_Touchscreen` library on a second SPIClass, does.

### USB, power, other parts

| Item | Value |
|---|---|
| USB-UART | **CH340C** (U6, SOP-16 by the USB ports), `USB\VID_1A86&PID_7523`. Both USB ports can flash |
| Auto-reset | DTR/RTS drive T1/T2 (S9013) into EN and IO0, the classic ESP32 auto-download circuit |
| Power | 5 V, ~115 mA (listing). Two AMS1117-3.3 LDOs: one for the ESP32, one for the TFT |
| USB-C note | The manual says to use a USB-A to USB-C cable, not C-to-C. The USB-C port likely has no CC resistors |
| Battery | **Not supported**. The 1.25 mm port P1 is UART, not a battery input |
| microSD | Slot on VSPI pins: CS 5, MOSI 23, CLK 18, MISO 19 (not used; no card) |
| RGB LED | Common anode, **active LOW**: IO4, IO16, IO17 |
| Light sensor | LDR GT36516 on **IO34** (ADC, input only) |
| Speaker amp | SC8002B (datasheet in the package is FM8002A, an equivalent) on **IO26** (DAC1). Speaker not included; P4 is the speaker header |
| BOOT button | IO0 |
| U4 | Empty footprint for a 25Q32 external flash. Not a missing part: the module has its own 4 MB |
| Size / weight | 50 × 86 mm, 50 g; operating -20 to 70 °C, storage -30 to 80 °C (listing spec table) |

### Connectors and free GPIO

| Connector | Pins |
|---|---|
| P1 (JST 1.25, 4-pin) | VIN, TX, RX, GND (UART0, shared with USB) |
| P3 (JST 1.25, 4-pin) | GND, IO35, IO22, **IO21 (backlight, do not use)**. Listing: "GPIO extended IO" |
| CN1 (JST 1.25, 4-pin) | GND, IO22, IO27, 3.3 V. The listing calls it the "temperature and humidity interface" (DHT11 port; DHT11 datasheet is in the vendor package) |
| P4 | Speaker |

**Free GPIO for projects: IO22, IO27, IO35 (input only).** That is all. Any expansion (sensors, buttons, I²C devices) must fit on these three, or reuse the SD-card pins when no card is used.

### Complete pin map

| GPIO | Use |
|---|---|
| 0 | BOOT button (strapping) |
| 1 / 3 | UART0 TX / RX (USB via CH340, and P1) |
| 2 | LCD DC (strapping) |
| 4, 16, 17 | RGB LED R/G/B (active LOW) |
| 5, 18, 19, 23 | microSD CS, CLK, MISO, MOSI |
| 12 | LCD MISO. Strapping pin (MTDI, internal pull-down): high at reset selects 1.8 V flash and breaks boot/flashing on 3.3 V flash. esptool reads 3.3 V and the board boots normally; never add a pull-up |
| 13, 14, 15 | LCD MOSI, SCK, CS |
| 21 | Backlight PWM |
| 22, 27 | Free (P3 / CN1) |
| 25, 32, 33, 39 | Touch CLK, MOSI, CS, MISO |
| 26 | Speaker amp (DAC) |
| 34 | Light sensor (ADC, input only) |
| 35 | Free, input only (P3) |
| 36 | Touch IRQ (input only) |
| EN | Reset; also LCD reset |

### Schematic caveat

The vendor schematic is `V0.1` (2022), drawn for the original micro-USB-only board. This board is `V1.4`: USB-C added and CN1 changed to GND/IO22/IO27/3.3V. The core pin map matches the V1.4 silkscreen and the vendor LovyanGFX demo, and every pin used by FenixDeck was proven on the hardware. Treat the schematic as close, not exact.

### Host PC (where the PoC ran)

Windows 11 Pro, Intel Core i7-13620H (16 threads), Intel UHD + NVIDIA GeForce RTX 4060 Laptop GPU, 32 GB RAM. The board was on `COM9`; the number can change with another USB port.

---

## 3. Vendor material, stock firmware, recovery

### Vendor download site

- Site: `https://pan.jczn1688.com/1#/` (GUITION-DISK; the link is printed on the board manual).
- It is slow from Malaysia (Chinese servers). Files come in shards that the browser merges, so download through a real browser (Chrome worked).
- Direct-link pattern: `https://pan.jczn1688.com/directlink/1/<server path>?lang=en`.
- Downloaded: `ESP32 module/2.8inch_ESP32-2432S028R.rar` (252,636,403 B), `TOOL/USB-SERIAL CH340.rar` (250,569 B), and `Bare screen/JC2432A028N.pdf` (panel spec; also inside the board package).

### Package contents (`docs/vendor/ESP32-2432S028R/2.8inch_ESP32-2432S028R/`)

The whole package is **local only** (not in git since 2026-10-29, when the fenixOS repo went public: the vendor files carry no redistribution licence). Re-download it from the vendor site above.

| Folder | Content |
|---|---|
| `1-Demo/Demo_Arduino/` | Arduino sketches: 3_x basics (LED, TFT, JPG/PNG, LVGL, key, interrupt, DHT11, PWM, timer, ADC), 4_x Wi-Fi, 5_x BLE, 6_x SD, 7_x touch (incl. a LovyanGFX touch demo), 8_x buzzer |
| `1-Demo/Demo_LVGL(idf-4.3)/` | LVGL demo for ESP-IDF 4.3 (the stock firmware), 482 MB |
| `2-Specification/` | Board spec PDF |
| `3-Structure_Diagram/` | Dimensions |
| `4-Driver_IC_Data_Sheet/` | ESP32-WROOM-32, 25Q32, FM8002A amp, JC2432A028N panel (ILI9341V), DHT11 |
| `5-Schematic/` | Two schematic sheets (JPG), WROOM pin definition. File names are swapped relative to content: `...-MCU.jpg` is power/USB/peripherals, `...-LCM.jpg` is ESP32/touch/LCM/audio |
| `6-User_Manual/` | "Getting started 2.8 Inch" PDF |
| `7-Character&Picture_Molding_Tool/` | Third-party Windows tools |
| `8-Burn operation/` | Prebuilt `LVGL-DEMO2.8R.bin` and flashing steps; Espressif flash tool 3.9.3 |

The vendor `libraries/` folder (346 MB) has copies of TFT_eSPI, LovyanGFX, lvgl, TJpg_Decoder, ESPAsyncWebServer, ESP8266Audio and more.

### Vendor config errors

- The vendor TFT_eSPI `User_Setup.h` sets `TFT_RST 12`. **Wrong**: IO12 is LCD MISO and reset is tied to EN. Use `TFT_MISO 12`, `TFT_RST -1`.
- The vendor TFT_eSPI setup uses `ILI9341_2_DRIVER`, 55 MHz, backlight 21 active HIGH, touch CS 33. Its built-in touch cannot work (separate bus).
- The vendor LovyanGFX demo has the pins right, but its touch range (`x_min/x_max` 0–239, `y_min/y_max` 0–319) is wrong: those are raw 12-bit XPT2046 values, and the demo never calibrates. Use x 300–3900, y 200–3900. Its touch `bus_shared = true` is not needed (separate bus).
- The V1.4 PCB resistors R15/R19 (`501`), R16/R17 (`101`) and R20 (`201`) do not match the V0.1 schematic values (1 MΩ LDR divider, 1 kΩ LED). The RGB LED and light-sensor circuits on V1.4 are unverified. R18 (10 kΩ, CN1/IO27 pull-up) matches. Details in `pics/ANALYSIS.md`.

### CH340 driver

The board enumerates with an error until the CH340 driver is installed. Use WCH's official `CH341SER.EXE` (`https://www.wch-ic.com/downloads/CH341SER_EXE.html`); the vendor copy is in `docs/vendor/CH340/` (local only). Install needs admin. Then the board appears as `USB-SERIAL CH340 (COMx)`.

### Stock firmware

| Item | Value |
|---|---|
| Backup | `firmware/stock/stock_flash_4MB.bin`, full 4 MB dump from offset 0 (local only, not in git) |
| SHA-256 | `db0256aad5a235cc19584cb6050421da27d08730e27da49347e0c52bdef75c22` (verified on the board with `esptool verify-flash`) |
| App | `lvgl-demo` v1, built `Nov 1 2025 11:46:16`, ESP-IDF v4.3 (the vendor LVGL demo) |
| Partitions | nvs 0x9000 24K, phy_init 0xF000 4K, factory app 0x10000 **1024K** |
| Used | Data ends near 0xFBE00; the rest of the flash is 0xFF |

Restore (overwrites the whole flash):

```
python -m esptool --port COM9 -b 460800 write-flash 0 firmware/stock/stock_flash_4MB.bin
```

### Flashing and download mode

- Auto-download through DTR/RTS works with `esptool` and `idf.py flash` at 460800 baud.
- Manual download mode, if auto-reset ever fails: hold BOOT, press and release RESET, release BOOT. The listing says this is needed for every flash; on this board auto-reset worked every time, so it is only a fallback.
- **921600 baud gave `Corrupt data` on a bulk flash read.** 460800 is reliable for both flashing and the app link.

---

## 4. Design constraints that come from the hardware

These shaped every design decision and will shape the OS too.

1. **No hardware vsync.** The TE pin is not wired, so drawing cannot be synced to the panel refresh. What works instead:
   - Draw each widget into an off-screen buffer in RAM, then DMA-push only that rectangle. No flicker, and tearing is confined to small regions that change briefly. In practice it was invisible.
   - Redraw only what changed, and cap the frame rate.
   - Full-screen redraws only on page or rotation changes.
2. **No PSRAM.** A full 16-bit framebuffer (150 KB) would take more than half the free heap. Use partial buffers: per-widget sprites (FenixDeck) or LVGL-style partial render buffers. FenixDeck used 2 × 40 KB and still had ~159 KB free.
3. **UART0 is both the USB link and the boot/log console.**
   - The firmware must turn the console off on UART0 (`CONFIG_ESP_CONSOLE_NONE`) and keep logs off it (bootloader log level none). Debug text goes inside protocol `LOG` frames.
   - The ROM bootloader still prints at 115200 baud after a reset. The PC side must drop bytes until a valid frame arrives. COBS framing with CRC makes that easy.
4. **DTR/RTS are wired to reset.** Opening the port with the wrong DTR/RTS state resets the board or puts it into download mode. The PC opens the port with **DTR = false, RTS = false**. Verified: opening the port does not reset the board.
5. **Link speed: 460800 baud** (~46 KB/s). The CH340C datasheet allows up to 2 Mbps; a distributor spec table gives 460,800 bps as its continuous rate with flow control, which matches what proved reliable here. Plenty for data: a stats frame is about 60–200 B, and a 48 × 48 icon is 4.6 KB. Too slow for streaming full frames (150 KB → 3 s per frame), so the board must render.
6. **Touch has its own SPI bus** (see section 2). Poll it from the UI task; LovyanGFX checks the IRQ pin first, so an untouched poll costs no SPI traffic.
7. **4 MB flash.** The stock layout gives the app only 1 MB. FenixDeck used a 2 MB app partition and reserved 1.9 MB for a future LittleFS `storage` partition (fonts, icons). There is no room for two 2 MB OTA slots plus storage; OTA needs a layout change (section 14).
8. **Only 3 free GPIO** (IO22, IO27, IO35 input-only).
9. **Resistive touch**: pressure-based, a stylus works best, and calibration matters more than with capacitive panels.

---

## 5. Decisions asked and answered (all options)

The design was settled with three batches of picker questions (2026-10-28), followed by three later questions. Every option offered is listed, with the pick and how it turned out. "(Rec.)" marks the option recommended at the time.

### Batch 1

**Q1. How should the board connect to the PC app?**

| Option | Description | |
|---|---|---|
| USB serial (Rec.) | Same cable powers the board and carries data through the CH340, plug and play. Board must stay cabled | |
| Wi-Fi | WebSocket or UDP over home network. Board needs only power. More setup, more delay, drops on Wi-Fi blips | |
| **Both, USB first** | Build USB serial now; design the message format so Wi-Fi can be added later without changes | **picked** |

Outcome: USB serial at 460800. The protocol is transport-agnostic (COBS frames work over any byte stream). Wi-Fi was not built.

**Q2. What language should the board firmware be in?**

| Option | Description | |
|---|---|---|
| Rust no_std (Rec.) | esp-hal + embassy (async), no OS. Smallest and leanest, one language with the PC app. Needs the espup toolchain. Fewer ready display/touch drivers (ILI9341 and XPT2046 ones exist) | |
| Rust std (esp-idf) | Rust on top of ESP-IDF (FreeRTOS). Easier networking, bigger std library; larger binaries, slower builds | |
| **C++ (ESP-IDF/Arduino)** | C++ with LovyanGFX. Most mature display support for this board; vendor demos work as-is. Two languages in the project | **picked** |

Outcome: C++17 on ESP-IDF, LovyanGFX. It worked well; firmware 334 KB.

**Q3. Who draws the screen?**

| Option | Description | |
|---|---|---|
| Board draws widgets (Rec.) | PC sends small data packets; board draws gauges and text, redrawing only what changed. Tiny bandwidth, smooth | |
| PC renders pixels | PC draws the UI and streams image regions. Unlimited freedom, but a full frame is 150 KB: about 1–5 fps over serial | |
| **Hybrid** | Board draws widgets for live data; PC can also push small images (icons, album art) | **picked** |

Outcome: board-drawn widgets plus `IMAGE_*` messages into 2 slots of up to 64 × 64 RGB565. Slot 0 is the waiting-screen logo.

**Q4. Which screen orientation?**

| Option | Description | |
|---|---|---|
| Landscape 320 × 240 (Rec.) | Wide; good for side-by-side gauges | |
| Portrait 240 × 320 | Tall; good for stacked lists | |
| **Configurable from the PC** | Rotation set in the PC app and sent to the board; layouts must adapt | **picked** |

Outcome: rotations 0–3 (portrait, landscape, and their 180° flips). Each page has a landscape and a portrait layout.

### Batch 2

**Q5. C++ on which framework?**

| Option | Description | |
|---|---|---|
| **ESP-IDF + LovyanGFX (Rec.)** | Espressif's own framework (idf.py or PlatformIO). Leanest binaries, full control of DMA and SPI, FreeRTOS tasks | **picked** |
| Arduino + LovyanGFX | arduino-esp32 core via PlatformIO. Simpler, faster to write, slightly bigger binaries | |
| Arduino + LVGL | LVGL widgets (gauges, charts, animations) on LovyanGFX. Nicest widgets, more RAM and flash | |

**Q6. Which PC stats in the first version?** (multi-select)

| Option | Description | |
|---|---|---|
| CPU | Total and per-core usage, clock, package temperature | **picked** |
| GPU (NVIDIA) | Usage, temperature, VRAM, power (via NVML) | **picked** |
| RAM + disk | Memory used/total, disk usage and read/write speed | **picked** |
| Network + battery | Up/down speed, battery % and charging state | **picked** |

**Q7. Which frontend for the Tauri 2 PC app?**

| Option | Description | |
|---|---|---|
| **Svelte + TS (Rec.)** | Compiles to small vanilla JS, no runtime | **picked** (Svelte 5) |
| Vanilla TS | No framework; lightest, more manual DOM code | |
| SolidJS + TS | React-like, fine-grained reactivity, also light | |
| React + TS | Most familiar, heaviest | |

**Q8. Should the touchscreen do anything in v1?**

| Option | Description | |
|---|---|---|
| Not in v1 (Rec.) | Display only | |
| Basic: switch pages | Tap cycles stat pages on the board | **picked** |
| Send taps to PC | Board reports touch events; PC decides (start of a stream-deck setup) | **picked** |

The user answered "do both". Outcome: a tap cycles pages and every touch event goes to the PC.

### Batch 3

**Q9. Windows doesn't expose CPU temperature without a kernel driver. How to get it?**

| Option | Description | |
|---|---|---|
| **Via LibreHardwareMonitor (Rec.)** | User runs LHM (free, open source); the app reads its data. Accurate CPU/GPU/fan temps. Needs admin and its driver | **picked** |
| Skip CPU temp in v1 | Use only what Windows gives freely | |
| Try WMI thermal zone | Built in, but on most laptops a generic ACPI zone, often wrong or stuck | |

The user added: the app may ask for UAC elevation to get the data ("personal project"). Outcome: the app can launch LHM elevated and configures its web server. LHM now uses the PawnIO driver instead of WinRing0 (checked online at design time).

**Q10. How should the PC app run?** (multi-select)

| Option | Description | |
|---|---|---|
| System tray | Closing the window keeps it running and pushing data | **picked** |
| Start with Windows | Launch on login, minimised | not picked |
| Auto-connect board | Find the board by USB ID (CH340 1A86:7523) + handshake; reconnect on replug | **picked** |

**Q11. What should the board show when the PC app isn't connected?**

| Option | Description | |
|---|---|---|
| **Waiting screen (Rec.)** | Logo plus "waiting for PC"; backlight dims after N minutes | **picked** |
| Last data, greyed | Keep last values with a "disconnected" badge | |
| Backlight off | Dark until reconnect | |

**Q12. How to build the firmware?**

| Option | Description | |
|---|---|---|
| **ESP-IDF v5 idf.py (Rec.)** | Official tooling, newest IDF, LovyanGFX as a component, terminal and VS Code | **picked** |
| PlatformIO | One tool for toolchains and builds; ESP-IDF support often lags | |

### Later questions

| Question | Options | Answer |
|---|---|---|
| Replug test (physical unplug/replug, 2 cycles) | Done / Can't right now | Done, 2 replugs |
| Close-to-tray mode | **Destroy WebView (Rec.)**: tray ~29 MB, 1 process, 0.07 % CPU; graphs and touch log reset on reopen. Hide window: ~95 MB, 4 processes, state kept | Destroy WebView |
| Replug timing (3 cycles with a port watcher) | Done / Can't right now | Done, 3 replugs (1.31–1.35 s each) |

### Other decisions made along the way

| Topic | Decision |
|---|---|
| Default poll rate | 1 s, configurable 250 ms – 10 s |
| ESP-IDF version | v5.5.5, not v6.0: LovyanGFX on v6 could not be confirmed (still unconfirmed on 2026-10-29) |
| LovyanGFX delivery | git submodule (it is not in the ESP component registry) |
| Board UI fonts | LovyanGFX built-in fonts (FreeSans/FreeSansBold 9/12/18/24 pt, Font0, Font2), auto-fit per widget |
| PC settings storage | Plain JSON file, no Tauri store plugin |
| GPU sensors | From LHM when it runs (keeps NVML out of the app process); NVML fallback |

---

## 6. Toolchain used (replaceable)

**This is what the PoC used. The new project may change any of it.** The gotchas in section 12 say which problems belong to which tool, so they can be skipped if the tool is dropped.

| Tool | Version | Notes |
|---|---|---|
| ESP-IDF | v5.5.5 | Installed by EIM CLI 0.20.1 (`winget install Espressif.EIM-CLI`) at `C:\Espressif`, target esp32 only. EIM brings its own Python venv |
| xtensa-esp-elf GCC | esp-14.2.0_20260121 | from IDF tools |
| CMake / Ninja | 3.30.2 / 1.12.1 | from IDF tools (not on global PATH) |
| LovyanGFX | 1.2.35 | git submodule `board/components/LovyanGFX` |
| esptool | 5.6.0 | pip (system Python 3.14), for reads and backups |
| Rust | 1.99.1 | rustup, MSVC target |
| Tauri | 2 (CLI 2.12) | with `tauri-plugin-single-instance`, `tray-icon` feature |
| Node / pnpm | 24.13.1 / 12.15.0 | pnpm via `npm i -g pnpm` |
| Svelte / Vite / TS | 5.57 / 8.3 / 5.9 | |
| MSVC | VS 2022 Build Tools | only for the C++ protocol host test |
| WebView2 | 149 | Windows runtime used by Tauri |
| LibreHardwareMonitor | winget package | `winget install LibreHardwareMonitor.LibreHardwareMonitor` |

Rust crates on the PC: `serialport 4` (no default features), `sysinfo 0.39`, `nvml-wrapper 0.13`, `ureq 3`, `starship-battery 0.12`, `windows 0.62` (PDH, DXGI, SetupAPI, Shell), `serde`/`serde_json`, `png 0.18` (CLI).

Board config that is independent of the build tool and worth keeping (`board/sdkconfig.defaults`):

```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESP_CONSOLE_NONE=y          # UART0 is the PC link
CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y
```

Partition table (`board/partitions.csv`):

```
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x6000
phy_init, data, phy,      0xf000,   0x1000
factory,  app,  factory,  0x10000,  0x200000
storage,  data, littlefs, 0x210000, 0x1F0000
```

---

## 7. Option catalogue for the next project

Everything that can run on this board, grouped by layer, with what we know about each on **this** board (ESP32 classic, no PSRAM, 4 MB flash, ILI9341 + XPT2046 on separate SPI buses). "Proven here" means FenixDeck ran it on this board. Everything else is from documentation or community reports and is not yet proven on this board.

### 7.1 Language and framework (board)

| Option | What it is | Fit for this board | Notes |
|---|---|---|---|
| **C++ on ESP-IDF** | Espressif's official SDK: FreeRTOS, drivers, Wi-Fi/BT, NVS, partitions, OTA | **Proven here** | Leanest C/C++ path, full control of DMA/SPI/tasks. Best documented. What FenixDeck used |
| C++ on Arduino (arduino-esp32 core) | Arduino API on top of ESP-IDF | Vendor demos use it | Faster to write; more libraries (TFT_eSPI, XPT2046_Touchscreen); slightly bigger binaries; less control. Can also be used as an ESP-IDF component |
| Arduino via PlatformIO | Same core, PlatformIO build system | Works | Easy library management; its ESP-IDF/arduino versions often lag official releases |
| Rust no_std: esp-hal + Embassy | Bare-metal Rust, async executor, no RTOS | Supported chip | **esp-hal 1.0.0 released 2025-10-30**, vendor-backed. Stable only for GPIO/UART/SPI/I²C + init + time; everything else behind `unstable`. Xtensa needs the `espup` toolchain. Drivers: `mipidsi` (ILI9341), `embedded-graphics`, XPT2046 crates exist. Wi-Fi via `esp-radio` |
| Rust std on ESP-IDF (`esp-idf-svc`/`esp-idf-hal`) | Rust std on top of ESP-IDF/FreeRTOS | Works on ESP32 | Full IDF features from Rust; bigger binaries, slow builds |
| MicroPython | Python interpreter | Listed by the vendor | Easy, slow, more RAM; `lv_micropython` bundles LVGL. Fine for experiments, weak for an efficient OS |
| Others listed by the vendor | Mixly, Scratch 3.0 | Educational | Not suitable |
| Zephyr RTOS | Full RTOS with device tree, drivers, shell, filesystems | ESP32 officially supported by Espressif (announced 2021); a board definition for this CYD would have to be written | Proper "OS" architecture, LVGL listed as an integration; heavier learning curve; Wi-Fi/BT come from Espressif's HAL |
| Apache NuttX | POSIX-like RTOS | **Has a ready board config `esp32-2432S028`** (configs include `nsh`, `buttons`, `wifi`, `lvgl` on the NuttX LCD driver) | Unix-like model (VFS, `nsh` shell, apps). Caveats from its docs: it describes the module as ESP32-WROVER-32 (WROVER has PSRAM; this board has none) and lists GPIO2 as LCD reset (here GPIO2 is DC, reset is EN). No touch config documented. Check before relying on it |
| ESPHome / Tasmota | Config-driven firmware | Supports ILI9341 | Not a base for a custom OS |

### 7.2 Display and graphics libraries

| Option | Language | Fit | Notes |
|---|---|---|---|
| **LovyanGFX** | C++ | **Proven here** (1.2.35 on IDF 5.5.5) | Fast SPI + DMA, sprites, built-in fonts, touch drivers (XPT2046), LCD read-back. Arduino and ESP-IDF. Not in the IDF component registry (submodule). ESP-IDF v6 support unconfirmed |
| TFT_eSPI | C++ (Arduino) | Vendor demos | Very popular, fast. Config by editing `User_Setup.h` (vendor one has the wrong RST pin). Built-in touch cannot reach the separate touch bus |
| Adafruit GFX + ILI9341 | C++ (Arduino) | Works | Simple, slower |
| Arduino_GFX (moononournation) | C++ (Arduino) | Supports ILI9341 (its default driver) | Many panels and buses, good speed |
| `esp_lcd` (ESP-IDF component) | C | ILI9341 via the registry component `espressif/esp_lcd_ili9341` (2.x) | Official IDF LCD driver layer; `espressif/esp_lvgl_port` connects any `esp_lcd` panel to LVGL; no sprites or fonts itself |
| `embedded-graphics` + `mipidsi` | Rust | Supports ILI9341 | Standard Rust embedded graphics stack |

### 7.3 UI toolkits (widgets, layouts, input)

| Option | Fit | Notes |
|---|---|---|
| Hand-made widgets on LovyanGFX sprites | **Proven here** | What FenixDeck did: about 900 lines for 7 widget types, 5 pages × 2 layouts. Total control and tiny memory, but every widget is hand-written |
| **LVGL** (v9; 9.5.0 released 2026-02-18, 9.6 likely) | Vendor's stock firmware is an LVGL demo built with IDF 4.3, so LVGL runs on this board. The vendor IDF 4.3 demo sources bundle LVGL **7.0.2**; the vendor Arduino libraries bundle LVGL **8.3.3** | Full widget set (buttons, lists, charts, gauges, keyboards, animations), themes, input devices, partial rendering. Docs: ~2 KB static RAM, 8 KB stack and ~48 KB heap recommended with many widgets. Reports: v9 uses more RAM than v8 (32-bit colours internally), and ESP32 users without PSRAM hit limits with many screens. Use small partial buffers (for example 1/10 screen, double-buffered) and keep screens few or load them on demand. Strong candidate for an "OS" UI |
| Slint (Rust/C++) | Unverified on this board | Has an MCU software renderer with line-by-line rendering; no confirmed report of a no-PSRAM ESP32 + ILI9341 setup |
| `embedded-graphics` + hand-made widgets | Possible | Rust equivalent of the FenixDeck approach |
| LVGL in MicroPython (`lv_micropython`) | Possible | Easy, but RAM-heavy on a no-PSRAM chip |

### 7.4 Build tools

| Option | Notes |
|---|---|
| **ESP-IDF `idf.py`** (CMake + Ninja) | **Proven here**. Official. EIM installs it on Windows. Needs the gotchas in section 12 |
| PlatformIO | Unified, VS Code friendly; lags official releases |
| Arduino IDE / arduino-cli | Simplest; least control |
| cargo + espup + espflash | Rust paths |
| Zephyr `west` | Zephyr path |

### 7.5 ESP-IDF version

| Version | Notes |
|---|---|
| v5.5.5 | **Proven here** with LovyanGFX 1.2.35 |
| v6.0 (released 2026-03) | Mostly compatible with 5.x apps. Removes the legacy ADC (`driver/adc.h`) and I2S (`driver/i2s.h`) drivers (migration guide) and the legacy DAC driver (release announcement); legacy I2C is end-of-life, removal planned for v7; driver headers no longer pull in FreeRTOS headers. LovyanGFX support not confirmed; test before switching. The speaker (IO26) needs the new DAC driver (`dac_oneshot`/`dac_continuous`) |

### 7.6 Transport to the PC

| Option | Notes |
|---|---|
| **USB serial (CH340)** | **Proven here**: 460800 baud, 21 ms round trip, 0 CRC errors, replug recovery 1.3 s. Same cable powers the board |
| Wi-Fi (TCP/WebSocket/UDP) | Not built. Same COBS frames can run over TCP. Needs provisioning (SmartConfig demo exists in the vendor package). Costs heap: one arduino-esp32 report measured ~45 KB for starting STA mode alone; sockets and buffers add more. Measure on the real build |
| BLE | Possible (vendor BLE demo); low bandwidth |
| Faster UART | CH340C is rated to 2 Mbps peak (460,800 continuous with flow control per a distributor spec), and 921600 failed once during a flash read. Untested for the app link with CRC counting |

### 7.7 PC side (if the OS keeps a PC companion)

| Option | Notes |
|---|---|
| **Rust + Tauri 2 + Svelte 5** | **Proven here**. 29 MB in the tray, 96–97 MB with the window open after tuning |
| Other Tauri frontends | Vanilla TS (lightest), SolidJS, React (heaviest) |
| Rust CLI/tray only | Smallest; no rich UI |
| Electron | Much heavier; not considered |
| Python | Fine for tools (`tools/validate_metrics.py`), not for the app |

### 7.8 PC metrics sources (Windows)

| Source | Gives | Notes |
|---|---|---|
| PDH (Performance Data Helper) | CPU utility (Task Manager's counter), per-core, live clock, GPU engines, VRAM used, disk I/O | Free, no driver, does not wake the dGPU |
| `sysinfo` crate | RAM, disks, networks | Network needs zero-MAC filtering |
| LibreHardwareMonitor web server | CPU package temp/power, GPU sensors, fans | Needs admin (UAC). Its low-level driver moved from WinRing0 (flagged by Defender) to PawnIO, which may need a separate install |
| NVML (`nvml-wrapper`) | NVIDIA temp/power/clock/load | ~19 MB in-process; can wake a sleeping dGPU unless gated |
| SetupAPI device power state | Whether the dGPU is awake (D0) | Reading it does not wake the GPU |
| `GetSystemPowerStatus` | Battery %, AC state, time left | Fast |
| `starship-battery` | Battery power (W) | ~115 ms per query; poll on a thread |
| WMI thermal zone | "CPU temp" | Usually a generic ACPI zone on laptops; rejected |

---

## 8. Wire protocol v1

Full spec: `docs/legacy/protocol.md`. Rust: `crates/fenix-proto`. C++: `board/components/fenix_proto`. Golden test vectors: `crates/fenix-proto/vectors/vectors.txt` (19 vectors; both implementations decode them and encode them byte-identically). The protocol is reusable as-is for any PC ↔ board link.

### Transport and framing

- USB serial, 460800 baud, 8N1, no flow control. The PC opens the port with DTR = false, RTS = false.
- Frame on the wire: `COBS(payload ‖ crc16_le) ‖ 0x00`.
  - COBS: standard Consistent Overhead Byte Stuffing. The encoded data has no `0x00`, so `0x00` is the delimiter, and a receiver can resync after garbage (the ROM boot text) at the next zero.
  - CRC-16/CCITT-FALSE: poly `0x1021`, init `0xFFFF`, no reflection, no xorout, over the payload, appended little-endian.
- Payload: `type:u8 ‖ seq:u8 ‖ body`. No length field (framing gives it). `seq` is a per-sender counter for diagnostics.
- Max payload 1200 bytes; larger frames are dropped and counted as errors.
- All integers little-endian; no floats (fixed-point `_x10` fields); strings are `len:u8 ‖ utf8`.
- Bad CRC or COBS errors drop the frame and increment the receiver's error counter.

### Session

1. PC sends `HELLO` every 300 ms for up to 2.5 s. Board answers `HELLO_ACK`, then a `LOG` with its reset reason and uptime. A protocol-version mismatch gets an `ERROR`; the PC treats the port as "no board" and keeps scanning.
2. PC sends `INFO` and `CONFIG`, then `METRICS` / `CORES` / `DISKS` at the poll rate.
3. PC sends `PING` every 1 s; the board answers `PONG`. Either side treats **3 s** without a valid frame as disconnected. On the board, `SHOT_REQ` alone does not count as traffic.
4. Board sends `STATS` every 5 s, and `TOUCH` / `PAGE` on events.

### Messages

| Type | Name | Dir | Body |
|---|---|---|---|
| 0x01 | HELLO | P→B | magic `"FNXD"` ‖ proto_ver:u8 |
| 0x02 | HELLO_ACK | B→P | magic ‖ proto_ver ‖ fw major/minor/patch ‖ width:u16 ‖ height:u16 ‖ free_heap:u32 ‖ mac:6 |
| 0x03 / 0x04 | PING / PONG | both | t_ms:u32 (PONG echoes) |
| 0x10 | CONFIG | P→B | rotation ‖ brightness ‖ dim_brightness ‖ dim_timeout_s:u16 ‖ fps_cap ‖ page_mask ‖ flags (bit0 °F, bit1 heap overlay) |
| 0x11 | METRICS | P→B | fixed 58-byte struct with a `present:u32` bitmask (CPU load, CPU sensors, GPU, RAM, disk I/O, network, battery, GPU idle) + uptime |
| 0x12 | CORES | P→B | count ‖ util[count] (0–100) |
| 0x13 | DISKS | P→B | count ‖ (letter ‖ used_mb:u32 ‖ total_mb:u32)… |
| 0x14 | INFO | P→B | cpu_name ‖ gpu_name ‖ host (strings) |
| 0x20–0x22 | IMAGE_BEGIN / CHUNK / END | P→B | slot (0–1), w, h ≤ 64; chunks of ≤ 1024 B RGB565 LE with offset; END → board logs "image slot N ready" |
| 0x30 | TOUCH | B→P | kind (1 down, 2 up, 3 tap, 4 long) ‖ x:u16 ‖ y:u16 ‖ page |
| 0x31 | PAGE | both | page (B→P changed, P→B go to) |
| 0x40 | LOG | B→P | level (1 err … 4 debug) ‖ text |
| 0x41 | ERROR | both | code ‖ text |
| 0x42 | STATS | B→P | free_heap ‖ min_heap ‖ fps_x10 ‖ render_us ‖ rx_frames ‖ rx_errors |
| 0x50 | SHOT_REQ | P→B | (empty): board reads its LCD back |
| 0x51 | SHOT_ROW | B→P | y ‖ w ‖ h ‖ RGB565 pixels of one row |

`PROTO_VERSION = 1`. Any layout change bumps it and regenerates the vectors (`FENIX_REGEN=1 cargo test -p fenix-proto`).

### Why this design worked

- COBS + CRC made boot garbage, partial frames and unplugs harmless: 0 CRC errors in normal runs, and clean resync after every reset.
- Fixed-size metrics with a present-bitmask kept the board parser trivial and made "sensor missing" explicit (drawn as "–").
- Golden vectors caught Rust/C++ mismatches before anything was flashed.
- LCD read-back (`SHOT_REQ`) gave exact screenshots for docs and visual checks without a camera.

---

## 9. Board firmware as built

Source: `board/main/` (about 1,400 lines of C++, of which `ui.cpp` is ~900) plus the 291-line protocol component.

### Tasks and data flow

| Part | Core | Priority | Stack | Job |
|---|---|---|---|---|
| `link_rx` task | 0 | 10 | 4 KB | Reads UART0 (driver RX buffer 8 KB, TX 4 KB), decodes COBS frames, answers HELLO/PING directly, writes everything else into the state store |
| `ui` task | 1 | 5 | 8 KB (uses ~2.6 KB) | Renders, polls touch, drives the backlight, sends STATS/TOUCH/PAGE/SHOT_ROW |
| State store | – | – | – | Latest-value store under a mutex. The link writes; the UI copies a snapshot per frame. Version counters say what changed |
| Stall timer | – | – | – | `esp_timer` every 1 s; logs if the UI loop stops advancing |

The namespace is `pclink`, not `link`: `link` is a POSIX function in ESP-IDF's newlib and clashes.

At boot: NVS init (erase and re-init on version/space errors), state init, UI task, link start, then a `LOG` "FenixDeck fw x.y.z up". After `HELLO_ACK` the board logs its reset reason (`esp_reset_reason`) and uptime, which is how "port open does not reset the board" was proven.

### Render pipeline

1. Each page has a fixed layout table per orientation: a list of widgets with rectangle, type and data source. Maximum 8 widgets per page plus the header.
2. Every frame, each widget's displayed content (formatted strings, bar fractions, ring version) is built and hashed. Only widgets whose hash changed are redrawn.
3. A dirty widget is drawn into one of **two 40 KB sprite buffers** (max 20,480 px per widget), used ping-pong: the CPU draws into one while DMA pushes the other (`pushImageDMA` waits for the previous transfer).
4. Frame loop: wake on new data or after ≤ 33 ms, never faster than the fps cap (1–60, default 30).
5. Full-screen clears only on page or rotation change.

Widget types: header (title, page dots, battery, optional heap overlay), tile (big value + subline + bar), arc gauge, key/value rows, per-core bars, sparkline (60-sample rings for CPU, GPU, net down/up), horizontal bar rows (RAM, disks).

Fonts: LovyanGFX built-in GFX fonts. Big values pick the largest of FreeSansBold 24/18/12/9 pt that fits the width ("auto-fit"). Narrow panels use the small `Font2` so label and value never collide.

Colours: a fixed dark palette in RGB565 (background `rgb(10,12,16)`, per-group accents CPU orange, GPU green, RAM blue, net cyan, upload purple, battery yellow, disk pink, warning red at ≥ 90 °C).

### Pages

| # | Page | Content |
|---|---|---|
| 0 | Overview | 4 tiles: CPU, GPU, RAM, NET |
| 1 | CPU | Gauge, key/values (temp, clock, power, threads), per-core bars (16 on the test PC), 60 s sparkline |
| 2 | GPU | Gauge, key/values (temp, power, clock, GPU name), VRAM bar, 60 s sparkline; "idle" when the dGPU sleeps |
| 3 | Memory | RAM bar, per-disk bars, disk read/write |
| 4 | Network | Down/up values, battery values, down/up sparkline, battery bar |

`page_mask` in CONFIG enables pages; tap skips disabled ones.

### Touch

- Polled in the UI task (~30 Hz). Down → `TOUCH down`; held > 600 ms → `long` (once); release → `up`, and a `tap` if it was < 600 ms and moved < 20 px.
- A tap also advances to the next enabled page and sends `PAGE`.
- Touch events are sent only while connected.
- Calibration: hold the screen during boot → LovyanGFX `calibrateTouch` (tap 4 arrow tips) → 8 values saved in NVS namespace `fenix`, key `tcal`. Otherwise the default raw range is used, which was accurate enough.

### Offline, backlight, images

- Waiting screen: logo (image slot 0 if the PC pushed one, else a drawn flame of triangles), "FenixDeck", "waiting for PC...", firmware version and baud rate, and three pulsing dots (a 60 × 12 sprite pushed by DMA).
- Backlight: configured brightness; after `dim_timeout_s` without data it fades to `dim_brightness`; restored when data arrives.
- Image slots: 2 × up to 64 × 64 RGB565 in RAM (8 KB each).

### Debug features

- `STATS` every 5 s: heap, min heap, fps, render time, rx frames/errors.
- Heap overlay in the header (CONFIG flag bit1).
- LCD read-back screenshot (`SHOT_REQ`), row by row.
- Boot reason + uptime in the first `LOG`.
- Stall detector.

---

## 10. PC side as built

### Crates and app

| Part | Job |
|---|---|
| `fenix-proto` | Frame codec (COBS, CRC), message types, golden vectors |
| `fenix-link` | Finds the CH340 (VID:PID `1A86:7523`, or a configured port), opens it DTR/RTS-safe at 460800, handshakes, heartbeats, reconnects. **One thread per session**: batch writes, then read with a 15 ms timeout. Rescans every 1 s after a failure |
| `fenix-metrics` | Collector producing one `Sample` per poll (~10 ms) from PDH, sysinfo, SetupAPI, LHM (background thread, 5 s back-off when unreachable), NVML (fallback, only while the dGPU is in D0), battery (`GetSystemPowerStatus` per sample, `starship-battery` power on a thread) |
| `fenix-cli` (`fenix`) | Dev tool: `ports`, `ping`, `demo`, `live`, `monitor`, `sample`, `logo <png>`, `shot`, `rawshot`; options `--port --secs --rate --rotation --fps --page --overlay --real --wait --out` |
| `app/` (Tauri) | Engine thread (poll → push to board → emit to UI only while the window is visible), tray (Show, Reconnect, Quit), settings JSON at `%APPDATA%\dev.fenixdeck.app\settings.json`, LHM launcher, logo push, single instance, `fenixdeck --quit` |
| `tools/` | `validate_metrics.py` (accuracy check), `measure_app.ps1` (footprint), `replug_watch.ps1` (port presence timestamps) |

### Metrics sources in detail

| Metric | Source |
|---|---|
| CPU load | PDH `Processor Information(_Total)\% Processor Utility` (Task Manager's counter; frequency-scaled, can exceed the time share under turbo) |
| Per-core | PDH per-instance utility |
| CPU clock | PDH `% Processor Performance` × base frequency (live). `sysinfo` reports a static value on Windows |
| CPU temp / power | LHM `data.json` (package sensors). No other source on Windows |
| GPU awake | SetupAPI device power state; asleep → no sensors, UI shows "idle", METRICS bit 7 |
| GPU load / temp / power / clock / VRAM total | LHM when running; else NVML (loaded only while D0); else load from PDH `GPU Engine` |
| VRAM used | PDH `GPU Adapter Memory` dedicated usage (matches Task Manager and nvidia-smi; LHM's figure includes ~230 MB driver reserve) |
| RAM | `sysinfo` |
| Disk space | `sysinfo` (refreshed every 30 s) |
| Disk I/O | PDH `PhysicalDisk(_Total)` |
| Network | `sysinfo` networks, skipping zero-MAC NDIS filter interfaces and virtual/loopback adapters |
| Battery | `GetSystemPowerStatus` + `starship-battery` power on a thread |

### LHM integration

- The app finds `LibreHardwareMonitor.exe` (configured path, winget package dir, Program Files).
- If LHM has no config yet, the app writes `LibreHardwareMonitor.config` next to the exe, enabling the web server on port 8085 and start-minimised/minimise-to-tray.
- Launch is `ShellExecuteW("runas")` (UAC prompt), on a thread, skipped if port 8085 already answers.
- Data URL: `http://127.0.0.1:8085/data.json`.

### WebView2 tuning (Tauri on Windows)

`additionalBrowserArgs` in `tauri.conf.json`:

```
--disable-features=msWebOOUI,msPdfOOUI,msSmartScreenProtection
--enable-features=NetworkServiceInProcess2 --disable-gpu --in-process-gpu
--renderer-process-limit=1 --disable-extensions --disable-background-networking
--disable-component-update
```

Plus: no CSS transitions/animations, no UI updates while hidden, the window is created at the end of `setup` (`create: false`), and closing the window destroys the webview (the engine keeps running in the tray). Any `--js-flags` value brought back an extra process (+60 MB), so none is used.

---

## 11. Measurements

All measured on the hardware on 2026-10-28.

### Board

| What | Value |
|---|---|
| Firmware size | 334 KB (of a 2 MB app partition; stock app partition was 1 MB). The image on the board, read back 2026-10-29 (build `74a62bb-dirty`), is 337,008 B (329 KiB) |
| Free heap | ~159 KB (min ~152 KB), with 2 × 40 KB sprite buffers, UART buffers, 2 image slots, Wi-Fi/BT off |
| UI task stack | ~2.6 KB used of 8 KB |
| Render time | 14–28 ms per frame with all widgets dirty; CPU page ~27 ms, Overview ~15–25 ms |
| Frame rate | 20 fps sustained at 20 Hz data, 0 link errors. At a 1 s poll, dirty-only redraw gives ~1–2 frames/s |
| Theoretical full-frame push | ~31 ms at 40 MHz (corrected: "55 MHz" runs at 40) |

### Link

| What | Value |
|---|---|
| Baud | 460800 |
| Round trip (PING → PONG) | ~21–22 ms |
| CRC errors | 0 in normal runs (`STATS` rx_errors) |
| Port open | Does not reset the board (reset reason power-on, uptime keeps growing across sessions) |
| Unplug detection | < 30 ms after the port disappears (`read: Access is denied (os error 5)`) |
| Replug recovery | **1311 / 1347 / 1336 ms** from port reappearance to reconnected (includes board boot, handshake, 1 s rescan), 3 physical replugs. Target was < 3 s |

### PC

| What | Value |
|---|---|
| Sample cost | ~10 ms per poll (was 550 ms before LHM and battery moved to threads) |
| App, window open (2-min soaks × 2) | 0.14–0.17 % of total CPU (2.2–2.7 % of one core); 96–97 MB private working set over 4 processes |
| App, in tray | 0.07 % CPU; 29 MB; 1 process |
| NVML in-process | +~19 MB private (avoided when LHM runs) |
| Installer | 1.28 MB NSIS; app exe 3.5 MB |

Conventions: Task Manager's (CPU as % of all logical processors, memory as private working set), summed over `fenixdeck.exe` and only its own WebView2 processes.

### Metrics accuracy (`docs/legacy/metrics-validation.md`)

15 s window, means compared: 19 pass, 0 fail, 1 info.

| Metric | FenixDeck | Reference | Tolerance |
|---|---|---|---|
| CPU load | 9.2 % | 9.2 % (typeperf) | ±5 % |
| CPU temp | 56.9 °C | 57.0 °C (LHM) | ±3 °C |
| CPU power | 13.0 W | 13.6 W (LHM) | ±3 W |
| GPU temp | 50.0 °C | 50.3 °C (nvidia-smi) | ±2 °C |
| GPU power | 6.3 W | 6.3 W | ±3 W |
| GPU util | 21.4 % | 18.6 % | ±10 % |
| VRAM used / total | 1798.8 / 8188 MB | 1791.0 / 8188 MB | ±5 % / ±1 MB |
| RAM used / total | 17940.7 / 32468.0 MB | 17964.7 / 32468.5 MB (CIM) | ±3 % / ±1 MB |
| Disk C:/D: used and total | exact to 0.1 GB | CIM | ±1 GB |
| Disk read / write | 1.4 / 546.4 KB/s | 1.4 / 545.4 KB/s (typeperf) | ±200 KB/s |
| Net down / up | 2.0 / 1.3 KB/s | 2.0 / 1.3 KB/s (typeperf) | ±20 KB/s |
| Battery | 95 % | 94 % (CIM rounds differently) | ±1 % |

---

## 12. Gotchas and lessons

Grouped by area. Tool-specific ones are tagged so they can be skipped if that tool is dropped.

### Hardware and board

- **No TE pin → no vsync.** Off-screen widget buffers + small DMA pushes make tearing invisible. Do not plan on true vsync.
- **No PSRAM.** Budget RAM from day one. A full framebuffer is not an option.
- **UART0 is shared with the console.** Turn the console off, or logs corrupt the link. The ROM bootloader still prints at reset; receivers must resync.
- **DTR/RTS reset the board.** Open the port with both released.
- **921600 baud corrupted a flash read.** Stay at 460800 unless tested with error counting.
- **Touch is on its own bus.** Libraries that assume a shared SPI bus for touch fail.
- **Vendor TFT_eSPI config has RST on IO12.** Wrong; IO12 is MISO.
- **Resistive touch is weak with fingers.** Use the stylus. Calibrate by holding at boot if needed.
- **Use a USB-A to USB-C cable**, not C-to-C (likely no CC resistors).
- **Only IO22, IO27, IO35 are free.** IO21 on P3 is the backlight.
- **Software unplug is not reliable on Windows.** `pnputil /disable-device` failed ("pending system reboot") and `/remove-device` deferred until reboot. Test replug physically.

### Firmware

- **`link` is taken** by newlib (POSIX); use another name for your link namespace. *(ESP-IDF/newlib)*
- **Hash-based dirty tracking** is simple and robust: build the displayed content, hash it, redraw only on change.
- **Ping-pong sprite buffers** let drawing overlap DMA.
- **Auto-fit fonts** prevent label/value overlap across orientations.
- **`volatile x++` warns in C++20 mode** of the ESP-IDF GCC; use `x = x + 1`. *(GCC 14 / C++20)*
- **`-Wno-format-truncation`** was added for intended `snprintf` truncation into fixed UI buffers. *(GCC)*
- **A stall detector and boot-reason log** are cheap and made several problems visible quickly.

### Windows serial (Rust `serialport`)

- **Do not use a reader thread on a cloned handle.** Reads and writes on one port serialize; a reader thread with a 20 ms timeout stalled every write and starved the link above ~4 Hz. Fix: one thread per session, batch writes, then read with a 15 ms timeout (returns as soon as bytes arrive).

### PC metrics (Windows)

- **Network 2x double count:** NDIS filter drivers (Npcap, VirtualBox, WFP) appear as extra interfaces with the same traffic and an all-zero MAC. Skip zero-MAC interfaces.
- **`sysinfo` CPU frequency is static on Windows.** Use PDH `% Processor Performance` × base frequency.
- **CPU load: use `% Processor Utility`** to match Task Manager. LHM "CPU Total" is time-based and reads lower under turbo.
- **NVML can wake a sleeping dGPU** and costs ~19 MB in-process. Gate it on the SetupAPI power state (D0), and prefer LHM's sensors.
- **`starship-battery` takes ~115 ms per query.** Use `GetSystemPowerStatus` per sample.
- **An unreachable LHM makes each HTTP call wait for its timeout.** Poll LHM on its own thread with a back-off.
- **LHM's "GPU Memory Used" includes ~230 MB driver reserve**; PDH's dedicated usage matches Task Manager.

### Tauri, WebView2, app

- **WebView2 by default runs separate GPU and renderer processes**, and CSS animations keep them busy. Use the lean `additionalBrowserArgs`, no CSS transitions, no UI updates while hidden, and destroy the webview when closed to the tray. *(Tauri/WebView2)*
- **Never block `setup` or sync commands on UAC.** `ShellExecuteW("runas")` waits for the prompt; in `setup` it hung startup and stacked prompts. Run it on a thread; make the command async. *(Tauri)*
- **Window vs managed-state race:** the webview called commands before `app.manage()`, which left the settings panel empty. Create the window at the end of `setup` (`create: false`), and retry initial loads in the UI. *(Tauri)*
- **Forced kills leave ghost tray icons.** Provide a clean `--quit`; and make `--quit` a no-op when no instance runs (it used to start a new instance). *(Tauri single-instance)*
- **Measuring WebView2 apps:** other apps run `msedgewebview2.exe` too. Count only processes whose command line has `--webview-exe-name=<your exe>`.
- **`tauri icon` accepts an SVG.** *(Tauri)*

### Toolchain on Windows

- **EIM's PowerShell profile fails to parse in pwsh 7.** Set the IDF environment in your own wrapper script (`board/idf.ps1`). *(ESP-IDF/EIM)*
- **`idf.py` refuses to run when `MSYSTEM` is set** (Git Bash). Clear it in the wrapper. *(ESP-IDF)*
- **`vcvars64.bat` breaks on PATH entries with parentheses** (`\Nmap was unexpected at this time`). Start the MSVC test script from a clean PATH. *(MSVC)*
- **Clone with submodules** (`git clone --recursive`) when a dependency is a submodule.
- **Python on Windows cannot see Git Bash `/tmp`**; use real paths.
- **The vendor download site needs a browser** (sharded downloads).

### Process

- **Measure before claiming.** Two early claims were wrong and were caught by re-measuring: a "~215 MB" app figure that included another app's WebView2 processes, and a "~1.5 s" replug time that the log did not support (re-measured with a port watcher: 1.31–1.35 s).
- **Validate metrics against independent sources** with a script, not by eye.
- **LCD read-back screenshots** are exact and make visual regressions easy to spot.
- **Golden vectors** for a protocol shared between two languages pay off immediately.

---

## 13. Corrections log

Things that were believed at some point and later corrected. The current docs hold the corrected values.

| Earlier belief | Correction | Source of the correction |
|---|---|---|
| U3 = CH340, U6 = touch | U3 = **XPT2046 touch**, U6 = **CH340C** | schematic cross-check |
| U4 is a missing part | U4 is an **unpopulated 25Q32 footprint** | schematic |
| Single core (vendor manual) | **Dual core** | esptool |
| ESP32-D0WDQ6 (listing) | **ESP32-D0WD-V3 rev 3.1** | esptool |
| Display might be ST7789 | **ILI9341V** | panel spec + hardware |
| Vendor `TFT_RST 12` | IO12 = MISO, **RST = EN (-1)** | schematic + LovyanGFX demo |
| Touch shares the LCD bus | **Separate bus** 25/32/39/33, IRQ 36 | schematic |
| ~300 KB free heap available (general ESP32 figure) | Free heap depends on the build (reports range from ~170 KB to ~310 KB). FenixDeck measured ~159 KB free after about 108 KB of its own buffers (80 KB sprites, 16 KB image slots, 12 KB UART driver) | STATS, online reports |
| Stock firmware LVGL is "v8 era" (first draft of this write-up) | Vendor IDF 4.3 demo bundles LVGL 7.0.2; vendor Arduino libraries bundle 8.3.3 | vendor package |
| PC app window ~215 MB "before tuning" | That figure included another app's WebView2 processes; no reliable "before" number. After tuning: 96–97 MB | `tools/measure_app.ps1` |
| Replug recovery ~1.5 s | Measured 1.31–1.35 s | `tools/replug_watch.ps1` |
| Network rate correct | It was 2x (filter interfaces); fixed | `tools/validate_metrics.py` |
| `fenixdeck --quit` always safe | Started a new instance when none ran; fixed | manual test |

---

## 14. Not done, open questions, advice for the OS project

### Not done in the PoC

- Wi-Fi transport (protocol is ready for it).
- OTA updates (needs a new partition layout).
- PC-defined layouts (layouts are compiled into the firmware).
- Stream-deck style actions on touch (events reach the PC; nothing acts on them yet).
- SD card, speaker, RGB LED, light sensor: wired but unused.
- Autostart with Windows.
- ESP-IDF v6 and LovyanGFX on it: untested.
- 921600 baud for the app link: untested with error counting.

### Open questions

- Touch regions: v1 cycles pages on any tap. An OS will need hit-testing and buttons (LVGL gives this).
- Long-press is reported but reserved; no action is bound.

### Advice for the "OS of sorts"

These are recommendations from what the PoC showed, not decisions.

1. **Keep the hardware facts and the pin config.** `board/main/lgfx_board.hpp` is a proven LovyanGFX config, and the pin map in section 2 is verified.
2. **Plan RAM first.** Without PSRAM, free heap before your own buffers is roughly 260 KB in a FenixDeck-like build (estimate: 159 KB measured + 108 KB of known buffers), and less with Wi-Fi/BT. With that, decide the render-buffer budget (FenixDeck: 80 KB), the Wi-Fi budget (if used), and the UI toolkit's heap up front. Measure with a STATS-like heartbeat from the first build.
3. **Pick the UI layer deliberately.**
   - For many apps/screens with standard widgets: LVGL with small partial buffers, on `esp_lcd` or LovyanGFX as the flush driver. Load screens on demand to stay inside RAM.
   - For a few highly custom screens: the FenixDeck approach (hash-dirty widgets on sprites) is smaller and faster.
4. **Keep the transport rules.** Console off UART0, COBS + CRC framing, DTR/RTS released, 460800 baud, heartbeat with a 3 s timeout, reset-reason log on connect.
5. **Re-plan the flash layout.** For OTA on 4 MB: two ~1.5 MB app slots + otadata + a smaller storage partition, or one app slot + factory recovery. FenixDeck's 334 KB firmware leaves lots of room, but LVGL, Wi-Fi and fonts grow it fast.
6. **Consider an RTOS-level structure.** NuttX already ships a board config for this exact board (with LVGL), which makes it the quickest "real OS" to try; check its pin table first (section 7.1). ESP-IDF already is FreeRTOS. A home-made "OS" on top usually means: an app/screen manager, an input event queue (touch → hit-test → focused app), a service layer (link, storage, settings in NVS, time), and a per-app lifecycle (create, resume, pause, destroy) to free RAM between apps. Zephyr or NuttX give more of that ready-made, at a cost in learning and footprint.
7. **If the new project needs more headroom,** a board with PSRAM (for example an ESP32-S3 variant of the CYD family) removes the biggest constraint. This is a hardware change, so note it as an option only.
8. **Keep the verification habits:** golden vectors for any wire format, LCD read-back screenshots, scripted validation, measured (not estimated) footprints.

---

## 15. Transfer kit: what to take to the new repo

### Docs

| File | Ready? | Notes |
|---|---|---|
| `docs/POC-WRITEUP.md` (this file) | **Ready** | Self-contained; the main hand-over doc |
| `pics/ANALYSIS.md` | **Ready** | Hardware analysis, per-image notes, corrections. Take with all images in `pics/` |
| `docs/board-info.md` | **Ready** | esptool read-out, stock backup + restore, hardware findings. Mentions FenixDeck paths (`board/`, `fenix shot`) |
| `docs/vendor/README.md` | **Ready** | What the vendor package holds and where (the package itself is local only) |
| `docs/legacy/protocol.md` | **Ready if the protocol is reused** | Complete spec; references the FenixDeck crate paths |
| `docs/legacy/metrics-validation.md` | Optional | Only relevant if the PC monitor carries over |
| `docs/toolchain.md` | Reference only | Versions and gotchas of this toolchain; expected to change (summarised in sections 6 and 12) |
| `docs/legacy/PLAN.md` | Historical | The FenixDeck plan, now superseded by this write-up |
| `AGENTS.md` | Reusable | The 6 working rules (no co-authoring, commit/push often, plan in `tasks.md`, balance knowledge/search/browser, document findings, clean stale content) |
| `README.md`, `tasks.md` | No | FenixDeck-specific |

### Images

| Path | Notes |
|---|---|
| `pics/*.jpg` (17 files; 16 in git, the packing slip is local only) | Board photos, manual, box label, two store listings. Analysed in `pics/ANALYSIS.md` |
| `docs/legacy/screens/v01-live-overview.png`, `v01-live-cpu.png` | Exact LCD read-back of the PoC UI |
| `docs/vendor/.../5-Schematic/*.jpg` (local only) | MCU and LCM schematics, WROOM pin definition |
| `docs/vendor/.../3-Structure_Diagram/Dimensions.png` (local only) | Board dimensions (for cases/mounts) |

### Vendor files and firmware (all local only, not in git)

Since 2026-10-29 the fenixOS repo is public, and these third-party files carry no redistribution licence, so they stay on the local disk (`.gitignore`).

| Path | Notes |
|---|---|
| `docs/vendor/.../4-Driver_IC_Data_Sheet/` | ESP32-WROOM-32, JC2432A028N (panel, ILI9341V), FM8002A, 25Q32, DHT11 |
| `docs/vendor/.../2-Specification/`, `6-User_Manual/` | Board spec and getting-started manual |
| `docs/vendor/CH340/` | CH340 driver installer |
| `firmware/stock/stock_flash_4MB.bin` (4 MB) | Factory backup; keep it to restore the board |
| `firmware/fenixdeck/fenixdeck_v0.1.0_flash_4MB.bin` (4 MB) | FenixDeck v0.1.0 as it ran on the board, read back 2026-10-29 (restore steps in `docs/board-info.md`) |
| `docs/vendor/archives/*.rar` | Full vendor package; re-download if needed |

### Code worth carrying over (any toolchain)

These paths are in the FenixDeck repo. **They were not copied into fenixOS** (the repo holds docs, pics, vendor files and flash backups only). Section 6 has the full `sdkconfig.defaults` and `partitions.csv`, and the vendor LovyanGFX demo has the LCD config (touch range fixed as noted in section 3).

| Path | Why |
|---|---|
| `board/main/lgfx_board.hpp` | Proven LovyanGFX config for this board (LCD, backlight, touch) |
| `board/sdkconfig.defaults`, `board/partitions.csv` | Proven ESP-IDF settings (console off UART0, 240 MHz, 4 MB flash) |
| `board/components/fenix_proto/` + `crates/fenix-proto/` + `vectors/vectors.txt` | Protocol codec in C++ and Rust with shared golden vectors |
| `board/idf.ps1` | Windows wrapper that works around the EIM/MSYSTEM issues |
| `tools/replug_watch.ps1` | Port presence watcher for replug timing |
| `crates/fenix-link/` | Windows serial session logic (single-thread design, DTR/RTS safe) |

---

## 16. Sources

Hardware facts come from the board itself (esptool, LCD read-back, measurements) and the vendor package. Outside sources used during the PoC and for this write-up:

- [LovyanGFX repository](https://github.com/lovyan03/LovyanGFX) and [docs](https://lovyangfx.readthedocs.io/en/latest/01_intro.html)
- [CH340 datasheet](https://cdn.sparkfun.com/datasheets/Dev/Arduino/Other/CH340DS1.PDF), [CH340C high-baud reports](https://esp32.com/viewtopic.php?p=95878)
- [WCH CH341SER driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
- [LibreHardwareMonitor switch to PawnIO](https://www.techspot.com/downloads/7761-librehardwaremonitor.html), [LHM web server setup](https://smarthomescene.com/guides/how-to-setup-librehardwaremonitor-in-home-assistant)
- [ESP-IDF v6.0 release](https://www.cnx-software.com/2026/03/24/esp-idf-v6-0-framework-adds-support-for-esp32-c5-and-esp32-c61-preview-for-esp32-h21-and-esp32-h4/?amp=1), [ESP-IDF support policy](https://dl.espressif.com/public/Espressif-EW26-ESP-IDF-Flyer.pdf)
- [esp-hal 1.0 announcement](https://developer.espressif.com/blog/2025/10/esp-hal-1/), [esp-hal 1.0.0 on docs.rs](https://docs.rs/crate/esp-hal/1.0.0)
- [LVGL 9.5 requirements](https://docs.lvgl.io/9.5/introduction/requirements.html), [LVGL release listing](https://www.arduinolibraries.info/libraries/lvgl), [LVGL 9 vs 8 memory on ESP32](https://forum.lvgl.io/t/lvgl-9-higher-memory-usage-and-different-usage-reports-in-lvgl-9-compared-to-8/15308), [LVGL 9.3 out of memory report](https://forum.lvgl.io/t/upgraded-app-from-v8-4-to-v9-3-now-running-out-of-memory/22037)
- [ESPHome ILI9xxx notes on RAM without PSRAM](https://esphome.io/components/display/ili9xxx/)
- [Espressif ESP32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf), [ESP32 overview (RTC SRAM split)](https://home.roboticlab.eu/en/iot-open/hardware2/esp32)
- [esptool boot mode selection (GPIO12/MTDI)](https://docs.espressif.com/projects/esptool/en/latest/advanced-topics/boot-mode-selection.html), [esp32.com MTDI thread](https://esp32.com/viewtopic.php?p=111208)
- [CYD pinout (Mischianti)](https://mischianti.org/esp32-2432s028-cheap-yellow-display-high-resolution-pinout-datasheet-schema-and-specs), [CYD pinout (Random Nerd Tutorials)](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/), [CYD notes (atomic14)](https://www.atomic14.com/esp32/boards/esp32-2432s028r/)
- [CH340 datasheet (mirror)](https://mikrocontroller.net/attachment/528524/CH340DS1.PDF), [CH340C distributor spec](https://www.datasheets.com/wch/ch340c)
- [NuttX board page: esp32-2432S028](https://nuttx.apache.org/docs/latest/platforms/xtensa/esp32/boards/esp32-2432S028/index.html), [Espressif: Zephyr and NuttX on ESP32](https://www.espressif.com/en/news/new_operating_systems_in_ESP32), [CNX: Zephyr/NuttX ESP32 status](https://www.cnx-software.com/2021/10/31/esp32-zephyr-nuttx-rtos-support/), [LVGL Espressif integrations](https://lvgl.io/docs/open/integration/chip_vendors/espressif/overview.mdx)
- [ESP-IDF v6.0 peripherals migration guide](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/migration-guides/release-6.x/6.0/peripherals.html), [ESP-IDF v6.0 release post](https://developer.espressif.com/blog/2026/03/idf-v6-0-release/)
- [esp_lcd_ili9341 component](https://components.espressif.com/components/espressif/esp_lcd_ili9341), [esp_lvgl_port component](https://components.espressif.com/components/espressif/esp_lvgl_port/versions/2.1.0/readme), [Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
- [Free heap without Wi-Fi report](https://esp32.com/viewtopic.php?p=16151), [Wi-Fi STA heap cost report](https://github.com/espressif/arduino-esp32/issues/8033)
- [PawnIO replacing WinRing0 (OpenRGB)](https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/2885), [Fan Control LHM PawnIO build](https://www.neowin.net/software/fan-control-v237/)

---

## 17. Glossary

| Term | Meaning |
|---|---|
| CYD | "Cheap Yellow Display", community name for the ESP32-2432S028R family |
| COBS | Consistent Overhead Byte Stuffing: removes zero bytes so `0x00` can delimit frames |
| CRC-16/CCITT-FALSE | 16-bit checksum (poly 0x1021, init 0xFFFF) |
| D0 | Device power state "fully on" (Windows/PCI); D3 = asleep |
| DMA | Direct memory access: SPI sends a buffer while the CPU keeps working |
| EIM | Espressif Installation Manager |
| LHM | LibreHardwareMonitor |
| NVML | NVIDIA Management Library |
| NVS | Non-volatile storage (ESP-IDF key/value store in flash) |
| PDH | Windows Performance Data Helper (performance counters) |
| PSRAM | External pseudo-static RAM; this board has none |
| RGB565 | 16-bit colour: 5 bits red, 6 green, 5 blue |
| Sprite | Off-screen pixel buffer (LovyanGFX `LGFX_Sprite`) |
| TE | Tearing-effect output of the LCD controller, used for vsync; not wired here |
| ULP | Ultra-low-power coprocessor inside the ESP32 |
| UAC | Windows User Account Control (admin prompt) |

---

## 18. Verification record

Cross-check run on 2026-10-29 over the whole repo. Every fact in this write-up was checked in one or more of these ways.

| Check | What was compared | Result |
|---|---|---|
| Doc vs doc | Every number and claim in README, PLAN, protocol, board-info, toolchain, metrics-validation, vendor README, pics/ANALYSIS, tasks.md and this write-up | Consistent. Fixed: PLAN §2.2 now labels the 300 KB / 96 KB figures as planning estimates with the measured values; README no longer says "no full-screen redraw" (page and rotation changes clear the screen) |
| Docs vs code | Protocol constants (max payload 1200, chunk 1024, baud 460800, magic, 58-byte METRICS), link timings (HELLO 300 ms / 2.5 s, heartbeat 1 s, timeout 3 s, rescan 1 s, read 15 ms), board timings (3 s link timeout, STATS 5 s, 33 ms frame wake, fps default 30, 600 ms long-press, 20 px tap), buffers (2 × 20,480 px, 60-sample rings, 2 × 64 × 64 images), task cores/priorities/stacks, LHM poll (1 s ok / 5 s back-off, 800 ms HTTP timeout), disk refresh 30 s, settings defaults, pin config, sdkconfig, partitions, WebView2 args | All match the code |
| Docs vs hardware | Measurements in section 11 were taken on the board; LCD read-back screenshots retaken 2026-10-28 with live data | Match |
| Docs vs pics | Re-viewed the purchase-notes, callout, spec-table, bundle, manual, PCB-back and box-label images | Match `pics/ANALYSIS.md`. Added: module shield "ESP-32S", CN1 = DHT11 port, P3 = extended IO, storage temperature, the listing's "download mode every flash" claim vs auto-reset working |
| Online (≥ 2 sources where possible) | ESP32 memory/BT/ULP, GPIO12 strapping, CYD pinout, CH340C rates, LVGL 9 versions and RAM, esp-hal 1.0, LovyanGFX on IDF 6, IDF 6 driver removals, esp_lcd_ili9341, Arduino_GFX, Zephyr/NuttX on ESP32, NuttX CYD board config, free-heap and Wi-Fi heap reports, LHM/PawnIO, Slint | Confirmed, with these limits: LovyanGFX on IDF 6 unconfirmed; Slint on a no-PSRAM ESP32 unconfirmed; ESP-32S vs WROOM-32 pin compatibility not confirmed online (irrelevant here: every pin used was proven on the board); exact LHM version that switched to PawnIO not found; CH340C 460,800 bps continuous figure is from a distributor, not WCH |

Facts that rest on one source only are marked as such in the text (listing-only specs such as weight, current draw and temperatures; single-user heap reports).
