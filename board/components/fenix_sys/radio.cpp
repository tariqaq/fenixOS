// Radio manager (P32): Wi-Fi or off. The mode (Settings > Wireless, or RADIO_SET from the PC)
// is saved and applied at boot; switching restarts, so Wi-Fi's memory is never half set up.
// Bluetooth was removed in P38 (docs/decisions.md): mode 1 is reserved, and a board that saved
// it boots as Wi-Fi with a saved network, else off.
//
// Wi-Fi: station at full speed (power save off), the network saved in NVS in plaintext
// (user choice: no encryption anywhere), reconnect with backoff, scans for the PC and the UI;
// its services (PC link over TCP, time) are in net.cpp.
//
// State changes come from the Wi-Fi tasks; the state is copied under a spinlock and sent to the
// PC as RADIO_STATE. Nothing here calls LVGL.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace fx::sys {

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static fnx::RadioState s_st;  // under s_mux

static void report(const fnx::RadioState& s) {
  if (!link_connected()) return;
  uint8_t b[64];
  fnx::Writer w(b, sizeof b);
  fnx::write_radio_state(w, s);
  link_send(fnx::ty::RADIO_STATE, b, w.size());
}

static void set_state(uint8_t state, const char* name = nullptr, int8_t rssi = 0, const uint8_t* ip = nullptr) {
  fnx::RadioState c;
  portENTER_CRITICAL(&s_mux);
  s_st.state = state;
  s_st.rssi = rssi;
  if (ip) memcpy(s_st.ip, ip, 4);
  else memset(s_st.ip, 0, 4);
  if (name) strlcpy(s_st.name, name, sizeof s_st.name);
  c = s_st;
  portEXIT_CRITICAL(&s_mux);
  report(c);
}

RadioInfo radio_info() {
  RadioInfo r;
  portENTER_CRITICAL(&s_mux);
  r.mode = s_st.mode;
  r.state = s_st.state;
  r.rssi = s_st.rssi;
  memcpy(r.ip, s_st.ip, 4);
  strlcpy(r.name, s_st.name, sizeof r.name);
  portEXIT_CRITICAL(&s_mux);
  return r;
}

void radio_report() {
  portENTER_CRITICAL(&s_mux);
  fnx::RadioState c = s_st;
  portEXIT_CRITICAL(&s_mux);
  report(c);
}

void radio_switch(uint8_t mode) {
  if ((mode != fnx::radio::OFF && mode != fnx::radio::WIFI) || mode == settings().radio) return;
  settings().radio = mode;
  settings_save();
  link_log(3, "radio: switching %s, restarting", mode == fnx::radio::WIFI ? "to Wi-Fi" : "off");
  vTaskDelay(pdMS_TO_TICKS(150));  // let the log leave
  esp_restart();
}

// ---- Saved network (NVS "fenix": wifi_ssid, wifi_pass) ----

static char s_ssid[fnx::SSID_LEN], s_pass[fnx::PASS_LEN];

static void creds_load() {
  nvs_handle_t h;
  if (nvs_open("fenix", NVS_READONLY, &h) != ESP_OK) return;
  size_t n = sizeof s_ssid;
  if (nvs_get_str(h, "wifi_ssid", s_ssid, &n) != ESP_OK) s_ssid[0] = 0;
  n = sizeof s_pass;
  if (nvs_get_str(h, "wifi_pass", s_pass, &n) != ESP_OK) s_pass[0] = 0;
  nvs_close(h);
}

static void creds_save() {
  nvs_handle_t h;
  if (nvs_open("fenix", NVS_READWRITE, &h) != ESP_OK) return;
  if (s_ssid[0]) {
    nvs_set_str(h, "wifi_ssid", s_ssid);
    nvs_set_str(h, "wifi_pass", s_pass);
  } else {
    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
  }
  nvs_commit(h);
  nvs_close(h);
}

bool radio_saved_network(char* out, size_t cap) {
  strlcpy(out, s_ssid, cap);
  return s_ssid[0] != 0;
}

// ---- Wi-Fi ----

static constexpr int MAX_APS = 24;  // one WIFI_SCAN frame
static esp_timer_handle_t s_retry;
static int s_fails;
static bool s_scan_for_pc;
static volatile bool s_scanning;
static fnx::WifiAp* s_aps;  // last scan, strongest first; under s_mux
static int s_naps;

