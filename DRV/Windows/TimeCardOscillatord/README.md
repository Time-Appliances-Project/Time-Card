# Native Windows oscillatord service

`TimeCardOscillatord.exe` provides oscillator discipline and monitoring on
Windows without WSL, a Linux virtual machine, `/dev/ptp*`, `chronyd`, or a
Linux `oscillatord` process. It is an x64 .NET Framework Windows Service. The
Orolia/Safran miniCOD 3.6.0 algorithm is compiled with `clang-cl` and embedded
inside `TimeCardOscillatord.exe`; no loose miniCOD DLL is loaded.
The runtime target is x64 Windows 10 or 11 so the embedded native loader can
use the restricted modern `LoadLibraryEx` search flags.

The service talks only to the OCP Time Card Windows driver. The same monitoring
protocol is available to Time Card Control Center locally through a protected
named pipe and, when enabled, to existing remote clients through the compatible
TCP JSON endpoint on port 2958.

## Architecture

```text
Windows Service Control Manager
             |
             v
 TimeCardOscillatord.exe
   |         |          |                |
   |         |          |                +-- optional W32Time sample publisher
   |         |          +-- TCP JSON monitor (127.0.0.1:2958 by default)
   |         +------------- protected local named pipe
   +----------------------- Time Card driver ABI 14
                                |
                 +--------------+----------------+
                 |                               |
          PHC / phase meter                 GNSS UART / mRO-50
                 |                               |
                 +------- miniCOD 3.6.0 ----------+
                              |
                     fine/coarse steering

 TimeCardOscillatord.exe -- seqlock sample -- TimeCardTimeProvider.dll
                                                  |
                                           Windows Time Service
```

The coordinator tolerates an absent card at service startup and retries at the
configured interval. On a supported ART card, the native GNSS session
resynchronizes a continuous UBX byte stream, associates UBX-NAV-PVT,
UBX-NAV-TIMELS, and UBX-TIM-TP by iTOW, and maintains UBX-TIM-SVIN and
UBX-MON-RF state. It validates checksums, UTC, leap-second, fix, TIM-TP
flag/reference, survey, component age, and antenna power/status fields and
distinguishes the latest epoch from one coherent enough for PHC initialization.
If MON-RF contains multiple RF blocks, the service retains a valid faulted
antenna state instead of hiding it behind a healthy block.

The discipline engine consumes only a fresh, unconsumed, associated navigation
and time-pulse epoch, including the preceding TIM-TP quantization error. It
pairs that epoch with newly advanced, error-free GNSS and oscillator PPS
counters from the FPGA phase meter; repeated UART messages or stale hardware
edges cannot be reused as new samples. It applies miniCOD-requested PHC phase,
mRO-50 fine, and mRO-50 coarse corrections through bounded driver operations.

Receiver writes are disabled by default. `gnssReceiverReconfigure` must be set
explicitly before the service detects the receiver baud and sends its
acknowledged u-blox CFG-VALSET profile. `gnssPersistConfiguration` is a second,
independent opt-in: false changes RAM only, while true also selects battery-
backed RAM and flash. A UBX-ACK-NAK or missing acknowledgement aborts the
startup attempt rather than assuming that the timing receiver accepted the
profile.

The driver discipline lease makes the active service the sole owner of
discipline-related writes. Closing the service's device handle releases that
lease and disables paired phase capture in the driver, including after a
process failure. Read-only driver telemetry remains available to Control Center.

## Card behavior

All behavior is selected from driver capabilities; PCI IDs or board names are
not used to force an oscillator algorithm onto unknown hardware.

