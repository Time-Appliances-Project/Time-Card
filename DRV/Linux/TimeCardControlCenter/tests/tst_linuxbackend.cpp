#include "AppController.h"
#include "LinuxTimeCardBackend.h"
#include "MockTimeCardBackend.h"
#include "OscillatordClient.h"
#include "SessionLog.h"
#include "TimingMath.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

namespace {
void writeFixture(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

class SlowSwitchingBackend final : public TimeCardBackend {
public:
    QString backendName() const override { return QStringLiteral("Slow test backend"); }
    QString selectedDevice() const override { return m_selectedDevice; }
    void setSelectedDevice(const QString &deviceId) override
    {
        if (availableDevices().contains(deviceId))
            m_selectedDevice = deviceId;
    }
    QStringList availableDevices() const override
    {
        return {QStringLiteral("cardA"), QStringLiteral("cardB")};
    }
    TimeCardSnapshot readSnapshot() override
    {
        QThread::msleep(150);
        TimeCardSnapshot snapshot;
        snapshot.connected = true;
        snapshot.backendName = backendName();
        snapshot.availableDevices = availableDevices();
        snapshot.deviceId = m_selectedDevice;
        snapshot.serialNumber = QStringLiteral("serial-") + m_selectedDevice;
        return snapshot;
    }

private:
    QString m_selectedDevice = QStringLiteral("cardA");
};
}

class LinuxBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void discoversOnlyNumberedCards();
    void readsStableSysfsAttributes();
    void readsTimingIoAndFpgaEngineAttributes();
    void reportsFpgaFaultAndContractMismatch();
    void readsR4006StandardSubsystemTelemetry();
    void scopesLedsToSelectedPciFunction();
    void detectsGnssCapabilityFromUart();
    void cachesStaticIdentityBetweenSamples();
    void rejectsUnsafeDeviceNode();
    void discardsInFlightSnapshotAfterSelection();
    void prefersKernelTaiOffset();
    void convertsTaiBeforeComparingClocks();
    void producesCoherentMockTelemetry();
    void parsesChunkedOscillatordStatus();
    void exposesOscillatordParitySemantics();
    void acceptsOscillatordStatusWithoutDisciplining();
    void rejectsUnsupportedOscillatordProtocol();
    void boundsAndExportsStructuredSessionLog();
};

void LinuxBackendTest::discoversOnlyNumberedCards()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QDir root(fixture.path());
    QVERIFY(root.mkdir(QStringLiteral("ocp0")));
    QVERIFY(root.mkdir(QStringLiteral("ocp12")));
    QVERIFY(root.mkdir(QStringLiteral("ocp-debug")));
    QVERIFY(root.mkdir(QStringLiteral("other0")));

    LinuxTimeCardBackend backend(fixture.path());
    QCOMPARE(backend.availableDevices(),
        QStringList({QStringLiteral("ocp0"), QStringLiteral("ocp12")}));
}

void LinuxBackendTest::readsStableSysfsAttributes()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QDir root(fixture.path());
    QVERIFY(root.mkpath(QStringLiteral("ocp3/tty")));
    const QString card = root.filePath(QStringLiteral("ocp3"));

    writeFixture(card + QStringLiteral("/ptp"), QByteArrayLiteral("ptp7\n"));
    writeFixture(card + QStringLiteral("/serialnum"), QByteArrayLiteral("02:54:43:00:00:07\n"));
    writeFixture(card + QStringLiteral("/clock_source"), QByteArrayLiteral("PPS\n"));
    writeFixture(card + QStringLiteral("/gnss_sync"), QByteArrayLiteral("SYNC\n"));
    writeFixture(card + QStringLiteral("/utc_tai_offset"), QByteArrayLiteral("37\n"));
    writeFixture(card + QStringLiteral("/clock_status_offset"), QByteArrayLiteral("-14\n"));
    writeFixture(card + QStringLiteral("/clock_status_drift"), QByteArrayLiteral("2\n"));
    writeFixture(card + QStringLiteral("/tod_protocol"), QByteArrayLiteral("NMEA\n"));
    writeFixture(card + QStringLiteral("/tod_baud_rate"), QByteArrayLiteral("115200\n"));
    writeFixture(card + QStringLiteral("/tty/ttyGNSS"), QByteArrayLiteral("ttyS4"));
    writeFixture(card + QStringLiteral("/tty/ttyMAC"), QByteArrayLiteral("ttyS6"));
    QVERIFY(root.mkdir(QStringLiteral("ocp3/gen1")));
    writeFixture(card + QStringLiteral("/gen1/running"), QByteArrayLiteral("0\n"));

    LinuxTimeCardBackend backend(fixture.path());
    backend.setSelectedDevice(QStringLiteral("ocp3"));
    const TimeCardSnapshot snapshot = backend.readSnapshot();

    QVERIFY(snapshot.connected);
    QCOMPARE(snapshot.deviceId, QStringLiteral("ocp3"));
    QCOMPARE(snapshot.ptpDevice, QStringLiteral("/dev/ptp7"));
    QCOMPARE(snapshot.serialNumber, QStringLiteral("02:54:43:00:00:07"));
    QCOMPARE(snapshot.clockSource, QStringLiteral("PPS"));
    QVERIFY(snapshot.gnssLocked);
    QCOMPARE(snapshot.utcTaiOffsetSeconds, 37);
    QCOMPARE(snapshot.clockOffsetNanoseconds, -14);
    QVERIFY(snapshot.clockDriftPpbValid);
    QCOMPARE(snapshot.clockDriftPartsPerBillion, 2);
    QCOMPARE(snapshot.ttyGnss, QStringLiteral("/dev/ttyS4"));
    QCOMPARE(snapshot.ttyMac, QStringLiteral("/dev/ttyS6"));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("PHC")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("Signal generators")));
}

