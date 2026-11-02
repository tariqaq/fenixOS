# The SD card (`/sd/fenixos`)

fenixOS keeps everything it writes on the card under `/sd/fenixos`. The card is FAT32, mounted at `/sd` (P50, `docs/board-info.md` "SD card check"). Nothing outside that folder is written, except that installing a firmware package from the card's root moves it into `firmware/` (`docs/ota.md`).

| Folder | What | Written by | Since |
|---|---|---|---|
| `apps/` | App packages `<id>.fxp` and `<id>.sha` | App installs (`docs/packages.md` "Where apps live") | P51, v0.1.21 |
| `firmware/` | Kept firmware packages `<v>.fwp`, a staged `<v>.part` | Firmware updates (`docs/ota.md`) | P52, v0.1.22 |
| `logs/` | The board log, `<n>.txt` per boot | The board | P53, v0.1.23 |
| `crash/` | Crash records `<n>.elf` and `<n>.txt` | The board, at the next mount after a crash | P53 |
| `fonts/` `data/` `web/` | Files the PC puts there | The PC (`fenix files put`) | P53 |
| `shots/` | Screenshots `<time>.bmp` | The board, when the PC asks | P53 |
| `state/` | Last bound values per app, `<id>.bin` | The board | P53 |
| `backup/` | `settings.bin`: settings, the Wi-Fi network, the weather place | The board | P53 |

## Files over the link (P53 step 1)

Messages: `proto/spec.md` "Files (0x6_)". Board: `board/components/fenix_sys/files.cpp`. PC: `crates/fenix-link/src/files.rs` (list, read, write) and `chunks.rs` (the window sender firmware updates use too).

- **What the PC may do.**
  - List and read: anything under `/sd/fenixos`.
  - Delete: inside `fonts/ data/ web/ logs/ crash/ shots/`; a folder only when it is empty.
  - Write: inside `fonts/ data/ web/`. Folders on the way are made.
- **Paths** are relative to `/sd/fenixos`, split by `/`, shorter than 128 bytes. No part may be empty, `.` or `..`, or end in a dot or a space (FAT drops those, so two names would mean one file). `\ : * ? " < > |` are refused.
- **A write** goes to `<path>.part`, with `<path>.pin` (size and sha256) beside it. After `FILE_END` the board reads the part back in a short task, checks the sha256, then puts it in place of any old file. A cut write goes on from where it stopped when the same file (same size and sha256) is sent again. A damaged one is deleted.
- **Timeouts.** A write or read with no message for 10 s is closed, and so is one whose link dropped. The part stays on the card.
- **CLI:** `fenix files ls [path]`, `get <path> [out]`, `put <local> <path>`, `rm <path>`.

Measured numbers: `docs/board-info.md` "Files over the link".

## Screenshots (P53 step 3)

- **Asking.** From the PC only: Board › "Screenshot to the card" in the PC app (shown when a card is in), or `fenix shot --sd [out.bmp]`. The message is `SHOT_SAVE`.
- **Saving.** The UI task reads the panel back over MISO, 8 rows at a time (5 KB), and writes a 16-bit BMP: RGB565 bitfields, rows top to bottom, 153,666 B at 320 x 240. Most viewers open it; Paint and Photos do.
- **Name:** `shots/YYYYMMDD-HHMMSS.bmp` in local time, `-2` to `-9` for more in the same second, or `shots/up-<seconds>.bmp` before the clock is set.
- **What it shows** is the glass, not what LVGL meant to draw. `fenix shot` (the stream) is the other way round.
- **The PC app** copies each shot to `%LOCALAPPDATA%\fenixOS\shots`; "Show" opens that folder. Older shots: `fenix files ls shots`, `fenix files get shots/<name>`. They stay on the card until deleted (`fenix files rm`).

## Last app values (P53 step 4)

Board: `board/components/fenix_apps/app_pkg.cpp` (the format), `fenix_sys/files.cpp` (the file).

- **Saved** when an app closes, and every 60 s while it is open and a value changed. Values the PC sets and controls the user moves both count. The file is written as `<id>.tmp`, then put in place, so a cut write leaves the old one.
- **Format:** `FXS1`, a count, then per set binding: its name (length byte + bytes), the kind, the value (i32 LE) and the text (length byte + up to 255 bytes). At most 4 KB. Values are matched by binding name, so an app update keeps the values of the bindings it still has.
- **Loaded** when the app opens, PC or not; values from the PC replace them as they come.
- **With no PC:** the layout shows the values under "PC not connected", behind a 50 % veil, and takes no taps (it still scrolls). With no values saved the screen is 6a "Waiting for host PC" as before. When the PC connects, the app screen is rebuilt live.
- **Removed** with the app (while the card is in).
- **Cost:** 2.1 KB of firmware; a 4 KB buffer while the app opens.

## Settings backup (P53 step 5)

Board: `board/components/fenix_sys/backup.cpp`; the sheet is in `fenix_apps/session.cpp`.

