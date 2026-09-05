# SPDX-License-Identifier: BSD-3-Clause

if(NOT IS_DIRECTORY "${TIMECARDCTL_BUNDLE}")
    message(FATAL_ERROR "timecardctl app bundle not found: ${TIMECARDCTL_BUNDLE}")
endif()

set(expected_executable "${TIMECARDCTL_BUNDLE}/Contents/MacOS/timecardctl")
if(NOT TIMECARDCTL_EXECUTABLE STREQUAL expected_executable)
    message(FATAL_ERROR
        "timecardctl executable is outside its app wrapper: ${TIMECARDCTL_EXECUTABLE}")
endif()
if(NOT EXISTS "${expected_executable}")
    message(FATAL_ERROR "timecardctl executable not found: ${expected_executable}")
endif()

set(info_plist "${TIMECARDCTL_BUNDLE}/Contents/Info.plist")
if(NOT EXISTS "${info_plist}")
    message(FATAL_ERROR "timecardctl Info.plist not found: ${info_plist}")
endif()

execute_process(
    COMMAND /usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "${info_plist}"
    RESULT_VARIABLE plist_result
    OUTPUT_VARIABLE bundle_identifier
    ERROR_VARIABLE plist_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT plist_result EQUAL 0)
    message(FATAL_ERROR "cannot read timecardctl bundle identifier: ${plist_error}")
endif()
if(NOT bundle_identifier STREQUAL "org.opentimeserver.timecard.macos.cli")
    message(FATAL_ERROR
        "unexpected timecardctl bundle identifier: ${bundle_identifier}")
endif()

execute_process(
    COMMAND /usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "${info_plist}"
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE bundle_version
    ERROR_VARIABLE version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT version_result EQUAL 0)
    message(FATAL_ERROR "cannot read timecardctl bundle version: ${version_error}")
endif()
if(NOT bundle_version STREQUAL "21")
    message(FATAL_ERROR "unexpected timecardctl bundle version: ${bundle_version}")
endif()
