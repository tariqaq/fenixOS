// Firmware updates (P52, docs/ota.md): two app slots, signed .fwp packages, rollback.
// Ways in: the PC over the link (FW_BEGIN..FW_END, staged on the SD card when one is in, else
// written straight into the spare slot), a package on the card's root, and "Go back" to a kept one.
// Every way checks sha256, the ECDSA P-256 signature (fw_pubkey.h) and that the header's version
// is the image's own before the board boots it. Long work (check, copy) runs in a short-lived
// task, so the link keeps answering and the UI keeps drawing. Nothing stays allocated at idle.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <new>

#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_pubkey.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

namespace fx::sys {

static constexpr const char* FW_DIR = "/sd/fenixos/firmware";
static constexpr size_t BUF = 4096;           // the one copy buffer, only while working
static constexpr uint32_t APP_DESC_MAGIC = 0xABCD5432;
static constexpr int64_t MARK_GOOD_US = 30 * 1000000LL;  // Home shown this long: the new image is good
static constexpr uint8_t LAYOUT = 2;          // NVS "ota"/"layout": 2 = two slots (P52)

// ---- Status (read by the UI) ----

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static OtaStatus s_st;
static OtaOffer s_offer;
static char s_failed[32];  // a rolled-back version, or ""
static bool s_pending;     // this image boots on trial until marked good

static void status(uint8_t phase, uint8_t pct, const char* text = nullptr, const char* version = nullptr) {
  taskENTER_CRITICAL(&s_mux);
  bool started = s_st.phase == OTA_IDLE && phase != OTA_IDLE;
  s_st.phase = phase;
  s_st.pct = pct;
  if (text) strlcpy(s_st.text, text, sizeof s_st.text);
  if (version) strlcpy(s_st.version, version, sizeof s_st.version);
  taskEXIT_CRITICAL(&s_mux);
  if (started || phase == OTA_FAILED) link_post(LinkEv::OTA);  // the UI shows the progress screen
}

OtaStatus ota_status() {
  taskENTER_CRITICAL(&s_mux);
  OtaStatus s = s_st;
  taskEXIT_CRITICAL(&s_mux);
  return s;
}

OtaOffer ota_offer() {
  taskENTER_CRITICAL(&s_mux);
  OtaOffer o = s_offer;
  taskEXIT_CRITICAL(&s_mux);
  return o;
}

const char* ota_failed_version() { return s_failed[0] ? s_failed : nullptr; }

void ota_dismiss() {
  if (s_st.phase == OTA_FAILED) status(OTA_IDLE, 0, "");
}

// ---- Package checks ----

static const char* running_version() { return esp_app_get_description()->version; }

// <0, 0, >0 like strcmp, on "major.minor.patch".
int ota_vercmp(const char* a, const char* b) {
  unsigned x[3] = {}, y[3] = {};
  sscanf(a, "%u.%u.%u", &x[0], &x[1], &x[2]);
  sscanf(b, "%u.%u.%u", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

struct Head {
  char version[32];
  uint32_t size;
  uint8_t sha[32], sig[64];
  uint32_t len;  // header bytes before the image
};

static size_t head_bytes(const Head& h, uint8_t* out) {
  size_t n = strlen(h.version), p = 0;
  memcpy(out, "FXFW", 4);
  out[4] = 1;
  out[5] = n;
  memcpy(out + 6, h.version, n);
  p = 6 + n;
  for (int i = 0; i < 4; i++) out[p++] = h.size >> (8 * i);
  memcpy(out + p, h.sha, 32);
  memcpy(out + p + 32, h.sig, 64);
  return p + 96;
}

static bool read_head(FILE* f, Head* h) {
  uint8_t b[6 + 31 + 100];
  if (fread(b, 1, 6, f) != 6 || memcmp(b, "FXFW", 4) || b[4] != 1 || b[5] == 0 || b[5] >= sizeof h->version) return false;
  size_t n = b[5];
  if (fread(b + 6, 1, n + 100, f) != n + 100) return false;
  memcpy(h->version, b + 6, n);
  h->version[n] = 0;
  const uint8_t* p = b + 6 + n;
  h->size = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
  memcpy(h->sha, p + 4, 32);
  memcpy(h->sig, p + 36, 64);
  h->len = 6 + n + 100;
  return true;
}

static bool sig_ok(const uint8_t sha[32], const uint8_t sig[64]) {
  mbedtls_ecp_group g;
  mbedtls_ecp_point q;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&g);
  mbedtls_ecp_point_init(&q);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  bool ok = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
            mbedtls_ecp_point_read_binary(&g, &q, FW_PUBKEY, sizeof FW_PUBKEY) == 0 &&
            mbedtls_mpi_read_binary(&r, sig, 32) == 0 && mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0 &&
            mbedtls_ecdsa_verify(&g, sha, 32, &q, &r, &s) == 0;
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&q);
  mbedtls_ecp_group_free(&g);
  return ok;
}

// The version inside an app image (esp_app_desc_t after the first segment header), or false.
static bool image_version(const uint8_t* img, size_t n, char* out, size_t cap) {
  if (n < 80 || img[0] != 0xE9) return false;
  uint32_t magic = img[32] | img[33] << 8 | img[34] << 16 | (uint32_t)img[35] << 24;
  if (magic != APP_DESC_MAGIC) return false;
  size_t len = strnlen((const char*)img + 48, 32);
  if (len >= cap) return false;
  memcpy(out, img + 48, len);
  out[len] = 0;
  return true;
}

// sha256 matches, the signature is good and the header names the image's own version.
static uint8_t check(const Head& h, const uint8_t computed[32], const uint8_t* first, size_t first_n) {
  if (memcmp(computed, h.sha, 32)) return fnx::fw::BAD_HASH;
  if (!sig_ok(h.sha, h.sig)) return fnx::fw::BAD_SIGNATURE;
  char v[32];
  if (!image_version(first, first_n, v, sizeof v) || strcmp(v, h.version)) return fnx::fw::BAD_SIGNATURE;
  return fnx::fw::OK;
}

static const char* code_text(uint8_t c) {
  switch (c) {
    case fnx::fw::OK: return "";
    case fnx::fw::NO_SPACE: return "Not enough space";
    case fnx::fw::BAD_HASH: return "The package is damaged";
    case fnx::fw::BAD_SIGNATURE: return "The package is not signed by fenixOS";
    case fnx::fw::BUSY: return "Busy";
    case fnx::fw::OLDER: return "Older than this firmware";
    default: return "Storage error";
  }
}

// ---- Kept packages on the card ----

static void fw_path(char* out, size_t cap, const char* version, const char* ext) {
  snprintf(out, cap, "%s/%.31s.%.4s", FW_DIR, version, ext);
}

static bool card_ready() { return sd_info().state == fnx::sd_state::READY; }

// Scans the card: a newer package on its root ("fenixOS_<v>.fwp") and the newest kept older one.
// sd task (after a mount) and the ota task (after an install). Reads only the headers.
void ota_scan_sd() {
  OtaOffer o{};
  if (card_ready()) {
    const char* run = running_version();
    char path[300];
    if (DIR* d = opendir(SD_PATH)) {
      while (struct dirent* e = readdir(d)) {
        size_t n = strlen(e->d_name);
        if (n < 12 || strncasecmp(e->d_name, "fenixos_", 8) || strcasecmp(e->d_name + n - 4, ".fwp")) continue;
        snprintf(path, sizeof path, "%s/%.250s", SD_PATH, e->d_name);
        Head h;
        FILE* f = fopen(path, "rb");
        bool ok = f && read_head(f, &h);
        if (f) fclose(f);
        if (ok && ota_vercmp(h.version, run) > 0 && (!o.update[0] || ota_vercmp(h.version, o.update) > 0)) {
          strlcpy(o.update, h.version, sizeof o.update);
          strlcpy(o.update_file, e->d_name, sizeof o.update_file);
        }
      }
      closedir(d);
    }
    if (DIR* d = opendir(FW_DIR)) {
      while (struct dirent* e = readdir(d)) {
        size_t n = strlen(e->d_name);
        if (n < 5 || n - 4 >= sizeof o.back || strcasecmp(e->d_name + n - 4, ".fwp")) continue;
        char v[32];
        memcpy(v, e->d_name, n - 4);
        v[n - 4] = 0;
        if (ota_vercmp(v, run) < 0 && (!o.back[0] || ota_vercmp(v, o.back) > 0)) strlcpy(o.back, v, sizeof o.back);
      }
      closedir(d);
    }
  }
  taskENTER_CRITICAL(&s_mux);
  bool changed = memcmp(&s_offer, &o, sizeof o) != 0;
  s_offer = o;
  taskEXIT_CRITICAL(&s_mux);
  if (changed) link_post(LinkEv::SD_CHANGED);  // About and Settings show the offer
}

// After an install: keep the new package and the running one's (Go back), delete the rest.
static void prune(const char* keep) {
  DIR* d = opendir(FW_DIR);
  if (!d) return;
  char path[300];
  while (struct dirent* e = readdir(d)) {
    size_t n = strlen(e->d_name);
    if (n < 5) continue;
    char v[32] = {};
    if (n - 4 < sizeof v) memcpy(v, e->d_name, n - 4);
    if (!strcasecmp(e->d_name + n - 4, ".fwp") && (!strcmp(v, keep) || !strcmp(v, running_version()))) continue;
    snprintf(path, sizeof path, "%s/%.250s", FW_DIR, e->d_name);
    unlink(path);
  }
  closedir(d);
}

// ---- Install from a file (staged, root package, go back). ota task only ----

static void restart_soon() {
  status(OTA_RESTARTING, 100, "Restarting");
  vTaskDelay(pdMS_TO_TICKS(1500));  // FW_RESULT out, the screen seen
  esp_restart();
}

static uint8_t install_file(const char* path, bool allow_older) {
  FILE* f = fopen(path, "rb");
  if (!f) return fnx::fw::STORAGE;
  Head h;
  if (!read_head(f, &h)) {
    fclose(f);
    return fnx::fw::BAD_HASH;
  }
  status(OTA_CHECKING, 0, "Checking", h.version);
  if (!allow_older && ota_vercmp(h.version, running_version()) < 0) {
    fclose(f);
    return fnx::fw::OLDER;
  }
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (!part || h.size > part->size) {
    fclose(f);
    return fnx::fw::NO_SPACE;
  }
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[BUF]);
  if (!buf) {
    fclose(f);
    return fnx::fw::STORAGE;
  }
  // Pass 1: hash and check before the slot is touched.
  mbedtls_sha256_context sc;
  mbedtls_sha256_init(&sc);
  mbedtls_sha256_starts(&sc, 0);
  uint8_t first[80] = {};
  uint32_t done = 0;
  while (done < h.size) {
    size_t want = h.size - done < BUF ? h.size - done : BUF;
    if (fread(buf.get(), 1, want, f) != want) break;
    if (done == 0) memcpy(first, buf.get(), want < sizeof first ? want : sizeof first);
    mbedtls_sha256_update(&sc, buf.get(), want);
    done += want;
    status(OTA_CHECKING, done * 100ULL / h.size);
  }
  uint8_t sha[32];
  mbedtls_sha256_finish(&sc, sha);
  mbedtls_sha256_free(&sc);
  if (done != h.size) {
    fclose(f);
    return fnx::fw::BAD_HASH;
  }
  uint8_t code = check(h, sha, first, sizeof first);
  if (code != fnx::fw::OK) {
    fclose(f);
    return code;
  }
  // Pass 2: copy into the spare slot.
  status(OTA_WRITING, 0, "Installing");
  esp_ota_handle_t oh;
  if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &oh) != ESP_OK) {
    fclose(f);
    return fnx::fw::STORAGE;
  }
  fseek(f, h.len, SEEK_SET);
  done = 0;
  bool ok = true;
  while (ok && done < h.size) {
    size_t want = h.size - done < BUF ? h.size - done : BUF;
    ok = fread(buf.get(), 1, want, f) == want && esp_ota_write(oh, buf.get(), want) == ESP_OK;
    done += want;
    status(OTA_WRITING, done * 100ULL / h.size);
  }
  fclose(f);
  if (!ok) {
    esp_ota_abort(oh);
    return fnx::fw::STORAGE;
  }
  if (esp_ota_end(oh) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) return fnx::fw::STORAGE;
  return fnx::fw::OK;
}

