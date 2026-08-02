# OCP Time Card Control Center

The Time Card Control Center is a dependency-free Windows desktop dashboard
for the OCP Time Card driver. It uses the public versioned IOCTL ABI directly;
it does not shell out to `timecardctl` or scrape Device Manager.

Driver 1.42 / ABI 15 recognizes the Meta/Facebook, Celestica, and Orolia/Safran
ART profiles from the Linux driver. The Celestica workspace uses its actual
LM75B, SHT3x, ICP-10100, and BNO08x population rather than presenting the
Meta-only BME280 and INA219 devices. On ART, the application switches to a
native mRO-50 FPGA-bridge workspace, probes all 24c08 banks at `0x50-0x57`,
uses the fixed ART SMA map, and labels the absent ToD/NMEA, secondary-GNSS,
sensor-mux, and RGB-LED capabilities as not implemented or not fitted.
The ART GNSS console preserves the baud selected by gateware until the operator
explicitly chooses a new divisor.

![Control Center overview](../assets/timecard-control-center.png)

| Precision clock | GNSS and sky map | Atomic clock |
| --- | --- | --- |
| ![Precision clock workspace](../assets/timecard-control-center-clock.png) | ![GNSS workspace](../assets/timecard-control-center-gnss.png) | ![Atomic clock workspace](../assets/timecard-control-center-atomic.png) |
| UART and NMEA | SMA connectors | Generators and frequency |
| ![UART and NMEA workspace](../assets/timecard-control-center-nmea.png) | ![SMA connector workspace](../assets/timecard-control-center-sma.png) | ![Timing generator workspace](../assets/timecard-control-center-timing.png) |
| Sensors and IMU | I2C and status LEDs | Subsystem map |
| ![Celestica sensors and compact 3D IMU workspace](../assets/timecard-control-center-sensors.png) | ![I2C and LED workspace](../assets/timecard-control-center-i2c.png) | ![Subsystem workspace](../assets/timecard-control-center-subsystems.png) |
| Telemetry Studio | Profiles and self-test | FPGA SPI flash |
| ![Telemetry Studio workspace](../assets/timecard-control-center-telemetry.png) | ![Profiles and self-test workspace](../assets/timecard-control-center-operations.png) | ![FPGA SPI-flash firmware update workspace](../assets/timecard-control-center-flash.png) |

## Current capabilities

- Live PHC time, system cross-timestamp, offset, and sampling-window display,
  with rolling 200-second offset histories and 60-second uncertainty histories.
- A live, clickable source-to-system topology that evaluates the PCIe
  controller, GNSS receiver, ToD engine, SA53, PHC, SMA fabric, I2C management
  bus, and Windows UTC as one health path. The four SMA endpoints occupy a
  dedicated, separated I/O lane so their direction and route state remain
  readable beside the core timing flow. Highlighted components open the
  appropriate corrective workspace.
- Clock engine, synchronization, PCIe layout, BAR, interrupt status, and a
  guarded selector for the documented FPGA clock sources. Synthesis-dependent
  NTP, SyncE, and dynamic sources remain disabled until their exact-image
  contract is active.
- Decoded GNSS fix and seen/locked satellite counts from the ToD engine.
- Direct u-blox receiver discovery and UBX telemetry, including model,
  firmware/protocol, fix, UTC, position accuracy, visible/used satellites,
  constellation counts, average carrier-to-noise level, and a vendor-style
  polar sky map with per-satellite elevation, azimuth, signal strength,
  constellation, tracking quality, and solution-use state. Compatible
  configuration-database receivers expose navigation rate and platform model,
  constellation selection, TP1 timing, and UART 1 message-rate controls.
  F9 timing receivers additionally expose survey-in/fixed-position mode,
  dual-band RF/antenna health, GPS time, UTC, and leap-second event telemetry.
- Guarded one-shot synchronization in both directions between system UTC and
  the Time Card PHC.
- A serial laboratory with streaming u-blox and NMEA decoders, dynamically
  enumerated Windows COM ports, generic-port parity/data/stop/handshake/DTR/RTS
  settings, line endings, live filtering, RX/TX counters, display pause without
  receive loss, bounded capture, offline replay, and text, CSV, JSON, or binary
  export. Auto/ASCII/hexadecimal/decimal/binary raw views remain available.
- FPGA NMEA sentence-generator enable, baud, and polarity configuration with
  one-click synchronized UART monitoring.
- A dedicated Microchip MAC-SA53 workspace with live identity, physics-lock,
  alarm, temperature, supply, runtime, phase, and steering telemetry. It can
  configure digital tuning, analog-tuning mode, PPS source/offset/width/cable
  delay, discipline and phase-metering modes, loop time constants, lock
  thresholds, PPS quantization correction, and the time-of-day counter.
- Four-connector SMA routing with input/output menus, named FPGA timing
  signals, fixed-direction detection, raw-map readback, and output warnings.
- A dedicated timing workspace for four periodic signal generators and four
  frequency counters, with frequency, duty, phase, active-high/active-low
  polarity, phase-aligned or exact future PHC/TAI start, repeat count, cable
  delay, integration-window, sticky error/time-jump state, and direct SMA
  routing controls, plus bounded completion/error/time-jump event reads.
- A dedicated FPGA Engines workspace with profile-gated, read-back-verified
  register controls for PPS master/slave, IRIG-B/G input/output, DCF77
  input/output, and the NMEA/UBX/TSIP/ESIP/PFEC ToD parser. ABI 15 adds a
  six-channel timestamp laboratory, static core inventory, smooth and advanced
  clock controls, ToD Master UTC metadata, and a guarded exact-image contract
  panel. Missing or locked fields remain visibly unavailable instead of being
  probed speculatively.
- Enumeration of every connected Time Card, with an explicit active-card
  selector, rescan, stable serial/path identity, hot-plug recovery, and
  per-card selection persistence.