| Card capability | Native service behavior |
|---|---|
| Orolia/Safran ART with direct mRO-50 and paired PPS phase meter | Runs miniCOD discipline, monitors GNSS and oscillator state, and persists learned parameters. |
| MAC-SA5x/SA53 with atomic UART and hardware discipline | Identifies the oscillator protocol, acquires the ABI 14 lease when discipline is enabled, verifies safe phase limits, manages GNSS/PPS holdover and TAU convergence, and enables hardware discipline only after lock, zero-phase, alarm, and reference checks. It does not run the mRO-50 algorithm. |
| Legacy SA3x detected on the atomic UART | Uses the SA3x protocol for cached, read-only oscillator telemetry. It does not issue SA5x control commands to an SA3x device. |
| Celestica or another supported Time Card without those discipline capabilities | Provides capability-safe monitoring and never writes unsupported oscillator registers. |
| No card, removed card, or unsupported device index | Reports `WAITING_FOR_DEVICE`, releases resources, and retries without terminating the service. |

Native miniCOD discipline requires driver ABI 14 or newer. Older drivers may
still expose telemetry, but the service refuses to start software discipline
without the exclusive lease and crash-cleanup contract.

SA5x management is similarly capability- and protocol-gated. Automatic latch
recovery is restricted to firmware positively identified as affected; unknown
revisions and V1.01-or-newer firmware are never auto-latched. The convergence
sequence advances through TAU 50, 500, and 10000 and returns to holdover when
GNSS freshness, PPS presence, or oscillator alarms are unsafe. If neither SA5x
nor SA3x identification succeeds, the card remains monitor-only and no atomic
clock setting is guessed.

Driver ABI 14 publishes a device interface for every connected Time Card. For
multi-card hosts, configure `deviceSerial` with the six-byte MAC identity; the
service probes the present interfaces and fails closed if the serial is absent
or duplicated. `devicePath` can instead pin the exact enumerated interface, and
setting both requires them to identify the same card. `deviceIndex` is used only
when neither stable selector is configured. Its ordering is deterministic for
the current inventory, but an index can move when cards are added or removed.
Index zero retains the legacy `\\.\TimeCard0` fallback for ABI 1-13 drivers.

## Build

Install these Windows build components:

- Visual Studio 2022 or Build Tools with MSBuild and .NET Framework 4.7.2
  targeting tools;
- Visual Studio C++ Clang tools (`clang-cl`, x64);
- the WDK when the driver itself also needs to be rebuilt.

From an x64 Command Prompt or PowerShell:

```powershell
cd DRV\Windows
.\build-oscillatord-service.cmd release
```

The output is in `TimeCardOscillatord\bin\Release` and includes:

- `TimeCardOscillatord.exe` and its `.config` file;
- `TimeCardTimeProvider.dll`, the optional native W32Time input provider;
- `oscillatord.example.json`;
- miniCOD license and third-party notices.

No Linux compiler, WSL distribution, or Linux service is used by this build.
The optional time-provider DLL remains separate because the Windows Time
Service loads providers through a registered native-DLL interface.

## Validate and run interactively

Validate a configuration without opening the driver:

```powershell
.\TimeCardOscillatord\bin\Release\TimeCardOscillatord.exe `
  --validate-config `
  --config .\TimeCardOscillatord\oscillatord.example.json
```

Run the same runtime in the foreground while developing:

```powershell
.\TimeCardOscillatord\bin\Release\TimeCardOscillatord.exe `
  --console `
  --config .\TimeCardOscillatord\oscillatord.example.json
```

Press Ctrl+C for orderly shutdown. The production Windows Service is preferred
for continuous operation because the Service Control Manager supplies delayed
automatic start, recovery actions, shutdown notification, and power-event
handling.

## Install or remove the service

Use an elevated PowerShell session:

```powershell
cd DRV\Windows
.\install-oscillatord-service.ps1
```

The installer builds the release by default, copies binaries under
`%ProgramFiles%\OCP Time Card\Oscillatord`, creates the delayed-auto
`OcpTimeCardOscillatord` service, configures restart recovery, and starts it.
Use `-SkipBuild` to install an already-built output and `-NoStart` to leave the
service stopped.

Register the included Time Card hardware source with Windows Time Service only
when that machine should use the PHC as an operating-system time source:

```powershell
.\install-oscillatord-service.ps1 -EnableWindowsTimeProvider
```

Provider registration is deliberately opt-in because it changes system time
policy. The installer registers `TimeCardTimeProvider.dll` below
`W32Time\TimeProviders\OcpTimeCard`, starts the oscillator service first,
reloads W32Time, and requests source rediscovery. Re-running the installer
without the switch does not silently opt the machine in.

