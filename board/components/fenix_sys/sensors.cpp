// Wired sensors (P59; docs/packages.md "Sensors", docs/board-info.md "DHT22 sensors on CN1").
// A DHT22 on CN1's IO22 (slot 0) or IO27 (slot 1). A pin is set up (RMT receiver, open drain)
// only while an installed app declares a sensor on it, or a SENSOR_WATCH asks for it; otherwise
// it is a plain input with no pull, as before P59.
//
// One task reads: every second it takes the next slot, so each pin is read every 2 s and the two
// are 1 s apart. A slot is read while the UI wants it (its app is open, or the app's PC half runs
// with the link up: sensors_want), a watch asks for it, or a page polled /sensors.json in the
// last 10 s (P61, declared pins only). A read drives the pin low for 1.2 ms by
// GPIO, releases it, and lets the RMT receiver time the answer (1 MHz ticks), so interrupts from
// the display, touch or Wi-Fi cannot corrupt the bits (measured in the spike: 24 of 24 good).
// Per slot: ok on a good read; after 3 failed reads in a row, missing when the last 3 got no
// answer at all, else error (a mix of bad and missing answers is a bad signal, not an absent
// sensor), holding the last state until then. Corrected (v0.1.30): v0.1.29 took the label from
// the last failure only, so bad, bad, none read "missing" and the label could flip per read. Readings reach the UI through sensors_get (a sequence number that moves
// on every change) and the PC through SENSOR frames, sent from this task.
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "fenix_proto.h"
#include "fenix_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace fx::sys {

static constexpr gpio_num_t PINS[SENSOR_SLOTS] = {GPIO_NUM_22, GPIO_NUM_27};
static constexpr int FAILS_TO_STATE = 3;  // failed reads in a row before missing or error
static constexpr int ONE_BIT_US = 48;     // a high over this is a 1 (spike: 0 <= 27 us, 1 >= 70 us)

struct Slot {
  rmt_channel_handle_t ch;  // null while the pin is not set up
  QueueHandle_t done;
  rmt_symbol_word_t buf[64];  // one answer is 42 symbols
  int fails;     // failed reads in a row (any kind), capped at FAILS_TO_STATE
  int none_run;  // of those, the latest ones in a row that got no answer at all
  fnx::SensorFake fake;
};
static Slot s_slot[SENSOR_SLOTS];
static SensorReading s_read[SENSOR_SLOTS];  // under s_mux
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t s_want, s_up, s_watch;  // pin masks: bit 0 IO22, bit 1 IO27
static volatile int64_t s_web_until;              // P61: read declared pins until then (us)
static constexpr int64_t WEB_HOLD_US = 10 * 1000000;

static bool IRAM_ATTR on_done(rmt_channel_handle_t, const rmt_rx_done_event_data_t* ev, void* ctx) {
  BaseType_t woke = pdFALSE;
  xQueueSendFromISR((QueueHandle_t)ctx, ev, &woke);
  return woke == pdTRUE;
}

static void release(gpio_num_t pin) {
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(pin, GPIO_FLOATING);
}

static void set_up(int i, bool on) {
  Slot& s = s_slot[i];
  if (on == (s.ch != nullptr)) return;
  if (!on) {
    rmt_disable(s.ch);
    rmt_del_channel(s.ch);
    s.ch = nullptr;
    release(PINS[i]);
    return;
  }
  if (!s.done) s.done = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
  rmt_rx_channel_config_t cfg = {};
  cfg.gpio_num = PINS[i];
  cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  cfg.resolution_hz = 1000000;
  cfg.mem_block_symbols = 64;
  rmt_rx_event_callbacks_t cbs = {};
  cbs.on_recv_done = on_done;
  if (!s.done || rmt_new_rx_channel(&cfg, &s.ch) != ESP_OK) {
    s.ch = nullptr;
    link_log(2, "sensors: IO%d: no RMT channel", (int)PINS[i]);
    return;
  }
  if (rmt_rx_register_event_callbacks(s.ch, &cbs, s.done) != ESP_OK || rmt_enable(s.ch) != ESP_OK) {
    rmt_del_channel(s.ch);
    s.ch = nullptr;
    return;
  }
  // The internal pull-up (~45 kOhm) backs the module's own; IO27 also has the board's R18 (10 kOhm).
  gpio_set_pull_mode(PINS[i], GPIO_PULLUP_ONLY);
  gpio_set_level(PINS[i], 1);
  gpio_set_direction(PINS[i], GPIO_MODE_INPUT_OUTPUT_OD);
}

