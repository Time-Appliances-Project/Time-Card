# OCP Time Card for macOS

This repository contains the native macOS driver and Control Center for the OCP
Time Card. It uses PCIDriverKit, packages the driver in a SwiftUI monitoring
app, and provides a small command-line diagnostic client.

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
- A versioned, size-checked user-client ABI v9
- SMA connector query and guarded route setting through DriverKit, including
  Linux-compatible fixed-route handling for FPGA images without writable SMA
  routing GPIO
- Board LED query and guarded RGB LED setting for the SMA LEDs through the
  Time Card I2C LED controller
- GNSS LED policy application through `timecardctl`, using ToD GNSS telemetry
  when valid and a clock-sync fallback while the exact GNSS image contract is
  still unavailable
- Safe I2C diagnostics through DriverKit: AXI IIC status, zero-byte address
  probes, bounded reads, and guarded mux query/set
- Optional ToD telemetry summary reads for UTC status, leap status, GNSS fix,
  and satellite counts when the active FPGA image exposes that register span
- Environmental sensor query for LM75B board temperature sensors, SHT3x
  humidity/temperature, ICP-10100 pressure/temperature with CRC and OTP
  compensation, plus BNO08x SHTP-header and BNO055 identity probes
- Bounded 16550 UART observe, baud-rate configure, read, and write operations
  for GNSS, GNSS2, MAC/atomic-clock, and NMEA ports on profiles that expose the
  UART register bank
- A native SwiftUI Control Center with:
  - driver activation, update, and removal
  - live card discovery and explicit multi-card selection
  - PCI identity, board profile, register-map, BAR, and capability views
  - raw card time, clock status, ToD status, UTC/leap summary, GNSS fix
    summary, satellite counts, and cross-timestamp telemetry
  - live SMA connector status and route controls when the active FPGA image
    exposes writable routing
  - live Sensors and IMU workspace with environmental metric cards, board
    temperature zones, pressure compensation, temperature history, raw sensor
    inventory, and IMU bring-up status
  - a Time Card hardware UART workspace for port selection, baud-rate
    configuration, non-draining line-status observation, bounded reads, guarded
    UBX poll writes, bounded poll-response capture, continuous polling capture
    with live progress, copy and raw capture export helpers, handoff into the
    NMEA and UBX decoder labs, a mixed UBX/NMEA/RTCM3 receiver timeline, and
    receiver summary rollups for firmware, fix, satellites, timing, position,
    RTCM3 correction names, a satellite sky map, and per-satellite signal
    records
  - NMEA summaries for GGA, RMC, GSA, GSV, GLL, VTG, GNS, ZDA, GST, TXT, HDT,
    and THS receiver sentences
  - read-only UBX polls and summaries for receiver version, monitor hardware,
    navigation status, PVT, DOP, receiver clock, GPS/UTC/leap-second time,
    satellite records, survey-in, and time-pulse telemetry
  - a read-only Windows built-in profile planner for GNSS disciplined,
    External PPS, PTP disciplined, NMEA service, and lab timing output profiles,
    with live macOS readiness and support-bundle export
  - a rolling bracketed sampling-window chart
  - clear user-client entitlement and restart diagnostics
- `timecardctl` commands for `status`, `get`, `set-card-from-system`, `sma`,
  `sma-set`, `led`, `led-set`, `led-sma-auto`, `led-gnss-auto`, `led-auto`,
  `i2c-status`, `i2c-scan`, `i2c-read`, `i2c-mux`, `sensors`,
  `uart-observe`, `uart-config`, `uart-read`, `uart-capture`,
  `uart-write-hex`, and `ubx-poll-read`, including BNO08x SHTP-header and
  BNO055 chip-ID probe detail

The common PHC block is available on every matched profile. ART uses its own
fixed layout and has no standard TOD block, so the driver never reads one.
ADVA profiles use the fixed common clock/TOD addresses published by current
upstream Linux, independent of their interrupt capability. The UTC, leap,
GNSS, and satellite telemetry registers are synthesis-optional. The driver
reads them only when the mapped BAR covers the optional ToD telemetry span and
marks each field valid independently.

Current upstream Linux defines the classic Meta/Celestica map and the fixed ART
and ADVA maps. The shifted revision-02 map comes from this repository's
LitePCIe gateware and Windows/Linux support; it is not currently in upstream
Linux. The Meta/Facebook classic profile is hardware-validated on an Intel Mac
Pro, including ABI v9 status, SMA fixed-route readback, SMA/GNSS LED policy
application, I2C diagnostics, optional ToD GNSS/UTC summary telemetry,
LM75B/SHT3x/ICP-10100 environmental sensor telemetry with compensated
ICP-10100 pressure, bounded UART observe/configure/read smoke checks on all
four Time Card UART ports, and bounded UART write smoke checks on the primary
GNSS receiver port. BNO08x SHTP-header probing is wired for the Celestica mux
route and reported separately from decoded fused motion.
Every other profile is covered by host-side layout and safety tests but still
requires physical-card validation before a production release.
Celestica cards programmed with the generic Meta PCI identity continue to
receive safe common-PHC support, and their environmental sensor population is
auto-probed by mux branch rather than relying only on PCI identity.

## Project layout