- **What:** `backup/settings.bin` holds the settings blob as NVS keeps it, the saved Wi-Fi network with its password **in plain text** (the user's choice), and the weather place. Format: `FXB1`, then records (tag, u16 length, bytes): 1 settings, 2 network name, 3 password, 4 place (lat and lon x 10^4 as i32 LE, then the name). Unknown tags are skipped.
- **Written** by the link task 3 s after the last change (a slider sends many), and at every mount. The file is written as `settings.tmp`, then put in place.
- **Offered** when a boot found no settings and no Wi-Fi network in NVS (a new or erased board) and the card has a backup. The sheet "Restore settings?" shows on Home and comes back after any rebuild until it is answered; nothing is written to the card until then.
  - **Restore** puts the settings, network and place in NVS and restarts. A board that was calibrated before the offer keeps its own touch calibration; otherwise the backup's comes back too.
  - **Start fresh** saves the board's current settings, so it is not asked again; the card's backup is then replaced 3 s later.
- **An empty board with no backup on its card** saves its settings at the mount, so a later boot does not take it for an erased one.
- **Tested** (2026-11-02): NVS erased with `fenix erase 0x9000 0x6000`; the next boot offered the backup (network "theworld"); Restore restarted the board, Wi-Fi came up with the restored network, and the backup on the card stayed byte-identical.
- **Cost:** 2.9 KB of firmware; nothing kept in RAM but a few flags.

## Status page from the card (P53 step 6)

Board: `board/components/fenix_sys/web.cpp`. In Wi-Fi mode the board serves `http://fenixos.local/`.

- **Files in `web/` come first.** `/` is `web/index.html`, `/a/b.css` is `web/a/b.css`, a path ending in `/` means its `index.html`. With no card, or no such file, `/` is the built-in page and anything else is 404.
- **`/status.json` stays built in**, so a page on the card shows live state the way the built-in one does. Fields: `version`, `uptime` (s), `heap` (B), `apps` (KB used in flash), `pc` (bool), `time` (`HH:MM`, empty before the clock is set), `date`, `weather`, `ssid`, `rssi`, `ip`.
- **Putting a page there:** `fenix files put index.html web/index.html`. Paths with `..`, `\` or `%` are refused (no decoding: names on the card are plain). Types follow the extension (html, css, js, json, svg, png, jpg, gif, ico, webp, woff2, txt). Files go out in 1 KB chunks with `Cache-Control: no-cache`.
- **Still status only:** the server takes GET only, so a page on the card can show things but change nothing on the board.
- **Measured** (2026-11-02): a 405 KB file in 1.4 to 1.9 s (about 210 KB/s); `..` (sent as is), `%` and missing files get 404; a query string is ignored. Heap low point with a link session over Wi-Fi: 44.6 KB with one download, 35.9 KB with three at once (lwIP buffers per socket; the server allows 3). Cost: 1.5 KB of firmware.

## Board log (P53 step 2)

Board: `board/components/fenix_sys/logs.cpp`.

- **Which lines.** Every `link_log` line (what the PC also gets as `LOG`), plus lines for the card only (`logs_note`):
  - PC connected or disconnected, and over which link.
  - App installs and removals.
  - The card mounted or removed.
  - Wi-Fi up, with the network, address and signal.
  - The heap every 15 min.
- **File.** One file per boot, `logs/<n>.txt`. `n` is one more than the highest number on the card. The 10 newest are kept, at most 256 KB each; after that one last line says the log is full. The first line names the firmware, the boot number, the reset reason and the heap.
- **Line format:** `<seconds since boot> <HH:MM:SS or --:--:--> <E|W|I|D> <text>`, for example `17.742 08:12:37 I PC disconnected`. The clock shows once the time is set (SNTP or the PC).
- **When it is written.** Lines wait in a 512 B buffer, which the link task writes every 5 s, and at once after an error or a warning. Lines from before the card mounted wait in the buffer too. When the buffer is full, later lines are counted and the file says how many were dropped. A card swapped while running starts a new file.
- **Reading it:** `fenix logs --board [n]` prints the list, any crash summaries and log `n` (default: the newest). In the PC app: Logs › On the card.

## Crash records (P53 step 2)

- **Writing.** At a panic, IDF writes an ELF core dump to the 56 KB `coredump` partition (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`, `board/sdkconfig.defaults`).
- **Copying.** At the next mount, `logs.cpp` copies it to `crash/<n>.elf` and writes a summary to `crash/<n>.txt`: the task, the PC, eight backtrace addresses and the firmware version. It then erases the dump from flash. `n` is the boot that found it; the crash was in the boot before. The summary also goes out as an error `LOG` and into the log.
- **Measured.** A test crash in the link task made a 26,564 B dump. Its backtrace decoded to the right line with `xtensa-esp32-elf-addr2line -pfiaC -e board/build/fenixos.elf <addresses>`. A full dump also opens with `idf.py coredump-info --core <n>.elf` against the same build's ELF.
- **Without a card** the dump stays in flash until a card is mounted.
- **Kept:** the 10 newest records.