void LinuxBackendTest::readsTimingIoAndFpgaEngineAttributes()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QDir root(fixture.path());
    QVERIFY(root.mkpath(QStringLiteral("ocp8/gen1")));
    QVERIFY(root.mkpath(QStringLiteral("ocp8/freq1")));
    QVERIFY(root.mkpath(QStringLiteral("ocp8/freq2")));
    QVERIFY(root.mkpath(QStringLiteral("ocp8/freq3")));
    QVERIFY(root.mkpath(QStringLiteral("ocp8/freq4")));
    const QString card = root.filePath(QStringLiteral("ocp8"));

    writeFixture(card + QStringLiteral("/sma1"), QByteArrayLiteral("IN: PPS1\n"));
    writeFixture(card + QStringLiteral("/gen1/running"), QByteArrayLiteral("1\n"));
    writeFixture(card + QStringLiteral("/gen1/period"), QByteArrayLiteral("1000000000\n"));
    writeFixture(card + QStringLiteral("/gen1/duty"), QByteArrayLiteral("50\n"));
    writeFixture(card + QStringLiteral("/gen1/phase"), QByteArrayLiteral("125\n"));
    writeFixture(card + QStringLiteral("/gen1/polarity"), QByteArrayLiteral("1\n"));
    writeFixture(card + QStringLiteral("/gen1/repeat_count"), QByteArrayLiteral("0\n"));
    writeFixture(card + QStringLiteral("/gen1/cable_delay"), QByteArrayLiteral("25\n"));
    writeFixture(card + QStringLiteral("/freq1/seconds"), QByteArrayLiteral("1\n"));
    writeFixture(card + QStringLiteral("/freq1/frequency"), QByteArrayLiteral("10000000\n"));
    writeFixture(card + QStringLiteral("/freq2/seconds"), QByteArrayLiteral("0\n"));
    writeFixture(card + QStringLiteral("/freq2/frequency"), QByteArray());
    writeFixture(card + QStringLiteral("/freq3/seconds"), QByteArrayLiteral("1\n"));
    writeFixture(card + QStringLiteral("/freq3/frequency"), QByteArrayLiteral("error\n"));
    writeFixture(card + QStringLiteral("/freq4/seconds"), QByteArrayLiteral("1\n"));
    writeFixture(card + QStringLiteral("/freq4/frequency"), QByteArrayLiteral("overrun\n"));
    writeFixture(card + QStringLiteral("/external_pps_polarity"), QByteArrayLiteral("1\n"));
    writeFixture(card + QStringLiteral("/external_pps_pulse_width"), QByteArrayLiteral("100\n"));
    writeFixture(card + QStringLiteral("/nmea_enable"), QByteArrayLiteral("1\n"));
    writeFixture(card + QStringLiteral("/nmea_baud_rate"), QByteArrayLiteral("115200\n"));
    writeFixture(card + QStringLiteral("/tod_protocol"), QByteArrayLiteral("NMEA\n"));
    writeFixture(card + QStringLiteral("/tod_errors"), QByteArrayLiteral("0x0\n"));
    writeFixture(card + QStringLiteral("/irig_output_error"), QByteArrayLiteral("0\n"));
    writeFixture(card + QStringLiteral("/optional_image_contract"), QByteArrayLiteral(
        "pci=0000:03:00.0 actual=0x12345678 expected=0x12345678 targeted=1 match=1 loader=0\n"));

    LinuxTimeCardBackend backend(fixture.path());
    const TimeCardSnapshot snapshot = backend.readSnapshot();

    QCOMPARE(snapshot.smaStates.size(), 1);
    QVERIFY(snapshot.smaStates.constFirst().contains(QStringLiteral("PPS1")));
    QCOMPARE(snapshot.generatorStates.size(), 1);
    QVERIFY(snapshot.generatorStates.constFirst().contains(QStringLiteral("continuous")));
    QVERIFY(snapshot.generatorStates.constFirst().contains(QStringLiteral("cable 25 ns")));
    QCOMPARE(snapshot.frequencyCounterStates.size(), 4);
    QVERIFY(snapshot.frequencyCounterStates.constFirst().contains(QStringLiteral("10000000 Hz")));
    QVERIFY(snapshot.frequencyCounterStates.at(1).contains(QStringLiteral("disabled")));
    QVERIFY(snapshot.frequencyCounterStates.at(2).contains(QStringLiteral("error")));
    QVERIFY(snapshot.frequencyCounterStates.at(3).contains(QStringLiteral("overrun")));
    QVERIFY(snapshot.error.contains(QStringLiteral("FREQ3 reports error")));
    QVERIFY(snapshot.error.contains(QStringLiteral("FREQ4 reports overrun")));
    QCOMPARE(snapshot.fpgaEngineStates.size(), 4);
    QVERIFY(snapshot.fpgaEngineStates.join(QLatin1Char('\n')).contains(QStringLiteral("ToD parser")));
    QVERIFY(snapshot.optionalImageContract.contains(QStringLiteral("match=1")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("FPGA engine status")));
}

