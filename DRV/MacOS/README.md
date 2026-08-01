# OCP Time Card for macOS

This repository contains the first native macOS driver foundation for the OCP
Time Card. It uses PCIDriverKit, packages the driver in a SwiftUI host app, and
provides a small command-line diagnostic client.

## Current milestone

The current implementation provides:

- PCI matching for the Meta/Facebook Time Card (`1d9b:0400`) and compatible
  Celestica card (`18d4:1008`)
- BAR0 discovery and bounds checking
- MSI versus MSI-X register-layout selection
- PHC time read and set operations
- Bracketed card/system cross timestamps
- Clock, TOD, UTC, leap, GNSS, and satellite status reads
- A versioned, size-checked user-client ABI
- A host app for driver activation and removal
- `timecardctl` commands for `status`, `get`, and `set-system`

The Orolia ART card is intentionally not matched yet because it uses a
different resource map.

## Project layout

```text
App/       SwiftUI driver installer
CLI/       timecardctl diagnostic client
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

Compile the complete app and DriverKit extension without installing them:

```sh
xcodebuild \
  -project TimeCardMacOS.xcodeproj \
  -scheme TimeCardMacOS \
  -configuration Debug \
  -derivedDataPath .build/xcode-app \
  CODE_SIGNING_ALLOWED=NO \
  build
```

An unsigned build validates the Swift, C, C++, IIG, linking, plist, and bundle
layout. It cannot activate the system extension.

## Signing requirements

Before installing the driver, configure one Apple development team for all
three targets and request these capabilities from Apple:

- DriverKit
- DriverKit PCI Transport
- System Extension installation
- DriverKit user-client access

The configured identifiers are:

```text
org.opentimeserver.timecard.macos
org.opentimeserver.timecard.macos.driver
org.opentimeserver.timecard.macos.cli
```

Change them consistently if the approved App IDs use a different prefix.

## First hardware bring-up

1. Verify the card and gateware on Linux using `ptp_ocp`.
2. Install the card in a Mac Pro or a compatible Thunderbolt PCIe chassis.
3. Confirm that macOS enumerates the expected PCI vendor and device ID.
4. Build and activate the signed host app.
5. Approve the extension in System Settings if prompted.
6. Confirm activation with `systemextensionsctl list`.
7. Run `timecardctl status`, followed by `timecardctl get`.
8. Compare the reported time and PPS output with the Linux reference setup.

Do not use `set-system` until read-only status and time access have been
validated on the target card.

## Known limitations

- Cross timestamps currently bracket the MMIO clock read with macOS realtime
  samples. PCIe PTM support is not implemented.
- UARTs, PPS interrupts, external timestamp inputs, SMA routing, I2C, SPI
  flash, frequency counters, and signal generators are not implemented.
- The driver does not create a Linux-style `/dev/ptpN` device.
- No daemon currently disciplines the macOS system clock.
- Sleep, wake, Thunderbolt disconnect, and multiple-card behavior require
  physical hardware testing.

See [ROADMAP.md](docs/ROADMAP.md) for the staged implementation plan.