enum class Got { OK, NONE, BAD };

// One DHT22 read: 40 bits = humidity x10 (16), temperature x10 (16, top bit the sign), checksum.
static Got read_dht22(int i, int16_t* t, uint16_t* h) {
  Slot& s = s_slot[i];
  rmt_receive_config_t rc = {};
  rc.signal_range_min_ns = 1000;    // glitch filter
  rc.signal_range_max_ns = 200000;  // 200 us without an edge ends the answer
  xQueueReset(s.done);
  gpio_set_level(PINS[i], 0);
  esp_rom_delay_us(1200);
  gpio_set_level(PINS[i], 1);
  if (rmt_receive(s.ch, s.buf, sizeof s.buf, &rc) != ESP_OK) return Got::BAD;
  rmt_rx_done_event_data_t ev;
  if (xQueueReceive(s.done, &ev, pdMS_TO_TICKS(30)) != pdTRUE) {
    rmt_disable(s.ch);  // drops the pending receive
    rmt_enable(s.ch);
    return Got::NONE;
  }
  uint16_t highs[90];
  int nh = 0;
  for (size_t k = 0; k < ev.num_symbols; k++) {
    const rmt_symbol_word_t& w = ev.received_symbols[k];
    if (w.level0 && w.duration0 && nh < 90) highs[nh++] = w.duration0;
    if (w.level1 && w.duration1 && nh < 90) highs[nh++] = w.duration1;
  }
  if (nh < 40) return ev.num_symbols < 4 ? Got::NONE : Got::BAD;
  uint8_t b[5] = {};
  for (int k = 0; k < 40; k++) b[k / 8] = (uint8_t)(b[k / 8] << 1 | (highs[nh - 40 + k] > ONE_BIT_US));
  if ((uint8_t)(b[0] + b[1] + b[2] + b[3]) != b[4]) return Got::BAD;
  int hum = b[0] << 8 | b[1], temp = (b[2] & 0x7f) << 8 | b[3];
  if (b[2] & 0x80) temp = -temp;
  if (hum > 1000 || temp < -400 || temp > 800) return Got::BAD;
  *t = (int16_t)temp;
  *h = (uint16_t)hum;
  return Got::OK;
}

// Pins some installed app declares (AppMeta::sensors: low nibble IO22, high nibble IO27).
uint8_t sensors_declared() {
  uint8_t m = 0;
  AppMeta a;
  for (int i = 0; store_at(i, &a); i++) m |= (a.sensors & 0x0F ? 1 : 0) | (a.sensors & 0xF0 ? 2 : 0);
  return m;
}

