idf_component_register(SRCS "settings.cpp" "clock.cpp" "store.cpp" "link.cpp" "stream.cpp"
  INCLUDE_DIRS "include"
  REQUIRES fenix_hal fenix_proto fenix_pkg mbedtls nvs_flash esp_driver_uart esp_timer esp_app_format esp_hw_support)
