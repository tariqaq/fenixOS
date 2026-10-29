// PC link on UART0 (CH340C), protocol in proto/spec.md.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "fenix_hal.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace fx::sys {

static constexpr uart_port_t UART = UART_NUM_0;
static constexpr int RX_BUF = 8192;  // 4 install chunks in flight while LittleFS writes
static constexpr int TX_BUF = 16384;  // also the stream TX ring (PLAN §4.1)
static constexpr int64_t LINK_TIMEOUT_US = 3000000;
static constexpr int64_t STATS_PERIOD_US = 5000000;
static constexpr int64_t INSTALL_TIMEOUT_US = 10000000;

static SemaphoreHandle_t s_tx_lock;
static QueueHandle_t s_events;
static QueueHandle_t s_binds;
static uint8_t s_seq;
static volatile bool s_connected;
static fnx::FrameDecoder s_dec;
static uint8_t s_wire[fnx::MAX_WIRE];

static portMUX_TYPE s_info_mux = portMUX_INITIALIZER_UNLOCKED;
static BoardInfo s_info;

// ---- TX ----

// Frame under the TX lock, then one uart_write_bytes so frames never interleave.
template <typename F>
static void send_with(uint8_t type, F fn) {
  xSemaphoreTake(s_tx_lock, portMAX_DELAY);
  size_t n = fnx::build_frame(type, s_seq++, s_wire, sizeof s_wire, fn);
  if (n) uart_write_bytes(UART, s_wire, n);
  xSemaphoreGive(s_tx_lock);
}

void link_send(uint8_t type, const uint8_t* body, size_t n) {
  send_with(type, [&](fnx::Writer& w) { w.bytes(body, n); });
}

