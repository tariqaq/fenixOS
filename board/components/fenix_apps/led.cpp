// RGB LED status light. Levels seen and kept by the user (spike S6, P19).
#include "fenix_hal.h"
#include "shell.h"

namespace fx::apps {

static constexpr uint8_t GREEN_PEAK = 60, BLUE_DIM = 24, RED_DIM = 24;
static bool s_busy, s_error;
static int32_t s_pulse;

static void show() {
  if (s_error)
    hal::led_set(RED_DIM, 0, 0);
  else if (s_busy)
    hal::led_set(0, 0, BLUE_DIM);
  else
    hal::led_set(0, s_pulse, 0);
}

static void pulse_exec(void*, int32_t v) {
  s_pulse = v;
  show();
}

void led_pulse_connected() {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &s_pulse);
  lv_anim_set_exec_cb(&a, pulse_exec);
  lv_anim_set_values(&a, 0, GREEN_PEAK);
  lv_anim_set_duration(&a, 300);
  lv_anim_set_reverse_duration(&a, 700);
  lv_anim_start(&a);
}

void led_busy(bool on) {
  s_busy = on;
  show();
}

void led_error(bool on) {
  s_error = on;
  show();
}

}  // namespace fx::apps
