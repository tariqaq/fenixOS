//! CJK bitmap fonts for the board's SD card (P54, `docs/fonts.md`). The PC draws every glyph of an
//! OpenType font (CFF or TrueType outlines) at a few pixel sizes into one `FXF1` file; the board
//! finds a glyph with two small reads (its index entry, then its record) and never rasterizes.
//!
//! Layout, little-endian:
//! - Header, 16 B: `FXF1`, version u8 (1), size count u8, 2 reserved, glyph records u32, 4 reserved.
//! - Size table, 16 B a size: px u8, line height u8, base line u8 (from the bottom), bpp u8 (4),
//!   range count u16, 2 reserved, ranges offset u32, 4 reserved.
//! - Ranges, 12 B each: first codepoint u32, count u32, index offset u32.
//! - Index: u32 per codepoint of a range, the file offset of its glyph record (0: no glyph).
//! - Glyph record: box w u8, box h u8, ofs x i8, ofs y i8 (LVGL's: the box's bottom above the
//!   base line), advance u8, 1 reserved, then w × h 4-bit coverage, rows run on, high nibble first.

use ab_glyph::{point, Font, FontRef, PxScale, ScaleFont};

pub const MAGIC: &[u8; 4] = b"FXF1";
pub const VERSION: u8 = 1;
/// The sizes the board's text fonts fall back to: 13 for label and caption (13, 12 px), 16 for
/// title and body (17, 15 px), 20 for the large title (22 px). `fenix_ui/cjk.cpp` matches them.
pub const SIZES: [u8; 3] = [13, 16, 20];
/// Codepoints drawn, first and last (inclusive). Those the font lacks get no record.
pub const RANGES: &[(u32, u32)] = &[
    (0x3000, 0x30FF), // CJK symbols and punctuation, hiragana, katakana
    (0x3130, 0x318F), // Hangul compatibility jamo
    (0x31F0, 0x31FF), // katakana phonetic extensions
    (0x4E00, 0x9FFF), // CJK unified ideographs
    (0xAC00, 0xD7A3), // Hangul syllables
    (0xFF00, 0xFFEF), // halfwidth and fullwidth forms
];
/// Codepoints a range may skip before it is split in two (index entries are 4 B).
const MAX_GAP: u32 = 64;
/// Ranges a size may have: the board reads at most this many (`MAX_RANGES` in
/// `fenix_ui/cjk.cpp`) and skips a size with more.
pub const MAX_RANGES: usize = 16;

/// The font the PC app downloads (OFL): Noto Sans CJK SC Regular, release Sans2.004. Its glyph set
/// covers Chinese, Japanese kana and Korean Hangul.
pub const NOTO_URL: &str =
    "https://github.com/notofonts/noto-cjk/raw/Sans2.004/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf";
pub const NOTO_SHA256: &str = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b";
pub const NOTO_SIZE: u64 = 16_437_364;
/// Where the board looks for it, under `/sd/fenixos`.
pub const BOARD_PATH: &str = "fonts/cjk.fxf";

/// One glyph as drawn.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Glyph {
    pub w: u8,
    pub h: u8,
    pub ofs_x: i8,
    pub ofs_y: i8,
    pub adv: u8,
    /// Coverage 0..=15, `w * h` values, rows top down.
    pub px: Vec<u8>,
}

