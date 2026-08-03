# Time Card Control Center for Linux

This directory contains the Qt 6 Linux control center for the OCP Time Card.
The current interface is intentionally read-only. It discovers `ptp_ocp`
devices, samples the PHC safely, presents TAI-aware UTC telemetry, inventories
hardware and timing I/O, and monitors the configured `oscillatord` service.

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
- SMA routing, signal-generator, frequency-counter, and FPGA engine status
- R4006 mux-channel/address-matched hwmon and IIO sensor telemetry plus
  PCI-scoped status LEDs
- Common read-only `oscillatord` v1 clock, oscillator, GNSS, antenna,
  convergence, holdover, action, and received `control_enabled` state over a
  bounded TCP request
- Bounded structured session log with normalized text, JSON export, and clear
  controls shared by both frontends
- Explicit `--mock` mode for development, screenshots, and demonstrations
- Qt Quick dashboard without Qt Charts or other GPL-only Qt add-ons
- Qt Core and ncurses terminal dashboard with Overview, Timing I/O, Sensors,
  GNSS, oscillatord, and Help workspaces
- Plain one-shot terminal output for scripts, logs, SSH diagnostics, and CI

The frontends never open serial ports automatically, write sysfs, change the
system clock, consume timestamp events, read debugfs, or send oscillatord
control requests.

Time Card sysfs, sensor, and LED telemetry is scoped to the selected PCI
function. `oscillatord` protocol v1 identifies only its configured endpoint,
not a Time Card. On multi-card systems, the UI therefore labels that telemetry
as endpoint-scoped rather than attributing it to the selected `ocpN` device.

## Dependencies

Ubuntu 24.04 or Debian 12 packages:

```sh
sudo apt install build-essential cmake ninja-build linux-libc-dev \
  libncurses-dev qt6-base-dev qt6-declarative-dev \
  qt6-declarative-dev-tools
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

Run the terminal dashboard against installed hardware:

```sh
./build/timecard-control-center-tui
```

Run the terminal dashboard with simulated cards, or print one snapshot without
terminal control sequences:

```sh
./build/timecard-control-center-tui --mock
./build/timecard-control-center-tui --mock --plain
```

The terminal dashboard uses arrow keys or `j`/`k` to highlight a discovered
card and Enter to select it. Left/Right, Tab, or the number keys change
workspaces. PageUp/PageDown scroll the current workspace. Use `r` to refresh
card telemetry, `o` to refresh oscillatord, `?` for help, `x` to export the
structured session log, `c` to clear the in-memory log, and `q` to quit.

For servers that do not need Qt Quick, configure a headless TUI-only build.
This requires only the Qt base development package and ncurses:

```sh
cmake -S . -B build-tui -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DTIMECARD_BUILD_GUI=OFF \
  -DTIMECARD_BUILD_TUI=ON \
  -DBUILD_TESTING=ON
cmake --build build-tui --parallel
ctest --test-dir build-tui --output-on-failure
```

`TIMECARD_BUILD_GUI` defaults to `ON`. `TIMECARD_BUILD_TUI` defaults to `ON`
on Linux and can be disabled for a GUI-only package.

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
--hwmon-root PATH          Read a fixture tree instead of /sys/class/hwmon
--iio-root PATH            Read a fixture tree instead of /sys/bus/iio/devices
--leds-root PATH           Read a fixture tree instead of /sys/class/leds
--oscillatord-host HOST    Monitoring host, default 127.0.0.1
--oscillatord-port PORT    Monitoring port, default 2958
--page NAME                Open overview, timing-io, sensors, gnss,
                           oscillatord, or help (help is TUI-only)
--plain                    Print one TUI snapshot without escape sequences
--screenshot PATH          Capture the GUI dashboard and exit
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

## Windows parity boundary

This release imports a selected, portable, read-only subset of the current
Windows Control Center: expanded `oscillatord` v1 telemetry, timing-I/O and
FPGA status attributes, R4006 environmental and LED telemetry, multi-card
selection, and a structured session log. It does not claim parity with
Windows-only IOCTLs, Device Manager operations, protected named-pipe
transport, service lifecycle, or UTC-setting workflows. Portable Windows
features not yet included are UART capture/replay and UBX sky-map inspection,
timestamp-event and IMU telemetry, atomic-clock controls, profiles and
rollback, self-test and support bundles, Telemetry Studio, log filtering, and
text export. The Linux sysfs ABI also does not currently expose the Windows
static-core table with every core version, register span, and IRQ, so the Linux
UI accurately labels its view as readable FPGA engine status.

## Next slices

1. Passive UART capture with explicit ownership and UBX/NMEA decoding
2. GNSS satellite and timing-quality workspace
3. SMA, signal-generator, and frequency-counter controls through a privileged
   D-Bus/polkit helper
4. Atomic-clock telemetry and guarded control workspace
5. Guarded system synchronization and gateware update workflows

The UI remains unprivileged. Future write operations belong in a narrow,
auditable helper rather than in the desktop process.