void LinuxBackendTest::reportsFpgaFaultAndContractMismatch()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QDir root(fixture.path());
    QVERIFY(root.mkdir(QStringLiteral("ocp9")));
    const QString card = root.filePath(QStringLiteral("ocp9"));
    writeFixture(card + QStringLiteral("/tod_errors"), QByteArrayLiteral("0x2\n"));
    writeFixture(card + QStringLiteral("/optional_image_contract"), QByteArrayLiteral(
        "pci=0000:03:00.0 actual=0x12345678 expected=0x87654321 targeted=1 match=0 loader=0\n"));

    LinuxTimeCardBackend backend(fixture.path());
    const TimeCardSnapshot snapshot = backend.readSnapshot();
    QVERIFY(snapshot.error.contains(QStringLiteral("does not match")));
    QVERIFY(snapshot.error.contains(QStringLiteral("tod_errors")));
}

void LinuxBackendTest::readsR4006StandardSubsystemTelemetry()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QDir root(fixture.path());
    const QString selectedPci = QStringLiteral("devices/0000:03:00.0");
    QVERIFY(root.mkpath(selectedPci + QStringLiteral("/timecard/ocp0")));
    QVERIFY(root.mkpath(QStringLiteral("class/timecard")));
    QVERIFY(root.mkpath(QStringLiteral("class/hwmon")));
    QVERIFY(root.mkpath(QStringLiteral("class/iio")));
    QVERIFY(root.mkpath(QStringLiteral("leds")));
    writeFixture(root.filePath(selectedPci + QStringLiteral("/vendor")),
        QByteArrayLiteral("0x1d9b\n"));
    writeFixture(root.filePath(selectedPci + QStringLiteral("/device")),
        QByteArrayLiteral("0x0400\n"));

    const QString selectedRoot = root.filePath(selectedPci);
    const QString adapter = selectedRoot + QStringLiteral("/i2c-4");
    const QString mux = adapter + QStringLiteral("/4-0070");
    QVERIFY(root.mkpath(selectedPci + QStringLiteral("/i2c-4/4-0070")));
    for (int bus = 5; bus <= 7; ++bus)
        QVERIFY(root.mkpath(selectedPci + QStringLiteral("/i2c-4/i2c-%1").arg(bus)));
    QVERIFY(QFile::link(adapter + QStringLiteral("/i2c-5"),
        mux + QStringLiteral("/channel-0")));
    QVERIFY(QFile::link(adapter + QStringLiteral("/i2c-6"),
        mux + QStringLiteral("/channel-1")));
    QVERIFY(QFile::link(adapter + QStringLiteral("/i2c-7"),
        mux + QStringLiteral("/channel-2")));

    const QString card = selectedRoot + QStringLiteral("/timecard/ocp0");
    QVERIFY(QFile::link(adapter, card + QStringLiteral("/i2c")));
    QVERIFY(QFile::link(card, root.filePath(QStringLiteral("class/timecard/ocp0"))));

    struct HwmonFixture {
        int bus;
        QString address;
        QString name;
        QByteArray temperature;
    };
    const QList<HwmonFixture> hwmonFixtures {
        {5, QStringLiteral("48"), QStringLiteral("lm75"), QByteArrayLiteral("41250\n")},
        {5, QStringLiteral("49"), QStringLiteral("lm75"), QByteArrayLiteral("42000\n")},
        {5, QStringLiteral("4a"), QStringLiteral("lm75"), QByteArrayLiteral("40875\n")},
        {6, QStringLiteral("44"), QStringLiteral("sht3x"), QByteArrayLiteral("39750\n")},
    };
    for (qsizetype index = 0; index < hwmonFixtures.size(); ++index) {
        const HwmonFixture &item = hwmonFixtures.at(index);
        const QString client = adapter + QStringLiteral("/i2c-%1/%1-00%2")
                                             .arg(item.bus)
                                             .arg(item.address);
        const QString monitor = client + QStringLiteral("/hwmon/hwmon%1").arg(index);
        QVERIFY(QDir().mkpath(monitor));
        writeFixture(monitor + QStringLiteral("/name"), item.name.toUtf8() + '\n');
        writeFixture(monitor + QStringLiteral("/temp1_input"), item.temperature);
        QVERIFY(QFile::link(monitor, root.filePath(
            QStringLiteral("class/hwmon/hwmon%1").arg(index))));
        if (item.name == QStringLiteral("sht3x")) {
            writeFixture(monitor + QStringLiteral("/humidity1_input"),
                QByteArrayLiteral("44200\n"));
        }
    }

    const QString icpClient = adapter + QStringLiteral("/i2c-7/7-0063");
    const QString iio = icpClient + QStringLiteral("/iio/iio:device0");
    QVERIFY(QDir().mkpath(iio));
    writeFixture(iio + QStringLiteral("/name"), QByteArrayLiteral("icp10100\n"));
    writeFixture(iio + QStringLiteral("/in_pressure_input"),
        QByteArrayLiteral("101.325000\n"));
    writeFixture(iio + QStringLiteral("/in_temp_raw"), QByteArrayLiteral("32768\n"));
    QVERIFY(QFile::link(iio, root.filePath(QStringLiteral("class/iio/iio:device0"))));

    const QString wrongMonitor = root.filePath(QStringLiteral(
        "devices/0000:04:00.0/i2c-14/i2c-15/15-0048/hwmon/hwmon4"));
    QVERIFY(QDir().mkpath(wrongMonitor));
    writeFixture(wrongMonitor + QStringLiteral("/name"), QByteArrayLiteral("lm75\n"));
    writeFixture(wrongMonitor + QStringLiteral("/temp1_input"), QByteArrayLiteral("99000\n"));
    QVERIFY(QFile::link(wrongMonitor,
        root.filePath(QStringLiteral("class/hwmon/hwmon4"))));

    QVERIFY(root.mkpath(QStringLiteral(
        "leds/timecard-0000-03-00-0:rgb:indicator-gnss1")));

    const QString led = root.filePath(QStringLiteral(
        "leds/timecard-0000-03-00-0:rgb:indicator-gnss1"));
    writeFixture(led + QStringLiteral("/brightness"), QByteArrayLiteral("128\n"));
    writeFixture(led + QStringLiteral("/max_brightness"), QByteArrayLiteral("255\n"));
    writeFixture(led + QStringLiteral("/multi_index"), QByteArrayLiteral("red green blue\n"));
    writeFixture(led + QStringLiteral("/multi_intensity"), QByteArrayLiteral("0 255 0\n"));

    LinuxTimeCardBackend backend(root.filePath(QStringLiteral("class/timecard")),
        root.filePath(QStringLiteral("class/hwmon")),
        root.filePath(QStringLiteral("class/iio")), root.filePath(QStringLiteral("leds")));
    const TimeCardSnapshot snapshot = backend.readSnapshot();

    QCOMPARE(snapshot.sensorStates.size(), 7);
    QVERIFY(snapshot.r4006TopologyDetected);
    QVERIFY(snapshot.sensorStates.join(QLatin1Char('\n')).contains(QStringLiteral("LM75B 0x48")));
    QVERIFY(snapshot.sensorStates.join(QLatin1Char('\n')).contains(QStringLiteral("LM75B 0x49")));
    QVERIFY(snapshot.sensorStates.join(QLatin1Char('\n')).contains(QStringLiteral("LM75B 0x4a")));
    QVERIFY(snapshot.sensorStates.join(QLatin1Char('\n')).contains(QStringLiteral("41.25 C")));
    QVERIFY(snapshot.sensorStates.join(QLatin1Char('\n')).contains(QStringLiteral("44.20%")));
    QVERIFY(snapshot.sensorStates.join(QLatin1Char('\n')).contains(QStringLiteral("101.325 kPa")));
    QVERIFY(snapshot.sensorStates.join(QLatin1Char('\n')).contains(QStringLiteral("42.50 C")));
    QVERIFY(!snapshot.sensorStates.join(QLatin1Char('\n')).contains(QStringLiteral("99.00 C")));
    QCOMPARE(snapshot.ledStates.size(), 1);
    QVERIFY(snapshot.ledStates.constFirst().contains(QStringLiteral("RGB 0 255 0")));
    QCOMPARE(snapshot.boardProfile,
        QStringLiteral("R4006-compatible peripheral profile"));
}

