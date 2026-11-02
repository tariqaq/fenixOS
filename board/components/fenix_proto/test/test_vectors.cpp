// Host test: the C++ codec must decode every Rust golden vector (proto/vectors.txt)
// and re-encode it byte for byte. Build and run: board/components/fenix_proto/test/run.ps1
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fenix_proto.h"

using namespace fnx;

static std::vector<uint8_t> unhex(const std::string& h) {
  std::vector<uint8_t> v;
  for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back((uint8_t)std::stoi(h.substr(i, 2), nullptr, 16));
  return v;
}

// Parse the body with the typed reader, then write it again with the typed writer.
static bool reencode(const uint8_t* p, size_t n, std::vector<uint8_t>& out) {
  uint8_t buf[MAX_PAYLOAD];
  Writer w(buf, sizeof buf);
  w.u8(p[0]);
  w.u8(p[1]);
  Reader r(p + 2, n - 2);
  switch (p[0]) {
    case ty::HELLO: {
      uint8_t v;
      if (!read_hello(r, &v)) return false;
      write_hello(w, v);
      break;
    }
    case ty::HELLO_ACK: {
      HelloAck a;
      if (!read_hello_ack(r, a)) return false;
      write_hello_ack(w, a);
      break;
    }
    case ty::PING:
    case ty::PONG:
    case ty::TIME_SET:
    case ty::FW_ACK:
    case ty::APP_ACK: {
      uint32_t v = r.u32();
      if (!r.done()) return false;
      w.u32(v);
      break;
    }
    case ty::TOUCH: {
      uint16_t x = r.u16(), y = r.u16();
      uint8_t d = r.u8();
      if (!r.done()) return false;
      w.u16(x);
      w.u16(y);
      w.u8(d);
      break;
    }
    case ty::LOG:
    case ty::FW_RESULT:
    case ty::ERROR: {
      uint8_t c = r.u8();
      size_t len;
      const uint8_t* t = r.rest(&len);
      w.u8(c);
      w.bytes(t, len);
      break;
    }
    case ty::STATS: {
      Stats s;
      if (!read_stats(r, s)) return false;
      write_stats(w, s);
      break;
    }
    case ty::SYSINFO: {
      Sysinfo s;
      if (!read_sysinfo(r, s)) return false;
      write_sysinfo(w, s);
      uint8_t cnt = r.u8();
      w.u8(cnt);
      for (int i = 0; i < cnt; i++) {
        TaskInfo t;
        if (!read_task_info(r, t)) return false;
        write_task_info(w, t);
      }
      if (!r.done()) return false;
      break;
    }
    case ty::RADIO_STATE: {
      RadioState s;
      if (!read_radio_state(r, s)) return false;
      write_radio_state(w, s);
      break;
    }
    case ty::SD_STATE: {
      SdState s;
      if (!read_sd_state(r, s)) return false;
      write_sd_state(w, s);
      break;
    }
    case ty::RADIO_SET:
    case ty::APP_INDEX_REQ:
      w.u8(r.u8());
      if (!r.done()) return false;
      break;
    case ty::WIFI_SCAN: {
      w.u8(r.u8());
      uint8_t cnt = r.u8();
      w.u8(cnt);
      for (int i = 0; i < cnt; i++) {
        WifiAp a;
        if (!read_wifi_ap(r, a)) return false;
        write_wifi_ap(w, a);
      }
      if (!r.done()) return false;
      break;
    }
    case ty::WIFI_JOIN: {
      WifiJoin j;
      if (!read_wifi_join(r, j)) return false;
      write_wifi_join(w, j);
      break;
    }
    case ty::WEATHER_SET: {
      WeatherSet s;
      if (!read_weather_set(r, s)) return false;
      write_weather_set(w, s);
      break;
    }
    case ty::WIFI_SCAN_REQ:
    case ty::SYSINFO_REQ:
    case ty::REBOOT:
    case ty::APP_LIST_REQ:
    case ty::APP_INSTALL_END:
    case ty::FW_END:
    case ty::FILE_END:
    case ty::SHOT_SAVE:
    case ty::STREAM_STOP:
    case ty::STREAM_SYNC:
      if (!r.done()) return false;
      break;
    case ty::BOARD_STATE: {
      BoardState s;
      if (!read_board_state(r, s)) return false;
      write_board_state(w, s);
      break;
    }
    case ty::APP_LIST: {
      uint8_t cnt = r.u8();
      w.u8(cnt);
      for (int i = 0; i < cnt; i++) {
        AppEntry e;
        if (!read_app_entry(r, e)) return false;
        write_app_entry(w, e);
      }
      if (!r.done()) return false;
      break;
    }
    case ty::APP_INSTALL_BEGIN: {
      AppInstallBegin b;
      if (!read_app_install_begin(r, b)) return false;
      write_app_install_begin(w, b);
      break;
    }
    case ty::FW_BEGIN: {
      FwBegin b;
      if (!read_fw_begin(r, b)) return false;
      write_fw_begin(w, b);
      break;
    }
    case ty::FW_CHUNK:
    case ty::FILE_CHUNK:
    case ty::APP_CHUNK: {
      uint32_t off = r.u32();
      size_t len;
      const uint8_t* d = r.rest(&len);
      w.u32(off);
      w.bytes(d, len);
      break;
    }
    case ty::FILE_ACK: {
      uint32_t next = r.u32();
      if (!r.done()) return false;
      w.u32(next);
      break;
    }
    case ty::FILE_LIST_REQ: {
      uint16_t start = r.u16();
      char path[PATH_LEN];
      r.str(path, sizeof path);
      if (!r.done()) return false;
      w.u16(start);
      w.str(path);
      break;
    }
    case ty::FILE_LIST: {
      w.u16(r.u16());
      w.u16(r.u16());
      char path[PATH_LEN];
      r.str(path, sizeof path);
      w.str(path);
      while (r.ok() && r.remaining()) {
        uint8_t dir = r.u8();
        uint32_t size = r.u32();
        char name[PATH_LEN];
        r.str(name, sizeof name);
        w.u8(dir);
        w.u32(size);
        w.str(name);
      }
      if (!r.done()) return false;
      break;
    }
    case ty::FILE_READ_REQ: {
      uint32_t off = r.u32();
      uint16_t len = r.u16();
      char path[PATH_LEN];
      r.str(path, sizeof path);
      if (!r.done()) return false;
      w.u32(off);
      w.u16(len);
      w.str(path);
      break;
    }
    case ty::FILE_DATA: {
      uint32_t off = r.u32(), total = r.u32();
      size_t len;
      const uint8_t* d = r.rest(&len);
      w.u32(off);
      w.u32(total);
      w.bytes(d, len);
      break;
    }
    case ty::FILE_DELETE: {
      char path[PATH_LEN];
      r.str(path, sizeof path);
      if (!r.done()) return false;
      w.str(path);
      break;
    }
    case ty::FILE_WRITE_BEGIN: {
      uint32_t size = r.u32();
      const uint8_t* h = r.take(32);
      char path[PATH_LEN];
      r.str(path, sizeof path);
      if (!h || !r.done()) return false;
      w.u32(size);
      w.bytes(h, 32);
      w.str(path);
      break;
    }
    case ty::FILE_RESULT: {
      uint8_t op = r.u8(), code = r.u8();
      char path[PATH_LEN];
      r.str(path, sizeof path);
      size_t len;
      const uint8_t* t = r.rest(&len);
      w.u8(op);
      w.u8(code);
      w.str(path);
      w.bytes(t, len);
      break;
    }
    case ty::APP_RESULT: {
      uint8_t op = r.u8(), code = r.u8();
      char id[ID_LEN];
      r.str(id, sizeof id);
      size_t len;
      const uint8_t* t = r.rest(&len);
      w.u8(op);
      w.u8(code);
      w.str(id);
      w.bytes(t, len);
      break;
    }
    case ty::APP_INDEX: {
      w.u8(r.u8());
      w.u8(r.u8());
      uint8_t cnt = r.u8();
      w.u8(cnt);
      for (int i = 0; i < cnt; i++) {
        AppIndexEntry e;
        if (!read_app_index_entry(r, e)) return false;
        write_app_index_entry(w, e);
      }
      if (!r.done()) return false;
      break;
    }
    case ty::APP_REMOVE:
    case ty::APP_OPENED:
    case ty::APP_TURN_ON:
    case ty::APP_CLOSED: {
      char id[ID_LEN];
      r.str(id, sizeof id);
      if (!r.done()) return false;
      w.str(id);
      break;
    }
    case ty::HOST_STATE: {
      uint8_t run = r.u8();
      char id[ID_LEN];
      r.str(id, sizeof id);
      if (!r.done()) return false;
      w.u8(run);
      w.str(id);
      break;
    }
    case ty::BIND_SET: {
      BindSet b;
      if (!read_bind_set(r, b)) return false;
      write_bind_set(w, b);
      break;
    }
    case ty::BIND_IMAGE: {
      BindImage b;
      if (!read_bind_image(r, b)) return false;
      write_bind_image(w, b);
      break;
    }
    case ty::EVENT: {
      Event e;
      if (!read_event(r, e)) return false;
      write_event(w, e);
      break;
    }
    case ty::STREAM_START: {
      uint8_t f = r.u8();
      if (!r.done()) return false;
      w.u8(f);
      break;
    }
    case ty::STREAM_RECT: {
      RectHeader h;
      if (!read_rect_header(r, h)) return false;
      size_t len;
      const uint8_t* d = r.rest(&len);
      write_rect_header(w, h);
      w.bytes(d, len);
      break;
    }
    case ty::STREAM_INFO: {
      uint16_t a = r.u16(), b = r.u16();
      uint8_t c = r.u8();
      if (!r.done()) return false;
      w.u16(a);
      w.u16(b);
      w.u8(c);
      break;
    }
    default:
      return false;
  }
  if (!w.ok()) return false;
  out.assign(buf, buf + w.size());
  return true;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: t <vectors.txt>\n");
    return 2;
  }
  std::ifstream f(argv[1]);
  std::string line;
  int n = 0, bad = 0;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string name, seq, hex;
    ss >> name >> seq >> hex;
    auto wire = unhex(hex);
    n++;
    // 1) decode through the streaming decoder
    struct Ctx {
      std::vector<uint8_t> payload;
      int count = 0;
    } ctx;
    FrameDecoder d;
    d.push(wire.data(), wire.size(), [](const uint8_t* p, size_t len, void* c) {
      auto* x = static_cast<Ctx*>(c);
      x->payload.assign(p, p + len);
      x->count++;
    }, &ctx);
    if (ctx.count != 1) {
      printf("FAIL %s: decode\n", name.c_str());
      bad++;
      continue;
    }
    // 2) typed re-encode of the payload, then frame it again
    std::vector<uint8_t> payload;
    if (!reencode(ctx.payload.data(), ctx.payload.size(), payload)) {
      printf("FAIL %s: body\n", name.c_str());
      bad++;
      continue;
    }
    uint8_t out[MAX_WIRE];
    size_t len = encode_frame(payload.data(), payload.size(), out, sizeof out);
    if (len != wire.size() || memcmp(out, wire.data(), len) != 0) {
      printf("FAIL %s: re-encode differs\n", name.c_str());
      bad++;
      continue;
    }
  }
  printf("%d vectors, %d failed\n", n, bad);
  return bad || n == 0 ? 1 : 0;
}
