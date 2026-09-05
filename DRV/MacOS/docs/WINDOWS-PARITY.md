# macOS and Windows Control Center parity

This comparison covers macOS app build 67, bundled driver 28 / DriverKit ABI v12, against the
Windows Control Center in the same repository (Windows driver ABI 15).
An implemented screen does not imply that every supported board has been
physically validated.

| Workspace | macOS implementation | Remaining Windows capabilities |
| --- | --- | --- |
| Precision Clock | PHC read/set, cross timestamps, synchronization status, guarded source selection, active/configured input, supported-source mask | Trusted time-domain conversion, NTP/SyncE/Dynamic synthesis contracts, smooth/advanced adjustment, system discipline |
| Time Synchronization | Continuous bounded GNSS observation, validated receiver UTC/GPS/leap agreement and integer UTC/TAI epochs, freshness and leap guards, evidence export, read-only time-service diagnostics, tested pure slew controller | Verified PHC epoch association and precision offset series, actual privileged clock writer, exclusive ownership transfer and production discipline |
| GNSS | Bounded UBX polls, mixed UBX/NMEA/RTCM3 decoding, receiver summary, sky map, satellite table | Persistent receiver configuration, survey-in/fixed-position controls, direct receiver session management |
| Serial laboratory | Hardware capture, native serial preview, protocol/search/error filters, display pause, raw views, offline replay, TXT/CSV/JSON/binary export | Persistent generic serial sessions, full line/flow-control settings, arbitrary send workflow, TX/RX session timeline, interrupt-backed capture |
| SMA and LEDs | Capability-aware routes, guarded setters, GNSS/SMA LED policies, readback | ART peripheral path, exhaustive board-specific validation |
| Sensors | LM75B, SHT3x, compensated ICP-10100, temperature charts, BNO055/BNO08x fusion, 3D orientation, calibration, low-rate vibration/RMS and motion exports; live BNO08x verified | Physical BNO055 validation, BME/BMP280, INA219 rail measurements, other board routes |
| Telemetry Studio | Sampling histogram, median/p95/p99, one-hour history, GNSS/temperature plots, low-rate vibration trend, point inspection, pause, six-hour bounded recording, CSV/JSON | Trusted PHC offset series, high-rate vibration measurement and spectral analysis |
| Timing and FPGA engines | Version-gated PPS input/output state and confirmed enable/width/polarity/delay controls with stale-state checks and verified recovery; live PPS 1.2 readouts on `.141`; four frequency counters on revision-02 LitePCIe; timing export and readiness catalog | Physical PPS setting-change and recovery validation, PPS error acknowledgement, classic/ART/ADVA counter contracts, signal-generator/timestamp-channel ABI and controls, IRIG/DCF/ToD settings |
| Atomic clock | Dedicated SA53 identity/telemetry/alarms, checksummed C3 transport, 15 bounded setters, mode checks, confirmed JamSync/load/store/acknowledge, readback and JSON export | Physical validation of clock-changing operations; ART mRO-50; persistent sessions |
| Profiles | Native versioned JSON capture/import/edit/export, persistent immutable profile library, live preview, confirmed apply, per-setting verification, guarded rollback and recovery reports; built-in readiness planner | Windows XML migration, GNSS/oscillator/generator/ToD settings as typed APIs become available |
| Diagnostics | Read-only self-test including clock-source and gated frequency APIs, session log, structured exports, support ZIP with profile evidence | Extended tests for future FPGA/peripheral APIs |
| Maintenance | Driver lifecycle, exact PCI matching, multi-card app selection | Protected SPI flash update, EEPROM identity, notarized release packaging |

## Validation scope

- Build 67 adds seven Swift-suite coverage alongside seven CTest suites and
  a universal signed app build. App 67 and CLI 24 are installed and verified
  on `.141`, with driver 28 unchanged. Live session start, evidence export,
  competing-operation blocking and Stop were checked; SA53 polling worked
  again after Stop. CLI 24 no longer labels raw PHC reads as UTC.
  Time qualification remains blocked on `.141`:
  both GNSS UARTs returned no identity data at five baud rates, and PHC UTC
  metadata remains invalid. Apple Network Time was left on. No clock steering,
  SA53, PHC epoch, PPS or persistent receiver changes were made.
  See [TRUSTED-TIME.md](TRUSTED-TIME.md).
