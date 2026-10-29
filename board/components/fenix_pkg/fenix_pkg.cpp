#include "fenix_pkg.h"

#include <string.h>

namespace fpkg {

static const uint8_t MAGIC[4] = {'F', 'X', 'P', '1'};

namespace {
struct Rd {
  const uint8_t* b;
  size_t n, i = 0;
  bool ok = true;
  const uint8_t* take(size_t k) {
    if (!ok || k > n - i) {
      ok = false;
      return nullptr;
    }
    const uint8_t* p = b + i;
    i += k;
    return p;
  }
  uint8_t u8() {
    const uint8_t* p = take(1);
    return p ? p[0] : 0;
  }
  uint16_t u16() {
    const uint8_t* p = take(2);
    return p ? (uint16_t)(p[0] | p[1] << 8) : 0;
  }
  uint32_t u32() {
    const uint8_t* p = take(4);
    return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
  }
};

int32_t le_i32(const uint8_t* p) {
  return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

// Header fields after the magic and format bytes; shared by parse and peek.
struct Counts {
  uint16_t ns, nb, ne, na, id, name, version;
};

bool read_header(Rd& r, Header& h, Counts& c) {
  const uint8_t* m = r.take(4);
  if (!m || memcmp(m, MAGIC, 4) != 0 || r.u8() != FORMAT) return false;
  r.u8();
  h.min_fenixos[0] = r.u8();
  h.min_fenixos[1] = r.u8();
  h.min_fenixos[2] = r.u8();
  r.u8();
  c.ns = r.u16();
  c.nb = r.u16();
  c.ne = r.u16();
  c.na = r.u16();
  c.id = r.u16();
  c.name = r.u16();
  c.version = r.u16();
  h.tile_color = r.u32();
  h.icon_small = r.u16();
  h.icon_large = r.u16();
  return r.ok;
}
}  // namespace

bool Elem::prop(uint8_t k, int32_t* v, bool portrait) const {
  bool found = false;
  for (int i = 0; i < nprops; i++) {
    const uint8_t* p = props + i * 5;
    if (portrait && p[0] == (k | key::PORTRAIT)) {
      *v = le_i32(p + 1);
      return true;
    }
    if (p[0] == k && !found) {
      *v = le_i32(p + 1);
      found = true;
    }
  }
  return found;
}

bool valid_id(std::string_view id) {
  if (id.empty() || id.size() > MAX_ID) return false;
  for (char c : id) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
              c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool peek(const uint8_t* data, size_t n, std::string_view* id, std::string_view* version) {
  Rd r{data, n};
  Header h;
  Counts c;
  if (!read_header(r, h, c) || c.id >= c.ns || c.version >= c.ns) return false;
  uint16_t last = c.id > c.version ? c.id : c.version;
  for (uint16_t i = 0; i <= last; i++) {
    uint16_t len = r.u16();
    const uint8_t* s = r.take(len);
    if (!s) return false;
    if (i == c.id) *id = std::string_view((const char*)s, len);
    if (i == c.version) *version = std::string_view((const char*)s, len);
  }
  return r.ok;
}

const char* Package::parse(const uint8_t* data, size_t n) {
  strings.clear();
  binds.clear();
  elems.clear();
  assets.clear();
  if (n > MAX_SIZE) return "package too big";
  Rd r{data, n};
  Counts c;
  if (!read_header(r, head, c)) return "not a fenixOS package";
  if (c.ne > MAX_ELEMS) return "too many elements";
  if (c.nb > MAX_BINDS) return "too many bindings";
  strings.reserve(c.ns);
  for (uint16_t i = 0; i < c.ns; i++) {
    uint16_t len = r.u16();
    const uint8_t* s = r.take(len);
    if (!s) return "truncated package";
    strings.emplace_back((const char*)s, len);
  }
  binds.reserve(c.nb);
  for (uint16_t i = 0; i < c.nb; i++) binds.push_back(r.u16());
  elems.reserve(c.ne);
  for (uint16_t i = 0; i < c.ne; i++) {
    Elem e;
    e.kind = r.u8();
    e.parent = r.u16();
    if (r.ok && e.parent != NONE && e.parent >= i) return "parent not before child";
    e.nprops = r.u8();
    e.props = r.take((size_t)e.nprops * 5);
    if (!r.ok) return "truncated package";
    elems.push_back(e);
  }
  assets.reserve(c.na);
  for (uint16_t i = 0; i < c.na; i++) {
    Asset a;
    a.format = r.u8();
    a.w = r.u16();
    a.h = r.u16();
    a.len = r.u32();
    a.data = r.take(a.len);
    if (!r.ok) return "truncated package";
    assets.push_back(a);
  }
  if (!r.ok) return "truncated package";
  if (r.i != n) return "trailing bytes";
  for (uint16_t s : {c.id, c.name, c.version}) {
    if (s >= strings.size()) return "string index out of range";
  }
  for (uint16_t s : binds) {
    if (s >= strings.size()) return "string index out of range";
  }
  id = strings[c.id];
  name = strings[c.name];
  version = strings[c.version];
  if (!valid_id(id)) return "bad app id";
  return check();
}

// Same checks as Package::check_refs and check_limits in crates/fenix-pkg.
const char* Package::check() {
  for (const Asset& a : assets) {
    size_t bpp = a.format == asset_format::A8 ? 1 : a.format == asset_format::RGB565 ? 2 : 0;
    if (!bpp) return "unknown asset format";
    if (a.len != (size_t)a.w * a.h * bpp) return "asset size mismatch";
  }
  for (uint16_t ix : {head.icon_small, head.icon_large}) {
    if (ix != NONE && ix >= assets.size()) return "icon asset out of range";
  }
  uint8_t depth[MAX_ELEMS];
  size_t image_bytes = 0;
  for (size_t i = 0; i < elems.size(); i++) {
    const Elem& e = elems[i];
    if (e.kind < kind::BOX || e.kind > kind::ICON) return "unknown element kind";
    depth[i] = e.parent == NONE ? 1 : depth[e.parent] + 1;
    if (depth[i] > MAX_DEPTH) return "tree too deep";
    for (int p = 0; p < e.nprops; p++) {
      uint8_t k = e.props[p * 5] & ~key::PORTRAIT;
      int32_t v = le_i32(e.props + p * 5 + 1);
      size_t limit;
      switch (k) {
        case key::TEXT:
        case key::EVENT: limit = strings.size(); break;
        case key::BIND: limit = binds.size(); break;
        case key::SRC: limit = assets.size(); break;
        case key::ICON: limit = N_ICONS; break;
        case key::FONT: limit = N_FONTS; break;
        default: continue;
      }
      if (v < 0 || (size_t)v >= limit) return "element index out of range";
    }
  }
  // Every asset except the two tile icons counts as image data.
  for (size_t i = 0; i < assets.size(); i++) {
    if (i != head.icon_small && i != head.icon_large) image_bytes += assets[i].len;
  }
  size_t string_bytes = 0;
  for (auto s : strings) string_bytes += s.size();
  if (string_bytes > MAX_STRING_BYTES) return "too many string bytes";
  if (image_bytes > MAX_IMAGE_BYTES) return "too many image bytes";
  if (elems.size() * RAM_PER_ELEM + string_bytes + image_bytes > MAX_RAM) return "too much RAM";
  return nullptr;
}

}  // namespace fpkg
