// Stream encoder: flushed LVGL areas -> STREAM_RECT frames (proto/spec.md, "Stream").
// Each area is cut into bands of a few rows, RLE16-coded (raw when RLE does not help)
// and written into the UART TX buffer. When the buffer has no room, the band is dropped
// and the caller marks it stale, so the UI re-sends it later.
#include <stdlib.h>
#include <string.h>

#include "fenix_proto.h"
#include "fenix_sys.h"

namespace fx::sys {

size_t link_tx_free();
void link_write_locked(const uint8_t* wire, size_t n);
void link_lock();
void link_unlock();
uint8_t link_next_seq();

static constexpr size_t BAND_RAW = 3072;  // raw bytes per band
static constexpr size_t RLE_CAP = BAND_RAW * 3 / 2 + 4;
// RLE and frame buffers (~5.8 KB) exist only while streaming (P31). stream_rect and
// stream_set_active both run on the UI task, so a stop never frees them mid-band.
static uint8_t* s_rle;
static uint8_t* s_wire;
static volatile bool s_active;
static uint16_t s_rect_id;
static uint32_t s_dropped;

bool stream_active() { return s_active; }
void stream_set_active(bool on) {
  s_active = on;
  if (on) return;
  free(s_rle);
  free(s_wire);
  s_rle = s_wire = nullptr;
}
uint32_t stream_dropped() { return s_dropped; }

// RLE16: count:u8 ‖ pixel:u16be. Pixels are already big-endian in memory.
static size_t rle16(const uint8_t* px, size_t npx, uint8_t* out) {
  size_t o = 0;
  for (size_t i = 0; i < npx;) {
    uint8_t a = px[i * 2], b = px[i * 2 + 1];
    size_t run = 1;
    while (i + run < npx && run < 255 && px[(i + run) * 2] == a && px[(i + run) * 2 + 1] == b) run++;
    out[o++] = run;
    out[o++] = a;
    out[o++] = b;
    i += run;
  }
  return o;
}

static size_t wire_bound(size_t data) {
  size_t frames = (data + fnx::MAX_CHUNK - 1) / fnx::MAX_CHUNK;
  return data + frames * (2 + fnx::RECT_HEADER_LEN + 2 + 1 + 6);
}

bool stream_has_room(size_t bytes) { return link_tx_free() >= wire_bound(bytes / 2) + 512; }

static bool send_band(int x, int y, int w, int h, const uint8_t* px) {
  size_t raw = (size_t)w * h * 2;
  size_t rle = rle16(px, (size_t)w * h, s_rle);
  const uint8_t* data = rle < raw ? s_rle : px;
  size_t total = rle < raw ? rle : raw;
  fnx::RectHeader hd{};
  hd.rect_id = s_rect_id++;
  hd.x = x;
  hd.y = y;
  hd.w = w;
  hd.h = h;
  hd.codec = rle < raw ? fnx::codec_id::RLE16 : fnx::codec_id::RAW;
  hd.total = total;
  link_lock();
  if (link_tx_free() < wire_bound(total)) {
    link_unlock();
    return false;
  }
  for (size_t off = 0; off < total; off += fnx::MAX_CHUNK) {
    size_t n = total - off < fnx::MAX_CHUNK ? total - off : fnx::MAX_CHUNK;
    hd.offset = off;
    size_t len = fnx::build_frame(fnx::ty::STREAM_RECT, link_next_seq(), s_wire, fnx::MAX_WIRE, [&](fnx::Writer& w) {
      fnx::write_rect_header(w, hd);
      w.bytes(data + off, n);
    });
    if (len) link_write_locked(s_wire, len);
  }
  link_unlock();
  return true;
}

bool stream_rect(int x, int y, int w, int h, const uint8_t* px) {
  if (!s_active || w <= 0 || h <= 0) return true;
  if (!s_rle) s_rle = (uint8_t*)malloc(RLE_CAP);
  if (!s_wire) s_wire = (uint8_t*)malloc(fnx::MAX_WIRE);
  if (!s_rle || !s_wire) {
    s_dropped++;
    return false;
  }
  int rows = BAND_RAW / (w * 2);
  if (rows < 1) rows = 1;
  bool ok = true;
  for (int r = 0; r < h; r += rows) {
    int bh = h - r < rows ? h - r : rows;
    if (!send_band(x, y + r, w, bh, px + (size_t)r * w * 2)) {
      s_dropped++;
      ok = false;
      break;
    }
  }
  return ok;
}

}  // namespace fx::sys