void LinuxBackendTest::scopesLedsToSelectedPciFunction()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QDir root(fixture.path());
    QVERIFY(root.mkpath(QStringLiteral("devices/0000:03:00.0/timecard/ocp0")));
    QVERIFY(root.mkpath(QStringLiteral("class/timecard")));
    QVERIFY(root.mkpath(QStringLiteral("leds")));
    const QString pci = root.filePath(QStringLiteral("devices/0000:03:00.0"));
    writeFixture(pci + QStringLiteral("/vendor"), QByteArrayLiteral("0x1d9b\n"));
    writeFixture(pci + QStringLiteral("/device"), QByteArrayLiteral("0x0400\n"));
    QVERIFY(QFile::link(pci + QStringLiteral("/timecard/ocp0"),
        root.filePath(QStringLiteral("class/timecard/ocp0"))));

    const QStringList names = {
        QStringLiteral("timecard-0000-03-00-0:rgb:indicator-gnss1"),
        QStringLiteral("timecard-0000-04-00-0:rgb:indicator-gnss1"),
    };
    for (const QString &name : names) {
        QVERIFY(root.mkpath(QStringLiteral("leds/") + name));
        const QString led = root.filePath(QStringLiteral("leds/") + name);
        writeFixture(led + QStringLiteral("/brightness"), QByteArrayLiteral("128\n"));
        writeFixture(led + QStringLiteral("/max_brightness"), QByteArrayLiteral("255\n"));
        writeFixture(led + QStringLiteral("/multi_index"), QByteArrayLiteral("red green blue\n"));
        writeFixture(led + QStringLiteral("/multi_intensity"), QByteArrayLiteral("0 255 0\n"));
    }

    LinuxTimeCardBackend backend(root.filePath(QStringLiteral("class/timecard")),
        root.filePath(QStringLiteral("hwmon")), root.filePath(QStringLiteral("iio")),
        root.filePath(QStringLiteral("leds")));
    const TimeCardSnapshot snapshot = backend.readSnapshot();
    QCOMPARE(snapshot.pciAddress, QStringLiteral("0000:03:00.0"));
    QCOMPARE(snapshot.ledStates.size(), 1);
}

