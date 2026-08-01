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
    QString serviceVersion() const;
    QString clockClass() const;
    qint64 clockOffsetNanoseconds() const;
    bool disciplineAvailable() const;
    QString disciplineStatus() const;
    double convergenceProgress() const;
    bool readyForHoldover() const;
    QString oscillatorModel() const;
    bool oscillatorLocked() const;
    double oscillatorTemperature() const;
    bool gnssFixOk() const;
    int satellites() const;
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
    QString m_clockClass;
    qint64 m_clockOffsetNanoseconds = 0;
    bool m_disciplineAvailable = false;
    QString m_disciplineStatus;
    double m_convergenceProgress = 0.0;
    bool m_readyForHoldover = false;
    QString m_oscillatorModel;
    bool m_oscillatorLocked = false;
    double m_oscillatorTemperature = 0.0;
    bool m_gnssFixOk = false;
    int m_satellites = 0;
    QString m_error;
};
