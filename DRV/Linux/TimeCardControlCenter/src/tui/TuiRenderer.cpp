#include "TuiRenderer.h"

#include <QtGlobal>

#include <array>
#include <utility>

namespace {

QString available(QString value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("Unavailable") : value.trimmed();
}

QString joined(const QStringList &values)
{
    return values.isEmpty() ? QStringLiteral("Unavailable")
                            : values.join(QStringLiteral(" | "));
}

QString clipped(const QString &value, int width)
{
    if (width <= 0)
        return {};
    if (value.size() <= width)
        return value;
    if (width <= 3)
        return value.left(width);
    return value.left(width - 3) + QStringLiteral("...");
}

void add(TuiFrame *frame, const QString &text, TuiStyle style, int width)
{
    frame->lines.append({clipped(text, width), style});
}

QStringList wrapped(QString value, int width)
{
    width = qMax(1, width);
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    QStringList result;
    const QStringList logicalLines = value.split(QLatin1Char('\n'));
    for (QString remaining : logicalLines) {
        remaining = remaining.trimmed();
        if (remaining.isEmpty()) {
            result.append(QString());
            continue;
        }
        while (remaining.size() > width) {
            int split = width;
            for (int index = width; index > 0; --index) {
                if (remaining.at(index - 1).isSpace()) {
                    split = index - 1;
                    break;
                }
            }
            if (split <= 0)
                split = width;
            result.append(remaining.left(split).trimmed());
            remaining = remaining.mid(split).trimmed();
        }
        result.append(remaining);
    }
    return result;
}

void addWrapped(TuiFrame *frame, const QString &text, TuiStyle style, int width)
{
    const QStringList lines = wrapped(text, width);
    for (const QString &line : lines)
        add(frame, line, style, width);
}

QString keyValue(const QString &key, const QString &value, int width)
{
    constexpr int preferredKeyWidth = 22;
    const int keyWidth = qMin(preferredKeyWidth, qMax(8, width / 3));
    const QString label = (key + QLatin1Char(':')).leftJustified(keyWidth, QLatin1Char(' '));
    return clipped(label + available(value), width);
}

void addKeyValue(
    TuiFrame *frame, const QString &key, const QString &value, int width,
    TuiStyle style = TuiStyle::Normal)
{
    add(frame, keyValue(key, value, width), style, width);
}

void addWrappedKeyValue(
    TuiFrame *frame, const QString &key, const QString &value, int width,
    TuiStyle style = TuiStyle::Normal)
{
    constexpr int preferredKeyWidth = 22;
    const int keyWidth = qMin(preferredKeyWidth, qMax(8, width / 3));
    const QString firstPrefix = key.isEmpty()
        ? QString(keyWidth, QLatin1Char(' '))
        : (key + QLatin1Char(':')).leftJustified(keyWidth, QLatin1Char(' '));
    const QString continuation(keyWidth, QLatin1Char(' '));
    const QStringList chunks = wrapped(available(value), width - keyWidth);
    for (qsizetype index = 0; index < chunks.size(); ++index) {
        add(frame, (index == 0 ? firstPrefix : continuation) + chunks.at(index),
            style, width);
    }
}

void addList(
    TuiFrame *frame, const QString &key, const QStringList &values, int width)
{
    if (values.isEmpty()) {
        addKeyValue(frame, key, QStringLiteral("Unavailable"), width);
        return;
    }
    for (qsizetype index = 0; index < values.size(); ++index) {
        addWrappedKeyValue(frame, index == 0 ? key : QString(), values.at(index), width);
    }
}

void addPair(
    TuiFrame *frame,
    const QString &leftKey,
    const QString &leftValue,
    const QString &rightKey,
    const QString &rightValue,
    int width)
{
    if (width < 100) {
        addKeyValue(frame, leftKey, leftValue, width);
        addKeyValue(frame, rightKey, rightValue, width);
        return;
    }

    const int leftWidth = (width - 3) / 2;
    const int rightWidth = width - leftWidth - 3;
    const QString line = keyValue(leftKey, leftValue, leftWidth).leftJustified(
                             leftWidth, QLatin1Char(' '))
        + QStringLiteral(" | ") + keyValue(rightKey, rightValue, rightWidth);
    add(frame, line, TuiStyle::Normal, width);
}

void addSection(TuiFrame *frame, const QString &title, int width)
{
    if (!frame->lines.isEmpty())
        add(frame, QString(), TuiStyle::Normal, width);
    add(frame, QStringLiteral("[ %1 ]").arg(title), TuiStyle::Accent, width);
}

QString navigation(TuiPage page, int width)
{
    const std::array<std::pair<TuiPage, QString>, 6> pages {{
        {TuiPage::Overview, QStringLiteral("1 Overview")},
        {TuiPage::TimingIo, QStringLiteral("2 Timing I/O")},
        {TuiPage::Sensors, QStringLiteral("3 Sensors")},
        {TuiPage::Gnss, QStringLiteral("4 GNSS")},
        {TuiPage::Oscillatord, QStringLiteral("5 oscillatord")},
        {TuiPage::Help, QStringLiteral("? Help")},
    }};

    QStringList entries;
    for (const auto &[candidate, label] : pages) {
        entries.append(candidate == page ? QStringLiteral("[%1]").arg(label) : label);
    }
    const QString separator = width >= 100 ? QStringLiteral("    ") : QStringLiteral(" | ");
    return entries.join(separator);
}

QString deviceLine(const TuiModel &model, int cursor)
{
    if (model.availableDevices.isEmpty())
        return QStringLiteral("Cards: none discovered");

    QStringList entries;
    for (qsizetype index = 0; index < model.availableDevices.size(); ++index) {
        const QString &device = model.availableDevices.at(index);
        QString entry;
        if (index == cursor)
            entry += QLatin1Char('>');
        if (device == model.selectedDevice)
            entry += QLatin1Char('*');
        entry += device;
        if (index == cursor)
            entry += QLatin1Char('<');
        entries.append(entry);
    }
    return QStringLiteral("Cards: %1    (* active, arrows choose, Enter selects)")
        .arg(entries.join(QLatin1Char(' ')));
}

TuiStyle connectionStyle(const TuiModel &model)
{
    if (!model.error.isEmpty())
        return TuiStyle::Error;
    if (model.offsetValid)
        return TuiStyle::Good;
    if (model.connected)
        return TuiStyle::Warning;
    return TuiStyle::Dim;
}

void renderOverview(TuiFrame *frame, const TuiModel &model, int width)
{
    addSection(frame, QStringLiteral("Card identity"), width);
    addPair(frame, QStringLiteral("Device"), model.selectedDevice,
        QStringLiteral("Serial"), model.serialNumber, width);
    addPair(frame, QStringLiteral("Board profile"), model.boardProfile,
        QStringLiteral("PCI"), model.pciIdentity, width);
    addKeyValue(frame, QStringLiteral("Sysfs"), model.sysfsPath, width);

    addSection(frame, QStringLiteral("Timing summary"), width);
    addPair(frame, QStringLiteral("PHC offset"), model.offsetText,
        QStringLiteral("Sample window"), model.sampleWindowText, width);
    addPair(frame, QStringLiteral("Clock source"), model.clockSource,
        QStringLiteral("GNSS"), model.gnssState, width);
    addKeyValue(frame, QStringLiteral("Timestamp method"), model.timestampMethod, width);

    addSection(frame, QStringLiteral("Capabilities"), width);
    addWrapped(frame, joined(model.capabilities), TuiStyle::Normal, width);

    addSection(frame, QStringLiteral("Diagnostics and session log"), width);
    addKeyValue(frame, QStringLiteral("Log status"), model.sessionLogStatus, width);
    const qsizetype firstLogLine = qMax<qsizetype>(0, model.sessionLog.size() - 3);
    for (qsizetype index = firstLogLine; index < model.sessionLog.size(); ++index)
        addWrapped(frame, model.sessionLog.at(index), TuiStyle::Dim, width);
}

void renderTimingIo(TuiFrame *frame, const TuiModel &model, int width)
{
    addSection(frame, QStringLiteral("Clock telemetry"), width);
    addKeyValue(frame, QStringLiteral("PHC UTC"), model.phcTime, width);
    addKeyValue(frame, QStringLiteral("System UTC"), model.systemTime, width);
    addPair(frame, QStringLiteral("PHC offset"), model.offsetText,
        QStringLiteral("Sample window"), model.sampleWindowText, width);
    addPair(frame, QStringLiteral("Clock offset"), model.clockOffset,
        QStringLiteral("Clock drift"), model.clockDrift, width);
    addPair(frame, QStringLiteral("UTC/TAI offset"), model.utcTaiOffset,
        QStringLiteral("Clock source"), model.clockSource, width);

    addSection(frame, QStringLiteral("Device nodes"), width);
    addPair(frame, QStringLiteral("PTP"), model.ptpDevice,
        QStringLiteral("PPS"), model.ppsDevice, width);
    addPair(frame, QStringLiteral("I2C"), model.i2cDevice,
        QStringLiteral("MRO-50"), model.mro50Device, width);

    addSection(frame, QStringLiteral("Timing I/O inventory"), width);
    addList(frame, QStringLiteral("SMA"), model.smaStates, width);
    addList(frame, QStringLiteral("Generators"), model.generatorStates, width);
    addList(frame, QStringLiteral("Counters"), model.frequencyCounterStates, width);
    addList(frame, QStringLiteral("FPGA status"), model.fpgaEngineStates, width);
}

void renderSensors(TuiFrame *frame, const TuiModel &model, int width)
{
    addSection(frame, QStringLiteral("Board"), width);
    addPair(frame, QStringLiteral("Profile"), model.boardProfile,
        QStringLiteral("Serial"), model.serialNumber, width);
    addWrappedKeyValue(
        frame, QStringLiteral("Optional image"), model.optionalImageContract, width);

    addSection(frame, QStringLiteral("Sensors"), width);
    if (model.sensorStates.isEmpty()) {
        add(frame, QStringLiteral("No sensor telemetry exposed by this card profile."),
            TuiStyle::Dim, width);
    } else {
        for (const QString &sensor : model.sensorStates)
            addWrapped(frame, sensor, TuiStyle::Normal, width);
    }

    addSection(frame, QStringLiteral("LEDs"), width);
    if (model.ledStates.isEmpty()) {
        add(frame, QStringLiteral("No LED telemetry exposed by this card profile."),
            TuiStyle::Dim, width);
    } else {
        for (const QString &led : model.ledStates)
            addWrapped(frame, led, TuiStyle::Normal, width);
    }
}

void renderGnss(TuiFrame *frame, const TuiModel &model, int width)
{
    addSection(frame, QStringLiteral("GNSS status"), width);
    addPair(frame, QStringLiteral("PPS supervisor"), model.gnssState,
        QStringLiteral("PPS sync"), model.gnssLocked ? QStringLiteral("Synchronized")
                                                     : QStringLiteral("Not synchronized"),
        width);
    addPair(frame, QStringLiteral("Clock source"), model.clockSource,
        QStringLiteral("ToD protocol"), model.todProtocol, width);
    addKeyValue(frame, QStringLiteral("ToD baud"), model.todBaudRate, width);

    addSection(frame, QStringLiteral("Serial endpoints"), width);
    addPair(frame, QStringLiteral("GNSS primary"), model.ttyGnss,
        QStringLiteral("GNSS secondary"), model.ttyGnss2, width);
    addPair(frame, QStringLiteral("MAC"), model.ttyMac,
        QStringLiteral("NMEA"), model.ttyNmea, width);
}

void renderOscillatord(TuiFrame *frame, const TuiModel &model, int width)
{
    addSection(frame, QStringLiteral("Service"), width);
    addKeyValue(frame, QStringLiteral("Endpoint"), model.oscillatordEndpoint, width);
    addPair(frame, QStringLiteral("Detected"),
        model.oscillatordAvailable ? QStringLiteral("Yes") : QStringLiteral("No"),
        QStringLiteral("Version"), model.oscillatordVersion, width);
    addPair(frame, QStringLiteral("Discipline API"),
        model.disciplineAvailable ? QStringLiteral("Available") : QStringLiteral("Unavailable"),
        QStringLiteral("Discipline"), model.disciplineStatus, width);
    addKeyValue(frame, QStringLiteral("Progress"), QStringLiteral("%1%").arg(
        qBound(0.0, model.convergenceProgress, 100.0), 0, 'f', 1), width);
    addKeyValue(frame, QStringLiteral("Progress detail"), model.disciplineProgressDetail, width);
    addPair(frame, QStringLiteral("Holdover"), model.holdoverReadiness,
        QStringLiteral("Status action"), model.oscillatordActionRequested, width);

    addSection(frame, QStringLiteral("Telemetry"), width);
    addKeyValue(frame, QStringLiteral("Clock"), model.oscillatordClockSummary, width);
    addKeyValue(frame, QStringLiteral("Oscillator"), model.oscillatorSummary, width);
    addKeyValue(frame, QStringLiteral("Oscillator control"), model.oscillatorControlSummary, width);
    addKeyValue(frame, QStringLiteral("GNSS"), model.oscillatordGnssSummary, width);
    addKeyValue(frame, QStringLiteral("GNSS detail"), model.oscillatordGnssDetail, width);
    addKeyValue(frame, QStringLiteral("Antenna"), model.oscillatordAntennaSummary, width);
    addKeyValue(frame, QStringLiteral("Control policy"), model.oscillatordControlPolicy, width);
    if (!model.oscillatordError.isEmpty())
        addKeyValue(frame, QStringLiteral("Service error"), model.oscillatordError,
            width, TuiStyle::Error);
}

void renderHelp(TuiFrame *frame, int width)
{
    addSection(frame, QStringLiteral("Keyboard"), width);
    add(frame, QStringLiteral("Left/Right or Tab     Change workspace"), TuiStyle::Normal, width);
    add(frame, QStringLiteral("1..5                  Open a workspace"), TuiStyle::Normal, width);
    add(frame, QStringLiteral("Up/Down or j/k        Move through discovered cards"),
        TuiStyle::Normal, width);
    add(frame, QStringLiteral("PageUp/PageDown        Scroll the current workspace"),
        TuiStyle::Normal, width);
    add(frame, QStringLiteral("Enter                 Select highlighted card"), TuiStyle::Normal, width);
    add(frame, QStringLiteral("r                     Refresh Time Card telemetry"),
        TuiStyle::Normal, width);
    add(frame, QStringLiteral("o                     Refresh oscillatord telemetry"),
        TuiStyle::Normal, width);
    add(frame, QStringLiteral("x                     Export the structured session log"),
        TuiStyle::Normal, width);
    add(frame, QStringLiteral("c                     Clear the in-memory session log"),
        TuiStyle::Normal, width);
    add(frame, QStringLiteral("?                     Open this help page"), TuiStyle::Normal, width);
    add(frame, QStringLiteral("q or Escape           Quit and restore the terminal"),
        TuiStyle::Normal, width);

    addSection(frame, QStringLiteral("Non-interactive output"), width);
    add(frame, QStringLiteral("Use --plain for one snapshot without terminal escape sequences."),
        TuiStyle::Dim, width);
    add(frame, QStringLiteral("Recent diagnostics are shown on the Overview workspace."),
        TuiStyle::Dim, width);
}

TuiFrame finish(
    TuiFrame frame, int width, int height, int fixedHeaderLines, int requestedOffset)
{
    width = qMax(1, width);
    for (TuiLine &line : frame.lines)
        line.text = clipped(line.text, width);

    if (height <= 0) {
        frame.lines.append({clipped(
            QStringLiteral("q quit | arrows pages/cards | Enter select | r refresh | x log export | ? help"),
            width), TuiStyle::Dim});
        return frame;
    }

    height = qMax(1, height);
    const int contentLines = qMax(0, height - 1);
    const int fixedLines = qMin(
        qMin(fixedHeaderLines, contentLines), static_cast<int>(frame.lines.size()));
    const int scrollingLines = qMax(0, contentLines - fixedLines);
    const int bodyLines = static_cast<int>(frame.lines.size()) - fixedLines;
    frame.maximumScrollOffset = qMax(0, bodyLines - scrollingLines);
    frame.scrollOffset = qBound(0, requestedOffset, frame.maximumScrollOffset);

    QVector<TuiLine> visible;
    visible.reserve(height);
    for (int index = 0; index < fixedLines; ++index)
        visible.append(frame.lines.at(index));
    const int firstBodyLine = fixedLines + frame.scrollOffset;
    const int lastBodyLine = qMin(
        firstBodyLine + scrollingLines, static_cast<int>(frame.lines.size()));
    for (int index = firstBodyLine; index < lastBodyLine; ++index)
        visible.append(frame.lines.at(index));
    while (visible.size() < static_cast<qsizetype>(contentLines))
        visible.append({QString(), TuiStyle::Normal});

    QString footer = QStringLiteral(
        "q quit | arrows pages/cards | Enter select | r refresh | x log export | ? help");
    if (frame.maximumScrollOffset > 0) {
        footer = QStringLiteral("q quit | arrows pages/cards | PgUp/PgDn scroll %1/%2 | r refresh | ? help")
                     .arg(frame.scrollOffset)
                     .arg(frame.maximumScrollOffset);
    }
    visible.append({clipped(footer, width), TuiStyle::Dim});
    frame.lines = std::move(visible);
    return frame;
}

} // namespace

