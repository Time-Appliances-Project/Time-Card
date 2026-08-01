#include "AppController.h"
#include "LinuxTimeCardBackend.h"
#include "MockTimeCardBackend.h"
#include "OscillatordClient.h"
#include "TimingMath.h"

#include <QDir>
#include <QFile>
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
    void detectsGnssCapabilityFromUart();
    void cachesStaticIdentityBetweenSamples();
    void rejectsUnsafeDeviceNode();
    void discardsInFlightSnapshotAfterSelection();
    void prefersKernelTaiOffset();
    void convertsTaiBeforeComparingClocks();
    void producesCoherentMockTelemetry();
    void parsesChunkedOscillatordStatus();
    void acceptsOscillatordStatusWithoutDisciplining();
    void rejectsUnsupportedOscillatordProtocol();
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
    QCOMPARE(snapshot.ttyGnss, QStringLiteral("/dev/ttyS4"));
    QCOMPARE(snapshot.ttyMac, QStringLiteral("/dev/ttyS6"));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("PHC")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("Signal generators")));
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
                    "\"clock\":{\"class\":\"locked\",\"offset\":-7},"
                    "\"disciplining\":{\"status\":\"tracking\","
                    "\"convergence_progress\":42.0,\"ready_for_holdover\":true},"
                    "\"oscillator\":{\"model\":\"mRO-50\",\"lock\":true,"
                    "\"temperature\":41.5},\"gnss\":{\"fixOk\":true,"
                    "\"satellites_count\":18}}"));
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
    QCOMPARE(client.clockClass(), QStringLiteral("locked"));
    QCOMPARE(client.clockOffsetNanoseconds(), -7);
    QVERIFY(client.disciplineAvailable());
    QCOMPARE(client.disciplineStatus(), QStringLiteral("tracking"));
    QCOMPARE(client.convergenceProgress(), 42.0);
    QVERIFY(client.readyForHoldover());
    QCOMPARE(client.oscillatorModel(), QStringLiteral("mRO-50"));
    QVERIFY(client.oscillatorLocked());
    QCOMPARE(client.oscillatorTemperature(), 41.5);
    QVERIFY(client.gnssFixOk());
    QCOMPARE(client.satellites(), 18);
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

QTEST_MAIN(LinuxBackendTest)
#include "tst_linuxbackend.moc"