void LinuxBackendTest::detectsGnssCapabilityFromUart()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QDir root(fixture.path());
    QVERIFY(root.mkpath(QStringLiteral("ocp4/tty")));
    const QString card = root.filePath(QStringLiteral("ocp4"));
    writeFixture(card + QStringLiteral("/tty/ttyGNSS"), QByteArrayLiteral("ttyS8"));

    LinuxTimeCardBackend backend(fixture.path());
    const TimeCardSnapshot snapshot = backend.readSnapshot();
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("GNSS")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("UART")));
}

void LinuxBackendTest::cachesStaticIdentityBetweenSamples()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QDir root(fixture.path());
    QVERIFY(root.mkdir(QStringLiteral("ocp5")));
    const QString serialPath = root.filePath(QStringLiteral("ocp5/serialnum"));
    writeFixture(serialPath, QByteArrayLiteral("first\n"));

    LinuxTimeCardBackend backend(fixture.path());
    QCOMPARE(backend.readSnapshot().serialNumber, QStringLiteral("first"));
    writeFixture(serialPath, QByteArrayLiteral("second\n"));
    QCOMPARE(backend.readSnapshot().serialNumber, QStringLiteral("first"));
}

void LinuxBackendTest::rejectsUnsafeDeviceNode()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const QString entry = QDir(fixture.path()).filePath(QStringLiteral("ptp"));
    writeFixture(entry, QByteArrayLiteral("/dev/../null\n"));
    QVERIFY(LinuxTimeCardBackend::resolveDeviceNode(entry).isEmpty());
}

