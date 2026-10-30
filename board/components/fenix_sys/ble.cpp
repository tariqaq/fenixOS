// BLE status service (P33), Bluetooth mode only. One service, one characteristic: the board's
// state as JSON (status_json, the same as the phone page's /status.json without the Wi-Fi
// fields), readable and notified every 2 s while a client subscribes. Status only, like the
// phone page: anyone in range can connect without pairing, so it changes nothing on the board.
//
// Bluedroid calls back on BTC_TASK, which has under 1 KB of stack to spare (docs/board-info.md),
// so the JSON is built on the esp_timer task into s_json and the callbacks only copy it.
#include <string.h>

#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_timer.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"

namespace fx::sys {

// 128-bit UUIDs, little-endian as Bluedroid wants them: service f3e10001-..., status f3e10002-...
// (text form: f3e1000x-6a9c-4b3e-9f0a-c5f3e1a0b0c1).
#define FENIX_UUID(x) {0xc1, 0xb0, 0xa0, 0xe1, 0xf3, 0xc5, 0x0a, 0x9f, 0x3e, 0x4b, 0x9c, 0x6a, x, 0x00, 0xe1, 0xf3}
static const uint8_t SVC_UUID[16] = FENIX_UUID(0x01);
static const uint8_t STATUS_UUID[16] = FENIX_UUID(0x02);

static constexpr uint16_t NONE = 0xFFFF;
static esp_gatt_if_t s_if = ESP_GATT_IF_NONE;
static uint16_t s_conn = NONE, s_status = 0, s_cccd = 0, s_mtu = 23;
static volatile bool s_notify;
static esp_timer_handle_t s_tick;

static portMUX_TYPE s_json_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_json[512];  // under s_json_mux
static size_t s_json_n;

static esp_ble_adv_params_t s_adv = {
    .adv_int_min = 0x100,  // 160 to 320 ms
    .adv_int_max = 0x200,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// esp_timer task, every 2 s from ble_start: rebuild the JSON, then notify a subscribed client.
static void tick(void*) {
  static char b[sizeof s_json];
  size_t n = status_json(b, sizeof b, false);
  portENTER_CRITICAL(&s_json_mux);
  memcpy(s_json, b, n);
  s_json_n = n;
  portEXIT_CRITICAL(&s_json_mux);
  uint16_t conn = s_conn;
  if (conn == NONE || !s_notify || !n) return;
  size_t room = s_mtu - 3;  // a notification carries MTU - 3 bytes; the JSON is ~250
  esp_ble_gatts_send_indicate(s_if, conn, s_status, n < room ? n : room, (uint8_t*)b, false);
}

static void gap_cb(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t*) {
  if (e == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT) esp_ble_gap_start_advertising(&s_adv);
}

static void read_rsp(esp_gatt_if_t gif, const esp_ble_gatts_cb_param_t::gatts_read_evt_param& r) {
  esp_gatt_rsp_t rsp = {};
  rsp.attr_value.handle = r.handle;
  if (r.handle == s_cccd) {
    rsp.attr_value.len = 2;
    rsp.attr_value.value[0] = s_notify ? 1 : 0;
  } else {
    // Long reads come in pieces at increasing offsets (read blob).
    portENTER_CRITICAL(&s_json_mux);
    size_t n = s_json_n > r.offset ? s_json_n - r.offset : 0;
    if (n > (size_t)(s_mtu - 1)) n = s_mtu - 1;
    memcpy(rsp.attr_value.value, s_json + r.offset, n);
    portEXIT_CRITICAL(&s_json_mux);
    rsp.attr_value.len = n;
    rsp.attr_value.offset = r.offset;
  }
  esp_ble_gatts_send_response(gif, r.conn_id, r.trans_id, ESP_GATT_OK, &rsp);
}

static void gatts_cb(esp_gatts_cb_event_t e, esp_gatt_if_t gif, esp_ble_gatts_cb_param_t* p) {
  switch (e) {
    case ESP_GATTS_REG_EVT: {
      s_if = gif;
      esp_gatt_srvc_id_t id = {};
      id.is_primary = true;
      id.id.uuid.len = ESP_UUID_LEN_128;
      memcpy(id.id.uuid.uuid.uuid128, SVC_UUID, 16);
      esp_ble_gatts_create_service(gif, &id, 4);  // service, characteristic + value, CCCD
      break;
    }
    case ESP_GATTS_CREATE_EVT: {
      esp_ble_gatts_start_service(p->create.service_handle);
      esp_bt_uuid_t u = {};
      u.len = ESP_UUID_LEN_128;
      memcpy(u.uuid.uuid128, STATUS_UUID, 16);
      esp_ble_gatts_add_char(p->create.service_handle, &u, ESP_GATT_PERM_READ,
                             ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, nullptr, nullptr);
      break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
      s_status = p->add_char.attr_handle;
      esp_bt_uuid_t u = {};
      u.len = ESP_UUID_LEN_16;
      u.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
      esp_ble_gatts_add_char_descr(p->add_char.service_handle, &u, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, nullptr,
                                   nullptr);
      break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
      s_cccd = p->add_char_descr.attr_handle;
      break;
    case ESP_GATTS_CONNECT_EVT:
      s_conn = p->connect.conn_id;
      s_mtu = 23;
      break;
    case ESP_GATTS_DISCONNECT_EVT:
      s_conn = NONE;
      s_notify = false;
      esp_ble_gap_start_advertising(&s_adv);
      break;
    case ESP_GATTS_MTU_EVT:
      s_mtu = p->mtu.mtu;
      break;
    case ESP_GATTS_READ_EVT:
      if (p->read.need_rsp) read_rsp(gif, p->read);
      break;
    case ESP_GATTS_WRITE_EVT:
      if (p->write.handle == s_cccd && p->write.len >= 1) s_notify = p->write.value[0] & 1;
      if (p->write.need_rsp)
        esp_ble_gatts_send_response(gif, p->write.conn_id, p->write.trans_id,
                                    p->write.handle == s_cccd ? ESP_GATT_OK : ESP_GATT_WRITE_NOT_PERMIT, nullptr);
      break;
    default:
      break;
  }
}

void ble_start() {
  esp_timer_create_args_t t = {};
  t.callback = tick;
  t.name = "ble_status";
  esp_timer_create(&t, &s_tick);
  tick(nullptr);  // a value before the first client
  esp_timer_start_periodic(s_tick, 2000000);
  esp_ble_gap_register_callback(gap_cb);
  esp_ble_gatts_register_callback(gatts_cb);
  esp_ble_gatt_set_local_mtu(517);  // the whole JSON in one notification
  esp_ble_gatts_app_register(0);
  // Flags, the name and the service UUID: 3 + 9 + 18 = 30 of 31 bytes.
  static uint8_t adv[31] = {0x02, 0x01, 0x06, 0x08, 0x09, 'f', 'e', 'n', 'i', 'x', 'O', 'S', 0x11, 0x07};
  memcpy(adv + 14, SVC_UUID, 16);
  esp_ble_gap_config_adv_data_raw(adv, 30);
}

}  // namespace fx::sys
