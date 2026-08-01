if (BUILD_TESTS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(oscillator-disciplining REQUIRED liboscillator-disciplining)
    include_directories(${oscillator-disciplining_INCLUDE_DIRS})

    file(GLOB TEST_CHECKS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/test-checks.c
        ${PROJECT_SOURCE_DIR}/src/checks.c
        ${PROJECT_SOURCE_DIR}/src/log.c
    )

    file(GLOB TEST_MINIPOD_LIB_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/test-minipod-lib.c
    )

    file(GLOB TEST_UTILS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/test-utils.c
        ${PROJECT_SOURCE_DIR}/src/utils.c
        ${PROJECT_SOURCE_DIR}/src/log.c
    )

    file(GLOB TEST_FINE_CIRCULAR_BUFFER
        ${CMAKE_CURRENT_SOURCE_DIR}/test_fine_circular_buffer.c
        ${PROJECT_SOURCE_DIR}/src/fine_circular_buffer.c
        ${PROJECT_SOURCE_DIR}/src/log.c
    )

    add_executable(test-checks ${TEST_CHECKS_SOURCES})
    add_executable(test-minipod-lib ${TEST_MINIPOD_LIB_SOURCES})
    add_executable(test-utils ${TEST_UTILS_SOURCES})
    add_executable(test-fine-circular-buffer ${TEST_FINE_CIRCULAR_BUFFER})

    target_link_libraries(test-minipod-lib PRIVATE
        ${oscillator-disciplining_LIBRARIES}
    )

    target_link_libraries(test-utils PRIVATE
        m
    )

    target_link_libraries(test-fine-circular-buffer PRIVATE
        m
    )
endif(BUILD_TESTS)
