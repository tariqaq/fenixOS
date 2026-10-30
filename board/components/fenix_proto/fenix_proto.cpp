#include "fenix_proto.h"

#include <string.h>

namespace fnx {

static const uint8_t MAGIC[4] = {'F', 'N', 'X', 'O'};

uint16_t crc16(const uint8_t* data, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int k = 0; k < 8; k++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

size_t cobs_encode(const uint8_t* in, size_t n, uint8_t* out) {
  size_t code_idx = 0, o = 1;
  uint8_t code = 1;
  for (size_t i = 0; i < n; i++) {
    if (in[i] == 0) {
      out[code_idx] = code;
      code_idx = o++;
      code = 1;
    } else {
      out[o++] = in[i];
      if (++code == 0xFF) {
        out[code_idx] = code;
        code_idx = o++;
        code = 1;
      }
    }
  }
  out[code_idx] = code;
  return o;
}

int cobs_decode(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
  size_t i = 0, o = 0;
  while (i < n) {
    uint8_t code = in[i++];
    if (code == 0) return -1;
    size_t run = code - 1;
    if (i + run > n || o + run > cap) return -1;
    memcpy(out + o, in + i, run);
    o += run;
    i += run;
    if (code != 0xFF && i < n) {
      if (o >= cap) return -1;
      out[o++] = 0;
    }
  }
  return static_cast<int>(o);
}

size_t encode_frame(const uint8_t* payload, size_t n, uint8_t* out, size_t cap) {
  if (n > MAX_PAYLOAD) return 0;
  uint8_t tmp[MAX_PAYLOAD + 2];
  memcpy(tmp, payload, n);
  uint16_t c = crc16(payload, n);
  tmp[n] = c & 0xFF;
  tmp[n + 1] = c >> 8;
  if (cap < (n + 2) + (n + 2) / 254 + 2) return 0;
  size_t len = cobs_encode(tmp, n + 2, out);
  out[len++] = 0;
  return len;
}

int decode_frame(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
  int len = cobs_decode(in, n, out, cap);
  if (len < 4 || static_cast<size_t>(len) > MAX_PAYLOAD + 2) return -1;
  size_t p = len - 2;
  uint16_t got = out[p] | (out[p + 1] << 8);
  if (got != crc16(out, p)) return -1;
  return static_cast<int>(p);
}

void FrameDecoder::push(const uint8_t* data, size_t n, Cb cb, void* ctx) {
  for (size_t i = 0; i < n; i++) {
    uint8_t b = data[i];
    if (b == 0) {
      if (len_) {
        int p = overflow_ ? -1 : decode_frame(buf_, len_, out_, sizeof out_);
        if (p >= 2) {
          frames++;
          cb(out_, p, ctx);
        } else {
          errors++;
        }
      }
      len_ = 0;
      overflow_ = false;
    } else if (len_ < sizeof buf_) {
      buf_[len_++] = b;
    } else {
      overflow_ = true;
    }
  }
}

// ---- Writer / Reader ----

void Writer::u8(uint8_t v) {
  if (n_ + 1 > cap_) {
    ok_ = false;
    return;
  }
  b_[n_++] = v;
}
void Writer::u16(uint16_t v) {
  u8(v & 0xFF);
  u8(v >> 8);
}
void Writer::u32(uint32_t v) {
  u16(v & 0xFFFF);
  u16(v >> 16);
}
void Writer::bytes(const void* p, size_t n) {
  if (n_ + n > cap_) {
    ok_ = false;
    return;
  }
  memcpy(b_ + n_, p, n);
  n_ += n;
}
void Writer::str(const char* s) {
  size_t n = strlen(s);
  if (n > 255) n = 255;
  u8(static_cast<uint8_t>(n));
  bytes(s, n);
}

const uint8_t* Reader::take(size_t n) {
  if (!ok_ || i_ + n > n_) {
    ok_ = false;
    return nullptr;
  }
  const uint8_t* p = b_ + i_;
  i_ += n;
  return p;
}
uint8_t Reader::u8() {
  const uint8_t* p = take(1);
  return p ? p[0] : 0;
}
uint16_t Reader::u16() {
  const uint8_t* p = take(2);
  return p ? (p[0] | (p[1] << 8)) : 0;
}
uint32_t Reader::u32() {
  const uint8_t* p = take(4);
  return p ? (p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24)) : 0;
}
void Reader::str(char* out, size_t cap) {
  size_t n = u8();
  const uint8_t* p = take(n);
  size_t c = (p && cap) ? (n < cap - 1 ? n : cap - 1) : 0;
  if (p) memcpy(out, p, c);
  if (cap) out[c] = 0;
}
const uint8_t* Reader::rest(size_t* len) {
  *len = n_ - i_;
  const uint8_t* p = b_ + i_;
  i_ = n_;
  return p;
}

// ---- Bodies ----

static bool magic_ok(Reader& r) {
  const uint8_t* m = r.take(4);
  return m && memcmp(m, MAGIC, 4) == 0;
}

bool read_hello(Reader& r, uint8_t* proto) {
  if (!magic_ok(r)) return false;
  *proto = r.u8();
  return r.done();
}
void write_hello(Writer& w, uint8_t proto) {
  w.bytes(MAGIC, 4);
  w.u8(proto);
}

bool read_hello_ack(Reader& r, HelloAck& a) {
  if (!magic_ok(r)) return false;
  a.proto = r.u8();
  a.fw_major = r.u8();
  a.fw_minor = r.u8();
  a.fw_patch = r.u8();
  a.width = r.u16();
  a.height = r.u16();
  a.rotation = r.u8();
  a.free_heap = r.u32();
  a.apps_used_kb = r.u16();
  a.apps_total_kb = r.u16();
  a.reset_reason = r.u8();
  a.uptime_s = r.u32();
  a.time_valid = r.u8();
  const uint8_t* m = r.take(6);
  if (m) memcpy(a.mac, m, 6);
  return r.done();
}
void write_hello_ack(Writer& w, const HelloAck& a) {
  w.bytes(MAGIC, 4);
  w.u8(a.proto);
  w.u8(a.fw_major);
  w.u8(a.fw_minor);
  w.u8(a.fw_patch);
  w.u16(a.width);
  w.u16(a.height);
  w.u8(a.rotation);
  w.u32(a.free_heap);
  w.u16(a.apps_used_kb);
  w.u16(a.apps_total_kb);
  w.u8(a.reset_reason);
  w.u32(a.uptime_s);
  w.u8(a.time_valid);
  w.bytes(a.mac, 6);
}

bool read_stats(Reader& r, Stats& s) {
  s.free_heap = r.u32();
  s.min_heap = r.u32();
  s.fps_x10 = r.u16();
  s.rx_frames = r.u32();
  s.rx_errors = r.u32();
  s.stream_dropped = r.u32();
  return r.done();
}
void write_stats(Writer& w, const Stats& s) {
  w.u32(s.free_heap);
  w.u32(s.min_heap);
  w.u16(s.fps_x10);
  w.u32(s.rx_frames);
  w.u32(s.rx_errors);
  w.u32(s.stream_dropped);
}

bool read_sysinfo(Reader& r, Sysinfo& s) {
  s.time_us = r.u32();
  s.cpu_mhz = r.u16();
  s.heap_total = r.u32();
  s.heap_free = r.u32();
  s.heap_min = r.u32();
  s.heap_largest = r.u32();
  s.dma_free = r.u32();
  s.flash_size = r.u32();
  s.app_size = r.u32();
  s.app_slot = r.u32();
  s.apps_used = r.u32();
  s.apps_total = r.u32();
  s.nvs_used = r.u16();
  s.nvs_total = r.u16();
  s.rx_bytes = r.u32();
  s.tx_bytes = r.u32();
  s.rx_frames = r.u32();
  s.rx_errors = r.u32();
  s.fps_x10 = r.u16();
  s.backlight = r.u8();
  s.flags = r.u8();
  return r.ok();
}
void write_sysinfo(Writer& w, const Sysinfo& s) {
  w.u32(s.time_us);
  w.u16(s.cpu_mhz);
  w.u32(s.heap_total);
  w.u32(s.heap_free);
  w.u32(s.heap_min);
  w.u32(s.heap_largest);
  w.u32(s.dma_free);
  w.u32(s.flash_size);
  w.u32(s.app_size);
  w.u32(s.app_slot);
  w.u32(s.apps_used);
  w.u32(s.apps_total);
  w.u16(s.nvs_used);
  w.u16(s.nvs_total);
  w.u32(s.rx_bytes);
  w.u32(s.tx_bytes);
  w.u32(s.rx_frames);
  w.u32(s.rx_errors);
  w.u16(s.fps_x10);
  w.u8(s.backlight);
  w.u8(s.flags);
}

bool read_task_info(Reader& r, TaskInfo& t) {
  t.id = r.u16();
  t.core = r.u8();
  t.priority = r.u8();
  t.state = r.u8();
  t.runtime = r.u32();
  t.stack_free = r.u16();
  r.str(t.name, sizeof t.name);
  return r.ok();
}
void write_task_info(Writer& w, const TaskInfo& t) {
  w.u16(t.id);
  w.u8(t.core);
  w.u8(t.priority);
  w.u8(t.state);
  w.u32(t.runtime);
  w.u16(t.stack_free);
  w.str(t.name);
}

bool read_board_state(Reader& r, BoardState& s) {
  s.screen = r.u8();
  s.dimmed = r.u8();
  s.rotation = r.u8();
  r.str(s.app_id, sizeof s.app_id);
  return r.done();
}
void write_board_state(Writer& w, const BoardState& s) {
  w.u8(s.screen);
  w.u8(s.dimmed);
  w.u8(s.rotation);
  w.str(s.app_id);
}

bool read_radio_state(Reader& r, RadioState& s) {
  s.mode = r.u8();
  s.state = r.u8();
  s.rssi = (int8_t)r.u8();
  const uint8_t* ip = r.take(4);
  if (ip) memcpy(s.ip, ip, 4);
  r.str(s.name, sizeof s.name);
  return r.done();
}
void write_radio_state(Writer& w, const RadioState& s) {
  w.u8(s.mode);
  w.u8(s.state);
  w.u8((uint8_t)s.rssi);
  w.bytes(s.ip, 4);
  w.str(s.name);
}

bool read_wifi_ap(Reader& r, WifiAp& a) {
  r.str(a.ssid, sizeof a.ssid);
  a.rssi = (int8_t)r.u8();
  a.channel = r.u8();
  a.auth = r.u8();
  const uint8_t* b = r.take(6);
  if (b) memcpy(a.bssid, b, 6);
  return r.ok();
}
void write_wifi_ap(Writer& w, const WifiAp& a) {
  w.str(a.ssid);
  w.u8((uint8_t)a.rssi);
  w.u8(a.channel);
  w.u8(a.auth);
  w.bytes(a.bssid, 6);
}

bool read_wifi_join(Reader& r, WifiJoin& j) {
  r.str(j.ssid, sizeof j.ssid);
  r.str(j.password, sizeof j.password);
  return r.done();
}
void write_wifi_join(Writer& w, const WifiJoin& j) {
  w.str(j.ssid);
  w.str(j.password);
}

bool read_app_entry(Reader& r, AppEntry& e) {
  r.str(e.id, sizeof e.id);
  r.str(e.version, sizeof e.version);
  const uint8_t* h = r.take(32);
  if (h) memcpy(e.sha256, h, 32);
  return r.ok();
}
void write_app_entry(Writer& w, const AppEntry& e) {
  w.str(e.id);
  w.str(e.version);
  w.bytes(e.sha256, 32);
}

bool read_app_install_begin(Reader& r, AppInstallBegin& b) {
  r.str(b.id, sizeof b.id);
  r.str(b.version, sizeof b.version);
  b.size = r.u32();
  const uint8_t* h = r.take(32);
  if (h) memcpy(b.sha256, h, 32);
  return r.done();
}
void write_app_install_begin(Writer& w, const AppInstallBegin& b) {
  w.str(b.id);
  w.str(b.version);
  w.u32(b.size);
  w.bytes(b.sha256, 32);
}

bool read_bind_set(Reader& r, BindSet& b) {
  b.bind = r.u16();
  b.kind = r.u8();
  r.str(b.id, sizeof b.id);
  b.i = 0;
  b.text[0] = 0;
  if (b.kind == 0) {
    b.i = r.i32();
  } else if (b.kind == 1) {
    b.i = r.u8();
  } else if (b.kind == 2) {
    r.str(b.text, sizeof b.text);
  } else {
    return false;
  }
  return r.done();
}
void write_bind_set(Writer& w, const BindSet& b) {
  w.u16(b.bind);
  w.u8(b.kind);
  w.str(b.id);
  if (b.kind == 0) {
    w.i32(b.i);
  } else if (b.kind == 1) {
    w.u8(b.i ? 1 : 0);
  } else {
    w.str(b.text);
  }
}

bool read_event(Reader& r, Event& e) {
  e.elem = r.u16();
  e.kind = r.u8();
  e.value = r.i32();
  r.str(e.id, sizeof e.id);
  return r.done();
}
void write_event(Writer& w, const Event& e) {
  w.u16(e.elem);
  w.u8(e.kind);
  w.i32(e.value);
  w.str(e.id);
}

bool read_rect_header(Reader& r, RectHeader& h) {
  h.rect_id = r.u16();
  h.x = r.u16();
  h.y = r.u16();
  h.w = r.u16();
  h.h = r.u16();
  h.codec = r.u8();
  h.total = r.u32();
  h.offset = r.u32();
  return r.ok();
}
void write_rect_header(Writer& w, const RectHeader& h) {
  w.u16(h.rect_id);
  w.u16(h.x);
  w.u16(h.y);
  w.u16(h.w);
  w.u16(h.h);
  w.u8(h.codec);
  w.u32(h.total);
  w.u32(h.offset);
}

}  // namespace fnx
