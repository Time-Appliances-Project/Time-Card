# macOS implementation roadmap

## Milestone 1: core PHC access

Status: implemented and signed-hardware validated on the classic Meta profile;
physical validation of the remaining profiles is pending.

- Driver activation app
- Exact PCI matching, board profiles, and BAR validation
- Classic, repository LitePCIe shifted, ART, and ADVA layout selection
- Capability and field-validity reporting for profile-specific blocks
- Clock and TOD status
- PHC read and set
- Bracketed cross timestamp
- Versioned user-client ABI
- Native SwiftUI read-only Control Center with multi-card discovery,
  capability-aware status, and sampling-window history
- SMA route readback for fixed-route classic Meta gateware
- CLI and host-side tests

Exit criteria:

- The driver starts on both older MSI and newer MSI-X gateware.
- `timecardctl status` returns plausible versions and only supported status
  fields.
- Repeated `timecardctl get` calls return monotonic card timestamps.
- No driver crash occurs during open, close, sleep, wake, or disconnect.
- The common PHC passes read-only validation on Celestica, ART, ADVA, and ADVA
  X1 hardware without touching absent or synthesis-optional registers.

## Milestone 2: serial and event access

Status: partially implemented. SMA route control moved here from milestone 3
and is implemented in ABI v3. Bounded UART observe/configure/read operations
for the GNSS, GNSS2, MAC/atomic-clock, and NMEA ports are implemented in ABI v8
and hardware-validated on the classic Meta profile.

- SMA input and output route query/set, with fixed-route fallback for older
  FPGA images
- GNSS, GNSS2, atomic-clock, and NMEA 16550 UART polling access
- UART writes and interrupt-backed stream capture
- MSI-X interrupt allocation and dispatch
- PPS and external timestamp events
- Asynchronous user-client notifications
- Cable delay and UTC/TAI configuration

## Milestone 3: timing configuration

- Clock-source selection
- IRIG-B and DCF configuration
- Periodic outputs and signal generators
- Frequency counters and timestamp channels
- SwiftUI configuration views; the read-only monitoring foundation is in
  milestone 1

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
- Physical Celestica, Orolia ART, ADVA, and ADVA X1 validation
- Board-specific R4006, ART, and ADVA peripheral support
- Signed, notarized release packaging