```text
App/       SwiftUI Control Center and driver lifecycle manager
CLI/       timecardctl diagnostic client, packaged as a macOS app
Driver/    PCIDriverKit extension and user client
Shared/    Stable C ABI and hardware register definitions
Tests/     Register-map, safety, bundle, and Swift model tests
```

## Build and test without signing

Run the portable driver tests, build the CLI, and run the Swift Control Center
model tests:

```sh
make check
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
Card PCI identifiers at runtime. When manually signing development builds,
extract the entitlement dictionary from the provisioning profile and sign the
DEXT with that profile-derived plist. A minimal hand-written DriverKit
entitlement file can pass `codesign` but still fail DriverKit's authenticated
open path when macOS stages the system extension.

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

For repeatable development installs, the host app accepts
`--activate-driver`. Launching `/Applications/TimeCardMacOS.app` with this
argument submits the DriverKit activation or replacement request for the
embedded `org.opentimeserver.timecard.macos.driver` extension. macOS may still
require approval in System Settings or a reboot before the PCI service binds to
the new driver build.

## First hardware bring-up

1. Verify the card and gateware on Linux using `ptp_ocp`.
2. Install the card in a Mac Pro or a compatible Thunderbolt PCIe chassis.
3. Confirm that macOS enumerates the expected PCI vendor and device ID.
4. Build and activate the signed host app.
5. Approve the extension in System Settings if prompted.
6. Confirm activation with `systemextensionsctl list`.
7. Open the Control Center and verify that the Overview, Precision Clock, and
   Hardware pages show the selected card.
8. Run `timecardctl.app/Contents/MacOS/timecardctl status`, followed by the
   same executable with `get`, `sma`, `led`, `led-auto`, `i2c-status`,
   `i2c-scan`, `i2c-read`, `i2c-mux`, `sensors`, `uart-observe`,
   `uart-config`, `uart-read`, `uart-capture`, `uart-write-hex`, and
   `ubx-poll-read mon-ver|mon-hw|mon-hw2|nav-status|nav-pvt|nav-dop|nav-clock|nav-timegps|nav-timeutc|nav-timels|nav-sat|nav-svin|tim-tp`.
9. Compare the reported card time, core versions, and available status fields
   with the Linux reference setup.

Do not use `set-card-from-system` until read-only status and time access have
been validated on the target card. This command copies raw macOS
`CLOCK_REALTIME` seconds into the card. It does not set the macOS system clock,
and it does not yet apply a UTC/TAI correction.

## Known limitations

- Cross timestamps currently bracket the MMIO clock read with macOS realtime
  samples. PCIe PTM support is not implemented.
- UART interrupt-backed streaming, PPS interrupts, external timestamp inputs
  beyond SMA route readback, arbitrary raw I2C writes, SPI flash, frequency
  counters, and signal generators are not implemented.
- I2C support intentionally exposes diagnostics, probes, bounded reads, and mux
  control only. General writes remain gated until the Control Center has device
  profiles, paging rules, and write-safety warnings.
- Sensor support currently reports LM75B, SHT3x, ICP-10100 telemetry with
  compensated pressure when OTP calibration is available, BNO08x SHTP-header
  presence, and BNO055 identity presence. BME/BMP280, INA219 rail telemetry,
  and fused IMU decoding remain planned on top of the same mux-aware sensor ABI.
- SMA routing is implemented for the classic, shifted LitePCIe, ART, and ADVA
  register maps. FPGA images with absent or fixed route GPIO report fixed
  direction and fixed function, and writable changes are rejected.
- SMA LED policy is implemented for boards that use the Xilinx AXI IIC LED
  controller path: Meta/Facebook, Celestica/R4006, ADVA, and ADVA X1. ART LED
  support remains disabled until its OpenCores I2C path is ported.
- GNSS LED policy currently uses raw ToD GNSS telemetry when the driver marks
  those fields valid. Otherwise GNSS1 falls back to the card clock sync bit,
  and GNSS2 reports status unknown unless a future second-receiver source is
  added.
- Optional UTC, leap, GNSS, and satellite registers are guarded by BAR span and
  validity bits. Receiver stream preview is available through bounded UART
  reads when the FPGA exposes 16550 ports. Safe UBX poll writes are available
  through the UART workspace. The decoder can show mixed UBX, NMEA, and RTCM3
  receiver traffic, validate RTCM3 CRC24Q checksums, summarize stream checksum
  health, report RTCM3 correction message types, and summarize UBX NAV-SAT and
  NMEA GSV satellite records in a sky map and signal table, while
  interrupt-backed continuous streaming and guarded persistent u-blox
  configuration remain planned.
- The Control Center labels card time as raw and does not calculate a card to
  macOS offset until the driver exposes a trusted UTC-to-TAI contract.
- The driver does not create a Linux-style `/dev/ptpN` device.
- No daemon currently disciplines the macOS system clock.
- `timecardctl` refuses ambiguous access when multiple cards are present;
  explicit per-card selection is not implemented yet.
- Sleep, wake, Thunderbolt disconnect, all non-Meta profiles, and multiple-card
  behavior require physical hardware testing.

See [ROADMAP.md](docs/ROADMAP.md) for the staged implementation plan.