/// Draw one codepoint at an em size of `px` pixels; `None` when the font has no glyph for it.
pub fn draw(font: &FontRef, px: u8, cp: u32) -> Option<Glyph> {
    let ch = char::from_u32(cp)?;
    let id = font.glyph_id(ch);
    if id.0 == 0 {
        return None;
    }
    let scale = em_scale(font, px);
    let adv = font
        .as_scaled(scale)
        .h_advance(id)
        .round()
        .clamp(0.0, 255.0) as u8;
    let Some(o) = font.outline_glyph(id.with_scale_and_position(scale, point(0.0, 0.0))) else {
        // A space: an advance and nothing to draw.
        return Some(Glyph {
            w: 0,
            h: 0,
            ofs_x: 0,
            ofs_y: 0,
            adv,
            px: Vec::new(),
        });
    };
    let b = o.px_bounds();
    let (w, h) = (b.width() as usize, b.height() as usize);
    if w > 255 || h > 255 {
        return None;
    }
    let mut cov = vec![0u8; w * h];
    o.draw(|x, y, c| {
        if let Some(p) = cov.get_mut(y as usize * w + x as usize) {
            *p = (c.clamp(0.0, 1.0) * 15.0 + 0.5) as u8;
        }
    });
    Some(Glyph {
        w: w as u8,
        h: h as u8,
        ofs_x: (b.min.x as i32).clamp(-128, 127) as i8,
        ofs_y: (-(b.max.y as i32)).clamp(-128, 127) as i8,
        adv,
        px: cov,
    })
}

/// ab_glyph scales so that ascent − descent is the given height; the board's sizes are em sizes,
/// as lv_font_conv draws the built-in fonts.
fn em_scale(font: &FontRef, px: u8) -> PxScale {
    let upem = font.units_per_em().unwrap_or(1000.0);
    PxScale::from(px as f32 * font.height_unscaled() / upem)
}

/// Line height and base line (from the bottom) at `px`, rounded up.
fn line_metrics(font: &FontRef, px: u8) -> (u8, u8) {
    let s = font.as_scaled(em_scale(font, px));
    let base = (-s.descent()).ceil().clamp(0.0, 255.0);
    let line = (s.ascent() - s.descent()).ceil().clamp(1.0, 255.0);
    (line as u8, base as u8)
}

/// The codepoints to draw, grouped into ranges: `RANGES`, or only `chars` when given.
fn plan(font: &FontRef, chars: Option<&str>) -> Vec<(u32, u32)> {
    let mut cps: Vec<u32> = match chars {
        Some(s) => s.chars().map(|c| c as u32).filter(|&c| c > 0x7F).collect(),
        None => RANGES.iter().flat_map(|&(a, b)| a..=b).collect(),
    };
    cps.sort_unstable();
    cps.dedup();
    cps.retain(|&cp| char::from_u32(cp).is_some_and(|c| font.glyph_id(c).0 != 0));
    let mut out: Vec<(u32, u32)> = Vec::new();
    for cp in cps {
        match out.last_mut() {
            Some((first, count)) if cp - (*first + *count) <= MAX_GAP => *count = cp - *first + 1,
            _ => out.push((cp, 1)),
        }
    }
    // Over the board's limit (scattered characters in a subset): join the two ranges with the
    // smallest gap until it fits; the gap costs 4 B of index a codepoint.
    while out.len() > MAX_RANGES {
        let i = (0..out.len() - 1)
            .min_by_key(|&i| out[i + 1].0 - (out[i].0 + out[i].1))
            .unwrap();
        let (first, next) = (out[i].0, out[i + 1]);
        out[i].1 = next.0 + next.1 - first;
        out.remove(i + 1);
    }
    out
}

fn put16(b: &mut Vec<u8>, v: u16) {
    b.extend_from_slice(&v.to_le_bytes());
}
fn put32(b: &mut Vec<u8>, v: u32) {
    b.extend_from_slice(&v.to_le_bytes());
}

