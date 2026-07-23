# Windows Time Card Driver

This directory contains the Windows x64 KMDF driver for every PCI Time Card
profile enumerated by the repository's Linux `ptp_ocp` driver:

- Meta/Facebook OCP Time Card: `PCI\VEN_1D9B&DEV_0400`
- Celestica OCP Time Card: `PCI\VEN_18D4&DEV_1008`
- Orolia/Safran ART Time Card: `PCI\VEN_1AD7&DEV_A000`

Windows does not provide a PHC class equivalent to Linux `/dev/ptpN`, so
applications use the versioned IOCTL ABI through `timecardctl.exe`.

## Features

- PHC read, set, and system-bracketed cross-timestamp operations.
- FPGA, TOD, synchronization, selectable clock source, UTC/leap, satellite,
  and GNSS status reporting.
- Polled access to the GNSS, GNSS2, atomic-clock, and NMEA 16550 UARTs, plus
  explicit enable, baud, and polarity control for the FPGA NMEA generator.
- Validated query and routing control for all four SMA connectors, including
  input/output direction, named timing functions, fixed-direction detection,
  and immediate readback.
- Bounded configuration and readback for four periodic signal generators and
  four frequency counters, including direct SMA input/output routing.
- Guarded Xilinx AXI IIC and OpenCores I2C controller access with status,
  address-only probes,
  7-bit bus discovery, bounded EEPROM/register reads, PCA9546A branch routing,
  dedicated IS32FL3207 LED updates, open/short diagnostics, and a bounded
  electrical test. The known identity devices are the board EEPROM at `0x50`
  and MAC EEPROM at `0x58`.
- Guarded one-shot telemetry for BME280/BMP280 environment sensors, three INA219
  power monitors, and BNO055 or BNO08x nine-axis IMUs on an auto-detected
  PCA9546A branch.
- Guarded Xilinx and Altera SPI/SPI-NOR access for FPGA firmware query,
  4 KiB erase,
  page programming, and bounded read-back. All offsets are relative to the
  FPGA image region at `0x00400000`; the configuration region is unreachable.
- Non-destructive UART receive-activity observation for subsystem presence
  checks without removing bytes from the receiver FIFO.
- A stable six-byte card serial number read from the factory-programmed
  EUI-48 area at raw 24MAC402 offset `0x9a`, matching the identity Linux
  exposes through the `24mac402` NVMEM provider to `ptp_ocp`.
- MSI and MSI-X/LitePCIe BAR layouts matching the Linux `ptp_ocp` driver.
- A dedicated **Time Card** Device Manager class with custom controller and
  subsystem icons.
- Raw child devices for the PHC, TOD/GNSS engine, four UARTs, SMA and timing
  I/O, I2C, FPGA/SPI flash, and PCIe PTM.

The controller owns all hardware access. The child devices describe and group
the integrated functions; they do not load separate function drivers. Each
child is auto-named, explicitly secured for Administrators and SYSTEM, and
created through a fail-open path so an optional child cannot prevent the
controller from starting.

## Hardware compatibility

Driver 1.24 / ABI 8 selects the same three board profiles and resource maps as
the Linux `ptp_ocp` driver. Meta/Facebook and Celestica share the rev1 and rev2
maps. Older PCI revision 00 gateware uses the rev1 MSI map and may expose 2 or
32 interrupt messages. Current PCI revision 02 LitePCIe gateware exposes 64
MSI-X messages and uses the rev2 map. The Windows driver also checks that the
selected PHC and ToD windows fit the assigned BAR and falls back to the other
known Meta/Celestica map before any register access when firmware does not
report a useful revision.

The Orolia/Safran ART profile uses its Linux-defined fixed map: PHC at
`0x01000000`, primary GNSS UART at `0x00161000`, atomic-clock UART at
`0x00190000`, SMA routing at `0x003c0000`, OpenCores I2C at `0x00350000`, and
Altera SPI at `0x00310000`. Its protected FPGA image begins at `0x01000000` in
SPI-NOR. The ART profile exposes only the PHC, primary GNSS, atomic-clock, SMA,
I2C, and flash child nodes because Linux does not map the Meta-specific ToD,
secondary GNSS, NMEA generator/UART, signal generators, frequency counters, or
PTM blocks on that card. Missing functions return a supported “not available”
result rather than aliasing another register window.

ART uses an OpenCores I2C controller and a 24c08 EEPROM. The serial Linux reads
at absolute EEPROM offset `0x263` is accessed through slave block `0x52`,
subaddress `0x63`. Its SMA menu is restricted to the ART gateware's PPS1 and
10 MHz inputs plus atomic-clock, GNSS, and 10 MHz outputs. Its UART 2 device is
an mRO-50 at 9,600 baud; the Control Center disables MAC-SA53 commands on this
profile and leaves the generic UART console available.