During an in-place upgrade, the installer pauses W32Time before replacing a
loaded provider only when the existing `OcpTimeCard` registration resolves
inside this product's installation directory. A `try`/`finally` path restores
W32Time when it was previously running even if the file deployment fails. An
unrelated or externally located provider registration is never stopped, and a
new registration still requires `-EnableWindowsTimeProvider` explicitly.

Configuration, logs, and per-card calibration data live under:

```text
%ProgramData%\OCP Time Card\Oscillatord\
  oscillatord.json
  Logs\oscillatord.log
  Cards\<card-serial-or-path-hash>\discipline.bin
```

An upgrade preserves the existing configuration and card data. The installer
restricts that directory to LocalSystem and local Administrators.

To remove only the service and program files:

```powershell
.\uninstall-oscillatord-service.ps1
```

Data is preserved by default. Permanent deletion requires the explicit
`-PurgeData` switch.

## Configuration

The installed configuration is
`%ProgramData%\OCP Time Card\Oscillatord\oscillatord.json`. Configuration is
loaded and validated when the process starts; edit it as an administrator and
restart the service to apply changes:

```powershell
Restart-Service OcpTimeCardOscillatord
```

Unknown settings, wrong JSON types, unsupported schema versions, and values
outside their safe ranges are rejected. This prevents spelling errors from
silently changing discipline behavior.

