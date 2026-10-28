// Spike S3: UART0 echo for the link speed test (tools/spike_s3_baud.py).
// Echoes every byte. A line "BAUD <n>" (sent at the current rate) makes it answer "OK"
// and switch to <n>. Console is off (sdkconfig.defaults), so UART0 is free.
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"

extern "C" void app_main(void) {
  const uart_port_t u = UART_NUM_0;
  uart_config_t cfg = {};
  cfg.baud_rate = 460800;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  uart_driver_install(u, 8192, 8192, 0, nullptr, 0);
  uart_param_config(u, &cfg);
  uart_set_pin(u, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);  // console is off, so route the pins here
  static uint8_t buf[2048];
  char line[32];
  int ll = 0;
  for (;;) {
    int n = uart_read_bytes(u, buf, sizeof buf, 2);
    if (n <= 0) continue;
    // Command detection: only when the chunk looks like "BAUD ...\n".
    if (n < 24 && memcmp(buf, "BAUD ", 5) == 0) {
      ll = n < 31 ? n : 31;
      memcpy(line, buf, ll);
      line[ll] = 0;
      int baud = atoi(line + 5);
      uart_write_bytes(u, "OK\n", 3);
      uart_wait_tx_done(u, 100);
      uart_set_baudrate(u, baud);
      continue;
    }
    uart_write_bytes(u, buf, n);
  }
}
