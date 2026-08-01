#pragma once

#include "TimeCardSnapshot.h"

class TimeCardBackend {
public:
    virtual ~TimeCardBackend() = default;

    virtual QString backendName() const = 0;
    virtual QString selectedDevice() const = 0;
    virtual void setSelectedDevice(const QString &deviceId) = 0;
    virtual QStringList availableDevices() const = 0;
    virtual TimeCardSnapshot readSnapshot() = 0;
};
