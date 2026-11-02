// Settings backup on the SD card (P53; docs/sd-card.md "Settings backup").
// /sd/fenixos/backup/settings.bin holds the settings blob, the saved Wi-Fi network (the password
// in plain text, the user's choice) and the weather place. The link task writes it a few seconds
// after any of them changes, and at a mount. After a boot that found no settings and no Wi-Fi
// network in NVS (a new or erased board), a mount offers the card's backup instead (the shell
// shows a sheet on Home until it is answered), and nothing is written until then. An empty board with no backup on
// its card saves its settings at once, so it is not taken for an erased one at the next boot.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>

#include "esp_system.h"
#include "esp_timer.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace fx::sys {

static constexpr const char* DIR = "/sd/fenixos/backup";
static constexpr const char* PATH = "/sd/fenixos/backup/settings.bin";
static constexpr const char* TMP = "/sd/fenixos/backup/settings.tmp";
static constexpr int64_t SETTLE_US = 3000000;  // a slider sends many changes: write once it rests
static constexpr size_t MAX = 512;

// Record tags: tag u8, length u16 LE, bytes.
enum : uint8_t { T_SETTINGS = 1, T_SSID = 2, T_PASS = 3, T_PLACE = 4 };

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_changed;     // when the last change came; 0 = written
static bool s_offer;          // a backup waits for the user's answer
static bool s_answered;       // answered this boot: never offered again until a restart
static char s_offer_ssid[fnx::SSID_LEN];

void backup_changed() {
  portENTER_CRITICAL(&s_mux);
  s_changed = esp_timer_get_time();
  if (!s_changed) s_changed = 1;
  portEXIT_CRITICAL(&s_mux);
}

static void put(std::vector<uint8_t>& b, uint8_t tag, const void* d, size_t n) {
  b.push_back(tag);
  b.push_back(n & 0xFF);
  b.push_back(n >> 8);
  b.insert(b.end(), (const uint8_t*)d, (const uint8_t*)d + n);
}

static bool write_now() {
  std::vector<uint8_t> b{'F', 'X', 'B', '1'};
  uint8_t blob[64];
  put(b, T_SETTINGS, blob, settings_blob(blob, sizeof blob));
  char ssid[fnx::SSID_LEN], pass[fnx::PASS_LEN];
  radio_creds(ssid, sizeof ssid, pass, sizeof pass);
  if (ssid[0]) {
    put(b, T_SSID, ssid, strlen(ssid));
    put(b, T_PASS, pass, strlen(pass));
  }
  fnx::WeatherSet p = weather_place();
  if (p.name[0]) {
    uint8_t w[8 + sizeof p.name];
    memcpy(w, &p.lat_e4, 4);  // little-endian on the ESP32
    memcpy(w + 4, &p.lon_e4, 4);
    size_t n = strnlen(p.name, sizeof p.name - 1);
    memcpy(w + 8, p.name, n);
    put(b, T_PLACE, w, 8 + n);
  }
  mkdir(DIR, 0775);
  FILE* f = fopen(TMP, "wb");
  if (!f) return false;
  bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
  ok = fclose(f) == 0 && ok;
  if (ok) {
    unlink(PATH);
    ok = rename(TMP, PATH) == 0;
  }
  if (!ok) unlink(TMP);
  return ok;
}

// The card's backup, parsed. False when there is none or it is damaged.
struct Backup {
  uint8_t blob[64];
  size_t blob_n = 0;
  char ssid[fnx::SSID_LEN] = "", pass[fnx::PASS_LEN] = "";
  fnx::WeatherSet place = {};
};

static bool read_backup(Backup& out) {
  FILE* f = fopen(PATH, "rb");
  if (!f) return false;
  uint8_t b[MAX];
  size_t n = fread(b, 1, sizeof b, f);
  fclose(f);
  if (n < 4 || memcmp(b, "FXB1", 4) != 0) return false;
  for (size_t at = 4; at + 3 <= n;) {
    uint8_t tag = b[at];
    size_t len = b[at + 1] | b[at + 2] << 8;
    const uint8_t* d = b + at + 3;
    if (at + 3 + len > n) return false;
    switch (tag) {
      case T_SETTINGS:
        out.blob_n = len < sizeof out.blob ? len : sizeof out.blob;
        memcpy(out.blob, d, out.blob_n);
        break;
      case T_SSID:
        snprintf(out.ssid, sizeof out.ssid, "%.*s", (int)len, (const char*)d);
        break;
      case T_PASS:
        snprintf(out.pass, sizeof out.pass, "%.*s", (int)len, (const char*)d);
        break;
      case T_PLACE:
        if (len >= 8) {
          memcpy(&out.place.lat_e4, d, 4);
          memcpy(&out.place.lon_e4, d + 4, 4);
          snprintf(out.place.name, sizeof out.place.name, "%.*s", (int)(len - 8), (const char*)d + 8);
        }
        break;
      default:
        break;  // a newer firmware's record
    }
    at += 3 + len;
  }
  return out.blob_n > 0;
}

void backup_mounted() {
  Backup b;
  char ssid[fnx::SSID_LEN];
  bool empty = settings_were_empty() && !radio_saved_network(ssid, sizeof ssid);
  if (empty && !s_answered && read_backup(b)) {
    strlcpy(s_offer_ssid, b.ssid, sizeof s_offer_ssid);
    s_offer = true;
    logs_note(3, "backup: settings are empty, offering the card's backup");
    return;
  }
  if (settings_were_empty()) settings_save();  // NVS gets a blob; also marks the backup changed
  else backup_changed();                       // this card gets the current settings
}

void backup_tick(int64_t now) {
  if (s_offer || !s_changed || now - s_changed < SETTLE_US) return;
  if (sd_info().state != fnx::sd_state::READY) return;
  portENTER_CRITICAL(&s_mux);
  s_changed = 0;
  portEXIT_CRITICAL(&s_mux);
  if (!write_now()) link_log(2, "backup: could not write %s", PATH);
}

bool backup_offer(char* ssid, size_t cap) {
  if (!s_offer) return false;
  if (ssid && cap) strlcpy(ssid, s_offer_ssid, cap);
  return true;
}

bool backup_restore() {
  Backup b;
  s_offer = false;
  s_answered = true;
  if (!read_backup(b) || !settings_restore(b.blob, b.blob_n)) {
    link_log(2, "backup: the card's backup could not be read");
    return false;
  }
  if (b.ssid[0]) radio_creds_restore(b.ssid, b.pass);
  if (b.place.name[0]) weather_set(b.place);
  logs_note(3, "backup: settings restored from the card, restarting");
  logs_flush_soon();
  vTaskDelay(pdMS_TO_TICKS(300));  // the link task writes the note before the restart
  esp_restart();
}

void backup_decline() {
  s_offer = false;
  s_answered = true;
  logs_note(3, "backup: not restored (the board starts fresh)");
  settings_save();  // not asked again; the card's backup becomes this board's
}

}  // namespace fx::sys