// ---- The ota task ----

enum Job : uint8_t { JOB_STAGED, JOB_DIRECT, JOB_ROOT, JOB_BACK };
static volatile bool s_working;  // the ota task runs
static Job s_job;
static bool s_from_link;
static char s_job_arg[300];

static void result(uint8_t code, const char* version) {
  if (s_from_link) {
    const char* t = code_text(code);
    uint8_t body[64];
    body[0] = code;
    size_t n = strlen(t);
    memcpy(body + 1, t, n);
    link_send(fnx::ty::FW_RESULT, body, n + 1);
  }
  if (code == fnx::fw::OK) {
    link_log(3, "firmware v%s installed, restarting", version);
    restart_soon();
  }
  link_log(2, "firmware update failed: %s", code_text(code));
  status(OTA_FAILED, 0, code_text(code));
}

static struct Rx {
  bool on, to_sd;
  Head h;
  FILE* f;
  uint32_t next;  // next image offset
  esp_ota_handle_t oh;
  const esp_partition_t* part;
  mbedtls_sha256_context sc;
  uint8_t first[80];
} s_rx;

static void ota_task(void*) {
  uint8_t code = fnx::fw::STORAGE;
  char version[32] = {};
  switch (s_job) {
    case JOB_STAGED: {
      // The .part on the card is whole: name it .fwp (kept for Go back), then install it.
      char fwp[300];
      fw_path(fwp, sizeof fwp, s_rx.h.version, "fwp");
      strlcpy(version, s_rx.h.version, sizeof version);
      unlink(fwp);
      code = rename(s_job_arg, fwp) == 0 ? install_file(fwp, false) : fnx::fw::STORAGE;
      if (code == fnx::fw::OK) prune(version);
      if (code == fnx::fw::BAD_HASH || code == fnx::fw::BAD_SIGNATURE) unlink(fwp);  // never offer it
      break;
    }
    case JOB_DIRECT: {
      strlcpy(version, s_rx.h.version, sizeof version);
      status(OTA_CHECKING, 100, "Checking");
      uint8_t sha[32];
      mbedtls_sha256_finish(&s_rx.sc, sha);
      mbedtls_sha256_free(&s_rx.sc);
      code = check(s_rx.h, sha, s_rx.first, sizeof s_rx.first);
      if (code != fnx::fw::OK)
        esp_ota_abort(s_rx.oh);
      else if (esp_ota_end(s_rx.oh) != ESP_OK || esp_ota_set_boot_partition(s_rx.part) != ESP_OK)
        code = fnx::fw::STORAGE;
      break;
    }
    case JOB_ROOT: {
      // Move the card's root package into the kept folder (a FAT rename), then install it.
      char src[300], fwp[300];
      snprintf(src, sizeof src, "%s/%.250s", SD_PATH, s_job_arg);
      FILE* f = fopen(src, "rb");
      Head h;
      bool ok = f && read_head(f, &h);
      if (f) fclose(f);
      if (!ok) break;
      strlcpy(version, h.version, sizeof version);
      mkdir("/sd/fenixos", 0775);
      mkdir(FW_DIR, 0775);
      fw_path(fwp, sizeof fwp, h.version, "fwp");
      unlink(fwp);
      const char* use = rename(src, fwp) == 0 ? fwp : src;
      code = install_file(use, false);
      if (code == fnx::fw::OK) prune(version);
      break;
    }
    case JOB_BACK: {
      char fwp[300];
      fw_path(fwp, sizeof fwp, s_job_arg, "fwp");
      strlcpy(version, s_job_arg, sizeof version);
      code = install_file(fwp, true);
      break;
    }
  }
  s_rx.on = false;
  result(code, version);  // no return when ok
  ota_scan_sd();
  s_working = false;
  vTaskDelete(nullptr);
}

