cmake_minimum_required(VERSION 3.24)

if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING
        "Minimum supported macOS version")
endif()

project(TimeCardMacOS LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_executable(timecard_register_tests Tests/RegisterMapTests.cpp)
target_include_directories(timecard_register_tests PRIVATE Shared)
target_compile_options(timecard_register_tests PRIVATE -Wall -Wextra -Werror)

if(APPLE)
    add_executable(timecardctl MACOSX_BUNDLE CLI/timecardctl.c)
    target_include_directories(timecardctl PRIVATE Shared)
    target_compile_options(timecardctl PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(timecardctl PRIVATE "-framework IOKit" "-framework CoreFoundation")
    set_target_properties(timecardctl PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "timecardctl"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.opentimeserver.timecard.macos.cli"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    )
endif()

enable_testing()
add_test(NAME register_map COMMAND timecard_register_tests)

if(APPLE)
    add_test(
        NAME timecardctl_bundle
        COMMAND ${CMAKE_COMMAND}
            "-DTIMECARDCTL_BUNDLE=$<TARGET_BUNDLE_DIR:timecardctl>"
            "-DTIMECARDCTL_EXECUTABLE=$<TARGET_FILE:timecardctl>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CLIAppBundleTests.cmake"
    )
endif()