- Build 66 adds PPS transaction and Swift model tests. Seven CTest and six
  Swift suites pass, with universal app/driver builds and strict signatures.
  App 66 and CLI 23 are installed on `.141`; the approved restart activated
  the hash-verified, actually bound driver 28 / ABI v12. Both PPS 1.2 cores
  returned live data; the output editor's validation and confirmation were
  checked without applying a change. Clock sync recovered, and sensor, SMA,
  LED, UART, SA53, and motion checks passed within the documented limitations.
  No physical PPS settings were changed. See [PPS-ENGINES.md](PPS-ENGINES.md).
- Build 65 completes live BNO08x validation on the actual bound driver 27:
  39/40 polls contained quaternion and linear acceleration, with one startup
  sample lacking those components. Remote GUI checks verified the 3D model,
  vibration chart/RMS, populated CSV export, and Stop. Low fusion calibration
  remains explicitly marked unreliable. No oscillator settings were changed.
  Saved profiles were captured, saved, reloaded, staged and previewed with
  zero changes; apply and removal confirmations were checked and cancelled.
  See [BUILD65-VALIDATION.md](BUILD65-VALIDATION.md).
- Build 64 added protocol/model tests and read-only SA53 hardware validation.
  The installed SA53 returned atomic lock, but no PPS input or disciplining
  lock. Older firmware rejects eight optional queries, which remain unavailable.
  Its pending driver replacement was resolved during build 65 validation.
  See [PERIPHERALS.md](PERIPHERALS.md).
- Host tests cover register layouts, driver safety, Swift ABI models, mixed
  protocol framing, corruption and truncation, replay size limits, export
  encoding, statistics, GSV epoch replacement, and recording boundaries.
- ABI v10 tests exercise the actual shared timing transaction code with mock
  MMIO: no access on unsupported layouts, boundary checks, source masks, invalid
  inputs, stale expected state, no-write idempotence, readback failure, and
  successful/failed rollback. Swift tests suppress disabled, invalid, error,
  and overrun frequency readings. Physical frequency measurements on LitePCIe
  cards have not been validated.
- The app is built for both Intel and Apple Silicon. The installed test system
  is the Intel Mac Pro at `192.168.1.141` with the classic Meta PCI profile.
- GNSS chart data requires valid ToD satellite fields. Missing or invalid
  satellite counts are not rendered as zero. A raw running PHC does not prove
  that its epoch or UTC/TAI interpretation is valid.
- Profile tests cover strict JSON validation, unavailable and fixed settings,
  identity mismatches, stale/expired previews, zero-write idempotence, dependency
  order, readback failures, rollback success/failure, and external changes during
  apply. Rollback never overwrites an unknown state. The capture format is
  separate from Windows XML, and profiles exclude clock epoch and system time.
- Build 62 requires driver build 25 activation for the new timing features.
  Existing features remain compatible with ABI v7-v9. Driver activation may
  require macOS approval or restart depending on system policy.
- Replay is local decoding only. Display filters never remove retained bytes;
  binary export includes undecoded data. Live capture remains bounded by the
  existing UART capture duration and byte limits.
- Recordings are in memory until saved. Changing cards stops a recording so
  observations from different cards cannot be silently mixed.
- Build 61 local GUI checks exercised replay, checksum filtering, display pause/resume,
  and saving a filtered JSON file, then verified that file's contents. The
  Telemetry Studio layout and unavailable-data states were also inspected.
- During build 61 validation, the Intel Mac Pro's installed build and signature, active driver, PHC access,
  and five environmental readings were verified over SSH. Live remote chart
  and recording UI checks were deferred because the Mac was at its login
  screen during deployment.
