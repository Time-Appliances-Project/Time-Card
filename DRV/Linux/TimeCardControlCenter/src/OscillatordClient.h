#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

class OscillatordClient final : public QObject {
    Q_OBJECT

public:
    explicit OscillatordClient(
        QString host = QStringLiteral("127.0.0.1"),
        quint16 port = 2958,
        QObject *parent = nullptr);

    bool available() const;
    QString endpoint() const;
    QString serviceVersion() const;
    QString actionRequested() const;
    bool controlEnabled() const;
    QString clockClass() const;
    qint64 clockOffsetNanoseconds() const;
    bool disciplineAvailable() const;
    QString disciplineStatus() const;
    int currentConvergenceCount() const;
    int convergenceThreshold() const;
    double convergenceProgress() const;
    bool readyForHoldover() const;
    QString oscillatorModel() const;
    qint64 fineControl() const;
    qint64 coarseControl() const;
    bool oscillatorLocked() const;
    double oscillatorTemperature() const;
    int gnssFix() const;
    bool gnssFixOk() const;
    int antennaPower() const;
    int antennaStatus() const;
    int leapSecondChange() const;
    int leapSeconds() const;
    int satellites() const;
    double surveyPositionErrorMeters() const;
    qint64 timeAccuracyNanoseconds() const;
    QString error() const;

    void start();

public slots:
    void poll();

signals:
    void updated();

private:
    void resetResponse();
    void applyResponse(const QByteArray &response);
    void fail(const QString &message);

    QString m_host;
    quint16 m_port;
    QTcpSocket m_socket;
    QTimer m_pollTimer;
    QTimer m_timeoutTimer;
    QByteArray m_response;

    bool m_available = false;
    QString m_serviceVersion;
    QString m_actionRequested;
    bool m_controlEnabled = false;
    QString m_clockClass;
    qint64 m_clockOffsetNanoseconds = 0;
    bool m_disciplineAvailable = false;
    QString m_disciplineStatus;
    int m_currentConvergenceCount = -1;
    int m_convergenceThreshold = -1;
    double m_convergenceProgress = 0.0;
    bool m_readyForHoldover = false;
    QString m_oscillatorModel;
    qint64 m_fineControl = -1;
    qint64 m_coarseControl = -1;
    bool m_oscillatorLocked = false;
    double m_oscillatorTemperature = 0.0;
    int m_gnssFix = -1;
    bool m_gnssFixOk = false;
    int m_antennaPower = -1;
    int m_antennaStatus = -1;
    int m_leapSecondChange = -10;
    int m_leapSeconds = -1;
    int m_satellites = -1;
    double m_surveyPositionErrorMeters = -1.0;
    qint64 m_timeAccuracyNanoseconds = -1;
    QString m_error;
};
