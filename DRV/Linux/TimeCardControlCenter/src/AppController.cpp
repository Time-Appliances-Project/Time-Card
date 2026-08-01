#include "AppController.h"

#include <QDateTime>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <utility>

namespace {
constexpr qint64 nanosecondsPerSecond = 1'000'000'000LL;
}

AppController::AppController(
    std::unique_ptr<TimeCardBackend> backend,
    QString oscillatordHost,
    quint16 oscillatordPort,
    QObject *parent)
    : QObject(parent),
      m_backend(std::move(backend)),
      m_oscillatord(std::move(oscillatordHost), oscillatordPort)
{
    m_refreshTimer.setInterval(1000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &AppController::refresh);
    connect(&m_oscillatord, &OscillatordClient::updated,
        this, &AppController::snapshotChanged);
    connect(&m_snapshotWatcher, &QFutureWatcher<TimeCardSnapshot>::finished,
        this, [this] {
            m_refreshInFlight = false;
            if (m_inFlightGeneration == m_selectionGeneration)
                applySnapshot(m_snapshotWatcher.result());
            if (m_refreshPending) {
                m_refreshPending = false;
                QTimer::singleShot(0, this, &AppController::refresh);
            }
        });

    refresh();
    m_refreshTimer.start();
    m_oscillatord.start();
}

AppController::~AppController()
{
    m_refreshTimer.stop();
    m_snapshotWatcher.waitForFinished();
}

bool AppController::connected() const { return m_snapshot.connected; }
QString AppController::backendName() const { return m_snapshot.backendName; }
QStringList AppController::availableDevices() const { return m_snapshot.availableDevices; }
QString AppController::selectedDevice() const
{
    return m_requestedDevice.isEmpty() ? m_snapshot.deviceId : m_requestedDevice;
}
QString AppController::serialNumber() const { return availableOr(m_snapshot.serialNumber); }
QString AppController::sysfsPath() const { return availableOr(m_snapshot.sysfsPath); }
QString AppController::ptpDevice() const { return availableOr(m_snapshot.ptpDevice); }
QString AppController::ppsDevice() const { return availableOr(m_snapshot.ppsDevice); }
QString AppController::i2cDevice() const { return availableOr(m_snapshot.i2cDevice); }
QString AppController::mro50Device() const { return availableOr(m_snapshot.mro50Device); }
QString AppController::phcTime() const
{
    return formatTimestamp(m_snapshot.phcUtcNanoseconds, m_snapshot.timingValid
        && m_snapshot.utcTaiOffsetValid);
}
QString AppController::systemTime() const
{
    return formatTimestamp(m_snapshot.systemUtcNanoseconds,
        m_snapshot.systemUtcNanoseconds != 0);
}
bool AppController::timingValid() const { return m_snapshot.timingValid; }
bool AppController::offsetValid() const { return m_snapshot.offsetValid; }
bool AppController::sampleWindowValid() const { return m_snapshot.sampleWindowValid; }
QString AppController::offsetText() const
{
    return formatDuration(m_snapshot.offsetNanoseconds, m_snapshot.offsetValid, true);
}
QString AppController::sampleWindowText() const
{
    return formatDuration(
        m_snapshot.sampleWindowNanoseconds, m_snapshot.sampleWindowValid);
}
double AppController::offsetNanoseconds() const
{
    return m_snapshot.offsetValid ? static_cast<double>(m_snapshot.offsetNanoseconds) : 0.0;
}
QString AppController::timestampMethod() const { return availableOr(m_snapshot.timestampMethod); }
QVariantList AppController::offsetHistory() const { return m_offsetHistory; }
QVariantList AppController::windowHistory() const { return m_windowHistory; }
QString AppController::clockSource() const { return availableOr(m_snapshot.clockSource); }
QString AppController::gnssState() const { return availableOr(m_snapshot.gnssState); }
bool AppController::gnssLocked() const { return m_snapshot.gnssLocked; }
QString AppController::todProtocol() const { return availableOr(m_snapshot.todProtocol); }
QString AppController::todBaudRate() const { return availableOr(m_snapshot.todBaudRate); }
QString AppController::ttyGnss() const { return availableOr(m_snapshot.ttyGnss); }
QString AppController::ttyGnss2() const { return availableOr(m_snapshot.ttyGnss2); }
QString AppController::ttyMac() const { return availableOr(m_snapshot.ttyMac); }
QString AppController::ttyNmea() const { return availableOr(m_snapshot.ttyNmea); }
QStringList AppController::capabilities() const { return m_snapshot.capabilities; }
QString AppController::error() const { return m_snapshot.error; }
QString AppController::lastUpdated() const { return m_lastUpdated; }