static bool start_job(Job j, bool from_link, const char* arg) {
  if (s_working) return false;
  s_working = true;
  s_job = j;
  s_from_link = from_link;
  strlcpy(s_job_arg, arg ? arg : "", sizeof s_job_arg);
  // mbedTLS ECDSA verify and FatFs: ~4 KB of stack; the 4 KB buffer is on the heap.
  if (xTaskCreatePinnedToCore(ota_task, "ota", 6144, nullptr, 3, nullptr, 0) != pdPASS) {
    s_working = false;
    return false;
  }
  return true;
}

bool ota_install_offer(bool back) {
  OtaOffer o = ota_offer();
  if (back ? !o.back[0] : !o.update[0]) return false;
  status(OTA_CHECKING, 0, "Checking", back ? o.back : o.update);
  if (start_job(back ? JOB_BACK : JOB_ROOT, false, back ? o.back : o.update_file)) return true;
  status(OTA_FAILED, 0, code_text(fnx::fw::BUSY));
  return false;
}

// ---- Over the link. Link task only ----

bool ota_busy() { return s_rx.on || s_working; }

uint8_t ota_begin(const fnx::FwBegin& b, uint32_t* next) {
  *next = 0;
  if (ota_busy() || store_installing(nullptr)) return fnx::fw::BUSY;
  if (ota_vercmp(b.version, running_version()) < 0) return fnx::fw::OLDER;
  if (strlen(b.version) >= sizeof s_rx.h.version || !b.size) return fnx::fw::BAD_HASH;
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (!part) return fnx::fw::STORAGE;
  if (b.size > part->size) return fnx::fw::NO_SPACE;
  Rx& x = s_rx;
  x = Rx{};
  strlcpy(x.h.version, b.version, sizeof x.h.version);
  x.h.size = b.size;
  memcpy(x.h.sha, b.sha256, 32);
  memcpy(x.h.sig, b.signature, 64);
  x.part = part;
  x.to_sd = card_ready();
  if (x.to_sd) {
    uint8_t head[6 + 31 + 100];
    size_t hn = head_bytes(x.h, head);
    x.h.len = hn;
    char path[300];
    mkdir("/sd/fenixos", 0775);
    mkdir(FW_DIR, 0775);
    fw_path(path, sizeof path, b.version, "part");
    // Resume a staged image of the same package (same header): send from where it ended.
    if (FILE* f = fopen(path, "rb")) {
      Head old;
      bool same = read_head(f, &old) && old.size == b.size && !memcmp(old.sha, b.sha256, 32);
      fseek(f, 0, SEEK_END);
      long have = ftell(f);
      fclose(f);
      if (same && have >= (long)hn) x.next = (uint32_t)(have - hn) > b.size ? b.size : (uint32_t)(have - hn);
    }
    if ((uint64_t)sd_info().free_kb * 1024 < (uint64_t)(b.size - x.next) + 64 * 1024) return fnx::fw::NO_SPACE;
    if (x.next) {
      x.f = fopen(path, "r+b");
      if (x.f) fseek(x.f, hn + x.next, SEEK_SET);
    } else {
      x.f = fopen(path, "wb");
      if (x.f && fwrite(head, 1, hn, x.f) != hn) {
        fclose(x.f);
        x.f = nullptr;
      }
    }
    if (!x.f) return fnx::fw::STORAGE;
  } else {
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &x.oh) != ESP_OK) return fnx::fw::STORAGE;
    mbedtls_sha256_init(&x.sc);
    mbedtls_sha256_starts(&x.sc, 0);
  }
  x.on = true;
  *next = x.next;
  status(OTA_RECEIVING, x.next * 100ULL / b.size, "Receiving", b.version);
  return fnx::fw::OK;
}