| Setting | Default | Purpose |
|---|---:|---|
| `schemaVersion` | `1` | Configuration schema; only version 1 is accepted. |
| `disciplining` | `true` | Permit native discipline on a capability-compatible card. |
| `monitoring` | `true` | Enable local-pipe and TCP status endpoints. |
| `deviceIndex` | `0` | Fallback index in the deterministically sorted current interface inventory. Used only when `devicePath` and `deviceSerial` are empty; range 0-31. |
| `devicePath` | empty | Exact `\\?\...` device-interface path to select. The configured path must be present; copy it exactly from Windows enumeration. Prefer `deviceSerial` across hardware moves. |
| `deviceSerial` | empty | Preferred stable card selector: the six-byte MAC identity as 12 hexadecimal digits, with optional colon or hyphen separators. Missing, duplicate, or path-mismatched identities are rejected. |
| `deviceRetrySeconds` | `5` | Hot-plug/removal retry interval. Range 1–300 seconds. |
| `socketAddress` | `127.0.0.1` | Numeric TCP bind address. Keep loopback unless remote access is required. |
| `socketPort` | `2958` | Compatible monitoring TCP port. Range 1–65535. |
| `monitoringAllowControl` | `false` | Permit authenticated state-changing TCP requests. |
| `monitoringControlToken` | empty | Shared TCP control token; at least 32 characters when control is enabled. |
| `namedPipe` | `OcpTimeCard.Oscillatord.v1` | Local IPC name. Keep the default for automatic Control Center discovery. |
| `windowsTimePublisher` | `true` | Publish guarded PHC/system cross-timestamp samples for the optional W32Time provider. This does not register the provider. |
| `gnssBaud` | `115200` | Primary u-blox UART baud. Range 1,200–2,000,000. |
| `gnssReceiverReconfigure` | `false` | Explicitly opt in to an acknowledged u-blox CFG-VALSET profile for UART1, one-hertz navigation, NAV-PVT/NAV-TIMELS/TIM-TP/TIM-SVIN/MON-RF, TP1, cable delay, and optional RTCM/raw output. False leaves receiver configuration untouched. |
| `gnssPersistConfiguration` | `false` | With receiver reconfiguration enabled, select RAM-only changes when false or RAM, battery-backed RAM, and flash when true. It has no effect unless `gnssReceiverReconfigure` is true. |
| `gnssPreferredTimeScale` | `GPS` | TP1 time grid: `UTC`, `GPS`, `GLO`, `BDS`, or `GAL`. Native PHC initialization currently requires `GPS` so TIM-TP and NAV-PVT use an unambiguous common iTOW grid. |
| `gnssCableDelayNanoseconds` | `0` | Nonnegative TP1 antenna-cable delay applied by the opt-in receiver profile. Range 0–32,767 ns; zero leaves the receiver's existing cable-delay key unchanged. |
| `gnssRtcmEnabled` | `false` | Start the protected raw-output pipe. When receiver reconfiguration is also enabled, request the upstream RTCM3, RXM-RAWX, and RXM-SFRBX output set; otherwise forward those messages only if the receiver already emits them. |
| `initializePhcFromGnss` | `true` | Run the upstream-equivalent cold-start sequence: validated GNSS UTC → PHC, paired-PPS rough phase correction, settle, GNSS UTC → PHC, then verify. |
| `startupAlignmentTimeoutSeconds` | `300` | Maximum wait for coherent NAV-PVT/NAV-TIMELS/TIM-TP and paired PPS. Range 10–1,800 seconds. |
| `startupAlignmentSettlingSeconds` | `6` | Settle time after the rough PHC phase correction. Range 0–60 seconds; six matches upstream miniCOD. |
| `gnssBypassSurvey` | `false` | Allow discipline without a completed valid TIM-SVIN survey. Use only for a deliberately configured receiver. |
| `oppositePhaseError` | `false` | Reverse the measured phase-error sign for an electrically inverted integration. |
| `calibrateFirst` | `false` | Request miniCOD calibration before ordinary tracking. |
| `debug` | `2` | Native miniCOD diagnostic level, 0–9. |
| `phaseResolutionNs` | `5` | Phase-meter resolution supplied to miniCOD. |
| `refFluctuationsNs` | `30` | Expected GNSS reference fluctuations. |
| `phaseJumpThresholdNs` | `300` | Threshold at which miniCOD may request a PHC phase correction. |
| `reactivityMin`, `reactivityMax`, `reactivityPower` | `10`, `30`, `2` | miniCOD tracking reactivity parameters. |
| `fineStopTolerance` | `100` | Fine-control stop tolerance. Range 0–4,800. |
| `maxAllowedCoarse` | `20` | Maximum coarse-control movement accepted by the algorithm. |
| `nbCalibration` | `50` | Samples per calibration point. Range 1–1,000. |
| `learnTemperatureTable` | `false` | Permit miniCOD to learn a per-card temperature table. |
| `useTemperatureTable` | `false` | Apply a previously learned per-card temperature table. |
| `oscillatorFactorySettings` | `true` | Tell miniCOD that factory oscillator settings are in use. |
| `trackingOnly` | `false` | Disable calibration requests and limit operation to tracking. |
| `parameterSaveMinutes` | `60` | Monotonic periodic persistence interval. Range 1–10,080 minutes. |
| `logMaximumBytes` | `2097152` | Size of one rotating log file. Range 64 KiB–1 GiB. |
| `logRetainedFiles` | `5` | Rotated files retained. Range 1–20. |

`disciplining` and `monitoring` cannot both be false. Start with the supplied
example, change one setting at a time, and run `--validate-config` before
restarting the service.

### GNSS coherence and PHC startup

The cold-start path accepts only a coherent epoch with a valid fix and UTC,
valid current leap offset, a checksum-valid one-hertz GPS TIM-TP reference, and
NAV-PVT/NAV-TIMELS/TIM-TP updates no more than two seconds apart. It excludes
the leap second itself and the five-second window around a pending leap event.
The selected coherent epoch must also be no more than two seconds old.

PHC initialization is edge-bound rather than a free-running system-time copy.
The service waits at most two seconds for exactly the next valid FPGA GNSS PPS
counter edge, refuses the write if more than one edge passed, applies only a
driver-bounded rough phase correction, waits the configured settling interval,
and requires a newer coherent epoch for the final whole-second correction. A
post-write epoch then has to verify both the edge-derived target and GNSS within
250 ms and within one reference-counter interval. Failure or the overall
startup timeout leaves discipline inactive and sends the coordinator through
its safe disconnect/retry path. The service never sets the Windows wall clock
as part of this sequence.

