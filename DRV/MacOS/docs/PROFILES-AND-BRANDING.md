# Native profiles, Windows migration, and app branding

App build 70 adds a transparent menu-bar Time Card mark, a brief native startup
screen, an About view with the same artwork and build information, PPS profile
coverage, and guarded Windows XML migration. Driver 28 / ABI v12 and CLI 24 are
unchanged. These features do not establish full Windows parity or a production
release.

## Menu bar and startup

Build 73 names the app **Time Card Control Center** in Finder, the Dock,
the menu bar, header and splash. The installed bundle is
`/Applications/Time Card Control Center.app`. The bundle identifier
`org.opentimeserver.timecard.macos`, internal executable `TimeCardMacOS`,
Swift module, Xcode target and scheme remain unchanged to preserve existing
preferences, UI restoration and DriverKit client identity.

Build 72 refines the monochrome menu-bar icon using a solid card silhouette,
clock cutouts, a PCI bracket and rounded trailing bars, following the supplied
design reference. Small details are simplified for menu-bar legibility.
The 26 × 18-point SVG has no lettering, shading, colored pixels or background.
Asset-catalog template rendering and
`NSImage.isTemplate` let macOS supply the same monochrome tint as neighboring
status icons, including light/dark menu bars and selected states. The vector
representation is retained for sharp scaling.

The full-color Dock icon, startup splash and About artwork are unchanged;
build 73 removes the OCP prefix from the displayed app name.

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

- Build 73 is installed and running as
  `/Applications/Time Card Control Center.app` on both the Apple Silicon laptop
  and Intel Mac Pro. Both executable SHA-256 values match
  `a6cca11651815a72fe9c7afa33ba2a293ad9a18b8c8201286a9d12fea95fceb6`.
  The signed archive SHA-256 is
  `317429732921ef57ee7f274028e951f3e6d9609344099a5799d4dcd2eb562290`.
  Xcode refreshed the host development profile to include the laptop while
  retaining previously covered Macs. The same signing identity, bundle ID
  and managed client entitlements are retained. The laptop's old build 64
  preview remains available for recovery; build 72 was backed up on the Mac Pro.
  Seven CTest and eight Swift suites pass. The bundle tests verify the new
  display name, menu-bar name, application filename and stable executable name,
  as well as the monochrome template asset. Strict signature verification
  passes on both Macs. The Mac Pro shows live PHC telemetry through the exact
  unchanged signed driver 28. No driver activation or hardware writes were made.
- Build 72 was previously installed in `/Applications/TimeCardMacOS.app` on the Intel Mac
  Pro. Its executable SHA-256 is
  `57f6c8b8bc0ccc7d33da63fe85f3afb0aef70652f99b19c4182683778b8f358d`;
  the signed archive SHA-256 is
  `75e2f18afbb38c9ee936c23f232ddae0f931e21e0fe9c7adbafcbf5bb7f8d13b`.
  Seven CTest and eight Swift suites pass, along with the app bundle checks
  for template rendering, transparency, vector preservation and 26 × 18-point
  dimensions at standard and Retina scales. Strict signature verification
  passes locally and on the Mac Pro. The live icon and its status menu were
  checked on the desktop. The exact signed driver 28 is unchanged, and build
  71 was backed up before replacement. No hardware settings were changed.
- Build 71 was previously installed in `/Applications/TimeCardMacOS.app` on the Intel Mac
  Pro. Its executable SHA-256 is
  `651b7486fdc5b8ad816b8dad8e2dae6bb6cbb486ccdddc8dd87d10c976a06973`;
  the signed archive SHA-256 is
  `b3014e9eb78410f4d729a6b6f4dc332676b7d068b85aaebcb8179eddd1b7d5fd`.
  Seven CTest and eight Swift suites pass. The app bundle test additionally
  verifies monochrome, transparent, template-rendered, vector-preserving
  standard and Retina renditions. The live icon was checked alongside the
  other macOS status icons. Build 70 was backed up before replacement.
- Build 70 was previously installed on the Intel Mac
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
