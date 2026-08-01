# Time Card Control Center for Linux

This directory contains the Qt 6 Linux dashboard for the OCP Time Card. The
first milestone is intentionally read-only. It discovers `ptp_ocp` devices,
samples the PHC safely, presents TAI-aware UTC telemetry, inventories hardware
capabilities and serial endpoints, and monitors the local `oscillatord`
service.

## Implemented

- Strict `/sys/class/timecard/ocpN` discovery with multi-card selection
- PCI identity, serial number, PHC node, and UART endpoint discovery
- `PTP_SYS_OFFSET_PRECISE` sampling when hardware cross timestamps are
  available
- Five-sample `PTP_SYS_OFFSET_EXTENDED` fallback with minimum-window selection
- Bracketed `clock_gettime` fallback
- Correct TAI-to-UTC conversion using the kernel TAI offset, with the card
  attribute as a guarded fallback
- PHC-to-system offset and sampling-window histories
- Clock source, GNSS supervisor, ToD, FPGA offset, and drift status
- Read-only `oscillatord` status over its bounded loopback TCP protocol
- Explicit `--mock` mode for development, screenshots, and demonstrations
- Qt Quick dashboard without Qt Charts or other GPL-only Qt add-ons

The GUI never opens serial ports automatically, writes sysfs, changes the
system clock, consumes timestamp events, reads debugfs, or sends oscillatord
control requests.

## Dependencies

Ubuntu 24.04 or Debian 12 packages:

```sh
sudo apt install build-essential cmake ninja-build linux-libc-dev \
  qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools
```

The distribution must also provide the Qt Quick Controls runtime QML modules.
See the CI workflow for the complete Ubuntu package list.

## Build and test

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run against installed hardware:

```sh
./build/timecard-control-center
```

Run without hardware:

```sh
./build/timecard-control-center --mock
```

## PHC access without root

The dashboard must read the Time Card PHC, but it should not run as root. The
included udev rule grants read-only access to `ptp_ocp` clock devices for
members of a dedicated `timecard` group:

```sh
sudo groupadd --system --force timecard
sudo usermod -aG timecard "$USER"
sudo install -Dm0644 packaging/70-timecard-control-center.rules \
  /etc/udev/rules.d/70-timecard-control-center.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=ptp
```

Log out and back in after changing group membership, then start the dashboard
normally. The rule matches `ATTR{clock_name}=="ptp_ocp"`; it does not broaden
access to unrelated `/dev/ptpN` devices.

Useful development options:

```text
--sysfs-root PATH          Read a fixture tree instead of /sys/class/timecard
--oscillatord-host HOST    Monitoring host, default 127.0.0.1
--oscillatord-port PORT    Monitoring port, default 2958
--page NAME                Open overview, gnss, or oscillatord
--screenshot PATH          Capture the rendered dashboard and exit
--quit-after MILLISECONDS  Run a bounded smoke test
```

## Clock semantics

The Time Card PHC operates in TAI while Linux `CLOCK_REALTIME` operates in UTC.
The dashboard prefers the kernel TAI offset reported by `adjtimex()`. A positive
card `utc_tai_offset` is used only when the kernel offset is unavailable. Both
values are shown when they differ. The dashboard calculates:

```text
system_tai = system_utc + utc_tai_offset
phc_offset = phc_tai - system_tai
phc_utc = phc_tai - utc_tai_offset
```

Comparing the raw PHC value directly to `CLOCK_REALTIME` would produce a false
offset equal to the accumulated leap seconds.

## Next slices

1. Passive UART capture with explicit ownership and UBX/NMEA decoding
2. GNSS satellite and timing-quality workspace
3. SMA, signal-generator, and frequency-counter controls through a privileged
   D-Bus/polkit helper
4. I2C sensors, status LEDs, and atomic-clock workspaces
5. Guarded system synchronization and gateware update workflows

The UI remains unprivileged. Future write operations belong in a narrow,
auditable helper rather than in the desktop process.
