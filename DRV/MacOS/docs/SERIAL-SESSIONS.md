# Native serial sessions, build 74

The UART and NMEA workspace now includes persistent sessions for macOS serial
devices enumerated by IOKit. This closes part of the generic COM-port gap with
the [Windows console](../../Windows/TimeCardControlCenter/MainWindow.xaml.cs).
It does not change the FPGA UART ABI or enable macOS clock discipline.

## Controls

- Explicit port selection and a reviewed Connect action. There is no automatic
  connection, receiver poll, command transmission or reconnection.
- 1,200 through 230,400 baud using the listed standard rates, 5-8 data bits,
  none/even/odd parity, one/two stop-bit settings, and none, RTS/CTS, XON/XOFF or
  combined flow control. Settings are fixed for the duration of a session.
- Continuous nonblocking RX and a direction-filtered RX/TX/status timeline.
  Display pause does not pause acquisition or change export contents.
- UTF-8 text with optional LF, CR or CRLF, or strict hexadecimal byte pairs.
  Hex adds no line ending. Each send is limited to 4 KiB and requires review
  of the exact destination, settings and bytes. Reviews expire after 30 seconds,
  are single-use and cannot cross a disconnect/reconnect boundary.
- JSON evidence, CSV timeline and retained-RX binary exports. Export snapshots
  are frozen when requested; subsequent incoming bytes do not mutate the file.
- Sessions persist across workspace navigation. The menu-bar status menu
  offers Disconnect even when the UART workspace is not visible.

## Safety and limits

Opening a serial port can change DTR/RTS and reset some attached devices.
Connect explains this before opening. Close other serial clients first.
The app requests `TIOCEXCL`, but enforcement is driver-dependent and does not
evict already-open clients. Do not treat it as proof of exclusive hardware
ownership. Apple's [PTY implementation](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/tty_dev.c)
is distinct from a physical USB or PCI serial driver.

The transport rejects symlinks and non-terminal devices, uses nonblocking,
close-on-exec descriptors, and reads back framing, baud and flow-control flags.
Unsupported settings fail instead of silently claiming the requested mode.
The inventory must still contain the selected path when Connect is confirmed.

Disconnect closes the descriptor, discards pending outbound data after sends,
and attempts to restore the original line settings. Cleanup failures are
reported. Sleep, orderly app termination, I/O failure and a 24-hour limit also
stop the session. Termination waits for an in-flight open to clean up. Abrupt
process termination or device removal cannot guarantee settings restoration.

The serial preview and receiver-time qualification workflows cannot start
while a native session holds the app's serial lease, and vice versa.
Other applications are not controlled by this lease.

TX events count bytes accepted by the OS, not bytes physically delivered or
acknowledged by a device. Backpressure is handled with partial-write accounting
and a two-second deadline. Errors stop the session with no automatic retries.
Already-accepted bytes may have affected the device even if a send is cancelled.

History retains at most 500 events and 1 MiB of payload. Total RX/TX counters
continue across eviction; dropped-history counts remain visible and are
included in JSON. The display shows the latest 100 matching events and previews
at most 256 bytes per event. Exports retain complete surviving events.
Binary export includes only retained RX, never TX or status text. CSV stores
payload as hex; JSON additionally preserves status notes and loss counters.
Host timestamps are observation labels, not precision UART arrival timestamps
or qualified GNSS time. Invalid UTF-8 may appear as replacement characters in
the text preview; the original bytes remain intact in hex and exports.

## Validation

- Seven CTest and nine Swift suites pass. The new suite covers all offered
  framing/flow combinations as pure termios construction, strict payload
  parsing, size limits, bounded retention, counters and export encoding.
  Seeded original settings verify that unrequested Darwin DTR/DSR/DCD flow
  control cannot leak into a new session. Freshness tests cover the exact
  30-second confirmation boundary and reject negative elapsed time.
- Virtual serial-port RX/TX tests pass on the Apple Silicon laptop and Intel
  Mac Pro. They cover original-settings restoration, double close, failure
  cleanup, cancelled open, repeated disconnect, unplug, stale and single-use
  send reviews, reconnect isolation and backpressure timeout with exact
  OS-accepted-byte accounting. No physical receiver or oscillator commands
  were sent during testing.
- Build 74 is a universal app and retains the exact signed driver 28 / ABI v12
  and existing host client entitlements. CLI 24 is unchanged.
- The same signed build is installed and running on the Apple Silicon laptop
  and Intel Mac Pro. Strict bundle verification passes on both. The previous
  build 73 bundles were retained as recoverable backups outside Applications.
- The connection confirmation and Cancel path were checked on the laptop
  without opening a serial endpoint. The send editor and timeline fit the
  laptop's dark-mode window; the Mac Pro's light-mode view is captured in the
  [screenshot gallery](screenshots/README.md). Live PHC access remains available,
  but UTC metadata is still invalid and system-clock steering remains disabled.

Deployment SHA-256 evidence:

| Artifact | SHA-256 |
| --- | --- |
| Build 74 host executable on both Macs | `c4fb322a5ad0d6700847ccc4787d657e520226207720a8ae382826da8f4778cd` |
| Deployment ZIP | `8149b256603a73f62ab2f1eaf509779ad10444c43d6ae47dcef53e80d7742bcc` |
| Unchanged active driver 28 executable | `7a514e92e1a5df36a8dbf8d66e9c2a32d4b5aa5dc4e7b0e04446abffb0e287db` |

## Remaining Windows capabilities

Physical USB/PCI serial adapters still need framing, flow-control and unplug
validation. PTYs do not prove physical baud accuracy, modem-line behavior,
parity error reporting or exclusive-open enforcement. Manual DTR/RTS, modem
status, mark/space parity, 1.5 stop bits, live reconfiguration, send-file/macros,
streaming protocol integration and interrupt-backed FPGA UART sessions remain
outside this increment. Raw serial data is not fed into trusted-time evidence.
