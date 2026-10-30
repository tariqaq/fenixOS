// Host apps (docs/packages.md): Home tiles for installed packages, and the app screen built from
// a package. One app is open at a time. Packages are read from flash in pieces (P39): tiles read
// the two icons only, and an open app keeps its strings, props and the images its elements show,
// from which its LVGL objects are rebuilt (rotation, host state). Artboards 6a, 6b.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "esp_timer.h"
#include "fenix_pkg.h"
#include "fenix_proto.h"
#include "shell.h"

namespace fx::apps {

using namespace fx::ui;

// ---- Installed apps (tiles) ----

static std::vector<AppTile> s_tiles;
static AppTile s_ghost;  // removed tile still fading out on Home; id empty = none
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

// Point the icon descriptors at this tile's own pixel copy (after a copy or move).
static void fix_icons(AppTile& t) {
  t.small.data = t.small.data_size ? t.px.data() : nullptr;
  t.large.data = t.large.data_size ? t.px.data() + t.small.data_size : nullptr;
}

// Parse an installed package from flash; `f` stays open for reading assets (caller closes).
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

static bool has_tile(const char* id) {
  for (const AppTile& t : s_tiles)
    if (strcmp(t.id, id) == 0) return true;
  return false;
}

void apps_reload() {
  s_tiles.clear();
  sys::AppMeta list[sys::MAX_APPS];
  int n = sys::store_list(list, sys::MAX_APPS);
  s_tiles.reserve(n);
  for (int i = 0; i < n; i++) {
    fpkg::Package p;
    FILE* f = nullptr;
    if (read_pkg(list[i].id, p, &f)) {
      sys::link_log(2, "app %s: unreadable package", list[i].id);
      if (f) fclose(f);
      continue;
    }
    AppTile t{};
    snprintf(t.id, sizeof t.id, "%.*s", (int)p.id.size(), p.id.data());
    snprintf(t.name, sizeof t.name, "%.*s", (int)p.name.size(), p.name.data());
    t.color = p.head.tile_color;
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
    s_tiles.push_back(std::move(t));
  }
  for (AppTile& t : s_tiles) fix_icons(t);
}

const std::vector<AppTile>& app_tiles() { return s_tiles; }
const AppTile* app_ghost() { return s_ghost.id[0] ? &s_ghost : nullptr; }
void app_ghost_done() {
  s_ghost = AppTile{};
}
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
};

