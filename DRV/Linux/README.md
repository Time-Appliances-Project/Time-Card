# Linux Time Card Driver

The Linux driver is based on the upstream kernel module for CentOS and Ubuntu.
Kernel 5.12 or newer is recommended.

## Build and install

Make sure VT-d is enabled in the BIOS. Build the module for the running kernel:

```sh
cd DRV/Linux
./remake
```

Install and load it with:

```sh
sudo ./remake install
sudo modprobe ptp_ocp
```

Run `./remake clean` to remove kernel-module build output. `KVER` and `KDIR`
can override the target kernel release and build directory.

## Modern kernels (6.x and later)

`ptp_ocp.c` carries version guards so it builds from 5.x up to current kernels
(verified against 7.0). The guarded APIs include the PTM cross-timestamp
interface, timer APIs, const-qualified `struct bin_attribute` callbacks and
groups, the `device_find_child()` match signature, and the embedded PPS
device.

### PTM (Precision Time Measurement)

With LitePCIe-with-PTM gateware (`TimeCardPTM_V31` or later), the driver
enables PCIe PTM at probe and implements `getcrosststamp()` through hardware
PTM dialogs. This allows `PTP_SYS_OFFSET_PRECISE` to work and lets tools such
as `phc2sys` and `chrony` use the hardware cross timestamp. The root port above
the card must advertise the PTM Root capability.

Verify PTM after boot:

```sh
lspci -vvv -s <card> | grep -A3 "Precision Time"
dmesg | grep "PTM enabled"
```

### Flashing note (spi-xilinx)

Updating gateware with `devlink dev flash` on modern kernels requires
`0002-spi-xilinx-Inhibit-transmitter-modern-kernels.patch`. It is a current
rebase of the original spi-xilinx patch. Install the patched `spi-xilinx.ko`
under `/lib/modules/$(uname -r)/updates/` before flashing.

## Exposed resources

The main resource directory is `/sys/class/timecard/ocpN`. It provides links
to the Time Card PHC and serial devices, along with configuration attributes
for clock sources, SMA routing, cable delays, UTC/TAI offset, and board status.

Device links can be used directly in scripts:

```sh
tty=$(basename "$(readlink /sys/class/timecard/ocp0/ttyGNSS)")
ptp=$(basename "$(readlink /sys/class/timecard/ocp0/ptp)")

echo "/dev/$tty"
echo "/dev/$ptp"
```

After the driver loads, the card exposes:

- a PTP POSIX hardware clock (`/dev/ptpN`)
- GNSS serial (`ttyGNSS`)
- atomic clock serial (`ttyMAC`)
- NMEA output serial (`ttyNMEA`)
- an I2C controller (`/dev/i2c-*`)

Standard `linuxptp` tools such as `phc2sys`, `ptp4l`, and `ts2phc` can use the
PHC.

## R4006 I2C peripherals and status LEDs

The [`R4006`](R4006) directory provides the PCA9546 mux, standard hwmon and
IIO sensor enumeration, IS32FL3207 multicolor LED support, the automatic GNSS
and SMA LED policy, CentOS Stream 10 backports, and the USB-C EEPROM utility.

## Rust Control Center

[`TimeCardControlCenter`](TimeCardControlCenter) provides a native Rust Linux
dashboard built with Relm4, GTK4, and libadwaita. It shows PHC telemetry,
TAI-aware system offset, GNSS and clock status, device endpoints, timing I/O,
FPGA engine status attributes, R4006 sensors and LEDs, common `oscillatord` v1
telemetry, rolling plots, and a bounded session log. Direct Time Card hardware
access is read-only and scoped to the selected PCI device; `oscillatord`
remains scoped to its configured endpoint because protocol v1 does not
identify a card. The built-in simulation mode supports development without
hardware. Build, packaging, and safety details are in its
[`README.md`](TimeCardControlCenter/README.md).

## FPGA timing-core controls

The documented NetTimeLogic timing cores are exposed under
`/sys/class/timecard/ocpN`.  The files are present for the published Facebook
and Celestica resource maps; an individual operation returns `EOPNOTSUPP` when
the card, core revision, or bitstream does not provide that register.  Core
version checks establish register-layout compatibility only.  Protocols and
other functions controlled by FPGA synthesis generics still require the
bitstream's build contract or a hardware loopback test to prove that the logic
was compiled in.

The four signal generators live in `gen1` through `gen4`.  In addition to the
existing `signal`, `duty`, `phase`, `period`, `polarity`, `running`, and `start`
files, current cores expose:

- `repeat_count` for finite pulse trains (generator version 1.3 or newer)
- `cable_delay` in nanoseconds (generator version 1.2 or newer)

