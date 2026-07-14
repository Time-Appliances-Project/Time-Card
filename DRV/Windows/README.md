# Windows Time Card Driver

This directory contains the Windows x64 KMDF driver for the Meta/OCP Time Card
(`PCI\VEN_1D9B&DEV_0400`). Windows does not provide a PHC class equivalent to
Linux `/dev/ptpN`, so applications use the versioned IOCTL ABI through
`timecardctl.exe`.

## Features

- PHC read, set, and system-bracketed cross-timestamp operations.
- FPGA, TOD, synchronization, selectable clock source, UTC/leap, satellite,
  and GNSS status reporting.
- Polled access to the GNSS, GNSS2, atomic-clock, and NMEA 16550 UARTs, plus
  explicit enable, baud, and polarity control for the FPGA NMEA generator.
- Validated query and routing control for all four SMA connectors, including
  input/output direction, named timing functions, fixed-direction detection,
  and immediate readback.
- Read-only Xilinx AXI IIC controller access with status, address-only probes,
  7-bit bus discovery, and bounded EEPROM/register reads. The known onboard
  devices are the board EEPROM at `0x50` and MAC EEPROM at `0x58`.
- A stable six-byte card serial number read from offset zero of the factory
  24MAC402 identity EEPROM, matching Linux `ptp_ocp`.
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

The repository also includes a polished native Windows dashboard for live PHC
and GNSS telemetry, clock control, direct u-blox receiver discovery and guarded
GNSS configuration, a dedicated Microchip MAC-SA53 atomic-clock configuration
workspace, UART monitoring and binary commands, SMA signal routing, I2C
discovery and reads, subsystem hierarchy management, and engineering
diagnostics.

```powershell
.\build-gui.cmd release
.\TimeCardControlCenter\bin\Release\TimeCardControlCenter.exe
```

The application can restart itself with administrator rights when the driver
requires elevation. See [TimeCardControlCenter/README.md](TimeCardControlCenter/README.md)
for complete build, UART, and capability details.

![OCP Time Card Control Center](assets/timecard-control-center.png)

| Clock-source control | NMEA generator and UART | SMA signal routing |
| --- | --- | --- |
| ![Clock-source control](assets/timecard-control-center-clock.png) | ![NMEA generator and UART](assets/timecard-control-center-nmea.png) | ![SMA signal routing](assets/timecard-control-center-sma.png) |

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

The NMEA generator is separate from the UART receiver. Driver 1.9 enables it
at 9,600 baud on first use when firmware left it disabled, and keeps the UART 3
receiver divisor synchronized whenever `nmea-set` changes the generator baud.
Supported rates are 1,200 through 2,000,000 baud using the selector table from
Linux `ptp_ocp`.

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