### Import an existing Linux configuration

The importer converts recognized upstream `key=value` settings into strict
Windows JSON and ignores Linux-only device paths:

```powershell
TimeCardOscillatord.exe `
  --import-config C:\staging\oscillatord.conf `
  --config C:\staging\oscillatord.json
```

Review and validate the generated file before placing it in ProgramData.

## Monitoring and control protocol

Each connection carries one UTF-8 JSON request and one JSON response. The
protocol version is 1 and messages are limited to 1 MiB with a five-second read
deadline.

```json
{"request":0}
```

The compatible request numbers are:

| Number | Request | State-changing |
|---:|---|:---:|
| 0 | Status | No |
| 1 | Start calibration | Yes |
| 2 | GNSS start | Yes |
| 3 | GNSS stop | Yes |
| 4 | GNSS software reset | Yes |
| 5 | GNSS hardware reset | Yes |
| 6 | GNSS cold reset | Yes |
| 7 | Read discipline-EEPROM metadata, SHA-256, and Base64 payload | No |
| 8 | Save discipline parameters | Yes |
| 9 | Start simulated holdover | Yes |
| 10 | Stop simulated holdover | Yes |
| 11 | Increment mRO-50 coarse control | Yes |
| 12 | Decrement mRO-50 coarse control | Yes |
| 13 | Reapply the configured u-blox UART baud | Yes |

Request 7 returns presence/validity metadata, byte length, SHA-256, and the
exact binary discipline image in the JSON `data_base64` field when data is
available. Decode that field as Base64; do not treat it as configuration text.
It is a read-only operation, but the payload contains per-card calibration
state and should be handled as diagnostic data.

### Local named pipe

The default endpoint is:

```text
\\.\pipe\OcpTimeCard.Oscillatord.v1
```

Authenticated local users can read status and the EEPROM diagnostic payload.
State-changing requests are accepted only when the connecting Windows identity
is a local Administrator. This is the preferred Control Center transport and
does not need a shared token.

### RTCM and raw-observation pipe

When `gnssRtcmEnabled` is true and the native GNSS session is active, the
service exposes this separate binary endpoint:

```text
\\.\pipe\OcpTimeCard.Rtcm.v1
```

It is a local-only, single-reader byte stream. LocalSystem and Administrators
have full control, authenticated local users have read access, and a client
whose computer identity is remote is rejected. The stream concatenates only
complete, checksum-validated RTCM3 frames (CRC-24Q) and UBX-RXM-RAWX or
UBX-RXM-SFRBX frames (UBX checksum); NMEA, unsupported UBX messages, malformed
lengths, and corrupt frames are discarded while the extractor resynchronizes.
No additional envelope is inserted, so consumers identify frames by their
RTCM3 or UBX sync and length fields.

GNSS UART ingestion never waits for a pipe reader. A drop-oldest queue is
bounded to 256 frames and 1 MiB; when no reader is present or a reader is slow,
old frames are discarded instead of blocking time discipline. This pipe is
disabled by default and is intended for a local NTRIP/RTCM client or raw-
observation processor.

### TCP endpoint

TCP binds to `127.0.0.1:2958` and is read-only by default. To enable control,
set `monitoringAllowControl` to true and configure a secret of at least 32
characters. A control request then includes that token:

```json
{"request":9,"token":"replace-with-a-long-random-secret"}
```

Token comparison is constant-time. TCP is not encrypted; do not expose it to an
untrusted network. Prefer the local pipe, a host firewall, and an authenticated
encrypted tunnel for remote administration.

## Persistence and recovery

miniCOD state is loaded from a valid card EEPROM image when the hardware
advertises that capability. A host copy is maintained per valid unique card
serial. If identity is unavailable, the service uses a SHA-256-derived key from
the exact Windows device-interface path; it never falls back to a shared board
profile. This prevents one card from silently consuming another card's learned
calibration. Host and card writes are tracked independently, so a failed card
write is retried even after the host backup succeeds. Learned temperature-table
output is also separated per card.

