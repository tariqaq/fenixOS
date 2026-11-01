// App store (P51; docs/packages.md "Where apps live"). Packages go to the SD card
// (/sd/fenixos/apps) when one is in, else to flash (LittleFS /apps, at most FLASH_PKG_MAX each).
// Both hold the same files: <id>.fxp, <id>.sha (sha256, written last: the commit marker) and
// <id>.tmp while an install runs. One app lives in one place.
// An index in RAM (AppMeta, 120 B per app) lists every app. /apps/sd.idx keeps the card's apps
// on flash, so their tiles still show (faded, "Insert the SD card") while the card is out.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string_view>
#include <vector>

#include "esp_app_desc.h"
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"
#include "fenix_pkg.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"

namespace fx::sys {

static constexpr const char* FLASH_DIR = "/apps";
static constexpr const char* SD_ROOT = "/sd/fenixos";
static constexpr const char* SD_DIR = "/sd/fenixos/apps";
static constexpr const char* SD_IDX = "/apps/sd.idx";
static constexpr uint32_t SD_SPARE = 64 * 1024;  // left free on the card after an install

static bool s_mounted;  // LittleFS
static bool s_sd;       // the card is in and its apps are in the index (READY)
static SemaphoreHandle_t s_lock;
static std::vector<AppMeta> s_index;  // sorted by id; under s_lock

namespace {
struct Lock {
  Lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_lock); }
};
}  // namespace

static const char* dir_of(uint8_t where) { return where == fnx::app_where::SD ? SD_DIR : FLASH_DIR; }

static void path(char* out, size_t cap, uint8_t where, const char* id, const char* ext) {
  snprintf(out, cap, "%s/%s.%s", dir_of(where), id, ext);
}

static bool exists(const char* p) {
  struct stat st;
  return stat(p, &st) == 0;
}

