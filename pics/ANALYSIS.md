# Pics analysis

All 17 images cover one purchase: an **ESP32-2432S028R "Cheap Yellow Display" (CYD)**, the dual-USB revision (PCB V1.4), in a clear acrylic case.

Revision 2 (2026-10-28): adds 9 store screenshots and cross-checks against the vendor schematic (`docs/vendor/.../5-Schematic/`), the panel spec (`JC2432A028N.pdf`) and the vendor demo configs. See **Corrections** below.

## Confirmed hardware

| Item | Value | Confirmed by |
|---|---|---|
| Board | ESP32-2432S028R, PCB `V1.4` | silkscreen, box label, listing |
| MCU | ESP32-WROOM-32 class (module shield marked **ESP-32S**; listing art says ESP-WROOM-32), chip **ESP32-D0WD-V3 rev 3.1** (listing says D0WDQ6), **dual core**, 240 MHz, 520 KB SRAM, 4 MB flash | esptool read-out (`docs/board-info.md`), listing, schematic |
| Display | 2.8" TFT, 240x320, 65K colours, **ILI9341V** | panel spec PDF, listing, vendor configs |
| Touch | Resistive, **XPT2046** (stylus recommended, finger is weak) | schematic, listing |
| USB-UART | CH340C, DTR/RTS auto-reset (T1/T2 S9013) | schematic |
| Power | 5V, about 115 mA, two AMS1117-3.3 LDOs (ESP rail and TFT rail) | listing, schematic |
| USB | micro-USB and USB-C; **both can flash** | listing, PCB |

## Pin map (schematic + vendor LovyanGFX demo)

| Function | Pins |
|---|---|
| LCD SPI (HSPI) | SCK 14, MOSI 13, **MISO 12**, CS 15, DC 2, **RST = EN (-1)** |
| Backlight | **GPIO21**, via AO3402 MOSFET, active HIGH (PWM OK) |
| Touch SPI (separate bus) | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| microSD (VSPI) | CS 5, MOSI 23, CLK 18, MISO 19 |
| RGB LED (common anode, **active LOW**) | IO4, IO16, IO17 |
| Light sensor (LDR GT36516) | IO34 (ADC) |
| Speaker amp (SC8002B), P4 | IO26 (DAC) |
| BOOT button | IO0 |
| P1 (JST 1.25 4P) | VIN, TX, RX, GND (UART0) |
| P3 (JST 1.25 4P) | GND, IO35, IO22, IO21 |
| CN1 (JST 1.25 4P) | GND, IO22, IO27, 3.3V (listing: "temperature and humidity interface", i.e. a DHT11 port) |

Free GPIO for projects: **IO22, IO27, IO35 (input only)**. Do not use IO21 on P3: it drives the backlight.

## Corrections vs revision 1

1. **U3 and U6 were swapped.** U3 (small TSSOP-16 by BOOT) is the **XPT2046 touch** controller. U6 (SOP-16 by the USB ports) is the **CH340C**.
2. **U4 is a 25Q32 external-flash footprint**, left unpopulated. It is not missing a part. The module has its own 4 MB flash.
3. **Dual core, not single core.** The manual's "Single Core" line is wrong. esptool reads an ESP32-D0WD-V3 rev 3.1, dual core 240 MHz (the listing's D0WDQ6 is also outdated).
4. **Display driver settled: ILI9341.** The panel spec says ILI9341V. The ST7789 worry from community posts does not match this panel. Verified on hardware: colours and orientation correct with `invert=false`, `rgb_order=false`.
5. **The vendor TFT_eSPI `User_Setup.h` has one wrong pin.** It sets `TFT_RST 12`, but IO12 is the LCD MISO and reset is tied to EN. Use `TFT_MISO 12`, `TFT_RST -1`. The vendor LovyanGFX demo has it right.
6. **Touch is on its own SPI bus** (25/32/39/33), not shared with the LCD. TFT_eSPI's built-in touch (`TOUCH_CS`) will not work. Use the `XPT2046_Touchscreen` library on a second SPIClass, or use LovyanGFX.

## Schematic caveat

The vendor schematic is `V0.1` (2022), drawn for the original micro-USB-only board. Your board is `V1.4`, with USB-C added and CN1 changed to GND/IO22/IO27/3.3V. The core pin map matches the V1.4 silkscreen and the demos. Treat the schematic as close but not exact. (The two sheet file names are swapped relative to their content; see `docs/vendor/README.md`.)

### Resistor markings on the V1.4 PCB (2026-10-29, zoomed `20261028_190554.jpg`)