void LinuxBackendTest::discardsInFlightSnapshotAfterSelection()
{
    AppController controller(
        std::make_unique<SlowSwitchingBackend>(), QStringLiteral("127.0.0.1"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(controller.serialNumber(), QStringLiteral("serial-cardA"), 1000);

    controller.refresh();
    QTest::qWait(10);
    controller.setSelectedDevice(QStringLiteral("cardB"));
    QCOMPARE(controller.selectedDevice(), QStringLiteral("cardB"));
    QCOMPARE(controller.serialNumber(), QStringLiteral("Unavailable"));

    QTest::qWait(170);
    QCOMPARE(controller.selectedDevice(), QStringLiteral("cardB"));
    QCOMPARE(controller.serialNumber(), QStringLiteral("Unavailable"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.serialNumber(), QStringLiteral("serial-cardB"), 1000);
}

void LinuxBackendTest::prefersKernelTaiOffset()
{
    const TaiOffsetSelection kernel = selectTaiOffset(true, 37, true, 0);
    QVERIFY(kernel.valid);
    QCOMPARE(kernel.seconds, 37);
    QCOMPARE(kernel.source, TaiOffsetSource::Kernel);

    const TaiOffsetSelection cardFallback = selectTaiOffset(false, 0, true, 37);
    QVERIFY(cardFallback.valid);
    QCOMPARE(cardFallback.seconds, 37);
    QCOMPARE(cardFallback.source, TaiOffsetSource::TimeCard);

    const TaiOffsetSelection unavailable = selectTaiOffset(false, 0, true, 0);
    QVERIFY(!unavailable.valid);
    QCOMPARE(unavailable.source, TaiOffsetSource::Unavailable);

    const TaiOffsetSelection implausible = selectTaiOffset(true, 1000, true, 1000);
    QVERIFY(!implausible.valid);
}

void LinuxBackendTest::convertsTaiBeforeComparingClocks()
{
    constexpr qint64 systemUtc = 1'700'000'000'000'000'000LL;
    constexpr qint64 expectedOffset = 125;
    constexpr qint64 phcTai = systemUtc + 37'000'000'000LL + expectedOffset;

    const TimingDerivedValues values = deriveTaiAwareTiming(phcTai, systemUtc, 37);
    QCOMPARE(values.phcUtcNanoseconds, systemUtc + expectedOffset);
    QCOMPARE(values.offsetNanoseconds, expectedOffset);
}

void LinuxBackendTest::producesCoherentMockTelemetry()
{
    MockTimeCardBackend backend;
    const TimeCardSnapshot snapshot = backend.readSnapshot();
    QVERIFY(snapshot.connected);
    QVERIFY(snapshot.timingValid);
    QVERIFY(snapshot.offsetValid);
    QCOMPARE(snapshot.phcUtcNanoseconds - snapshot.systemUtcNanoseconds,
        snapshot.offsetNanoseconds);
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("GNSS")));
}

void LinuxBackendTest::parsesChunkedOscillatordStatus()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    connect(&server, &QTcpServer::newConnection, this, [&server] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            const QByteArray request = socket->readAll();
            QCOMPARE(request, QByteArrayLiteral("{\"request\":0}"));
            socket->write(QByteArrayLiteral(
                "{\"service\":\"oscillatord\",\"version\":\"3.10.0\","
                "\"protocol_version\":1,"));
            socket->flush();
            QTimer::singleShot(10, socket, [socket] {
                socket->write(QByteArrayLiteral(
                    "\"control_enabled\":false,\"Action requested\":\"None\","
                    "\"clock\":{\"class\":\"locked\",\"offset\":-7},"
                    "\"disciplining\":{\"status\":\"tracking\","
                    "\"current_phase_convergence_count\":21,"
                    "\"valid_phase_convergence_threshold\":50,"
                    "\"convergence_progress\":42.0,\"ready_for_holdover\":true},"
                    "\"oscillator\":{\"model\":\"mRO-50\",\"fine_ctrl\":1234,"
                    "\"coarse_ctrl\":5678,\"lock\":true,\"temperature\":41.5},"
                    "\"gnss\":{\"fix\":3,\"fixOk\":true,\"antenna_power\":1,"
                    "\"antenna_status\":2,\"lsChange\":0,\"leap_seconds\":18,"
                    "\"satellites_count\":18,\"survey_in_position_error\":0.42,"
                    "\"time_accuracy\":12}}"));
                socket->flush();
            });
        });
    });

    OscillatordClient client(
        QStringLiteral("127.0.0.1"), server.serverPort());
    QSignalSpy updated(&client, &OscillatordClient::updated);
    client.poll();

    QTRY_VERIFY_WITH_TIMEOUT(client.available(), 1000);
    QVERIFY(!updated.isEmpty());
    QCOMPARE(client.serviceVersion(), QStringLiteral("3.10.0"));
    QCOMPARE(client.actionRequested(), QStringLiteral("None"));
    QCOMPARE(client.clockClass(), QStringLiteral("locked"));
    QCOMPARE(client.clockOffsetNanoseconds(), -7);
    QVERIFY(client.disciplineAvailable());
    QCOMPARE(client.disciplineStatus(), QStringLiteral("tracking"));
    QCOMPARE(client.currentConvergenceCount(), 21);
    QCOMPARE(client.convergenceThreshold(), 50);
    QCOMPARE(client.convergenceProgress(), 42.0);
    QVERIFY(client.readyForHoldover());
    QCOMPARE(client.oscillatorModel(), QStringLiteral("mRO-50"));
    QCOMPARE(client.fineControl(), 1234);
    QCOMPARE(client.coarseControl(), 5678);
    QVERIFY(client.oscillatorLocked());
    QCOMPARE(client.oscillatorTemperature(), 41.5);
    QCOMPARE(client.gnssFix(), 3);
    QVERIFY(client.gnssFixOk());
    QCOMPARE(client.antennaPower(), 1);
    QCOMPARE(client.antennaStatus(), 2);
    QCOMPARE(client.leapSecondChange(), 0);
    QCOMPARE(client.leapSeconds(), 18);
    QCOMPARE(client.satellites(), 18);
    QCOMPARE(client.surveyPositionErrorMeters(), 0.42);
    QCOMPARE(client.timeAccuracyNanoseconds(), 12);
}

