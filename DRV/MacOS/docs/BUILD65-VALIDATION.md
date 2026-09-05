# Build 65 validation

Validated September 4, 2026 (America/Los_Angeles) on the Intel Mac Pro at
`192.168.1.141`, classic Meta/Facebook PCI `1d9b:0400`, revision 0.
This is a development deployment, not a notarized production release or an
assertion of full Windows parity.

## Delivered

- App 65, driver 27, DriverKit ABI v11, CLI 22.
- Persistent profile library: immutable JSON snapshots, bounded storage,
  corruption isolation, review-only loading, and confirmed removal to Trash.
- BNO08x empty-queue handling and one-millisecond feature-command pacing.
- Actual activation and physical validation of the SA53 and motion workspace
  introduced in build 64, including populated remote GUI checks.

## Build and host tests

Universal x86_64/arm64 app and driver builds pass. `make check` passes six
CMake/CTest suites and five Swift executable suites. New library tests cover
save/reopen, duplicate-name preservation, malformed and oversized files,
entry/directory symlink rejection, stale-removal rejection, and the 200-entry
boundary. Empty SHTP queues are distinguished from malformed packet headers.
Bundle metadata, embedded provisioning profiles, and strict signatures pass.

## Deployment and hardware

Two approved restarts completed the previously pending driver replacement and
then activated the final driver 27. The actual PCI-bound extension reports
ABI v11 and its executable matches the final bundled driver, rather than only
appearing as activated in `systemextensionsctl`.

| Check | Observed result |
| --- | --- |
| BNO08x smoke | 40 polls; 39 with quaternion and linear acceleration; one startup magnetic-only poll |
| Motion validity | No incomplete-packet or reset flags in the final smoke run |
| Motion stop | BNO08x subscriptions disabled, mux restored to 0 |
| Fusion accuracy | Sensor reports 0, displayed as unreliable |
| SA53 queries | Atomic lock; PPS input absent; disciplining not locked; eight optional fields unsupported |
| SA53 temperature/supply | 50.239 C / 4.968 V in the signed final smoke snapshot |
| Clock reference | Configured and active PPS (3), unchanged |
| Clock status | Returned to 1 (in sync) after startup; not evidence of a valid UTC epoch |
| Environmental sensors | Five valid blocks, mux restored |
| GUI hardware self-test | 13 passed; one frequency-counter check gated by the classic FPGA image |

No clock source, oscillator setting, PHC epoch, flash, or macOS time was changed
during validation. IMU report start/stop only changes volatile sensor reporting.
The SA53 refresh uses query commands on the established MAC UART configuration.

## Remote GUI checks

- Screen Sharing connected and displayed the logged-in desktop.
- Sensors and IMU: Start, updating quaternion geometry, roll/pitch/yaw,
  unreliable-calibration indicator, gravity-compensated acceleration, vibration
  history and RMS, CSV export, and Stop were verified.
- Camera orbit changed the view without changing sensor angles; double-click
  reset returned to the initial camera view.
- The exported CSV contains a header and 120 real motion samples, fractional
  host timestamps, sequence, sensor/accuracy, linear axes, and quaternion axes.
  Sample timing reflects actual host throughput, not an asserted uniform 4 Hz.
- Atomic Clock: identification and refresh populated lock, temperature, supply,
  serial/firmware identity, the No PPS input alarm, and unavailable-field states.
  Inactive PPS phase was not plotted as a valid zero.
- Profiles: captured the five-setting PPS/SMA baseline, saved it as
  `mac pro pps baseline`, reloaded it, selected it, staged it, and previewed
  zero changes / five unchanged. Apply and Trash confirmation dialogs were
  opened and cancelled. The saved baseline was retained.
- The read-only self-test report was saved and independently read over SSH.

Local evidence under `DRV/MacOS/.build/` includes
`timecard-motion65-final.json`, `timecard-sa53-65-final.json`,
`timecard-motion-build65-verification.csv`, and `timecard-build65-self-test.txt`.
The baseline library is retained in the test user's Application Support folder.

## Signed artifact identity

| Artifact | SHA-256 |
| --- | --- |
| App 65 package | `209d9aa611402aba3fa02b1c61e1599bd96e0c753e616801ddbdaf1e015cf532` |
| App executable | `a6225be64e5d0c04b96b5e80bcf03d6db611e35f46b49db03e0534261323af4c` |
| Bundled and PCI-bound driver executable | `4f920fdfe81ff024063e74d4cedcb3a9c243d13a9f82c73dde98896dec32331f` |

The local build 64 final archive is retained as a recovery artifact. Remote
deployment directories under `/tmp` must not be relied on across restarts.

## Remaining validation

BNO055, Celestica/ART/ADVA variants, sleep/wake, multi-card use, long-duration
recordings, physical clock-changing and rollback scenarios, high-rate vibration,
and notarized release installation remain unvalidated. Low-rate IMU trends
are not calibrated vibration analysis. The absent SA53 PPS signal and low
fusion accuracy remain visible hardware conditions, not hidden by the UI.
The remaining implementation gaps are tracked in [WINDOWS-PARITY.md](WINDOWS-PARITY.md).
