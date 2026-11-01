cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.sd")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fenix_spike_sd)
