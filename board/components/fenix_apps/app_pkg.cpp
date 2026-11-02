// Host apps (docs/packages.md): Home tiles for installed packages, and the app screen built from
// a package. One app is open at a time. Packages are read from the SD card or flash in pieces
// (P39): tiles read the two icons only, and only for the tiles Home shows (P51); an open app keeps
// its strings, props and the images its elements show, from which its LVGL objects are rebuilt
// (rotation, host state). Artboards 6a, 6b. The last bound values are kept on the SD card (P53):
// with no PC the app opens on them, dimmed, under "PC not connected".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "fenix_pkg.h"
#include "fenix_proto.h"
#include "shell.h"

namespace fx::apps {

using namespace fx::ui;

// ---- Installed apps (tiles) ----

static std::vector<std::string> s_ids;  // ids at the last reload, to tell installs from removals
static char s_ghost[fpkg::MAX_ID + 1];  // removed tile still fading out on Home; empty = none
static char s_fade_in[fpkg::MAX_ID + 1];

static lv_image_dsc_t a8_dsc(const uint8_t* px, uint16_t w, uint16_t h) {
  lv_image_dsc_t d{};
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_A8;
  d.header.w = w;
  d.header.h = h;
  d.header.stride = w;
  d.data_size = (uint32_t)w * h;
  d.data = px;
  return d;
}

void app_icons_fix(AppIcons& t) {
  t.small.data = t.small.data_size ? t.px.data() : nullptr;
  t.large.data = t.large.data_size ? t.px.data() + t.small.data_size : nullptr;
}

// Parse an installed package; `f` stays open for reading assets (caller closes).
static const char* read_pkg(const char* id, fpkg::Package& p, FILE** f) {
  size_t n = 0;
  *f = sys::store_open(id, &n);
  if (!*f) return "missing";
  fpkg::FileSource src(*f, n);
  return p.parse(src, n);
}

static bool read_at(FILE* f, const fpkg::Asset& a, uint8_t* out) {
  return fseek(f, (long)a.offset, SEEK_SET) == 0 && fread(out, 1, a.len, f) == a.len;
}

static bool known(const char* id) {
  for (const std::string& s : s_ids)
    if (s == id) return true;
  return false;
}

void apps_reload() {
  s_ids.clear();
  sys::AppMeta m;
  for (int i = 0; sys::store_at(i, &m); i++) s_ids.emplace_back(m.id);
}

int app_count() { return sys::store_count(); }
bool app_at(int i, sys::AppMeta* out) { return sys::store_at(i, out); }

bool app_icons(const char* id, AppIcons& t) {
  t = AppIcons{};
  fpkg::Package p;
  FILE* f = nullptr;
  if (read_pkg(id, p, &f)) {
    if (f) fclose(f);
    return false;
  }
  const fpkg::Asset* s = p.head.icon_small != fpkg::NONE ? &p.assets[p.head.icon_small] : nullptr;
  const fpkg::Asset* l = p.head.icon_large != fpkg::NONE ? &p.assets[p.head.icon_large] : nullptr;
  // Tile icons are A8 by construction (fenix-pkg); anything else shows the puzzle glyph.
  if (s && s->format != fpkg::asset_format::A8) s = nullptr;
  if (l && l->format != fpkg::asset_format::A8) l = nullptr;
  t.px.resize((s ? s->len : 0) + (l ? l->len : 0));
  if ((s && !read_at(f, *s, t.px.data())) || (l && !read_at(f, *l, t.px.data() + (s ? s->len : 0)))) {
    t.px.clear();
    s = l = nullptr;
  }
  fclose(f);
  if (s) t.small = a8_dsc(nullptr, s->w, s->h);
  if (l) t.large = a8_dsc(nullptr, l->w, l->h);
  app_icons_fix(t);
  return s || l;
}

const char* app_ghost() { return s_ghost[0] ? s_ghost : nullptr; }
void app_ghost_done() { s_ghost[0] = 0; }
bool app_take_fade_in(const char* id) {
  if (strcmp(s_fade_in, id) != 0) return false;
  s_fade_in[0] = 0;
  return true;
}

// ---- The open app ----

struct Val {
  bool set = false;
  uint8_t kind = 0;
  int32_t i = 0;
  std::string text;
};
struct Bound {
  uint16_t bind;
  uint8_t kind;  // element kind
  lv_obj_t* obj;
  bool show;      // the element's `show` binding, not its `bind`
  uint16_t elem;  // the element's index
};

static struct {
  bool open;
  fpkg::Package pkg;
  char id[fpkg::MAX_ID + 1];
  char name[32];
  std::vector<Val> vals;  // last value per binding, so rebuilds keep what the PC set
  std::vector<Bound> bound;
  std::vector<lv_obj_t*> objs;  // per element, null if not built; for view and show (P50-M)
  std::vector<uint16_t> pos;    // per element, its place among its parent's children
  std::vector<lv_image_dsc_t> images;  // per asset; data null unless an element shows it
  std::vector<uint8_t*> pixels;        // what the descriptors point at (malloc)
  // The bound image (P50-M): its descriptor is images[art] (after the assets' ones), its buffer
  // the last of `pixels`; the link fills it (sys::bind_image_target). -1 = none.
  int art;
  bool art_ready;     // a whole image arrived since the app opened
  lv_obj_t* art_obj;  // the element showing it in the current build
  int64_t close_at;  // pending return to Home (app removed while showing HostOff)
  bool missing;      // stored on the SD card, which is out: no package loaded (P51)
  bool turning_on;   // Turn on was pressed; waiting for the PC's HOST_STATE
  bool dirty;        // values changed since they were last saved to the card (P53)
  int64_t saved_at;
} s_app;

// A closed app's package, images and pixels, kept until its screen is deleted: labels show the
// package's strings in place (lv_label_set_text_static) and images point at its descriptors, and
// the screen still draws while it slides out. Freeing them at close read freed memory (P45: a
// label then ran on through garbage; the simulator hung once in ~30 runs).
static struct {
  fpkg::Package pkg;
  std::vector<lv_image_dsc_t> images;
  std::vector<uint8_t*> pixels;
} s_retired;

static void free_retired(lv_event_t* = nullptr) {
  for (uint8_t* px : s_retired.pixels) free(px);
  s_retired.pixels.clear();
  s_retired.images.clear();
  s_retired.pkg = fpkg::Package();
}

// HOST_STATE per app, as the PC last said; cleared when the link drops. No entry = running.
struct Host {
  std::string id;
  uint8_t state;  // fnx::host
};
static std::vector<Host> s_host;

uint8_t app_host(const char* id) {
  for (const Host& h : s_host)
    if (h.id == id) return h.state;
  return fnx::host::RUNNING;
}

const char* open_app_id() { return s_app.open ? s_app.id : ""; }

static void send_id(uint8_t type, const char* id) {
  uint8_t b[fnx::ID_LEN + 2];
  fnx::Writer w(b, sizeof b);
  w.str(id);
  sys::link_send(type, b, w.size());
}

static void send_event(uint16_t elem, uint8_t kind, int32_t value) {
  fnx::Event e{};
  e.elem = elem;
  e.kind = kind;
  e.value = value;
  strlcpy(e.id, s_app.id, sizeof e.id);
  uint8_t b[fnx::ID_LEN + 16];
  fnx::Writer w(b, sizeof b);
  fnx::write_event(w, e);
  sys::link_send(fnx::ty::EVENT, b, w.size());
}

// ---- Last values on the card (P53) ----
// state/<id>.bin: "FXS1", a count, then per set binding: its name (u8 length + bytes), kind u8,
// value i32 LE, text (u8 length + bytes). Matched by name, so an update keeps the values of the
// bindings it still has. Saved when the app closes and every 60 s while it is open and changed.

static constexpr int64_t STATE_EVERY_US = 60000000;
static constexpr size_t STATE_MAX = 4096;

static std::string_view bind_name(size_t k) { return s_app.pkg.strings[s_app.pkg.binds[k]]; }

static void state_save() {
  s_app.dirty = false;
  s_app.saved_at = esp_timer_get_time();
  std::vector<uint8_t> b{'F', 'X', 'S', '1', 0};
  for (size_t k = 0; k < s_app.vals.size(); k++) {
    const Val& v = s_app.vals[k];
    std::string_view name = bind_name(k);
    size_t tn = v.kind == 2 ? (v.text.size() < 255 ? v.text.size() : 255) : 0;
    if (!v.set || name.size() > 255 || b[4] == 255 || b.size() + 2 + name.size() + 6 + tn > STATE_MAX) continue;
    b[4]++;
    b.push_back(name.size());
    b.insert(b.end(), name.begin(), name.end());
    b.push_back(v.kind);
    for (int i = 0; i < 4; i++) b.push_back((uint32_t)v.i >> (8 * i));
    b.push_back(tn);
    b.insert(b.end(), v.text.begin(), v.text.begin() + tn);
  }
  if (!sys::app_state_write(s_app.id, b.data(), b.size())) s_app.dirty = true;  // no card: later
}

static void state_load() {
  std::unique_ptr<uint8_t[]> b(new (std::nothrow) uint8_t[STATE_MAX]);
  if (!b) return;
  size_t n = sys::app_state_read(s_app.id, b.get(), STATE_MAX);
  if (n < 5 || memcmp(b.get(), "FXS1", 4) != 0) return;
  size_t at = 5;
  for (int e = 0; e < b[4]; e++) {
    if (at + 1 > n || at + 1 + b[at] + 6 > n) return;
    std::string_view name((const char*)&b[at + 1], b[at]);
    at += 1 + b[at];
    uint8_t kind = b[at];
    int32_t i = (int32_t)(b[at + 1] | b[at + 2] << 8 | b[at + 3] << 16 | (uint32_t)b[at + 4] << 24);
    size_t tn = b[at + 5];
    at += 6;
    if (at + tn > n) return;
    for (size_t k = 0; k < s_app.vals.size(); k++) {
      if (bind_name(k) != name) continue;
      Val& v = s_app.vals[k];
      v.set = true;
      v.kind = kind;
      v.i = i;
      v.text.assign((const char*)&b[at], tn);
    }
    at += tn;
  }
}

static bool has_vals() {
  for (const Val& v : s_app.vals)
    if (v.set) return true;
  return false;
}

static void close_app() {
  if (!s_app.open) return;
  sys::bind_image_target(0, 0, nullptr, 0, 0);  // the link stops writing before the buffer retires
  s_app.art = -1;
  s_app.art_obj = nullptr;
  if (s_app.dirty && !s_app.missing) state_save();
  if (sys::link_connected() && !s_app.missing) send_id(fnx::ty::APP_CLOSED, s_app.id);
  s_app.open = false;
  // The app's screen may outlive this call (app_tick closes the app once another screen is on
  // top, and a slide still draws it); its delete event frees what is retired here
  // (screen_deleted). At most one app screen exists at a time, so older leftovers are free.
  free_retired();
  s_retired.pkg = std::move(s_app.pkg);  // moves keep the buffers, so the screen's pointers stay valid
  s_retired.images = std::move(s_app.images);
  s_retired.pixels = std::move(s_app.pixels);
  s_app.pkg = fpkg::Package();
  s_app.images.clear();
  s_app.pixels.clear();
  s_app.vals.clear();
  s_app.bound.clear();
  s_app.objs.clear();
  s_app.pos.clear();
  s_app.close_at = 0;
  s_app.missing = false;
  s_app.turning_on = false;
  s_app.dirty = false;
}

static bool load_app(const char* id) {
  FILE* f = nullptr;
  const char* err = read_pkg(id, s_app.pkg, &f);
  const fpkg::Package& p = s_app.pkg;
  // Only images an element shows are read (in either orientation); tile icons stay in flash.
  std::vector<bool> shown(err ? 0 : p.assets.size(), false);
  for (const fpkg::Elem& e : p.elems) {
    int32_t src;
    if (e.kind != fpkg::kind::IMAGE) continue;
    if (e.prop(fpkg::key::SRC, &src)) shown[src] = true;
    if (e.prop(fpkg::key::SRC, &src, true)) shown[src] = true;
  }
  s_app.pixels.assign(shown.size(), nullptr);
  for (size_t i = 0; !err && i < shown.size(); i++) {
    if (!shown[i]) continue;
    s_app.pixels[i] = (uint8_t*)malloc(p.assets[i].len);
    if (!s_app.pixels[i]) err = "out of memory";
    else if (!read_at(f, p.assets[i], s_app.pixels[i])) err = "read failed";
  }
  if (f) fclose(f);
  if (err) {
    sys::link_log(2, "app %s: %s", id, err);
    for (uint8_t* px : s_app.pixels) free(px);
    s_app.pixels.clear();
    s_app.pkg = fpkg::Package();
    return false;
  }
  s_app.open = true;
  strlcpy(s_app.id, id, sizeof s_app.id);
  snprintf(s_app.name, sizeof s_app.name, "%.*s", (int)p.name.size(), p.name.data());
  s_app.vals.assign(p.binds.size(), Val{});
  state_load();
  s_app.dirty = false;
  s_app.saved_at = esp_timer_get_time();
  s_app.images.clear();
  for (size_t i = 0; i < p.assets.size(); i++) {
    const fpkg::Asset& a = p.assets[i];
    lv_image_dsc_t d = a8_dsc(s_app.pixels[i], a.w, a.h);
    if (a.format == fpkg::asset_format::RGB565) {
      d.header.cf = LV_COLOR_FORMAT_RGB565;
      d.header.stride = a.w * 2;
      d.data_size = a.len;
    }
    s_app.images.push_back(d);
  }
  // The bound image's buffer: allocated now, counted in the package's RAM estimate (P50-M).
  s_app.art = -1;
  s_app.art_ready = false;
  for (const fpkg::Elem& e : p.elems) {
    int32_t bind, w, h;
    if (e.kind != fpkg::kind::IMAGE || !e.prop(fpkg::key::BIND, &bind)) continue;
    if (!e.prop(fpkg::key::W, &w) || !e.prop(fpkg::key::H, &h)) break;  // the reader checked both
    uint8_t* buf = (uint8_t*)calloc((size_t)w * h, 2);
    if (!buf) {
      sys::link_log(2, "app %s: no memory for its %ldx%ld image", id, (long)w, (long)h);
      break;
    }
    lv_image_dsc_t d{};
    d.header.magic = LV_IMAGE_HEADER_MAGIC;
    d.header.cf = LV_COLOR_FORMAT_RGB565;
    d.header.w = w;
    d.header.h = h;
    d.header.stride = w * 2;
    d.data_size = (uint32_t)w * h * 2;
    d.data = buf;
    s_app.art = (int)s_app.images.size();
    s_app.images.push_back(d);
    s_app.pixels.push_back(buf);
    sys::bind_image_target(sys::app_hash(id), (uint16_t)bind, buf, w, h);
    break;
  }
  return true;
}

// App open slides like Settings and Stream when the heap allows (P57, user): Home stays until
// the slide ends, so the app's objects are built next to it. With less than SLIDE_MARGIN left
// after them (a big layout with album art), it falls back to the P50-M fade from black, which
// deletes Home first. The pop back goes the same way (nav.cpp). The package, its strings and the
// art buffer are already in RAM here, so only the objects are estimated.
static constexpr size_t SLIDE_MARGIN = 28 * 1024;

static bool slide_fits(size_t built) {
  size_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  bool slide = free > built * fpkg::RAM_PER_ELEM + SLIDE_MARGIN;
  sys::link_log(4, "app open: %s (%u B free, %u elements)", slide ? "slide" : "fade", (unsigned)free,
                (unsigned)built);
  return slide;
}

void app_open(const char* id) {
  close_app();
  sys::AppMeta m;
  if (!sys::store_find(id, &m)) return;
  if (m.state == fnx::app_state::CARD_MISSING) {
    // "Insert the SD card" (P51): the card is looked for now, and the app opens for real when
    // the card's apps come back (APPS_CHANGED with an empty id).
    s_app.open = s_app.missing = true;
    strlcpy(s_app.id, m.id, sizeof s_app.id);
    strlcpy(s_app.name, m.name, sizeof s_app.name);
    sys::sd_probe();
    nav_push(app_screen, !slide_fits(0));
    return;
  }
  if (!load_app(id)) return;
  nav_push(app_screen, !slide_fits(s_app.pkg.built));
  if (sys::link_connected()) send_id(fnx::ty::APP_OPENED, s_app.id);
}

// ---- Building the screen from the package ----

static const lv_font_t* const FONTS[fpkg::N_FONTS] = {
    &fx_font_body,    &fx_font_body_semibold, &fx_font_label,    &fx_font_label_medium,
    &fx_font_label_semibold, &fx_font_caption, &fx_font_caption_medium, &fx_font_title,
    &fx_font_title_l, &fx_font_clock_56,      &fx_font_clock_64,
};
// Order of the built-in icon list in docs/packages.md (and fenix-pkg ICONS).
static const char* const ICONS[fpkg::N_ICONS] = {
    FX_ICON_GEAR_SIX,      FX_ICON_POWER,       FX_ICON_SCREENCAST,    FX_ICON_ARROW_CLOCKWISE,
    FX_ICON_PLUGS_CONNECTED, FX_ICON_PLUGS,     FX_ICON_SUN,           FX_ICON_MOON,
    FX_ICON_TIMER,         FX_ICON_DEVICE_ROTATE, FX_ICON_HAND_TAP,    FX_ICON_HARD_DRIVES,
    FX_ICON_DESKTOP_TOWER, FX_ICON_INFO,        FX_ICON_SQUARES_FOUR,  FX_ICON_WARNING,
    FX_ICON_PUZZLE_PIECE,  FX_ICON_CHECK_CIRCLE, FX_ICON_X_CIRCLE,     FX_ICON_MONITOR,
    // P50-M (v0.1.25)
    FX_ICON_PLAY,          FX_ICON_PAUSE,       FX_ICON_SKIP_FORWARD,  FX_ICON_SKIP_BACK,
    FX_ICON_SPEAKER_HIGH,  FX_ICON_SPEAKER_X,   FX_ICON_MICROPHONE,    FX_ICON_MICROPHONE_SLASH,
    FX_ICON_HEADPHONES,    FX_ICON_GAME_CONTROLLER, FX_ICON_MUSIC_NOTES, FX_ICON_VIDEO,
    FX_ICON_PHONE,         FX_ICON_SPARKLE,
};

// An int value as a glyph: an icon index, or "" (none) out of range.
static const char* glyph_of(int32_t i) { return i >= 0 && i < fpkg::N_ICONS ? ICONS[i] : ""; }

static const lv_font_t* icon_font(int px) {
  static const struct {
    int px;
    const lv_font_t* f;
  } SIZES[] = {{12, &fx_icon_fill_12}, {14, &fx_icon_fill_14}, {15, &fx_icon_fill_15},
               {16, &fx_icon_fill_16}, {18, &fx_icon_fill_18}, {20, &fx_icon_fill_20},
               {26, &fx_icon_fill_26}, {28, &fx_icon_fill_28}, {40, &fx_icon_fill_40}};
  for (auto& s : SIZES)
    if (px <= s.px) return s.f;
  return &fx_icon_fill_40;
}

static int32_t lv_size(int32_t v) {
  if (v == fpkg::SIZE_CONTENT) return LV_SIZE_CONTENT;
  if (v & fpkg::SIZE_PCT) return LV_PCT(v & 0xFFFF);
  return v;
}

static lv_flex_align_t flex_align(int32_t v) {
  switch (v) {
    case 1: return LV_FLEX_ALIGN_CENTER;
    case 2: return LV_FLEX_ALIGN_END;
    case 3: return LV_FLEX_ALIGN_SPACE_BETWEEN;
    default: return LV_FLEX_ALIGN_START;
  }
}

static void apply(const Bound& b, const Val& v) {
  switch (b.kind) {
    case fpkg::kind::LABEL: {
      char n[16];
      if (v.kind == 2)
        lv_label_set_text(b.obj, v.text.c_str());
      else if (v.kind == 1)
        lv_label_set_text(b.obj, v.i ? "On" : "Off");
      else {
        snprintf(n, sizeof n, "%ld", (long)v.i);
        lv_label_set_text(b.obj, n);
      }
      break;
    }
    case fpkg::kind::SWITCH:
      if (v.i)
        lv_obj_add_state(b.obj, LV_STATE_CHECKED);
      else
        lv_obj_remove_state(b.obj, LV_STATE_CHECKED);
      break;
    case fpkg::kind::SLIDER:
      if (!lv_slider_is_dragged(b.obj)) lv_slider_set_value(b.obj, v.i, LV_ANIM_OFF);
      break;
    case fpkg::kind::BAR:
      lv_bar_set_value(b.obj, v.i, LV_ANIM_ON);
      break;
    case fpkg::kind::ICON:  // an int picks the glyph (P50-M)
      if (v.kind == 0) lv_label_set_text(b.obj, glyph_of(v.i));
      break;
    case fpkg::kind::BUTTON: {  // text sets the text, an int the glyph (P50-M); see make()
      uint32_t n = lv_obj_get_child_count(b.obj);
      lv_obj_t* g = lv_obj_get_child(b.obj, 0);
      if (v.kind == 2 && n == 2) {
        lv_label_set_text(lv_obj_get_child(b.obj, 1), v.text.c_str());
      } else if (v.kind == 0 && g) {
        const char* s = glyph_of(v.i);
        lv_label_set_text(g, s);
        if (n == 2) {  // a capsule drops an empty glyph, so its gap does not shift the text
          if (*s) lv_obj_remove_flag(g, LV_OBJ_FLAG_HIDDEN);
          else lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
        }
      }
      break;
    }
    default:
      break;
  }
}

// The user changed a bound control: keep the cache in step so a rebuild shows the same value.
static void remember(uint16_t elem, int32_t value) {
  int32_t bind;
  if (!s_app.pkg.elems[elem].prop(fpkg::key::BIND, &bind)) return;
  Val& v = s_app.vals[bind];
  v.set = true;
  v.kind = 0;
  v.i = value;
  s_app.dirty = true;
}

static void elem_cb(lv_event_t* e) {
  uint16_t idx = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED:
      send_event(idx, fnx::event::TAP, 0);
      break;
    case LV_EVENT_LONG_PRESSED:
      send_event(idx, fnx::event::LONG, 0);
      break;
    case LV_EVENT_VALUE_CHANGED: {  // switch
      int32_t on = lv_obj_has_state(o, LV_STATE_CHECKED);
      remember(idx, on);
      send_event(idx, fnx::event::CHANGE, on);
      break;
    }
    case LV_EVENT_RELEASED: {  // slider: report on release only
      int32_t v = lv_slider_get_value(o);
      remember(idx, v);
      send_event(idx, fnx::event::CHANGE, v);
      break;
    }
    default:
      break;
  }
}

