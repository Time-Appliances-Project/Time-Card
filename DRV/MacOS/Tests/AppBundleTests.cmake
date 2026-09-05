# SPDX-License-Identifier: BSD-3-Clause

if(NOT IS_DIRECTORY "${TIMECARD_APP_BUNDLE}")
    message(FATAL_ERROR "TimeCardMacOS app bundle not found: ${TIMECARD_APP_BUNDLE}")
endif()

set(app_info_plist "${TIMECARD_APP_BUNDLE}/Contents/Info.plist")
set(app_executable "${TIMECARD_APP_BUNDLE}/Contents/MacOS/TimeCardMacOS")
if(NOT EXISTS "${app_info_plist}")
    message(FATAL_ERROR "Control Center Info.plist not found: ${app_info_plist}")
endif()
if(NOT EXISTS "${app_executable}")
    message(FATAL_ERROR "Control Center executable not found: ${app_executable}")
endif()

function(read_app_plist_key output_variable key)
    execute_process(
        COMMAND /usr/libexec/PlistBuddy -c "Print :${key}" "${app_info_plist}"
        RESULT_VARIABLE app_plist_result
        OUTPUT_VARIABLE app_plist_value
        ERROR_VARIABLE app_plist_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT app_plist_result EQUAL 0)
        message(FATAL_ERROR "cannot read Control Center ${key}: ${app_plist_error}")
    endif()
    set(${output_variable} "${app_plist_value}" PARENT_SCOPE)
endfunction()

read_app_plist_key(app_bundle_identifier CFBundleIdentifier)
read_app_plist_key(app_display_name CFBundleDisplayName)
read_app_plist_key(app_bundle_name CFBundleName)
read_app_plist_key(app_executable_name CFBundleExecutable)
read_app_plist_key(app_marketing_version CFBundleShortVersionString)
read_app_plist_key(app_build_version CFBundleVersion)
read_app_plist_key(app_icon_name CFBundleIconName)

if(NOT app_bundle_identifier STREQUAL "org.opentimeserver.timecard.macos")
    message(FATAL_ERROR
        "unexpected Control Center bundle identifier: ${app_bundle_identifier}")
endif()
if(NOT app_display_name STREQUAL "Time Card Control Center")
    message(FATAL_ERROR
        "unexpected Control Center display name: ${app_display_name}")
endif()
if(NOT app_bundle_name STREQUAL "Time Card Control Center")
    message(FATAL_ERROR "unexpected Control Center menu-bar name: ${app_bundle_name}")
endif()
get_filename_component(app_filename "${TIMECARD_APP_BUNDLE}" NAME)
if(NOT app_filename STREQUAL "Time Card Control Center.app")
    message(FATAL_ERROR "unexpected Control Center application filename: ${app_filename}")
endif()
if(NOT app_executable_name STREQUAL "TimeCardMacOS")
    message(FATAL_ERROR "Control Center internal executable name must stay stable")
endif()
if(NOT app_marketing_version STREQUAL "0.2.0")
    message(FATAL_ERROR
        "unexpected Control Center version: ${app_marketing_version}")
endif()
if(NOT app_build_version STREQUAL "73")
    message(FATAL_ERROR
        "unexpected Control Center build: ${app_build_version}")
endif()
if(NOT app_icon_name STREQUAL "AppIcon")
    message(FATAL_ERROR
        "unexpected Control Center icon name: ${app_icon_name}")
endif()
if(NOT EXISTS "${TIMECARD_APP_BUNDLE}/Contents/Resources/Assets.car")
    message(FATAL_ERROR "Control Center asset catalog was not compiled")
endif()
execute_process(COMMAND /usr/bin/xcrun assetutil --info
    "${TIMECARD_APP_BUNDLE}/Contents/Resources/Assets.car"
    RESULT_VARIABLE branding_status OUTPUT_VARIABLE branding_assets ERROR_VARIABLE branding_error)
if(NOT branding_status EQUAL 0 OR NOT branding_assets MATCHES "TimeCardArtwork")
    message(FATAL_ERROR "Splash artwork is missing: ${branding_error}")
endif()
if(NOT branding_assets MATCHES "TimeCardMenuBar")
    message(FATAL_ERROR "Monochrome menu-bar template asset is missing")