The PPS core controls are:

- `external_pps_polarity` and `internal_pps_polarity`
- `external_pps_pulse_width`, bounded to the documented 1-999 ms range
- `external_pps_cable_delay` and `internal_pps_cable_delay`, using signed
  nanoseconds

IRIG/DCF configuration and sticky error handling are available through
`irig_output_mode`, `irig_input_mode`, `irig_b_mode`,
`irig_output_control_bits`, `irig_input_cable_delay`,
`dcf_input_air_delay`, `dcf_input_bit_position`, and the four `*_error` files.
Write `1` to a set error file to clear its write-one-to-clear status bit. The
synthesis-optional IRIG controls are additionally protected by the per-device
exact-image contract described below. IRIG Master mode and code selection are
part of the published core interface and require only Master 1.2; they do not
require a synthesis opt-in. IRIG Slave mode selection similarly requires only
Slave 1.3. With an exact-image contract, IRIG Master 1.5 exposes
`irig_output_am`; IRIG Slave 1.5 exposes `irig_input_code` and
`irig_input_manual_year`; and IRIG Slave 1.6 exposes `irig_input_am`. Manual
years are limited to 1970-2069 and are committed with the core disabled and the
documented self-clearing `YEAR_VAL` strobe.
IRIG mode, code, AM, control-bit, and manual-year stores preserve reserved bits
and the prior enable state, verify readback, and restore the previous register
values when the hardware does not accept the change.

The ToD parser exposes `tod_protocol`, `available_tod_protocols`, `tod_gnss`,
`available_tod_gnss`, `tod_message_disable_mask`, `tod_uart_polarity`,
`tod_errors`, `tod_baud_rate`, `available_tod_baud_rates`, and
`tod_correction`.  Configuration changes use the manuals' required
disable-change-restore sequence.  A protocol shown as version-compatible can
still be absent from a custom bitstream when its corresponding synthesis
generic was disabled.

`tod_message_disable_mask` is checked against the selected parser and core
revision.  The accepted masks are NMEA `0x1b` (`0x1f` from 2.0), UBX `0x07`
(`0x1f` from 1.7), TSIP `0x1f`, ESIP `0xff`, and PFEC `0x7f` from 2.3; bits
outside that set are rejected. PFEC is protocol selector `4`. NMEA GNSS and
satellite telemetry is considered layout-compatible only from ToD Slave 2.2;
PFEC UTC, GNSS, and satellite telemetry requires 2.3. Those telemetry reads
still require both the telemetry opt-in and exact-image contract below.
`tod_uart_polarity` exposes the raw FPGA convention: `1` is normal polarity and
`0` is inverted. Driver initialization preserves the protocol and message gates
selected by the FPGA image rather than assuming UBX from the interface version.

The separate ToD Master/NMEA generator exposes `nmea_enable`,
`nmea_uart_polarity`, `nmea_baud_rate`, `available_nmea_baud_rates`,
`nmea_errors`, and signed `nmea_correction_seconds` in addition to the
local-offset, GNSS, and message-mask files below. `nmea_uart_polarity` uses the
raw FPGA convention: `1` is normal and `0` is inverted. `nmea_errors` reports
the version-1.1 sticky error bit; write `1` to acknowledge it. The driver
deliberately does not probe, enable, or rewrite this synthesis-optional core
during PCI initialization. Its firmware configuration is preserved until an
operator uses one of these typed files.

The manuals mark the Clock servo/log registers, ToD UTC/leap/GNSS/satellite
telemetry, and several extended IRIG functions as synthesis-optional. They are
therefore never accessed or configured by default, even when their offsets or
control bits exist in the newest register map. The feature flags alone are not
enough: the driver also requires an operator-supplied exact match against the
raw FPGA Image Version register, bound to the card's PCI BDF. First load the
driver without opt-ins and read the safe status file:

```sh
cat /sys/class/timecard/ocp0/optional_image_contract
# pci=0000:03:00.0 actual=0xXXXXXXXX expected=0x00000000 targeted=0 match=0 loader=0
```

After verifying that this exact image's build manifest guarantees the requested
generics, reload with both the reported raw word and that card's BDF, plus only
the required feature flags. For example:

```sh
sudo modprobe -r ptp_ocp
sudo modprobe ptp_ocp optional_image_device=0000:03:00.0 \
  optional_image_version=0xXXXXXXXX \
  clock_optional_registers=1 tod_optional_telemetry=1 \
  irig_optional_features=1
```

For multiple cards, use the read-only contract array instead of a module-wide
version. Each comma-separated entry independently binds one PCI function to
one raw image word:

