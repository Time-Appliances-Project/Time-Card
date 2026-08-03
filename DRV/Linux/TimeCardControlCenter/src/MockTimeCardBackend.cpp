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
    const bool secondary = m_selectedDevice == QStringLiteral("mock1");
    const double phase = secondary ? 1.3 : 0.0;
    const qint64 offset = static_cast<qint64>(
        42.0 * std::sin(elapsedSeconds / 8.0 + phase)
        + 8.0 * std::sin(elapsedSeconds / 2.1));

    TimeCardSnapshot snapshot;
    snapshot.connected = true;
    snapshot.backendName = backendName();
    snapshot.availableDevices = availableDevices();
    snapshot.deviceId = m_selectedDevice;
    snapshot.sysfsPath = QStringLiteral("/sys/class/timecard/%1").arg(m_selectedDevice);
    snapshot.ptpDevice = !secondary
        ? QStringLiteral("/dev/ptp0") : QStringLiteral("/dev/ptp1");
    snapshot.ppsDevice = secondary ? QStringLiteral("/dev/pps1")
                                   : QStringLiteral("/dev/pps0");
    snapshot.i2cDevice = secondary ? QStringLiteral("/dev/i2c-8")
                                   : QStringLiteral("/dev/i2c-4");
    snapshot.pciAddress = secondary ? QStringLiteral("0000:04:00.0")
                                    : QStringLiteral("0000:03:00.0");
    snapshot.pciVendor = QStringLiteral("0x1d9b");
    snapshot.pciDevice = QStringLiteral("0x0400");
    snapshot.serialNumber = secondary ? QStringLiteral("02:54:43:00:00:02")
                                      : QStringLiteral("02:54:43:00:00:01");
    snapshot.boardProfile = QStringLiteral("R4006-compatible peripheral profile");
    snapshot.clockSource = QStringLiteral("PPS");
    snapshot.gnssState = QStringLiteral("SYNC");
    snapshot.gnssLocked = true;
    snapshot.todProtocol = QStringLiteral("NMEA");
    snapshot.todBaudRate = QStringLiteral("115200");
    snapshot.ttyGnss = secondary ? QStringLiteral("/dev/ttyS8")
                                 : QStringLiteral("/dev/ttyS4");
    snapshot.ttyGnss2 = secondary ? QStringLiteral("/dev/ttyS9")
                                  : QStringLiteral("/dev/ttyS5");
    snapshot.ttyMac = secondary ? QStringLiteral("/dev/ttyS10")
                                : QStringLiteral("/dev/ttyS6");
    snapshot.ttyNmea = secondary ? QStringLiteral("/dev/ttyS11")
                                 : QStringLiteral("/dev/ttyS7");
    snapshot.cardUtcTaiOffsetValid = true;
    snapshot.cardUtcTaiOffsetSeconds = taiOffsetSeconds;
    snapshot.utcTaiOffsetValid = true;
    snapshot.utcTaiOffsetSeconds = taiOffsetSeconds;
    snapshot.clockDriftPpbValid = true;
    snapshot.clockDriftPartsPerBillion =
        static_cast<qint64>(2.0 * std::sin(elapsedSeconds / 5.0));
    snapshot.r4006TopologyDetected = true;
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
        QStringLiteral("SPI flash"), QStringLiteral("FPGA engine status"),
        QStringLiteral("Sensors"), QStringLiteral("Status LEDs")
    };
    snapshot.smaStates = {
        QStringLiteral("SMA1 | OUT: PHC PPS"),
        QStringLiteral("SMA2 | IN: PPS1"),
        QStringLiteral("SMA3 | OUT: GNSS1"),
        QStringLiteral("SMA4 | disabled"),
    };
    snapshot.generatorStates = {
        QStringLiteral("GEN1 | running | period 1000000000 ns | duty 50% | active high | continuous"),
        QStringLiteral("GEN2 | stopped | period 10000000 ns | duty 25% | active high | repeat 100"),
    };
    snapshot.frequencyCounterStates = {
        QStringLiteral("FREQ1 | 10000000 Hz | gate 1 s"),
        QStringLiteral("FREQ2 | waiting for sample | gate 1 s"),
    };
    snapshot.fpgaEngineStates = {
        QStringLiteral("PPS | external polarity 1 | pulse 100 | external cable 0"),
        QStringLiteral("NMEA output | enabled 1 | baud 115200 | GNSS COMBINED"),
        QStringLiteral("ToD parser | protocol NMEA | GNSS COMBINED | baud 115200"),
        QStringLiteral("IRIG/DCF | output mode B | input mode B | output error 0"),
    };
    snapshot.sensorStates = {
        QStringLiteral("LM75 1 | 41.25 C"),
        QStringLiteral("LM75 2 | 42.00 C"),
        QStringLiteral("LM75 3 | 40.88 C"),
        QStringLiteral("SHT3X 1 | 39.75 C"),
        QStringLiteral("SHT3x humidity | 44.20%"),
        QStringLiteral("ICP-10100 pressure | 101.325 kPa"),
    };
    snapshot.ledStates = {
        QStringLiteral("GNSS1 | brightness 128/255 | RGB 0 255 0"),
        QStringLiteral("SMA1 | brightness 96/255 | RGB 0 255 0"),
        QStringLiteral("SMA2 | brightness 96/255 | RGB 0 0 255"),
    };
    snapshot.optionalImageContract = QStringLiteral(
        "pci=%1 actual=0x12345678 expected=0x12345678 targeted=1 match=1 loader=0")
                                         .arg(snapshot.pciAddress);
    return snapshot;
}