endif()
string(JSON branding_count LENGTH "${branding_assets}")
math(EXPR branding_last "${branding_count} - 1")
set(menu_template_scales "")
foreach(index RANGE 0 ${branding_last})
    string(JSON asset_name ERROR_VARIABLE name_error GET "${branding_assets}" ${index} Name)
    string(JSON asset_type ERROR_VARIABLE type_error GET "${branding_assets}" ${index} AssetType)
    if(asset_name STREQUAL "TimeCardMenuBar" AND asset_type STREQUAL "Image")
        string(JSON template_mode GET "${branding_assets}" ${index} "Template Mode")
        string(JSON color_model GET "${branding_assets}" ${index} ColorModel)
        string(JSON opaque GET "${branding_assets}" ${index} Opaque)
        string(JSON vector GET "${branding_assets}" ${index} "Preserved Vector Representation")
        string(JSON scale GET "${branding_assets}" ${index} Scale)
        string(JSON pixel_width GET "${branding_assets}" ${index} PixelWidth)
        string(JSON pixel_height GET "${branding_assets}" ${index} PixelHeight)
        math(EXPR expected_width "26 * ${scale}")
        math(EXPR expected_height "18 * ${scale}")
        if(NOT template_mode STREQUAL "template" OR NOT color_model STREQUAL "Monochrome" OR opaque OR NOT vector)
            message(FATAL_ERROR "Menu-bar image must be monochrome, transparent, vector-preserving and template-rendered")
        endif()
        if(NOT pixel_width EQUAL expected_width OR NOT pixel_height EQUAL expected_height)
            message(FATAL_ERROR "Menu-bar image must retain its 26 by 18 point aspect ratio")
        endif()
        list(APPEND menu_template_scales "${scale}")
    endif()
endforeach()
list(FIND menu_template_scales "1" menu_standard_scale)
list(FIND menu_template_scales "2" menu_retina_scale)
if(menu_standard_scale LESS 0 OR menu_retina_scale LESS 0)
    message(FATAL_ERROR "Menu-bar template must have standard and Retina renditions")
endif()

set(extensions_dir
    "${TIMECARD_APP_BUNDLE}/Contents/Library/SystemExtensions")
file(GLOB dext_bundles LIST_DIRECTORIES true "${extensions_dir}/*.dext")
list(LENGTH dext_bundles dext_count)
if(NOT dext_count EQUAL 1)
    message(FATAL_ERROR
        "expected exactly one embedded DriverKit extension, found ${dext_count}")
endif()

list(GET dext_bundles 0 dext_bundle)
get_filename_component(dext_filename "${dext_bundle}" NAME)
string(REGEX REPLACE "\\.dext$" "" dext_filename_identifier
    "${dext_filename}")

set(dext_info_plist "${dext_bundle}/Info.plist")
if(NOT EXISTS "${dext_info_plist}")
    message(FATAL_ERROR "embedded DEXT Info.plist not found: ${dext_info_plist}")
endif()

function(read_plist_key output_variable key)
    execute_process(
        COMMAND /usr/libexec/PlistBuddy -c "Print :${key}" "${dext_info_plist}"
        RESULT_VARIABLE plist_result
        OUTPUT_VARIABLE plist_value
        ERROR_VARIABLE plist_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT plist_result EQUAL 0)
        message(FATAL_ERROR "cannot read DEXT ${key}: ${plist_error}")
    endif()
    set(${output_variable} "${plist_value}" PARENT_SCOPE)
endfunction()

read_plist_key(bundle_identifier CFBundleIdentifier)
read_plist_key(bundle_executable CFBundleExecutable)
read_plist_key(bundle_package_type CFBundlePackageType)
read_plist_key(bundle_build_version CFBundleVersion)
read_plist_key(bundle_icon_file CFBundleIconFile)
read_plist_key(usage_description OSBundleUsageDescription)
read_plist_key(pci_matches
    "IOKitPersonalities:'OCP Time Card':IOPCIPrimaryMatch")

if(NOT bundle_identifier STREQUAL "org.opentimeserver.timecard.macos.driver")
    message(FATAL_ERROR "unexpected DEXT bundle identifier: ${bundle_identifier}")
endif()
if(NOT dext_filename_identifier STREQUAL bundle_identifier)
    message(FATAL_ERROR
        "DEXT filename must match its bundle identifier: ${dext_filename}")
endif()
if(NOT bundle_executable STREQUAL bundle_identifier)
    message(FATAL_ERROR
        "unexpected DEXT executable name: ${bundle_executable}")
endif()
if(NOT bundle_package_type STREQUAL "DEXT")
    message(FATAL_ERROR "unexpected DEXT package type: ${bundle_package_type}")
endif()
if(NOT bundle_build_version STREQUAL "28")
    message(FATAL_ERROR "unexpected DEXT build: ${bundle_build_version}")
endif()
if(NOT bundle_icon_file STREQUAL "TimeCardDriver")
    message(FATAL_ERROR "unexpected DEXT icon file: ${bundle_icon_file}")
endif()
if(NOT EXISTS "${dext_bundle}/TimeCardDriver.icns")
    message(FATAL_ERROR "embedded DEXT icon not found")
endif()
if(usage_description STREQUAL "")
    message(FATAL_ERROR "DEXT usage description must not be empty")
endif()
set(expected_pci_matches
    "0x04001d9b 0x100818d4 0xa0001ad7 0x0400ad5a 0x0410ad5a")
if(NOT pci_matches STREQUAL expected_pci_matches)
    message(FATAL_ERROR "unexpected DEXT PCI match set: ${pci_matches}")
endif()
if(NOT EXISTS "${dext_bundle}/${bundle_executable}")
    message(FATAL_ERROR
        "embedded DEXT executable not found: ${dext_bundle}/${bundle_executable}")
endif()
