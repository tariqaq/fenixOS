file(GLOB FONT_SRCS "fonts/*.c")
idf_component_register(SRCS "ui_core.cpp" "widgets.cpp" "nav.cpp" ${FONT_SRCS}
  INCLUDE_DIRS "include"
  REQUIRES fenix_hal fenix_sys esp_timer)