QString AppController::connectionState() const
{
    if (!m_requestedDevice.isEmpty())
        return QStringLiteral("LOADING TIME CARD");
    if (!m_snapshot.connected)
        return QStringLiteral("WAITING FOR TIME CARD");
    if (m_snapshot.offsetValid)
        return QStringLiteral("TIME CARD READY");
    if (m_snapshot.timingValid)
        return QStringLiteral("TIME CARD TIMING LIMITED");
    return QStringLiteral("TIME CARD DISCOVERED");
}

QString AppController::pciIdentity() const
{
    QStringList parts;
    if (!m_snapshot.pciAddress.isEmpty())
        parts.append(m_snapshot.pciAddress);
    if (!m_snapshot.pciVendor.isEmpty() || !m_snapshot.pciDevice.isEmpty())
        parts.append(m_snapshot.pciVendor + QStringLiteral(":") + m_snapshot.pciDevice);
    return parts.isEmpty() ? QStringLiteral("Unavailable") : parts.join(QStringLiteral("  "));
}

QString AppController::utcTaiOffset() const
{
    if (!m_snapshot.utcTaiOffsetValid)
        return QStringLiteral("Unavailable");

    QString result = QStringLiteral("%1 s (%2)")
        .arg(m_snapshot.utcTaiOffsetSeconds)
        .arg(m_snapshot.utcTaiOffsetFromKernel
            ? QStringLiteral("kernel") : QStringLiteral("card"));
    if (m_snapshot.utcTaiOffsetFromKernel
        && m_snapshot.cardUtcTaiOffsetValid
        && m_snapshot.cardUtcTaiOffsetSeconds != m_snapshot.utcTaiOffsetSeconds) {
        result += QStringLiteral(", card %1 s").arg(m_snapshot.cardUtcTaiOffsetSeconds);
    }
    return result;
}

QString AppController::clockDrift() const
{
    return formatDuration(
        m_snapshot.clockDriftNanoseconds, m_snapshot.clockDriftValid, true);
}

QString AppController::clockOffset() const
{
    return formatDuration(
        m_snapshot.clockOffsetNanoseconds, m_snapshot.clockOffsetValid, true);
}

bool AppController::oscillatordAvailable() const { return m_oscillatord.available(); }
QString AppController::oscillatordVersion() const
{
    return m_oscillatord.available()
        ? QStringLiteral("oscillatord %1").arg(m_oscillatord.serviceVersion())
        : QStringLiteral("Not detected");
}
bool AppController::disciplineAvailable() const { return m_oscillatord.disciplineAvailable(); }
QString AppController::disciplineStatus() const
{
    return availableOr(m_oscillatord.disciplineStatus());
}
double AppController::convergenceProgress() const { return m_oscillatord.convergenceProgress(); }
QString AppController::oscillatordError() const { return m_oscillatord.error(); }

QString AppController::oscillatorSummary() const
{
    if (!m_oscillatord.available())
        return QStringLiteral("No monitoring response");
    const QString lock = m_oscillatord.oscillatorLocked()
        ? QStringLiteral("locked") : QStringLiteral("unlocked");
    const double temperature = m_oscillatord.oscillatorTemperature();
    const QString temperatureText = temperature < -273.15
        ? QStringLiteral("temperature unavailable")
        : QStringLiteral("%1 C").arg(temperature, 0, 'f', 1);
    return QStringLiteral("%1  %2  %3")
        .arg(availableOr(m_oscillatord.oscillatorModel()), lock, temperatureText);
}

QString AppController::oscillatordGnssSummary() const
{
    if (!m_oscillatord.available())
        return QStringLiteral("No monitoring response");
    const QString satellites = m_oscillatord.satellites() < 0
        ? QStringLiteral("satellites unavailable")
        : QStringLiteral("%1 satellites").arg(m_oscillatord.satellites());
    return QStringLiteral("%1  %2")
        .arg(m_oscillatord.gnssFixOk() ? QStringLiteral("valid fix") : QStringLiteral("no fix"),
            satellites);
}

