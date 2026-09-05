# Native profiles, Windows migration, and app branding

App build 70 adds a transparent menu-bar Time Card mark, a brief native startup
screen, an About view with the same artwork and build information, PPS profile
coverage, and guarded Windows XML migration. Driver 28 / ABI v12 and CLI 24 are
unchanged. These features do not establish full Windows parity or a production
release.

## Menu bar and startup

The 22-point menu-bar image uses a regular transparent artwork asset, not the
application icon. This avoids the square backing macOS applies to Dock icons.
The existing Dock icon is unchanged. Both standard and Retina image assets
retain alpha transparency.

The menu shows the selected card, clock-core synchronization, UTC metadata
availability, last update, and the disabled state of system-clock discipline.
It offers Open Control Center, Refresh Status, About, and Quit. Stale telemetry
does not present a fresh lock claim. Clock-core lock is not labeled trusted UTC.
The menu stays available after the main window closes.

Startup artwork appears for approximately 2.4 seconds with a short fade, or
can be dismissed immediately with Continue or Escape. Reduce Motion suppresses
the fade. Card discovery runs independently; a missing or inaccessible card
never blocks entry. The status reports connection only, not trusted timing.
About reopens the same design without a timeout.

## PPS profile schema 2

Existing schema 1 JSON remains accepted. Profiles containing PPS use schema 2
and allow up to 11 unique settings: four SMA routes, four counters, two PPS
engines, and clock source. Capture reads the hardware configuration twice.

Each `pps` setting stores a channel of 1 (output) or 2 (input), and six values:

| Index | Value |
| --- | --- |
| 0 | Exact PPS core version |
| 1 | Writable field mask from the validated version contract |
| 2 | Enable, 0 or 1 |
| 3 | Active-high polarity, 0 if unsupported |
| 4 | Output width in milliseconds, 0 if not writable |
| 5 | Signed Int32 cable delay represented as UInt32 bits, 0 if unsupported |

Input width is measured telemetry and is never persisted as a setting. Neither
alarm status nor alarm acknowledgement is part of a profile. Reserved register
bits are not synthesized from the file. The live setter uses the driver's
versioned expected-state checks and then verifies the readback.

Apply order is SMA, counters, PPS, clock source. Core-version or writable-mask
mismatches block preview. Every write requires an unchanged baseline; failures
use the existing conservative reverse-order recovery. Recovery files containing
PPS also use schema 2. Unknown or externally changed state is not overwritten.
This is a recoverable sequence, not a hardware-atomic transaction.

## Windows XML import

Supported inputs match the Windows `ControlCenterProduct.cs` definitions:
single `ConfigurationProfile` files or `ConfigurationProfileList` libraries,
schemas 0...2, UTF-8, at most 64 KiB and 32 entries. DTD/entity declarations,
external entities, unknown fields/namespaces/attributes, duplicate fields,
malformed scalars, excessive nesting and oversized fields are rejected.

Import reads a fresh destination baseline and produces a per-entry review.
Compatible clock-source, SMA, and PPS controls can be staged as a native copy
bound to the destination's PCI identity, revision, layout and clock version.
Staging still requires a fresh Preview and confirmed Apply before any write.

Each entry is all-or-blocked, never silently imported as a subset. Unsupported
NMEA/ToD, timecode and generator settings block the entry. Exact FPGA image
identity requirements are also blocked because the macOS driver does not yet
expose the corresponding identity contract. Windows ABI numbers are not treated
as macOS ABI numbers; requirements newer than the supported Windows contract
(15) are rejected. In a library, another fully compatible entry can still be
staged independently.

Windows `HasPulseWidth=false` preserves the destination output width, matching
the source workflow. A measured input pulse width cannot become a writable
setting. Exact PPS version, signed delay range and all native restrictions
are enforced. Nonzero unsupported delay or requested unsupported polarity
blocks migration.

The [representative PPS fixture](../Tests/Fixtures/windows-pps-profile.xml)
is a test example, not a saved user configuration. No user profiles are
overwritten during migration.

## Validation

- Build 70 is installed in `/Applications/TimeCardMacOS.app` on the Intel Mac
  Pro. Its executable SHA-256 is
  `1c8ca77108f8b8ae4984ceb339745329a483a1e7b475bd2400a47dd768886b5f`.
  The signed archive SHA-256 is
  `86bea21b3174e606d391d4b6c2891993e66a3bdbf2eec3167dc9a763dbd4d509`.
  The embedded driver is the same signed driver 28 binary, SHA-256
  `7a514e92e1a5df36a8dbf8d66e9c2a32d4b5aa5dc4e7b0e04446abffb0e287db`.
  App 67 was retained before updating; app-only iterations 68 and 69 were
  also backed up. No reboot or driver replacement was requested.
- Seven CTest and eight Swift suites pass, including schema 1/2 compatibility,
  PPS writable-only capture, signed delay bounds, stale-state rejection,
  reverse-order recovery, XML single/library import and malicious/malformed
  input rejection.
- Universal Intel/Apple Silicon app builds and strict signing checks pass.
  The bundle check verifies the separate artwork asset is compiled.
- A signed, write-prohibiting hardware test on the Intel Mac Pro captured
  seven settings: clock source, four SMA routes and both PPS 1.2 cores.
  JSON round-trip, live preview and unchanged apply passed with zero write
  attempts. Physical PPS setting-change and recovery tests remain pending.
- The native profile UI captured seven settings and previewed zero changes.
  The Windows PPS fixture was imported, staged and previewed as three
  unchanged settings. No hardware Apply was invoked in the GUI.
- The menu was checked with live status, main-window close/reopen, and Quit.
  The transparent artwork is visible without a square backing. The shared
  splash/About design was visually checked on the live Mac Pro, including
  version/build text, connection status and Continue dismissal. A full-screen
  capture of that design in About is included in the screenshot gallery.
- The card remained on source 3, with PPS output enabled/high at 100 ms and
  PPS input enabled/high with 0 ns cable delay. The observed input width was
  80 ms. PHC UTC metadata remained invalid; no clock steering, PHC epoch,
  oscillator, persistent receiver, or driver activation changes were made.

See [remaining Windows parity](WINDOWS-PARITY.md) for the controls still needed.