- Build 62 and CLI build 21 were installed on `.141`, with recoverable backups
  of build 61 and CLI 20. macOS initially left build 24 bound to the PCI card,
  listed as `terminating for upgrade via delegate`. The authorized restart on
  September 4, 2026 completed the replacement: the bound service now reports
  ABI v10, with build 25 activated and enabled at that stage.
- Live ABI v10 checks returned configured and active PPS source `0x03`, source
  mask `0xc000003f`, core version `0x01020000`, and enabled clock control `0x01`.
  Reapplying the existing PPS source succeeded through the no-write path. A
  stale expected source was rejected with `kIOReturnBusy` (`0xe00002d5`). The
  source remained PPS throughout; changing to a different reference and live
  rollback fault injection were not tested on the physical card.
- The classic image correctly omits the frequency-counter capability. The CLI
  rejects counter access without probing optional timing registers. Five
  consecutive PHC reads advanced monotonically, with 11-12 microsecond host
  sampling windows in that short check. Five environmental readings, SMA fixed
  routes, and non-draining observation on all four UART ports remained functional. Both
  installed signatures passed verification, and the VNC port was reachable.
- Clock status was initially not synchronized after restart, then reported
  `0x01` (in sync) within approximately four minutes, with PPS still configured
  and active. The raw PHC remains boot-relative with a 1970-looking epoch; lock
  does not establish UTC validity. The logged-in GUI checks followed in builds
  63 and 65, as recorded here.
- Local build 62 GUI checks verified the Generators and Precision Clock layout,
  unavailable-data messaging, and disabled export without a connected card.
- Build 63's signed profile hardware smoke test captured five settings on the
  Mac Pro: PPS clock source and four fixed SMA routes. JSON round-trip, matching
  preview, and unchanged apply passed through a wrapper that rejects every write
  call, with zero write attempts. Full multi-setting changes and rollback remain
  mock-tested, not fault-injected on live timing hardware.
- In the logged-in Mac Pro desktop, build 63 GUI checks exercised capture,
  zero-change preview, confirmation, unchanged apply, JSON save/import, and
  apply-report export. Exported JSON was checked independently over SSH.
  Editing SMA 1 from input to output was blocked as a fixed route, with Apply
  disabled and the live input route preserved. The draft was then recaptured.
  PPS remained configured and active, clock status stayed in sync, and all five
  environmental readings remained valid. This does not establish PHC UTC validity.
- The final signed build 63 was installed and launched without replacing the
  active build 25 driver or rebooting. Intel and Apple Silicon slices and both
  bundle signatures were verified; the installed executable matched the local
  build's SHA-256. Its exported hardware self-test reported 13 passing checks
  and one gated frequency-counter check. The final signed no-write profile
  smoke test passed again after the recovery-safety refinements.

Build 63 package SHA-256:
`c118974b3c64907230ffeb496e86b09d29695aef4dd08e2c74c4cccc60ac30c0`.
Historical deployment backups in `/tmp` do not survive every reboot; retain
local release archives. Profile changes, rollback conflicts, and disconnect recovery are
covered by host fault-injection tests; physical writes remain untested by this
profile validation run.

## Next engineering priorities

1. Extend exact-image presence contracts for classic/ART/ADVA counters and
   physically validate the implemented PPS engines, then add error
   acknowledgement, signal generators, and timestamp channels. Preserve
   the ABI v10 expected-state, readback, and rollback guarantees, and physically
   validate the LitePCIe counter path before claiming production support.
2. Add persistent UART transport and receiver/mRO-50 sessions,
   preserving each board's baud and peripheral access rules.
3. Extend the validated BNO08x path to physical BNO055 validation and
   board-specific sensors; physically validate Celestica,
   ART, ADVA, and ADVA X1 cards.
4. Extend the implemented profile capture/apply/rollback workflow to new typed
   APIs and migrate Windows XML settings
   only where an equivalent hardware contract is available.
5. Validate sleep/wake, disconnect/reconnect, multi-card operation, long
   recordings, clock recovery, and signed/notarized release installation.

Full Windows feature parity and a production release remain incomplete until
these hardware and release requirements are met.
