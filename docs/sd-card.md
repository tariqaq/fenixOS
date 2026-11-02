# The SD card (`/sd/fenixos`)

fenixOS keeps everything it writes on the card under `/sd/fenixos`. The card is FAT32, mounted at `/sd` (P50, `docs/board-info.md` "SD card check"). Nothing outside that folder is written, except that installing a firmware package from the card's root moves it into `firmware/` (`docs/ota.md`).

| Folder | What | Written by | Since |
|---|---|---|---|
| `apps/` | App packages `<id>.fxp` and `<id>.sha` | App installs (`docs/packages.md` "Where apps live") | P51, v0.1.21 |
| `firmware/` | Kept firmware packages `<v>.fwp`, a staged `<v>.part` | Firmware updates (`docs/ota.md`) | P52, v0.1.22 |
| `logs/` | The board log, `<n>.txt` per boot | The board | P53, v0.1.23 |
| `crash/` | Crash records `<n>.elf` and `<n>.txt` | The board, at the next mount after a crash | P53 |
| `fonts/` `data/` `wallpaper/` | Files the PC puts there | The PC (`fenix files put`) | P53 |
| `shots/` `state/` `backup/` `web/` | Screenshots, last app values, settings backup, status page | Planned (P53 steps 3 to 7, `tasks.md`) | |

## Files over the link (P53 step 1)

Messages: `proto/spec.md` "Files (0x6_)". Board: `board/components/fenix_sys/files.cpp`. PC: `crates/fenix-link/src/files.rs` (list, read, write) and `chunks.rs` (the window sender firmware updates use too).

- **What the PC may do.**
  - List and read: anything under `/sd/fenixos`.
  - Delete: inside `fonts/ data/ wallpaper/ logs/ crash/ shots/`; a folder only when it is empty.
  - Write: inside `fonts/ data/ wallpaper/`. Folders on the way are made.
- **Paths** are relative to `/sd/fenixos`, split by `/`, shorter than 128 bytes. No part may be empty, `.` or `..`, or end in a dot or a space (FAT drops those, so two names would mean one file). `\ : * ? " < > |` are refused.
- **A write** goes to `<path>.part`, with `<path>.pin` (size and sha256) beside it. After `FILE_END` the board reads the part back in a short task, checks the sha256, then puts it in place of any old file. A cut write goes on from where it stopped when the same file (same size and sha256) is sent again. A damaged one is deleted.
- **Timeouts.** A write or read with no message for 10 s is closed, and so is one whose link dropped. The part stays on the card.
- **CLI:** `fenix files ls [path]`, `get <path> [out]`, `put <local> <path>`, `rm <path>`.

Measured numbers: `docs/board-info.md` "Files over the link".

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
