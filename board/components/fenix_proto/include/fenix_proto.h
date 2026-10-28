// fenixOS link protocol, C++ side. Spec: proto/spec.md.
// No heap use: callers own all buffers. Strings are copied into fixed arrays.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fnx {

constexpr uint8_t PROTO_VERSION = 1;
constexpr uint32_t BAUD = 921600;
constexpr size_t MAX_PAYLOAD = 1200;
constexpr size_t MAX_CHUNK = 1024;
// Worst-case wire size of one frame: COBS overhead + CRC + delimiter.
constexpr size_t MAX_WIRE = MAX_PAYLOAD + 2 + (MAX_PAYLOAD + 2) / 254 + 2;
constexpr size_t ID_LEN = 64;  // app ids and versions are cut to 63 bytes + NUL

namespace ty {
constexpr uint8_t HELLO = 0x01, HELLO_ACK = 0x02, PING = 0x03, PONG = 0x04, LOG = 0x05, ERROR = 0x06, STATS = 0x07;
constexpr uint8_t TIME_SET = 0x10, REBOOT = 0x11, BOARD_STATE = 0x12, TOUCH = 0x13;
constexpr uint8_t APP_LIST_REQ = 0x20, APP_LIST = 0x21, APP_INSTALL_BEGIN = 0x22, APP_CHUNK = 0x23, APP_ACK = 0x24;
constexpr uint8_t APP_INSTALL_END = 0x25, APP_RESULT = 0x26, APP_REMOVE = 0x27, APP_OPENED = 0x28, APP_CLOSED = 0x29;
constexpr uint8_t HOST_STATE = 0x2A;
constexpr uint8_t BIND_SET = 0x30, EVENT = 0x31;
constexpr uint8_t STREAM_START = 0x40, STREAM_STOP = 0x41, STREAM_RECT = 0x42, STREAM_SYNC = 0x43, STREAM_INFO = 0x44;
}  // namespace ty

namespace err {
constexpr uint8_t PROTO_MISMATCH = 1, UNKNOWN_TYPE = 2, BAD_BODY = 3, BUSY = 4;
}
namespace app {
constexpr uint8_t OP_INSTALL = 1, OP_REMOVE = 2;
constexpr uint8_t OK = 0, NO_SPACE = 1, BAD_HASH = 2, BAD_PACKAGE = 3, BUSY = 4, STORAGE = 5, UNKNOWN_APP = 6;
}  // namespace app
namespace screen {
constexpr uint8_t BOOT = 0, HOME = 1, SETTINGS = 2, SESSION = 3, STREAM = 4, APP = 5;
}
namespace event {
constexpr uint8_t TAP = 1, LONG = 2, CHANGE = 3;
}
namespace codec_id {
constexpr uint8_t RAW = 0, RLE16 = 1;
}

uint16_t crc16(const uint8_t* data, size_t n);
size_t cobs_encode(const uint8_t* in, size_t n, uint8_t* out);
int cobs_decode(const uint8_t* in, size_t n, uint8_t* out, size_t cap);
// payload -> wire (incl. trailing 0). Returns length, 0 on error.
size_t encode_frame(const uint8_t* payload, size_t n, uint8_t* out, size_t cap);
// wire (without the 0) -> payload. Returns payload length, -1 on error.
int decode_frame(const uint8_t* in, size_t n, uint8_t* out, size_t cap);

class Writer {
 public:
  Writer(uint8_t* b, size_t cap) : b_(b), cap_(cap) {}
  void u8(uint8_t v);
  void u16(uint16_t v);
  void u32(uint32_t v);
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void bytes(const void* p, size_t n);
  void str(const char* s);
  size_t size() const { return n_; }
  bool ok() const { return ok_; }

 private:
  uint8_t* b_;
  size_t cap_, n_ = 0;
  bool ok_ = true;
};