Subsystem availability still depends on the FPGA image and populated board
options. Missing optional resources never prevent the controller and PHC from
starting.

The rev1 path has been validated on a connected revision 00 implementation
with a 32 MiB BAR, 16 assigned MSI messages, clock core 1.2.0, and ToD core
2.0.0.1. Its PHC, primary GNSS/NMEA stream, NMEA generator, four SMA routes,
I2C controller, board EEPROM, and MAC EEPROM are accessible. That FPGA image
returns all ones for the optional GNSS summary registers; raw UART access
continues to work and the Control Center reports those summaries as unavailable
instead of interpreting them as 255 satellites.

## Build

Install Visual Studio with the Windows Driver Kit, then use an x64 developer
prompt:

```bat
cd DRV\Windows
build.cmd
```

Build output is written to:

- `x64\Release\timecard\` - driver package (`timecard.inf`, `timecard.sys`,
  `timecard.cat`, the controller icon, and subsystem icons).
- `out\timecardctl.exe` - command-line control tool.

## Time Card Control Center

The repository also includes a polished native Windows dashboard covering live
PHC telemetry and clock synchronization, u-blox GNSS configuration and a sky
map, Microchip MAC-SA53 atomic-clock control, protocol-aware u-blox UART
decoding, decoded FPGA NMEA sentences, dynamically enumerated Windows COM
ports, and multi-format raw monitoring,
NMEA generation, SMA routing, timing generators and frequency counters,
sensors and IMU telemetry, I2C mux and status-LED control, subsystem hierarchy,
FPGA firmware updates, and engineering diagnostics. The application includes
an animated, aspect-preserving Time Card identity, a polished splash screen,
clear connection state, and one-click administrator restart when required.
It also includes a clickable end-to-end timing health topology, a timestamped
Telemetry Studio with CSV/JSON recording, cross-timestamp distribution and
percentiles, and a gravity-compensated IMU vibration trace with rolling RMS;
verified configuration profiles with automatic rollback; a read-only guided
hardware self-test; privacy-conscious ZIP support bundles; responsive themes;
and a synthetic no-write demo mode.
The serial laboratory supports complete generic COM framing and flow-control
settings, filters, counters, capture, offline decoder replay, and text, CSV,
JSON, or binary export.

```powershell
.\build-gui.cmd release
.\TimeCardControlCenter\bin\Release\TimeCardControlCenter.exe
```

The application can restart itself with administrator rights when the driver
requires elevation. See [TimeCardControlCenter/README.md](TimeCardControlCenter/README.md)
for complete build, UART, and capability details.

Driver **1.24 / ABI 8** is required for the complete feature set shown below.

![Control Center overview](assets/timecard-control-center.png)

| Precision clock | GNSS and sky map | Atomic clock |
| --- | --- | --- |
| ![Precision clock workspace](assets/timecard-control-center-clock.png) | ![GNSS workspace](assets/timecard-control-center-gnss.png) | ![Atomic clock workspace](assets/timecard-control-center-atomic.png) |
| UART and NMEA | SMA connectors | Generators and frequency |
| ![UART and NMEA workspace](assets/timecard-control-center-nmea.png) | ![SMA connector workspace](assets/timecard-control-center-sma.png) | ![Timing generator workspace](assets/timecard-control-center-timing.png) |
| Sensors and IMU | I2C and status LEDs | Subsystem map |
| ![Sensors and IMU workspace](assets/timecard-control-center-sensors.png) | ![I2C and LED workspace](assets/timecard-control-center-i2c.png) | ![Subsystem workspace](assets/timecard-control-center-subsystems.png) |
| Telemetry Studio | Profiles and self-test | FPGA SPI flash |
| ![Telemetry Studio workspace](assets/timecard-control-center-telemetry.png) | ![Profiles and self-test workspace](assets/timecard-control-center-operations.png) | ![FPGA SPI-flash firmware update workspace](assets/timecard-control-center-flash.png) |

The default Release build is unsigned and ready for Microsoft production
signing. Use `build.cmd test` only for a local development package. To create
the HLK driver folder and an attestation CAB, see
[PRODUCTION_SIGNING.md](PRODUCTION_SIGNING.md).

## Install

For local development only, build a test-signed package first:

```bat
build.cmd test
```

Secure Boot must be disabled before that locally test-signed kernel driver can
load. From an Administrator PowerShell prompt:

```powershell
cd DRV\Windows
.\install.ps1
```

The script enables Windows test-signing and stages or installs the package.
Reboot when requested, then verify the controller:

```powershell
.\verify.ps1 -SetFromSystem -TestGnssUart
```

## Device hierarchy

Subsystem enumeration is disabled on first install. After the controller,
PHC, and UART checks pass, enable the hierarchy without persisting it:

```powershell
.\out\timecardctl.exe hierarchy-enable
.\verify.ps1 -ExpectHierarchy -TestGnssUart
```

Persist the verified hierarchy for subsequent device starts:

```powershell
.\out\timecardctl.exe hierarchy-persist
```

`hierarchy-disable` clears persistence. Existing child nodes disappear after
the Time Card device or Windows is restarted.

## Control tool

Run the tool from an Administrator prompt:

```text
timecardctl status
timecardctl get
timecardctl set-system
timecardctl clock-source tod
timecardctl serial
timecardctl nmea-status
timecardctl nmea-set on 9600 normal
timecardctl uart-read 3 256 1000
timecardctl uart-config 0 115200
timecardctl uart-read 0 256 1000
timecardctl hierarchy-status
timecardctl hierarchy-enable
timecardctl hierarchy-persist
timecardctl hierarchy-disable
timecardctl sma-status
timecardctl sma-status 1
timecardctl sma-set 1 input 0x0001
timecardctl sma-set 3 output 0x0001
timecardctl sma-set 1 disabled
timecardctl i2c-status
timecardctl i2c-scan
timecardctl i2c-read 0x50 0x00 32 1
timecardctl sensors
timecardctl led-status
timecardctl led-set 1 0 180 30 96
timecardctl led-test
```

UART ports are `0=GNSS`, `1=GNSS2`, `2=atomic clock`, and `3=NMEA`.

SMA selectors use the same values as Linux `ptp_ocp`. Common input values are
`0x0000=10 MHz`, `0x0001=PPS1`, `0x0002=PPS2`, `0x0004=TS1`,
`0x0010=IRIG-B`, and `0x0020=DCF77`. Common output values are `0x0000=10 MHz`,
`0x0001=PHC`, `0x0002=atomic clock`, `0x0004=GNSS1`, `0x0008=GNSS2`,
`0x0010=IRIG-B`, and `0x0020=DCF77`. Fixed-direction firmware is handled with
the same mode-change ordering as Linux: the driver never writes the absent
opposite-direction map and verifies the applied function by immediate
readback. Disconnect an external source before changing a connector to output.

I2C reads use 7-bit addresses. `i2c-read` performs a combined subaddress write
and repeated-start read; the final argument selects a 0-, 1-, or 2-byte
subaddress. Transfers are limited to 255 bytes. The `serial` command reads the
same six identity bytes used by the Linux `serialnum` attribute. The ABI
deliberately provides no I2C data-write or EEPROM-programming operation.

Driver 1.11 fixes the AXI IIC dynamic-receive completion sequence. It waits for
the optional subaddress message to leave the transmit FIFO, drains data only
after the receive watermark event, advances that watermark for reads longer
than one FIFO, and clears the expected final receive NACK with `RX_FULL`. A
failed transient transaction is reset and retried once within the requested
bounded timeout. Driver 1.12 also reports controller transport failures as I/O
device errors instead of the misleading Windows "CRC data error" translation.

The NMEA generator is separate from the UART receiver. Driver 1.9 enables it
at 9,600 baud on first use when firmware left it disabled, and keeps the UART 3
receiver divisor synchronized whenever `nmea-set` changes the generator baud.
Supported rates are 1,200 through 2,000,000 baud using the selector table from
Linux `ptp_ocp`.

Driver 1.10 / ABI 5 adds dedicated controls for all four periodic signal
generators and frequency counters. Generator configuration follows the Linux
`ptp_ocp` register sequence: disable, program a PHC-aligned start, period,
pulse width and polarity, then assert valid plus enable. Frequency counters
accept an integration window of 0 (disabled) or 1–255 seconds and report the
FPGA valid, error, overrun, and 24-bit frequency-result fields.

Driver 1.12 / ABI 6 adds the guarded FPGA flash interface and UART activity
observation. The SPI controller offsets, Xilinx FIFO sequence, FPGA image
start, 4 KiB erase geometry, and OCPC image-header format follow Linux
`ptp_ocp`. Flash programming cannot address bytes below `0x00400000`, cannot
cross a 256-byte page, and requires write-enable plus ready polling. The
Control Center performs a complete read-back comparison after programming.

Driver 1.13 / ABI 7 adds schematic-aware controls for U27, the PCA9546A at
`0x70`, and U6, the IS32FL3207 on mux channel 1. The four mux bits
route the MAC clock, sensor, analog/ADC expansion, and DC expansion branches.
LED IOCTLs are limited to the six RGB indicators (GNSS1, GNSS2, IO1â€“IO4),
use 8-bit PWM, cap global current at 128, and restore the caller's mux mask
after every operation. Arbitrary I2C writes and EEPROM programming remain
unavailable.

Driver 1.14 / ABI 7 corrects the AXI IIC dynamic-mode startup handshake,
preventing the stale bus-not-busy latch from ending a transaction before its
START reaches the bus. It also reads the factory
EUI-48 from raw 24MAC402 offset `0x9a` and uses U6's 7-bit address `0x37`;
the schematic's `0x6e` label is the corresponding 8-bit write address.
Driver 1.14.6 also reports the IS32FL3207 per-output open/short masks and
supports a bounded electrical test. The test proves SDB state through the
device reset behavior, forces OUT1-18 on in DC mode for five seconds, and then
restores the prior colors. It does not expose arbitrary I2C writes.

Driver 1.15.1 / ABI 8 adds a guarded sensor-telemetry query for every populated
device on PCA9546A channel 1. It triggers a compensated BME280 environment
sample at `0x76`, reads the +12 V, +5 V, and +3.3 V INA219 monitors at `0x40`,
`0x41`, and `0x44` using the schematic's 2 milliohm shunts, and starts the
BNO055 at `0x29` in NDOF fusion mode with the fitted 32.768 kHz crystal. A
failed or absent sensor is reported independently. The driver restores the
caller's mux mask after every sample and still exposes no arbitrary write API.

Driver 1.18 / ABI 8 corrects the short-transfer completion ordering to match
Linux `i2c-xiic`: START/address is queued, stale completion state is cleared,
and only then is the register payload appended. This prevents a fast
one-register transaction from losing its real TX-empty event. The driver
auto-detects BME280 or BMP280 at `0x76`/`0x77`, BNO055 at `0x28`/`0x29`, and
all four IS32FL3207 AD strap addresses (`0x34`-`0x37`), while continuing to
prefer the V9 schematic's `0x76`, `0x29`, and `0x37` choices. LED programming
now reads back control, global current, per-channel scaling, PWM, and DC-mode
registers before reporting success.

Driver 1.19 / ABI 8 initializes any responding INA219 that firmware left in
reset/power-down. It programs the safe 32 V, +/-320 mV, 12-bit continuous
bus-and-shunt configuration, verifies the configuration readback, waits for a
conversion, and then reports voltage/current/power from the board's 2 milliohm
shunt.

Driver 1.23 / ABI 8 adds the verified Rev00/MSI indicator wiring map without
changing the original Rev02/MSI-X card: GNSS1/GNSS2 exchange physical groups
with IO3/IO4, IO1/IO2 remain direct, and red/green sink channels are exchanged.
Logical names and RGB values therefore remain consistent in the Control
Center on both assemblies. It also auto-discovers the populated sensor branch
instead of assuming PCA9546A channel 1 and supports the schematic-permitted
BNO080/BNO08x alternative at `0x4a`/`0x4b`. BNO08x SHTP reports are configured
and decoded into the existing ABI's fused orientation, acceleration, linear
acceleration, gravity, angular-velocity, and magnetic-field readings; BNO055
cards continue to use the NDOF register path.

Driver 1.24 adds a BNO08x liveness watchdog. If the IMU resets internally or
its finite SH-2 report queue stalls while the mux branch is idle, the next
telemetry request drains stale packets, re-establishes all six feature
subscriptions, resets the SHTP control-channel sequence, and retries the
sample without requiring a driver reload or reboot.

The schematic's U26 TMUX1072 is not software-controlled. Its `MACSER` select
comes only from the physical DIP switch: 0 routes MAC I2C and 1 routes the FPGA
MAC UART. The Control Center identifies this prerequisite instead of claiming
that U26 can be changed through the driver.

## Device Manager

Device Manager's **Devices by type** view lists the controller and its
subsystems in the dedicated **Time Card** category. **Devices by connection**
shows the subsystem PDOs nested under the PCI controller.

Each subsystem has a compact blue, gold, and white icon matched to its
function. The OCP Time Card Controller uses the green clock artwork, while
the Time Card category retains the original card artwork.

![Time Card subsystem icons](assets/subsystem-icon-sheet.png)

![OCP Time Card controller and subsystem icons in Windows Device Manager](assets/device-manager-time-card.png)

Production deployment requires the package returned by Microsoft Hardware Dev
Center. The repository does not contain a private signing key or a WDK test
certificate.
