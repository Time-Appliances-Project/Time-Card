#include "SessionLog.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <stdexcept>

QString SessionLogRecord::severityLabel() const
{
    switch (severity) {
    case SessionLogSeverity::Trace:
        return QStringLiteral("TRACE");
    case SessionLogSeverity::Warning:
        return QStringLiteral("WARN");
    case SessionLogSeverity::Error:
        return QStringLiteral("ERROR");
    case SessionLogSeverity::Information:
        return QStringLiteral("INFO");
    }
    return QStringLiteral("INFO");
}

QString SessionLogRecord::toText() const
{
    return QStringLiteral("%1 [%2] [%3] [%4] %5")
        .arg(timestampUtc.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ")),
            severityLabel(), category, cardContext, message);
}

QJsonObject SessionLogRecord::toJson() const
{
    return {
        {QStringLiteral("timestampUtc"),
            timestampUtc.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"))},
        {QStringLiteral("severity"), severityLabel()},
        {QStringLiteral("category"), category},
        {QStringLiteral("cardContext"), cardContext},
        {QStringLiteral("message"), message},
    };
}

SessionLogStore::SessionLogStore(qsizetype capacity)
    : m_capacity(capacity)
{
    if (capacity < 1)
        throw std::invalid_argument("Session log capacity must be positive");
    m_records.reserve(capacity);
}

qsizetype SessionLogStore::capacity() const { return m_capacity; }
qsizetype SessionLogStore::count() const { return m_records.size(); }
quint64 SessionLogStore::droppedRecordCount() const { return m_droppedRecordCount; }

void SessionLogStore::append(
    SessionLogSeverity severity,
    const QString &category,
    const QString &cardContext,
    const QString &message,
    const QDateTime &timestampUtc)
{
    if (m_records.size() == m_capacity) {
        m_records.removeFirst();
        ++m_droppedRecordCount;
    }
    m_records.append({
        timestampUtc.toUTC(),
        severity,
        cleanField(category, QStringLiteral("Application")),
        cleanField(cardContext, QStringLiteral("offline")),
        cleanField(message, QStringLiteral("(empty message)")),
    });
}

void SessionLogStore::clear()
{
    m_records.clear();
    m_droppedRecordCount = 0;
}

QStringList SessionLogStore::textLines(qsizetype maximum) const
{
    const qsizetype first = maximum < 0 || maximum >= m_records.size()
        ? 0 : m_records.size() - maximum;
    QStringList result;
    result.reserve(m_records.size() - first);
    for (qsizetype index = first; index < m_records.size(); ++index)
        result.append(m_records.at(index).toText());
    return result;
}

QByteArray SessionLogStore::toJson() const
{
    QJsonArray records;
    for (const SessionLogRecord &record : m_records)
        records.append(record.toJson());
    const QJsonObject root {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("exportedAtUtc"),
            QDateTime::currentDateTimeUtc().toString(
                QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"))},
        {QStringLiteral("capacity"), static_cast<qint64>(m_capacity)},
        {QStringLiteral("retainedRecordCount"), static_cast<qint64>(m_records.size())},
        {QStringLiteral("droppedRecordCount"),
            static_cast<qint64>(m_droppedRecordCount)},
        {QStringLiteral("records"), records},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool SessionLogStore::writeJson(const QString &path, QString *error) const
{
    QSaveFile file(QFileInfo(path).absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray contents = toJson();
    if (file.write(contents) != contents.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

QString SessionLogStore::cleanField(const QString &value, const QString &fallback)
{
    QString cleaned = value.trimmed();
    if (cleaned.isEmpty())
        cleaned = fallback;
    cleaned.replace(QStringLiteral("\r\n"), QStringLiteral(" | "));
    cleaned.replace(QLatin1Char('\r'), QLatin1Char(' '));
    cleaned.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return cleaned;
}