class Reader {
 public:
  Reader(const uint8_t* b, size_t n) : b_(b), n_(n) {}
  const uint8_t* take(size_t n);
  uint8_t u8();
  uint16_t u16();
  uint32_t u32();
  int32_t i32() { return static_cast<int32_t>(u32()); }
  void str(char* out, size_t cap);
  const uint8_t* rest(size_t* len);
  size_t remaining() const { return n_ - i_; }
  bool ok() const { return ok_; }
  bool done() const { return ok_ && i_ == n_; }

 private:
  const uint8_t* b_;
  size_t n_, i_ = 0;
  bool ok_ = true;
};

// Streaming splitter: feed raw bytes, get decoded payloads through the callback.
class FrameDecoder {
 public:
  using Cb = void (*)(const uint8_t* payload, size_t n, void* ctx);
  void push(const uint8_t* data, size_t n, Cb cb, void* ctx);
  uint32_t frames = 0, errors = 0;

 private:
  uint8_t buf_[MAX_WIRE];
  uint8_t out_[MAX_PAYLOAD + 2];
  size_t len_ = 0;
  bool overflow_ = false;
};

// ---- Message bodies (read = parse body after type+seq; write = body only) ----

struct HelloAck {
  uint8_t proto, fw_major, fw_minor, fw_patch;
  uint16_t width, height;
  uint8_t rotation;
  uint32_t free_heap;
  uint16_t apps_used_kb, apps_total_kb;
  uint8_t reset_reason;
  uint32_t uptime_s;
  uint8_t time_valid;
  uint8_t mac[6];
};
bool read_hello(Reader& r, uint8_t* proto);
void write_hello(Writer& w, uint8_t proto);
bool read_hello_ack(Reader& r, HelloAck& a);
void write_hello_ack(Writer& w, const HelloAck& a);

struct Stats {
  uint32_t free_heap, min_heap;
  uint16_t fps_x10;
  uint32_t rx_frames, rx_errors, stream_dropped;
};
bool read_stats(Reader& r, Stats& s);
void write_stats(Writer& w, const Stats& s);

struct BoardState {
  uint8_t screen, dimmed, rotation;
  char app_id[ID_LEN];
};
bool read_board_state(Reader& r, BoardState& s);
void write_board_state(Writer& w, const BoardState& s);

struct AppEntry {
  char id[ID_LEN], version[ID_LEN];
  uint8_t sha256[32];
};
void write_app_entry(Writer& w, const AppEntry& e);
bool read_app_entry(Reader& r, AppEntry& e);

struct AppInstallBegin {
  char id[ID_LEN], version[ID_LEN];
  uint32_t size;
  uint8_t sha256[32];
};
bool read_app_install_begin(Reader& r, AppInstallBegin& b);
void write_app_install_begin(Writer& w, const AppInstallBegin& b);

struct BindSet {
  uint16_t bind;
  uint8_t kind;  // 0 int, 1 bool, 2 text
  char id[ID_LEN];
  int32_t i;     // int or bool
  char text[256];
};
bool read_bind_set(Reader& r, BindSet& b);
void write_bind_set(Writer& w, const BindSet& b);

struct Event {
  uint16_t elem;
  uint8_t kind;
  int32_t value;
  char id[ID_LEN];
};
bool read_event(Reader& r, Event& e);
void write_event(Writer& w, const Event& e);

struct RectHeader {
  uint16_t rect_id, x, y, w, h;
  uint8_t codec;
  uint32_t total, offset;
};
constexpr size_t RECT_HEADER_LEN = 19;
bool read_rect_header(Reader& r, RectHeader& h);
void write_rect_header(Writer& w, const RectHeader& h);

// Build a whole wire frame: type, seq, then body written by fn(Writer&).
// Returns wire length (0 if the body did not fit).
template <typename F>
size_t build_frame(uint8_t type, uint8_t seq, uint8_t* wire, size_t cap, F fn) {
  uint8_t payload[MAX_PAYLOAD];
  Writer w(payload, sizeof payload);
  w.u8(type);
  w.u8(seq);
  fn(w);
  if (!w.ok()) return 0;
  return encode_frame(payload, w.size(), wire, cap);
}

}  // namespace fnx
