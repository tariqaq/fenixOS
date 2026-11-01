idf_component_register(SRCS "settings.cpp" "clock.cpp" "store.cpp" "link.cpp" "stream.cpp" "sysinfo.cpp" "radio.cpp" "net.cpp" "web.cpp" "sd.cpp" "ota.cpp"
  INCLUDE_DIRS "include"
  REQUIRES fenix_hal fenix_proto fenix_pkg mbedtls nvs_flash esp_driver_uart esp_timer esp_app_format esp_hw_support
           spi_flash app_update fatfs sdmmc esp_driver_sdspi esp_driver_spi bootloader_support esp_wifi esp_netif esp_event lwip mdns esp_http_server)
