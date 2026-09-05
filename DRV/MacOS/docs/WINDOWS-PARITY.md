# macOS and Windows Control Center parity

This comparison covers macOS app build 61, DriverKit ABI v9, against the
Windows Control Center in the same repository (Windows driver ABI 15).
An implemented screen does not imply that every supported board has been
physically validated.

| Workspace | macOS implementation | Remaining Windows capabilities |
| --- | --- | --- |
| Precision Clock | PHC read/set, cross timestamps, synchronization status | Trusted time-domain conversion, source selection, smooth/advanced adjustment, system discipline |
| GNSS | Bounded UBX polls, mixed UBX/NMEA/RTCM3 decoding, receiver summary, sky map, satellite table | Persistent receiver configuration, survey-in/fixed-position controls, direct receiver session management |
| Serial laboratory | Hardware capture, native serial preview, protocol/search/error filters, display pause, raw views, offline replay, TXT/CSV/JSON/binary export | Persistent generic serial sessions, full line/flow-control settings, arbitrary send workflow, TX/RX session timeline, interrupt-backed capture |
| SMA and LEDs | Capability-aware routes, guarded setters, GNSS/SMA LED policies, readback | ART peripheral path, exhaustive board-specific validation |
| Sensors | LM75B, SHT3x, compensated ICP-10100, BNO presence probes, live temperature charts | BME/BMP280, INA219 rail measurements, fused BNO055/BNO08x motion and vibration |
| Telemetry Studio | Sampling histogram, median/p95/p99, one-hour history, GNSS/temperature plots, point inspection, pause, six-hour bounded recording, CSV/JSON | PHC offset and vibration series when validated data is available |
| Timing and FPGA engines | Capability and core-readiness catalog | Generator/counter/timestamp-channel ABI and controls, PPS/IRIG/DCF/ToD settings, exact-image contracts |
| Atomic clock | Bounded MAC UART access | Dedicated SA53 and ART mRO-50 telemetry and configuration |
| Profiles | Built-in profile readiness planner | Capture/import/apply/rollback through typed, verified driver setters |
| Diagnostics | Read-only self-test, session log, structured exports, support ZIP | Extended tests for new FPGA/peripheral APIs |
| Maintenance | Driver lifecycle, exact PCI matching, multi-card app selection | Protected SPI flash update, EEPROM identity, notarized release packaging |

## Build 61 validation scope

- Host tests cover register layouts, driver safety, Swift ABI models, mixed
  protocol framing, corruption and truncation, replay size limits, export
  encoding, statistics, GSV epoch replacement, and recording boundaries.
- The app is built for both Intel and Apple Silicon. The installed test system
  is the Intel Mac Pro at `192.168.1.141` with the classic Meta PCI profile.
- GNSS chart data requires valid ToD satellite fields. Missing or invalid
  satellite counts are not rendered as zero. A raw running PHC does not prove
  that its epoch or UTC/TAI interpretation is valid.
- New features use existing bounded driver access. They do not require a
  driver ABI change or another system-extension activation.
- Replay is local decoding only. Display filters never remove retained bytes;
  binary export includes undecoded data. Live capture remains bounded by the
  existing UART capture duration and byte limits.
- Recordings are in memory until saved. Changing cards stops a recording so
  observations from different cards cannot be silently mixed.
- Local GUI checks exercised replay, checksum filtering, display pause/resume,
  and saving a filtered JSON file, then verified that file's contents. The
  Telemetry Studio layout and unavailable-data states were also inspected.
- The Intel Mac Pro's installed build and signature, active driver, PHC access,
  and five environmental readings were verified over SSH. Live remote chart
  and recording UI checks remain pending because the Mac was at its login
  screen during deployment.

## Next engineering priorities

1. Establish validated FPGA image contracts and typed setters for clock source,
   PPS engines, signal generators, and frequency counters. Add round-trip
   readback tests before enabling hardware controls.
2. Add persistent UART transport and dedicated receiver/atomic-clock sessions,
   preserving each board's baud and peripheral access rules.
3. Complete fused IMU and board-specific sensor support, followed by physical
   validation on Celestica, ART, ADVA, and ADVA X1 cards.
4. Implement versioned profile capture/apply/rollback on those typed APIs.
5. Validate sleep/wake, disconnect/reconnect, multi-card operation, long
   recordings, clock recovery, and signed/notarized release installation.

Full Windows feature parity and a production release remain incomplete until
these hardware and release requirements are met.
