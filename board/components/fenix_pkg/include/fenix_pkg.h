// Reader for fenixOS app packages, format 1 (docs/packages.md). No IDF dependency: the host test
// (test/run.ps1) builds it with MSVC. Must match crates/fenix-pkg (parse, check_refs, check_limits).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <string_view>
#include <vector>

namespace fpkg {

constexpr uint8_t FORMAT = 1;
constexpr size_t HEADER_LEN = 32;
constexpr uint16_t NONE = 0xFFFF;

constexpr size_t MAX_SIZE = 256 * 1024;
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
constexpr uint8_t A8 = 1, RGB565 = 2;
}
constexpr int32_t SIZE_CONTENT = -1;
constexpr int32_t SIZE_PCT = 0x40000000;

struct Elem {
  uint8_t kind;
  uint16_t parent;
  uint8_t nprops;
  const uint8_t* props;  // nprops × (key:u8 ‖ value:i32 LE)
  // Value of `key`; with `portrait`, a portrait-only value wins over the plain one.
  bool prop(uint8_t key, int32_t* v, bool portrait = false) const;
};

struct Asset {
  uint8_t format;
  uint16_t w, h;
  uint32_t len;
  const uint8_t* data;
};

struct Header {
  uint8_t min_fenixos[3];
  uint32_t tile_color;
  uint16_t icon_small, icon_large;
};

// A parsed package. It points into the bytes given to parse(); keep them alive while using it.
class Package {
 public:
  // nullptr on success, else a short reason.
  const char* parse(const uint8_t* data, size_t n);
  Header head{};
  std::vector<std::string_view> strings;
  std::vector<uint16_t> binds;  // string index of each binding name
  std::vector<Elem> elems;
  std::vector<Asset> assets;
  std::string_view id, name, version;

 private:
  const char* check();
};

bool valid_id(std::string_view id);

// Read only the header and string table (enough for id and version) from the start of a file.
// `n` may be shorter than the package; false if the strings do not fit in it.
bool peek(const uint8_t* data, size_t n, std::string_view* id, std::string_view* version);

}  // namespace fpkg
