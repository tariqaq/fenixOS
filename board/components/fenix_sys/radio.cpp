// Radio manager (P32): one radio at a time. The mode (Settings > Wireless, or RADIO_SET from
// the PC) is saved and applied at boot; switching restarts, because Bluetooth memory handed
// back to the heap (esp_bt_mem_release) returns only on reboot. Off and Wi-Fi release it first:
// +44 KB (docs/board-info.md "Radios one at a time").
//
// Bluetooth: Classic SPP server "fenixOS" (Just Works pairing), the PC link over it (P33), and
// BLE advertising.
// Wi-Fi: station at full speed (power save off), the network saved in NVS in plaintext
// (user choice: no encryption anywhere), reconnect with backoff, scans for the PC and the UI;
// its services (PC link over TCP, time) are in net.cpp.
//
// State changes come from the Bluetooth and Wi-Fi tasks; the state is copied under a spinlock
// and sent to the PC as RADIO_STATE. Nothing here calls LVGL.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_event.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_netif.h"
#include "esp_spp_api.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
  if (mode > fnx::radio::WIFI || mode == settings().radio) return;
  settings().radio = mode;
  settings_save();
  link_log(3, "radio: switching to %s, restarting",
           mode == fnx::radio::WIFI ? "Wi-Fi" : mode == fnx::radio::BLUETOOTH ? "Bluetooth" : "off");
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

// ---- Bluetooth ----

static void bda_text(const uint8_t* a, char* out, size_t cap) {
  snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

static void gap_bt_cb(esp_bt_gap_cb_event_t e, esp_bt_gap_cb_param_t* p) {
  if (e == ESP_BT_GAP_CFM_REQ_EVT) esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, true);
  if (e == ESP_BT_GAP_PIN_REQ_EVT) {  // legacy pairing (old hosts): fixed PIN
    esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
    esp_bt_gap_pin_reply(p->pin_req.bda, true, 4, pin);
  }
}

// PC link over SPP. Bluedroid copies and queues every write without a limit, so at most
// SPP_PENDING writes are in flight and none while RFCOMM reports congestion.
static constexpr int SPP_PENDING = 6;
static volatile uint32_t s_spp;  // open connection, 0 = none
static volatile int s_spp_pending;
static volatile bool s_spp_cong;
static SemaphoreHandle_t s_spp_room;  // given on every write completion or congestion change

static bool spp_write(const uint8_t* d, size_t n) {
  int64_t end = esp_timer_get_time() + 500000;
  while (s_spp && (s_spp_cong || s_spp_pending >= SPP_PENDING)) {
    xSemaphoreTake(s_spp_room, pdMS_TO_TICKS(20));
    if (esp_timer_get_time() > end) return false;
  }
  uint32_t h = s_spp;
  if (!h) return false;
  portENTER_CRITICAL(&s_mux);
  s_spp_pending = s_spp_pending + 1;
  portEXIT_CRITICAL(&s_mux);
  if (esp_spp_write(h, n, (uint8_t*)d) == ESP_OK) return true;
  portENTER_CRITICAL(&s_mux);
  s_spp_pending = s_spp_pending - 1;
  portEXIT_CRITICAL(&s_mux);
  return false;
}

static size_t spp_tx_free() {
  if (!s_spp || s_spp_cong) return 0;
  return (SPP_PENDING - s_spp_pending) * fnx::MAX_WIRE;
}

static const RemoteLink SPP_LINK = {"Bluetooth", spp_write, spp_tx_free};

static void spp_room() {
  if (s_spp_room) xSemaphoreGive(s_spp_room);
}

static void spp_cb(esp_spp_cb_event_t e, esp_spp_cb_param_t* p) {
  switch (e) {
    case ESP_SPP_INIT_EVT:
      esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_SLAVE, 0, "fenixOS");
      break;
    case ESP_SPP_START_EVT:
      set_state(p->start.status == ESP_SPP_SUCCESS ? fnx::radio_state::READY : fnx::radio_state::FAILED, "");
      break;
    case ESP_SPP_SRV_OPEN_EVT: {
      char a[18];
      bda_text(p->srv_open.rem_bda, a, sizeof a);
      s_spp_pending = 0;
      s_spp_cong = false;
      s_spp = p->srv_open.handle;
      set_state(fnx::radio_state::CONNECTED, a);
      break;
    }
    case ESP_SPP_CLOSE_EVT:
      s_spp = 0;
      spp_room();
      link_remote_closed();
      set_state(fnx::radio_state::READY, "");
      break;
    case ESP_SPP_DATA_IND_EVT:
      link_remote_rx(p->data_ind.data, p->data_ind.len);
      break;
    case ESP_SPP_WRITE_EVT:
      portENTER_CRITICAL(&s_mux);
      if (s_spp_pending > 0) s_spp_pending = s_spp_pending - 1;
      portEXIT_CRITICAL(&s_mux);
      s_spp_cong = p->write.cong;
      spp_room();
      break;
    case ESP_SPP_CONG_EVT:
      s_spp_cong = p->cong.cong;
      spp_room();
      break;
    default:
      break;
  }
}

static esp_ble_adv_params_t s_adv = {
    .adv_int_min = 0x100,
    .adv_int_max = 0x200,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_ble_cb(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t*) {
  if (e == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT) esp_ble_gap_start_advertising(&s_adv);
}

static bool bt_start() {
  esp_bt_controller_config_t c = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if (esp_bt_controller_init(&c) != ESP_OK || esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) return false;
  if (esp_bluedroid_init() != ESP_OK || esp_bluedroid_enable() != ESP_OK) return false;
  esp_bt_gap_set_device_name("fenixOS");
  esp_bt_gap_register_callback(gap_bt_cb);
  esp_bt_io_cap_t io = ESP_BT_IO_CAP_NONE;  // Just Works: the board has no keypad
  esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof io);
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  s_spp_room = xSemaphoreCreateBinary();
  link_remote_start(&SPP_LINK, 4096);  // ~48 KB free in Bluetooth mode: half the UART's buffer
  esp_spp_register_callback(spp_cb);
  esp_spp_cfg_t sc = {.mode = ESP_SPP_MODE_CB, .enable_l2cap_ertm = true, .tx_buffer_size = 0};
  if (esp_spp_enhanced_init(&sc) != ESP_OK) return false;
  esp_ble_gap_register_callback(gap_ble_cb);
  static uint8_t adv[] = {0x02, 0x01, 0x06, 0x08, 0x09, 'f', 'e', 'n', 'i', 'x', 'O', 'S'};
  esp_ble_gap_config_adv_data_raw(adv, sizeof adv);
  return true;
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
  uint8_t mode = settings().radio <= fnx::radio::WIFI ? settings().radio : fnx::radio::OFF;
  portENTER_CRITICAL(&s_mux);
  s_st.mode = mode;
  s_st.state = mode == fnx::radio::OFF ? fnx::radio_state::OFF : fnx::radio_state::STARTING;
  portEXIT_CRITICAL(&s_mux);
  if (mode != fnx::radio::BLUETOOTH) esp_bt_mem_release(ESP_BT_MODE_BTDM);
  bool ok = mode == fnx::radio::BLUETOOTH ? bt_start() : mode == fnx::radio::WIFI ? wifi_start() : true;
  if (!ok) set_state(fnx::radio_state::FAILED, "");
}

}  // namespace fx::sys