uint32_t ota_chunk(uint32_t off, const uint8_t* d, size_t n) {
  Rx& x = s_rx;
  if (!x.on || s_working) return 0;
  if (off != x.next || n > x.h.size - x.next) return x.next;
  bool ok;
  if (x.to_sd) {
    ok = fwrite(d, 1, n, x.f) == n;
  } else {
    if (off < sizeof x.first) memcpy(x.first + off, d, n < sizeof x.first - off ? n : sizeof x.first - off);
    mbedtls_sha256_update(&x.sc, d, n);
    ok = esp_ota_write(x.oh, d, n) == ESP_OK;
  }
  if (!ok) {
    ota_abort();
    return 0;
  }
  x.next += n;
  status(OTA_RECEIVING, x.next * 100ULL / x.h.size);
  return x.next;
}

bool ota_receiving() { return s_rx.on && !s_working; }

uint8_t ota_end() {
  Rx& x = s_rx;
  if (!x.on || s_working) return fnx::fw::BUSY;
  if (x.next != x.h.size) {
    ota_abort();
    status(OTA_FAILED, 0, code_text(fnx::fw::BAD_HASH));
    return fnx::fw::BAD_HASH;
  }
  if (x.to_sd) {
    fclose(x.f);
    x.f = nullptr;
    sd_update_free();
    char part[300];
    fw_path(part, sizeof part, x.h.version, "part");
    if (!start_job(JOB_STAGED, true, part)) return fnx::fw::BUSY;
  } else if (!start_job(JOB_DIRECT, true, nullptr)) {
    return fnx::fw::BUSY;
  }
  return 0xFF;  // working: the ota task sends FW_RESULT
}

