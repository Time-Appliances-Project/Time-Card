qt_add_executable(timecard_control_center_tests
    tst_linuxbackend.cpp
)
target_link_libraries(timecard_control_center_tests PRIVATE
    timecard_control_core
    timecard_linux_backend
    Qt6::Test
)
target_compile_options(timecard_control_center_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
)

add_test(NAME timecard_control_center_tests COMMAND timecard_control_center_tests)