- A guarded I2C workbench with AXI IIC health, known-device probing, full 7-bit
  discovery, EEPROM/register presets and paging, adjustable timeout,
  hex/ASCII/decimal/binary views, decoded transaction diagnostics, the unique
  card serial, and direct PCA9546A branch routing.
- Manual and automatic control of all six IS32FL3207 RGB subsystem indicators,
  including current limiting, open/short reporting, and a bounded electrical
  test that restores the previous colors.
- Live one-hertz board-specific telemetry: BME280/BMP280 and INA219 rails on
  Meta cards; three LM75B temperatures, SHT3x humidity, and ICP-10100 pressure
  on Celestica R4006 cards; plus BNO055 NDOF or BNO08x SH-2 fused IMU data.
- Runtime and persistent Device Manager subsystem-hierarchy controls.
- A guarded FPGA SPI-flash workspace with JEDEC/geometry discovery, OCPC
  vendor/device/length/CRC validation, explicit raw-image warnings, protected
  4 KiB erase and page programming, progress reporting, and full read-back
  verification.
- Copyable engineering diagnostics and a structured in-application session
  log with UTC timestamps, severity, subsystem category, card identity context,
  bounded retention, live filtering/search, and direct TXT/JSON export.
- A Telemetry Studio with timestamped PHC offset, sampling-window, locked-
  satellite, board-temperature, and gravity-compensated overall-vibration
  charts. A cross-timestamp histogram reports the median, 95th, and 99th
  percentile sampling windows. Mouse-wheel zoom, point inspection, pause,
  session recording, bounded retention, and CSV/JSON export are built in.
- Versioned configuration profiles for clock discipline, advanced NMEA,
  PPS master/slave, IRIG and DCF77 master/slave, the ToD parser, all four
  signal generators, and lab SMA timing outputs. Automatic capture includes
  only cores whose presence is safe to establish from the trusted board/image
  contract; it deliberately omits the synthesis-optional ToD Master because a
  generic reserved address or core mask cannot prove that deployed gateware
  decodes it. Explicit NMEA profiles and the NMEA workspace remain available.
  Profiles record their
  source ABI, minimum ABI, core revisions, and required FPGA-core mask; apply
  is rejected before any write when the connected image is incompatible.
  Every apply starts with a live rollback capture, previews exact typed-field
  changes, uses only the public IOCTL setters, verifies readback, and restores
  the prior state automatically if verification fails. Custom profiles can be
  captured, imported, and exported as XML, with a local audit trail. Legacy
  unversioned XML profiles remain supported and do not acquire new settings
  implicitly.
- A guided, read-only self-test covering driver/ABI, advancing PHC, lock,
  GNSS/ToD, card identity, SMA, NMEA, I2C, sensors, UART, and Device Manager
  hierarchy. Text reports and privacy-conscious ZIP support bundles include
  diagnostics, compatibility, the session log, self-test, and telemetry.
- Persistent dark, midnight-blue, and high-contrast themes; responsive compact
  navigation; keyboard shortcuts; and a complete synthetic demo mode that
  never writes to hardware.
- A subsystem capability map using the same artwork as Device Manager, direct
  links to NMEA and generator/frequency configuration, and a non-destructive
  UART activity check that reports GNSS2 as `NOT PRESENT` when it is silent.
- Polished startup and session behavior with an extended splash screen,
  aspect-preserving animated Time Card branding, `Time Card Connected` status,
  and a notification with one-click elevation when started as a non-admin.

## Build

Visual Studio 2022 with the managed desktop build tools and the .NET Framework
4.7.2 targeting pack is required. The runtime target is x64 Windows 10 or 11;
the hardened native loader uses the modern restricted `LoadLibraryEx` search
flags. No NuGet packages are used.

From a regular command prompt or PowerShell window:

```powershell
cd C:\path\to\Time-Card\DRV\Windows
.\build-gui.cmd release
```

The executable is written to:

```text
TimeCardControlCenter\bin\Release\TimeCardControlCenter.exe
```

Create the portable ZIP, including the required license and notice files, with:

```powershell
.\package-control-center.ps1 -SkipBuild
```

## Run

The kernel device currently requires administrator access for both read and
write IOCTLs. The application opens normally so its interface and diagnostics
remain available; when access is denied, select **Restart as administrator**.
Drivers that predate the multi-card device-interface inventory remain
discoverable through the legacy `\\.\TimeCard0` compatibility path. Installing
driver 1.42 is still recommended to expose the complete ABI 15 feature set.

The driver and card must already be installed. The dashboard reconnects every
five seconds if the card is temporarily unavailable.

Workspaces can be opened directly for lab consoles, documentation, or test
automation:

```powershell
TimeCardControlCenter.exe --page Clock
TimeCardControlCenter.exe --page Gnss
TimeCardControlCenter.exe --page Uart --uart-port=3
TimeCardControlCenter.exe --page Uart --com-port=COM3
TimeCardControlCenter.exe --page Sma
TimeCardControlCenter.exe --page Timing
TimeCardControlCenter.exe --page Fpga
TimeCardControlCenter.exe --page Sensors
TimeCardControlCenter.exe --page I2c
TimeCardControlCenter.exe --page Telemetry
TimeCardControlCenter.exe --page Operations
TimeCardControlCenter.exe --page Subsystems
TimeCardControlCenter.exe --page Flash
```

Use `--demo` to exercise the topology, telemetry, profiles, and guided test UI
without opening the kernel device. Demo mode is synthetic and disables profile
application:

```powershell
TimeCardControlCenter.exe --demo --page Telemetry
TimeCardControlCenter.exe --demo --compact --theme=High-contrast
TimeCardControlCenter.exe --demo --demo-no-imu --page Sensors
```

`--demo-no-imu` exercises the Sensors workspace exactly as a supported card
without a fitted IMU: the readouts remain unavailable while the six-face Time
Card cube runs its multi-axis showcase rotation.

