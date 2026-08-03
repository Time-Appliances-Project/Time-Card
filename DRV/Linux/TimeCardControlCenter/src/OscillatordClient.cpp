#include "OscillatordClient.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace {
constexpr qsizetype maximumResponseBytes = 1024 * 1024;

qint64 optionalInteger(
    const QJsonObject &object, const QString &name, qint64 fallback)
{
    const QJsonValue value = object.value(name);
    return value.isDouble() ? value.toVariant().toLongLong() : fallback;
}
}

OscillatordClient::OscillatordClient(QString host, quint16 port, QObject *parent)
    : QObject(parent), m_host(std::move(host)), m_port(port)
{
    m_pollTimer.setInterval(5000);
    m_timeoutTimer.setInterval(1000);
    m_timeoutTimer.setSingleShot(true);

    connect(&m_pollTimer, &QTimer::timeout, this, &OscillatordClient::poll);
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        static const QByteArray statusRequest = QByteArrayLiteral("{\"request\":0}");
        m_socket.write(statusRequest);
        m_socket.flush();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this] {
        m_response.append(m_socket.readAll());
        if (m_response.size() > maximumResponseBytes) {
            fail(QStringLiteral("oscillatord returned an oversized response"));
            m_socket.abort();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(m_response, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            applyResponse(m_response);
            m_timeoutTimer.stop();
            m_socket.disconnectFromHost();
        }
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this,
        [this](QAbstractSocket::SocketError) {
            fail(m_socket.errorString());
        });
    connect(&m_timeoutTimer, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("oscillatord status request timed out"));
        m_socket.abort();
    });
}

bool OscillatordClient::available() const { return m_available; }
QString OscillatordClient::endpoint() const
{
    QString displayHost = m_host;
    if (displayHost.contains(QLatin1Char(':'))
        && !displayHost.startsWith(QLatin1Char('['))) {
        displayHost = QLatin1Char('[') + displayHost + QLatin1Char(']');
    }
    return QStringLiteral("%1:%2").arg(displayHost).arg(m_port);
}
QString OscillatordClient::serviceVersion() const { return m_serviceVersion; }
QString OscillatordClient::actionRequested() const { return m_actionRequested; }
bool OscillatordClient::controlEnabled() const { return m_controlEnabled; }
QString OscillatordClient::clockClass() const { return m_clockClass; }
qint64 OscillatordClient::clockOffsetNanoseconds() const { return m_clockOffsetNanoseconds; }
bool OscillatordClient::disciplineAvailable() const { return m_disciplineAvailable; }
QString OscillatordClient::disciplineStatus() const { return m_disciplineStatus; }
int OscillatordClient::currentConvergenceCount() const { return m_currentConvergenceCount; }
int OscillatordClient::convergenceThreshold() const { return m_convergenceThreshold; }
double OscillatordClient::convergenceProgress() const { return m_convergenceProgress; }
bool OscillatordClient::readyForHoldover() const { return m_readyForHoldover; }
QString OscillatordClient::oscillatorModel() const { return m_oscillatorModel; }
qint64 OscillatordClient::fineControl() const { return m_fineControl; }
qint64 OscillatordClient::coarseControl() const { return m_coarseControl; }
bool OscillatordClient::oscillatorLocked() const { return m_oscillatorLocked; }
double OscillatordClient::oscillatorTemperature() const { return m_oscillatorTemperature; }
int OscillatordClient::gnssFix() const { return m_gnssFix; }
bool OscillatordClient::gnssFixOk() const { return m_gnssFixOk; }
int OscillatordClient::antennaPower() const { return m_antennaPower; }
int OscillatordClient::antennaStatus() const { return m_antennaStatus; }
int OscillatordClient::leapSecondChange() const { return m_leapSecondChange; }
int OscillatordClient::leapSeconds() const { return m_leapSeconds; }
int OscillatordClient::satellites() const { return m_satellites; }
double OscillatordClient::surveyPositionErrorMeters() const
{
    return m_surveyPositionErrorMeters;
}
qint64 OscillatordClient::timeAccuracyNanoseconds() const
{
    return m_timeAccuracyNanoseconds;
}
QString OscillatordClient::error() const { return m_error; }

void OscillatordClient::start()
{
    poll();
    m_pollTimer.start();
}

void OscillatordClient::poll()
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        return;
    resetResponse();
    m_socket.connectToHost(m_host, m_port);
    m_timeoutTimer.start();
}

void OscillatordClient::resetResponse()
{
    m_response.clear();
}

