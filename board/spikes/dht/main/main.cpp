// Spike DHT (P59): two DHT22 (AM2302) on CN1, sensor A on IO27 and sensor B on IO22.
// Reports on the UART0 console at 115200 (python tools/serial_read.py 40 --reset).
// Each read: the pin is driven low by GPIO (open drain) for 1.2 ms and released, then the RMT
// receiver times the reply (1 MHz ticks), so interrupts cannot corrupt the bit timing. The reply
// is an 80 us low, an 80 us high, then 40 bits: a 50 us low and a high of ~27 us (0) or ~70 us (1).
// The last 40 highs before the line goes idle are the bits. Sensors are read 1.25 s apart, each
// every 2.5 s (the DHT22 needs at least 2 s between reads).
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

struct Sensor {
  const char* name;
  gpio_num_t pin;
  rmt_channel_handle_t ch;
  QueueHandle_t done;
  rmt_symbol_word_t buf[64];
  int ok, bad_sum, bad_short, none;
};

static Sensor s_sensors[] = {{"A (IO27)", GPIO_NUM_27}, {"B (IO22)", GPIO_NUM_22}};

static bool IRAM_ATTR on_done(rmt_channel_handle_t, const rmt_rx_done_event_data_t* ev, void* ctx) {
  BaseType_t woke = pdFALSE;
  xQueueSendFromISR((QueueHandle_t)ctx, ev, &woke);
  return woke == pdTRUE;
}

static void setup(Sensor& s) {
  rmt_rx_channel_config_t cfg = {};
  cfg.gpio_num = s.pin;
  cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  cfg.resolution_hz = 1000000;
  cfg.mem_block_symbols = 64;
  ESP_ERROR_CHECK(rmt_new_rx_channel(&cfg, &s.ch));
  s.done = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
  rmt_rx_event_callbacks_t cbs = {};
  cbs.on_recv_done = on_done;
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s.ch, &cbs, s.done));
  ESP_ERROR_CHECK(rmt_enable(s.ch));
  gpio_set_pull_mode(s.pin, GPIO_PULLUP_ONLY);
  gpio_set_level(s.pin, 1);
  gpio_set_direction(s.pin, GPIO_MODE_INPUT_OUTPUT_OD);
}

static void read(Sensor& s) {
  rmt_receive_config_t rc = {};
  rc.signal_range_min_ns = 1000;     // glitch filter
  rc.signal_range_max_ns = 200000;   // 200 us without an edge ends the frame
  int64_t t0 = esp_timer_get_time();
  gpio_set_level(s.pin, 0);
  esp_rom_delay_us(1200);
  gpio_set_level(s.pin, 1);
  ESP_ERROR_CHECK(rmt_receive(s.ch, s.buf, sizeof s.buf, &rc));
  rmt_rx_done_event_data_t ev;
  if (xQueueReceive(s.done, &ev, pdMS_TO_TICKS(30)) != pdTRUE) {
    s.none++;
    printf("%s: no reply (no edges in 30 ms)\n", s.name);
    rmt_disable(s.ch);  // stop the pending receive
    rmt_enable(s.ch);
    return;
  }
  // Flatten to (level, us) and keep the highs.
  int highs[90], nh = 0, lows = 0;
  for (size_t i = 0; i < ev.num_symbols; i++) {
    const rmt_symbol_word_t& w = ev.received_symbols[i];
    unsigned lv[2] = {w.level0, w.level1}, du[2] = {w.duration0, w.duration1};
    for (int k = 0; k < 2; k++) {
      if (!du[k]) continue;
      if (lv[k]) { if (nh < 90) highs[nh++] = du[k]; } else lows++;
    }
  }
  if (nh < 40) {
    s.bad_short++;
    printf("%s: short reply (%u symbols, %d highs, %d lows)\n", s.name, (unsigned)ev.num_symbols, nh, lows);
    return;
  }
  uint8_t b[5] = {};
  int maxh0 = 0, minh1 = 999;
  for (int i = 0; i < 40; i++) {
    int h = highs[nh - 40 + i];
    bool one = h > 48;
    if (one) { if (h < minh1) minh1 = h; } else if (h > maxh0) maxh0 = h;
    b[i / 8] = (uint8_t)(b[i / 8] << 1 | one);
  }
  if ((uint8_t)(b[0] + b[1] + b[2] + b[3]) != b[4]) {
    s.bad_sum++;
    printf("%s: checksum %02x %02x %02x %02x %02x\n", s.name, b[0], b[1], b[2], b[3], b[4]);
    return;
  }
  int hum = b[0] << 8 | b[1];
  int t = (b[2] & 0x7f) << 8 | b[3];
  if (b[2] & 0x80) t = -t;
  s.ok++;
  printf("%s: %d.%d C  %d.%d %%RH   (%u symbols, highs: 0 <= %d us, 1 >= %d us, %d us)\n", s.name, t / 10,
         (t < 0 ? -t : t) % 10, hum / 10, hum % 10, (unsigned)ev.num_symbols, maxh0, minh1,
         (int)(esp_timer_get_time() - t0));
}

extern "C" void app_main() {
  printf("\nDHT spike: sensor A on IO27, sensor B on IO22\n");
  for (auto& s : s_sensors) setup(s);
  vTaskDelay(pdMS_TO_TICKS(2000));  // DHT22 needs ~1 s after power-up
  for (int round = 1; round <= 12; round++) {
    for (auto& s : s_sensors) {
      read(s);
      vTaskDelay(pdMS_TO_TICKS(1250));
    }
  }
  for (auto& s : s_sensors)
    printf("%s: %d ok, %d checksum, %d short, %d no reply of 12\n", s.name, s.ok, s.bad_sum, s.bad_short, s.none);
  printf("heap free %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
  for (;;) vTaskDelay(portMAX_DELAY);
}
