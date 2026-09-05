# Time Card Drivers

Platform-specific drivers and build instructions are kept in separate
directories:

- [Linux](Linux/README.md) - Linux PTP/PHC driver, Rust/Relm4 Control Center, kernel
  build files, and SPI patches.
- [macOS](MacOS/README.md) - native SwiftUI Control Center, DriverKit PCI
  extension, command-line tools, live hardware telemetry, and guarded controls.
  See the [current screenshots](MacOS/docs/screenshots/README.md) and
  [Windows parity status](MacOS/docs/WINDOWS-PARITY.md).
- [Windows](Windows/README.md) - Windows KMDF driver, control tool, installer,
  Device Manager hierarchy, and icon assets.
