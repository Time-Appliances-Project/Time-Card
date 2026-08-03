cmake_minimum_required(VERSION 3.21)

project(TimeCardControlCenter VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GNUInstallDirs)

option(TIMECARD_BUILD_GUI "Build the Qt Quick dashboard" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_timecard_tui_default ON)
else()
    set(_timecard_tui_default OFF)
endif()
option(TIMECARD_BUILD_TUI "Build the Qt Core ncurses dashboard" ${_timecard_tui_default})

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Concurrent
    Core
    Network
)
if(TIMECARD_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS
        Gui
        Qml
        Quick
        QuickControls2
    )
endif()
if(TIMECARD_BUILD_TUI)
    if(NOT UNIX)
        message(FATAL_ERROR "TIMECARD_BUILD_TUI requires a Unix-like terminal")
    endif()
    set(CURSES_NEED_NCURSES TRUE)
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses REQUIRED)
    if(NOT TARGET Curses::Curses)
        add_library(Curses::Curses INTERFACE IMPORTED)
        set_target_properties(Curses::Curses PROPERTIES
            INTERFACE_LINK_LIBRARIES "${CURSES_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${CURSES_INCLUDE_DIRS}"
        )
        if(CURSES_CFLAGS)
            string(REPLACE ";" " " _timecard_curses_cflags "${CURSES_CFLAGS}")
            separate_arguments(_timecard_curses_compile_options NATIVE_COMMAND
                "${_timecard_curses_cflags}")
            set_property(TARGET Curses::Curses PROPERTY
                INTERFACE_COMPILE_OPTIONS ${_timecard_curses_compile_options})
        endif()
    endif()
endif()

qt_standard_project_setup(REQUIRES 6.4)

add_library(timecard_control_core STATIC
    src/AppController.cpp
    src/AppController.h
    src/MockTimeCardBackend.cpp
    src/MockTimeCardBackend.h
    src/OscillatordClient.cpp
    src/OscillatordClient.h
    src/SessionLog.cpp
    src/SessionLog.h
    src/TimeCardBackend.h
    src/TimeCardSnapshot.h
    src/TimingMath.h
)
target_include_directories(timecard_control_core PUBLIC src)
target_link_libraries(timecard_control_core PUBLIC
    Qt6::Concurrent
    Qt6::Core
    Qt6::Network
)
target_compile_options(timecard_control_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
)

add_library(timecard_linux_backend STATIC
    src/LinuxTimeCardBackend.cpp
    src/LinuxTimeCardBackend.h
)
target_include_directories(timecard_linux_backend PUBLIC src)
target_link_libraries(timecard_linux_backend PUBLIC timecard_control_core Qt6::Core)
target_compile_options(timecard_linux_backend PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
)

if(TIMECARD_BUILD_GUI)
    qt_add_executable(timecard_control_center
        src/main.cpp
    )
    set_target_properties(timecard_control_center PROPERTIES
        OUTPUT_NAME timecard-control-center
        MACOSX_BUNDLE FALSE
    )

    set(qml_files
        qml/Main.qml
        qml/KeyValueRow.qml
        qml/MetricTile.qml
        qml/NavButton.qml
        qml/Panel.qml
        qml/TelemetryChart.qml
    )
    set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
    set_source_files_properties(qml/KeyValueRow.qml PROPERTIES QT_RESOURCE_ALIAS KeyValueRow.qml)
    set_source_files_properties(qml/MetricTile.qml PROPERTIES QT_RESOURCE_ALIAS MetricTile.qml)
    set_source_files_properties(qml/NavButton.qml PROPERTIES QT_RESOURCE_ALIAS NavButton.qml)
    set_source_files_properties(qml/Panel.qml PROPERTIES QT_RESOURCE_ALIAS Panel.qml)
    set_source_files_properties(qml/TelemetryChart.qml PROPERTIES QT_RESOURCE_ALIAS TelemetryChart.qml)

    qt_add_qml_module(timecard_control_center
        URI TimeCard.ControlCenter
        VERSION 1.0
        RESOURCE_PREFIX /qt/qml
        QML_FILES ${qml_files}
    )

    target_link_libraries(timecard_control_center PRIVATE
        timecard_control_core
        timecard_linux_backend
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
    )
    target_compile_options(timecard_control_center PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
    )

    install(TARGETS timecard_control_center
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES packaging/org.opentimeserver.TimeCardControlCenter.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES packaging/org.opentimeserver.TimeCardControlCenter.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
endif()

if(TIMECARD_BUILD_TUI)
    add_library(timecard_tui_core STATIC
        src/tui/TuiRenderer.cpp
        src/tui/TuiRenderer.h
    )
    target_include_directories(timecard_tui_core PUBLIC src/tui)
    target_link_libraries(timecard_tui_core PUBLIC Qt6::Core)
    target_compile_options(timecard_tui_core PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )

    qt_add_executable(timecard_control_center_tui
        src/tui/main.cpp
        src/tui/NcursesTerminal.cpp
        src/tui/NcursesTerminal.h
        src/tui/TuiApplication.cpp
        src/tui/TuiApplication.h
        src/tui/UnixSignalBridge.cpp
        src/tui/UnixSignalBridge.h
    )
    set_target_properties(timecard_control_center_tui PROPERTIES
        OUTPUT_NAME timecard-control-center-tui
        MACOSX_BUNDLE FALSE
    )
    target_compile_definitions(timecard_control_center_tui PRIVATE
        _XOPEN_SOURCE_EXTENDED=1
    )
    target_link_libraries(timecard_control_center_tui PRIVATE
        timecard_tui_core
        timecard_control_core
        timecard_linux_backend
        Qt6::Core
        Curses::Curses
    )
    target_compile_options(timecard_control_center_tui PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    install(TARGETS timecard_control_center_tui
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(FILES packaging/70-timecard-control-center.rules
    DESTINATION lib/udev/rules.d
)

if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
    if(TIMECARD_BUILD_TUI)
        qt_add_executable(timecard_tui_renderer_tests
            src/tui/tests/tst_tuirenderer.cpp
        )
        target_link_libraries(timecard_tui_renderer_tests PRIVATE
            timecard_tui_core
            Qt6::Test
        )
        target_compile_options(timecard_tui_renderer_tests PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
            $<$<CXX_COMPILER_ID:AppleClang>:-Wno-error=variadic-macro-arguments-omitted>
        )
        add_test(NAME timecard_tui_renderer_tests COMMAND timecard_tui_renderer_tests)
    endif()
endif()