void OscillatordClient::applyResponse(const QByteArray &response)
{
    const QJsonObject root = QJsonDocument::fromJson(response).object();
    const QString service = root.value(QStringLiteral("service")).toString();
    if (service != QStringLiteral("oscillatord")) {
        fail(QStringLiteral("Unexpected monitoring service response"));
        return;
    }
    if (!root.value(QStringLiteral("version")).isString()) {
        fail(QStringLiteral("oscillatord response is missing service metadata"));
        return;
    }

    const QJsonValue protocol = root.value(QStringLiteral("protocol_version"));
    if (!protocol.isDouble() || protocol.toInt() != 1) {
        fail(QStringLiteral("Unsupported oscillatord monitoring protocol"));
        return;
    }

    const QJsonValue clockValue = root.value(QStringLiteral("clock"));
    const QJsonValue oscillatorValue = root.value(QStringLiteral("oscillator"));
    const QJsonValue gnssValue = root.value(QStringLiteral("gnss"));
    if (!clockValue.isObject() || !oscillatorValue.isObject() || !gnssValue.isObject()) {
        fail(QStringLiteral("oscillatord response is missing required telemetry"));
        return;
    }

    const QJsonObject clock = clockValue.toObject();
    const QJsonObject oscillator = oscillatorValue.toObject();
    const QJsonObject gnss = gnssValue.toObject();
    if (!clock.value(QStringLiteral("class")).isString()
        || !clock.value(QStringLiteral("offset")).isDouble()
        || !oscillator.value(QStringLiteral("model")).isString()
        || !oscillator.value(QStringLiteral("lock")).isBool()
        || !oscillator.value(QStringLiteral("temperature")).isDouble()
        || !gnss.value(QStringLiteral("fixOk")).isBool()
        || !gnss.value(QStringLiteral("satellites_count")).isDouble()) {
        fail(QStringLiteral("oscillatord response contains invalid telemetry types"));
        return;
    }

    m_disciplineAvailable = false;
    m_disciplineStatus.clear();
    m_currentConvergenceCount = -1;
    m_convergenceThreshold = -1;
    m_convergenceProgress = 0.0;
    m_readyForHoldover = false;
    const QJsonValue disciplineValue = root.value(QStringLiteral("disciplining"));
    if (!disciplineValue.isUndefined()) {
        if (!disciplineValue.isObject()) {
            fail(QStringLiteral("oscillatord disciplining telemetry is invalid"));
            return;
        }
        const QJsonObject discipline = disciplineValue.toObject();
        if (!discipline.value(QStringLiteral("status")).isString()
            || !discipline.value(QStringLiteral("convergence_progress")).isDouble()
            || !discipline.value(QStringLiteral("ready_for_holdover")).isBool()) {
            fail(QStringLiteral("oscillatord disciplining telemetry has invalid types"));
            return;
        }
        m_disciplineAvailable = true;
        m_disciplineStatus = discipline.value(QStringLiteral("status")).toString();
        m_currentConvergenceCount = discipline.value(
            QStringLiteral("current_phase_convergence_count")).toInt(-1);
        m_convergenceThreshold = discipline.value(
            QStringLiteral("valid_phase_convergence_threshold")).toInt(-1);
        m_convergenceProgress = qBound(0.0, discipline.value(
            QStringLiteral("convergence_progress")).toDouble(), 100.0);
        m_readyForHoldover = discipline.value(
            QStringLiteral("ready_for_holdover")).toBool();
    }

    m_available = true;
    m_serviceVersion = root.value(QStringLiteral("version")).toString();
    m_actionRequested = root.value(QStringLiteral("Action requested")).toString();
    m_controlEnabled = root.value(QStringLiteral("control_enabled")).toBool(false);
    m_clockClass = clock.value(QStringLiteral("class")).toString();
    m_clockOffsetNanoseconds = clock.value(QStringLiteral("offset")).toVariant().toLongLong();
    m_oscillatorModel = oscillator.value(QStringLiteral("model")).toString();
    m_fineControl = optionalInteger(oscillator, QStringLiteral("fine_ctrl"), -1);
    m_coarseControl = optionalInteger(oscillator, QStringLiteral("coarse_ctrl"), -1);
    m_oscillatorLocked = oscillator.value(QStringLiteral("lock")).toBool();
    m_oscillatorTemperature = oscillator.value(QStringLiteral("temperature")).toDouble();
    m_gnssFix = gnss.value(QStringLiteral("fix")).toInt(-1);
    m_gnssFixOk = gnss.value(QStringLiteral("fixOk")).toBool();
    m_antennaPower = gnss.value(QStringLiteral("antenna_power")).toInt(-1);
    m_antennaStatus = gnss.value(QStringLiteral("antenna_status")).toInt(-1);
    m_leapSecondChange = gnss.value(QStringLiteral("lsChange")).toInt(-10);
    m_leapSeconds = gnss.value(QStringLiteral("leap_seconds")).toInt(-1);
    m_satellites = gnss.value(QStringLiteral("satellites_count")).toInt(-1);
    m_surveyPositionErrorMeters = gnss.value(
        QStringLiteral("survey_in_position_error")).toDouble(-1.0);
    m_timeAccuracyNanoseconds = optionalInteger(
        gnss, QStringLiteral("time_accuracy"), -1);
    m_error = root.value(QStringLiteral("error")).toString();
    emit updated();
}

void OscillatordClient::fail(const QString &message)
{
    m_timeoutTimer.stop();
    m_available = false;
    m_actionRequested.clear();
    m_controlEnabled = false;
    m_disciplineAvailable = false;
    m_disciplineStatus.clear();
    m_currentConvergenceCount = -1;
    m_convergenceThreshold = -1;
    m_convergenceProgress = 0.0;
    m_readyForHoldover = false;
    m_fineControl = -1;
    m_coarseControl = -1;
    m_gnssFix = -1;
    m_antennaPower = -1;
    m_antennaStatus = -1;
    m_leapSecondChange = -10;
    m_leapSeconds = -1;
    m_satellites = -1;
    m_surveyPositionErrorMeters = -1.0;
    m_timeAccuracyNanoseconds = -1;
    m_error = message;
    emit updated();
}