// `grow` shares the parent's free space along its flow. A parent sized to its content has none,
// and LVGL would squeeze the element to 0 (a row turned column in portrait), so it only applies
// when the parent has a set size along its flow. The root's parent is the app body (fixed).
static bool grow_applies(const fpkg::Elem& e, bool port) {
  if (e.parent == fpkg::NONE) return true;
  const fpkg::Elem& p = s_app.pkg.elems[e.parent];
  int32_t flow = 0, size;
  p.prop(fpkg::key::FLOW, &flow, port);
  bool set = p.prop(flow == 0 ? fpkg::key::H : fpkg::key::W, &size, port);
  return set && size != fpkg::SIZE_CONTENT;
}

static lv_obj_t* make(const fpkg::Elem& e, uint16_t idx, lv_obj_t* parent, bool port) {
  auto get = [&](uint8_t k, int32_t def) {
    int32_t v;
    return e.prop(k, &v, port) ? v : def;
  };
  int32_t text_ix = get(fpkg::key::TEXT, -1);
  const char* label = text_ix >= 0 ? s_app.pkg.strings[text_ix].data() : "";  // NUL-terminated
  int32_t icon = get(fpkg::key::ICON, -1), v0;
  lv_obj_t* o = nullptr;
  switch (e.kind) {
    case fpkg::kind::BOX:
    case fpkg::kind::VIEW: {  // a view lays out like a box; refresh_shown() hides all but one child
      o = column(parent, 0);
      lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      int32_t flow = get(fpkg::key::FLOW, 0);
      lv_obj_set_flex_flow(o, flow == 1 ? LV_FLEX_FLOW_ROW : flow == 2 ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
      int32_t gap = get(fpkg::key::GAP, 0);
      lv_obj_set_style_pad_row(o, gap, 0);
      lv_obj_set_style_pad_column(o, gap, 0);
      lv_obj_set_style_pad_all(o, get(fpkg::key::PAD, 0), 0);
      lv_flex_align_t cross = flex_align(get(fpkg::key::CROSS, 0));
      lv_obj_set_flex_align(o, flex_align(get(fpkg::key::MAIN, 0)), cross, cross);
      int32_t bg;
      if (e.prop(fpkg::key::BG, &bg, port)) {
        lv_obj_set_style_bg_color(o, col(bg), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
      }
      lv_obj_set_style_radius(o, get(fpkg::key::RADIUS, 0), 0);
      break;
    }
    case fpkg::kind::LABEL: {
      // Static text from the package's own copy (lives while the app is open): no second copy.
      o = text(parent, "", FONTS[get(fpkg::key::FONT, 0)], get(fpkg::key::COLOR, FX_COLOR_LABEL));
      lv_label_set_text_static(o, label);
      int32_t al = get(fpkg::key::ALIGN, 0);
      lv_obj_set_style_text_align(o, al == 1 ? LV_TEXT_ALIGN_CENTER : al == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT,
                                  0);
      // Long text (P51-M): wrap (default), clip or ellipsis after max_lines (default 1; the height
      // stays the text's up to that), or one line scrolling round (marquee). Needs a set width.
      static const lv_label_long_mode_t MODES[] = {LV_LABEL_LONG_MODE_WRAP, LV_LABEL_LONG_MODE_CLIP,
                                                   LV_LABEL_LONG_MODE_DOTS, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR};
      int32_t ov = get(fpkg::key::TEXT_OVERFLOW, 0);
      if (ov > 0 && ov < 4) {
        lv_label_set_long_mode(o, MODES[ov]);
        if (ov != 3) {
          int32_t n = LV_CLAMP(1, get(fpkg::key::MAX_LINES, 1), 8);
          int32_t lh = lv_font_get_line_height(FONTS[get(fpkg::key::FONT, 0)]);
          lv_obj_set_style_max_height(o, n * lh + (n - 1) * lv_obj_get_style_text_line_space(o, LV_PART_MAIN), 0);
        }
      }
      break;
    }
    case fpkg::kind::BUTTON: {
      uint32_t bg = get(fpkg::key::BG, FX_COLOR_ACCENT);
      const char* glyph = icon >= 0 ? ICONS[icon] : nullptr;
      if (!*label && glyph) {  // icon button
        o = rowbox(parent, 0);
        lv_obj_set_size(o, 40, 40);
        lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o, col(bg), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, lv_color_mix(col(FX_COLOR_LABEL), col(bg), LV_OPA_20), LV_STATE_PRESSED);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        text(o, glyph, &fx_icon_fill_18, get(fpkg::key::COLOR, FX_COLOR_LABEL));
      } else {
        // A bound capsule always gets a glyph label (children: glyph, text), hidden while empty,
        // so a binding can set either (apply()).
        bool bound = e.prop(fpkg::key::BIND, &v0, port);
        o = capsule(parent, glyph ? glyph : bound ? "" : nullptr, &fx_icon_fill_16, label, bg, 40, nullptr,
                    nullptr);  // DESIGN.md: 40 px targets
        if (bound && !glyph) lv_obj_add_flag(lv_obj_get_child(o, 0), LV_OBJ_FLAG_HIDDEN);
      }
      break;
    }
    case fpkg::kind::SWITCH:
      o = toggle(parent, get(fpkg::key::VALUE, 0) != 0);
      break;
    case fpkg::kind::SLIDER:
      o = slider(parent, get(fpkg::key::MIN, 0), get(fpkg::key::MAX, 100), get(fpkg::key::VALUE, 0));
      lv_obj_set_flex_grow(o, 0);
      lv_obj_set_width(o, LV_PCT(100));
      break;
    case fpkg::kind::BAR: {
      o = lv_bar_create(parent);
      lv_obj_remove_style_all(o);
      lv_bar_set_range(o, get(fpkg::key::MIN, 0), get(fpkg::key::MAX, 100));
      lv_bar_set_value(o, get(fpkg::key::VALUE, 0), LV_ANIM_OFF);
      lv_obj_set_size(o, LV_PCT(100), 6);
      lv_obj_set_style_bg_color(o, col(FX_COLOR_SURFACE_3), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(o, col(get(fpkg::key::COLOR, FX_COLOR_ACCENT)), LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_INDICATOR);
      lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
      lv_obj_set_style_anim_duration(o, FX_MOTION_FAST_MS, 0);
      break;
    }
    case fpkg::kind::IMAGE: {
      o = lv_image_create(parent);
      int32_t src = get(fpkg::key::SRC, -1);
      const lv_image_dsc_t* d = src >= 0 ? &s_app.images[src] : nullptr;
      if (s_app.art >= 0 && e.prop(fpkg::key::BIND, &v0)) {  // the bound image (P50-M)
        s_app.art_obj = o;
        if (s_app.art_ready) d = &s_app.images[s_app.art];
        lv_obj_set_style_bg_color(o, col(FX_COLOR_SURFACE_2), 0);  // until the first picture
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
      }
      if (d) {
        lv_image_set_src(o, d);
        if (d->header.cf == LV_COLOR_FORMAT_A8) {
          lv_obj_set_style_image_recolor(o, col(get(fpkg::key::COLOR, FX_COLOR_LABEL)), 0);
          lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
        }
      }
      break;
    }
    case fpkg::kind::ICON:
      o = text(parent, icon >= 0 ? ICONS[icon] : "", icon_font(get(fpkg::key::SIZE, 20)),
               get(fpkg::key::COLOR, FX_COLOR_LABEL));
      break;
  }
  if (!o) return nullptr;
  int32_t v;
  if (e.prop(fpkg::key::W, &v, port)) lv_obj_set_width(o, lv_size(v));
  if (e.prop(fpkg::key::H, &v, port)) lv_obj_set_height(o, lv_size(v));
  if (e.prop(fpkg::key::GROW, &v, port) && grow_applies(e, port)) lv_obj_set_flex_grow(o, v);
  if (e.prop(fpkg::key::BIND, &v, port)) s_app.bound.push_back({(uint16_t)v, e.kind, o, false, idx});
  if (e.prop(fpkg::key::SHOW, &v, port)) s_app.bound.push_back({(uint16_t)v, e.kind, o, true, idx});
  if (e.prop(fpkg::key::EVENT, &v, port)) {
    void* ud = (void*)(uintptr_t)idx;
    if (e.kind == fpkg::kind::SWITCH)
      lv_obj_add_event_cb(o, elem_cb, LV_EVENT_VALUE_CHANGED, ud);
    else if (e.kind == fpkg::kind::SLIDER)
      lv_obj_add_event_cb(o, elem_cb, LV_EVENT_RELEASED, ud);
    else {
      lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(o, elem_cb, LV_EVENT_CLICKED, ud);
      lv_obj_add_event_cb(o, elem_cb, LV_EVENT_LONG_PRESSED, ud);
    }
  }
  return o;
}

// The child a view shows: its binding once set, else `value`, else 0.
static int32_t view_pick(size_t view, bool port) {
  const fpkg::Elem& p = s_app.pkg.elems[view];
  int32_t b, pick = 0;
  if (p.prop(fpkg::key::BIND, &b, port) && s_app.vals[b].set) pick = s_app.vals[b].i;
  else p.prop(fpkg::key::VALUE, &pick, port);
  return pick;
}

// A view's child that is not picked is not built (only the picked one is in RAM, P50-M step 5).
static bool picked(size_t i, bool port) {
  const fpkg::Elem& e = s_app.pkg.elems[i];
  if (e.parent == fpkg::NONE || s_app.pkg.elems[e.parent].kind != fpkg::kind::VIEW) return true;
  return view_pick(e.parent, port) == s_app.pos[i];
}

// Whether element `i` shows (P50-M): its `show` binding once set, else not `hidden`.
static bool shown(size_t i, bool port) {
  const fpkg::Elem& e = s_app.pkg.elems[i];
  int32_t b, v = 0;
  if (e.prop(fpkg::key::SHOW, &b, port) && s_app.vals[b].set) {
    const Val& s = s_app.vals[b];
    return s.kind == 2 ? !s.text.empty() : s.i != 0;
  }
  return !(e.prop(fpkg::key::HIDDEN, &v, port) && v);
}

static void fade_exec(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }

static void fade_in(lv_obj_t* o) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, fade_exec);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, FX_MOTION_FAST_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  fade_exec(o, LV_OPA_TRANSP);
  lv_anim_start(&a);
}