static struct {
  bool open;
  fpkg::Package pkg;
  char id[fpkg::MAX_ID + 1];
  char name[32];
  std::vector<Val> vals;  // last value per binding, so rebuilds keep what the PC set
  std::vector<Bound> bound;
  std::vector<lv_image_dsc_t> images;  // per asset; data null unless an element shows it
  std::vector<uint8_t*> pixels;        // what the descriptors point at (malloc)
  int64_t close_at;  // pending return to Home (app removed while showing HostOff)
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

static std::vector<std::string> s_host_off;  // apps whose PC half is switched off (HOST_STATE)

static bool host_off(const char* id) {
  for (const std::string& s : s_host_off)
    if (s == id) return true;
  return false;
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

static void close_app() {
  if (!s_app.open) return;
  if (sys::link_connected()) send_id(fnx::ty::APP_CLOSED, s_app.id);
  s_app.open = false;
  // At most one app screen outlives the app (nav keeps two screens, only during a slide), and
  // opening another app loads a new screen first, which deletes it: the older leftovers are free.
  free_retired();
  s_retired.pkg = std::move(s_app.pkg);  // moves keep the buffers, so the screen's pointers stay valid
  s_retired.images = std::move(s_app.images);
  s_retired.pixels = std::move(s_app.pixels);
  s_app.pkg = fpkg::Package();
  s_app.images.clear();
  s_app.pixels.clear();
  s_app.vals.clear();
  s_app.bound.clear();
  s_app.close_at = 0;
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
  return true;
}

void app_open(const char* id) {
  close_app();
  if (!load_app(id)) return;
  nav_push(app_screen, true);
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
};

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
  int32_t icon = get(fpkg::key::ICON, -1);
  lv_obj_t* o = nullptr;
  switch (e.kind) {
    case fpkg::kind::BOX: {
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
        o = capsule(parent, glyph, &fx_icon_fill_16, label, bg, 40, nullptr, nullptr);  // DESIGN.md: 40 px targets
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
      if (src >= 0) {
        lv_image_set_src(o, &s_app.images[src]);
        if (s_app.pkg.assets[src].format == fpkg::asset_format::A8) {
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
  if (get(fpkg::key::HIDDEN, 0)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (e.prop(fpkg::key::BIND, &v, port)) s_app.bound.push_back({(uint16_t)v, e.kind, o});
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

static void build_layout(lv_obj_t* body) {
  const fpkg::Package& p = s_app.pkg;
  bool port = portrait();
  std::vector<lv_obj_t*> objs(p.elems.size(), nullptr);
  for (size_t i = 0; i < p.elems.size(); i++) {
    const fpkg::Elem& e = p.elems[i];
    lv_obj_t* parent = e.parent == fpkg::NONE ? body : objs[e.parent];
    if (!parent) continue;
    objs[i] = make(e, i, parent, port);
  }
  for (const Bound& b : s_app.bound) {
    if (s_app.vals[b.bind].set) apply(b, s_app.vals[b.bind]);
  }
}

// Artboards 6a / 6b: centred icon, title, one line of help.
static void host_state(lv_obj_t* body, bool off) {
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(body, 24, 0);
  lv_obj_set_style_pad_bottom(body, 16, 0);
  lv_obj_set_style_pad_row(body, 6, 0);
  if (off) {
    text(body, FX_ICON_DESKTOP_TOWER, &fx_icon_fill_40, FX_COLOR_LABEL_2);
  } else {
    spinner(body, 22, FX_COLOR_WARNING);
  }
  lv_obj_t* t = text(body, off ? "Turned off on host PC" : "Waiting for host PC", &fx_font_title, FX_COLOR_LABEL);
  lv_obj_set_style_margin_top(t, off ? 4 : 8, 0);
  char help[120];
  if (off)
    snprintf(help, sizeof help, "Switch %s on in fenixOS on your PC to use it here.", s_app.name);
  else
    snprintf(help, sizeof help, "%s runs with your PC. Open fenixOS on the PC and connect the board.", s_app.name);
  lv_obj_t* h = text(body, help, &fx_font_label, FX_COLOR_LABEL_2);
  lv_obj_set_width(h, LV_PCT(100));
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(h, 18 - lv_font_get_line_height(&fx_font_label), 0);
}

void app_screen(lv_obj_t* scr) {
  s_app.bound.clear();
  lv_obj_add_event_cb(scr, free_retired, LV_EVENT_DELETE, nullptr);
  navbar(scr, "Home", s_app.open ? s_app.name : "");
  lv_obj_t* body = column(scr, 0);
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_flex_grow(body, 1);
  if (!s_app.open) return;
  if (!sys::link_connected()) return host_state(body, false);
  if (host_off(s_app.id)) return host_state(body, true);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);  // drags must reach the scroller
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_ACTIVE);
  build_layout(body);
}

// ---- Link events and the UI loop ----

void app_on_link(const sys::LinkEvent& e) {
  switch (e.type) {
    case sys::LinkEv::CONNECTED:
      if (s_app.open) send_id(fnx::ty::APP_OPENED, s_app.id);
      refresh_if_top(app_screen);
      break;
    case sys::LinkEv::DISCONNECTED:
      s_host_off.clear();
      refresh_if_top(app_screen);
      break;
    case sys::LinkEv::HOST_STATE: {
      bool was = host_off(e.text);
      if (e.value) {
        for (size_t i = 0; i < s_host_off.size(); i++)
          if (s_host_off[i] == e.text) s_host_off.erase(s_host_off.begin() + i--);
      } else if (!was) {
        s_host_off.emplace_back(e.text);
      }
      if (was != host_off(e.text) && strcmp(open_app_id(), e.text) == 0) refresh_if_top(app_screen);
      break;
    }
    case sys::LinkEv::APPS_CHANGED: {
      const char* id = e.text;
      bool home_top = nav_top() == home_screen;
      bool had = has_tile(id);
      AppTile old{};
      for (const AppTile& t : s_tiles)
        if (strcmp(t.id, id) == 0) old = t;
      apps_reload();
      bool now = has_tile(id);
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
        bool off_shown = is_open && nav_top() == app_screen && host_off(id);
        if (is_open || home_top) {
          s_ghost = old;
          fix_icons(s_ghost);
        }
        if (is_open && off_shown)
          s_app.close_at = esp_timer_get_time() + 2000000;  // 6b stays up for 2 s
        else if (is_open)
          nav_pop();
      }
      if (home_top) nav_rebuild();
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
  while (sys::bind_poll(&v)) {
    if (strcmp(v.id, s_app.id) != 0 || v.bind >= s_app.vals.size()) continue;
    Val& val = s_app.vals[v.bind];
    val.set = true;
    val.kind = v.kind;
    val.i = v.i;
    if (v.kind == 2) val.text = v.text;
    for (const Bound& b : s_app.bound)
      if (b.bind == v.bind) apply(b, val);
  }
}

}  // namespace fx::apps
