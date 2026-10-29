// Minimal PNG writer: 8-bit RGB, zlib "stored" blocks (no compression), so no image library is
// needed. tools/sim_shots.py recompresses the golden copies.
#include <stdio.h>

#include "sim.h"

namespace sim {

static uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
  static uint32_t t[256];
  if (!t[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t v = i;
      for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      t[i] = v;
    }
  c ^= 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

static void be32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}

static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& body) {
  std::vector<uint8_t> c;
  be32(c, (uint32_t)body.size());
  c.insert(c.end(), type, type + 4);
  c.insert(c.end(), body.begin(), body.end());
  be32(c, crc32(c.data() + 4, c.size() - 4));
  fwrite(c.data(), 1, c.size(), f);
}

bool write_png(const char* path, int w, int h, const std::vector<uint16_t>& px) {
  std::vector<uint8_t> raw;
  raw.reserve((size_t)h * (1 + w * 3));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);  // filter: none
    for (int x = 0; x < w; x++) {
      uint16_t c = px[(size_t)y * w + x];
      uint8_t r = c >> 11, g = (c >> 5) & 63, b = c & 31;
      raw.push_back((uint8_t)(r << 3 | r >> 2));
      raw.push_back((uint8_t)(g << 2 | g >> 4));
      raw.push_back((uint8_t)(b << 3 | b >> 2));
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t off = 0; off < raw.size() || off == 0;) {
    size_t n = raw.size() - off < 65535 ? raw.size() - off : 65535;
    z.push_back(off + n == raw.size() ? 1 : 0);
    z.push_back((uint8_t)n);
    z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)~n);
    z.push_back((uint8_t)(~n >> 8));
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
    off += n;
    if (n == 0) break;
  }
  be32(z, b << 16 | a);

  FILE* f = fopen(path, "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, (uint32_t)w);
  be32(ihdr, (uint32_t)h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit RGB
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  return fclose(f) == 0;
}

}  // namespace sim
