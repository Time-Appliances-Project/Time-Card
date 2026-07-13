# Windows Time Card Driver

This directory contains the Windows x64 KMDF driver for the Meta/OCP Time Card
(`PCI\VEN_1D9B&DEV_0400`). Windows does not provide a PHC class equivalent to
Linux `/dev/ptpN`, so applications use the versioned IOCTL ABI through
`timecardctl.exe`.

## Features

- PHC read, set, and system-bracketed cross-timestamp operations.
- FPGA, TOD, synchronization, clock source, UTC/leap, satellite, and GNSS
  status reporting.
- Polled access to the GNSS, GNSS2, atomic-clock, and NMEA 16550 UARTs.
- MSI and MSI-X/LitePCIe BAR layouts matching the Linux `ptp_ocp` driver.
- A dedicated **Time Card** Device Manager class with a custom icon.
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
  `timecard.cat`, and `timecard.ico`).
- `out\timecardctl.exe` - command-line control tool.

The project uses WDK test signing for development builds.

## Install

Secure Boot must be disabled before a locally test-signed kernel driver can
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
timecardctl uart-config 0 115200
timecardctl uart-read 0 256 1000
timecardctl hierarchy-status
timecardctl hierarchy-enable
timecardctl hierarchy-persist
timecardctl hierarchy-disable
```

UART ports are `0=GNSS`, `1=GNSS2`, `2=atomic clock`, and `3=NMEA`.

## Device Manager

Device Manager's **Devices by type** view lists the controller and its
subsystems in the dedicated **Time Card** category. **Devices by connection**
shows the subsystem PDOs nested under the PCI controller.

![Time Card controller and subsystem devices in Windows Device Manager](assets/device-manager-time-card.png)

Production deployment requires an EV/attestation-signed catalog. The included
WDK certificate is suitable only for development and test systems.
