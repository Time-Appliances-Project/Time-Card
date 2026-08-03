#pragma once

#include "OscillatordClient.h"
#include "SessionLog.h"
#include "TimeCardBackend.h"

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <memory>

class AppController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY snapshotChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY snapshotChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList availableDevices READ availableDevices NOTIFY snapshotChanged)
    Q_PROPERTY(QString selectedDevice READ selectedDevice WRITE setSelectedDevice NOTIFY snapshotChanged)
    Q_PROPERTY(QString serialNumber READ serialNumber NOTIFY snapshotChanged)
    Q_PROPERTY(QString boardProfile READ boardProfile NOTIFY snapshotChanged)
    Q_PROPERTY(QString pciIdentity READ pciIdentity NOTIFY snapshotChanged)
    Q_PROPERTY(QString sysfsPath READ sysfsPath NOTIFY snapshotChanged)
    Q_PROPERTY(QString ptpDevice READ ptpDevice NOTIFY snapshotChanged)
    Q_PROPERTY(QString ppsDevice READ ppsDevice NOTIFY snapshotChanged)
    Q_PROPERTY(QString i2cDevice READ i2cDevice NOTIFY snapshotChanged)
    Q_PROPERTY(QString mro50Device READ mro50Device NOTIFY snapshotChanged)
    Q_PROPERTY(QString phcTime READ phcTime NOTIFY snapshotChanged)
    Q_PROPERTY(QString systemTime READ systemTime NOTIFY snapshotChanged)
    Q_PROPERTY(bool timingValid READ timingValid NOTIFY snapshotChanged)
    Q_PROPERTY(bool offsetValid READ offsetValid NOTIFY snapshotChanged)
    Q_PROPERTY(bool sampleWindowValid READ sampleWindowValid NOTIFY snapshotChanged)
    Q_PROPERTY(QString offsetText READ offsetText NOTIFY snapshotChanged)
    Q_PROPERTY(QString sampleWindowText READ sampleWindowText NOTIFY snapshotChanged)
    Q_PROPERTY(double offsetNanoseconds READ offsetNanoseconds NOTIFY snapshotChanged)
    Q_PROPERTY(QString timestampMethod READ timestampMethod NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList offsetHistory READ offsetHistory NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList windowHistory READ windowHistory NOTIFY snapshotChanged)
    Q_PROPERTY(QString clockSource READ clockSource NOTIFY snapshotChanged)
    Q_PROPERTY(QString gnssState READ gnssState NOTIFY snapshotChanged)
    Q_PROPERTY(bool gnssLocked READ gnssLocked NOTIFY snapshotChanged)
    Q_PROPERTY(QString utcTaiOffset READ utcTaiOffset NOTIFY snapshotChanged)
    Q_PROPERTY(QString clockDrift READ clockDrift NOTIFY snapshotChanged)
    Q_PROPERTY(QString clockOffset READ clockOffset NOTIFY snapshotChanged)
    Q_PROPERTY(QString todProtocol READ todProtocol NOTIFY snapshotChanged)
    Q_PROPERTY(QString todBaudRate READ todBaudRate NOTIFY snapshotChanged)
    Q_PROPERTY(QString ttyGnss READ ttyGnss NOTIFY snapshotChanged)
    Q_PROPERTY(QString ttyGnss2 READ ttyGnss2 NOTIFY snapshotChanged)
    Q_PROPERTY(QString ttyMac READ ttyMac NOTIFY snapshotChanged)
    Q_PROPERTY(QString ttyNmea READ ttyNmea NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList capabilities READ capabilities NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList smaStates READ smaStates NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList generatorStates READ generatorStates NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList frequencyCounterStates READ frequencyCounterStates NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList fpgaEngineStates READ fpgaEngineStates NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList sensorStates READ sensorStates NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList ledStates READ ledStates NOTIFY snapshotChanged)
    Q_PROPERTY(QString optionalImageContract READ optionalImageContract NOTIFY snapshotChanged)
    Q_PROPERTY(QString error READ error NOTIFY snapshotChanged)
    Q_PROPERTY(QString lastUpdated READ lastUpdated NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList sessionLog READ sessionLog NOTIFY snapshotChanged)
    Q_PROPERTY(QString sessionLogStatus READ sessionLogStatus NOTIFY snapshotChanged)
    Q_PROPERTY(bool oscillatordObserved READ oscillatordObserved NOTIFY snapshotChanged)
    Q_PROPERTY(bool oscillatordAvailable READ oscillatordAvailable NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordEndpoint READ oscillatordEndpoint NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordVersion READ oscillatordVersion NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordActionRequested READ oscillatordActionRequested NOTIFY snapshotChanged)
    Q_PROPERTY(bool disciplineAvailable READ disciplineAvailable NOTIFY snapshotChanged)
    Q_PROPERTY(QString disciplineStatus READ disciplineStatus NOTIFY snapshotChanged)
    Q_PROPERTY(QString disciplineProgressDetail READ disciplineProgressDetail NOTIFY snapshotChanged)
    Q_PROPERTY(QString holdoverReadiness READ holdoverReadiness NOTIFY snapshotChanged)
    Q_PROPERTY(double convergenceProgress READ convergenceProgress NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordClockSummary READ oscillatordClockSummary NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatorSummary READ oscillatorSummary NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatorControlSummary READ oscillatorControlSummary NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordGnssSummary READ oscillatordGnssSummary NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordGnssDetail READ oscillatordGnssDetail NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordAntennaSummary READ oscillatordAntennaSummary NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordControlPolicy READ oscillatordControlPolicy NOTIFY snapshotChanged)
    Q_PROPERTY(QString oscillatordError READ oscillatordError NOTIFY snapshotChanged)

