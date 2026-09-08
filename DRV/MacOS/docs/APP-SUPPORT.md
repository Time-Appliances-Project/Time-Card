# Time Card Control Center support

Time Card Control Center is a free, native macOS app for Time Card hardware
diagnostics and configuration. Brought to you by Ahmad Byagowi.

## Contact

Email [ahmadexp@gmail.com](mailto:ahmadexp@gmail.com) for help with the macOS app
or questions about privacy. Use a subject such as **Time Card Control Center
support** and describe the issue and what you expected to happen.

Useful details include:

- App version and build from About Time Card Control Center.
- macOS version and Mac model.
- Time Card variant, FPGA firmware version and attached peripheral model.
- Steps to reproduce the issue and the exact error message.

Do not send passwords, remote-access credentials or unrelated personal data.
Before sharing screenshots or exported support bundles, inspect them for GNSS
coordinates, hardware serial numbers and private serial messages. A support
bundle is saved locally and is not automatically sent to anyone. Redact details
you do not want to share. Avoid posting private diagnostics in public issues.

## Requirements and troubleshooting

The app requires macOS 14 or later. Hardware functions need a compatible Time
Card, an appropriate PCIe connection and approval of the Time Card system
extension. Available controls depend on the card, firmware and peripherals.
Offline record review does not require a card.

If no card appears, check the physical connection, driver status in the app,
and any approval request in macOS System Settings. Do not disable system
security protections as a troubleshooting shortcut. On a managed Mac, ask your
administrator whether system extensions are restricted.

If a GNSS or atomic-clock panel has no fresh readings, check the attached
receiver/clock, serial settings and whether another session is using the same
port. Imported records and replays are not evidence of live hardware status.

Automatic LED setup and IMU sampling can make volatile hardware changes.
Disable these preferences before using the app on an installation that must
remain unchanged. Review configuration changes before applying them.

The current app does not discipline the macOS system clock, provide a PTP
network service or perform FPGA firmware updates. Hardware time readings alone
do not certify valid UTC/TAI or traceability.

## Documentation and release status

- [macOS source and documentation](../README.md)
- [Privacy policy](APP-PRIVACY-POLICY.md)

The Mac App Store release is being prepared. Publication of this support page
does not mean that the app or its driver has passed Apple review.