void LinuxBackendTest::exposesOscillatordParitySemantics()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, this, [&server] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            socket->write(QByteArrayLiteral(
                "{\"service\":\"oscillatord\",\"version\":\"3.10.0\","
                "\"protocol_version\":1,\"Action requested\":\"None\","
                "\"control_enabled\":false,"
                "\"clock\":{\"class\":\"locked\",\"offset\":-7},"
                "\"disciplining\":{\"status\":\"tracking\","
                "\"convergence_progress\":100,\"ready_for_holdover\":true},"
                "\"oscillator\":{\"model\":\"mRO-50\","
                "\"fine_ctrl\":4294967295,\"coarse_ctrl\":4294967295,\"lock\":true,"
                "\"temperature\":41.5},"
                "\"gnss\":{\"fixOk\":true,\"antenna_power\":1,"
                "\"antenna_status\":2,\"leap_seconds\":18,"
                "\"lsChange\":-10,\"satellites_count\":18}}"));
            socket->flush();
        });
    });

    AppController controller(std::make_unique<MockTimeCardBackend>(),
        QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(controller.oscillatordObserved(), 1000);
    QVERIFY(controller.oscillatordAvailable());
    QCOMPARE(controller.oscillatordEndpoint(),
        QStringLiteral("127.0.0.1:%1").arg(server.serverPort()));
    QCOMPARE(controller.oscillatordActionRequested(), QStringLiteral("None"));
    QCOMPARE(controller.oscillatorControlSummary(),
        QStringLiteral("fine unavailable, coarse unavailable"));
    QCOMPARE(controller.holdoverReadiness(), QStringLiteral("Ready for holdover"));
    QVERIFY(controller.oscillatordGnssDetail().contains(QStringLiteral("GPS-UTC 18 s")));
    QVERIFY(!controller.oscillatordGnssDetail().contains(QStringLiteral("pending")));
    QVERIFY(controller.oscillatordControlPolicy().contains(QStringLiteral("not correlated")));
    QTRY_VERIFY_WITH_TIMEOUT(controller.clockDrift().contains(QStringLiteral("ppb")), 1000);
    QVERIFY(controller.sessionLog().join(QLatin1Char('\n')).contains(
        QStringLiteral("oscillatord@127.0.0.1:")));
}