public:
    AppController(
        std::unique_ptr<TimeCardBackend> backend,
        QString oscillatordHost,
        quint16 oscillatordPort,
        QObject *parent = nullptr);
    ~AppController() override;

    bool connected() const;
    QString connectionState() const;
    QString backendName() const;
    QStringList availableDevices() const;
    QString selectedDevice() const;
    void setSelectedDevice(const QString &deviceId);
    QString serialNumber() const;
    QString boardProfile() const;
    QString pciIdentity() const;
    QString sysfsPath() const;
    QString ptpDevice() const;
    QString ppsDevice() const;
    QString i2cDevice() const;
    QString mro50Device() const;
    QString phcTime() const;
    QString systemTime() const;
    bool timingValid() const;
    bool offsetValid() const;
    bool sampleWindowValid() const;
    QString offsetText() const;
    QString sampleWindowText() const;
    double offsetNanoseconds() const;
    QString timestampMethod() const;
    QVariantList offsetHistory() const;
    QVariantList windowHistory() const;
    QString clockSource() const;
    QString gnssState() const;
    bool gnssLocked() const;
    QString utcTaiOffset() const;
    QString clockDrift() const;
    QString clockOffset() const;
    QString todProtocol() const;
    QString todBaudRate() const;
    QString ttyGnss() const;
    QString ttyGnss2() const;
    QString ttyMac() const;
    QString ttyNmea() const;
    QStringList capabilities() const;
    QStringList smaStates() const;
    QStringList generatorStates() const;
    QStringList frequencyCounterStates() const;
    QStringList fpgaEngineStates() const;
    QStringList sensorStates() const;
    QStringList ledStates() const;
    QString optionalImageContract() const;
    QString error() const;
    QString lastUpdated() const;
    QStringList sessionLog() const;
    QString sessionLogStatus() const;
    bool oscillatordObserved() const;
    bool oscillatordAvailable() const;
    QString oscillatordEndpoint() const;
    QString oscillatordVersion() const;
    QString oscillatordActionRequested() const;
    bool disciplineAvailable() const;
    QString disciplineStatus() const;
    QString disciplineProgressDetail() const;
    QString holdoverReadiness() const;
    double convergenceProgress() const;
    QString oscillatordClockSummary() const;
    QString oscillatorSummary() const;
    QString oscillatorControlSummary() const;
    QString oscillatordGnssSummary() const;
    QString oscillatordGnssDetail() const;
    QString oscillatordAntennaSummary() const;
    QString oscillatordControlPolicy() const;
    QString oscillatordError() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshOscillatord();
    Q_INVOKABLE void clearSessionLog();
    Q_INVOKABLE void exportSessionLogToDocuments();

    bool writeSessionLog(const QString &path, QString *error = nullptr) const;

signals:
    void snapshotChanged();

private:
    void applySnapshot(TimeCardSnapshot snapshot);
    static QString formatTimestamp(qint64 nanoseconds, bool valid);
    static QString formatDuration(qint64 nanoseconds, bool valid, bool forceSign = false);
    static QString availableOr(const QString &value);
    static void appendBounded(QVariantList *history, double value, qsizetype capacity);
    QString logCardContext() const;
    void appendLog(
        SessionLogSeverity severity, const QString &category, const QString &message,
        const QString &context = QString());

    std::unique_ptr<TimeCardBackend> m_backend;
    TimeCardSnapshot m_snapshot;
    QFutureWatcher<TimeCardSnapshot> m_snapshotWatcher;
    OscillatordClient m_oscillatord;
    QTimer m_refreshTimer;
    QVariantList m_offsetHistory;
    QVariantList m_windowHistory;
    SessionLogStore m_sessionLog;
    QString m_sessionLogStatus;
    QString m_lastUpdated;
    QString m_requestedDevice;
    quint64 m_selectionGeneration = 0;
    quint64 m_inFlightGeneration = 0;
    bool m_refreshInFlight = false;
    bool m_refreshPending = false;
    bool m_oscillatordObserved = false;
    bool m_previousOscillatordAvailable = false;
    QString m_previousOscillatordError;
};
