# R4006 I2C peripherals and status LEDs

This directory contains the Linux support used by the production R4006 Time
Card on CentOS Stream 10. It exposes the board's PCA9546 I2C topology,
IS32FL3207 front-panel RGB LEDs, and detected environmental sensors through
standard Linux subsystems.

The board EEPROM selects the R4006 profile. The verified front-panel mapping
is:

| Linux LED name | Physical controller index |
| --- | ---: |
| `gnss1` | 4 |
| `sma1` | 2 |
| `sma2` | 3 |
| `sma3` | 0 |
| `sma4` | 1 |

The R4006 sensor topology is:

| Mux channel | I2C address | Linux driver | Interface |
| ---: | ---: | --- | --- |
| 0 | `0x48` | `lm75` (`lm75b`) | hwmon |
| 0 | `0x49` | `lm75` (`lm75b`) | hwmon |
| 0 | `0x4a` | `lm75` (`lm75b`) | hwmon |
| 1 | `0x44` | `sht3x` | hwmon |
| 2 | `0x63` | `icp10100` | IIO |

The BNO080/BNO08x sensor hub at channel 3, address `0x4a`, is detected but is
not instantiated because the upstream Linux IIO tree does not provide a
BNO08x SHTP/SH-2 driver.

## Build on CentOS Stream 10

Install the compiler and matching kernel headers, then build the main driver
and the supplemental modules:

```sh
sudo dnf install gcc make kernel-devel-$(uname -r)

cd DRV/Linux
./remake build

cd R4006
make
```

The supplemental directory includes Linux v6.12 backports of `i2c-xiic`,
`at24`, `led-class-multicolor`, `sht3x`, and `icp10100`. The `icp10100`
source includes the CentOS Stream 10.3 IIO API compatibility guard. Do not
install these backports on a kernel that already supplies compatible modules
unless the in-tree modules have been tested with this card.

For Secure Boot, sign every `.ko` with a key enrolled on the target machine
before installing it. Never store the private signing key in this repository.
Install the modules, dependency configuration, and LED policy with:

```sh
cd DRV/Linux
sudo ./remake install

cd R4006
sudo make install
sudo install -m 0644 modprobe.d/ptp_ocp-i2c.conf \
  /etc/modprobe.d/ptp_ocp-i2c.conf
sudo install -m 0755 tools/timecard-ledctl /usr/local/sbin/timecard-ledctl
sudo install -m 0755 tools/timecard-led-policy \
  /usr/local/sbin/timecard-led-policy
sudo install -m 0644 systemd/timecard-led-policy.service \
  /etc/systemd/system/timecard-led-policy.service
sudo depmod -a
sudo systemctl daemon-reload
sudo systemctl enable --now timecard-led-policy.service
```

Load order is defined by `modprobe.d/ptp_ocp-i2c.conf`. After a reboot, verify
the standard interfaces:

```sh
ls -l /sys/class/leds/timecard-*:rgb:indicator-*
find /sys/bus/i2c/devices -maxdepth 2 -name hwmon -print
find /sys/bus/iio/devices -maxdepth 2 -name name -print -exec cat {} \;
timecard-ledctl list
```

## LED policy

`timecard-led-policy` polls the first Time Card once per second. It shows GNSS
fixed as green, searching as amber, and no fix as red. SMA outputs are green,
inputs are blue, and disabled or unknown connectors are amber. GNSS state is
read from the local `oscillatord` monitoring socket when available, with the
Time Card debugfs telemetry used as a fallback.

Use `timecard-ledctl` for manual tests. The policy service will restore the
configuration-derived color on its next poll.

## EEPROM programming over USB-C

`tools/timecard-flash-eeprom` uses the FT4232H USB-C interface to back up,
program, and verify the 256-byte board EEPROM at `0x50`. It never accesses the
identity EEPROM at `0x58`. Install its Python dependencies and keep the
approved production image outside the repository:

```sh
python3 -m pip install pyftdi pyusb
sudo install -m 0755 tools/timecard-flash-eeprom \
  /usr/local/sbin/timecard-flash-eeprom
sudo timecard-flash-eeprom --image /root/timecard-eeprom/eeprom.bin
sudo timecard-flash-eeprom --image /root/timecard-eeprom/eeprom.bin --flash
```

The first command performs a read and backup only. The second requires typing
`FLASH`, writes only changed 8-byte pages, verifies each page, and verifies the
complete image afterward. The utility accepts only the approved production
image SHA-256 embedded in the script.