TuiFrame TuiRenderer::render(
    const TuiModel &model, TuiPage page, int deviceCursor, int width, int height,
    int scrollOffset)
{
    width = qMax(1, width);
    TuiFrame frame;

    if (height > 0 && (width < 48 || height < 12)) {
        add(&frame, QStringLiteral("TIME CARD CONTROL CENTER"), TuiStyle::Header, width);
        add(&frame, QString(), TuiStyle::Normal, width);
        add(&frame, QStringLiteral("Terminal too small."), TuiStyle::Warning, width);
        add(&frame, QStringLiteral("Resize to at least 48 columns by 12 rows."),
            TuiStyle::Dim, width);
        return finish(std::move(frame), width, height, 0, 0);
    }

    const QString sampled = model.lastUpdated.isEmpty()
        ? QStringLiteral("starting telemetry")
        : QStringLiteral("updated %1").arg(model.lastUpdated);
    add(&frame, QStringLiteral("OCP TIME CARD CONTROL CENTER  |  %1  |  %2")
            .arg(pageName(page), sampled),
        TuiStyle::Header, width);
    add(&frame, navigation(page, width), TuiStyle::Accent, width);
    add(&frame, deviceLine(model, deviceCursor), TuiStyle::Accent, width);
    add(&frame, QStringLiteral("%1  |  %2")
            .arg(available(model.connectionState), available(model.backendName)),
        connectionStyle(model), width);

    switch (page) {
    case TuiPage::Overview:
        renderOverview(&frame, model, width);
        break;
    case TuiPage::TimingIo:
        renderTimingIo(&frame, model, width);
        break;
    case TuiPage::Sensors:
        renderSensors(&frame, model, width);
        break;
    case TuiPage::Gnss:
        renderGnss(&frame, model, width);
        break;
    case TuiPage::Oscillatord:
        renderOscillatord(&frame, model, width);
        break;
    case TuiPage::Help:
        renderHelp(&frame, width);
        break;
    }

    if (!model.error.isEmpty()) {
        addSection(&frame, QStringLiteral("Hardware error"), width);
        addWrapped(&frame, model.error, TuiStyle::Error, width);
    }
    return finish(std::move(frame), width, height, 4, scrollOffset);
}

