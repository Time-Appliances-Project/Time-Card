# Atomic clock and motion, build 64

## SA53

The native Atomic Clock page uses MAC UART 2 at 57,600 baud on Meta/Celestica
profiles. ART is not probed with this protocol. C3 messages have XOR checksums
and two-digit hexadecimal sequence matching. Parsing handles fragmented
responses and quoted brackets, rejects corrupt replies, and bounds transfers
and refresh time. App UART captures and oscillator operations share a recursive
session lock; other processes must be closed before making changes.

The page includes all 15 writable parameters from the Windows SA53 workspace,
34 telemetry queries, five identity queries, alarm names with unknown-bit
preservation, lock indicators, and valid PPS phase history. JamSync, alarm
acknowledge, load and flash store each require explicit review. Setters validate
bounds and mode dependencies, recheck oscillator identity and current state,
and verify persistent readback. TimeOfDay and PpsQErr have explicit transient
semantics. Flash store and clock-changing operations were not exercised on the
physical oscillator in this validation run.

Read-only testing on the Mac Pro identified `sa5x`, hardware A, firmware
`V1.0.23.0.5DAA2CC2,V1.16`. Atomic lock was true, lock progress 100%,
temperature 50.176 C, and supply 4.932 V. Disciplining was enabled but not
locked, PPS input was absent, and alarm 131072 decoded to **No PPS input**.
The inactive raw Phase value of zero is not plotted as a valid phase sample.
Unsupported queries on this firmware were `pn?`, `TotalRuntime`, `TotalLocktime`,
`LaserTempSet`, `OscTuning`, `OvenCurrent`, `DCSignal`, and `DisciplineTuning`.
The app preserves the remaining telemetry rather than rejecting the snapshot.

## Motion ABI

ABI v11 appends selector 22, a 16-byte request and 144-byte response. Existing
selectors and structure sizes are unchanged. Modes are poll, start/resubscribe,
and stop. Capability bit 11 is restricted to the Meta/Celestica AXI IIC contract.
Sensor routes are BNO08x mux 0x08/address 0x4a or 0x4b, and BNO055 mux 0x02/
address 0x28 or 0x29. The driver holds its register lock and restores and
verifies the prior mux selection. Restore failure rejects the response.

BNO08x handling assembles bounded SHTP I2C cargos, verifies continuation
headers and sequence progression, discards incomplete frames, decodes SH-2
reports transactionally, and tracks reset/resubscription state. Requested
rates are 4 Hz for vectors/quaternion and 1 Hz for temperature. BNO055 reads
NDOF fusion only with valid system status and converts its configured units.
The output includes quaternion, raw and linear acceleration, gravity, gyro,
magnetic field, temperature, per-component validity, and calibration quality.

Swift validates response layout and quaternion norm. Orientation expires after
two seconds. Vibration requires a real linear-acceleration report, not raw
acceleration minus an assumed gravity vector. History is capped at 300 host
samples; charts and RMS use the last 60 seconds and do not bridge gaps over
two seconds. This low-rate trend cannot measure high-frequency vibration.
The 3D board is a sensor-frame schematic with an independent orbit camera,
not an assertion about sensor-to-chassis mounting orientation.

## Verification and deployment

`make check` passes six CMake/CTest suites and four Swift executable suites.
New tests cover SHTP fragmentation, sequence wrap, corruption, transactional
report decoding, BNO055 units, C3 framing/checksums/sequence/quoting, parameter
bounds, unknown data, quaternion normalization, RMS and CSV behavior.
Universal Intel/Apple Silicon app and driver builds and bundle checks pass.
Local interface checks confirm the dedicated Atomic Clock page and motion
workspace, unavailable-state messaging, and disabled controls without hardware.

Signed app 64 and CLI 22 are installed on `.141`. The upgrade request for driver
26 succeeded, but driver 25 remained bound to the PCI card, reporting ABI v10
and `terminating for upgrade via delegate`. A reboot requires fresh approval.
Live BNO08x/BNO055 motion, populated remote UI, sleep/wake, and other board
variants therefore remain unverified. No reboot or clock-changing test was
performed. Screen Sharing and remote screen capture currently return no usable
desktop image. Existing PPS source and clock-sync status remained unchanged.
Previous app 63 and CLI 21 are recoverable in `/tmp/timecard-build64.fKPG8c`.

The final app and driver executable hashes match the local signed build, and
both provisioning profiles are present. Final package SHA-256:
`2b30d3e53fae1638940fe70f3207787f8d528cda7127c9f18b3fbfff791c03fe`.
An additional activation request for the final driver refinements was rejected
while the initial upgrade was pending: sysextd reported two entries, one
terminating and one activated. Do not edit `/Library/SystemExtensions` or kill
the driver to work around this. After the approved restart, activate the final
bundled driver if its executable differs from the bound extension, and follow
any further system-requested restart before claiming validation of that binary.
Final bundled driver executable SHA-256:
`2b2f86022f11571c4fb99a3342ca11df27938c8772af70878a49c128cbdb0c15`.

After the approved restart, check `timecardctl status` for ABI 11, then run the
signed `Tests/PeripheralHardwareSmoke.swift` helper with `--motion` or use the
Sensors and IMU Start/Stop controls. Verify fresh quaternion and linear values,
valid mux restoration, environmental sensors, and unchanged PPS source.

## Primary protocol references

- [Microchip MAC-SA5x user guide, DS50002938](https://ww1.microchip.com/downloads/aemDocuments/documents/FTD/ProductDocuments/UserGuides/Miniature-Atomic-Clock-MAC-SA5X-Users-Guide-DS50002938.pdf)
- [CEVA SH-2 reference manual](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf)
- [CEVA Sensor Hub Transport Protocol](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf)
- [Bosch BNO055 data sheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bno055-ds000.pdf)
