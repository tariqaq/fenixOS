#include "fenix_pkg.h"

#include <string.h>

namespace fpkg {

static const uint8_t MAGIC[4] = {'F', 'X', 'P', '1'};

bool MemSource::read(void* dst, size_t n) {
  if (n > n_ - pos) return false;
  memcpy(dst, b_ + pos, n);
  pos += n;
  return true;
}

bool MemSource::skip(size_t n) {
  if (n > n_ - pos) return false;
  pos += n;
  return true;
}

bool FileSource::read(void* dst, size_t n) {
  if (n > n_ - pos || fread(dst, 1, n, f_) != n) return false;
  pos += n;
  return true;
}

bool FileSource::skip(size_t n) {
  if (n > n_ - pos || fseek(f_, (long)n, SEEK_CUR) != 0) return false;
  pos += n;
  return true;
}

namespace {
// Little-endian fields from a Source; after the first short read every value is 0 and ok is false.
struct Rd {
  Source& s;
  bool ok = true;
  bool get(void* d, size_t n) {
    if (ok && !s.read(d, n)) ok = false;
    if (!ok) memset(d, 0, n);
    return ok;
  }
  uint8_t u8() {
    uint8_t p[1];
    get(p, 1);
    return p[0];
  }
  uint16_t u16() {
    uint8_t p[2];
    get(p, 2);
    return (uint16_t)(p[0] | p[1] << 8);
  }
  uint32_t u32() {
    uint8_t p[4];
    get(p, 4);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
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
  uint8_t m[4];
  if (!r.get(m, 4) || memcmp(m, MAGIC, 4) != 0 || r.u8() != FORMAT) return false;
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
  MemSource src(data, n);
  Rd r{src};
  Header h;
  Counts c;
  if (!read_header(r, h, c) || c.id >= c.ns || c.version >= c.ns) return false;
  uint16_t last = c.id > c.version ? c.id : c.version;
  for (uint16_t i = 0; i <= last; i++) {
    uint16_t len = r.u16();
    size_t at = src.pos;
    if (!r.ok || !src.skip(len)) return false;
    if (i == c.id) *id = std::string_view((const char*)data + at, len);
    if (i == c.version) *version = std::string_view((const char*)data + at, len);
  }
  return r.ok;
}

const char* Package::parse(const uint8_t* data, size_t n) {
  MemSource src(data, n);
  return parse(src, n);
}

const char* Package::parse(Source& src, size_t n) {
  strings.clear();
  binds.clear();
  elems.clear();
  assets.clear();
  arena.clear();
  id = name = version = {};
  if (n > MAX_SIZE) return "package too big";
  Rd r{src};
  Counts c;
  if (!read_header(r, head, c)) return "not a fenixOS package";
  if (c.ne > MAX_ELEMS) return "too many elements";
  if (c.nb > MAX_BINDS) return "too many bindings";
  // Strings and props go into the arena by offset; views are made once it stops growing. It holds
  // bytes read from the package plus one NUL per string, so it stays under twice `n`.
  // Each string gets a NUL after it, so a view's data() is also a C string.
  std::vector<uint32_t> at;
  std::vector<uint16_t> lens;
  at.reserve(c.ns + c.ne);
  lens.reserve(c.ns);
  for (uint16_t i = 0; i < c.ns; i++) {
    uint16_t len = r.u16();
    at.push_back((uint32_t)arena.size());
    lens.push_back(len);
    arena.resize(arena.size() + len + 1);
    if (!r.get(arena.data() + at.back(), len)) return "truncated package";
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
    e.props = nullptr;
    size_t len = (size_t)e.nprops * 5;
    at.push_back((uint32_t)arena.size());
    arena.resize(arena.size() + len);
    if (!r.get(arena.data() + at.back(), len)) return "truncated package";
    elems.push_back(e);
  }
  assets.reserve(c.na);
  for (uint16_t i = 0; i < c.na; i++) {
    Asset a;
    a.format = r.u8();
    a.w = r.u16();
    a.h = r.u16();
    a.len = r.u32();
    a.offset = (uint32_t)src.pos;
    if (!r.ok || !src.skip(a.len)) return "truncated package";
    assets.push_back(a);
  }
  if (!r.ok) return "truncated package";
  if (src.pos != n) return "trailing bytes";
  strings.reserve(c.ns);
  for (uint16_t i = 0; i < c.ns; i++) strings.emplace_back((const char*)arena.data() + at[i], lens[i]);
  for (uint16_t i = 0; i < c.ne; i++) elems[i].props = arena.data() + at[c.ns + i];
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