// The link dropped or stalled: a staged image stays on the card (FW_BEGIN resumes it).
void ota_abort() {
  Rx& x = s_rx;
  if (!x.on || s_working) return;
  if (x.f) fclose(x.f);
  if (!x.to_sd) {
    esp_ota_abort(x.oh);
    mbedtls_sha256_free(&x.sc);
  }
  x = Rx{};
  status(OTA_FAILED, 0, "The update stopped. It goes on when the PC sends it again.");
}

// ---- Boot, trial and rollback ----

static esp_timer_handle_t s_good_timer;

static void mark_good(void*) {
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    s_pending = false;
    link_log(3, "firmware v%s kept (it ran 30 s)", running_version());
  }
}

void ota_boot() {
  // First boot on the two-slot layout: the flash app store moved, so clear what was there.
  nvs_handle_t h;
  if (nvs_open("ota", NVS_READWRITE, &h) == ESP_OK) {
    uint8_t layout = 0;
    nvs_get_u8(h, "layout", &layout);
    if (layout != LAYOUT) {
      if (const esp_partition_t* p =
              esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "apps"))
        esp_partition_erase_range(p, 0, p->size);
      nvs_set_u8(h, "layout", LAYOUT);
      nvs_commit(h);
    }
    nvs_close(h);
  }
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK)
    s_pending = st == ESP_OTA_IMG_PENDING_VERIFY;
  if (const esp_partition_t* bad = esp_ota_get_last_invalid_partition()) {
    esp_app_desc_t d;
    if (esp_ota_get_partition_description(bad, &d) == ESP_OK && ota_vercmp(d.version, running_version()) > 0)
      strlcpy(s_failed, d.version, sizeof s_failed);
  }
}

void ota_home_shown() {
  if (!s_pending || s_good_timer) return;
  esp_timer_create_args_t a = {};
  a.callback = mark_good;
  a.name = "ota_good";
  if (esp_timer_create(&a, &s_good_timer) == ESP_OK) esp_timer_start_once(s_good_timer, MARK_GOOD_US);
}

void ota_report() {
  static bool said;
  if (said || !s_failed[0]) return;
  said = true;
  link_log(2, "Update to v%s failed; back on v%s", s_failed, running_version());
}

}  // namespace fx::sys