/// Build an `FXF1` file from an OpenType font. `chars`: only these (a subset, as for the
/// simulator); `None`: every codepoint in `RANGES` the font has. `progress` gets 0..=100.
pub fn build(
    font_data: &[u8],
    sizes: &[u8],
    chars: Option<&str>,
    progress: &mut dyn FnMut(u8),
) -> Result<Vec<u8>, String> {
    let font = FontRef::try_from_slice(font_data).map_err(|e| format!("not a font: {e}"))?;
    if sizes.is_empty() || sizes.len() > 8 {
        return Err("1 to 8 sizes".into());
    }
    let ranges = plan(&font, chars);
    // A subset draws only its own characters, not the gaps a range spans.
    let wanted: Option<std::collections::HashSet<u32>> =
        chars.map(|s| s.chars().map(|c| c as u32).collect());
    if ranges.is_empty() {
        return Err("the font has none of the characters".into());
    }
    let per_size: u32 = ranges.iter().map(|r| r.1).sum();
    // Offsets: header, size table, then per size its ranges and index; records after all of them.
    let mut at = 16 + 16 * sizes.len() as u32;
    let mut ranges_at = Vec::new();
    let mut index_at = Vec::new();
    for _ in sizes {
        ranges_at.push(at);
        at += 12 * ranges.len() as u32;
        index_at.push(at);
        at += 4 * per_size;
    }
    let mut head = Vec::new();
    let mut index = vec![0u32; sizes.len() * per_size as usize];
    let mut records: Vec<u8> = Vec::new();
    let records_at = at;
    let mut count = 0u32;
    let total = sizes.len() as u32 * per_size;
    let mut done = 0u32;
    for (si, &px) in sizes.iter().enumerate() {
        let mut k = 0usize;
        for &(first, n) in &ranges {
            for cp in first..first + n {
                let want = wanted.as_ref().is_none_or(|w| w.contains(&cp));
                if let Some(g) = want.then(|| draw(&font, px, cp)).flatten() {
                    index[si * per_size as usize + k] = records_at + records.len() as u32;
                    records.extend_from_slice(&[g.w, g.h, g.ofs_x as u8, g.ofs_y as u8, g.adv, 0]);
                    for pair in g.px.chunks(2) {
                        records.push(pair[0] << 4 | pair.get(1).copied().unwrap_or(0));
                    }
                    count += 1;
                }
                k += 1;
                done += 1;
                if done % 2048 == 0 {
                    progress((done * 100 / total) as u8);
                }
            }
        }
    }
    head.extend_from_slice(MAGIC);
    head.push(VERSION);
    head.push(sizes.len() as u8);
    put16(&mut head, 0);
    put32(&mut head, count);
    put32(&mut head, 0);
    for (si, &px) in sizes.iter().enumerate() {
        let (line, base) = line_metrics(&font, px);
        head.extend_from_slice(&[px, line, base, 4]);
        put16(&mut head, ranges.len() as u16);
        put16(&mut head, 0);
        put32(&mut head, ranges_at[si]);
        put32(&mut head, 0);
    }
    for si in 0..sizes.len() {
        debug_assert_eq!(head.len() as u32, ranges_at[si]);
        let mut k = 0u32;
        for &(first, n) in &ranges {
            put32(&mut head, first);
            put32(&mut head, n);
            put32(&mut head, index_at[si] + 4 * k);
            k += n;
        }
        for &off in &index[si * per_size as usize..(si + 1) * per_size as usize] {
            put32(&mut head, off);
        }
    }
    debug_assert_eq!(head.len() as u32, records_at);
    head.extend_from_slice(&records);
    progress(100);
    Ok(head)
}

