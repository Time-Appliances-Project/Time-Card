cmake_minimum_required(VERSION 3.21)

project(TimeCardControlCenter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Concurrent
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
)

qt_standard_project_setup(REQUIRES 6.4)

add_library(timecard_control_core STATIC
    src/AppController.cpp
    src/AppController.h
    src/MockTimeCardBackend.cpp
    src/MockTimeCardBackend.h
    src/OscillatordClient.cpp
    src/OscillatordClient.h
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
install(FILES packaging/70-timecard-control-center.rules
    DESTINATION lib/udev/rules.d
)

if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
