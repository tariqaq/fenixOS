# Fonts

The board's text fonts are Inter (Latin, a few marks), built into the firmware by
`tools/gen_fonts.py`. Chinese, Japanese and Korean text comes from a bitmap font file on the SD
card (P54, v0.1.24): `/sd/fenixos/fonts/cjk.fxf`.

## How the board uses it

- `board/components/fenix_ui/cjk.cpp` defines three LVGL fonts, `fx_cjk_13`, `fx_cjk_16` and
  `fx_cjk_20`. Each built-in text font names one as its `fallback` (gen_fonts.py writes it):

  | Built-in font | Size | CJK fallback |
  |---|---|---|
  | `fx_font_title_l` | 22 | `fx_cjk_20` |
  | `fx_font_title`, `fx_font_body`, `fx_font_body_semibold` | 17, 15 | `fx_cjk_16` |
  | label and caption fonts | 13, 12 | `fx_cjk_13` |
  | clock fonts | 64, 56 | none (digits only) |

- LVGL asks the fallback only for characters Inter lacks. A glyph is two small reads: its index
  entry, then its record (box, offsets, advance, 4-bit bitmap). The board never rasterizes outlines.
- The file stays open (one of the card's 6 file slots). The last 32 glyphs used stay in RAM
  (32 slots of 240 B, least recently used goes first). LVGL draws the screen in bands and asks for
  every glyph of a label in each band, so a direct-mapped cache missed 90 times for 20 glyphs; the
  LRU cache reads each glyph once while the screen shows at most 32 of them. Nothing is allocated
  when there is no font.
- The UI opens it when the card is mounted (`SD_CHANGED`) and again after the PC writes or deletes
  a file in `fonts/` (`FONTS`, posted by files.cpp). Before files.cpp replaces or deletes a file
  there, and before the card is unmounted, it calls `sys::fonts_release()`, which closes the font
  under the font's mutex.
- No card or no font: the fallback finds nothing, and text looks as before (missing characters
  draw nothing).
- FatFs fast seek is on (`CONFIG_FATFS_USE_FASTSEEK`): a file opened for reading gets a cluster
  map, so a seek in the 12.5 MB file does not walk the FAT.

## Making and installing it

The PC app installs it from the Board page (P54 step 3). By hand:

```
fenix font make NotoSansCJKsc-Regular.otf cjk.fxf
fenix files put cjk.fxf fonts/cjk.fxf
```

- `fenix font make <font> <out.fxf> [--chars <text>]` draws every codepoint in the ranges below
  that the font has, at 13, 16 and 20 px. `--chars` draws only the given characters (the
  simulator's `board/sim/fonts/cjk-sample.fxf` is made that way).
- The source font is Noto Sans CJK SC Regular (OFL-1.1), release Sans2.004:
  `https://github.com/notofonts/noto-cjk/raw/Sans2.004/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`,
  16,437,364 B, sha256 `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
  (`fenix_font::NOTO_*`). Its glyph set covers Chinese, kana and Hangul. Chinese glyphs have the
  simplified forms.
- Ranges: U+3000 to U+30FF (CJK punctuation, hiragana, katakana), U+3130 to U+318F (Hangul
  jamo), U+31F0 to U+31FF, U+4E00 to U+9FFF (CJK ideographs), U+AC00 to U+D7A3 (Hangul
  syllables), U+FF00 to U+FFEF (full-width forms).
- Measured (2026-11-02): 98,337 glyphs in 13,154,755 B (12.5 MB), made in 0.5 s on the PC.

## Measured on the board (v0.1.24, 2026-11-02)

| What | Result |
|---|---|
| Firmware | +6.8 KB (`cjk.cpp`, fast seek) |
| Free heap at idle | 62.9 KB without a font, 52.8 KB with it open (the 32 slots, the open file and its FatFs buffers, the fast-seek map) |
| A glyph read from the card | ~3.1 ms (index entry, then record), once per glyph while it stays among the last 32 |
| 20-character title, first draw | ~62 ms more than Latin text; redraws come from RAM |
| `fenix files put` of the 12.5 MB file over USB | 184 s (~71 KB/s), sha256 read-back included |
| PC app Install (download cached, drawn, sent) | download 16 MB < 8 s here; sending as above |

## File format FXF1

Little-endian. Made by `crates/fenix-font` (`build`; `read` reads a glyph back as the board does).

| Part | Layout |
|---|---|
| Header, 16 B | `FXF1`, version u8 (1), size count u8 (1 to 8), 2 reserved, glyph records u32, 4 reserved |
| Size table, 16 B a size | px u8, line height u8, base line u8 (from the bottom), bpp u8 (4), range count u16, 2 reserved, ranges offset u32, 4 reserved |
| Ranges, 12 B each | first codepoint u32, count u32, index offset u32 |
| Index | a u32 per codepoint of a range: the file offset of its glyph record, 0 when the font has none |
| Glyph record | box w u8, box h u8, ofs x i8, ofs y i8, advance u8, 1 reserved, then w × h 4-bit coverage values, rows run on, high nibble first |

- `ofs y` is LVGL's: the box's bottom above the base line (negative: below).
- Sizes are em sizes (as lv_font_conv draws Inter), so a 16 px CJK glyph is about 16 px wide.
- A range skips at most 64 codepoints the font lacks; a bigger gap starts a new range. The board
  reads up to 16 ranges a size.
- The board uses only 13, 16 and 20 px, 4 bpp. Other sizes in a file are skipped.
