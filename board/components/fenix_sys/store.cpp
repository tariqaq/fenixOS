// App store on LittleFS: /apps/<id>.fxp + /apps/<id>.sha. Format and install rules: docs/packages.md.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string_view>

#include "esp_app_desc.h"
#include "esp_littlefs.h"
#include "fenix_pkg.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "mbedtls/sha256.h"

namespace fx::sys {

static constexpr const char* DIR_PATH = "/apps";
static bool s_mounted;

static void path(char* out, size_t cap, const char* id, const char* ext) {
  snprintf(out, cap, "%s/%s.%s", DIR_PATH, id, ext);
}

static bool exists(const char* p) {
  struct stat st;
  return stat(p, &st) == 0;
}

static bool ends_with(const char* s, const char* suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcmp(s + n - m, suffix) == 0;
}

// Delete what a power cut can leave behind: temp files, and a .fxp or .sha without its partner.
static void clean() {
  DIR* d = opendir(DIR_PATH);
  if (!d) return;
  char p[272], other[272];  // d_name is up to 255 bytes
  while (dirent* e = readdir(d)) {
    snprintf(p, sizeof p, "%s/%s", DIR_PATH, e->d_name);
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

bool store_mount() {
  esp_vfs_littlefs_conf_t c = {};
  c.base_path = DIR_PATH;
  c.partition_label = "apps";
  c.format_if_mount_failed = true;
  s_mounted = esp_vfs_littlefs_register(&c) == ESP_OK;
  if (s_mounted) clean();
  return s_mounted;
}

void store_usage(uint32_t* used_kb, uint32_t* total_kb) {
  size_t total = 0, used = 0;
  if (s_mounted) esp_littlefs_info("apps", &total, &used);
  *used_kb = used / 1024;
  *total_kb = total / 1024;
}

int store_list(AppMeta* out, int max) {
  DIR* d = s_mounted ? opendir(DIR_PATH) : nullptr;
  if (!d) return 0;
  int n = 0;
  char p[272];
  uint8_t head[512];
  while (dirent* e = readdir(d)) {
    if (n >= max || !ends_with(e->d_name, ".fxp")) continue;
    AppMeta& m = out[n];
    snprintf(p, sizeof p, "%s/%s", DIR_PATH, e->d_name);
    FILE* f = fopen(p, "rb");
    if (!f) continue;
    size_t got = fread(head, 1, sizeof head, f);
    fclose(f);
    std::string_view id, ver;
    // The id and version are the first strings in practice; 512 bytes holds them.
    if (!fpkg::peek(head, got, &id, &ver)) continue;
    snprintf(m.id, sizeof m.id, "%.*s", (int)id.size(), id.data());
    snprintf(m.version, sizeof m.version, "%.*s", (int)ver.size(), ver.data());
    path(p, sizeof p, m.id, "sha");
    f = fopen(p, "rb");
    if (!f) continue;
    got = fread(m.sha256, 1, 32, f);
    fclose(f);
    if (got == 32) n++;
  }
  closedir(d);
  std::sort(out, out + n, [](const AppMeta& a, const AppMeta& b) { return strcmp(a.id, b.id) < 0; });
  return n;
}

uint8_t* store_load(const char* id, size_t* n) {
  char p[96];
  path(p, sizeof p, id, "fxp");
  FILE* f = fopen(p, "rb");
  if (!f) return nullptr;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* b = size > 0 && (size_t)size <= fpkg::MAX_SIZE ? (uint8_t*)malloc(size) : nullptr;
  if (b && fread(b, 1, size, f) != (size_t)size) {
    free(b);
    b = nullptr;
  }
  fclose(f);
  *n = b ? size : 0;
  return b;
}

// ---- Install (link task only) ----

static struct {
  bool active;
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
  path(p, sizeof p, s_in.id, "tmp");
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
  if (!fpkg::valid_id(id) || size < fpkg::HEADER_LEN || size > fpkg::MAX_SIZE) return fnx::app::BAD_PACKAGE;
  char p[96];
  path(p, sizeof p, id, "fxp");
  if (!exists(p)) {
    AppMeta tmp[MAX_APPS];
    if (store_list(tmp, MAX_APPS) >= MAX_APPS) return fnx::app::NO_SPACE;
  }
  size_t total = 0, used = 0;
  esp_littlefs_info("apps", &total, &used);
  // The temp file sits next to the old copy until commit; keep a few blocks spare for metadata.
  if (used + size + 16 * 4096 > total) return fnx::app::NO_SPACE;
  path(p, sizeof p, id, "tmp");
  s_in.f = fopen(p, "wb");
  if (!s_in.f) return fnx::app::STORAGE;
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
  path(tmp, sizeof tmp, id, "tmp");
  path(fxp, sizeof fxp, id, "fxp");
  path(sha, sizeof sha, id, "sha");
  // Check the package itself: the same rules as fenix-pkg, plus id and version match BEGIN.
  size_t n = 0;
  uint8_t* b = nullptr;
  if (FILE* f = fopen(tmp, "rb")) {
    b = (uint8_t*)malloc(s_in.size);
    n = b ? fread(b, 1, s_in.size, f) : 0;
    fclose(f);
  }
  const char* err = b ? nullptr : "out of memory";
  fpkg::Package pkg;
  if (!err && n != s_in.size) err = "read back failed";
  if (!err) err = pkg.parse(b, n);
  if (!err && (pkg.id != id || pkg.version != version)) err = "id or version differs from install request";
  if (!err && newer_than_me(pkg.head.min_fenixos)) err = "needs a newer fenixOS";
  free(b);
  if (err) {
    strlcpy(why, err, cap);
    unlink(tmp);
    return fnx::app::BAD_PACKAGE;
  }
  // Commit: drop the old hash first so a stale hash never pairs with the new package, then
  // rename (LittleFS replaces the old .fxp atomically), then write the new hash.
  unlink(sha);
  if (rename(tmp, fxp) != 0) {
    unlink(tmp);
    strlcpy(why, "rename failed", cap);
    return fnx::app::STORAGE;
  }
  FILE* f = fopen(sha, "wb");
  bool ok = f && fwrite(s_in.sha, 1, 32, f) == 32;
  if (f) fclose(f);
  if (!ok) {
    unlink(fxp);
    strlcpy(why, "hash write failed", cap);
    return fnx::app::STORAGE;
  }
  return fnx::app::OK;
}

uint8_t store_remove(const char* id) {
  if (!fpkg::valid_id(id)) return fnx::app::UNKNOWN_APP;
  const char* busy;
  if (store_installing(&busy) && strcmp(busy, id) == 0) store_abort();
  char p[96];
  path(p, sizeof p, id, "fxp");
  if (!exists(p)) return fnx::app::UNKNOWN_APP;
  if (unlink(p) != 0) return fnx::app::STORAGE;
  path(p, sizeof p, id, "sha");
  unlink(p);
  return fnx::app::OK;
}

}  // namespace fx::sys