void LinuxBackendTest::rejectsUnsupportedOscillatordProtocol()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    connect(&server, &QTcpServer::newConnection, this, [&server] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            socket->write(QByteArrayLiteral(
                "{\"service\":\"oscillatord\",\"version\":\"3.10.0\","
                "\"protocol_version\":2}"));
            socket->flush();
        });
    });

    OscillatordClient client(QStringLiteral("127.0.0.1"), server.serverPort());
    QSignalSpy updated(&client, &OscillatordClient::updated);
    client.poll();

    QTRY_VERIFY_WITH_TIMEOUT(!updated.isEmpty(), 1000);
    QVERIFY(!client.available());
    QVERIFY(client.error().contains(QStringLiteral("Unsupported")));
}

void LinuxBackendTest::acceptsOscillatordStatusWithoutDisciplining()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    connect(&server, &QTcpServer::newConnection, this, [&server] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            socket->write(QByteArrayLiteral(
                "{\"service\":\"oscillatord\",\"version\":\"3.10.0\","
                "\"protocol_version\":1,"
                "\"clock\":{\"class\":\"unavailable\",\"offset\":0},"
                "\"oscillator\":{\"model\":\"mRO-50\",\"lock\":false,"
                "\"temperature\":-400.0},"
                "\"gnss\":{\"fixOk\":false,\"satellites_count\":-1}}"));
            socket->flush();
        });
    });

    OscillatordClient client(QStringLiteral("127.0.0.1"), server.serverPort());
    client.poll();

    QTRY_VERIFY_WITH_TIMEOUT(client.available(), 1000);
    QVERIFY(!client.disciplineAvailable());
    QCOMPARE(client.oscillatorTemperature(), -400.0);
    QCOMPARE(client.satellites(), -1);
}

void LinuxBackendTest::boundsAndExportsStructuredSessionLog()
{
    SessionLogStore log(2);
    log.append(SessionLogSeverity::Information, QStringLiteral("Connection"),
        QStringLiteral("ocp0"), QStringLiteral("Connected"),
        QDateTime::fromString(QStringLiteral("2026-08-02T12:00:00Z"), Qt::ISODate));
    log.append(SessionLogSeverity::Warning, QStringLiteral("Telemetry"),
        QStringLiteral("ocp0"), QStringLiteral("First line\nSecond line"));
    log.append(SessionLogSeverity::Error, QStringLiteral("Sensors"),
        QStringLiteral("ocp0"), QStringLiteral("Read failed"));

    QCOMPARE(log.count(), 2);
    QCOMPARE(log.droppedRecordCount(), 1);
    QVERIFY(log.textLines().constFirst().contains(QStringLiteral("First line Second line")));

    const QJsonDocument document = QJsonDocument::fromJson(log.toJson());
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(document.object().value(QStringLiteral("retainedRecordCount")).toInt(), 2);
    QCOMPARE(document.object().value(QStringLiteral("droppedRecordCount")).toInt(), 1);
    QCOMPARE(document.object().value(QStringLiteral("records")).toArray().size(), 2);

    QTemporaryDir exportDirectory;
    QVERIFY(exportDirectory.isValid());
    const QString exportPath = QDir(exportDirectory.path()).filePath(
        QStringLiteral("session.json"));
    QString exportError;
    QVERIFY2(log.writeJson(exportPath, &exportError), qPrintable(exportError));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const QJsonDocument exportedDocument = QJsonDocument::fromJson(exported.readAll());
    QVERIFY(exportedDocument.isObject());
    QCOMPARE(exportedDocument.object().value(QStringLiteral("retainedRecordCount")).toInt(), 2);
}

QTEST_MAIN(LinuxBackendTest)
#include "tst_linuxbackend.moc"
