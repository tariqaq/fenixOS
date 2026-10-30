cmake_minimum_required(VERSION 3.16)
# Lockstep version: the repo-root VERSION file is the only source.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" FENIX_VERSION)
# Re-run CMake when VERSION changes; without this a bump keeps the old PROJECT_VER.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
string(STRIP "${FENIX_VERSION}" FENIX_VERSION)
set(PROJECT_VER "${FENIX_VERSION}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fenixos)
# An LVGL assert (a failed allocation) restarts the board instead of LVGL's default while(1),
# which froze the UI for good (P37). Private: passed on to other targets the escape breaks.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE "LV_ASSERT_HANDLER=abort()\;")
