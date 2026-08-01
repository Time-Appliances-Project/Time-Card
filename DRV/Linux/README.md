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
