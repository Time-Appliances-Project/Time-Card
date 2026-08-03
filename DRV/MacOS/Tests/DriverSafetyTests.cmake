# SPDX-License-Identifier: BSD-3-Clause

set(driver_source "${TIMECARD_SOURCE_DIR}/Driver/TimeCardDriver.cpp")
if(NOT EXISTS "${driver_source}")
    message(FATAL_ERROR "Driver source not found: ${driver_source}")
endif()
file(READ "${driver_source}" driver)

# These registers are synthesis-optional and may only be added after an exact
# per-device FPGA image contract exists.
foreach(forbidden_register IN ITEMS
        kTimeCardTodUtcStatus
        kTimeCardTodLeap
        kTimeCardTodGnssStatus
        kTimeCardTodSatellites)
    string(FIND "${driver}" "${forbidden_register}" forbidden_offset)
    if(NOT forbidden_offset EQUAL -1)
        message(FATAL_ERROR
            "Driver accesses gated optional register ${forbidden_register}")
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
