#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

enum class TuiPage {
    Overview,
    TimingIo,
    Sensors,
    Gnss,
    Oscillatord,
    Help
};

enum class TuiStyle {
    Normal,
    Header,
    Accent,
    Good,
    Warning,
    Error,
    Dim
};

struct TuiLine {
    QString text;
    TuiStyle style = TuiStyle::Normal;
};

struct TuiFrame {
    QVector<TuiLine> lines;
    int scrollOffset = 0;
    int maximumScrollOffset = 0;
};

struct TuiModel {
    bool connected = false;
    QString connectionState;
    QString backendName;
    QStringList availableDevices;
    QString selectedDevice;
    QString serialNumber;
    QString boardProfile;
    QString pciIdentity;
    QString sysfsPath;
    QString ptpDevice;
    QString ppsDevice;
    QString i2cDevice;
    QString mro50Device;

    QString phcTime;
    QString systemTime;
    bool timingValid = false;
    bool offsetValid = false;
    bool sampleWindowValid = false;
    QString offsetText;
    QString sampleWindowText;
    QString timestampMethod;
    QString clockSource;
    QString utcTaiOffset;
    QString clockDrift;
    QString clockOffset;

    QString gnssState;
    bool gnssLocked = false;
    QString todProtocol;
    QString todBaudRate;
    QString ttyGnss;
    QString ttyGnss2;
    QString ttyMac;
    QString ttyNmea;

    QStringList capabilities;
    QStringList smaStates;
    QStringList generatorStates;
    QStringList frequencyCounterStates;
    QStringList fpgaEngineStates;
    QStringList sensorStates;
    QStringList ledStates;
    QString optionalImageContract;

    QString error;
    QString lastUpdated;
    QStringList sessionLog;
    QString sessionLogStatus;

    bool oscillatordObserved = false;
    bool oscillatordAvailable = false;
    QString oscillatordEndpoint;
    QString oscillatordVersion;
    QString oscillatordActionRequested;
    bool disciplineAvailable = false;
    QString disciplineStatus;
    QString disciplineProgressDetail;
    QString holdoverReadiness;
    double convergenceProgress = 0.0;
    QString oscillatordClockSummary;
    QString oscillatorSummary;
    QString oscillatorControlSummary;
    QString oscillatordGnssSummary;
    QString oscillatordGnssDetail;
    QString oscillatordAntennaSummary;
    QString oscillatordControlPolicy;
    QString oscillatordError;
};

class TuiRenderer final {
public:
    static TuiFrame render(
        const TuiModel &model, TuiPage page, int deviceCursor, int width, int height,
        int scrollOffset = 0);
    static QString toPlainText(const TuiFrame &frame);
    static QString pageName(TuiPage page);
    static TuiPage pageFromName(const QString &name);
};
