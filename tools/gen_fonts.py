"""Generate the board's LVGL bitmap fonts (Inter text, Phosphor icons) with lv_font_conv.

Usage: python tools/gen_fonts.py
Needs Node (npx downloads lv_font_conv 1.5.3 on first use).
Outputs: board/components/fenix_ui/fonts/*.c and include/fenix_fonts.h.
Sizes come from DESIGN.md (Type, Icons) and the board artboards.
"""
import pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONTS = ROOT / "design/fonts"
OUT = ROOT / "board/components/fenix_ui/fonts"
HDR = ROOT / "board/components/fenix_ui/include/fenix_fonts.h"
CONV = "lv_font_conv@1.5.3"

# Basic Latin, Latin-1 Supplement (P51-M: accented letters in titles, ° · ×) and the few typographic
# marks the UI uses (… – — ’ › ‹ •). 0xA0 is the no-break space; 0xAD (soft hyphen) has no glyph.
TEXT = "0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2019,0x2022,0x2026,0x2039,0x203A"
CLOCK = "0x30-0x3A,0x2D,0x20"  # digits, colon, hyphen (for --:--), space
# Line metrics come from this range, not TEXT: accented capitals (Á Å) raise lv_font_conv's line
# height (13 px: 15 to 16, 22 px: 26 to 28), which moved every screen. Their accents may draw up to
# 2 px into the line above, or be cut at a label's top edge (P51-M, `docs/board-info.md`).
METRICS = "0x20-0x7E"

# (C name, ttf, size, range)
# The text fonts (not the clocks) fall back to the CJK font from the SD card for characters Inter
# lacks (P54, board/components/fenix_ui/cjk.cpp): 13 px for 12 and 13, 16 for 15 and 17, 20 for 22.
CJK = {22: "fx_cjk_20", 17: "fx_cjk_16", 15: "fx_cjk_16", 13: "fx_cjk_13", 12: "fx_cjk_13"}
TEXT_FONTS = [
    ("fx_font_clock_64", "inter/InterDisplay-SemiBold.ttf", 64, CLOCK),
    ("fx_font_clock_56", "inter/InterDisplay-SemiBold.ttf", 56, CLOCK),
    ("fx_font_title_l", "inter/Inter-SemiBold.ttf", 22, TEXT),
    ("fx_font_title", "inter/Inter-SemiBold.ttf", 17, TEXT),
    ("fx_font_body", "inter/Inter-Regular.ttf", 15, TEXT),
    ("fx_font_body_semibold", "inter/Inter-SemiBold.ttf", 15, TEXT),
    ("fx_font_label", "inter/Inter-Regular.ttf", 13, TEXT),
    ("fx_font_label_medium", "inter/Inter-Medium.ttf", 13, TEXT),
    ("fx_font_label_semibold", "inter/Inter-SemiBold.ttf", 13, TEXT),
    ("fx_font_caption", "inter/Inter-Regular.ttf", 12, TEXT),
    ("fx_font_caption_medium", "inter/Inter-Medium.ttf", 12, TEXT),
]

# Symbol icons (fill weight) used by the system screens; app icons come in their packages.
FILL_ICONS = [
    "gear-six", "power", "screencast", "arrow-clockwise", "plugs-connected", "plugs",
    "sun", "moon", "timer", "device-rotate", "hand-tap", "hard-drives", "desktop-tower",
    "info", "squares-four", "warning", "puzzle-piece", "check-circle", "x-circle", "monitor",
    "wifi-high", "wifi-medium", "wifi-low", "wifi-none", "lock",
    "sim-card", "download-simple", "arrow-counter-clockwise",  # P51 SD badge (no sd-card glyph); P52 update, go back
    # P50-M: for apps (Morpheus), appended to the app icon list after "monitor" (fenix-pkg ICONS)
    "play", "pause", "skip-forward", "skip-back", "speaker-high", "speaker-x", "microphone",
    "microphone-slash", "headphones", "game-controller", "music-notes", "video", "phone", "sparkle",
]
FILL_SIZES = [12, 14, 15, 16, 18, 20, 26, 28, 40]
BOLD_ICONS = ["caret-left", "caret-right", "check", "plus", "x"]
BOLD_SIZES = [10, 14]


