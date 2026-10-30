// Radio probe (P29, sdkconfig.radio only): Wi-Fi station started without a network, plus BLE
// advertising as "fenixOS" (NimBLE). Logs the heap before and after each step to the PC log;
// the Task manager then shows the radios' tasks and the heap left. The release build has
// Bluetooth off, so none of this is compiled in.
#include "sdkconfig.h"
#if CONFIG_BT_ENABLED
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
  link_log(3, "radio probe: %s: heap free %lu (-%lu), largest block %u", step, (unsigned long)f,
           (unsigned long)(s_free0 - f), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
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
#endif
