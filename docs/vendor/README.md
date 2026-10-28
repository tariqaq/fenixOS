# Vendor files (Guition)

Source: `https://pan.jczn1688.com/1#/` (GUITION-DISK, the link printed in the board manual). Downloaded 2026-10-28.

| Downloaded | Server path | Size |
|---|---|---|
| `archives/2.8inch_ESP32-2432S028R.rar` | `ESP32 module/2.8inch_ESP32-2432S028R.rar` | 252,636,403 B |
| `archives/USB-SERIAL CH340.rar` | `TOOL/USB-SERIAL CH340.rar` | 250,569 B |
| (panel spec, duplicate of the copy inside the board package) | `Bare screen/JC2432A028N.pdf` | 1,889,104 B |

Direct-download page pattern: `https://pan.jczn1688.com/directlink/1/<server path>?lang=en`. Files come in shards and are merged in the browser, so download them through Chrome.

## Layout

- `CH340/`: CH340/CH341 USB-serial driver (`CH341SER.EXE`).
- `ESP32-2432S028R/2.8inch_ESP32-2432S028R/`:
  - `1-Demo/Demo_Arduino/`: Arduino example sketches (3_x basics, 4_x Wi-Fi, 5_x BLE, 6_x SD, 7_x touch, 8_x buzzer).
  - `1-Demo/Demo_LVGL(idf-4.3)/`: LVGL demo for ESP-IDF 4.3. **Local only.**
  - `2-Specification/`: board spec PDF.
  - `3-Structure_Diagram/`: board dimensions.
  - `4-Driver_IC_Data_Sheet/`: ESP32-WROOM-32, 25Q32 flash, FM8002A audio amp, JC2432A028N panel, DHT11.
  - `5-Schematic/`: two schematic sheets (JPG, V0.1, 2022) and a WROOM pin-definition PNG. **The file names do not match the content:** `ESP32-2432S028-MCU.jpg` holds power (P1, two AMS1117), USB-to-TTL (CH340C, auto-reset T1/T2), ADC (LDR on IO34), RGB LED, CN1, microSD and the U4 flash footprint. `ESP32-2432S028-LCM.jpg` holds the ESP32-WROOM-32 (U2) with RST/BOOT and P3, XPT2046 touch (U3), the 18-pin LCM connector with backlight MOSFET, and audio (SC8002B, P4).
  - `6-User_Manual/`: "Getting started 2.8 Inch" PDF.
  - `7-Character&Picture_Molding_Tool/`: third-party Windows tools (image converter, WinHex, serial/network tools). **Local only.**
  - `8-Burn operation/`: prebuilt `LVGL-DEMO2.8R.bin` and flashing steps. Espressif `flash_download_tool_3.9.3` is **local only**.

## Not in git

These paths are in `.gitignore`: too large for GitHub, or third-party binaries. They stay on the local disk. To restore them, re-download the rar and extract it with `7z x`.

- `archives/` (241 MB rar)
- `1-Demo/Demo_Arduino/libraries/` (346 MB): vendor copies of TFT_eSPI, LovyanGFX, lvgl, TJpg_Decoder, ESPAsyncWebServer, ESP8266Audio, etc.
- `1-Demo/Demo_LVGL(idf-4.3)/` (482 MB)
- Large demos: `4_8`, `4_9`, `4_10`, `4_11`, `9_1`
- `7-Character&Picture_Molding_Tool/`, `8-Burn operation/flash_download_tool_3.9.3/`

## Key facts from the vendor config

From `libraries/TFT_eSPI/User_Setup.h`:

| Setting | Value |
|---|---|
| Display driver | `ILI9341_2_DRIVER`, 240x320 |
| SPI pins | MOSI 13, SCLK 14, CS 15, DC 2, RST 12 (**wrong**, see below) |
| Backlight | GPIO 21, active HIGH |
| Touch CS (XPT2046) | GPIO 33 |
| SPI frequency | 55 MHz |

**Correction (from the schematic and the vendor LovyanGFX demo):** IO12 is the LCD MISO, and LCD reset is tied to EN. Use `TFT_MISO 12` and `TFT_RST -1`. Touch is on a separate SPI bus (CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36), so TFT_eSPI's built-in `TOUCH_CS` touch will not work. The panel spec (`JC2432A028N.pdf`) confirms the ILI9341V driver. The full pin map is in `pics/ANALYSIS.md`.

## Vendor LovyanGFX demo (`7_1_Touch_button_ILI9341_LovyanGFX`)

The LCD and backlight settings are right, and they match what FenixDeck proved on the board: HSPI, SCLK 14, MOSI 13, MISO 12, DC 2, CS 15, RST -1, write 55 MHz, read 20 MHz, `invert = false`, `rgb_order = false`, backlight 21 at 44.1 kHz. Touch: VSPI, 2.5 MHz, SCLK 25, MOSI 32, MISO 39, CS 33, INT 36.

Two touch settings are wrong for this board (checked 2026-10-29):

| Setting | Vendor demo | Use instead | Why |
|---|---|---|---|
| `x_min`/`x_max`, `y_min`/`y_max` | 0–239, 0–319 | x 300–3900, y 200–3900 (or run `calibrateTouch`) | These are **raw** XPT2046 values (12-bit, 0–4095), not pixels. The demo never calls `calibrateTouch`, so its mapping is wrong. FenixDeck ran 300–3900 / 200–3900 on this board with no stored calibration (NVS has no `tcal` key), see `docs/board-info.md` |
| touch `bus_shared` | `true` | `false` | Touch has its own SPI host (VSPI). `true` only adds bus locking; it is not needed |
