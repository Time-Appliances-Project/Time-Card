#include "TuiRenderer.h"

#include <QtTest>

class TuiRendererTest final : public QObject {
    Q_OBJECT

private slots:
    void rendersAllPages();
    void clipsEveryLine();
    void marksCardCursorAndSelection();
    void rendersSmallTerminalFallback();
    void plainTextHasNoEscapeSequences();
    void rendersOscillatordPercentage();
    void scrollsAllTimingInventoryAt80x24();
    void unboundedPlainFrameKeepsAllTimingInventory();
    void splitsMultilineHardwareErrors();
};

namespace {

TuiModel sampleModel()
{
    TuiModel model;
    model.connected = true;
    model.connectionState = QStringLiteral("TIME CARD READY");
    model.backendName = QStringLiteral("Recorded hardware simulation");
    model.availableDevices = {QStringLiteral("mock0"), QStringLiteral("mock1")};
    model.selectedDevice = QStringLiteral("mock0");
    model.serialNumber = QStringLiteral("02:54:43:00:00:01");
    model.boardProfile = QStringLiteral("Time Card");
    model.pciIdentity = QStringLiteral("0000:03:00.0  0x1d9b:0x0400");
    model.sysfsPath = QStringLiteral("/sys/class/timecard/mock0");
    model.ptpDevice = QStringLiteral("/dev/ptp0");
    model.ppsDevice = QStringLiteral("/dev/pps0");
    model.i2cDevice = QStringLiteral("/dev/i2c-4");
    model.phcTime = QStringLiteral("2026-08-02 12:00:00.000000000 UTC");
    model.systemTime = QStringLiteral("2026-08-02 12:00:00.000000042 UTC");
    model.offsetValid = true;
    model.offsetText = QStringLiteral("+42 ns");
    model.sampleWindowText = QStringLiteral("760 ns");
    model.timestampMethod = QStringLiteral("PTP_SYS_OFFSET_EXTENDED simulation");
    model.clockSource = QStringLiteral("PPS");
    model.gnssState = QStringLiteral("SYNC");
    model.gnssLocked = true;
    model.todProtocol = QStringLiteral("NMEA");
    model.todBaudRate = QStringLiteral("115200");
    model.ttyGnss = QStringLiteral("/dev/ttyS4");
    model.sensorStates = {QStringLiteral("board temperature: 42.0 C")};
    model.ledStates = {QStringLiteral("GNSS: on")};
    model.smaStates = {QStringLiteral("SMA1 | input PPS1"),
        QStringLiteral("SMA4 | output NMEA")};
    model.generatorStates = {
        QStringLiteral("GEN1 | running | period 1000000000 ns | duty 50%"),
        QStringLiteral("GEN2 | stopped | period 10000000 ns | duty 25%"),
        QStringLiteral("GEN3 | stopped | repeat 10"),
        QStringLiteral("GEN4 | running | cable 25 ns | continuous"),
    };
    model.frequencyCounterStates = {
        QStringLiteral("FREQ1 | 10000000 Hz | gate 1 s"),
        QStringLiteral("FREQ2 | disabled | gate 0 s"),
        QStringLiteral("FREQ3 | waiting for sample | gate 1 s"),
        QStringLiteral("FREQ4 | overrun | gate 1 s"),
    };
    model.fpgaEngineStates = {
        QStringLiteral("PPS | external polarity 1 | pulse width 100 ms"),
        QStringLiteral("NMEA output | enabled 1 | baud 115200"),
        QStringLiteral("ToD parser | protocol NMEA | correction 0 s"),
        QStringLiteral("IRIG/DCF | output mode B | input mode B"),
    };
    model.capabilities = {QStringLiteral("PHC"), QStringLiteral("GNSS")};
    model.lastUpdated = QStringLiteral("12:00:00");
    model.oscillatordAvailable = true;
    model.oscillatordVersion = QStringLiteral("oscillatord 1.0");
    model.disciplineStatus = QStringLiteral("tracking");
    model.convergenceProgress = 75.0;
    return model;
}

} // namespace