// Hide and show elements to match the values; with `animate`, what appears fades in (DESIGN.md:
// apps have no motion props, the board animates). Fade only: a scale would draw through a
// full-size layer (P51, home.cpp).
static void refresh_shown(bool animate) {
  bool port = portrait();
  for (size_t i = 0; i < s_app.objs.size(); i++) {
    lv_obj_t* o = s_app.objs[i];
    if (!o) continue;
    bool on = shown(i, port) && picked(i, port), was = !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (on == was) continue;
    if (!on) {
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (animate) fade_in(o);
  }
}

// Build every element not built yet whose parent is built, except a view's children it does not
// pick; then apply the cached values to what was built. With nothing built, the whole layout.
static void build_missing(lv_obj_t* body, bool port) {
  const fpkg::Package& p = s_app.pkg;
  size_t first = s_app.bound.size();
  for (size_t i = 0; i < p.elems.size(); i++) {
    const fpkg::Elem& e = p.elems[i];
    if (s_app.objs[i] || !picked(i, port)) continue;
    lv_obj_t* parent = e.parent == fpkg::NONE ? body : s_app.objs[e.parent];
    if (parent) s_app.objs[i] = make(e, i, parent, port);
  }
  for (size_t k = first; k < s_app.bound.size(); k++) {
    const Bound& b = s_app.bound[k];
    if (!b.show && s_app.vals[b.bind].set) apply(b, s_app.vals[b.bind]);
  }
}

static bool under(size_t i, size_t root) {
  for (uint16_t p = s_app.pkg.elems[i].parent; p != fpkg::NONE; p = s_app.pkg.elems[p].parent)
    if (p == root) return true;
  return false;
}

// A view's binding changed: when it picks another child, delete the built one (its LVGL objects
// and bindings) and build the new one, which fades in. Its values come from the cache.
static void switch_view(size_t view, bool port) {
  int32_t pick = view_pick(view, port);
  size_t n = s_app.pkg.elems.size(), now = n;
  for (size_t i = view + 1; i < n; i++)
    if (s_app.objs[i] && s_app.pkg.elems[i].parent == view) now = i;
  if (now < n && s_app.pos[now] == pick) return;
  if (now < n) {
    lv_obj_delete(s_app.objs[now]);  // and its children
    for (size_t i = now; i < n; i++) {
      if (i != now && !under(i, now)) continue;
      if (s_app.objs[i] == s_app.art_obj) s_app.art_obj = nullptr;
      s_app.objs[i] = nullptr;
    }
    auto gone = [&](const Bound& b) { return b.elem == now || under(b.elem, now); };
    s_app.bound.erase(std::remove_if(s_app.bound.begin(), s_app.bound.end(), gone), s_app.bound.end());
  }
  build_missing(nullptr, port);  // the root exists, so `body` is never needed here
  refresh_shown(false);
  for (size_t i = view + 1; i < n; i++)
    if (s_app.objs[i] && s_app.pkg.elems[i].parent == view && !lv_obj_has_flag(s_app.objs[i], LV_OBJ_FLAG_HIDDEN))
      fade_in(s_app.objs[i]);
}

static void build_layout(lv_obj_t* body) {
  const fpkg::Package& p = s_app.pkg;
  bool port = portrait();
  s_app.objs.assign(p.elems.size(), nullptr);
  s_app.pos.assign(p.elems.size(), 0);
  std::vector<uint16_t> kids(p.elems.size(), 0);  // children seen so far, per element
  for (size_t i = 0; i < p.elems.size(); i++)
    if (p.elems[i].parent != fpkg::NONE) s_app.pos[i] = kids[p.elems[i].parent]++;
  build_missing(body, port);
  refresh_shown(false);
}

enum Why { WAITING, OFF, NOT_HERE, STARTING, MISSING };

// Taps pass through to the scroller: nothing in the layout takes them.
static void untouchable(lv_obj_t* o) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
    lv_obj_t* c = lv_obj_get_child(o, i);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    untouchable(c);
  }
}