void AppController::setSelectedDevice(const QString &deviceId)
{
    if (!m_snapshot.availableDevices.contains(deviceId)
        || deviceId == selectedDevice()) {
        return;
    }
    m_requestedDevice = deviceId;
    ++m_selectionGeneration;
    m_offsetHistory.clear();
    m_windowHistory.clear();

    TimeCardSnapshot pendingSnapshot;
    pendingSnapshot.connected = true;
    pendingSnapshot.backendName = m_snapshot.backendName;
    pendingSnapshot.availableDevices = m_snapshot.availableDevices;
    pendingSnapshot.deviceId = deviceId;
    m_snapshot = std::move(pendingSnapshot);
    emit snapshotChanged();
    refresh();
}

void AppController::refresh()
{
    if (m_refreshInFlight) {
        m_refreshPending = true;
        return;
    }

    const QString requestedDevice = m_requestedDevice;
    m_inFlightGeneration = m_selectionGeneration;
    m_refreshInFlight = true;
    m_snapshotWatcher.setFuture(QtConcurrent::run([this, requestedDevice] {
        if (!requestedDevice.isEmpty())
            m_backend->setSelectedDevice(requestedDevice);
        return m_backend->readSnapshot();
    }));
}

void AppController::applySnapshot(TimeCardSnapshot snapshot)
{
    const bool wasConnected = m_snapshot.connected;
    const bool wasOffsetValid = m_snapshot.offsetValid;
    const bool wasSampleWindowValid = m_snapshot.sampleWindowValid;
    const QString oldDevice = m_snapshot.deviceId;
    m_snapshot = std::move(snapshot);
    if (!m_requestedDevice.isEmpty()
        && (m_snapshot.deviceId == m_requestedDevice
            || !m_snapshot.availableDevices.contains(m_requestedDevice))) {
        m_requestedDevice.clear();
    }

    if ((!m_snapshot.connected && wasConnected) || oldDevice != m_snapshot.deviceId) {
        m_offsetHistory.clear();
        m_windowHistory.clear();
    }
    if (wasOffsetValid && !m_snapshot.offsetValid)
        m_offsetHistory.clear();
    if (wasSampleWindowValid && !m_snapshot.sampleWindowValid)
        m_windowHistory.clear();
    if (m_snapshot.offsetValid)
        appendBounded(&m_offsetHistory, static_cast<double>(m_snapshot.offsetNanoseconds), 200);
    if (m_snapshot.sampleWindowValid)
        appendBounded(&m_windowHistory,
            static_cast<double>(m_snapshot.sampleWindowNanoseconds), 60);

    m_lastUpdated = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    emit snapshotChanged();
}

void AppController::refreshOscillatord()
{
    m_oscillatord.poll();
}

QString AppController::formatTimestamp(qint64 nanoseconds, bool valid)
{
    if (!valid)
        return QStringLiteral("Unavailable");

    qint64 seconds = nanoseconds / nanosecondsPerSecond;
    qint64 remainder = nanoseconds % nanosecondsPerSecond;
    if (remainder < 0) {
        --seconds;
        remainder += nanosecondsPerSecond;
    }
    const QDateTime value = QDateTime::fromSecsSinceEpoch(seconds).toUTC();
    return value.toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss"))
        + QStringLiteral(".%1 UTC").arg(remainder, 9, 10, QLatin1Char('0'));
}

QString AppController::formatDuration(qint64 nanoseconds, bool valid, bool forceSign)
{
    if (!valid)
        return QStringLiteral("Unavailable");

    const double absolute = std::abs(static_cast<double>(nanoseconds));
    const QString sign = forceSign && nanoseconds >= 0 ? QStringLiteral("+") : QString();
    if (absolute >= 1'000'000'000.0)
        return QStringLiteral("%1%2 s").arg(sign).arg(nanoseconds / 1.0e9, 0, 'f', 6);
    if (absolute >= 1'000'000.0)
        return QStringLiteral("%1%2 ms").arg(sign).arg(nanoseconds / 1.0e6, 0, 'f', 3);
    if (absolute >= 1'000.0)
        return QStringLiteral("%1%2 us").arg(sign).arg(nanoseconds / 1.0e3, 0, 'f', 3);
    return QStringLiteral("%1%2 ns").arg(sign).arg(nanoseconds);
}

QString AppController::availableOr(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("Unavailable") : value;
}

void AppController::appendBounded(QVariantList *history, double value, qsizetype capacity)
{
    history->append(value);
    while (history->size() > capacity)
        history->removeFirst();
}
