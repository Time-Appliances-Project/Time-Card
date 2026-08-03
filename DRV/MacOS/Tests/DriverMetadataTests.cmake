# SPDX-License-Identifier: BSD-3-Clause

if(NOT IS_DIRECTORY "${TIMECARD_SOURCE_DIR}")
    message(FATAL_ERROR "macOS driver source directory not found")
endif()

set(register_header "${TIMECARD_SOURCE_DIR}/Shared/TimeCardRegisters.h")
file(READ "${register_header}" registers)
string(REGEX MATCH
    "#define[ \t]+TIMECARD_PCI_MATCH_STRING[ \t\\\n\r]+\"([^\"]*)\""
    canonical_match_definition "${registers}")
if(canonical_match_definition STREQUAL "")
    message(FATAL_ERROR "canonical PCI match definition not found")
endif()
set(expected_pci_matches "${CMAKE_MATCH_1}")
if(NOT expected_pci_matches STREQUAL
   "0x04001d9b 0x100818d4 0xa0001ad7 0x0400ad5a 0x0410ad5a")
    message(FATAL_ERROR
        "unexpected canonical PCI match set: ${expected_pci_matches}")
endif()

foreach(relative_path IN ITEMS
        Driver/Info.plist
        Driver/TimeCardDriver.entitlements)
    set(metadata_path "${TIMECARD_SOURCE_DIR}/${relative_path}")
    if(NOT EXISTS "${metadata_path}")
        message(FATAL_ERROR "driver metadata file not found: ${metadata_path}")
    endif()
    file(READ "${metadata_path}" metadata)
    string(REGEX MATCHALL "<key>IOPCIPrimaryMatch</key>" match_keys
        "${metadata}")
    list(LENGTH match_keys match_key_count)
    if(NOT match_key_count EQUAL 1)
        message(FATAL_ERROR
            "${relative_path} must contain exactly one IOPCIPrimaryMatch key")
    endif()
    string(REGEX MATCH
        "<key>IOPCIPrimaryMatch</key>[ \t\r\n]*<string>([^<]*)</string>"
        match_entry "${metadata}")
    if(match_entry STREQUAL "")
        message(FATAL_ERROR
            "${relative_path} does not contain a string IOPCIPrimaryMatch value")
    endif()
    if(NOT CMAKE_MATCH_1 STREQUAL expected_pci_matches)
        message(FATAL_ERROR
            "${relative_path} PCI match set is '${CMAKE_MATCH_1}', expected "
            "'${expected_pci_matches}'")
    endif()
endforeach()