void TuiRendererTest::rendersAllPages()
{
    const TuiModel model = sampleModel();
    const QList<TuiPage> pages {TuiPage::Overview, TuiPage::TimingIo,
        TuiPage::Sensors, TuiPage::Gnss, TuiPage::Oscillatord, TuiPage::Help};
    for (const TuiPage page : pages) {
        const QString output = TuiRenderer::toPlainText(
            TuiRenderer::render(model, page, 0, 120, 35));
        QVERIFY2(output.contains(TuiRenderer::pageName(page)),
            qPrintable(QStringLiteral("missing page name %1").arg(TuiRenderer::pageName(page))));
    }
}

void TuiRendererTest::clipsEveryLine()
{
    const TuiFrame frame = TuiRenderer::render(sampleModel(), TuiPage::TimingIo, 0, 80, 24);
    QCOMPARE(frame.lines.size(), qsizetype(24));
    for (const TuiLine &line : frame.lines)
        QVERIFY(line.text.size() <= 80);
}

void TuiRendererTest::marksCardCursorAndSelection()
{
    const QString output = TuiRenderer::toPlainText(
        TuiRenderer::render(sampleModel(), TuiPage::Overview, 1, 100, 24));
    QVERIFY(output.contains(QStringLiteral("*mock0")));
    QVERIFY(output.contains(QStringLiteral(">mock1<")));
}

void TuiRendererTest::rendersSmallTerminalFallback()
{
    const TuiFrame frame = TuiRenderer::render(sampleModel(), TuiPage::Overview, 0, 40, 8);
    QCOMPARE(frame.lines.size(), qsizetype(8));
    QVERIFY(TuiRenderer::toPlainText(frame).contains(QStringLiteral("Terminal too small")));
}

void TuiRendererTest::plainTextHasNoEscapeSequences()
{
    const QString output = TuiRenderer::toPlainText(
        TuiRenderer::render(sampleModel(), TuiPage::Sensors, 0, 120, 30));
    QVERIFY(!output.contains(QChar(0x1b)));
}

void TuiRendererTest::rendersOscillatordPercentage()
{
    const QString output = TuiRenderer::toPlainText(
        TuiRenderer::render(sampleModel(), TuiPage::Oscillatord, 0, 120, 30));
    QVERIFY(output.contains(QStringLiteral("75.0%")));
}

void TuiRendererTest::scrollsAllTimingInventoryAt80x24()
{
    const TuiModel model = sampleModel();
    const TuiFrame first = TuiRenderer::render(model, TuiPage::TimingIo, 0, 80, 24);
    QVERIFY(first.maximumScrollOffset > 0);

    QString reachable;
    for (int offset = 0; offset <= first.maximumScrollOffset; ++offset) {
        reachable += TuiRenderer::toPlainText(
            TuiRenderer::render(model, TuiPage::TimingIo, 0, 80, 24, offset));
    }
    QVERIFY(reachable.contains(QStringLiteral("GEN4")));
    QVERIFY(reachable.contains(QStringLiteral("FREQ4")));
    QVERIFY(reachable.contains(QStringLiteral("IRIG/DCF")));
}

void TuiRendererTest::unboundedPlainFrameKeepsAllTimingInventory()
{
    const QString output = TuiRenderer::toPlainText(
        TuiRenderer::render(sampleModel(), TuiPage::TimingIo, 0, 120, 0));
    QVERIFY(output.contains(QStringLiteral("GEN4")));
    QVERIFY(output.contains(QStringLiteral("FREQ4")));
    QVERIFY(output.contains(QStringLiteral("IRIG/DCF")));
}

void TuiRendererTest::splitsMultilineHardwareErrors()
{
    TuiModel model = sampleModel();
    model.error = QStringLiteral("first fault\nsecond fault");
    const TuiFrame frame = TuiRenderer::render(model, TuiPage::Overview, 0, 80, 0);
    for (const TuiLine &line : frame.lines)
        QVERIFY(!line.text.contains(QLatin1Char('\n')));
    const QString output = TuiRenderer::toPlainText(frame);
    QVERIFY(output.contains(QStringLiteral("first fault")));
    QVERIFY(output.contains(QStringLiteral("second fault")));
}

QTEST_APPLESS_MAIN(TuiRendererTest)

#include "tst_tuirenderer.moc"
