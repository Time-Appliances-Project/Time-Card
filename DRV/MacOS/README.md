# OCP Time Card for macOS

This repository contains the first native macOS driver foundation for the OCP
Time Card. It uses PCIDriverKit, packages the driver in a SwiftUI host app, and
provides a small command-line diagnostic client.

## Current milestone

The current implementation provides:

- Exact PCI matching and board profiles for:
  - Meta/Facebook Time Card (`1d9b:0400`)
  - Celestica R4006 (`18d4:1008`)
  - Orolia/Safran ART (`1ad7:a000`)
  - upstream Linux ADVA Time Card (`ad5a:0400`)
  - upstream Linux ADVA Time Card X1 (`ad5a:0410`)
- BAR0 discovery and per-profile bounds checking
- PCI-revision and MSI-X-signature selection of the classic and repository
  LitePCIe shifted maps
- Fixed ART and ADVA common-clock resource-map handling
- PHC time read and set operations
- Bracketed card/system cross timestamps
- Version-gated clock and TOD status reads
- Capability and field-validity reporting for absent or gated registers
- A versioned, size-checked user-client ABI v2
- A host app for driver activation and removal
- `timecardctl` commands for `status`, `get`, and `set-card-from-system`

The common PHC block is available on every matched profile. ART uses its own
fixed layout and has no standard TOD block, so the driver never reads one.
ADVA profiles use the fixed common clock/TOD addresses published by current
upstream Linux, independent of their interrupt capability. The UTC, leap,
GNSS, and satellite telemetry registers are synthesis-optional. They remain
untouched until a future exact-image contract can prove that a specific FPGA
image implements them.

Current upstream Linux defines the classic Meta/Celestica map and the fixed ART
and ADVA maps. The shifted revision-02 map comes from this repository's
LitePCIe gateware and Windows/Linux support; it is not currently in upstream
Linux. The Meta/Facebook classic profile is hardware-validated on an Intel Mac
Pro. Every other profile is covered by host-side layout and safety tests but
still requires physical-card validation before a production release. Celestica
cards programmed with the generic Meta PCI identity continue to receive safe
common-PHC support, but board-specific R4006 identification will require the
future EEPROM/I2C work.

## Project layout

```text
App/       SwiftUI driver installer
CLI/       timecardctl diagnostic client, packaged as a macOS app
Driver/    PCIDriverKit extension and user client
Shared/    Stable C ABI and hardware register definitions
Tests/     Host-side register-map tests
```

## Build and test without signing

Run the portable register-map tests and build the CLI:

```sh
cmake -S . -B .build/cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build .build/cmake --parallel
ctest --test-dir .build/cmake --output-on-failure
```

On macOS, CMake places the executable in the unsigned wrapper at
`.build/cmake/timecardctl.app/Contents/MacOS/timecardctl`. The wrapper gives
the diagnostic client a bundle identifier and a place for its development
provisioning profile.

Build the same unsigned CLI app with Xcode:

```sh
xcodebuild \
  -project TimeCardMacOS.xcodeproj \
  -scheme timecardctl \
  -configuration Debug \
  -derivedDataPath .build/xcode-cli \
  ARCHS=x86_64 \
  ONLY_ACTIVE_ARCH=NO \
  CODE_SIGNING_ALLOWED=NO \
  build
```

The equivalent convenience command for the Intel Mac Pro build is
`make build-cli-app ARCHS=x86_64`.

Compile the complete app and DriverKit extension without installing them:

```sh
xcodebuild \
  -project TimeCardMacOS.xcodeproj \
  -scheme TimeCardMacOS \
  -configuration Debug \
  -derivedDataPath .build/xcode-app \
  ARCHS=x86_64 \
  ONLY_ACTIVE_ARCH=NO \
  CODE_SIGNING_ALLOWED=NO \
  build
```

