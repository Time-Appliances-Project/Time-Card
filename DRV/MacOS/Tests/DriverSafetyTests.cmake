# SPDX-License-Identifier: BSD-3-Clause

set(driver_source "${TIMECARD_SOURCE_DIR}/Driver/TimeCardDriver.cpp")
if(NOT EXISTS "${driver_source}")
    message(FATAL_ERROR "Driver source not found: ${driver_source}")
endif()
file(READ "${driver_source}" driver)

# These registers are synthesis-optional. They may only be read through a
# driver path that first proves the optional ToD telemetry span fits in BAR0.
string(FIND "${driver}" "TimeCardRegisterMapHasTODTelemetry(" tod_span_guard)
if(tod_span_guard EQUAL -1)
    message(FATAL_ERROR
        "Driver must guard optional ToD telemetry by BAR span")
endif()
foreach(optional_register IN ITEMS
        kTimeCardTodUtcStatus
        kTimeCardTodLeap
        kTimeCardTodGnssStatus
        kTimeCardTodSatellites)
    string(FIND "${driver}" "${optional_register}" optional_offset)
    if(optional_offset EQUAL -1)
        message(FATAL_ERROR
            "Driver must expose guarded optional register ${optional_register}")
    endif()
endforeach()

string(FIND "${driver}" "TimeCardBoardProfileForDevice" identity_check)
string(FIND "${driver}" "ConfigurationWrite16" first_config_write)
if(identity_check EQUAL -1 OR first_config_write EQUAL -1 OR
   identity_check GREATER first_config_write)
    message(FATAL_ERROR
        "PCI identity must be validated before configuration writes")
endif()

string(FIND "${driver}" "~kIOPCICommandBusLead" bus_lead_disabled)
if(bus_lead_disabled EQUAL -1)
    message(FATAL_ERROR
        "MMIO-only driver must explicitly leave PCI bus mastering disabled")
endif()

string(FIND "${driver}" "verifiedCommand" command_readback)
if(command_readback EQUAL -1)
    message(FATAL_ERROR
        "PCI command update must be read back before MMIO access")
endif()

string(REGEX MATCHALL "TimeCardPersistentClockControl\\(" control_preserves
    "${driver}")
list(LENGTH control_preserves control_preserve_count)
if(control_preserve_count LESS 2)
    message(FATAL_ERROR
        "GetTime and SetTime must preserve persistent clock-control bits")
endif()

string(FIND "${driver}"
    "const uint32_t configuredSource = TimeCardConfiguredClockSource(select)"
    restore_source)
if(restore_source EQUAL -1)
    message(FATAL_ERROR
        "SetTime must restore the configured low-byte clock source")
endif()

set(cli_source "${TIMECARD_SOURCE_DIR}/CLI/timecardctl.c")
file(READ "${cli_source}" cli)
string(FIND "${cli}" "set-card-from-system" explicit_set_command)
string(FIND "${cli}" "\"set-system\"" ambiguous_set_command)
if(explicit_set_command EQUAL -1 OR NOT ambiguous_set_command EQUAL -1)
    message(FATAL_ERROR
        "CLI must use the explicit set-card-from-system command name")
endif()

string(FIND "${cli}"
    "TimeCardConfiguredClockSource(info.clockSelect)" cli_low_byte_source)
if(cli_low_byte_source EQUAL -1)
    message(FATAL_ERROR
        "CLI must report the configured low-byte clock source")
endif()

foreach(required_cli_sma IN ITEMS
        "command_sma("
        "command_sma_set("
        "command_led("
        "command_led_set("
        "command_led_sma_auto("
        "command_led_gnss_auto("
        "command_led_auto("
        "command_i2c_status("
        "command_i2c_scan("
        "command_i2c_read("
        "command_i2c_mux("
        "command_sensors("
        "gnss_fix_name("
        "icp10100_pressure_pascals("
        "print_sensor_capabilities("
        "print_imu_probe_detail("
        "UTC status:"
        "GNSS status:"
        "Satellites:"
        "SHTP header"
        "chip ID"
        "BNO08x"
        "\"sma\""
        "\"sma-set\""
        "\"led\""
        "\"led-set\""
        "\"led-sma-auto\""
        "\"led-gnss-auto\""
        "\"led-auto\""
        "\"i2c-status\""
        "\"i2c-scan\""
        "\"i2c-read\""
        "\"i2c-mux\""
        "\"sensors\"")
    string(FIND "${cli}" "${required_cli_sma}" cli_sma_offset)
    if(cli_sma_offset EQUAL -1)
        message(FATAL_ERROR
            "CLI must expose SMA route and LED policy commands")
    endif()