static void wifi_connect() {
  if (!s_ssid[0]) return set_state(fnx::radio_state::READY, "");
  wifi_config_t c = {};
  strlcpy((char*)c.sta.ssid, s_ssid, sizeof c.sta.ssid);
  strlcpy((char*)c.sta.password, s_pass, sizeof c.sta.password);
  c.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;  // join old networks too
  c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  esp_wifi_set_config(WIFI_IF_STA, &c);
  set_state(fnx::radio_state::CONNECTING, s_ssid);
  esp_wifi_connect();
}

static void retry_cb(void*) { wifi_connect(); }

static uint8_t auth_code(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:
    case WIFI_AUTH_OWE:
      return fnx::wifi_auth::OPEN;
    case WIFI_AUTH_WEP:
      return fnx::wifi_auth::WEP;
    case WIFI_AUTH_WPA_PSK:
      return fnx::wifi_auth::WPA;
    case WIFI_AUTH_WPA2_PSK:
      return fnx::wifi_auth::WPA2;
    case WIFI_AUTH_WPA_WPA2_PSK:
      return fnx::wifi_auth::WPA_WPA2;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
      return fnx::wifi_auth::WPA3;
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return fnx::wifi_auth::WPA2_WPA3;
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA_ENTERPRISE:
      return fnx::wifi_auth::ENTERPRISE;
    default:
      return fnx::wifi_auth::OTHER;
  }
}

static void send_scan(uint8_t status) {
  static uint8_t b[fnx::MAX_PAYLOAD];  // link task or Wi-Fi event task, never both (s_scanning)
  fnx::Writer w(b, sizeof b);
  w.u8(status);
  portENTER_CRITICAL(&s_mux);
  int n = status == fnx::scan_status::OK ? s_naps : 0;
  w.u8(n);
  for (int i = 0; i < n; i++) fnx::write_wifi_ap(w, s_aps[i]);
  portEXIT_CRITICAL(&s_mux);
  link_send(fnx::ty::WIFI_SCAN, b, w.size());
}

static void scan_done() {
  uint16_t n = 0;
  esp_wifi_scan_get_ap_num(&n);
  if (n > 40) n = 40;
  wifi_ap_record_t* r = n ? (wifi_ap_record_t*)malloc(n * sizeof *r) : nullptr;
  if (!r || esp_wifi_scan_get_ap_records(&n, r) != ESP_OK) n = 0;
  if (!r) esp_wifi_clear_ap_list();
  std::sort(r, r + n, [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) { return a.rssi > b.rssi; });
  fnx::WifiAp* out = (fnx::WifiAp*)malloc(MAX_APS * sizeof(fnx::WifiAp));
  int k = 0;
  for (int i = 0; out && i < n && k < MAX_APS; i++) {
    const char* ssid = (const char*)r[i].ssid;
    if (!ssid[0]) continue;  // hidden
    bool dup = false;
    for (int j = 0; j < k && !dup; j++) dup = strcmp(out[j].ssid, ssid) == 0;
    if (dup) continue;
    fnx::WifiAp& a = out[k++];
    strlcpy(a.ssid, ssid, sizeof a.ssid);
    a.rssi = r[i].rssi;
    a.channel = r[i].primary;
    a.auth = auth_code(r[i].authmode);
    memcpy(a.bssid, r[i].bssid, 6);
  }
  free(r);
  portENTER_CRITICAL(&s_mux);
  fnx::WifiAp* old = s_aps;
  s_aps = out;
  s_naps = out ? k : 0;
  portEXIT_CRITICAL(&s_mux);
  free(old);
  bool pc = s_scan_for_pc;
  s_scan_for_pc = false;
  s_scanning = false;
  if (pc) send_scan(out ? fnx::scan_status::OK : fnx::scan_status::FAILED);
  // The scan stopped any retry: reconnect if not connected.
  if (radio_info().state != fnx::radio_state::CONNECTED) wifi_connect();
}

