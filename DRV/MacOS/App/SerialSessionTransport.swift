/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
import Darwin

// All descriptor access, including close, is under one lock. Each nonblocking
// operation is bounded. A late reader/writer can never use a recycled fd.
final class SerialSessionTransport: @unchecked Sendable {
    private let lock = NSLock()
    private var descriptor: Int32 = -1
    private var original = termios()
    private var hasOriginal = false
    private var exclusive = false
    private var wroteBytes = false
    deinit { _ = close() }

    init(path: String, settings: SerialLineSettings) throws {
        guard path.hasPrefix("/dev/"), !path.contains(".."), !path.utf8.contains(0) else {
            throw SerialSessionError.invalid("Select a native serial device.")
        }
        _ = try settings.configured(from: termios())
        descriptor = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW)
        guard descriptor >= 0 else { throw SerialSessionError.system("Open \(path)", errno) }
        do {
            var info = stat()
            guard fstat(descriptor, &info) == 0, info.st_mode & mode_t(S_IFMT) == mode_t(S_IFCHR), isatty(descriptor) == 1 else {
                throw SerialSessionError.invalid("The selected device is not a serial terminal.")
            }
            guard ioctl(descriptor, TIOCEXCL) == 0 else { throw SerialSessionError.system("Request exclusive serial access", errno) }
            exclusive = true
            guard tcgetattr(descriptor, &original) == 0 else { throw SerialSessionError.system("Read original serial settings", errno) }
            hasOriginal = true
            var requested = try settings.configured(from: original)
            guard tcsetattr(descriptor, TCSANOW, &requested) == 0 else { throw SerialSessionError.system("Apply serial settings", errno) }
            var actual = termios()
            guard tcgetattr(descriptor, &actual) == 0 else { throw SerialSessionError.system("Verify serial settings", errno) }
            guard actual.c_cflag & SerialLineSettings.controlMask == requested.c_cflag & SerialLineSettings.controlMask,
                  actual.c_iflag & SerialLineSettings.inputMask == requested.c_iflag & SerialLineSettings.inputMask,
                  cfgetispeed(&actual) == cfgetispeed(&requested), cfgetospeed(&actual) == cfgetospeed(&requested) else {
                throw SerialSessionError.invalid("The serial driver did not accept these line settings. Original settings were restored where possible.")
            }
            // Do not flush RX: bytes already waiting belong in the session.
        } catch { _ = close(); throw error }
    }

    func receive() throws -> [UInt8] {
        lock.lock(); defer { lock.unlock() }
        guard descriptor >= 0 else { throw SerialSessionError.invalid("Serial session is closed.") }
        var event = pollfd(fd: descriptor, events: Int16(POLLIN), revents: 0)
        let ready = Darwin.poll(&event, 1, 0)
        if ready < 0 { if errno == EINTR { return [] }; throw SerialSessionError.system("Poll serial port", errno) }
        if event.revents & Int16(POLLHUP | POLLERR | POLLNVAL) != 0 {
            throw SerialSessionError.invalid("Serial device disconnected or reported an I/O error. Reconnect explicitly after checking the device.")
        }
        guard ready > 0, event.revents & Int16(POLLIN) != 0 else { return [] }
        var bytes = [UInt8](repeating: 0, count: 16_384)
        let count = bytes.withUnsafeMutableBytes { Darwin.read(descriptor, $0.baseAddress, $0.count) }
        if count < 0 {
            if [EAGAIN, EWOULDBLOCK, EINTR].contains(errno) { return [] }
            throw SerialSessionError.system("Read serial port", errno)
        }
        return Array(bytes.prefix(count))
    }

    func transmit(_ bytes: ArraySlice<UInt8>) throws -> Int {
        lock.lock(); defer { lock.unlock() }
        guard descriptor >= 0 else { throw SerialSessionError.invalid("Serial session is closed.") }
        guard !bytes.isEmpty, bytes.count <= SerialPayload.maximumBytes else { throw SerialSessionError.invalid("Invalid transmit chunk.") }
        let count = bytes.withUnsafeBytes { Darwin.write(descriptor, $0.baseAddress, $0.count) }
        if count < 0 {
            if [EAGAIN, EWOULDBLOCK, EINTR].contains(errno) { return 0 }
            throw SerialSessionError.system("Write serial port", errno)
        }
        wroteBytes = wroteBytes || count > 0
        return count
    }

    @discardableResult func close() -> String? {
        lock.lock(); defer { lock.unlock() }
        guard descriptor >= 0 else { return nil }
        var failures: [String] = []
        if wroteBytes && tcflush(descriptor, TCOFLUSH) != 0 { failures.append("could not discard queued output") }
        if hasOriginal && tcsetattr(descriptor, TCSANOW, &original) != 0 { failures.append("could not restore line settings") }
        if exclusive && ioctl(descriptor, TIOCNXCL) != 0 { failures.append("could not clear exclusive access") }
        Darwin.close(descriptor); descriptor = -1
        return failures.isEmpty ? nil : failures.joined(separator: "; ")
    }
}
