// Chinese, Japanese and Korean text from the SD card (P54, docs/fonts.md). The PC draws a CJK font
// into fonts/cjk.fxf (FXF1: 13, 16 and 20 px, 4 bpp, an index entry per codepoint); the built-in
// text fonts name fx_cjk_13/16/20 as their LVGL fallback (tools/gen_fonts.py), so a character Inter
// lacks is looked up here. A glyph costs two small reads (index entry, record) through the file's
// own 4 KB FatFs cache; the last SLOTS glyphs stay in RAM. Nothing is allocated without the font.
// No card or no font: the fallback finds nothing and text looks as before.
// Threads: LVGL calls the font functions on the UI task; the SD and file tasks call release()
// (through sys::fonts_release) before they replace, delete or unmount the file. One mutex.
#include <stdio.h>
#include <string.h>

#include <mutex>
#include <new>

#include "fenix_sys.h"
#include "fenix_ui.h"

namespace fx::ui {

namespace {

constexpr const char* CARD_PATH = "/sd/fenixos/fonts/cjk.fxf";
constexpr int SIZES = 3;
constexpr uint8_t PX[SIZES] = {13, 16, 20};  // fenix-font's SIZES; the fonts below in this order
constexpr int MAX_RANGES = 16;
constexpr int SLOTS = 32;
constexpr int BITS = 240;     // a packed bitmap up to this fits a slot (20 px glyphs are ~21 × 21)
constexpr int REC_MAX = 512;  // a record read for a bigger glyph (a 31 × 31 box)
constexpr uint32_t FIRST = 0x3000;  // nothing below is in the file (FXF1 ranges start here)

struct Range {
  uint32_t first, count, index;
};
struct Size {
  uint8_t nr;  // 0: the file has no glyphs at this size
  Range r[MAX_RANGES];
};
struct Slot {
  uint32_t cp;  // 0: empty
  uint32_t used;  // s_tick at the last lookup: the least recent goes first
  uint32_t off;  // record offset; 0: the font has no glyph for cp
  uint8_t size, w, h, adv;
  int8_t ox, oy;
  bool full;  // bits holds the whole bitmap
  uint8_t bits[BITS];
};

std::mutex s_mu;
FILE* s_f;
Size s_sz[SIZES];
Slot* s_slots;
uint32_t s_tick;
const char* s_path = CARD_PATH;

uint32_t le32(const uint8_t* b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

bool read_at(uint32_t off, void* out, size_t n, size_t* got = nullptr) {
  if (fseek(s_f, (long)off, SEEK_SET)) return false;
  size_t k = fread(out, 1, n, s_f);
  if (got) *got = k;
  return got ? k > 0 : k == n;
}

void close_locked() {
  if (s_f) fclose(s_f);
  s_f = nullptr;
  delete[] s_slots;
  s_slots = nullptr;
  memset(s_sz, 0, sizeof s_sz);
}

// Header and ranges; false: not an FXF1 file (left closed).
bool open_locked() {
  close_locked();
  s_f = fopen(s_path, "rb");
  if (!s_f) return false;
  setvbuf(s_f, nullptr, _IONBF, 0);  // FatFs keeps its own sector cache; stdio's would double it
  uint8_t h[16];
  bool ok = read_at(0, h, sizeof h) && !memcmp(h, "FXF1", 4) && h[4] == 1 && h[5] >= 1 && h[5] <= 8;
  int any = 0;
  for (int i = 0; ok && i < h[5]; i++) {
    uint8_t e[16];
    if (!read_at(16 + 16 * i, e, sizeof e)) {
      ok = false;
      break;
    }
    int s = -1;
    for (int k = 0; k < SIZES; k++)
      if (PX[k] == e[0]) s = k;
    uint16_t nr = e[4] | e[5] << 8;
    if (s < 0 || e[3] != 4 || !nr || nr > MAX_RANGES) continue;  // a size or depth not drawn here
    uint8_t r[12 * MAX_RANGES];
    if (!read_at(le32(e + 8), r, 12u * nr)) {
      ok = false;
      break;
    }
    for (int k = 0; k < nr; k++) s_sz[s].r[k] = {le32(r + 12 * k), le32(r + 12 * k + 4), le32(r + 12 * k + 8)};
    s_sz[s].nr = (uint8_t)nr;
    any++;
  }
  if (ok && any) s_slots = new (std::nothrow) Slot[SLOTS]();
  if (!ok || !any || !s_slots) {
    close_locked();
    return false;
  }
  return true;
}

void release() {
  std::lock_guard<std::mutex> l(s_mu);
  close_locked();
}

void hook() {
  static bool done;
  if (!done) sys::fonts_hook(release);
  done = true;
}

int size_of(const lv_font_t* f) { return (int)(intptr_t)f->user_data; }

// LVGL draws the screen in bands and asks for every glyph of a label in each band, so the cache
// holds the last SLOTS glyphs by use (a direct-mapped cache missed 90 times for 20 glyphs).
Slot* cached(uint32_t cp, int s) {
  for (int i = 0; i < SLOTS; i++)
    if (s_slots[i].cp == cp && s_slots[i].size == s) {
      s_slots[i].used = ++s_tick;
      return &s_slots[i];
    }
  return nullptr;
}

Slot& victim() {
  Slot* v = &s_slots[0];
  for (int i = 0; i < SLOTS; i++) {
    if (!s_slots[i].cp) return s_slots[i];
    if (s_slots[i].used < v->used) v = &s_slots[i];
  }
  return *v;
}

// The glyph's slot, read from the card on a miss; nullptr on a read error.
Slot* find_locked(uint32_t cp, int s) {
  if (Slot* c = cached(cp, s)) return c;
  const Size& z = s_sz[s];
  uint32_t idx = 0;
  for (int k = 0; k < z.nr; k++)
    if (cp >= z.r[k].first && cp - z.r[k].first < z.r[k].count) idx = z.r[k].index + 4 * (cp - z.r[k].first);
  uint32_t off = 0;
  if (idx) {
    uint8_t b[4];
    if (!read_at(idx, b, 4)) return nullptr;
    off = le32(b);
  }
  Slot& sl = victim();
  sl = Slot{};
  sl.used = ++s_tick;
  sl.cp = cp;
  sl.size = (uint8_t)s;
  sl.off = off;
  if (!off) return &sl;
  uint8_t rec[6 + BITS];
  size_t got = 0;
  if (!read_at(off, rec, sizeof rec, &got) || got < 6) {
    sl.cp = 0;
    return nullptr;
  }
  sl.w = rec[0];
  sl.h = rec[1];
  sl.ox = (int8_t)rec[2];
  sl.oy = (int8_t)rec[3];
  sl.adv = rec[4];
  size_t n = ((size_t)sl.w * sl.h + 1) / 2;
  sl.full = n <= BITS && got >= 6 + n;
  if (sl.full) memcpy(sl.bits, rec + 6, n);
  return &sl;
}

bool get_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* d, uint32_t cp, uint32_t) {
  if (cp < FIRST) return false;
  std::lock_guard<std::mutex> l(s_mu);
  if (!s_f) return false;
  Slot* g = find_locked(cp, size_of(font));
  if (!g || !g->off) return false;
  d->adv_w = g->adv;
  d->box_w = g->w;
  d->box_h = g->h;
  d->ofs_x = g->ox;
  d->ofs_y = g->oy;
  d->stride = 0;
  d->format = LV_FONT_GLYPH_FORMAT_A4;
  d->is_placeholder = false;
  d->gid.index = cp;
  return true;
}

// Expand the 4-bit coverage into the A8 buffer LVGL hands over (rows at its stride).
const void* get_bitmap(lv_font_glyph_dsc_t* d, lv_draw_buf_t* buf) {
  if (!buf) return nullptr;
  std::lock_guard<std::mutex> l(s_mu);
  if (!s_f) return nullptr;
  Slot* g = find_locked(d->gid.index, size_of(d->resolved_font));
  if (!g || !g->off || g->w != d->box_w || g->h != d->box_h) return nullptr;
  const uint8_t* src = g->bits;
  uint8_t big[REC_MAX];
  size_t n = ((size_t)g->w * g->h + 1) / 2;
  if (!g->full) {
    if (n > sizeof big || !read_at(g->off + 6, big, n)) return nullptr;
    src = big;
  }
  uint32_t stride = buf->header.stride;
  uint8_t* out = buf->data;
  size_t i = 0;
  for (int y = 0; y < g->h; y++, out += stride)
    for (int x = 0; x < g->w; x++, i++) {
      uint8_t v = i & 1 ? src[i / 2] & 0x0F : src[i / 2] >> 4;
      out[x] = v * 17;
    }
  return buf;
}

// Line metrics are the built-in font's (LVGL places a fallback glyph on the caller's base line).
lv_font_t make(int size, int32_t line, int32_t base) {
  lv_font_t f{};
  f.get_glyph_dsc = get_dsc;
  f.get_glyph_bitmap = get_bitmap;
  f.line_height = line;
  f.base_line = base;
  f.user_data = (void*)(intptr_t)size;
  return f;
}

}  // namespace

void cjk_load(const char* path) {
  if (path) s_path = path;
  hook();
  bool on;
  {
    std::lock_guard<std::mutex> l(s_mu);
    on = open_locked();
  }
  sys::link_log(3, on ? "fonts: CJK font loaded" : "fonts: no CJK font");
  lv_obj_report_style_change(nullptr);  // labels measure their text again, with or without it
}

bool cjk_ready() {
  std::lock_guard<std::mutex> l(s_mu);
  return s_f != nullptr;
}

void cjk_sd_changed() {
  hook();
  bool ready = sys::sd_info().state == fnx::sd_state::READY;
  bool open;
  {
    std::lock_guard<std::mutex> l(s_mu);
    open = s_f != nullptr;
  }
  if (ready && !open) {
    std::lock_guard<std::mutex> l(s_mu);
    open = open_locked();
    if (!open) return;  // no font on this card: nothing to redraw
  } else if (!ready && open) {
    release();
  } else {
    return;  // free space changed, or still no card
  }
  sys::link_log(3, open ? "fonts: CJK font loaded" : "fonts: CJK font closed (no card)");
  lv_obj_report_style_change(nullptr);
}

}  // namespace fx::ui

// Declared in fenix_fonts.h (C linkage, as the generated fonts that name them).
extern "C" {
const lv_font_t fx_cjk_13 = fx::ui::make(0, 16, 4);
const lv_font_t fx_cjk_16 = fx::ui::make(1, 19, 4);
const lv_font_t fx_cjk_20 = fx::ui::make(2, 24, 5);
}