static void turn_on_cb(lv_event_t*) {
  if (s_app.turning_on || !sys::link_connected()) return;
  s_app.turning_on = true;
  send_id(fnx::ty::APP_TURN_ON, s_app.id);  // the PC switches it on, then sends HOST_STATE
  refresh_if_top(app_screen);
}

// Artboards 6a / 6b: centred icon (or spinner), title, one line of help. P51 adds Turn on for an
// app switched off on the PC, "Not on this PC", "Starting" and "Insert the SD card".
static void host_state(lv_obj_t* body, Why why) {
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(body, 24, 0);
  lv_obj_set_style_pad_bottom(body, 16, 0);
  lv_obj_set_style_pad_row(body, 6, 0);
  bool spin = why == WAITING || why == STARTING || (why == OFF && s_app.turning_on);
  if (spin)
    spinner(body, 22, why == WAITING ? FX_COLOR_WARNING : FX_COLOR_ACCENT);
  else
    text(body, why == MISSING ? FX_ICON_SIM_CARD : FX_ICON_DESKTOP_TOWER, &fx_icon_fill_40, FX_COLOR_LABEL_2);
  const char* title = why == WAITING     ? "Waiting for host PC"
                      : why == STARTING  ? "Starting on host PC"
                      : why == NOT_HERE  ? "Not on this PC"
                      : why == MISSING   ? "Insert the SD card"
                      : s_app.turning_on ? "Turning on"
                                         : "Turned off on host PC";
  lv_obj_t* t = text(body, title, &fx_font_title, FX_COLOR_LABEL);
  lv_obj_set_style_margin_top(t, spin ? 8 : 4, 0);
  char help[140];
  const char* n = s_app.name;
  switch (why) {
    case WAITING:
      snprintf(help, sizeof help, "%s runs with your PC. Open fenixOS on the PC and connect the board.", n);
      break;
    case STARTING:
      snprintf(help, sizeof help, "%s is starting on your PC. It shows here when it is ready.", n);
      break;
    case NOT_HERE:
      snprintf(help, sizeof help, "%s was added on another PC. Add it in fenixOS on this PC to use it here.", n);
      break;
    case MISSING:
      snprintf(help, sizeof help, "%s is stored on the SD card. Put the card back in to open it.", n);
      break;
    default:
      snprintf(help, sizeof help, "Turn %s on here or in fenixOS on your PC.", n);
      break;
  }
  lv_obj_t* h = text(body, help, &fx_font_label, FX_COLOR_LABEL_2);
  lv_obj_set_width(h, LV_PCT(100));
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(h, 18 - lv_font_get_line_height(&fx_font_label), 0);
  if (why == OFF && !s_app.turning_on) {
    lv_obj_t* b = capsule(body, FX_ICON_POWER, &fx_icon_fill_16, "Turn on", FX_COLOR_ACCENT, 40, turn_on_cb, nullptr);
    lv_obj_set_style_margin_top(b, 10, 0);
  }
}