`--theme=Dark`, `--theme=Midnight-blue`, and `--theme=High-contrast` are
available for repeatable visual validation. `--compact` forces the responsive
icon-only navigation used on narrower windows. `--width=<pixels>` and
`--height=<pixels>` set the initial window dimensions while respecting the
supported minimum size.

For repeatable documentation and visual regression checks, `--capture` renders
the selected live application window to PNG after the initial connection pass,
then exits:

```powershell
TimeCardControlCenter.exe --page Uart --uart-port=3 --capture=nmea.png
TimeCardControlCenter.exe --demo --page Telemetry --capture-delay=3000 --capture=telemetry.png
```

## UART console

Ports use the same stable numbering as the public driver ABI:

| Port | Function |
| ---: | --- |
| 0 | Primary GNSS receiver |
| 1 | Secondary GNSS receiver |
| 2 | Miniature atomic clock |
| 3 | NMEA output |

The selector also lists every Windows serial device currently returned by
`SerialPort.GetPortNames()` as entries such as **COM1** or **COM12**. Use the
refresh button beside the selector after attaching or removing a USB serial
adapter. The app preserves the current selection when that port remains
available and falls back to a Time Card hardware UART if it disappears.

Generic COM ports use the selected baud rate, 5–8 data bits, none/even/odd/
mark/space parity, one/one-and-a-half/two stop bits, optional XON/XOFF or
RTS/CTS flow control, and explicit DTR/RTS state. **Configure line** verifies
that Windows can open the selected configuration. The four FPGA UARTs remain
fixed at 8N1 with no flow control. Read once, continuous monitoring, text/hex
transmission, byte limits, timeouts, retained history, and every raw display
format work on both transports. Monitoring holds a generic COM port open until
stopped; if another program owns it, Windows reports the access error in the
app. Generic ports do not require the Time Card driver connection.
The u-blox mixed-stream and NMEA-only decoded views are also available for a
generic COM port, so an external receiver or USB serial adapter can use the
same framing, checksum, filtering, replay, and export workflow.

The live filter searches rendered protocol summaries and payload text without
discarding captured bytes. RX-only and TX-only views, pause/resume, counters,
and capture ranges do not interrupt the monitor. **Replay** feeds a file of up
to 16 MiB through the selected display and protocol decoder without
transmitting it. Exports can preserve the console text, one frame per CSV/JSON
record, or the concatenated binary payload. Text sends support no ending, CR,
LF, or CR+LF.

UART framing is currently 8N1. Reads and writes are limited to 256 bytes, and
timeouts are clamped to five seconds. An idle read is treated as a normal
zero-byte monitor sample rather than an application error.

On primary and secondary GNSS, the default **u-blox decoded** view reassembles
frames split across driver reads, separates back-to-back messages, and displays
their names and useful fields. It recognizes binary UBX, NMEA sentences, and
RTCM3 correction frames, validates the applicable UBX checksum, NMEA checksum,
or RTCM CRC-24Q, and reports stream resynchronization. Common navigation,
satellite, timing, raw-measurement, receiver-version, acknowledgement, and
configuration messages receive detailed summaries; unknown valid messages are
still identified by class and message ID.

The display selector can instead automatically choose a readable raw
representation or show every byte explicitly as ASCII, hexadecimal, decimal,
or binary. Changing the selection re-renders the retained console history
without another read. Selecting the atomic-clock UART returns the display to
**Auto** because that port does not carry a GNSS protocol stream.

The UART selectors use an application-owned dark template so their selected
values, editable baud field, and dropdown entries remain readable regardless
of the active Windows theme.

Selecting UART 3 opens the NMEA generator panel. The generator and the 16550
receiver are different FPGA blocks, so configuring only the receiver cannot
make sentences appear. Driver 1.9 / ABI 4 controls both together: it enables
the generator, selects one of the Linux-supported baud rates, applies normal
or inverted polarity, configures UART 3 to the same baud, and then starts the
monitor. The driver defaults a disabled generator to 9,600 baud, matching the
Linux initialization sequence.

With driver 1.40 / ABI 13 the panel also exposes signed UTC correction, a
-13:59 through +13:59 local-zone offset, the NMEA talker/GNSS identifier, and
individual RMC, ZDA and proprietary UTC sentence gates. Controls are enabled
only for the core revision that introduced them (GNSS 1.3, RMC 1.4, UTC 1.6),
and captured configuration profiles round-trip these values. Older profiles
retain legacy behavior and do not overwrite advanced generator settings.
For ToD Master 1.1 and newer, the panel also turns red and identifies the
sticky transmitter-error status without clearing it as a side effect. Once
the output configuration has been corrected, operators can acknowledge that
W1C status explicitly with `timecardctl nmea-set ... clear`; the same trailing
keyword works with both the legacy and advanced command forms.

UART 3 automatically selects **NMEA decoded**. The streaming decoder preserves
partial sentences between driver reads, separates consecutive sentences,
validates XOR checksums, and displays the talker/message identifier plus useful
fields for RMC, GGA, GSA, GSV, GLL, VTG, GNS, ZDA, GST, TXT, HDT, and THS.
Unrecognized sentences remain visible with their data fields instead of being
dropped. Select **ASCII** when the original wire sentence is required.

## Telemetry, profiles, and self-test

**Telemetry Studio** keeps up to 1,800 live samples by default. Recording is
explicit: the charts continue to show live state while the session recorder is
stopped, and only recorded samples appear in CSV/JSON export. Pausing freezes
the visual display while driver polling continues. Hovering a chart reports
the exact UTC acquisition time and value; the mouse wheel changes the visible
sample window.

The cross-timestamp distribution uses the same system-sampling-window values
as the line chart and marks its median and 95th percentile; the summary also
reports the 99th percentile. Overall vibration is the Euclidean magnitude of
the IMU gravity-compensated linear-acceleration vector,
`sqrt(x*x + y*y + z*z)`, in m/s². The live label includes a rolling 60-sample
RMS, and recorded CSV/JSON rows include `vibration_m_s2` /
`vibrationMetersPerSecondSquared`. These are board-motion indicators, not a
calibrated structural-vibration measurement.