endforeach()

set(control_center_source "${TIMECARD_SOURCE_DIR}/App/TimeCardClient.swift")
if(NOT EXISTS "${control_center_source}")
    message(FATAL_ERROR
        "Control Center IOKit client not found: ${control_center_source}")
endif()
file(READ "${control_center_source}" control_center)

foreach(required_selector IN ITEMS "selector: 0" "selector: 3")
    string(FIND "${control_center}" "${required_selector}" selector_offset)
    if(selector_offset EQUAL -1)
        message(FATAL_ERROR
            "Control Center must use read-only ${required_selector}")
    endif()
endforeach()

string(FIND "${control_center}" "setCardFromSystem" guarded_set_time)
string(FIND "${control_center}" "selector: 2" reviewed_set_selector)
if(guarded_set_time EQUAL -1 OR reviewed_set_selector EQUAL -1)
    message(FATAL_ERROR
        "Control Center must expose set-time only through a named guarded path")
endif()

foreach(required_control_center_sma IN ITEMS
        "querySMARoutes"
        "setSMARoute"
        "TimeCardSMARoute"
        "selector: 4"
        "selector: 5"
        "querySensors"
        "TimeCardSensorSnapshot"
        "queryI2CStatus"
        "probeI2C"
        "scanI2CBus"
        "readI2C"
        "queryI2CMux"
        "setI2CMux"
        "setLEDState"
        "queryLEDStates"
        "TimeCardI2CStatusSnapshot"
        "TimeCardI2CProbeResult"
        "TimeCardI2CTransferSnapshot"
        "TimeCardI2CMuxSnapshot"
        "TimeCardLEDState"
        "selector: 6"
        "selector: 7"
        "selector: 8"
        "selector: 9"
        "selector: 10"
        "selector: 11"
        "selector: 12"
        "todTelemetryAvailable"
        "gnssTelemetryAvailable"
        "gnssFixName"
        "selector: 13")
    string(FIND "${control_center}" "${required_control_center_sma}" control_sma_offset)
    if(control_sma_offset EQUAL -1)
        message(FATAL_ERROR
            "Control Center must expose SMA query and route controls")
    endif()
endforeach()

set(control_center_view_source "${TIMECARD_SOURCE_DIR}/App/ContentView.swift")
if(NOT EXISTS "${control_center_view_source}")
    message(FATAL_ERROR
        "Control Center SwiftUI view not found: ${control_center_view_source}")
endif()
file(READ "${control_center_view_source}" control_center_view)
foreach(required_control_center_view IN ITEMS
        "GNSSWorkspaceView"
        "I2CAndLEDView"
        "UTC and leap summary"
        "GNSS fix and satellite counts"
        "I2C controller registers"
        "I2C bus laboratory"
        "LED controller readback")
    string(FIND "${control_center_view}" "${required_control_center_view}" view_offset)
    if(view_offset EQUAL -1)
        message(FATAL_ERROR
            "Control Center must expose the GNSS summary workspace")
    endif()
endforeach()

foreach(required_sensor_driver IN ITEMS
        "TimeCardBNO08xHeaderValid"
        "TimeCardBNO08xProbeLocked"
        "TimeCardBNO055ProbeLocked"
        "TimeCardSensorCapabilitiesForBoard"
        "TimeCardRegisterMapHasTODTelemetry"
        "kTimeCardTodGnssStatus"
        "kTimeCardInfoValidGNSSStatus")
    string(FIND "${driver}" "${required_sensor_driver}" sensor_driver_offset)
    if(sensor_driver_offset EQUAL -1)
        message(FATAL_ERROR
            "Driver must preserve the macOS sensor and IMU probe helpers")
    endif()
endforeach()

foreach(forbidden_operation IN ITEMS
        "IOConnectCallScalarMethod"
        "clock_settime"
        "settimeofday"
        "adjtime(")
    string(FIND "${control_center}" "${forbidden_operation}" operation_offset)
    if(NOT operation_offset EQUAL -1)
        message(FATAL_ERROR
            "Control Center contains forbidden write operation: "
            "${forbidden_operation}")
    endif()
endforeach()
