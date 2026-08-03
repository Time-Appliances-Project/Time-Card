#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

enum class SessionLogSeverity {
    Trace,
    Information,
    Warning,
    Error,
};

struct SessionLogRecord {
    QDateTime timestampUtc;
    SessionLogSeverity severity = SessionLogSeverity::Information;
    QString category;
    QString cardContext;
    QString message;

    QString severityLabel() const;
    QString toText() const;
    QJsonObject toJson() const;
};

class SessionLogStore final {
public:
    explicit SessionLogStore(qsizetype capacity = 500);

    qsizetype capacity() const;
    qsizetype count() const;
    quint64 droppedRecordCount() const;

    void append(
        SessionLogSeverity severity,
        const QString &category,
        const QString &cardContext,
        const QString &message,
        const QDateTime &timestampUtc = QDateTime::currentDateTimeUtc());
    void clear();

    QStringList textLines(qsizetype maximum = -1) const;
    QByteArray toJson() const;
    bool writeJson(const QString &path, QString *error = nullptr) const;

private:
    static QString cleanField(const QString &value, const QString &fallback);

    qsizetype m_capacity;
    QVector<SessionLogRecord> m_records;
    quint64 m_droppedRecordCount = 0;
};