/// A glyph read back from an `FXF1` file, as the board reads it (for tests and `fenix font`).
pub fn read(file: &[u8], px: u8, cp: u32) -> Option<Glyph> {
    let u8at = |o: usize| file.get(o).copied();
    let u16at = |o: usize| Some(u16::from_le_bytes(file.get(o..o + 2)?.try_into().ok()?));
    let u32at = |o: usize| Some(u32::from_le_bytes(file.get(o..o + 4)?.try_into().ok()?));
    if file.get(..4)? != MAGIC || u8at(4)? != VERSION {
        return None;
    }
    let sizes = u8at(5)? as usize;
    let s = (0..sizes)
        .map(|i| 16 + 16 * i)
        .find(|&o| file.get(o) == Some(&px))?;
    let (nranges, ranges_at) = (u16at(s + 4)? as usize, u32at(s + 8)? as usize);
    let off = (0..nranges).find_map(|i| {
        let r = ranges_at + 12 * i;
        let (first, n, idx) = (u32at(r)?, u32at(r + 4)?, u32at(r + 8)?);
        (cp >= first && cp < first + n).then(|| u32at(idx as usize + 4 * (cp - first) as usize))?
    })? as usize;
    if off == 0 {
        return None;
    }
    let (w, h) = (u8at(off)?, u8at(off + 1)?);
    let n = w as usize * h as usize;
    let packed = file.get(off + 6..off + 6 + n.div_ceil(2))?;
    let px = (0..n)
        .map(|i| {
            if i % 2 == 0 {
                packed[i / 2] >> 4
            } else {
                packed[i / 2] & 0xF
            }
        })
        .collect();
    Some(Glyph {
        w,
        h,
        ofs_x: u8at(off + 2)? as i8,
        ofs_y: u8at(off + 3)? as i8,
        adv: u8at(off + 4)?,
        px,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    // Inter (in the repo, OFL) has Latin and a few marks, no CJK: enough for the format.
    fn inter() -> Vec<u8> {
        let p = concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../design/fonts/inter/Inter-Regular.ttf"
        );
        std::fs::read(p).expect("design/fonts/inter/Inter-Regular.ttf")
    }

    #[test]
    fn round_trip() {
        let data = inter();
        let font = FontRef::try_from_slice(&data).unwrap();
        // ° (0xB0), × (0xD7), – (0x2013), … (0x2026): above ASCII, with gaps between them.
        let chars = "°×–…";
        let file = build(&data, &[13, 20], Some(chars), &mut |_| {}).unwrap();
        assert_eq!(&file[..4], MAGIC);
        for c in chars.chars() {
            for px in [13, 20] {
                let want = draw(&font, px, c as u32).unwrap();
                assert_eq!(read(&file, px, c as u32), Some(want), "{c} at {px}");
            }
        }
        // Not drawn: a codepoint inside a range (between – and …), one outside, a missing size.
        assert_eq!(read(&file, 13, 0x2014), None);
        assert_eq!(read(&file, 13, 0x4E00), None);
        assert_eq!(read(&file, 16, 0xB0), None);
    }

    #[test]
    fn metrics_look_right() {
        let data = inter();
        let font = FontRef::try_from_slice(&data).unwrap();
        // × at 20 px: a few px wide, on the base line's upper side, advance about 0.6 em.
        let g = draw(&font, 20, 0xD7).unwrap();
        assert!(g.w > 4 && g.w < 16 && g.h > 4 && g.h < 16, "{g:?}");
        assert!(g.ofs_y >= 0 && g.adv >= 9 && g.adv <= 14, "{g:?}");
        assert!(g.px.iter().any(|&v| v == 15));
        let (line, base) = line_metrics(&font, 20);
        assert!(
            (22..=27).contains(&line) && (3..=7).contains(&base),
            "{line} {base}"
        );
    }

    #[test]
    fn ranges_split_at_big_gaps() {
        let data = inter();
        let font = FontRef::try_from_slice(&data).unwrap();
        let r = plan(&font, Some("°×–…"));
        // ° and × are 0x27 apart (one range); – is far away; … is 0x13 after – (same range).
        assert_eq!(
            r,
            vec![(0xB0, 0xD7 - 0xB0 + 1), (0x2013, 0x2026 - 0x2013 + 1)]
        );
        // Characters far apart: never more ranges than the board reads, and all still covered.
        let spread: String = (0..40)
            .filter_map(|i| char::from_u32(0x41 + 0x80 + i * 100))
            .filter(|&c| font.glyph_id(c).0 != 0)
            .collect();
        let r = plan(&font, Some(&spread));
        assert!(r.len() <= MAX_RANGES, "{}", r.len());
        for c in spread.chars() {
            let cp = c as u32;
            assert!(r.iter().any(|&(f, n)| cp >= f && cp < f + n), "{c}");
        }
    }
}
