#include <stdlib.h>
#include <sys/time.h>

#include "fenix_hal.h"
#include "fenix_sys.h"

namespace fx::sys {

void clock_init() {
  setenv("TZ", "MYT-8", 1);
  tzset();
  hal::time_boot_check();
}

void clock_set_utc(uint32_t epoch_s) {
  timeval tv{static_cast<time_t>(epoch_s), 0};
  settimeofday(&tv, nullptr);
  hal::time_mark_valid();
}

bool clock_valid() { return hal::time_valid(); }

bool clock_local(struct tm* out) {
  if (!clock_valid()) return false;
  time_t t = time(nullptr);
  localtime_r(&t, out);
  return true;
}

}  // namespace fx::sys
