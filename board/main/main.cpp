// fenixOS board firmware entry point. Init order lives here only (PLAN §P4):
// display + boot screen first, then the rest while the boot line plays.
#include "fenix_apps.h"
#include "fenix_hal.h"
#include "fenix_sys.h"
#include "fenix_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void) {
  fx::hal::display_init();
  fx::sys::nvs_start();
  const fx::sys::Settings& st = fx::sys::settings();
  if (st.cal_valid) fx::hal::touch_set_calibration(st.cal);

  fx::ui::init(st.rotation);
  fx::apps::start();
  fx::ui::step();  // draw the boot screen before the backlight comes up
  fx::ui::backlight_config(st.brightness, st.dim_level, st.dim_after_s);
  fx::ui::burn_guard(st.burn_guard);

  fx::hal::led_init();
  fx::sys::clock_init();
  fx::sys::store_mount();
  fx::sys::link_start();
  fx::sys::radio_start();

  fx::sys::LinkEvent e;
  for (;;) {
    uint32_t wait = fx::ui::step();
    while (fx::sys::link_poll(&e)) fx::apps::on_link(e);
    fx::apps::tick();
    vTaskDelay(pdMS_TO_TICKS(wait));
  }
}
