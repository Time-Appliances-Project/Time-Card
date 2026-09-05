# Atomic clock and motion, build 65

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
Driver 27 treats an all-zero SHTP header as an empty queue, not an incomplete
packet. Seven feature commands are paced one millisecond apart, matching the
Windows implementation. Other malformed headers remain rejected.

Swift validates response layout and quaternion norm. Orientation expires after
two seconds. Vibration requires a real linear-acceleration report, not raw
acceleration minus an assumed gravity vector. History is capped at 300 host
samples; charts and RMS use the last 60 seconds and do not bridge gaps over
two seconds. This low-rate trend cannot measure high-frequency vibration.
The 3D board is a sensor-frame schematic with an independent orbit camera,
not an assertion about sensor-to-chassis mounting orientation.

## Verification and deployment

`make check` passes six CMake/CTest suites and five Swift executable suites.
New tests cover SHTP fragmentation, sequence wrap, corruption, transactional
report decoding, BNO055 units, C3 framing/checksums/sequence/quoting, parameter
bounds, unknown data, quaternion normalization, RMS and CSV behavior.
Universal Intel/Apple Silicon app and driver builds and bundle checks pass.
Local interface checks confirm the dedicated Atomic Clock page and motion
workspace, unavailable-state messaging, and disabled controls without hardware.

Signed app 65, driver 27 and CLI 22 are deployed on the Intel Mac Pro `.141`.
The approved restarts completed the formerly pending driver replacement.
The actual PCI-bound driver reports ABI v11 and its executable hash matches
the final bundled driver. Do not infer this from activation-list status alone.

The signed `Tests/PeripheralHardwareSmoke.swift --motion` run collected 40
polls from the fitted BNO08x at mux 0x08/address 0x4a. Thirty-nine included valid
quaternion and linear acceleration; the initial poll contained magnetic data
only. No empty-queue incomplete errors or reset flags occurred. Stop disabled
the subscriptions, and every returned mux selection was restored to zero.
Five environmental blocks remained valid and the configured/active source
remained PPS (3). Clock status returned to in-sync after startup. This does not
establish PHC UTC validity.

Screen Sharing works after login. Remote GUI checks verified live quaternion
geometry, vibration history/RMS, populated CSV export, and Stop returning the
3D model to its unavailable state. The sensor's fusion accuracy reports zero
(unreliable); this is shown explicitly, not treated as calibrated orientation.
Final SA53 read-only telemetry returned atomic lock, no PPS/disciplining lock,
50.239 C and 4.968 V, with the same eight unsupported optional fields.
No oscillator settings, clock source, flash, or PHC epoch were changed.

BNO055, other board variants, sleep/wake, and high-rate vibration remain
unvalidated. See [BUILD65-VALIDATION.md](BUILD65-VALIDATION.md) for hashes and
the GUI and profile-library test record. The prior build 64 archive is retained
locally; deployment backups in `/tmp` must not be relied on across reboots.

## Primary protocol references

- [Microchip MAC-SA5x user guide, DS50002938](https://ww1.microchip.com/downloads/aemDocuments/documents/FTD/ProductDocuments/UserGuides/Miniature-Atomic-Clock-MAC-SA5X-Users-Guide-DS50002938.pdf)
- [CEVA SH-2 reference manual](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf)
- [CEVA Sensor Hub Transport Protocol](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf)
- [Bosch BNO055 data sheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bno055-ds000.pdf)
