# OCP Time Card Control Center

The Time Card Control Center is a dependency-free Windows desktop dashboard
for the OCP Time Card driver. It uses the public versioned IOCTL ABI directly;
it does not shell out to `timecardctl` or scrape Device Manager.

![OCP Time Card Control Center](../assets/timecard-control-center.png)

| Clock source | NMEA output | SMA connectors |
| --- | --- | --- |
| ![Clock-source configuration](../assets/timecard-control-center-clock.png) | ![NMEA generator and UART monitor](../assets/timecard-control-center-nmea.png) | ![SMA connector routing](../assets/timecard-control-center-sma.png) |

## Current capabilities

- Live PHC time, Windows cross-timestamp, offset, and sampling-window display.
- Clock engine, synchronization, PCIe layout, BAR, interrupt status, and a
  guarded selector for all FPGA clock sources exposed by Linux `ptp_ocp`.
- Decoded GNSS fix and seen/locked satellite counts from the ToD engine.
- Direct u-blox receiver discovery and UBX telemetry, including model,
  firmware/protocol, fix, UTC, position accuracy, visible/used satellites,
  constellation counts, and average carrier-to-noise level. Compatible
  configuration-database receivers expose navigation rate and platform model,
  constellation selection, TP1 timing, and UART 1 message-rate controls.
- One-shot PHC synchronization from Windows UTC.
- Polled UART configuration, monitoring, text/hex display, and text or binary
  transmission for GNSS, GNSS2, atomic-clock, and NMEA ports.
- FPGA NMEA sentence-generator enable, baud, and polarity configuration with
  one-click synchronized UART monitoring.
- A dedicated Microchip MAC-SA53 workspace with live identity, physics-lock,
  alarm, temperature, supply, runtime, phase, and steering telemetry. It can
  configure digital tuning, analog-tuning mode, PPS source/offset/width/cable
  delay, discipline and phase-metering modes, loop time constants, lock
  thresholds, PPS quantization correction, and the time-of-day counter.
- Four-connector SMA routing with input/output menus, named FPGA timing
  signals, fixed-direction detection, raw-map readback, and output warnings.
- A read-only I2C workbench with AXI IIC health, known-device probing, full
  7-bit discovery, a bounded EEPROM/register hex and ASCII reader, and the
  unique card serial from the 24MAC402 identity EEPROM.
- Runtime and persistent Device Manager subsystem-hierarchy controls.
- Copyable engineering diagnostics and an in-application session log.
- A subsystem capability map using the same artwork as Device Manager.

## Build

Visual Studio 2022 with the managed desktop build tools and the .NET Framework
4.7.2 targeting pack is required. No NuGet packages are used.

From a regular command prompt or PowerShell window:

```powershell
cd C:\Users\Ahmad\git\Time-Card\DRV\Windows
.\build-gui.cmd release
```

The executable is written to:

```text
TimeCardControlCenter\bin\Release\TimeCardControlCenter.exe
```

## Run

The kernel device currently requires administrator access for both read and
write IOCTLs. The application opens normally so its interface and diagnostics
remain available; when access is denied, select **Restart as administrator**.

The driver and card must already be installed. The dashboard reconnects every
five seconds if the card is temporarily unavailable.

Workspaces can be opened directly for lab consoles, documentation, or test
automation:

```powershell
TimeCardControlCenter.exe --page Clock
TimeCardControlCenter.exe --page Uart --uart-port=3
TimeCardControlCenter.exe --page Sma
```

For repeatable documentation and visual regression checks, `--capture` renders
the selected live application window to PNG after the initial connection pass,
then exits:

```powershell
TimeCardControlCenter.exe --page Uart --uart-port=3 --capture=nmea.png
```

## UART console

Ports use the same stable numbering as the public driver ABI:

| Port | Function |
| ---: | --- |
| 0 | Primary GNSS receiver |
| 1 | Secondary GNSS receiver |
| 2 | Miniature atomic clock |
| 3 | NMEA output |

UART framing is currently 8N1. Reads and writes are limited to 256 bytes, and
timeouts are clamped to five seconds. An idle read is treated as a normal
zero-byte monitor sample rather than an application error.

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

## Precision clock source

