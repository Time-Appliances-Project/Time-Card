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
for the GNSS, GNSS2, MAC/atomic-clock, and NMEA ports are implemented in ABI v8.
Bounded UART writes, safe UBX poll request support, and bounded poll-response
capture are implemented in ABI v9 and hardware-validated on the classic Meta
profile. Continuous app-side and CLI polling capture are implemented on top of
the bounded ABI v8 read path.

- SMA input and output route query/set, with fixed-route fallback for older
  FPGA images
- GNSS, GNSS2, atomic-clock, and NMEA 16550 UART polling access
- Bounded UART writes, guarded UBX poll requests, and poll-response capture
- Expanded read-only UBX receiver polls and telemetry summaries
- RTCM3 CRC24Q validation in the mixed receiver stream decoder
- Receiver stream checksum health and RTCM3 correction summary cards
- Read-only Windows built-in configuration profile planner on macOS
- RTCM3 correction message names for common MSM, ephemeris, and base-station frames
- Native SwiftUI subsystem topology diagram for Windows parity
- FPGA core readiness matrix with support-bundle export
- Receiver validation checklist for the Windows GNSS workflow
- Windows-style LED policy preset cards with guarded verified readback
- Board-variant compatibility matrix with support-bundle export
- Continuous bounded polling capture in the SwiftUI app and CLI, with live
  app progress, raw capture save, and support-bundle export
- Mixed UBX, NMEA, and RTCM3 receiver stream timeline
- Expanded NMEA summaries for GLL, VTG, GNS, GST, TXT, HDT, and THS
- Receiver stream summary dashboard for decoded UBX and NMEA captures
- Receiver satellite signal table and support-bundle CSV export
- Receiver satellite sky map from UBX NAV-SAT and NMEA GSV records
- Receiver console with filtering, display pause, raw byte formats, bounded
  offline replay, full binary preservation, and TXT/CSV/JSON message exports
- Sampling-window histogram and percentiles, selectable history ranges,
  valid GNSS satellite and temperature charts, sample inspection, and
  independent bounded session recording with CSV/JSON export
- Native macOS file exports for telemetry JSON/CSV, diagnostics, self-test
  reports, and session-log text/JSON
- Interrupt-backed stream capture
- MSI-X interrupt allocation and dispatch
- PPS and external timestamp events
- Asynchronous user-client notifications
- Cable delay and UTC/TAI configuration

## Milestone 3: timing configuration

Status: partially implemented in ABI v10. Clock-source query/set and guarded
frequency-counter controls have SwiftUI and CLI interfaces, shared transaction
tests, expected-state checks, and verified rollback. Counter presence is
restricted to the Meta/Celestica revision-02 LitePCIe layout; classic/ART/ADVA
images remain gated until exact-image presence is established.

- Clock-source selection, implemented with supported-source masks and confirmation
- IRIG-B and DCF configuration
- Periodic outputs and signal generators
- Frequency counters, implemented for the validated LitePCIe contract
- Timestamp channels
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

The current application comparison and next implementation priorities are
tracked in [WINDOWS-PARITY.md](WINDOWS-PARITY.md).
