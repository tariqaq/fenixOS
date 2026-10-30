// Radio probes, built only with Bluetooth on (never in the release build). P29, NimBLE
// (sdkconfig.radio): Wi-Fi station started without a network, plus BLE advertising as "fenixOS".
// P30, Bluedroid (sdkconfig.bluedroid): see below. Each logs the heap before and after each step
// to the PC log; the Task manager then shows the radios' tasks and the heap left.
#include "sdkconfig.h"
#if CONFIG_BT_NIMBLE_ENABLED
#include <string.h>

#include "esp_bt.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "fenix_sys.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

namespace fx::sys {

static uint32_t s_free0;

static void heap_note(const char* step) {
  uint32_t f = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  link_log(3, "radio probe: %s: heap free %lu (%+ld), largest block %u", step, (unsigned long)f,
           (long)f - (long)s_free0, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static int gap_event(struct ble_gap_event*, void*) { return 0; }

static void advertise() {
  ble_hs_adv_fields f{};
  f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  const char* name = ble_svc_gap_device_name();
  f.name = (const uint8_t*)name;
  f.name_len = strlen(name);
  f.name_is_complete = 1;
  ble_gap_adv_set_fields(&f);
  ble_gap_adv_params p{};
  p.conn_mode = BLE_GAP_CONN_MODE_NON;
  p.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &p, gap_event, nullptr);
  heap_note("BLE advertising");
}

static void host_task(void*) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void radio_probe() {
  s_free0 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  heap_note("start");
  // BLE only: hand the Classic Bluetooth controller's reserved RAM back to the heap first.
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  heap_note("Classic BT memory released");
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  heap_note("Wi-Fi started");
  nimble_port_init();
  ble_svc_gap_device_name_set("fenixOS");
  ble_hs_cfg.sync_cb = advertise;
  nimble_port_freertos_init(host_task);
  heap_note("BLE host started");
}

}  // namespace fx::sys

#elif CONFIG_BT_BLUEDROID_ENABLED
// Bluedroid probe (P30): one radio per boot, as the planned restart-to-switch works. Boots
// alternate (`fenix reboot` for the next): Bluetooth (Classic SPP server + BLE advertising), then
// Wi-Fi alone with all Bluetooth memory handed back to the heap.
#include <string.h>

#include "esp_attr.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_event.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_spp_api.h"
#include "esp_wifi.h"
#include "fenix_sys.h"

namespace fx::sys {

static uint32_t s_free0;
RTC_NOINIT_ATTR static uint32_t s_next;  // survives esp_restart: which radio this boot probes
static constexpr uint32_t NEXT_WIFI = 0x57494649;

static void heap_note(const char* step) {
  uint32_t f = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  link_log(3, "radio probe: %s: heap free %lu (%+ld), largest block %u", step, (unsigned long)f,
           (long)f - (long)s_free0, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
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

static void ble_cb(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t*) {
  if (e == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT) esp_ble_gap_start_advertising(&s_adv);
  if (e == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) heap_note("BLE advertising");
}

static void spp_cb(esp_spp_cb_event_t e, esp_spp_cb_param_t*) {
  if (e == ESP_SPP_INIT_EVT) esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_SLAVE, 0, "fenixOS");
  if (e == ESP_SPP_START_EVT) heap_note("SPP server up");
}

static void probe_bluetooth() {
  esp_bt_controller_config_t c = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_bt_controller_init(&c);
  esp_bt_controller_enable(ESP_BT_MODE_BTDM);
  heap_note("controller (Classic + BLE)");
  esp_bluedroid_init();
  esp_bluedroid_enable();
  heap_note("Bluedroid");
  esp_bt_gap_set_device_name("fenixOS");
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  esp_spp_register_callback(spp_cb);
  esp_spp_cfg_t sc = {.mode = ESP_SPP_MODE_CB, .enable_l2cap_ertm = true, .tx_buffer_size = 0};
  esp_spp_enhanced_init(&sc);
  esp_ble_gap_register_callback(ble_cb);
  static uint8_t adv[] = {0x02, 0x01, 0x06, 0x08, 0x09, 'f', 'e', 'n', 'i', 'x', 'O', 'S'};
  esp_ble_gap_config_adv_data_raw(adv, sizeof adv);
}

static void probe_wifi() {
  esp_bt_mem_release(ESP_BT_MODE_BTDM);
  heap_note("all Bluetooth memory released");
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  heap_note("Wi-Fi started (full-speed settings)");
}

void radio_probe() {
  s_free0 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  bool wifi = s_next == NEXT_WIFI;
  s_next = wifi ? 0 : NEXT_WIFI;
  heap_note(wifi ? "start, Wi-Fi boot" : "start, Bluetooth boot");
  wifi ? probe_wifi() : probe_bluetooth();
}

}  // namespace fx::sys
#endif
