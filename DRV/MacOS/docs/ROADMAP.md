# macOS implementation roadmap

## Milestone 1: core PHC access

Status: implemented in source, awaiting signed hardware validation.

- Driver activation app
- PCI matching and BAR validation
- MSI and MSI-X layout discovery
- Clock and TOD status
- PHC read and set
- Bracketed cross timestamp
- Versioned user-client ABI
- CLI and host-side tests

Exit criteria:

- The driver starts on both older MSI and newer MSI-X gateware.
- `timecardctl status` returns plausible versions and GNSS state.
- Repeated `timecardctl get` calls return monotonic card timestamps.
- No driver crash occurs during open, close, sleep, wake, or disconnect.

## Milestone 2: serial and event access

- GNSS, GNSS2, atomic-clock, and NMEA 16550 UARTs
- MSI-X interrupt allocation and dispatch
- PPS and external timestamp events
- Asynchronous user-client notifications
- Cable delay and UTC/TAI configuration

## Milestone 3: timing configuration

- Clock-source selection
- SMA input and output routing
- IRIG-B and DCF configuration
- Periodic outputs and signal generators
- Frequency counters and timestamp channels
- SwiftUI monitoring and configuration views

## Milestone 4: system integration

- Privileged `launchd` service
- Offset and drift estimator
- Controlled macOS clock discipline
- Leap-second handling
- Conflict detection for other system time services
- Long-duration holdover and recovery tests

## Milestone 5: maintenance and precision

- PCIe PTM feasibility and implementation
- I2C EEPROM and board identity
- SPI gateware update with recovery protections
- Multiple Time Cards
- Celestica validation
- Orolia ART resource-map support
- Signed, notarized release packaging