static void publish(int i, uint8_t state, int16_t t, uint16_t h, bool values) {
  // Local time first (no clock calls inside the critical section). Without a clock the last
  // good time stays: it was right when it was taken.
  char hms[9] = "";
  struct tm tm;
  if (values && state == fnx::sensor_state::OK && clock_local(&tm))
    snprintf(hms, sizeof hms, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  portENTER_CRITICAL(&s_mux);
  SensorReading& r = s_read[i];
  bool changed = r.state != state || (values && (r.temp_x10 != t || r.hum_x10 != h));
  r.kind = SENSOR_KIND_DHT22;
  r.state = state;
  if (values) {
    r.temp_x10 = t;
    r.hum_x10 = h;
  }
  r.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (values && state == fnx::sensor_state::OK) {  // P61: the time of every good read
    r.good_ms = r.uptime_ms ? r.uptime_ms : 1;
    if (hms[0] || !r.good_hms[0]) memcpy(r.good_hms, hms, sizeof r.good_hms);
    changed = true;
  }
  if (changed) r.seq++;
  portEXIT_CRITICAL(&s_mux);
}

static void stop(int i) {
  s_slot[i].fails = s_slot[i].none_run = 0;
  publish(i, SENSOR_STARTING, 0, 0, false);
}

static void send(int i) {
  SensorReading r = sensors_get(i);
  if (r.state == SENSOR_STARTING || !link_connected()) return;
  fnx::SensorReading m{};
  m.pin = (uint8_t)PINS[i];
  m.kind = r.kind;
  m.state = r.state;
  m.temp_x10 = r.temp_x10;
  m.hum_x10 = r.hum_x10;
  m.uptime_ms = r.uptime_ms;
  uint8_t b[16];
  fnx::Writer w(b, sizeof b);
  fnx::write_sensor(w, m);
  link_send(fnx::ty::SENSOR, b, w.size());
}

static void sensor_task(void*) {
  for (int i = 0;; i = (i + 1) % SENSOR_SLOTS) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint8_t bit = 1 << i, watch = s_watch, decl = sensors_declared();
    Slot& s = s_slot[i];
    set_up(i, (decl | watch) & bit);
    // A page polling /sensors.json (P61) keeps the declared pins read; it sends no frames.
    uint8_t web = esp_timer_get_time() < s_web_until ? decl : 0;
    if (!((s_want | watch | web) & bit)) {
      if (sensors_get(i).state != SENSOR_STARTING) stop(i);  // the next read starts afresh
      continue;
    }
    fnx::SensorFake f = s.fake;
    if (f.mode == fnx::sensor_fake::OK) {
      s.fails = s.none_run = 0;
      publish(i, fnx::sensor_state::OK, f.temp_x10, f.hum_x10, true);
    } else if (f.mode == fnx::sensor_fake::MISSING || f.mode == fnx::sensor_fake::ERROR) {
      s.fails = FAILS_TO_STATE;
      publish(i, f.mode == fnx::sensor_fake::MISSING ? fnx::sensor_state::MISSING : fnx::sensor_state::ERROR, 0, 0,
              false);
    } else if (s.ch) {
      int16_t t = 0;
      uint16_t h = 0;
      Got g = read_dht22(i, &t, &h);
      if (g == Got::OK) {
        s.fails = s.none_run = 0;
        publish(i, fnx::sensor_state::OK, t, h, true);
      } else {
        s.none_run = g == Got::NONE ? (s.none_run < FAILS_TO_STATE ? s.none_run + 1 : FAILS_TO_STATE) : 0;
        if (++s.fails >= FAILS_TO_STATE) {
          s.fails = FAILS_TO_STATE;
          bool missing = s.none_run >= FAILS_TO_STATE;
          publish(i, missing ? fnx::sensor_state::MISSING : fnx::sensor_state::ERROR, 0, 0, false);
        }
      }
    } else {
      continue;
    }
    if ((s_up | watch) & bit) send(i);
  }
}

void sensors_start() {
  for (int i = 0; i < SENSOR_SLOTS; i++) s_read[i].state = SENSOR_STARTING;
  // Below the link (6) and the UI; reads block for ~6 ms on a queue, not on the CPU.
  if (xTaskCreatePinnedToCore(sensor_task, "sensors", 3072, nullptr, 3, nullptr, 0) != pdPASS)
    link_log(1, "sensors: no task");
}

void sensors_want(uint8_t pins, uint8_t upstream) {
  s_want = pins;
  s_up = upstream;
}

SensorReading sensors_get(int slot) {
  portENTER_CRITICAL(&s_mux);
  SensorReading r = s_read[slot];
  portEXIT_CRITICAL(&s_mux);
  return r;
}

void sensors_watch(uint8_t pins) { s_watch = pins & 3; }

void sensors_web_poll() { s_web_until = esp_timer_get_time() + WEB_HOLD_US; }

void sensors_fake(const fnx::SensorFake& f) {
  int i = f.pin == 22 ? 0 : f.pin == 27 ? 1 : -1;
  if (i < 0 || f.mode > fnx::sensor_fake::ERROR) return;
  s_slot[i].fake = f;
  s_slot[i].fails = s_slot[i].none_run = 0;
  link_log(3, "sensors: IO%d %s", f.pin, f.mode ? "faked" : "real readings again");
}

}  // namespace fx::sys