// The app screen is going. Left for another screen (the pop deletes it before building the next,
// P50-M): close the app now and free what it read, so Home is built with that RAM back. A rebuild
// (rotation, host state) keeps the app open.
static void screen_deleted(lv_event_t*) {
  if (s_app.open && nav_top() != app_screen) close_app();
  free_retired();
}

void app_screen(lv_obj_t* scr) {
  s_app.bound.clear();
  s_app.art_obj = nullptr;
  s_app.objs.clear();  // rebuilt by build_layout; a host-state screen has none
  lv_obj_add_event_cb(scr, screen_deleted, LV_EVENT_DELETE, nullptr);
  navbar(scr, "Home", s_app.open ? s_app.name : "");
  lv_obj_t* body = column(scr, 0);
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_flex_grow(body, 1);
  if (!s_app.open) return;
  if (s_app.missing) return host_state(body, MISSING);
  bool offline = !sys::link_connected();
  if (offline && !has_vals()) return host_state(body, WAITING);
  switch (offline ? fnx::host::RUNNING : app_host(s_app.id)) {
    case fnx::host::OFF:
      return host_state(body, OFF);
    case fnx::host::NOT_HERE:
      return host_state(body, NOT_HERE);
    case fnx::host::STARTING:
      return host_state(body, STARTING);
    default:
      break;
  }
  if (offline) {  // the last values, dimmed and not touchable, under one line (P53)
    lv_obj_t* r = rowbox(body, 6);
    lv_obj_set_style_pad_hor(r, 16, 0);
    lv_obj_set_style_pad_ver(r, 6, 0);
    text(r, FX_ICON_PLUGS, &fx_icon_fill_16, FX_COLOR_WARNING);
    text(r, "PC not connected", &fx_font_label, FX_COLOR_LABEL_2);
    body = column(body, 0);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
  }
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);  // drags must reach the scroller
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_ACTIVE);
  build_layout(body);
  if (offline) {
    untouchable(body);
    lv_obj_t* veil = box(body);  // above the layout; stays put while it scrolls
    lv_obj_add_flag(veil, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(veil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(veil, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(veil, col(FX_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(veil, LV_OPA_50, 0);
  }
}

// ---- Link events and the UI loop ----

void app_on_link(const sys::LinkEvent& e) {
  switch (e.type) {
    case sys::LinkEv::CONNECTED:
      if (s_app.open && !s_app.missing) send_id(fnx::ty::APP_OPENED, s_app.id);
      refresh_if_top(app_screen);
      break;
    case sys::LinkEv::DISCONNECTED: {
      bool faded = false;
      for (const Host& h : s_host) faded |= h.state == fnx::host::OFF || h.state == fnx::host::NOT_HERE;
      s_host.clear();
      s_app.turning_on = false;
      refresh_if_top(app_screen);
      if (faded && nav_top() == home_screen) nav_rebuild();  // no PC: tiles show plain again
      break;
    }
    case sys::LinkEv::HOST_STATE: {
      uint8_t was = app_host(e.text), now = (uint8_t)e.value;
      bool is_open = strcmp(open_app_id(), e.text) == 0;
      if (is_open && now != fnx::host::OFF && s_app.turning_on) {
        s_app.turning_on = false;
        refresh_if_top(app_screen);
      }
      if (was == now) break;
      bool found = false;
      for (Host& h : s_host)
        if (h.id == e.text) {
          h.state = now;
          found = true;
        }
      if (!found) s_host.push_back({e.text, now});
      if (is_open) refresh_if_top(app_screen);
      // Tiles fade while the PC half is off or missing (P51).
      auto faded = [](uint8_t s) { return s == fnx::host::OFF || s == fnx::host::NOT_HERE; };
      if (faded(was) != faded(now) && nav_top() == home_screen) nav_rebuild();
      break;
    }
    case sys::LinkEv::APPS_CHANGED: {
      const char* id = e.text;
      bool home_top = nav_top() == home_screen;
      if (!id[0]) {  // the SD card came or went: many tiles changed (P51)
        apps_reload();
        if (s_app.open) {
          sys::AppMeta m;
          bool there = sys::store_find(s_app.id, &m);
          bool ready = there && m.state == fnx::app_state::READY;
          if (s_app.missing && ready) {  // card back: open the app for real
            char keep[fpkg::MAX_ID + 1];
            strlcpy(keep, s_app.id, sizeof keep);
            close_app();
            if (load_app(keep) && sys::link_connected()) send_id(fnx::ty::APP_OPENED, s_app.id);
            refresh_if_top(app_screen);
          } else if (!s_app.missing && !ready && nav_top() == app_screen) {
            nav_pop();  // its card was pulled while it showed
          }
        }
        if (home_top) nav_rebuild();
        settings_on_sd();
        break;
      }
      bool had = known(id);
      apps_reload();
      bool now = known(id);
      bool is_open = strcmp(open_app_id(), id) == 0;
      if (now) {
        if (!had) strlcpy(s_fade_in, id, sizeof s_fade_in);
        if (is_open) {  // updated while open: show the new version
          close_app();
          if (load_app(id)) {
            if (sys::link_connected()) send_id(fnx::ty::APP_OPENED, s_app.id);
          }
          refresh_if_top(app_screen);
        }
      } else if (had) {
        sys::app_state_forget(id);
        if (is_open) s_app.dirty = false;  // closing must not write it again
        bool off_shown = is_open && nav_top() == app_screen && app_host(id) == fnx::host::OFF;
        if (is_open || home_top) strlcpy(s_ghost, id, sizeof s_ghost);
        if (is_open && off_shown)
          s_app.close_at = esp_timer_get_time() + 2000000;  // 6b stays up for 2 s
        else if (is_open)
          nav_pop();
      }
      if (home_top) nav_rebuild();
      settings_on_sd();  // Storage lists the apps and where they live
      break;
    }
    default:
      break;
  }
}

void app_tick() {
  sys::BindValue v;
  if (!s_app.open) {
    while (sys::bind_poll(&v)) {
    }  // no app open: values are not kept (docs/packages.md)
    return;
  }
  if (nav_top() != app_screen) {  // left the app (back, Home, rotation of another screen)
    close_app();
    return;
  }
  if (s_app.close_at && esp_timer_get_time() >= s_app.close_at) {
    s_app.close_at = 0;
    nav_pop();
    return;
  }
  if (s_app.dirty && esp_timer_get_time() - s_app.saved_at > STATE_EVERY_US) state_save();
  if (sys::bind_image_done() && s_app.art >= 0) {  // a whole picture is in the buffer (P50-M)
    s_app.art_ready = true;
    const lv_image_dsc_t* d = &s_app.images[s_app.art];
    if (lv_obj_t* o = s_app.art_obj) {
      // No image cache to drop: its size is 0 (LV_CACHE_DEF_SIZE) and LVGL draws raw RGB565 from
      // the buffer itself, so an invalidate shows the new pixels.
      if (lv_image_get_src(o) != d) lv_image_set_src(o, d);
      lv_obj_set_style_image_recolor_opa(o, LV_OPA_TRANSP, 0);
      lv_obj_invalidate(o);
    }
  }
  while (sys::bind_poll(&v)) {
    if (v.app != sys::app_hash(s_app.id) || v.bind >= s_app.vals.size()) continue;
    Val& val = s_app.vals[v.bind];
    val.set = true;
    val.kind = v.kind;
    val.i = v.i;
    if (v.kind == 2) val.text = v.text;
    s_app.dirty = true;
    bool shows = false;
    std::vector<uint16_t> views;  // views bound to it (switching changes `bound`, so not in the loop)
    for (const Bound& b : s_app.bound) {
      if (b.bind != v.bind) continue;
      if (b.kind == fpkg::kind::VIEW && !b.show) views.push_back(b.elem);
      else if (b.show) shows = true;
      else apply(b, val);
    }
    for (uint16_t view : views) switch_view(view, portrait());
    if (shows) refresh_shown(true);
  }
}

}  // namespace fx::apps
