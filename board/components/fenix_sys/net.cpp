// Wi-Fi services (P34), started by radio.cpp in Wi-Fi mode: the PC link over TCP (one client,
// the same frames as on USB, no TLS by user choice), the name fenixos.local (mDNS), the time
// by SNTP and the weather from Open-Meteo over plain HTTP (no key, no TLS).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_netif_sntp.h"
#include "fenix_hal.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"

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

// ---- Weather ----

static constexpr int64_t WX_PERIOD_MS = 15 * 60 * 1000;
static constexpr int64_t WX_RETRY_MS = 60 * 1000;
static portMUX_TYPE s_wx_mux = portMUX_INITIALIZER_UNLOCKED;
static fnx::WeatherSet s_place;  // under s_wx_mux; name empty = off
static Weather s_wx;             // under s_wx_mux
static TaskHandle_t s_wx_task;

static void place_load() {
  nvs_handle_t h;
  if (nvs_open("fenix", NVS_READONLY, &h) != ESP_OK) return;
  fnx::WeatherSet p = {};
  size_t n = sizeof p.name;
  if (nvs_get_i32(h, "wx_lat", &p.lat_e4) == ESP_OK && nvs_get_i32(h, "wx_lon", &p.lon_e4) == ESP_OK &&
      nvs_get_str(h, "wx_name", p.name, &n) == ESP_OK) {
    portENTER_CRITICAL(&s_wx_mux);
    s_place = p;
    portEXIT_CRITICAL(&s_wx_mux);
  }
  nvs_close(h);
}

void weather_set(const fnx::WeatherSet& p) {
  nvs_handle_t h;
  if (nvs_open("fenix", NVS_READWRITE, &h) == ESP_OK) {
    if (p.name[0]) {
      nvs_set_i32(h, "wx_lat", p.lat_e4);
      nvs_set_i32(h, "wx_lon", p.lon_e4);
      nvs_set_str(h, "wx_name", p.name);
    } else {
      nvs_erase_key(h, "wx_lat");
      nvs_erase_key(h, "wx_lon");
      nvs_erase_key(h, "wx_name");
    }
    nvs_commit(h);
    nvs_close(h);
  }
  portENTER_CRITICAL(&s_wx_mux);
  s_place = p;
  s_wx = {};
  portEXIT_CRITICAL(&s_wx_mux);
  if (s_wx_task) xTaskNotifyGive(s_wx_task);
}

bool weather(Weather* out) {
  portENTER_CRITICAL(&s_wx_mux);
  *out = s_wx;
  portEXIT_CRITICAL(&s_wx_mux);
  return out->valid;
}

// WMO codes (Open-Meteo docs), one short word for the Home date line.
const char* weather_text(uint8_t c) {
  if (c == 0) return "Clear";
  if (c <= 2) return "Partly cloudy";
  if (c == 3) return "Cloudy";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return "Rain";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "Snow";
  if (c >= 95) return "Storm";
  return "";
}

// Degrees x 10000 as text without float printf (newlib nano).
static void deg(char* out, size_t cap, int32_t e4) {
  uint32_t a = e4 < 0 ? -e4 : e4;
  snprintf(out, cap, "%s%lu.%04lu", e4 < 0 ? "-" : "", (unsigned long)(a / 10000), (unsigned long)(a % 10000));
}

// HTTP/1.0, so the answer is never chunked. True with the reading in *w.
static bool wx_fetch(const fnx::WeatherSet& p, Weather* w) {
  static char buf[2048];  // weather task only
  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* ai = nullptr;
  if (getaddrinfo("api.open-meteo.com", "80", &hints, &ai) != 0 || !ai) return false;
  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  timeval tv = {5, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  bool ok = connect(s, ai->ai_addr, ai->ai_addrlen) == 0;
  freeaddrinfo(ai);
  size_t len = 0;
  if (ok) {
    char lat[16], lon[16];
    deg(lat, sizeof lat, p.lat_e4);
    deg(lon, sizeof lon, p.lon_e4);
    int n = snprintf(buf, sizeof buf,
                     "GET /v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,weather_code HTTP/1.0\r\n"
                     "Host: api.open-meteo.com\r\nUser-Agent: fenixOS\r\nConnection: close\r\n\r\n",
                     lat, lon);
    ok = send(s, buf, n, 0) == n;
    while (ok && len < sizeof buf - 1) {
      int k = recv(s, buf + len, sizeof buf - 1 - len, 0);
      if (k <= 0) break;
      len += k;
    }
  }
  close(s);
  if (!ok || len == 0) return false;
  buf[len] = 0;
  if (strncmp(buf, "HTTP/1.", 7) != 0 || strncmp(buf + 9, "200", 3) != 0) return false;
  const char* cur = strstr(buf, "\"current\":{");
  if (!cur) return false;
  const char* t = strstr(cur, "\"temperature_2m\":");
  const char* c = strstr(cur, "\"weather_code\":");
  if (!t || !c) return false;
  float temp = strtof(t + 17, nullptr);
  w->temp_x10 = (int16_t)(temp * 10 + (temp < 0 ? -0.5f : 0.5f));
  w->code = (uint8_t)atoi(c + 15);
  w->valid = true;
  return true;
}

static void wx_task(void*) {
  int64_t wait_ms = WX_PERIOD_MS;
  for (;;) {
    fnx::WeatherSet p;
    portENTER_CRITICAL(&s_wx_mux);
    p = s_place;
    portEXIT_CRITICAL(&s_wx_mux);
    if (p.name[0] && radio_info().state == fnx::radio_state::CONNECTED) {
      Weather w = {};
      strlcpy(w.place, p.name, sizeof w.place);
      bool ok = wx_fetch(p, &w);
      portENTER_CRITICAL(&s_wx_mux);
      if (ok && strcmp(s_place.name, p.name) == 0) s_wx = w;  // the place may have changed meanwhile
      portEXIT_CRITICAL(&s_wx_mux);
      wait_ms = ok ? WX_PERIOD_MS : WX_RETRY_MS;
    } else {
      wait_ms = WX_PERIOD_MS;  // net_up or weather_set wakes the task
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
  }
}

// ---- Start ----

void net_start() {
  s_sock_lock = xSemaphoreCreateMutex();
  link_remote_start(&TCP_LINK, 8192);  // as the UART: 4 install chunks in flight
  xTaskCreatePinnedToCore(tcp_task, "tcp_link", 3072, nullptr, 5, nullptr, 0);
  if (mdns_init() == ESP_OK) {
    mdns_hostname_set("fenixos");
    mdns_instance_name_set("fenixOS");
    mdns_service_add(nullptr, "_fenixos", "_tcp", fnx::TCP_PORT, nullptr, 0);
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
  }
  web_start();
  // Two servers: one pool address sometimes never answered (P34); lwIP then tries the next.
  esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com"));
  c.start = false;  // net_up starts it: before an address its DNS query fails and backs off
  c.sync_cb = sntp_synced;
  esp_netif_sntp_init(&c);
  place_load();
  xTaskCreatePinnedToCore(wx_task, "weather", 4096, nullptr, 2, &s_wx_task, 0);
}

// Each new address (radio.cpp, GOT_IP): ask for the time now.
void net_up() {
  if (esp_netif_sntp_start() != ESP_OK) link_log(2, "net: SNTP did not start");
  if (s_wx_task) xTaskNotifyGive(s_wx_task);
}

}  // namespace fx::sys
