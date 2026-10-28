cmake_minimum_required(VERSION 3.16)
# Lockstep version: the repo-root VERSION file is the only source.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" FENIX_VERSION)
string(STRIP "${FENIX_VERSION}" FENIX_VERSION)
set(PROJECT_VER "${FENIX_VERSION}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fenixos)
