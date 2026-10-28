idf_component_register(SRCS "shell.cpp" "home.cpp" "settings_ui.cpp" "session.cpp" "stream_ui.cpp" "led.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "."
  REQUIRES fenix_ui fenix_sys fenix_hal fenix_proto esp_timer esp_hw_support esp_app_format spi_flash)