static void wifi_event(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_scanning) return;  // radio_scan dropped the attempt; scan_done reconnects
    if (!s_ssid[0]) return set_state(fnx::radio_state::READY, "");
    // Back off 1, 2, 4 ... 30 s; after 5 tries say "failed" but keep trying.
    s_fails++;
    set_state(s_fails >= 5 ? fnx::radio_state::FAILED : fnx::radio_state::CONNECTING, s_ssid);
    int64_t ms = std::min(30000, 1000 << std::min(s_fails - 1, 5));
    esp_timer_stop(s_retry);
    esp_timer_start_once(s_retry, ms * 1000);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    s_fails = 0;
    auto* ev = (ip_event_got_ip_t*)data;
    uint8_t ip[4];
    memcpy(ip, &ev->ip_info.ip.addr, 4);  // network order: a.b.c.d
    wifi_ap_record_t ap;
    int8_t rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    set_state(fnx::radio_state::CONNECTED, s_ssid, rssi, ip);
    net_up();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    scan_done();
  }
}

static bool wifi_start() {
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_t* nif = esp_netif_create_default_wifi_sta();
  esp_netif_set_hostname(nif, "fenixos");
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) return false;
  esp_wifi_set_storage(WIFI_STORAGE_RAM);  // our own NVS keys hold the network
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr);
  esp_timer_create_args_t t = {};
  t.callback = retry_cb;
  t.name = "wifi_retry";
  esp_timer_create(&t, &s_retry);
  esp_wifi_set_mode(WIFI_MODE_STA);
  if (esp_wifi_start() != ESP_OK) return false;
  esp_wifi_set_ps(WIFI_PS_NONE);  // lowest latency (P30: best latency and speed)
  net_start();
  return true;
}

bool radio_scan(bool for_pc) {
  RadioInfo r = radio_info();
  if (r.mode != fnx::radio::WIFI) {
    if (for_pc) send_scan(fnx::scan_status::NOT_WIFI);
    return false;
  }
  if (for_pc) s_scan_for_pc = true;
  if (s_scanning) return true;
  s_scanning = true;
  // A connect attempt in progress refuses scans: drop it; scan_done reconnects.
  if (r.state != fnx::radio_state::CONNECTED) {
    esp_timer_stop(s_retry);
    esp_wifi_disconnect();
  }
  wifi_scan_config_t sc = {};
  if (esp_wifi_scan_start(&sc, false) != ESP_OK) {
    bool pc = s_scan_for_pc;
    s_scan_for_pc = false;
    s_scanning = false;
    if (pc) send_scan(fnx::scan_status::FAILED);
    if (r.state != fnx::radio_state::CONNECTED) wifi_connect();
    return false;
  }
  return true;
}

bool radio_scanning() { return s_scanning; }

int radio_scan_results(fnx::WifiAp* out, int max) {
  portENTER_CRITICAL(&s_mux);
  int n = std::min(s_naps, max);
  if (n > 0) memcpy(out, s_aps, n * sizeof *out);
  portEXIT_CRITICAL(&s_mux);
  return n;
}

void radio_join(const char* ssid, const char* pass) {
  strlcpy(s_ssid, ssid, sizeof s_ssid);
  strlcpy(s_pass, pass, sizeof s_pass);
  creds_save();
  if (s_ssid[0]) link_log(3, "radio: network \"%s\" saved", s_ssid);
  else link_log(3, "radio: network forgotten");
  if (radio_info().mode != fnx::radio::WIFI) return;
  s_fails = 0;
  esp_timer_stop(s_retry);
  esp_wifi_disconnect();
  wifi_connect();
}

void radio_reconnect() {
  if (radio_info().mode != fnx::radio::WIFI || !s_ssid[0]) return;
  s_fails = 0;
  esp_timer_stop(s_retry);
  esp_wifi_disconnect();
  wifi_connect();
}

// ---- Boot ----

void radio_start() {
  creds_load();
  uint8_t mode = settings().radio;
  if (mode != fnx::radio::OFF && mode != fnx::radio::WIFI) {
    // Bluetooth (removed in P38) or an unknown value: Wi-Fi if a network is saved, else off.
    mode = s_ssid[0] ? fnx::radio::WIFI : fnx::radio::OFF;
    settings().radio = mode;
    settings_save();
  }
  portENTER_CRITICAL(&s_mux);
  s_st.mode = mode;
  s_st.state = mode == fnx::radio::OFF ? fnx::radio_state::OFF : fnx::radio_state::STARTING;
  portEXIT_CRITICAL(&s_mux);
  if (mode == fnx::radio::WIFI && !wifi_start()) set_state(fnx::radio_state::FAILED, "");
}

}  // namespace fx::sys