```sh
sudo modprobe ptp_ocp \
  optional_image_contracts=0000:03:00.0=0xXXXXXXXX,0000:41:00.0=0xYYYYYYYY \
  clock_optional_registers=1 tod_optional_telemetry=1 \
  irig_optional_features=1
```

All contract and feature parameters are read-only after module load. The legacy
`optional_image_version` parameter remains accepted, but never enables a card
unless `optional_image_device` names that exact PCI BDF. A version alone cannot
act as a module-wide blanket. Unlisted cards remain gated even if they report
the same image word. Conflicting per-device entries and zero, invalid,
mismatched, loader, ART, or missing Image Versions can never satisfy the
contract. This also prevents an opt-in from silently carrying over after the
FPGA is reflashed. Do not use any option based only on a core version number. With
`tod_optional_telemetry=0` (the safe default), configure the card's UTC-to-TAI
offset through `utc_tai_offset`; the watchdog will not infer it from optional
ToD registers. The debugfs ToD report states when optional telemetry is gated.
Clock status is read only from version 1.2 onward, PPS Master/Slave status from
1.2/1.3, and IRIG/DCF status from 1.1.  Signal generators older than 1.3 keep
their stable enable register disabled because their timing and interrupt layout
differs; their revision-safe configuration files still return
`EOPNOTSUPP` where appropriate.

For example:

```sh
root=/sys/class/timecard/ocp0
cat "$root/available_tod_protocols"
echo NMEA | sudo tee "$root/tod_protocol"
echo 115200 | sudo tee "$root/tod_baud_rate"
echo 115200 | sudo tee "$root/nmea_baud_rate"
echo 1 | sudo tee "$root/nmea_uart_polarity"
echo -37 | sudo tee "$root/nmea_correction_seconds"
echo -300 | sudo tee "$root/nmea_local_offset_minutes"
echo COMBINED | sudo tee "$root/nmea_gnss"
echo 0x01 | sudo tee "$root/nmea_message_disable_mask"
echo 1 | sudo tee "$root/nmea_enable"
echo 100 | sudo tee "$root/gen1/repeat_count"
echo 25 | sudo tee "$root/gen1/cable_delay"
```

The `nmea_*` attributes configure the separate ToD Master output core. Local
offset accepts -839 through +839 minutes. GNSS/talker selection requires core
1.3; RMC gating requires 1.4 and proprietary UTC gating requires 1.6. The
driver disables the core around each write, preserves reserved bits and checks
both data and enable-state readback. Generic `utc_tai_offset` distribution does
not probe the optional ToD Master; configure its signed correction explicitly
through `nmea_correction_seconds`.
Set the `ttyNMEA` termios baud to the same value when consuming the generated
stream through the serial device.

## Oscillator disciplining service

The repository includes the complete Orolia/Safran
[`oscillatord`](../../Software/oscillatord) v3.10.0 source. On an ART card it
disciplines the mRO-50 from GNSS and PHC external timestamps, initializes the
PHC, exports PPS to NTP shared memory, and serves live monitoring telemetry.
The integrated backend prefers this driver's `/dev/mro50.N` IOCTL bridge and
falls back to `ttyMAC` only when the direct bridge is absent.

Install Linux prerequisites on Debian or Ubuntu, then build all pinned source
dependencies into an isolated prefix:

```sh
sudo apt-get install build-essential cmake git pkg-config libjson-c-dev \
  pps-tools libsystemd-dev libpath-tiny-perl libdata-float-perl
cd Software/oscillatord
bash ./tools/build-timecard.sh
```

For a system installation, use `sudo bash ./tools/install-timecard.sh`, review
`/etc/oscillatord.conf`, and only then run:

```sh
sudo systemctl enable --now oscillatord.service
systemctl status oscillatord.service
journalctl -u oscillatord.service -f
```

Monitoring binds to `127.0.0.1:2958` by default. State-changing requests are
disabled unless `monitoring-allow-control=true`; a long
`monitoring-control-token` is strongly recommended before exposing the endpoint
to the Control Center on another host. The protocol itself is not encrypted;
prefer an SSH tunnel such as `ssh -L 2958:127.0.0.1:2958 timecard-host` and keep
the Control Center endpoint at `127.0.0.1:2958`. Otherwise, restrict TCP/2958
to the management network with the host firewall.

## Upstream kernel support

- [Initial driver in Linux 5.2](https://git.kernel.org/pub/scm/linux/kernel/git/netdev/net-next.git/commit/?id=a7e1abad13f3f0366ee625831fecda2b603cdc17)
- [Integrated device support in Linux 5.15](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=773bda96492153e11d21eb63ac814669b51fc701)
