cmake_minimum_required(VERSION 3.20)

set(PROJECT_NAME "oscillatord")

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/version PACKAGE_VERSION)
message("${PROJECT_NAME} version is ${PACKAGE_VERSION}")
STRING(REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+" "\\1" VERSION_MAJOR "${PACKAGE_VERSION}")
STRING(REGEX REPLACE "^[0-9]+\\.([0-9])+\\.[0-9]+" "\\1" VERSION_MINOR "${PACKAGE_VERSION}")
STRING(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+)" "\\1" VERSION_PATCH "${PACKAGE_VERSION}")
set(PACKAGE_NAME "lib${PROJECT_NAME}")
set(PACKAGE_DESCRIPTION_SUMMARY "Oscillator disciplining daemon.")
set(CMAKE_C_STANDARD 11)

project(${PROJECT_NAME} C)
include(GNUInstallDirs)
include(CTest)

find_package(PkgConfig REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

pkg_check_modules(oscillator-disciplining REQUIRED IMPORTED_TARGET liboscillator-disciplining)
pkg_check_modules(ubloxcfg REQUIRED IMPORTED_TARGET ubloxcfg ff)
pkg_check_modules(json-c REQUIRED json-c)

# Keep the complete pkg-config link interface, including non-system library
# directories used by tools/build-timecard.sh's private dependency prefix.
set(oscillator-disciplining_LIBRARIES PkgConfig::oscillator-disciplining)
set(ubloxcfg_LIBRARIES PkgConfig::ubloxcfg)

ADD_DEFINITIONS( -DPACKAGE_VERSION=\"${PACKAGE_VERSION}\" )

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common/)
include_directories(${oscillator-disciplining_INCLUDE_DIRS})
include_directories(${ubloxcfg_INCLUDE_DIRS})

add_compile_options(
    -Wall
    -Wextra
    -Wformat=2
    -Wunused-variable
    -Wunused-function
    -Wold-style-definition
    -Wstrict-prototypes
    -Wno-unused-parameter
    -Wno-address-of-packed-member
    -Wmissing-declarations
    -Wmissing-prototypes
    -Wpointer-arith)
add_compile_definitions(LOG_USE_COLOR _GNU_SOURCE
    OD_REVISION="${PACKAGE_VERSION}")

add_subdirectory(src)
add_subdirectory(systemd)
add_subdirectory(tests)
add_subdirectory(tests_production)
add_subdirectory(utils)

install(FILES example_configurations/oscillatord_timecard.conf
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
