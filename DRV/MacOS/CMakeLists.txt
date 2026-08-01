cmake_minimum_required(VERSION 3.24)
project(TimeCardMacOS LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_executable(timecard_register_tests Tests/RegisterMapTests.cpp)
target_include_directories(timecard_register_tests PRIVATE Shared)
target_compile_options(timecard_register_tests PRIVATE -Wall -Wextra -Werror)

if(APPLE)
    add_executable(timecardctl CLI/timecardctl.c)
    target_include_directories(timecardctl PRIVATE Shared)
    target_compile_options(timecardctl PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(timecardctl PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()

enable_testing()
add_test(NAME register_map COMMAND timecard_register_tests)