Periodic-save freshness, GNSS message freshness, and retry timing use monotonic
timestamps and are unaffected by Windows wall-clock corrections. Suspend,
service stop, card removal, and unexpected worker failure all take the safe
cleanup path: stop writes, disable paired phase capture, release the driver
lease, and close the device.

Logs rotate in ProgramData and warnings/errors are also sent to the Windows
Application Event Log when the registered source is available.

## Windows Time Service integration

The oscillator service disciplines the Time Card PHC and its oscillator. It
does **not** repeatedly step the Windows wall clock. When
`windowsTimePublisher` is true, it instead publishes each bracketed PHC/system
cross-timestamp into a LocalSystem-owned, seqlock-protected shared-memory sample
bridge. The optional x64 `TimeCardTimeProvider.dll` reads that bridge through
the documented W32Time input-provider API.

Before publishing, the service qualifies the source itself. The PHC must report
synchronized state, its UTC year must be 2020–2100, and the bracketed system
sampling window must be valid and no greater than 10 ms. Native ART discipline
must be running and either locked or holdover-ready, with a fresh GNSS solution
or qualified holdover. SA5x requires healthy communications, no active alarms,
an eligible locked/holdover class, and fresh GNSS or qualified holdover; the
read-only SA3x path requires oscillator lock and a valid card GNSS fix.

Sample age is anchored to the monotonic midpoint around the driver's
cross-timestamp IOCTL, not to the later time at which oscillator polling happens
to publish the bridge record. The service rejects a cross timestamp that is
already more than five seconds old before publication, and the provider applies
the same monotonic age bound again when W32Time requests it.

The provider then submits a sample only when all of these checks pass:

- the bridge magic, version, and fixed size match;
- the card reports synchronized state;
- the sample is no more than five seconds old;
- delay and dispersion are valid and the offset is bounded to one day;
- a complete even seqlock generation is read before and after the copy.

An absent, stale, unsynchronized, malformed, or changing sample produces no
W32Time sample. Stopping the oscillator service invalidates the bridge. The
provider also handles `TPC_TimeJumped` by invalidating every old or in-flight
offset association and waiting for a newly captured cross timestamp beyond a
one-second monotonic settling barrier. The provider only supplies measurements
to W32Time; Windows Time Service retains control of operating-system clock
selection and steering policy.

Linux-specific NTP SHM and chrony mechanisms are not used. Control Center also
retains explicit, administrator-gated **Sync from Time Card** and **Sync from
System Clock** operations for deliberate one-time alignment.

Use the installer’s `-EnableWindowsTimeProvider` switch to register the provider
and evaluate the computer’s domain/NTP policy before enabling it on a production
host. The built provider must receive the same appropriate production signing
and deployment review as the rest of the release package.

## Verification without hardware

The service test builds the native components, validates good and bad
configuration files, checks the protocol, GNSS startup, W32Time-provider safety,
and exact u-blox reset/configuration contracts, and runs a
temporary no-device hot-plug/IPC smoke test. The separate RTCM golden test
exercises byte-at-a-time fragmentation, corrupt-frame recovery, CRC/checksum
validation, queue bounds, and a live local named-pipe transfer. Neither test
installs a service, registers a W32Time provider, or accesses a real Time Card:

```powershell
cd DRV\Windows
.\tools\test-oscillatord-service.ps1
.\tools\test-native-rtcm-publisher.ps1
```

Use `-SkipBuild` with an up-to-date Release output, or `-SkipRuntimeSmoke` when
only static, configuration, and protocol tests are appropriate.

## Third-party software

The native wrapper links Orolia/Safran `disciplining-minipod` 3.6.0 under the
GNU Lesser General Public License 2.1 or later. Its complete corresponding
source, license, pinned commit, and local Windows compatibility changes are in
`Software/oscillatord/third_party/disciplining-minipod`. See
`TimeCardDiscipline/THIRD_PARTY_NOTICES.md` in the source tree and the notices
shipped beside the executable.

The imported upstream Linux `oscillatord` tree remains under its upstream GPL
license in `Software/oscillatord`; it is not required to run this Windows
service.