| Ref | Marking | Value on V1.4 | Schematic V0.1 | Note |
|---|---|---|---|---|
| R18 (by LED1, next to CN1) | `103` | 10 kΩ | 10 kΩ pull-up, 3.3V to IO27 (CN1 data) | **Matches.** The DHT11 data pull-up is on the board |
| R19, R15 (by P3) | `501` | 500 Ω | 1 MΩ each: LDR divider on IO34 | **Differs** |
| R16, R17 (by P3) | `101` | 100 Ω | 1 kΩ: RGB LED | **Differs** |
| R20 (by P3) | `201` | 200 Ω | 1 kΩ: RGB LED | **Differs** |

On V1.4, R15/R16/R17/R19/R20 sit in a row between the module and P3, and their values do not match V0.1. The photo alone cannot show what they connect to, so the V1.4 RGB LED current and the IO34 light-sensor circuit are **unverified**. Measure them, or test the LDR on IO34 in firmware, before relying on them.

## Per image

### Photos (2026-10-28)
- `20261028_185710.jpg` (**local only**, not in git: personal order data): Pinduoduo packing slip. Paid 2026-10-23, 2 items `L0496-1` and `L0496`, total 58, sent via a forwarder warehouse in Dongguan.
- `20261028_185833.jpg`: manual.
  - Use 5V/1A and install the CH340 driver.
  - Use a USB-A to USB-C cable, not C-to-C (the USB-C port likely has no CC resistors).
  - Docs: `pan.jczn1688.com/1`.
- `20261028_190048.jpg`, `20261028_190545.jpg`, `20261028_190554.jpg`: PCB back. All parts as in the pin map above. Guition logo, `TF V1.4`.
- `20261028_191605.jpg`: box label. SKU `10101001 (2641)`, `ESP32-2432S028R`, 240*320, resistive.
- `20261028_192349.jpg`: front in the acrylic case, screen unpowered.
- `20261028_192357.jpg`: back of the case, with cutouts for P4, RST/BOOT, P3 and CN1.

### Store listing A (2026-10-23, brand SCVSTEM, ¥53 / RM32.79, 1037 sold)
- `Screenshot_20261023_151143.jpg`: hero image (LVGL demo UI), ships from Guangdong.
- `Screenshot_20261023_151205.jpg`: purchase notes.
  - ESP-WROOM-32, 520 KB SRAM, 4 MB flash.
  - No AI-assistant support. **No microphone, no speaker included** (speaker header only).
  - **No battery support.** The 1.25 mm port is UART.
  - Resistive screen, so use the stylus.
  - Type-C and micro-USB can both flash. A micro-USB cable is included.
  - Docs folder 5 = schematic, folder 6 = user manual.
  - **Manual download mode:** hold BOOT, press and release RESET, then release BOOT. The listing says every flash needs it; on this board auto-reset worked every time, so it is only a fallback.
- `Screenshot_20261023_151209.jpg`: ESP32-D0WDQ6, dual core 240 MHz, SDIO/SPI/UART, auto download.
- `Screenshot_20261023_151219.jpg`: board callouts (RGB LED, extended IO, temp/humidity port, TF, Micro-USB, Type-C, 4P 1.25 power/UART, speaker, RESET/BOOT, ESP-WROOM-32). Spec: ILI9341, 320*240.
- `Screenshot_20261023_151228.jpg`: spec table.
  - 65K colour, viewable area 43.2x57.6 mm, module 50x86 mm, viewing angle >60°.
  - Runs at -20 to 70 °C (storage -30 to 80 °C), 5V, 115 mA, weighs 50 g.
  - Arduino IDE / MicroPython / Mixly / Scratch 3.0.
- `Screenshot_20261023_151233.jpg`: marketing for remote IoT and a drag-and-drop phone app.
- `Screenshot_20261023_151238.jpg`: app widget list (Blynk-style) and price notes. Not needed.

### Store listing B (2026-10-28, flagship store, ¥65 / RM40.25, 1086 sold)
- `Screenshot_20261028_194542.jpg` (English) and `Screenshot_20261028_194545.jpg` (Chinese): the same page.
  - Bundle: board, **acrylic case, USB cable, 4-pin JST 1.25 cable, stylus**.
  - Free shipping to Malaysia.
  - This matches the case in your photos. Check the box for the stylus and the JST cable.

## First flash (done 2026-10-28)
- The CH340 driver (`docs/vendor/CH340/CH341SER.EXE`, local only) is installed; the board shows up as `COM9`. Corrected (2026-10-29): updated to 4.0.2026.2 after the old one blue-screened the PC (`docs/board-info.md`).
- The stock firmware (vendor LVGL demo) was backed up before flashing: `firmware/stock/` (local only), restore steps in `docs/board-info.md`.
- The board then ran the FenixDeck firmware. Since 2026-10-29 it runs fenixOS (`docs/board-info.md`).
