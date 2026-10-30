// Reader for fenixOS app packages, format 1 (docs/packages.md). No IDF dependency: the host test
// (test/run.ps1) builds it with MSVC. Must match crates/fenix-pkg (parse, check_refs, check_limits).
//
// Since P39 a package is read in pieces (from a file or from memory): strings and element props are
// copied into the Package; asset pixels are not read, only their offsets, so a caller loads just the
// images it shows.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <string_view>
#include <vector>

namespace fpkg {

constexpr uint8_t FORMAT = 1;
constexpr size_t HEADER_LEN = 32;
constexpr uint16_t NONE = 0xFFFF;

constexpr size_t MAX_SIZE = 64 * 1024;
constexpr size_t MAX_ELEMS = 200;
constexpr size_t MAX_STRING_BYTES = 8 * 1024;
constexpr size_t MAX_BINDS = 64;
constexpr size_t MAX_IMAGE_BYTES = 24 * 1024;
constexpr size_t MAX_RAM = 40 * 1024;
constexpr size_t RAM_PER_ELEM = 160;
constexpr size_t MAX_DEPTH = 8;
constexpr size_t MAX_ID = 48;
constexpr int N_FONTS = 11;
constexpr int N_ICONS = 20;

namespace kind {
constexpr uint8_t BOX = 1, LABEL = 2, BUTTON = 3, SWITCH = 4, SLIDER = 5, BAR = 6, IMAGE = 7, ICON = 8;
}
namespace key {
constexpr uint8_t TEXT = 1, FONT = 2, COLOR = 3, BG = 4, BIND = 5, W = 6, H = 7, GROW = 8, GAP = 9, PAD = 10,
                  FLOW = 11, MAIN = 12, CROSS = 13, RADIUS = 14, MIN = 15, MAX = 16, VALUE = 17, ICON = 18,
                  SIZE = 19, SRC = 20, EVENT = 21, HIDDEN = 22, ALIGN = 23, PORTRAIT = 0x80;
}
namespace asset_format {
constexpr uint8_t A8 = 1, RGB565 = 2;  // RGB565 is little-endian (LVGL's native order)
}
constexpr int32_t SIZE_CONTENT = -1;
constexpr int32_t SIZE_PCT = 0x40000000;

struct Elem {
  uint8_t kind;
  uint16_t parent;
  uint8_t nprops;
  const uint8_t* props;  // nprops × (key:u8 ‖ value:i32 LE), in the Package's own copy
  // Value of `key`; with `portrait`, a portrait-only value wins over the plain one.
  bool prop(uint8_t key, int32_t* v, bool portrait = false) const;
};

struct Asset {
  uint8_t format;
  uint16_t w, h;
  uint32_t len;
  uint32_t offset;  // of the pixels, from the start of the package
};

struct Header {
  uint8_t min_fenixos[3];
  uint32_t tile_color;
  uint16_t icon_small, icon_large;
};

// Sequential bytes of one package. `pos` counts the bytes read or skipped so far.
class Source {
 public:
  virtual ~Source() = default;
  virtual bool read(void* dst, size_t n) = 0;
  virtual bool skip(size_t n) = 0;
  size_t pos = 0;
};

class MemSource : public Source {
 public:
  MemSource(const uint8_t* b, size_t n) : b_(b), n_(n) {}
  bool read(void* dst, size_t n) override;
  bool skip(size_t n) override;

 private:
  const uint8_t* b_;
  size_t n_;
};

// A package file of `size` bytes, read from its current position (the start). The caller closes it.
class FileSource : public Source {
 public:
  FileSource(FILE* f, size_t size) : f_(f), n_(size) {}
  bool read(void* dst, size_t n) override;
  bool skip(size_t n) override;

 private:
  FILE* f_;
  size_t n_;
};

// A parsed package. It owns its strings and props (`arena`) and can be moved, not copied.
class Package {
 public:
  Package() = default;
  Package(Package&&) = default;
  Package& operator=(Package&&) = default;
  Package(const Package&) = delete;
  Package& operator=(const Package&) = delete;

  // nullptr on success, else a short reason. `n` is the whole package's size.
  const char* parse(Source& src, size_t n);
  const char* parse(const uint8_t* data, size_t n);
  Header head{};
  std::vector<std::string_view> strings;  // each data() is NUL-terminated (a C string)
  std::vector<uint16_t> binds;  // string index of each binding name
  std::vector<Elem> elems;
  std::vector<Asset> assets;
  std::string_view id, name, version;

 private:
  const char* check();
  std::vector<uint8_t> arena;  // string bytes (each with a NUL), then element props
};

bool valid_id(std::string_view id);

// Read only the header and string table (enough for id and version) from the start of a file.
// `n` may be shorter than the package; false if the strings do not fit in it.
bool peek(const uint8_t* data, size_t n, std::string_view* id, std::string_view* version);

}  // namespace fpkg
