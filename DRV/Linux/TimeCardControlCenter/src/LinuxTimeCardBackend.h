#pragma once

#include "TimeCardBackend.h"

#include <QElapsedTimer>

class LinuxTimeCardBackend final : public TimeCardBackend {
public:
    explicit LinuxTimeCardBackend(QString sysfsRoot = QStringLiteral("/sys/class/timecard"));

    QString backendName() const override;
    QString selectedDevice() const override;
    void setSelectedDevice(const QString &deviceId) override;
    QStringList availableDevices() const override;
    TimeCardSnapshot readSnapshot() override;

    QString sysfsRoot() const;

    static QString readTextFile(const QString &path);
    static QString resolveDeviceNode(const QString &entryPath);

private:
    static bool readInteger(const QString &path, qint64 *value);
    static qint64 currentSystemUtcNanoseconds();
    static void discoverPciIdentity(const QString &cardPath, TimeCardSnapshot *snapshot);
    static void samplePhc(TimeCardSnapshot *snapshot);
    static void addCapabilityIfPresent(
        const QString &cardPath, const QString &relativePath,
        const QString &capability, QStringList *capabilities);

    void invalidateIdentityCache();
    void refreshIdentityCache();
    QString ttyDevice(const QString &cardPath, const QString &name) const;
    QString cardPath(const QString &deviceId) const;

    QString m_sysfsRoot;
    QString m_selectedDevice;
    QString m_cachedIdentityDevice;
    TimeCardSnapshot m_cachedIdentity;
    QElapsedTimer m_identityAge;
};
