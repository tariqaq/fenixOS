// Files under /sd/fenixos over the link (P53; proto/spec.md "Files (0x6_)").
// The PC lists and reads anything there, deletes in the folders it may change, and writes only to
// fonts/, data/ and wallpaper/. A write goes to <path>.part with <path>.pin (size and sha256)
// beside it, so a cut transfer goes on from where it stopped, as firmware staging does. FILE_END
// checks the sha256 in a short-lived task (a big font takes seconds to read back), then renames
// the part into place. Link task only, except that check and shot_save (the UI task, which owns
// the LCD); nothing stays allocated at idle.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <new>

#include "esp_timer.h"
#include "fenix_hal.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

namespace fx::sys {

namespace file = fnx::file;

static constexpr const char* ROOT = "/sd/fenixos";
static constexpr size_t FULL = 160;            // ROOT + "/" + a path + ".part"
static constexpr int64_t IDLE_US = 10000000;   // a write or read with no message for this long is closed
static constexpr size_t CHECK_BUF = 4096;

static const char* const WRITE_DIRS[] = {"fonts", "data", "wallpaper"};
static const char* const DELETE_DIRS[] = {"fonts", "data", "wallpaper", "logs", "crash", "shots"};

// ---- Paths ----

// A path relative to /sd/fenixos: parts split by '/', none empty, "." or "..", none ending in a dot
// or a space (FAT drops those, so two names would mean one file), no '\\' or ':' or other
// characters FAT refuses. "" is the top, for a list.
static bool path_ok(const char* p, bool top_ok) {
  size_t n = strlen(p);
  if (!n) return top_ok;
  if (n >= fnx::PATH_LEN) return false;
  const char* part = p;
  for (const char* c = p;; c++) {
    if (*c == '/' || !*c) {
      if (c == part || c[-1] == '.' || c[-1] == ' ') return false;  // also "." and ".."
      if (!*c) return true;
      part = c + 1;
    } else if ((uint8_t)*c < 0x20 || strchr("\\:*?\"<>|", *c)) {
      return false;
    }
  }
}

// The path names something inside one of `dirs` (not the folder itself).
template <size_t N>
static bool inside(const char* p, const char* const (&dirs)[N]) {
  const char* slash = strchr(p, '/');
  if (!slash || !slash[1]) return false;
  size_t n = slash - p;
  for (const char* d : dirs)
    if (strlen(d) == n && !strncasecmp(p, d, n)) return true;
  return false;
}

static bool ends_with(const char* s, const char* tail) {
  size_t n = strlen(s), t = strlen(tail);
  return n >= t && !strcasecmp(s + n - t, tail);
}

static void join(char* out, const char* path, const char* suffix = "") {
  snprintf(out, FULL, "%s%s%.127s%.5s", ROOT, path[0] ? "/" : "", path, suffix);
}

// The folders above a file, made as needed.
static void make_dirs(const char* path) {
  char full[FULL];
  join(full, path);
  for (char* s = full + strlen(ROOT); s; s = strchr(s + 1, '/')) {
    *s = 0;
    mkdir(full, 0775);
    *s = '/';
  }
}

static bool card() { return sd_info().state == fnx::sd_state::READY; }

static uint32_t le32(const uint8_t* b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static void put_le32(uint8_t* b, uint32_t v) {
  for (int i = 0; i < 4; i++) b[i] = v >> (8 * i);
}

// ---- Replies ----

static void result(uint8_t op, uint8_t code, const char* path, const char* text = "") {
  uint8_t b[2 + 1 + fnx::PATH_LEN + 64];
  fnx::Writer w(b, sizeof b);
  w.u8(op);
  w.u8(code);
  w.str(path);
  w.bytes(text, strnlen(text, 63));
  link_send(fnx::ty::FILE_RESULT, b, w.size());
}

static void bad_body(const char* what) {
  uint8_t b[32] = {fnx::err::BAD_BODY};
  size_t n = strnlen(what, sizeof b - 1);
  memcpy(b + 1, what, n);
  link_send(fnx::ty::ERROR, b, n + 1);
}

static void ack(uint32_t next) {
  uint8_t b[4];
  put_le32(b, next);
  link_send(fnx::ty::FILE_ACK, b, 4);
}

// ---- State ----

struct Rd {  // the file being read, kept open between FILE_READ_REQs
  FILE* f;
  char path[fnx::PATH_LEN];
  uint32_t size, pos;
  int64_t at;
};
struct Wr {  // the file being written
  bool on;
  FILE* f;
  char path[fnx::PATH_LEN];
  uint32_t size, next;
  uint8_t sha[32];
  int64_t at;
};
static Rd s_rd;
static Wr s_wr;
static Wr s_job;                // what the check task checks
static volatile bool s_checking;

static void rd_close() {
  if (s_rd.f) fclose(s_rd.f);
  s_rd = Rd{};
}

// The part and its pin stay on the card for a resume.
static void wr_close() {
  if (s_wr.f) fclose(s_wr.f);
  s_wr = Wr{};
}

// ---- List ----

static void list(fnx::Reader& r) {
  uint16_t start = r.u16();
  char path[fnx::PATH_LEN];
  r.str(path, sizeof path);
  if (!r.done()) return bad_body("file_list");
  if (!path_ok(path, true)) return result(file::OP_LIST, file::NOT_ALLOWED, path);
  if (!card()) return result(file::OP_LIST, file::NO_CARD, path);
  char full[FULL];
  join(full, path);
  if (!path[0]) mkdir(ROOT, 0775);
  DIR* d = opendir(full);
  if (!d) return result(file::OP_LIST, file::NOT_FOUND, path);
  constexpr size_t CAP = fnx::MAX_PAYLOAD - 2;  // the body; type and seq come first
  std::unique_ptr<uint8_t[]> b(new (std::nothrow) uint8_t[CAP]);
  if (!b) {
    closedir(d);
    return result(file::OP_LIST, file::STORAGE, path, "out of memory");
  }
  fnx::Writer w(b.get(), CAP);
  w.u16(start);
  w.u16(0);  // total, filled in after the walk
  w.str(path);
  uint16_t total = 0;
  bool page_full = false;
  char item[FULL + 260];
  while (struct dirent* e = readdir(d)) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    uint16_t i = total++;
    if (i < start || page_full) continue;
    size_t nl = strnlen(e->d_name, 255);
    if (w.size() + 6 + nl > CAP) {
      page_full = true;
      continue;
    }
    bool dir = e->d_type == DT_DIR;
    uint32_t size = 0;
    if (!dir) {
      snprintf(item, sizeof item, "%s/%.255s", full, e->d_name);
      struct stat st;
      if (!stat(item, &st)) size = st.st_size;
    }
    w.u8(dir);
    w.u32(size);
    w.u8(nl);
    w.bytes(e->d_name, nl);
  }
  closedir(d);
  b[2] = total & 0xFF;
  b[3] = total >> 8;
  link_send(fnx::ty::FILE_LIST, b.get(), w.size());
}

// ---- Read ----

static void read(fnx::Reader& r) {
  uint32_t off = r.u32();
  uint16_t len = r.u16();
  char path[fnx::PATH_LEN];
  r.str(path, sizeof path);
  if (!r.done()) return bad_body("file_read");
  if (!path_ok(path, false)) return result(file::OP_READ, file::NOT_ALLOWED, path);
  if (!card()) {
    rd_close();
    return result(file::OP_READ, file::NO_CARD, path);
  }
  if (!s_rd.f || strcmp(s_rd.path, path)) {
    rd_close();
    char full[FULL];
    join(full, path);
    struct stat st;
    if (stat(full, &st) || S_ISDIR(st.st_mode)) return result(file::OP_READ, file::NOT_FOUND, path);
    s_rd.f = fopen(full, "rb");
    if (!s_rd.f) return result(file::OP_READ, file::STORAGE, path);
    strlcpy(s_rd.path, path, sizeof s_rd.path);
    s_rd.size = st.st_size;
  }
  s_rd.at = esp_timer_get_time();
  if (len > fnx::MAX_CHUNK) len = fnx::MAX_CHUNK;
  if (off > s_rd.size) off = s_rd.size;
  if (len > s_rd.size - off) len = s_rd.size - off;
  std::unique_ptr<uint8_t[]> b(new (std::nothrow) uint8_t[8 + len]);
  if (!b) return result(file::OP_READ, file::STORAGE, path, "out of memory");
  if (off != s_rd.pos && fseek(s_rd.f, off, SEEK_SET)) len = 0;
  size_t got = len ? fread(b.get() + 8, 1, len, s_rd.f) : 0;
  if (got != len) {
    rd_close();
    return result(file::OP_READ, file::STORAGE, path, "read failed");
  }
  s_rd.pos = off + got;
  put_le32(b.get(), off);
  put_le32(b.get() + 4, s_rd.size);
  link_send(fnx::ty::FILE_DATA, b.get(), 8 + got);
}

// ---- Delete ----

static void remove(fnx::Reader& r) {
  char path[fnx::PATH_LEN];
  r.str(path, sizeof path);
  if (!r.done()) return bad_body("file_delete");
  if (!path_ok(path, false) || !inside(path, DELETE_DIRS)) return result(file::OP_DELETE, file::NOT_ALLOWED, path);
  if (!card()) return result(file::OP_DELETE, file::NO_CARD, path);
  // The file being written, or its part or pin.
  const char* busy = s_wr.on ? s_wr.path : s_checking ? s_job.path : nullptr;
  if (busy && !strncasecmp(path, busy, strlen(busy))) return result(file::OP_DELETE, file::BUSY, path, "being written");
  if (s_rd.f && !strcmp(s_rd.path, path)) rd_close();
  char full[FULL];
  join(full, path);
  struct stat st;
  if (stat(full, &st)) return result(file::OP_DELETE, file::NOT_FOUND, path);
  bool dir = S_ISDIR(st.st_mode);
  if (dir ? rmdir(full) : unlink(full))
    return result(file::OP_DELETE, file::STORAGE, path, dir ? "the folder is not empty" : "");
  sd_update_free();
  result(file::OP_DELETE, file::OK, path);
}

// ---- Write ----

static void write_begin(fnx::Reader& r) {
  uint32_t size = r.u32();
  const uint8_t* sha = r.take(32);
  char path[fnx::PATH_LEN];
  r.str(path, sizeof path);
  if (!sha || !r.done()) return bad_body("file_write");
  // A new FILE_WRITE_BEGIN replaces an unfinished write: the PC reconnected.
  wr_close();
  if (s_checking) return result(file::OP_WRITE, file::BUSY, path);
  if (!path_ok(path, false) || !inside(path, WRITE_DIRS) || ends_with(path, ".part") || ends_with(path, ".pin"))
    return result(file::OP_WRITE, file::NOT_ALLOWED, path);
  if (!card()) return result(file::OP_WRITE, file::NO_CARD, path);
  if (s_rd.f && !strcmp(s_rd.path, path)) rd_close();
  char part[FULL], pin[FULL];
  join(part, path, ".part");
  join(pin, path, ".pin");
  make_dirs(path);
  // Resume: the pin names the same size and sha256, so the part holds the start of this file.
  struct stat st;
  uint32_t have = 0;
  if (FILE* f = fopen(pin, "rb")) {
    uint8_t old[36];
    bool same = fread(old, 1, 36, f) == 36 && le32(old) == size && !memcmp(old + 4, sha, 32);
    fclose(f);
    if (same && !stat(part, &st)) have = (uint32_t)st.st_size < size ? st.st_size : size;
  }
  if ((uint64_t)sd_info().free_kb * 1024 < (uint64_t)(size - have) + 64 * 1024)
    return result(file::OP_WRITE, file::NO_SPACE, path);
  FILE* f = nullptr;
  if (have) {
    f = fopen(part, "r+b");
    if (f && fseek(f, have, SEEK_SET)) {
      fclose(f);
      f = nullptr;
    }
  } else {
    uint8_t rec[36];
    put_le32(rec, size);
    memcpy(rec + 4, sha, 32);
    FILE* p = fopen(pin, "wb");
    bool ok = p && fwrite(rec, 1, 36, p) == 36;
    if (p) ok = !fclose(p) && ok;
    if (ok) f = fopen(part, "wb");
  }
  if (!f) return result(file::OP_WRITE, file::STORAGE, path);
  s_wr.on = true;
  s_wr.f = f;
  strlcpy(s_wr.path, path, sizeof s_wr.path);
  s_wr.size = size;
  s_wr.next = have;
  memcpy(s_wr.sha, sha, 32);
  s_wr.at = esp_timer_get_time();
  ack(have);
}

static void write_chunk(fnx::Reader& r) {
  uint32_t off = r.u32();
  size_t n = 0;
  const uint8_t* d = r.rest(&n);
  if (!r.ok()) return bad_body("file_chunk");
  if (!s_wr.on) return result(file::OP_WRITE, file::BUSY, "", "no write open");
  s_wr.at = esp_timer_get_time();
  // Out of order (a lost chunk): ack the offset still expected, and the PC goes back to it.
  if (off == s_wr.next && n <= s_wr.size - s_wr.next) {
    if (fwrite(d, 1, n, s_wr.f) != n) {
      char path[fnx::PATH_LEN];
      strlcpy(path, s_wr.path, sizeof path);
      wr_close();
      return result(file::OP_WRITE, file::STORAGE, path, "write failed");
    }
    s_wr.next += n;
  }
  ack(s_wr.next);
}

static void check_task(void*) {
  char part[FULL], pin[FULL], full[FULL];
  join(part, s_job.path, ".part");
  join(pin, s_job.path, ".pin");
  join(full, s_job.path);
  uint8_t code = file::OK;
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[CHECK_BUF]);
  FILE* f = buf ? fopen(part, "rb") : nullptr;
  if (!f) {
    code = file::STORAGE;
  } else {
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    size_t n;
    while ((n = fread(buf.get(), 1, CHECK_BUF, f)) > 0) mbedtls_sha256_update(&c, buf.get(), n);
    bool err = ferror(f);
    fclose(f);
    uint8_t got[32];
    mbedtls_sha256_finish(&c, got);
    mbedtls_sha256_free(&c);
    if (err) code = file::STORAGE;
    else if (memcmp(got, s_job.sha, 32)) code = file::BAD_HASH;
  }
  buf.reset();
  if (code == file::OK) {
    unlink(full);  // FAT's rename does not replace
    if (rename(part, full)) code = file::STORAGE;
    else unlink(pin);
  } else if (code == file::BAD_HASH) {
    unlink(part);
    unlink(pin);
  }
  sd_update_free();
  result(file::OP_WRITE, code, s_job.path, code == file::BAD_HASH ? "damaged on the way; send it again" : "");
  s_checking = false;
  vTaskDelete(nullptr);
}