**Profiles & Self-Test** provides capability-aware operations. A profile only
touches fields it declares. Before the first write, the app captures clock
source, safely discoverable timing cores, and all present SMA routes. Automatic
capture omits the optional NMEA/ToD Master until a trusted image-specific
synthesis contract can prove that block exists; choosing an NMEA profile or the
NMEA workspace remains an explicit operator request. Each setter is followed by
readback verification; any exception or mismatch triggers an automatic
best-effort restore of that capture. **Restore last known good** can explicitly
return to the most recently captured pre-change state with the same verification
and rollback protection. The history stores action summaries, not
credentials or raw receiver locations, under
`%LOCALAPPDATA%\OCP\TimeCardControlCenter`.

The guided self-test is intentionally read-only and stops the one-second
refresh timer while it owns the driver handle. A support ZIP contains plain-
text application/OS metadata, the public driver diagnostics, test results,
session log, compatibility matrix, and recorded telemetry. It does not collect
credentials, browser data, or unrelated files.

The **Engineering Diagnostics** workspace retains the newest 2,000 session
records. Every record carries a UTC timestamp, inferred severity and subsystem
category, the current card serial (or layout/device context while identity is
unavailable), and the original message. Severity, category, and free-text
filters combine without deleting history. **Export view** writes the currently
visible records directly as a line-oriented UTF-8 text log or structured JSON;
the support bundle and **Copy summary** always use the complete bounded log,
independent of the visible filters. The retained and discarded counts remain
visible so long-running laboratory sessions are unambiguous.

Keyboard shortcuts are `F5` refresh, `Ctrl+R` start/stop telemetry recording,
`Ctrl+E` open Telemetry Studio and export, and `Ctrl+1`, `Ctrl+2`, `Ctrl+3` for
Overview, Precision Clock, and GNSS respectively.

## Validation

The release build treats warnings as errors. Run the hardware-independent
product and protocol suites from `DRV\Windows`:

```powershell
.\tools\test-control-center-product.ps1
.\tools\test-multi-card-control-center.ps1
.\tools\test-fpga-advanced.ps1
.\tools\test-session-logging.ps1
.\tools\test-ublox-decoder.ps1
.\tools\test-oscillatord-client.ps1
```

The product suite validates the health graph, built-in profile catalog, XML
profile round-trip, telemetry retention, and CSV/JSON export including
vibration. The session-logging suite validates bounded retention, structured
fields, combined filters, text/JSON serialization, UI wiring, and continued
support-bundle compatibility. The protocol suite validates fragmented and
back-to-back UBX, NMEA, and RTCM3 streams plus checksum/CRC handling.
The multi-card suite validates device-interface enumeration and active-card
selection. The advanced FPGA suite is static: it checks ABI layouts, exact
image and revision gates, interrupt maps, static-inventory safety, and rollback
invariants without accessing hardware.
`--demo --capture=<file.png>` provides repeatable visual smoke tests for every
workspace. `--capture-delay=<milliseconds>` (100 through 10,000) allows live
charts to populate before capture; `verify.ps1 -ExpectHierarchy -TestGnssUart`
remains the real-card driver validation.

## Precision clock source

The **Precision Clock** workspace can select None, Time-of-Day/GNSS, IRIG-B,
external PPS, PTP, RTC, DCF77, register-controlled, or external-selector mode.
NTP is enabled only for Clock 1.8+ with the matching image contract; SyncE and
dynamic source are enabled only for Clock 2.7+ with their contracts. Every
change requires confirmation because selecting an unavailable source can
remove PHC synchronization. Time-of-Day/GNSS is the normal Time Card setting.

## Clock synchronization

The **Overview** workspace provides both synchronization directions. **Sync
from System Clock** writes the current system UTC value to the PHC. **Sync from
Time Card** reads a fresh bracketed PHC cross-timestamp and sets Windows UTC
from the card after explicit confirmation. The second operation requires
administrator privileges and is blocked when the PHC date is outside 2020 to
2100, preventing an uninitialized clock such as 1970 from replacing a valid
system time. Windows accepts this one-shot value at millisecond resolution.

The **Overview** and **Precision Clock** workspaces retain rolling 200-second
histories for measured system-clock offset. Their cross-timestamp sampling
window and uncertainty histories retain 60 seconds. Detail-focused automatic
vertical zoom follows the visible data range so small offset changes remain
easy to see without clipping larger excursions.

## u-blox GNSS configuration