static bool ends_with(const char* s, const char* suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void sort(std::vector<AppMeta>& v) {
  std::sort(v.begin(), v.end(), [](const AppMeta& a, const AppMeta& b) { return strcmp(a.id, b.id) < 0; });
}

static AppMeta* find_in(std::vector<AppMeta>& v, const char* id) {
  for (AppMeta& m : v)
    if (strcmp(m.id, id) == 0) return &m;
  return nullptr;
}

// Delete what a power cut can leave behind: temp files, and a .fxp or .sha without its partner.
static void clean(const char* dir) {
  DIR* d = opendir(dir);
  if (!d) return;
  char p[300], other[300];  // dir (16) + "/" + d_name (up to 255)
  while (dirent* e = readdir(d)) {
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    if (ends_with(e->d_name, ".tmp")) {
      unlink(p);
      continue;
    }
    bool fxp = ends_with(e->d_name, ".fxp"), sha = ends_with(e->d_name, ".sha");
    if (!fxp && !sha) continue;
    snprintf(other, sizeof other, "%.*s%s", (int)strlen(p) - 4, p, fxp ? ".sha" : ".fxp");
    if (!exists(other)) unlink(p);
  }
  closedir(d);
}

static FILE* open_sized(const char* p, size_t* n) {
  FILE* f = fopen(p, "rb");
  if (!f) return nullptr;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  *n = size > 0 ? (size_t)size : 0;
  return f;
}

static void set_meta(AppMeta& m, std::string_view id, std::string_view ver, std::string_view name, uint32_t color) {
  snprintf(m.id, sizeof m.id, "%.*s", (int)id.size(), id.data());
  snprintf(m.version, sizeof m.version, "%.*s", (int)ver.size(), ver.data());
  snprintf(m.name, sizeof m.name, "%.*s", (int)name.size(), name.data());
  m.color = color;
}

// Id, version, name and colour of a package file: from its first 512 bytes when the strings fit,
// else from a full parse (strings and props only, P39).
static bool read_meta(const char* p, AppMeta& m) {
  size_t n = 0;
  FILE* f = open_sized(p, &n);
  if (!f) return false;
  uint8_t head[512];
  size_t got = fread(head, 1, sizeof head, f);
  std::string_view id, ver, name;
  uint32_t color = 0;
  bool ok = fpkg::peek(head, got, &id, &ver, &name, &color);
  if (ok) {
    set_meta(m, id, ver, name, color);
  } else {
    fseek(f, 0, SEEK_SET);
    fpkg::Package pkg;
    fpkg::FileSource src(f, n);
    ok = !pkg.parse(src, n);
    if (ok) set_meta(m, pkg.id, pkg.version, pkg.name, pkg.head.tile_color);
  }
  fclose(f);
  return ok;
}

// Every committed package in one place, after cleaning up leftovers.
static void scan(uint8_t where, std::vector<AppMeta>& out) {
  const char* dir = dir_of(where);
  clean(dir);
  DIR* d = opendir(dir);
  if (!d) return;
  char p[300];
  while (dirent* e = readdir(d)) {
    if (!ends_with(e->d_name, ".fxp") || out.size() >= 2 * MAX_APPS) continue;
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    AppMeta m{};
    if (!read_meta(p, m)) {
      link_log(2, "store: %s is not a readable package", p);
      continue;
    }
    size_t idn = strlen(m.id);
    if (strlen(e->d_name) != idn + 4 || strncmp(e->d_name, m.id, idn) != 0) continue;  // renamed by hand
    m.where = where;
    m.state = fnx::app_state::READY;
    out.push_back(m);
  }
  closedir(d);
}

// /apps/sd.idx: one line per app on the card, "id <tab> version <tab> colour <tab> name".
static void save_sd_idx() {
  std::vector<AppMeta> sd;
  {
    Lock l;
    for (const AppMeta& m : s_index)
      if (m.where == fnx::app_where::SD) sd.push_back(m);
  }
  if (sd.empty()) {
    unlink(SD_IDX);
    return;
  }
  char tmp[24];
  snprintf(tmp, sizeof tmp, "%s.new", SD_IDX);
  FILE* f = fopen(tmp, "w");
  if (!f) return;
  for (const AppMeta& m : sd) {
    char name[sizeof m.name];
    strlcpy(name, m.name, sizeof name);
    for (char* c = name; *c; c++)
      if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' ';
    fprintf(f, "%s\t%s\t%08lx\t%s\n", m.id, m.version, (unsigned long)m.color, name);
  }
  bool ok = fclose(f) == 0;
  if (ok) {
    unlink(SD_IDX);  // LittleFS rename replaces, but say it plainly
    ok = rename(tmp, SD_IDX) == 0;
  }
  if (!ok) unlink(tmp);
}

static void load_sd_idx(std::vector<AppMeta>& out) {
  FILE* f = fopen(SD_IDX, "r");
  if (!f) return;
  char line[160];
  while (fgets(line, sizeof line, f) && out.size() < MAX_APPS) {
    line[strcspn(line, "\r\n")] = 0;
    char* save = nullptr;
    char* id = strtok_r(line, "\t", &save);
    char* ver = strtok_r(nullptr, "\t", &save);
    char* color = strtok_r(nullptr, "\t", &save);
    char* name = strtok_r(nullptr, "", &save);
    if (!id || !ver || !color || !fpkg::valid_id(id)) continue;
    AppMeta m{};
    strlcpy(m.id, id, sizeof m.id);
    strlcpy(m.version, ver, sizeof m.version);
    strlcpy(m.name, name ? name : id, sizeof m.name);
    m.color = strtoul(color, nullptr, 16);
    m.where = fnx::app_where::SD;
    m.state = fnx::app_state::CARD_MISSING;
    out.push_back(m);
  }
  fclose(f);
}

bool store_mount() {
  s_lock = xSemaphoreCreateMutex();
  esp_vfs_littlefs_conf_t c = {};
  c.base_path = FLASH_DIR;
  c.partition_label = "apps";
  c.format_if_mount_failed = true;
  s_mounted = esp_vfs_littlefs_register(&c) == ESP_OK;
  if (!s_mounted) return false;
  std::vector<AppMeta> v, sd;
  scan(fnx::app_where::FLASH, v);
  load_sd_idx(sd);  // missing until sd.cpp mounts the card and store_sd_changed scans it
  for (const AppMeta& m : sd)
    if (!find_in(v, m.id) && v.size() < MAX_APPS) v.push_back(m);
  sort(v);
  Lock l;
  s_index = std::move(v);
  return true;
}

void store_sd_changed(bool ready) {
  if (!s_mounted) return;
  std::vector<AppMeta> sd;
  if (ready) {
    mkdir(SD_ROOT, 0775);
    mkdir(SD_DIR, 0775);
    scan(fnx::app_where::SD, sd);
  }
  std::vector<AppMeta> stale;  // card copies of apps reinstalled on flash while the card was out
  {
    Lock l;
    if (ready) {
      s_index.erase(std::remove_if(s_index.begin(), s_index.end(),
                                   [](const AppMeta& m) { return m.where == fnx::app_where::SD; }),
                    s_index.end());
      for (const AppMeta& m : sd) {
        if (find_in(s_index, m.id))
          stale.push_back(m);  // the flash copy is the newer one: installs go to the card when it is in
        else if (s_index.size() < MAX_APPS)
          s_index.push_back(m);
      }
      sort(s_index);
    } else {
      for (AppMeta& m : s_index)
        if (m.where == fnx::app_where::SD) m.state = fnx::app_state::CARD_MISSING;
    }
    s_sd = ready;
  }
  char p[96];
  for (const AppMeta& m : stale) {
    path(p, sizeof p, fnx::app_where::SD, m.id, "sha");
    unlink(p);
    path(p, sizeof p, fnx::app_where::SD, m.id, "fxp");
    unlink(p);
  }
  if (ready) save_sd_idx();
  link_post(LinkEv::APPS_CHANGED, 0, "");
}

void store_usage(uint32_t* used_kb, uint32_t* total_kb) {
  size_t total = 0, used = 0;
  if (s_mounted) esp_littlefs_info("apps", &total, &used);
  *used_kb = used / 1024;
  *total_kb = total / 1024;
}

int store_count() {
  if (!s_lock) return 0;
  Lock l;
  return (int)s_index.size();
}

bool store_at(int i, AppMeta* out) {
  if (!s_lock) return false;
  Lock l;
  if (i < 0 || i >= (int)s_index.size()) return false;
  *out = s_index[i];
  return true;
}

bool store_find(const char* id, AppMeta* out) {
  if (!s_lock) return false;
  Lock l;
  AppMeta* m = find_in(s_index, id);
  if (m && out) *out = *m;
  return m != nullptr;
}

bool store_sha(const char* id, uint8_t out[32]) {
  AppMeta m;
  if (!store_find(id, &m) || m.state != fnx::app_state::READY) return false;
  char p[96];
  path(p, sizeof p, m.where, m.id, "sha");
  FILE* f = fopen(p, "rb");
  if (!f) return false;
  size_t got = fread(out, 1, 32, f);
  fclose(f);
  return got == 32;
}

FILE* store_open(const char* id, size_t* n) {
  AppMeta m;
  if (!store_find(id, &m) || m.state != fnx::app_state::READY) return nullptr;
  char p[96];
  path(p, sizeof p, m.where, m.id, "fxp");
  return open_sized(p, n);
}

// ---- Install (link task only) ----

static struct {
  bool active;
  uint8_t where;
  char id[fnx::ID_LEN], version[fnx::ID_LEN];
  uint32_t size, offset;
  uint8_t sha[32];
  FILE* f;
  mbedtls_sha256_context ctx;
} s_in;

void store_abort() {
  if (!s_in.active) return;
  char p[96];
  fclose(s_in.f);
  path(p, sizeof p, s_in.where, s_in.id, "tmp");
  unlink(p);
  mbedtls_sha256_free(&s_in.ctx);
  s_in.active = false;
}

bool store_installing(const char** id) {
  if (id) *id = s_in.id;
  return s_in.active;
}

uint8_t store_begin(const char* id, const char* version, uint32_t size, const uint8_t sha[32]) {
  store_abort();  // a new BEGIN restarts any install in progress
  if (!s_mounted) return fnx::app::STORAGE;
  if (!fpkg::valid_id(id) || size < fpkg::HEADER_LEN || strlen(version) >= sizeof(AppMeta::version))
    return fnx::app::BAD_PACKAGE;
  if (!store_find(id, nullptr) && store_count() >= MAX_APPS) return fnx::app::NO_SPACE;
  uint8_t where;
  bool sd;
  {
    Lock l;
    sd = s_sd;
  }
  if (sd) {
    // No size cap on the card (user, 2026-11-02): free space and FAT32's 4 GB per file only.
    where = fnx::app_where::SD;
    uint64_t total = 0, free = 0;
    if (esp_vfs_fat_info(SD_PATH, &total, &free) != ESP_OK) {
      sd_probe();  // pulled out since the last check
      return fnx::app::STORAGE;
    }
    if ((uint64_t)size + SD_SPARE > free) return fnx::app::NO_SPACE;
  } else {
    where = fnx::app_where::FLASH;
    if (size > FLASH_PKG_MAX) return fnx::app::TOO_BIG_FOR_FLASH;
    size_t total = 0, used = 0;
    esp_littlefs_info("apps", &total, &used);
    // The temp file sits next to the old copy until commit; keep a few blocks spare for metadata.
    if (used + size + 16 * 4096 > total) return fnx::app::NO_SPACE;
  }
  char p[96];
  path(p, sizeof p, where, id, "tmp");
  s_in.f = fopen(p, "wb");
  if (!s_in.f) {
    if (sd) sd_probe();
    return fnx::app::STORAGE;
  }
  s_in.where = where;
  strlcpy(s_in.id, id, sizeof s_in.id);
  strlcpy(s_in.version, version, sizeof s_in.version);
  s_in.size = size;
  s_in.offset = 0;
  memcpy(s_in.sha, sha, 32);
  mbedtls_sha256_init(&s_in.ctx);
  mbedtls_sha256_starts(&s_in.ctx, 0);
  s_in.active = true;
  return fnx::app::OK;
}

uint32_t store_chunk(uint32_t offset, const uint8_t* data, size_t n) {
  if (!s_in.active || offset != s_in.offset || n > s_in.size - s_in.offset) return s_in.offset;
  if (fwrite(data, 1, n, s_in.f) != n) {
    store_abort();
    return 0;
  }
  mbedtls_sha256_update(&s_in.ctx, data, n);
  s_in.offset += n;
  return s_in.offset;
}

static bool newer_than_me(const uint8_t v[3]) {
  unsigned a = 0, b = 0, c = 0;
  sscanf(esp_app_get_description()->version, "%u.%u.%u", &a, &b, &c);
  uint32_t mine = a << 16 | b << 8 | c, need = (uint32_t)v[0] << 16 | v[1] << 8 | v[2];
  return need > mine;
}

static void drop_files(uint8_t where, const char* id) {
  char p[96];
  path(p, sizeof p, where, id, "sha");
  unlink(p);
  path(p, sizeof p, where, id, "fxp");
  unlink(p);
}

uint8_t store_end(char* why, size_t cap) {
  why[0] = 0;
  if (!s_in.active) {
    strlcpy(why, "no install in progress", cap);
    return fnx::app::BAD_PACKAGE;
  }
  uint8_t got[32];
  mbedtls_sha256_finish(&s_in.ctx, got);
  bool flushed = fflush(s_in.f) == 0 && fsync(fileno(s_in.f)) == 0;
  bool complete = s_in.offset == s_in.size;
  bool hash_ok = memcmp(got, s_in.sha, 32) == 0;
  char id[fnx::ID_LEN], version[fnx::ID_LEN];
  uint8_t where = s_in.where;
  strlcpy(id, s_in.id, sizeof id);
  strlcpy(version, s_in.version, sizeof version);
  if (!flushed || !complete || !hash_ok) {
    strlcpy(why, !flushed ? "write failed" : !complete ? "size mismatch" : "sha256 mismatch", cap);
    store_abort();
    return !flushed ? fnx::app::STORAGE : fnx::app::BAD_HASH;
  }
  fclose(s_in.f);
  mbedtls_sha256_free(&s_in.ctx);
  s_in.active = false;

  char tmp[96], fxp[96], sha[96];
  path(tmp, sizeof tmp, where, id, "tmp");
  path(fxp, sizeof fxp, where, id, "fxp");
  path(sha, sizeof sha, where, id, "sha");
  // Check the package itself: the same rules as fenix-pkg, plus id and version match BEGIN. It is
  // read in pieces (P39): pixels are skipped, so this needs the strings and props only.
  size_t n = 0;
  FILE* f = open_sized(tmp, &n);
  const char* err = f ? nullptr : "read back failed";
  fpkg::Package pkg;
  if (!err && n != s_in.size) err = "read back failed";
  if (!err) {
    fpkg::FileSource src(f, n);
    err = pkg.parse(src, n);
  }
  if (f) fclose(f);
  if (!err && (pkg.id != id || pkg.version != version)) err = "id or version differs from install request";
  if (!err && newer_than_me(pkg.head.min_fenixos)) err = "needs a newer fenixOS";
  if (err) {
    strlcpy(why, err, cap);
    unlink(tmp);
    return fnx::app::BAD_PACKAGE;
  }
  // Commit: drop the old hash first so a stale hash never pairs with the new package, then
  // rename, then write the new hash. LittleFS replaces the old .fxp atomically; FAT does not
  // rename over a file, so the card's old copy goes first (a power cut then loses the app, and
  // the PC's Sync puts it back).
  unlink(sha);
  if (where == fnx::app_where::SD) unlink(fxp);
  if (rename(tmp, fxp) != 0) {
    unlink(tmp);
    strlcpy(why, "rename failed", cap);
    return fnx::app::STORAGE;
  }
  f = fopen(sha, "wb");
  bool ok = f && fwrite(s_in.sha, 1, 32, f) == 32;
  if (f && fclose(f) != 0) ok = false;
  if (!ok) {
    unlink(fxp);
    strlcpy(why, "hash write failed", cap);
    return fnx::app::STORAGE;
  }
  // One app, one place: drop a copy in the other store.
  AppMeta old{};
  bool had = store_find(id, &old);
  if (had && old.where != where && old.state == fnx::app_state::READY) drop_files(old.where, id);
  AppMeta m{};
  set_meta(m, pkg.id, pkg.version, pkg.name, pkg.head.tile_color);
  m.where = where;
  m.state = fnx::app_state::READY;
  {
    Lock l;
    AppMeta* e = find_in(s_index, id);
    if (e) {
      *e = m;
    } else {
      s_index.push_back(m);
      sort(s_index);
    }
  }
  if (where == fnx::app_where::SD || (had && old.where == fnx::app_where::SD)) save_sd_idx();
  if (where == fnx::app_where::SD) sd_update_free();
  return fnx::app::OK;
}

uint8_t store_remove(const char* id) {
  if (!fpkg::valid_id(id)) return fnx::app::UNKNOWN_APP;
  const char* busy;
  if (store_installing(&busy) && strcmp(busy, id) == 0) store_abort();
  AppMeta m;
  if (!store_find(id, &m)) return fnx::app::UNKNOWN_APP;
  // Its files are on a card that is out: removing it here would bring it back with the card.
  if (m.state == fnx::app_state::CARD_MISSING) return fnx::app::STORAGE;
  char p[96];
  path(p, sizeof p, m.where, id, "fxp");
  if (unlink(p) != 0 && exists(p)) return fnx::app::STORAGE;
  path(p, sizeof p, m.where, id, "sha");
  unlink(p);
  {
    Lock l;
    s_index.erase(std::remove_if(s_index.begin(), s_index.end(),
                                 [&](const AppMeta& e) { return strcmp(e.id, id) == 0; }),
                  s_index.end());
  }
  if (m.where == fnx::app_where::SD) {
    save_sd_idx();
    sd_update_free();
  }
  return fnx::app::OK;
}

}  // namespace fx::sys
