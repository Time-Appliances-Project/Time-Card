cmake_minimum_required(VERSION 3.2)
find_package(PkgConfig)

set(PROJECT_NAME "oscillator-disciplining")

set(BUILD_DOC off)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/version PACKAGE_VERSION)
message("${PROJECT_NAME} version is ${PACKAGE_VERSION}")
STRING(REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+" "\\1" VERSION_MAJOR "${PACKAGE_VERSION}")
STRING(REGEX REPLACE "^[0-9]+\\.([0-9])+\\.[0-9]+" "\\1" VERSION_MINOR "${PACKAGE_VERSION}")
STRING(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+)" "\\1" VERSION_PATCH "${PACKAGE_VERSION}")
set(PACKAGE_NAME "lib${PROJECT_NAME}")
set(PACKAGE_DESCRIPTION_SUMMARY "Minipod oscillator disciplining library.")

project(${PROJECT_NAME} C)

include(GNUInstallDirs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/)

add_definitions("
    -O3
    -Wall
    -Wextra
    -Wformat=2
    -Wunused-variable
    -Wold-style-definition
    -Wstrict-prototypes
    -Wno-unused-parameter
    -Wmissing-declarations
    -Wmissing-prototypes
    -Wpointer-arith
    -DLOG_USE_COLOR")
add_definitions("-DPACKAGE_VERSION=\"${PACKAGE_VERSION}\"")

add_subdirectory(include)
add_subdirectory(pkgconfig)
add_subdirectory(src)
add_subdirectory(test)