void link_log(uint8_t level, const char* fmt, ...) {
  char text[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(text, sizeof text, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= (int)sizeof text) n = sizeof text - 1;
  send_with(fnx::ty::LOG, [&](fnx::Writer& w) {
    w.u8(level);
    w.bytes(text, n);
  });
}

// Used by stream.cpp: write a ready wire frame without blocking on a full buffer.
size_t link_tx_free() {
  size_t f = 0;
  uart_get_tx_buffer_free_size(UART, &f);
  return f;
}

void link_write_locked(const uint8_t* wire, size_t n) { uart_write_bytes(UART, wire, n); }
void link_lock() { xSemaphoreTake(s_tx_lock, portMAX_DELAY); }
void link_unlock() { xSemaphoreGive(s_tx_lock); }
uint8_t link_next_seq() { return s_seq++; }

// ---- Board info ----

static void send_board_state(const BoardInfo& b) {
  fnx::BoardState s{};
  s.screen = b.screen;
  s.dimmed = b.dimmed;
  s.rotation = b.rotation;
  strlcpy(s.app_id, b.app_id, sizeof s.app_id);
  send_with(fnx::ty::BOARD_STATE, [&](fnx::Writer& w) { fnx::write_board_state(w, s); });
}

void board_info_set(const BoardInfo& b) {
  taskENTER_CRITICAL(&s_info_mux);
  BoardInfo old = s_info;
  s_info = b;
  taskEXIT_CRITICAL(&s_info_mux);
  bool changed = old.screen != b.screen || old.dimmed != b.dimmed || old.rotation != b.rotation ||
                 strcmp(old.app_id, b.app_id) != 0;
  if (changed && s_connected) send_board_state(b);
}

BoardInfo board_info() {
  taskENTER_CRITICAL(&s_info_mux);
  BoardInfo b = s_info;
  taskEXIT_CRITICAL(&s_info_mux);
  return b;
}

// ---- Events to the UI ----

static void post(LinkEv t, int32_t v = 0, const char* text = nullptr) {
  LinkEvent e{t, v, {}};
  if (text) strlcpy(e.text, text, sizeof e.text);
  xQueueSend(s_events, &e, 0);
}

bool link_poll(LinkEvent* e) { return s_events && xQueueReceive(s_events, e, 0) == pdTRUE; }
bool link_connected() { return s_connected; }
bool bind_poll(BindValue* v) { return s_binds && xQueueReceive(s_binds, v, 0) == pdTRUE; }

static volatile uint32_t s_remote;  // x:12 | y:12 | down:1, one word so reads are atomic

bool remote_touch(int16_t* x, int16_t* y) {
  uint32_t v = s_remote;
  if (!(v & 1)) return false;
  *x = (v >> 13) & 0xFFF;
  *y = (v >> 1) & 0xFFF;
  return true;
}
uint32_t link_rx_frames() { return s_dec.frames; }
uint32_t link_rx_errors() { return s_dec.errors; }

// ---- RX dispatch ----

static void send_hello_ack() {
  fnx::HelloAck a{};
  a.proto = fnx::PROTO_VERSION;
  unsigned ma = 0, mi = 0, pa = 0;
  sscanf(esp_app_get_description()->version, "%u.%u.%u", &ma, &mi, &pa);
  a.fw_major = ma;
  a.fw_minor = mi;
  a.fw_patch = pa;
  BoardInfo b = board_info();
  a.width = b.width;
  a.height = b.height;
  a.rotation = b.rotation;
  a.free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  uint32_t used = 0, total = 0;
  store_usage(&used, &total);
  a.apps_used_kb = used;
  a.apps_total_kb = total;
  a.reset_reason = hal::reset_reason();
  a.uptime_s = esp_timer_get_time() / 1000000;
  a.time_valid = clock_valid();
  esp_efuse_mac_get_default(a.mac);
  send_with(fnx::ty::HELLO_ACK, [&](fnx::Writer& w) { fnx::write_hello_ack(w, a); });
}

static void send_stats() {
  fnx::Stats s{};
  s.free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  s.min_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  s.fps_x10 = board_info().fps_x10;
  s.rx_frames = s_dec.frames;
  s.rx_errors = s_dec.errors;
  s.stream_dropped = stream_dropped();
  send_with(fnx::ty::STATS, [&](fnx::Writer& w) { fnx::write_stats(w, s); });
}

static void send_error(uint8_t code, const char* text) {
  send_with(fnx::ty::ERROR, [&](fnx::Writer& w) {
    w.u8(code);
    w.bytes(text, strlen(text));
  });
}

static int64_t s_last_rx;
static int64_t s_install_rx;  // last install message, for the stall timeout

static void send_app_result(uint8_t op, uint8_t code, const char* id, const char* text) {
  send_with(fnx::ty::APP_RESULT, [&](fnx::Writer& w) {
    w.u8(op);
    w.u8(code);
    w.str(id);
    w.bytes(text, strlen(text));
  });
}

static void on_frame(const uint8_t* p, size_t n, void*) {
  if (n < 2) return;
  uint8_t type = p[0];
  fnx::Reader r(p + 2, n - 2);
  s_last_rx = esp_timer_get_time();
  switch (type) {
    case fnx::ty::HELLO: {
      uint8_t proto = 0;
      if (!fnx::read_hello(r, &proto)) return send_error(fnx::err::BAD_BODY, "hello");
      if (proto != fnx::PROTO_VERSION) return send_error(fnx::err::PROTO_MISMATCH, "proto");
      send_hello_ack();
      if (!s_connected) {
        s_connected = true;
        post(LinkEv::CONNECTED);
        send_board_state(board_info());
      }
      return;
    }
    case fnx::ty::PING: {
      uint32_t t = r.u32();
      send_with(fnx::ty::PONG, [&](fnx::Writer& w) { w.u32(t); });
      return;
    }
    case fnx::ty::PONG:
      return;
    case fnx::ty::TIME_SET: {
      uint32_t epoch = r.u32();
      if (!r.done()) return send_error(fnx::err::BAD_BODY, "time");
      clock_set_utc(epoch);
      post(LinkEv::TIME_SET);
      return;
    }
    case fnx::ty::REBOOT:
      uart_wait_tx_done(UART, pdMS_TO_TICKS(100));
      esp_restart();
    case fnx::ty::TOUCH: {
      uint16_t x = r.u16(), y = r.u16();
      uint8_t down = r.u8();
      if (!r.done()) return send_error(fnx::err::BAD_BODY, "touch");
      s_remote = ((uint32_t)(x & 0xFFF) << 13) | ((uint32_t)(y & 0xFFF) << 1) | (down ? 1 : 0);
      return;
    }
    case fnx::ty::APP_LIST_REQ: {
      AppMeta apps[MAX_APPS];
      int n = store_list(apps, MAX_APPS);
      send_with(fnx::ty::APP_LIST, [&](fnx::Writer& w) {
        w.u8(n);
        for (int i = 0; i < n; i++) {
          fnx::AppEntry e{};
          strlcpy(e.id, apps[i].id, sizeof e.id);
          strlcpy(e.version, apps[i].version, sizeof e.version);
          memcpy(e.sha256, apps[i].sha256, 32);
          fnx::write_app_entry(w, e);
        }
      });
      return;
    }
    case fnx::ty::APP_INSTALL_BEGIN: {
      fnx::AppInstallBegin b{};
      if (!fnx::read_app_install_begin(r, b)) return send_error(fnx::err::BAD_BODY, "install");
      uint8_t code = store_begin(b.id, b.version, b.size, b.sha256);
      s_install_rx = esp_timer_get_time();
      if (code != fnx::app::OK) return send_app_result(fnx::app::OP_INSTALL, code, b.id, "");
      send_with(fnx::ty::APP_ACK, [](fnx::Writer& w) { w.u32(0); });
      return;
    }
    case fnx::ty::APP_CHUNK: {
      uint32_t off = r.u32();
      size_t len = 0;
      const uint8_t* d = r.rest(&len);
      if (!r.ok()) return send_error(fnx::err::BAD_BODY, "chunk");
      const char* id;
      if (!store_installing(&id)) return send_app_result(fnx::app::OP_INSTALL, fnx::app::BUSY, "", "no install");
      char keep[fnx::ID_LEN];
      strlcpy(keep, id, sizeof keep);
      uint32_t next = store_chunk(off, d, len);
      s_install_rx = esp_timer_get_time();
      if (!store_installing(nullptr)) return send_app_result(fnx::app::OP_INSTALL, fnx::app::STORAGE, keep, "write failed");
      send_with(fnx::ty::APP_ACK, [&](fnx::Writer& w) { w.u32(next); });
      return;
    }
    case fnx::ty::APP_INSTALL_END: {
      const char* id = "";
      store_installing(&id);
      char keep[fnx::ID_LEN], why[64];
      strlcpy(keep, id, sizeof keep);
      uint8_t code = store_end(why, sizeof why);
      send_app_result(fnx::app::OP_INSTALL, code, keep, why);
      if (code == fnx::app::OK) post(LinkEv::APPS_CHANGED, 0, keep);
      return;
    }
    case fnx::ty::APP_REMOVE: {
      char id[fnx::ID_LEN];
      r.str(id, sizeof id);
      if (!r.done()) return send_error(fnx::err::BAD_BODY, "remove");
      uint8_t code = store_remove(id);
      send_app_result(fnx::app::OP_REMOVE, code, id, "");
      if (code == fnx::app::OK) post(LinkEv::APPS_CHANGED, 0, id);
      return;
    }
    case fnx::ty::STREAM_START: {
      uint8_t fps = r.u8();
      post(LinkEv::STREAM_START, fps ? fps : 10);
      return;
    }
    case fnx::ty::STREAM_STOP:
      post(LinkEv::STREAM_STOP);
      return;
    case fnx::ty::STREAM_SYNC:
      post(LinkEv::STREAM_SYNC);
      return;
    case fnx::ty::BIND_SET: {
      fnx::BindSet b{};
      if (!fnx::read_bind_set(r, b)) return send_error(fnx::err::BAD_BODY, "bind");
      BindValue v{};
      v.bind = b.bind;
      v.kind = b.kind;
      v.i = b.i;
      strlcpy(v.id, b.id, sizeof v.id);
      strlcpy(v.text, b.text, sizeof v.text);
      xQueueSend(s_binds, &v, 0);  // full queue: drop, the PC sends again on change
      return;
    }
    case fnx::ty::HOST_STATE: {
      uint8_t running = r.u8();
      char id[64];
      r.str(id, sizeof id);
      post(LinkEv::HOST_STATE, running, id);
      return;
    }
    default:
      send_error(fnx::err::UNKNOWN_TYPE, "type");
  }
}

static void link_task(void*) {
  static uint8_t buf[512];
  int64_t next_stats = esp_timer_get_time() + STATS_PERIOD_US;
  for (;;) {
    int n = uart_read_bytes(UART, buf, sizeof buf, pdMS_TO_TICKS(20));
    if (n > 0) s_dec.push(buf, n, on_frame, nullptr);
    int64_t now = esp_timer_get_time();
    if (s_connected && now - s_last_rx > LINK_TIMEOUT_US) {
      s_connected = false;
      s_remote = 0;
      store_abort();
      post(LinkEv::DISCONNECTED);
    }
    if (store_installing(nullptr) && now - s_install_rx > INSTALL_TIMEOUT_US) store_abort();
    if (now >= next_stats) {
      next_stats = now + STATS_PERIOD_US;
      if (s_connected) send_stats();
    }
  }
}

void link_start() {
  s_tx_lock = xSemaphoreCreateMutex();
  s_events = xQueueCreate(16, sizeof(LinkEvent));
  s_binds = xQueueCreate(16, sizeof(BindValue));
  uart_config_t c = {};
  c.baud_rate = fnx::BAUD;
  c.data_bits = UART_DATA_8_BITS;
  c.parity = UART_PARITY_DISABLE;
  c.stop_bits = UART_STOP_BITS_1;
  c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  c.source_clk = UART_SCLK_DEFAULT;
  uart_driver_install(UART, RX_BUF, TX_BUF, 0, nullptr, 0);
  uart_param_config(UART, &c);
  // With the console off, UART0 is not routed to its pins by default (spike S3).
  uart_set_pin(UART, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  xTaskCreatePinnedToCore(link_task, "link", 8192, nullptr, 6, nullptr, 0);
}

}  // namespace fx::sys