The equivalent convenience command for the Intel Mac Pro build is
`make build-app ARCHS=x86_64`. It also checks that the embedded extension is
named `org.opentimeserver.timecard.macos.driver.dext`. DriverKit requires the
DEXT filename, without the `.dext` suffix, to exactly match its bundle
identifier.

An unsigned build validates the Swift, C, C++, IIG, linking, plist, and bundle
layout. It cannot activate the system extension.

## Signing requirements

Before installing the driver, configure one Apple development team for all
three targets and request these capabilities from Apple:

- DriverKit
- DriverKit PCI Transport
- System Extension installation
- DriverKit user-client access

For macOS clients, request
`com.apple.developer.driverkit.userclient-access` through Apple's System
Extension or DriverKit entitlement form. Set **UserClient Bundle IDs** to
`org.opentimeserver.timecard.macos.driver`. The portal capability named
**DriverKit Communicates with Drivers** is for iPadOS and does not authorize
the macOS user-client entitlement. A development host can activate and bind
the DEXT without user-client access, but client commands cannot open the
DriverKit user client until Apple grants it and replacement profiles include
the entitlement.

The development DriverKit PCI profile authorizes Apple's wildcard
`IOPCIPrimaryMatch` value. Use that value only in development signing
entitlements. `Driver/Info.plist` remains restricted to the supported Time
Card PCI identifiers at runtime.

A distribution DriverKit profile must authorize every PCI primary match that
will ship. Keep the entitlement and runtime personality synchronized with this
exact list:

```text
0x04001d9b 0x100818d4 0xa0001ad7 0x0400ad5a 0x0410ad5a
```

The build tests extract and compare the single `IOPCIPrimaryMatch` value in
each file against the canonical match set in `Shared/TimeCardRegisters.h`.
Extra matches, wildcards, and missing values fail the test. Using the primary
match key also prevents subsystem IDs with the same encoded value from entering
the runtime match set.

The configured identifiers are:

```text
org.opentimeserver.timecard.macos
org.opentimeserver.timecard.macos.driver
org.opentimeserver.timecard.macos.cli
```

Change them consistently if the approved App IDs use a different prefix.
The signed CLI bundle uses `CLI/timecardctl.entitlements`; place its matching
development provisioning profile at
`timecardctl.app/Contents/embedded.provisionprofile` before signing.

## First hardware bring-up

1. Verify the card and gateware on Linux using `ptp_ocp`.
2. Install the card in a Mac Pro or a compatible Thunderbolt PCIe chassis.
3. Confirm that macOS enumerates the expected PCI vendor and device ID.
4. Build and activate the signed host app.
5. Approve the extension in System Settings if prompted.
6. Confirm activation with `systemextensionsctl list`.
7. Run `timecardctl.app/Contents/MacOS/timecardctl status`, followed by the
   same executable with `get`.
8. Compare the reported card time, core versions, and available status fields
   with the Linux reference setup.

Do not use `set-card-from-system` until read-only status and time access have
been validated on the target card. This command copies raw macOS
`CLOCK_REALTIME` seconds into the card. It does not set the macOS system clock,
and it does not yet apply a UTC/TAI correction.

## Known limitations

- Cross timestamps currently bracket the MMIO clock read with macOS realtime
  samples. PCIe PTM support is not implemented.
- UARTs, PPS interrupts, external timestamp inputs, SMA routing, I2C, SPI
  flash, frequency counters, and signal generators are not implemented.
- Optional UTC, leap, GNSS, and satellite registers are deliberately gated
  until an exact per-card FPGA image contract is implemented.
- The driver does not create a Linux-style `/dev/ptpN` device.
- No daemon currently disciplines the macOS system clock.
- `timecardctl` refuses ambiguous access when multiple cards are present;
  explicit per-card selection is not implemented yet.
- Sleep, wake, Thunderbolt disconnect, all non-Meta profiles, and multiple-card
  behavior require physical hardware testing.

See [ROADMAP.md](docs/ROADMAP.md) for the staged implementation plan.