static void write_end(fnx::Reader& r) {
  if (!r.done()) return bad_body("file_end");
  if (!s_wr.on) return result(file::OP_WRITE, file::BUSY, "", "no write open");
  if (s_wr.next != s_wr.size) {
    char path[fnx::PATH_LEN];
    strlcpy(path, s_wr.path, sizeof path);
    wr_close();
    return result(file::OP_WRITE, file::BAD_HASH, path, "cut short");
  }
  fclose(s_wr.f);
  s_wr.f = nullptr;
  s_job = s_wr;
  s_wr = Wr{};
  s_checking = true;
  if (xTaskCreatePinnedToCore(check_task, "files", 4096, nullptr, 3, nullptr, 0) != pdPASS) {
    s_checking = false;
    result(file::OP_WRITE, file::BUSY, s_job.path, "out of memory");
  }
}

// ---- Screenshot (SHOT_SAVE) ----

static constexpr int SHOT_BAND = 8;  // rows read back per pass: 5 KB at 320 wide

static void put_le16(uint8_t* b, uint16_t v) {
  b[0] = v;
  b[1] = v >> 8;
}

// UI task: read the panel back in bands and write a top-down 16-bit BMP (RGB565 bitfields), which
// needs no colour conversion. Named by the local time, or the uptime before the clock is set.
void shot_save() {
  if (!card()) return result(file::OP_SHOT, file::NO_CARD, "");
  int w = hal::display_width(), h = hal::display_height();
  char stem[32];
  struct tm t;
  if (clock_local(&t)) strftime(stem, sizeof stem, "shots/%Y%m%d-%H%M%S", &t);
  else snprintf(stem, sizeof stem, "shots/up-%lu", (unsigned long)(esp_timer_get_time() / 1000000));
  char path[48], full[FULL];
  struct stat st;
  for (int i = 1; i < 10; i++) {  // two shots in one second get -2, -3 ...
    snprintf(path, sizeof path, i == 1 ? "%s.bmp" : "%s-%d.bmp", stem, i);
    join(full, path);
    if (stat(full, &st) != 0) break;
  }
  make_dirs(path);

  uint32_t row = (uint32_t)w * 2, size = row * h, off = 14 + 40 + 12;
  uint8_t hd[66] = {'B', 'M'};
  put_le32(hd + 2, off + size);
  put_le32(hd + 10, off);
  put_le32(hd + 14, 40);
  put_le32(hd + 18, w);
  put_le32(hd + 22, (uint32_t)-h);  // negative: rows top to bottom
  put_le16(hd + 26, 1);
  put_le16(hd + 28, 16);
  put_le32(hd + 30, 3);  // BI_BITFIELDS
  put_le32(hd + 34, size);
  put_le32(hd + 38, 2835);  // 72 dpi
  put_le32(hd + 42, 2835);
  put_le32(hd + 54, 0xF800);
  put_le32(hd + 58, 0x07E0);
  put_le32(hd + 62, 0x001F);

  int band = SHOT_BAND;
  std::unique_ptr<uint16_t[]> px(new (std::nothrow) uint16_t[w * band]);
  if (!px) {
    band = 2;
    px.reset(new (std::nothrow) uint16_t[w * band]);
    if (!px) return result(file::OP_SHOT, file::STORAGE, path, "out of memory");
  }
  FILE* f = fopen(full, "wb");
  if (!f) return result(file::OP_SHOT, file::STORAGE, path);
  bool ok = fwrite(hd, 1, sizeof hd, f) == sizeof hd;
  for (int y = 0; ok && y < h; y += band) {
    int n = h - y < band ? h - y : band;
    hal::display_read(0, y, w, n, px.get());
    ok = fwrite(px.get(), 1, row * n, f) == row * n;
  }
  ok = fclose(f) == 0 && ok;
  if (!ok) {
    unlink(full);
    return result(file::OP_SHOT, sd_info().free_kb < 256 ? file::NO_SPACE : file::STORAGE, path);
  }
  logs_note(3, "screenshot saved to %s", path);
  result(file::OP_SHOT, file::OK, path);
}

// ---- Link task ----

void files_frame(uint8_t type, fnx::Reader& r) {
  switch (type) {
    case fnx::ty::FILE_LIST_REQ:
      return list(r);
    case fnx::ty::FILE_READ_REQ:
      return read(r);
    case fnx::ty::FILE_DELETE:
      return remove(r);
    case fnx::ty::FILE_WRITE_BEGIN:
      return write_begin(r);
    case fnx::ty::FILE_CHUNK:
      return write_chunk(r);
    case fnx::ty::FILE_END:
      return write_end(r);
    default:
      return;
  }
}

void files_tick(int64_t now) {
  if (s_wr.on && now - s_wr.at > IDLE_US) wr_close();
  if (s_rd.f && now - s_rd.at > IDLE_US) rd_close();
}

void files_abort() {
  wr_close();
  rd_close();
}

}  // namespace fx::sys
