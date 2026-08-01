#include "MockTimeCardBackend.h"

#include <QDateTime>

#include <cmath>

MockTimeCardBackend::MockTimeCardBackend()
{
    m_elapsed.start();
}

QString MockTimeCardBackend::backendName() const
{
    return QStringLiteral("Recorded hardware simulation");
}

QString MockTimeCardBackend::selectedDevice() const
{
    return m_selectedDevice;
}

void MockTimeCardBackend::setSelectedDevice(const QString &deviceId)
{
    if (availableDevices().contains(deviceId))
        m_selectedDevice = deviceId;
}

QStringList MockTimeCardBackend::availableDevices() const
{
    return {QStringLiteral("mock0"), QStringLiteral("mock1")};
}

TimeCardSnapshot MockTimeCardBackend::readSnapshot()
{
    constexpr qint64 nanosecondsPerSecond = 1'000'000'000LL;
    constexpr int taiOffsetSeconds = 37;

    const double elapsedSeconds = static_cast<double>(m_elapsed.elapsed()) / 1000.0;
    const qint64 systemUtc = QDateTime::currentMSecsSinceEpoch() * 1'000'000LL;
    const double phase = m_selectedDevice == QStringLiteral("mock1") ? 1.3 : 0.0;
    const qint64 offset = static_cast<qint64>(
        42.0 * std::sin(elapsedSeconds / 8.0 + phase)
        + 8.0 * std::sin(elapsedSeconds / 2.1));

    TimeCardSnapshot snapshot;
    snapshot.connected = true;
    snapshot.backendName = backendName();
    snapshot.availableDevices = availableDevices();
    snapshot.deviceId = m_selectedDevice;
    snapshot.sysfsPath = QStringLiteral("/sys/class/timecard/%1").arg(m_selectedDevice);
    snapshot.ptpDevice = m_selectedDevice == QStringLiteral("mock0")
        ? QStringLiteral("/dev/ptp0") : QStringLiteral("/dev/ptp1");
    snapshot.ppsDevice = QStringLiteral("/dev/pps0");
    snapshot.i2cDevice = QStringLiteral("/dev/i2c-4");
    snapshot.pciAddress = QStringLiteral("0000:03:00.0");
    snapshot.pciVendor = QStringLiteral("0x1d9b");
    snapshot.pciDevice = QStringLiteral("0x0400");
    snapshot.serialNumber = QStringLiteral("02:54:43:00:00:01");
    snapshot.clockSource = QStringLiteral("PPS");
    snapshot.gnssState = QStringLiteral("SYNC");
    snapshot.gnssLocked = true;
    snapshot.todProtocol = QStringLiteral("NMEA");
    snapshot.todBaudRate = QStringLiteral("115200");
    snapshot.ttyGnss = QStringLiteral("/dev/ttyS4");
    snapshot.ttyGnss2 = QStringLiteral("/dev/ttyS5");
    snapshot.ttyMac = QStringLiteral("/dev/ttyS6");
    snapshot.ttyNmea = QStringLiteral("/dev/ttyS7");
    snapshot.cardUtcTaiOffsetValid = true;
    snapshot.cardUtcTaiOffsetSeconds = taiOffsetSeconds;
    snapshot.utcTaiOffsetValid = true;
    snapshot.utcTaiOffsetSeconds = taiOffsetSeconds;
    snapshot.clockDriftValid = true;
    snapshot.clockDriftNanoseconds = static_cast<qint64>(2.0 * std::sin(elapsedSeconds / 5.0));
    snapshot.clockOffsetValid = true;
    snapshot.clockOffsetNanoseconds = offset;
    snapshot.timingValid = true;
    snapshot.systemUtcNanoseconds = systemUtc;
    snapshot.phcTaiNanoseconds = systemUtc + taiOffsetSeconds * nanosecondsPerSecond + offset;
    snapshot.phcUtcNanoseconds = snapshot.phcTaiNanoseconds
        - taiOffsetSeconds * nanosecondsPerSecond;
    snapshot.offsetValid = true;
    snapshot.offsetNanoseconds = offset;
    snapshot.sampleWindowValid = true;
    snapshot.sampleWindowNanoseconds = 760 + static_cast<qint64>(
        120.0 * (1.0 + std::sin(elapsedSeconds / 3.0)));
    snapshot.timestampMethod = QStringLiteral("PTP_SYS_OFFSET_EXTENDED simulation");
    snapshot.capabilities = {
        QStringLiteral("PHC"), QStringLiteral("GNSS"), QStringLiteral("UART"),
        QStringLiteral("SMA"), QStringLiteral("Signal generators"),
        QStringLiteral("Frequency counters"), QStringLiteral("I2C"),
        QStringLiteral("SPI flash")
    };
    return snapshot;
}