QString TuiRenderer::toPlainText(const TuiFrame &frame)
{
    QStringList lines;
    lines.reserve(frame.lines.size());
    for (const TuiLine &line : frame.lines)
        lines.append(line.text);
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString TuiRenderer::pageName(TuiPage page)
{
    switch (page) {
    case TuiPage::Overview:
        return QStringLiteral("Overview");
    case TuiPage::TimingIo:
        return QStringLiteral("Timing I/O");
    case TuiPage::Sensors:
        return QStringLiteral("Sensors");
    case TuiPage::Gnss:
        return QStringLiteral("GNSS");
    case TuiPage::Oscillatord:
        return QStringLiteral("oscillatord");
    case TuiPage::Help:
        return QStringLiteral("Help");
    }
    return QStringLiteral("Overview");
}

TuiPage TuiRenderer::pageFromName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("timing")
        || normalized == QStringLiteral("timing-io")
        || normalized == QStringLiteral("io")) {
        return TuiPage::TimingIo;
    }
    if (normalized == QStringLiteral("sensors"))
        return TuiPage::Sensors;
    if (normalized == QStringLiteral("gnss"))
        return TuiPage::Gnss;
    if (normalized == QStringLiteral("oscillatord")
        || normalized == QStringLiteral("oscillator")) {
        return TuiPage::Oscillatord;
    }
    if (normalized == QStringLiteral("help"))
        return TuiPage::Help;
    return TuiPage::Overview;
}
