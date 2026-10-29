# fenixOS board simulator (docs/toolchain.md, "Board simulator"): the board's UI code on the PC.
# Build: pwsh board/sim/build.ps1   (MSVC + Ninja; tools/sim_shots.py calls it)
cmake_minimum_required(VERSION 3.20)
project(fenix_sim C CXX)

set(BOARD ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(COMP ${BOARD}/components)

# LVGL: the board's managed component (same version and files), else fetched at the pinned tag.
set(LVGL_DIR ${BOARD}/managed_components/lvgl__lvgl)
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
  include(FetchContent)
  FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.5.0 GIT_SHALLOW TRUE)
  FetchContent_Populate(lvgl)
  set(LVGL_DIR ${lvgl_SOURCE_DIR})
endif()
file(GLOB_RECURSE LVGL_SRCS ${LVGL_DIR}/src/*.c ${LVGL_DIR}/src/*.cpp)

file(READ ${BOARD}/../VERSION FENIX_VERSION)
string(STRIP "${FENIX_VERSION}" FENIX_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BOARD}/../VERSION)

file(GLOB FONT_SRCS ${COMP}/fenix_ui/fonts/*.c)
add_executable(fenix_sim
  sim_main.cpp sim_platform.cpp sim_sys.cpp sim_png.cpp
  ${COMP}/fenix_ui/ui_core.cpp ${COMP}/fenix_ui/widgets.cpp ${COMP}/fenix_ui/nav.cpp ${FONT_SRCS}
  ${COMP}/fenix_apps/shell.cpp ${COMP}/fenix_apps/home.cpp ${COMP}/fenix_apps/settings_ui.cpp
  ${COMP}/fenix_apps/session.cpp ${COMP}/fenix_apps/stream_ui.cpp ${COMP}/fenix_apps/led.cpp
  ${COMP}/fenix_apps/app_pkg.cpp
  ${COMP}/fenix_pkg/fenix_pkg.cpp ${COMP}/fenix_proto/fenix_proto.cpp
  ${LVGL_SRCS})
set_target_properties(fenix_sim PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(fenix_sim PRIVATE
  LV_CONF_KCONFIG_EXTERNAL_INCLUDE="lv_kconfig.h" LV_CONF_SKIP
  FENIX_VERSION="${FENIX_VERSION}" _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(fenix_sim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${LVGL_DIR}
  ${COMP}/fenix_ui/include ${COMP}/fenix_apps/include ${COMP}/fenix_apps ${COMP}/fenix_hal/include
  ${COMP}/fenix_sys/include ${COMP}/fenix_pkg/include ${COMP}/fenix_proto/include)
if(MSVC)
  target_compile_options(fenix_sim PRIVATE /utf-8 /W1 /FI${CMAKE_CURRENT_SOURCE_DIR}/sim_compat.h)
  target_link_libraries(fenix_sim PRIVATE user32 gdi32)
else()
  target_compile_options(fenix_sim PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/sim_compat.h)
endif()
