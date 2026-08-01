# oscillatord - daemon for disciplining an oscillator

Oscillatord aims at disciplining an oscillator to an external reference. It is responsible for fetching oscillator and reference data and pass them to a disciplining algorithm, and apply the decision of the algorithm regarding the oscillator.

It can be used to discipline the [Atomic Reference Time Card](https://www.orolia.com/about-the-atomic-reference-time-card-art-card/) from Orolia

Oscillatord can either be used to discipline and/or monitor a oscillatord a GNSS external reference.

For now only the following oscillators are supported:
- Orolia's mRO50
- Microsemi's SA3X
- Microsemi's SA5X
## Requirements

* cmake
* libjson-c-dev
* pps-tools
* [disciplining-minipod](https://github.com/Orolia2s/disciplining-minipod)
* [ubloxcfg](https://github.com/Orolia2s/ubloxcfg)
* libsystemd for tests

## Installation

This program is built using **cmake**:

```
mkdir build
cd build
cmake ..
make
sudo make install
```

- **make install** will install the executable as well as a service to run oscillatord
- for the service to work, one must copy the [oscillatord_default.conf](./example_configurations/oscillatord_default.conf) file and rename it in */etc/oscillatord.conf*

For test purposes, it is easier to compile the executable and run it from a terminal:
in project's root directory
```
make -C build
sudo ./build/src/oscillatord example_configurations/oscillatord_default.conf
```

### Integrated Time Card build

This copy is integrated into the Time Card repository. It retains the complete
upstream v3.10.0 project and adds automatic `ocpN` discovery, direct
`/dev/mro50.N` driver control, safer monitoring defaults, pinned dependency
builds, and Windows Control Center monitoring. Provenance is recorded in
[`UPSTREAM.md`](UPSTREAM.md).

From this directory, `bash tools/build-timecard.sh` fetches pinned revisions of
`ubloxcfg` and `disciplining-minipod`, builds them into an isolated prefix,
builds all utilities and tests, runs CTest, and installs the result under that
prefix. `sudo bash tools/install-timecard.sh` performs a system installation without
overwriting an existing `/etc/oscillatord.conf`.

Use [`oscillatord_timecard.conf`](example_configurations/oscillatord_timecard.conf)
as the starting configuration. It automatically selects
`/sys/class/timecard/ocp0`, binds monitoring to loopback, and rejects every
state-changing monitoring command by default. Remote control requires explicit
opt-in and should use a long token plus a firewall rule.


## Overview

The **oscillatord** daemon, takes input from a PHC clock,
reporting once per second, the phase error between an oscillator and a reference
GNSS receiver.
For an example of such a device, please see the **ptp_ocp** kernel
driver.

The phase error read is then used as an input to the
[disciplining-minipod](https://github.com/Orolia2s/disciplining-minipod) library which will compute a setpoint, used by
**oscillatord** to control an oscillator and discipline it to the 1PPS from a GNSS receiver.
**Oscillatord** also sets PHC'stime at start up, using Output from a GNSS receiver.

To communicate with GNSS receiver's serial it uses [ubloxcfg](https://github.com/Orolia2s/ubloxcfg). This library handles the serial connection to the GNSS receiver and parses Ublox messages.

## Operation

**oscillatord** uses a config file, for which examples are provided in the
**example_configurations/** directory.

So to invoke **oscillatord**, one can, for example do:

    oscillatord /etc/oscillatord.conf

Super user rights may be required to access the devices.

The daemon can be terminated with a **SIGINT** (Ctrl+C) or a **SIGTERM**.

## Oscillators supported

* **mRO50**
* **sa3x/sa5x (monitoring only)**

## Configuration

The configuration file is a succession of lines of the form:

    key=value

Any unsupported key will be ignored and thus can be used as a comment, but using
'#' at the start of a comment line is a good practice.
No blank character must be added before or after the '=' sign, or they will be
considered as part of, respectively, the **key** or the **value**.

### Common configuration keys

#### Oscillatord Modes
* **disciplining**: Wether oscillatord should discipline the oscillator or not
* **monitoring**: Wether oscillatord should expose a socket to send monitoring data
  * **socket-address**: Monitoring's socket address
  * **socket-port**: Monitoring's socket port
* **oscillator**: name of the oscillator to use, accepted: mRO50 only **Required**.

:warning: At least **monitoring** or **disciplining** should be set to **true** for program to work.

#### Devices paths and configuration
* **ptp-clock**: path to the PHC used to get the phase error and set time **Required**.
* **mro50-device**: Path the mro50 device used to control the oscillator **Required**
* **pps-device**: path to the 1PPS phase error device. will trigger write to Chrony SHM. **Optional**.
* **gnss-device-tty**: path to the device tty (e.g /dev/ttyS2) **Required**.
  * **gnss-receiver-reconfigure**: if set to **true**, Oscillatord will check if gnss receiver is configured as specified in the [default configuration file](common/f9_defvalsets.c)
  * **gnss-bypass-survey**: Wether to bypass surveyIn error display if GNSS's Survey in fails

#### Oscillatord runtime var
* **debug**: set debug level.

#### Algorithm parameters
* **oscillator_factory_settings**: Define wether to use factory settings or not for calibration parameters
* **tracking_only**: Set the track only mode

#### Disciplining algorithm-related variables
* **opposite-phase-error**: if **true**, the opposite of the phase error
reported by the 1PPS phase error device, will be fed into **disciplining-minipod**. Any other value means **false**.
* **calibrate_first**: Wether to start calibration at boot
* **phase_resolution_ns**: Phasemeter resolution, depend on the card.
* **ref_fluctuations_ns**: Reference fluctuation of phase error
* **phase_jump_threshold_ns**: Limit upon which a phasejump is requested
* **reactivity_min/max.power**: Reactivity parameters of the algorithm
* **fine_stop_tolerance**: Tolerance authorized for estimated equilibrium in algorithm
* **max_allowed_coarse**: Maximum allowed delta coarse
* **nb_calibration**: Number of phase error measures to get for each control points when doing a calibration

check [default config](./example_configurations/oscillatord_default.conf) for description and default values of parameters

## GNSS SurveyIn

Oscillatord ask for GNSS receiver to perform a SurveyIn so that it may enter Time mode.

During SurveyIn the gnss receiver tries to estimate its position with a certain accuracy (9m default set in our config)

Once it has reached the specified accuracy it will switch to TIME mode which will improve timing performance

Please see [Ublox F9T Interface Description](https://www.u-blox.com/en/docs/UBX-19003606) for further details

## ART Integration tests

ART Integration tests check wether art card handled by ptp_ocp driver works by interacting will all its devices
Integration tests can be run to test the behaviour an ART Card. they are located in *tests/art_integration_testsuite*.
Integration test do the following:
- Analyze filesystem to scan for */sys/class/timecard/\** directories
- Test GNSS Receiver
- Test PTP Hardware clock primary and auxillary functions
- Test mRO50 oscillator
- Check for EEPROM presence
- Start oscillatord service and check that phase error is not upon a threshold during 10 minutes.

## Build tests

```
mkdir build
cmake -D BUILD_TESTS=true ..
make
```

## Utils

### Build tests

```
mkdir build
cmake -D BUILD_UTILS=true ..
make
```

### ART eeprom format

This program write default factory data into ART card's eeprom:
```
art-eeprom-format -p PATH -s SERIAL_NUMBER
```
* **-p PATH**: path of the file/EEPROM data should be written to
* **-s SERIAL_NUMBER**: Serial number that should be written within data. Serial must start with an F followed by 8 numerical characters

### ART Disciplining manager

This program allows user to R/W disciplining parameters from/to a config file such as [art_calibration.conf](utils/art_calibration.conf) to/from EEPROM file of the ART Card.

Reading from disciplining parameters from the EEPROM into a config file allows user to modify the disciplining parameters inside the config file before writing the updated parameters into the EEPROM in order to increase/decrease the number of *ctrl_load_nodes* and *ctrl_drift_coeffs* (which must be *ctrl_nodes_length* at all time)
Resetting temperature table will only reset temperature table and not modify the rest of the EEPROM

```
art_disciplining_manager [-p eeprom_path | -m mro50_path]  [-w calibration.conf -r -f -o output_file_path -t -h]
```
* **-p eeprom_path**: Path to the eeprom file
* **-m mro50_path**: Path to mro50 device
* **-w calibration.conf**: Path to the calibration parameters file to write in the eeprom
* **-r**: Read calibration parameters from the eeprom
* **-f**: force write operation to write factory parameters
* **-o output_file_path**: write calibration parameters read in file
* **-t**: Reset Temperature Table
* **-h**: print help

### ART Temperature table manager

This program allows user to R/W temperature table from/to a text file such as [relative_temp_table.txt](utils/relative_temp_table.txt ) to/from EEPROM file of the ART Card.

```
art_temperature_table_manager -m eeprom_path  [-w input_file | -r -o output_file_path | -h]
```
* **-p eeprom_path**: Path to the eeprom file
* **-w input_file**: Path to the text file containing the temperature table
* **-r**: Read and display temperature table
* **-o output_file_path**: write calibration parameters read in file
* **-h**: print help

### Monitoring Client

art_monitoring_client program offers a simple interface to test and interact with monitoring socket inside oscillatord

Program allows to fetch data sent by the monitoring socket as well as perform the different actions oscillatord can respond to coming from a socket client:

```
art_monitoring_client -a address -p port [-r request]
```
* **-a address**: address of the socket server (set in oscillatord.conf)
* **-p port**: socket port to bind to (set in oscillatord.conf)
* **-r request**: allows to send a request. If empty, program will only output monitoring data. Possible values are:
  * **calibration**: Requests algorithm to perform a calibration of the card
  * **gnss_start**: Sends GNSS_START command to GNSS receiver
  * **gnss_stop**: Sends GNSS_STOP command to GNSS receiver (receiver will not send data over UART and stop itself)
  * **read_eeprom**: Reads content of EEPROM and send it to monitoring client
  * **save_eeprom**: Requests oscillatord to save current disciplining data used by algorithm to the EEPROM

## Source tree organisation

    .
    ├── example_configurations        : per oscillator type configuration examples
    ├── gnss_config                   : GNSS default config file as output by libubloxcfg
    ├── src                           : main oscillatord source code
    │   └── oscillators               : oscillator implementations
    ├── systemd                       : systemd service file
    └── tests                         : code of the oscillator simulator and integration tests
        └── art_integration_testsuite : Integration tests for the ART card
        └── lib_osc_sim_stubs         : code of the lib_osc_sim_stubs library
    └── utils                         : Programs used to configure ART card EEPROM
