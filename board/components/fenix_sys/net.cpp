// Wi-Fi services (P34), started by radio.cpp in Wi-Fi mode: the PC link over TCP (one client,
// the same frames as on USB, no TLS by user choice), the name fenixos.local (mDNS) and the time
// by SNTP.
#include <string.h>

#include "esp_netif_sntp.h"
#include "fenix_hal.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"

namespace fx::sys {

// ---- PC link over TCP ----

static volatile int s_client = -1;
static SemaphoreHandle_t s_sock_lock;  // a send never runs on a socket being closed

// Whole frame or none: a send that stalls (500 ms) or fails ends the connection, so the
// PC never sees half a frame followed by the next.
static bool tcp_write(const uint8_t* d, size_t n) {
  xSemaphoreTake(s_sock_lock, portMAX_DELAY);
  int c = s_client;
  bool ok = c >= 0;
  for (size_t off = 0; ok && off < n;) {
    int k = send(c, d + off, n - off, 0);
    if (k <= 0) {
      shutdown(c, SHUT_RDWR);  // the reader sees the close and cleans up
      ok = false;
    } else {
      off += k;
    }
  }
  xSemaphoreGive(s_sock_lock);
  return ok;
}

// lwIP does not report free send-buffer space; Wi-Fi drains faster than the stream fills it.
static size_t tcp_tx_free() { return s_client >= 0 ? 8192 : 0; }

static const RemoteLink TCP_LINK = {"Wi-Fi", tcp_write, tcp_tx_free};

static void tcp_task(void*) {
  int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(fnx::TCP_PORT);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 1) != 0) {
    link_log(1, "net: TCP port %u unavailable", fnx::TCP_PORT);
    vTaskDelete(nullptr);
  }
  static uint8_t buf[1024];
  for (;;) {
    int c = accept(ls, nullptr, nullptr);
    if (c < 0) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    setsockopt(c, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
    timeval tv = {0, 500000};
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    s_client = c;
    for (;;) {
      int n = recv(c, buf, sizeof buf, 0);
      if (n <= 0) break;
      link_remote_rx(buf, n);
    }
    xSemaphoreTake(s_sock_lock, portMAX_DELAY);
    s_client = -1;
    close(c);
    xSemaphoreGive(s_sock_lock);
    link_remote_closed();
  }
}

// ---- Time ----

// Runs on the lwIP task (3 KB stack).
static void sntp_synced(struct timeval*) { hal::time_mark_valid(); }

// ---- Start ----

void net_start() {
  s_sock_lock = xSemaphoreCreateMutex();
  link_remote_start(&TCP_LINK, 8192);  // as the UART: 4 install chunks in flight
  xTaskCreatePinnedToCore(tcp_task, "tcp_link", 3072, nullptr, 5, nullptr, 0);
  if (mdns_init() == ESP_OK) {
    mdns_hostname_set("fenixos");
    mdns_instance_name_set("fenixOS");
    mdns_service_add(nullptr, "_fenixos", "_tcp", fnx::TCP_PORT, nullptr, 0);
  }
  // Two servers: one pool address sometimes never answered (P34); lwIP then tries the next.
  esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com"));
  c.start = false;  // net_up starts it: before an address its DNS query fails and backs off
  c.sync_cb = sntp_synced;
  esp_netif_sntp_init(&c);
}

// Each new address (radio.cpp, GOT_IP): ask for the time now.
void net_up() {
  if (esp_netif_sntp_start() != ESP_OK) link_log(2, "net: SNTP did not start");
}

}  // namespace fx::sys