The **GNSS & Time-of-Day** workspace can query a receiver on UART 0 or UART 1
using checksum-protected UBX frames. It recognizes the repository-recommended
[u-blox RCB-F9T](https://www.u-blox.com/en/product/rcb-f9t-timing-board) through
`UBX-MON-VER`. On cards whose FPGA path is receive-only, it also recognizes an
F9 timing receiver from a validated live combination of `UBX-MON-RF`,
`UBX-TIM-SVIN`, `UBX-NAV-PVT`, and the navigation-time messages. Passive mode
keeps telemetry and decoding available while clearly disabling controls that
require receiver acknowledgements. The recommended RCB-F9T normally uses UART
0 at 115,200 baud; the host baud selector can communicate at another existing
baud without changing the receiver's own port configuration.

Older revision 00 FPGA images can expose a working GNSS UART without
implementing the ToD GNSS-status and satellite-summary registers. The register
value `0xffffffff` is treated as “Not available,” while the UART console and
u-blox/NMEA decoders remain fully usable.

The satellite sky map decodes every repeated `UBX-NAV-SAT` block. It uses the
standard north-up polar layout with zenith at the center and the horizon at the
outer ring. Marker color identifies the constellation, marker size follows
C/N₀, and a white outline identifies a satellite used in the navigation
solution. The adjacent scrollable signal table exposes elevation, azimuth,
C/N₀, and tracking quality for every reported satellite.

Receivers supporting `UBX-CFG-VALGET` and `UBX-CFG-VALSET` expose four
independently detected control groups:

- Measurement period, navigation ratio, time reference, and dynamic platform
  model.
- GPS, Galileo, BeiDou, GLONASS, QZSS, and SBAS constellation enables.
- TP1 enable, GNSS synchronization, locked settings, edge polarity, time grid,
  period, and pulse width.
- UBX NAV-PVT, NAV-TIMEGPS, NAV-TIMEUTC, NAV-TIMELS, TIM-SVIN, and NMEA GGA,
  GSA, GSV, RMC, and ZDA output rates on UART 1.

The F9 timing panel decodes `UBX-NAV-TIMEGPS` into GPS week, time of week,
GPS-UTC offset, validity, and time accuracy. It decodes `UBX-NAV-TIMELS` into
the current leap-second offset and source plus any scheduled change, countdown,
GPS week/day, and validity flags. The UART laboratory presents the same fields
for live or replayed streams. F9 receivers with a working configuration path
also expose disabled, survey-in, and fixed-position timing modes, survey
duration/accuracy, fixed LLH/accuracy, `UBX-TIM-SVIN` progress, and per-band
`UBX-MON-RF` antenna, AGC, noise, and jamming state.

Configuration changes target receiver RAM by default and are therefore lost
at reset. Selecting **Persist BBR + flash** asks for confirmation before the
same values are written to the battery-backed and flash layers. Disabling the
time pulse, changing it away from rising-edge 1 PPS, and unusually high
navigation rates receive additional warnings because they can interrupt Time
Card synchronization or overload serial output. The application never changes
the receiver UART baud, disables UBX protocol access, resets the receiver, or
updates receiver firmware. Parameter keys, bounds, frame formats, and ACK/NAK
handling follow the official
[ZED-F9T Interface Description](https://content.u-blox.com/sites/default/files/ZED-F9T-10B_InterfaceDescription_UBX-20033631.pdf).

## Atomic clocks: MAC-SA53 and ART mRO-50

The **Atomic Clock** workspace talks directly to UART 2 using Microchip's C3
protocol at the factory-default 57,600 baud, 8N1. It uses the documented
MAC-SA5X parameter names and bounds from the
[MAC-SA5X User's Guide](https://ww1.microchip.com/downloads/aemDocuments/documents/FTD/ProductDocuments/UserGuides/Miniature-Atomic-Clock-MAC-SA5X-Users-Guide-DS50002938.pdf).
The general UART monitor is stopped before an SA53 transaction and the complete
command/response exchange is serialized so monitor reads cannot consume C3
responses.

On an Orolia/Safran ART card, the workspace changes automatically to the
mRO-50 direct FPGA bridge. It reports oscillator enable and lock, the raw
gateware temperature word, fine adjustment, coarse adjustment, bridge control,
and board-configuration state. Fine and coarse writes are available from the
ART panel; saving coarse adjustment to nonvolatile storage requires explicit
confirmation. The application does not invent a Celsius conversion for the
raw temperature word because ART FPGA v0.0.9 does not publish its scale.

The optional ART 16550 mRO serial route remains configured for 9,600 baud, 8N1
when implemented. The direct bridge is authoritative and continues to work on
the tested FPGA v0.0.9 image even though its mRO UART block reports line status
`0x00`.

Digital tuning and discipline settings take effect immediately. Enabling PPS
disciplining can initiate a JamSync and move the 1PPS phase; the application
therefore asks for confirmation. Analog tuning cannot be enabled from the UI
while digital tuning is nonzero or PPS disciplining is selected. PPS
disciplining and phase metering are also enforced as mutually exclusive.

Alarm acknowledgement sends the current active alarm bitmask and does not hide
the underlying alarm condition. JamSync, loading stored settings, and writing
configuration to flash each require confirmation. Calibration latching, CPU
reset, and serial-baud changes are intentionally unavailable because they can
be irreversible or interrupt communication.

## Oscillator discipline

The top half of **Oscillator Discipline** is native Windows control. Its
capability contract was introduced in ABI 11 and remains available in ABI 12;
it does not assume one oscillator implementation for every card:

- **Orolia/Safran ART + mRO-50:** the application uses the driver's paired GNSS
  and internal-oscillator PPS samples and the exact vendored Orolia miniCOD
  3.6.0 algorithm. It displays phase, state, clock class, GNSS validity,
  convergence/holdover, mRO fine/coarse words, raw temperature, and every
  requested/applied action. Start, stop, full three-point calibration, and a
  simulated holdover test are available with confirmations where appropriate.
- **Meta/Facebook + MAC-SA53:** the page routes configuration to the Atomic
  Clock workspace, where the SA53's hardware discipline, phase meter, loop,
  PPS, JamSync, and steering controls already live.
- **Celestica and other variants:** capability and protocol discovery select
  supported controls. Unknown oscillators remain visible but monitor-only; the
  application never attempts ART offsets on a different PCI profile.

ART discipline starts only when the mRO bridge and paired phase meter are both
reported. A sample is valid only after both PPS counters advance without a
gateware error. miniCOD actions are bounded again in user mode before the
driver enforces its own ranges. Stop releases phase capture and persists only
changed calibration data. The application loads a valid card EEPROM image
first, then its atomic host backup in
`%ProgramData%\OCP Time Card\discipline-profile-N.bin`; card writes are
page-aligned and verified by the driver. Closing the application stops the loop
and releases capture. A loss of valid measurements drives miniCOD into its
native holdover behavior rather than continuing open-loop adjustments.

While native discipline is running, the application passively owns the primary
GNSS UART long enough to collect each UBX epoch. It preserves the gateware baud,
requires TIM-TP plus a valid NAV-PVT fix, tracks TIM-SVIN completion, rejects
quantization errors beyond the upstream 5,000 ps limit, and feeds miniCOD the
previous epoch's `qErr` just like Linux `oscillatord`. The UART console should
not monitor GNSS port 0 at the same time. **Bypass survey** matches upstream
`gnss-bypass-survey`; it is off by default and adds an explicit warning to the
start confirmation.

The Release build embeds `TimeCardDiscipline.dll` inside
`TimeCardControlCenter.exe`, so the portable application no longer depends on
a loose native DLL beside it. Windows still requires a native PE image on disk
while loading it; at first use the application extracts the exact embedded
bytes into a SHA-256-versioned cache, verifies them, loads by absolute path,
and holds the file read-locked for the process lifetime. Non-elevated sessions
use the user's LocalAppData; elevated sessions and the Windows service use the
administrator-protected Program Files tree so a medium-integrity process
cannot replace code before it is loaded.
`tools\test-native-discipline.ps1` exercises that embedded-resource loader,
the native ABI, one process step, and exact 512-byte EEPROM serialization
without requiring hardware.

### Native Windows oscillatord and compatible remote service

The lower half of the same workspace defaults explicitly to the protected local
`OcpTimeCardOscillatord` Windows-service pipe. Local mode has no host, IP port,
or shared-token setting, and no WSL or Linux daemon is needed. Selecting
**Remote oscillatord** reveals the host, TCP port, and temporary control-token
fields and forces a TCP request—even when the remote host is `127.0.0.1`—so the
reported transport is never ambiguous. The same client remains wire-compatible
with the integrated Linux
[`oscillatord`](../../../Software/oscillatord) TCP monitoring protocol. It
shows service and protocol versions, phase offset and clock class, disciplining
state and convergence, holdover readiness, mRO-50 lock/temperature/fine/coarse
controls, and GNSS fix, satellites, accuracy, survey, leap-second, and antenna
state. The native service and Linux service both use TCP port 2958 by default.

The remote token is held only in the password field for the current process,
is cleared when returning to local mode, and is never written to application
settings, telemetry exports, support bundles, or logs. Status and
calibration-EEPROM reads work against a read-only service.
Calibration, EEPROM saves, fake holdover, GNSS start/stop/reset, and mRO-50
coarse changes require confirmation in the app and independent authorization
by the caller's local Administrator token on the Windows pipe, or by
`monitoring-allow-control` plus `monitoring-control-token` on TCP. Keep TCP on
loopback unless remote management is required; when remote,
the protocol is not encrypted, so prefer an SSH local forward such as
`ssh -L 2958:127.0.0.1:2958 timecard-host`. Otherwise bind a trusted interface
and restrict TCP/2958 with the Linux host firewall.

![oscillatord discipline and telemetry workspace](../assets/timecard-control-center-oscillatord.png)

## SMA connector control

The bounded SMA query and set operations are modeled on Linux `ptp_ocp`. The
application exposes named 10 MHz, PPS, timestamp, frequency, IRIG-B, DCF77,
GNSS, PHC, atomic-clock, and generator routes; it does not permit arbitrary
MMIO writes. Firmware with fixed connector directions is detected and the
direction menu is locked accordingly. Driver 1.9 fixes fixed-direction routing
by never clearing the absent opposite map and verifies every applied route by
immediate register readback.

For the Orolia/Safran ART profile, the menus automatically switch to the
gateware's smaller routing table: PPS1 or 10 MHz inputs, and atomic-clock,
GNSS, or 10 MHz outputs. Fixed ART connector functions remain read-only.

Routing changes are immediate. Disconnect externally driven equipment before
changing a connector to output. The application asks for confirmation before
applying any output route.

## Generators and frequency counters

Driver 1.10 / ABI 5 exposes four bounded periodic-output modules and four
frequency counters without permitting arbitrary MMIO. Each generator accepts
a frequency, 1–99% duty cycle, phase in nanoseconds, and polarity. The driver
converts these into period and pulse widths, schedules the next start against
the PHC, and applies the same disable/program/enable sequence used by Linux
`ptp_ocp`. A generator can be routed directly to any compatible SMA output.

Each generator card has two deliberately compact start modes. **Next
PHC-aligned** is the backward-compatible default and applies the phase field.
**Exact PHC time** accepts `seconds.nanoseconds` in the card's PHC/TAI
timescale, with up to nine fractional digits; it does not label POSIX time as
UTC or guess a leap offset. The kernel requires a start more than 1 ms in the
future, verifies the exact start registers, and restores the complete previous
configuration on any readback failure. Refresh/query reports the programmed
start but never persists or replays the one-shot absolute-start request in a
configuration profile.

Per `Clk_SignalGenerator_ReferenceManual.pdf` section 3.2.1.3, polarity `1`
means active-high and `0` means active-low. The workspace and new profiles use
those terms. Legacy profile XML named `Inverted` remains import-compatible and
maps to the same unchanged raw bit.

Driver 1.39 / ABI 12 additionally exposes the manual's 32-bit repeat count,
16-bit cable-delay compensation, and sticky error/time-jump state. A repeat
count of zero means continuous generation. ABI 15 connects the published
MSI/MSI-X completion vectors to bounded per-generator event queues; **Read
events** reports completion, error, time jump, overflow, and sequence data.
On ABI 13 and newer, **Clear status** issues the documented write-one-to-clear
mask without disabling, rescheduling, or changing generator configuration.

Each frequency counter accepts an integration window of 0 (disabled) or
1–255 seconds. The workspace decodes valid, error, and overrun states and can
route any front-panel SMA input to `FREQ1` through `FREQ4`.

## FPGA Engines

The workspace is capability-first. It reads the active board profile and ABI
feature mask, then queries only kernel-advertised cores. ABI 15's static
inventory lists each trusted core instance, BAR offset, span, version, and
interrupt message. It explicitly reports that no Configuration Slave ROM is
present; neither the driver nor application falls back to scanning guessed
addresses. ABI 13 image identity remains visible as a raw word plus decoded
loader/application tag and version, and is included in diagnostics and
image-bound profile compatibility checks.

The **Exact image synthesis contract** card is the only route to optional
register banks. It shows the live raw identity and provides independent choices
for clock servo/log and advanced controls, ToD telemetry, ToD Master UTC
read/write, IRIG Master/Slave AM, IRIG Slave year, NTP/SyncE/dynamic sources,
and future ART extended timestamp support. Activation requires a separate
acknowledgement and warning. The kernel verifies ABI, board, layout, reserved
fields, exact raw word, and capability dependencies, then rechecks the image
before every optional access. Clearing the card locks those banks immediately;
contracts are never restored automatically in a later session.

The PPS cards expose enable, active level, signed cable compensation, output
pulse width, measured input width, filter/supervision faults, and explicit W1C
actions. IRIG adds B/G mode, code, correction, control bits, input delay, and,
when contracted, Master 1.5 AM, Slave 1.5 code/manual year, and Slave 1.6 AM.
DCF77 exposes correction, input air delay, decoder bit position, and errors.
The ToD parser exposes NMEA/UBX/TSIP/ESIP/PFEC, GNSS system, baud, logical UART
polarity, correction, message gates, and contract-gated UTC/GNSS/satellite
telemetry. The ToD Master UTC card performs the documented bounded `GxUTC`
handshake and is writable only when both UTC read and write are contracted.

The clock area separates common smooth adjustment from synthesis-optional
advanced control. Common fields cover signed offset, update window, signed
Q16.16 drift, drift window, and in-sync threshold. The advanced card exposes
only revision-compatible holdover, outlier filters, rate limiters, dynamic
update, aging/revert, servo factors and status logs. It also decodes the base
holdover-ready and aging-ready states. Raw fixed-point/configuration words are
shown explicitly to avoid a lossy round trip.

The timestamper laboratory covers GNSS1, TS1-TS4 and PHC/PPS. It configures
enable, edge polarity and cable delay where writable, reports counters and
queue loss, and reads bounded interrupt batches with optional payload data.
Standard MSI/MSI-X images use their full v1.3 aperture. ART stays on its
published basic surface; the UI never enables extended ART fields from a
contract that the kernel cannot validate. The signal-generator event card
similarly drains bounded completion/error/time-jump batches.

All setters are transactional in the kernel: validate, snapshot, disable only
when required, write while preserving reserved bits, restore requested state,
and verify readback. A failure restores all touched registers and the prior
ToD/NMEA UART baud when applicable. Successful register readback still cannot
prove a synthesis-selected datapath exists, so use a known input stream or
PPS/IRIG/DCF/SMA loopback for functional validation. See
[../FPGA_CORE_COVERAGE.md](../FPGA_CORE_COVERAGE.md) for the manual-by-manual
and UCM matrix.

## I2C workbench

Driver ABI 3 adds bounded Xilinx AXI IIC status, address-only probe, and read
operations using the register layout and transfer flow used by Linux
`i2c-xiic`. The application recognizes the board 24C02 EEPROM at `0x50` and
the 24MAC402 identity EEPROM at `0x58`, can scan all normal 7-bit addresses,
and displays reads as hex plus ASCII, hex, ASCII, decimal, or binary. Presets,
previous/next page navigation, a 50/100/250 ms timeout selector, copy, an ACK
map, and decoded control/status/interrupt flags support bus diagnosis. The six
bytes in the factory EUI-48 area at raw offset `0x9a` are formatted as the
card's unique serial number. Linux exposes the same identity bytes at NVMEM
offset zero through the `24mac402` provider to `ptp_ocp`'s `serialnum`
attribute.

Reads support no subaddress or a one- or two-byte subaddress followed by a
repeated start. Transfers are limited to 255 bytes and a bounded timeout.
Driver 1.18 corrects the dynamic-mode short-transfer handshake by queueing
START/address, clearing stale completion state, and then appending the
register payload, matching Linux `i2c-xiic`. Receive data is drained whenever available,
the expected final receive NACK is handled separately from an address NACK,
and a failed transient transaction is reset and retried once. Native Win32
error text and a post-failure controller snapshot are shown if a read still
fails. The Control Center requires driver 1.14 or newer for I2C operations.

Driver 1.14 / ABI 7 provides dedicated controls for the schematic's U27 PCA9546A
at `0x70`. Channel 0 is the MAC-clock branch, channel 1 contains the onboard
sensors and RGB LED driver, channel 2 is the analog/ADC expansion branch, and
channel 3 is the DC expansion branch. The workspace shows the selected mask,
offers safe single-branch shortcuts, annotates full scans with the expected
BNO055, BNO08x, INA219, BME280/BMP280, and IS32FL3207 addresses, and provides
matching read presets.

U26, the TMUX1072 near the MAC connector, is controlled by the physical
`MACSER` DIP only; the select net is not routed to the FPGA. The workspace
therefore explains that channel 0 also requires `MACSER=0` instead of exposing
a non-functional software toggle.

Time Card V9 straps the IS32FL3207 at 7-bit address `0x37`; the schematic's
`0x6e` label is its 8-bit write address. Driver 1.18 also detects the other
documented AD strap choices (`0x34`-`0x36`). It drives six common-anode RGB
indicators: GNSS1 uses
OUT1-3, GNSS2 OUT4-6, and IO1 through IO4 use OUT7-18 in groups of three.
Manual RGB/current controls and automatic status mapping are available.
Automatic mode uses green for a GNSS fix or configured SMA output, blue for an
SMA input, amber for searching/unknown/disabled states, and red for missing or
failed status. LED transactions temporarily select sensor channel 1 and then
restore the user's PCA9546A mask. The driver caps global current at 128 and
does not expose a general data-write or EEPROM-programming IOCTL.

The Electrical test button saves the six colors, verifies that the hardware
SDB pin permits a device reset, bypasses PWM, and forces all 18 current sinks
on for five seconds before restoring the saved state. Open/short results are
shown in the workspace, and automatic mapping marks affected packages as
`HARDWARE FAULT` instead of reporting a successful visible update.

## Sensors and IMU

On Meta/Facebook profiles, the Sensors & IMU workspace resolves the
BME280/BMP280, INA219, and IMU routes independently across the known PCA9546A
branches, with the V9 schematic's sensor channel tried first. This keeps an
alternate BNO055/BNO08x assembly route from masking populated environmental or
rail monitors. BME280 and BMP280 are
auto-detected at `0x76` or `0x77`; BME280 cards show factory-compensated
temperature, relative humidity, pressure, and calculated dew point, while a
BMP280 correctly reports humidity and dew point as unavailable. The three
INA219 cards show bus voltage, shunt current, and load power for +12 V, +5 V,
and +3.3 V using the board's 2 milliohm shunts.
Driver 1.19 also wakes and verifies an INA219 that gateware left in
reset/power-down before collecting its first conversion.
Driver 1.35 retries the V9 monitors with a STOP-then-START register transaction
when the pointer write ACKs but the AXI-IIC repeated-START read-address phase
NACKs. The
fallback is limited to this NACK case and preserves the normal repeated-START
path for devices that accept it.

On Celestica R4006-G0001-03 Rev02, driver 1.37 / ABI 10 follows the schematic's
TCA9546A routes exactly. Channel 0 provides three LM75B temperatures at
`0x48`-`0x4a`; channel 1 provides SHT3x temperature, humidity, and calculated
dew point at `0x44`; channel 2 provides an ICP-10100 pressure/temperature
sample at `0x63`; and channel 3 provides the BNO08x at `0x4a`. The driver
validates every SHT3x and ICP-10100 CRC-8 frame, caches all four factory OTP
coefficients, restores the previous mux mask after sampling, and exposes
explicit capability flags so the application never presents Meta-only sensors
as missing Celestica hardware.
The I2C workspace also follows sheet 26's Celestica LED population: one GPS
indicator and four SMA indicators at IS32FL3207 address `0x34`, with the
schematic's green/red sink order and unused second-GNSS group handled safely.

The BNO055 is auto-detected at `0x28` or `0x29`, placed in NDOF fusion mode,
and uses the V9 schematic's external 32.768 kHz crystal. The
schematic-permitted BNO080/BNO08x alternative is detected at `0x4a` or `0x4b`;
the driver configures its SH-2 SHTP acceleration, gyro, magnetic, linear-
acceleration, rotation-vector, and gravity reports. The workspace reports
heading, roll, pitch, quaternion, calibration levels, acceleration, linear
acceleration, gravity, angular velocity, and magnetic field. A native 3D cube
tracks the normalized quaternion in real time with smoothed shortest-path
rotation. Its six faces use the black, blue, green, purple, red, and yellow
Time Card artwork, centered at a consistent scale without stretching or
clipping. The compact visualization holds its last good pose across isolated
invalid or identity `0°, 0°, 0°` samples; a real zero orientation is
accepted after three consecutive valid samples. On cards without an IMU,
including Orolia ART, the same cube rotates continuously about all three axes
so every face remains visible. BNO055 also reports
temperature. Sampling is live at one hertz while the workspace is visible.
The header's rolling **Sensor read time** graph records the last 60 complete
driver acquisitions in milliseconds, shows the current and minimum/maximum
latency, and marks failed transactions without interrupting automatic sampling.
No manual refresh is needed while the workspace is open.
Each query temporarily selects the schematic sensor branch and the independently
discovered IMU branch, reports missing devices separately, and restores the
previous mux selection. A nonresponding monitor is explicitly
shown as **NO I2C ACK - NOT FITTED OR UNPOWERED**; the application never turns
an absent sensor into a plausible zero reading.
For BNO08x cards, driver 1.24 also detects a silent or reset SH-2 stream,
re-establishes the six feature subscriptions, and retries the sample
automatically. The workspace can therefore recover from `INITIALIZING` without
restarting the application, reloading the driver, or rebooting Windows.
Driver 1.25 additionally probes SH-2 report `0x0e` and displays its signed-Q7
ambient temperature when the installed BNO08x firmware publishes it. Firmware
without that optional report continues to show an em dash rather than a
fabricated die-temperature value.

The Orolia ART profile has no PCA9546A environmental/power/IMU branch and no
IS32FL3207 status-LED controller. These workspaces therefore show **not fitted
on ART** and do not issue unsupported sensor, mux, or LED IOCTLs. The mRO-50
raw temperature word remains available in the Atomic workspace.

## FPGA SPI flash

Driver 1.12 / ABI 6 exposes only the FPGA firmware portion of SPI-NOR. The
configuration area below physical offset `0x00400000` cannot be addressed by
the public IOCTLs. Erases are fixed to aligned 4 KiB sectors, programs cannot
cross the reported 256-byte page boundary, and reads/programs are limited to
256 bytes per request.

The workspace accepts the `OCPC` wrapper used by Linux `ptp_ocp`; it verifies
the PCI identity (`1D9B:0400`), declared payload length, and CRC16 before
stripping the wrapper. Raw images are allowed only with a prominent warning
and the same explicit acknowledgement as wrapped images. An update erases the
required sectors, programs every page, then reads and compares every payload
byte. Closing the application is blocked while this operation runs. Keep the
card powered throughout and power-cycle it only after verification succeeds.

## Hardware-dependent roadmap

ABI 15 covers the software-feasible gaps found in the FPGA-manual and UCM
audits. Remaining work requires new bitstream or board contracts rather than an
unrestricted user-mode interface: a machine-readable synthesis manifest, an
actually instantiated Configuration Slave ROM, trusted maps for UCM's optional
network/SyncE/RTC/PHY cores, and an ART image identity that can prove its
extended timestamp aperture. The ABI deliberately continues to exclude
arbitrary register writes, I2C data writes, the protected flash configuration
region, and untyped PTM controls.
