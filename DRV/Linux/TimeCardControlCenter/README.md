# Time Card Control Center for Linux

Time Card Control Center is a native Rust desktop application for the OCP Time
Card. Its interface uses Relm4, GTK4, and libadwaita. It has no Qt, QML, C++, or
.NET runtime dependency.

The current Linux hardware surface is intentionally read-only. The application
discovers `ptp_ocp` cards, samples their precision hardware clocks, inventories
timing and FPGA resources, reads standard sensor and LED classes, and monitors
an `oscillatord` protocol v1 endpoint.

## Implemented workspaces

- Overview with precision timing, hardware identity, rolling offset and sample
  window plots, diagnostics, and a bounded session log
- Timing I/O with SMA routing, signal generators, frequency counters, the FPGA
  image contract, and optional PPS, NMEA, ToD, IRIG, and DCF engine attributes
- Sensors and LEDs with PCI-scoped R4006 hwmon, IIO, and LED class telemetry
- GNSS and serial with supervisor state, ToD configuration, capabilities, and
  the card's discovered UART endpoints
- Oscillatord with clock, oscillator, GNSS, antenna, convergence, holdover, and
  control-policy telemetry

Telemetry acquisition runs in a Relm4 worker, never on the GTK main thread.
The app polls card telemetry once per second and the endpoint-scoped
`oscillatord` service every five seconds. `--mock` provides two simulated cards
for development and demonstrations.

## Linux dependencies

Ubuntu 24.04:

```sh
sudo apt install build-essential pkg-config libgtk-4-dev libadwaita-1-dev \
  linux-libc-dev
```

Debian 12 provides a compatible GTK 4.8 and libadwaita 1.2 baseline. The Rust
toolchain must be 1.93 or newer.

## Build and test

```sh
cargo build --release
cargo test --no-default-features
cargo clippy --all-targets --all-features -- -D warnings
```

Run against installed hardware or the simulation:

```sh
cargo run --release
cargo run --release -- --mock
```

Useful development options:

```text
--sysfs-root PATH          Read a fixture tree instead of /sys/class/timecard
--hwmon-root PATH          Read a fixture tree instead of /sys/class/hwmon
--iio-root PATH            Read a fixture tree instead of /sys/bus/iio/devices
--leds-root PATH           Read a fixture tree instead of /sys/class/leds
--oscillatord-host HOST    Monitoring host, default 127.0.0.1
--oscillatord-port PORT    Monitoring port, default 2958
--page PAGE                overview, timing-io, sensors, gnss, or oscillatord
--quit-after MILLISECONDS  Exit after a bounded GUI smoke test
```

Core logic can be built and tested on a host without GTK development packages:

```sh
cargo test --no-default-features
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

Log out and back in after changing group membership. The rule matches only
devices whose clock name is `ptp_ocp`.

## Clock semantics

The Time Card PHC operates in TAI while Linux `CLOCK_REALTIME` operates in UTC.
The backend prefers the kernel TAI offset from `adjtimex()`, then uses the card
attribute as a guarded fallback:

```text
system_tai = system_utc + utc_tai_offset
phc_offset = phc_tai - system_tai
phc_utc = phc_tai - utc_tai_offset
```

PHC sampling tries `PTP_SYS_OFFSET_PRECISE`, a five-sample
`PTP_SYS_OFFSET_EXTENDED` minimum-window selection, and finally bracketed
`clock_gettime`. Comparing raw PHC TAI directly with `CLOCK_REALTIME` would
produce a false leap-second-sized offset.

## Safety boundary

The desktop process does not open serial ports automatically, write sysfs,
change the system clock, consume timestamp events, access debugfs, or issue
oscillatord control requests. Future writes should use a narrow D-Bus service
with polkit authorization, typed operations, selected-card scoping, readback
verification, and rollback where the device supports it.

Sensor and LED telemetry is scoped to the selected PCI function. Oscillatord
protocol v1 identifies only its configured endpoint, not a particular Time
Card, so that workspace is explicitly labeled endpoint-scoped.

## Install desktop integration

After building the release binary:

```sh
sudo install -Dm0755 target/release/timecard-control-center \
  /usr/local/bin/timecard-control-center
sudo install -Dm0644 packaging/org.opentimeserver.TimeCardControlCenter.desktop \
  /usr/local/share/applications/org.opentimeserver.TimeCardControlCenter.desktop
sudo install -Dm0644 packaging/org.opentimeserver.TimeCardControlCenter.svg \
  /usr/local/share/icons/hicolor/scalable/apps/org.opentimeserver.TimeCardControlCenter.svg
sudo install -Dm0644 packaging/org.opentimeserver.TimeCardControlCenter.metainfo.xml \
  /usr/local/share/metainfo/org.opentimeserver.TimeCardControlCenter.metainfo.xml
```