def codepoints(css_name, names):
    css = (FONTS / "phosphor" / css_name).read_text(encoding="utf-8")
    out = {}
    for n in names:
        m = re.search(r"\.ph-(?:fill|bold)\.ph-" + re.escape(n) + r':before \{\s*content: "\\([0-9a-f]+)"', css)
        if not m:
            sys.exit(f"icon not found: {n} in {css_name}")
        out[n] = int(m.group(1), 16)
    return out


def conv(name, ttf, size, rng):
    OUT.mkdir(parents=True, exist_ok=True)
    # Repo-relative paths: lv_font_conv writes its options into the .c header comment.
    rel = lambda p: p.relative_to(ROOT).as_posix()
    cmd = ["npx", "--yes", CONV, "--font", rel(FONTS / ttf), "-r", rng, "--size", str(size), "--bpp", "4",
           "--format", "lvgl", "--no-compress", "--lv-include", "lvgl.h", "--lv-font-name", name,
           "-o", rel(OUT / f"{name}.c")]
    subprocess.run(cmd, check=True, cwd=ROOT, shell=(sys.platform == "win32"))


def keep_metrics(name, ttf, size):
    """Set a text font's line_height and base_line to what the METRICS range alone gives."""
    tmp = f"{name}_metrics"
    conv(tmp, ttf, size, METRICS)
    t = OUT / f"{tmp}.c"
    ref = t.read_text(encoding="utf-8")
    t.unlink()
    p = OUT / f"{name}.c"
    s = p.read_text(encoding="utf-8")
    for field in ("line_height", "base_line"):
        v = re.search(rf"\.{field} = (-?\d+),", ref).group(1)
        s = re.sub(rf"\.{field} = -?\d+,", f".{field} = {v},", s, count=1)
    p.write_text(s, encoding="utf-8", newline="\n")


def set_fallback(name, size):
    """Point a text font's `.fallback` at its CJK size (lv_font_conv writes NULL). Idempotent."""
    if name.startswith("fx_font_clock"):
        return
    p = OUT / f"{name}.c"
    s = p.read_text(encoding="utf-8")
    cjk = CJK[size]
    s = re.sub(r"\.fallback = [^,]+,", f".fallback = &{cjk},", s, count=1)
    decl = f"extern const lv_font_t {cjk};  /* fenix_ui/cjk.cpp (P54) */\n"
    marker = "/*Initialize a public general font descriptor*/"
    if decl not in s:
        s = s.replace(marker, decl + "\n" + marker, 1)
    p.write_text(s, encoding="utf-8", newline="\n")


def main():
    for name, ttf, size, rng in TEXT_FONTS:
        conv(name, ttf, size, rng)
        if rng == TEXT:
            keep_metrics(name, ttf, size)
        set_fallback(name, size)
    fill = codepoints("fill.css", FILL_ICONS)
    bold = codepoints("bold.css", BOLD_ICONS)
    for s in FILL_SIZES:
        conv(f"fx_icon_fill_{s}", "phosphor/Phosphor-Fill.ttf", s, ",".join(hex(c) for c in fill.values()))
    for s in BOLD_SIZES:
        conv(f"fx_icon_bold_{s}", "phosphor/Phosphor-Bold.ttf", s, ",".join(hex(c) for c in bold.values()))
    lines = ["// Generated by tools/gen_fonts.py. Do not edit.", "#pragma once", '#include "lvgl.h"', ""]
    names = [n for n, *_ in TEXT_FONTS] + [f"fx_icon_fill_{s}" for s in FILL_SIZES] + [f"fx_icon_bold_{s}" for s in BOLD_SIZES]
    # The fonts are C; C linkage keeps the names unmangled under MSVC too (board/sim).
    lines += ["#ifdef __cplusplus", 'extern "C" {', "#endif"]
    lines += [f"LV_FONT_DECLARE({n})" for n in names]
    lines += [f"LV_FONT_DECLARE({n})  // CJK from the SD card, the text fonts' fallback (P54)" for n in sorted(set(CJK.values()))]
    lines += ["#ifdef __cplusplus", "}", "#endif", ""]
    lines.append("// Icon glyphs as UTF-8 strings (Phosphor private-use code points).")
    for n, c in list(fill.items()) + list(bold.items()):
        utf8 = "".join(f"\\x{b:02X}" for b in chr(c).encode("utf-8"))
        lines.append(f'#define FX_ICON_{n.replace("-", "_").upper()} "{utf8}"')
    HDR.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    total = sum(p.stat().st_size for p in OUT.glob("*.c"))
    print(f"{len(names)} fonts, {total // 1024} KB of C source")


if __name__ == "__main__":
    main()
