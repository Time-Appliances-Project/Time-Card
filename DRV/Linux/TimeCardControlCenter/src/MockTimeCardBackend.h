#pragma once

#include "TimeCardBackend.h"

#include <QElapsedTimer>

class MockTimeCardBackend final : public TimeCardBackend {
public:
    MockTimeCardBackend();

    QString backendName() const override;
    QString selectedDevice() const override;
    void setSelectedDevice(const QString &deviceId) override;
    QStringList availableDevices() const override;
    TimeCardSnapshot readSnapshot() override;

private:
    QElapsedTimer m_elapsed;
    QString m_selectedDevice = QStringLiteral("mock0");
};