The **Precision Clock** workspace can select None, Time-of-Day/GNSS, IRIG-B,
external PPS, PTP, RTC, DCF77, register-controlled, or external-selector mode.
The values and register semantics match the Linux `clock_source` attribute.
Every change requires confirmation because selecting an unavailable source can
remove PHC synchronization. Time-of-Day/GNSS is the normal Time Card setting.

## u-blox GNSS configuration

The **GNSS & Time-of-Day** workspace can query a receiver on UART 0 or UART 1
using checksum-protected UBX frames. It recognizes the repository-recommended
[u-blox RCB-F9T](https://www.u-blox.com/en/product/rcb-f9t-timing-board) through
`UBX-MON-VER`, while `UBX-NAV-PVT` and `UBX-NAV-SAT` provide generic status on
many other u-blox generations. The recommended RCB-F9T normally uses UART 0 at
115,200 baud on the Time Card; the host baud selector can communicate with a
receiver at another existing baud without changing the receiver's own port
configuration.

Receivers supporting `UBX-CFG-VALGET` and `UBX-CFG-VALSET` expose four
independently detected control groups:

- Measurement period, navigation ratio, time reference, and dynamic platform
  model.
- GPS, Galileo, BeiDou, GLONASS, QZSS, and SBAS constellation enables.
- TP1 enable, GNSS synchronization, locked settings, edge polarity, time grid,
  period, and pulse width.
- UBX NAV-PVT and NMEA GGA, GSA, GSV, RMC, and ZDA output rates on UART 1.

Configuration changes target receiver RAM by default and are therefore lost
at reset. Selecting **Persist BBR + flash** asks for confirmation before the
same values are written to the battery-backed and flash layers. Disabling the
time pulse, changing it away from rising-edge 1 PPS, and unusually high
navigation rates receive additional warnings because they can interrupt Time
Card synchronization or overload serial output. The application never changes
the receiver UART baud, disables UBX protocol access, resets the receiver, or
updates receiver firmware. Parameter keys, bounds, frame formats, and ACK/NAK
handling follow the official
[RCB-F9T Interface Description](https://content.u-blox.com/sites/default/files/RCB-F9T_InterfaceDescription_%28UBX-19003606%29.pdf).

## Microchip MAC-SA53 atomic clock

The **Atomic Clock** workspace talks directly to UART 2 using Microchip's C3
protocol at the factory-default 57,600 baud, 8N1. It uses the documented
MAC-SA5X parameter names and bounds from the
[MAC-SA5X User's Guide](https://ww1.microchip.com/downloads/aemDocuments/documents/FTD/ProductDocuments/UserGuides/Miniature-Atomic-Clock-MAC-SA5X-Users-Guide-DS50002938.pdf).
The general UART monitor is stopped before an SA53 transaction and the complete
command/response exchange is serialized so monitor reads cannot consume C3
responses.

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

## SMA connector control

The bounded SMA query and set operations are modeled on Linux `ptp_ocp`. The
application exposes named 10 MHz, PPS, timestamp, frequency, IRIG-B, DCF77,
GNSS, PHC, atomic-clock, and generator routes; it does not permit arbitrary
MMIO writes. Firmware with fixed connector directions is detected and the
direction menu is locked accordingly. Driver 1.9 fixes fixed-direction routing
by never clearing the absent opposite map and verifies every applied route by
immediate register readback.

Routing changes are immediate. Disconnect externally driven equipment before
changing a connector to output. The application asks for confirmation before
applying any output route.

## I2C workbench

Driver ABI 3 adds bounded Xilinx AXI IIC status, address-only probe, and read
operations using the register layout and transfer flow used by Linux
`i2c-xiic`. The application recognizes the board 24C02 EEPROM at `0x50` and
the 24MAC402 identity EEPROM at `0x58`, can scan all normal 7-bit addresses,
and displays reads as hex plus ASCII. The six bytes at MAC EEPROM offset zero
are formatted as the card's unique serial number, the same mapping used by
Linux `ptp_ocp` and its `serialnum` attribute.

Reads support no subaddress or a one- or two-byte subaddress followed by a
repeated start. Transfers are limited to 255 bytes and a bounded timeout.
I2C data writes and EEPROM programming remain intentionally unavailable until
they have been validated on hardware.

## Driver API roadmap

The current ABI deliberately does not expose arbitrary register writes for
SMA polarity/calibration, signal generators, I2C data writes, flash, or PTM.
Those controls should be enabled only after dedicated, validated kernel IOCTLs
are added using the Linux driver behavior as the reference.
