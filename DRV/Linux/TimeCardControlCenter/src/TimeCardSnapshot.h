#pragma once

#include <QString>
#include <QStringList>

struct TimeCardSnapshot {
    bool connected = false;
    QString backendName;
    QString deviceId;
    QString sysfsPath;
    QString ptpDevice;
    QString ppsDevice;
    QString i2cDevice;
    QString mro50Device;
    QString pciAddress;
    QString pciVendor;
    QString pciDevice;
    QString serialNumber;
    QString boardProfile;

    QString clockSource;
    QString gnssState;
    bool gnssLocked = false;
    QString todProtocol;
    QString todBaudRate;

    QString ttyGnss;
    QString ttyGnss2;
    QString ttyMac;
    QString ttyNmea;

    bool cardUtcTaiOffsetValid = false;
    int cardUtcTaiOffsetSeconds = 0;
    bool utcTaiOffsetValid = false;
    int utcTaiOffsetSeconds = 0;
    bool utcTaiOffsetFromKernel = false;
    bool clockDriftPpbValid = false;
    qint64 clockDriftPartsPerBillion = 0;
    bool r4006TopologyDetected = false;
    bool clockOffsetValid = false;
    qint64 clockOffsetNanoseconds = 0;

    bool timingValid = false;
    qint64 phcTaiNanoseconds = 0;
    qint64 phcUtcNanoseconds = 0;
    qint64 systemUtcNanoseconds = 0;
    bool offsetValid = false;
    qint64 offsetNanoseconds = 0;
    bool sampleWindowValid = false;
    qint64 sampleWindowNanoseconds = 0;
    QString timestampMethod;

    QStringList availableDevices;
    QStringList capabilities;
    QStringList smaStates;
    QStringList generatorStates;
    QStringList frequencyCounterStates;
    QStringList fpgaEngineStates;
    QStringList sensorStates;
    QStringList ledStates;
    QString optionalImageContract;
    QString error;
};
