#include "TuiApplication.h"

#include "AppController.h"
#include "NcursesTerminal.h"

#include <QCoreApplication>
#include <QSocketNotifier>
#include <QTextStream>

#include <unistd.h>

namespace {

constexpr int pageCount = 6;

int pageIndex(TuiPage page)
{
    return static_cast<int>(page);
}

TuiPage pageAt(int index)
{
    const int normalized = (index % pageCount + pageCount) % pageCount;
    return static_cast<TuiPage>(normalized);
}

} // namespace

TuiApplication::TuiApplication(
    AppController &controller,
    NcursesTerminal *terminal,
    bool plain,
    TuiPage initialPage,
    QObject *parent)
    : QObject(parent),
      m_controller(controller),
      m_terminal(terminal),
      m_plain(plain),
      m_page(initialPage),
      m_pageBeforeHelp(initialPage),
      m_signalBridge(this)
{
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(0);
    connect(&m_renderTimer, &QTimer::timeout, this, &TuiApplication::renderNow);
    connect(&m_controller, &AppController::snapshotChanged,
        this, &TuiApplication::scheduleRender);

    m_plainFallbackTimer.setSingleShot(true);
    m_plainFallbackTimer.setInterval(2000);
    connect(&m_plainFallbackTimer, &QTimer::timeout, this, [this] {
        if (!m_plainPrinted)
            printPlain(collectModel(), true);
    });
}

bool TuiApplication::start(QString *error)
{
    if (m_plain) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, [this] {
                if (!m_plainPrinted)
                    printPlain(collectModel(), false);
            });
        m_plainFallbackTimer.start();
        scheduleRender();
        return true;
    }

    connect(&m_signalBridge, &UnixSignalBridge::terminationRequested,
        QCoreApplication::instance(), &QCoreApplication::quit);
    connect(&m_signalBridge, &UnixSignalBridge::resizeRequested, this, [this] {
        if (m_terminal)
            m_terminal->handleResize();
        scheduleRender();
    });
    if (!m_signalBridge.start()) {
        if (error)
            *error = QStringLiteral("could not install the terminal signal bridge");
        return false;
    }

    m_inputNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(m_inputNotifier, &QSocketNotifier::activated,
        this, [this] { drainInput(); });
    scheduleRender();
    return true;
}

TuiModel TuiApplication::collectModel() const
{
    TuiModel model;
    model.connected = m_controller.connected();
    model.connectionState = m_controller.connectionState();
    model.backendName = m_controller.backendName();
    model.availableDevices = m_controller.availableDevices();
    model.selectedDevice = m_controller.selectedDevice();
    model.serialNumber = m_controller.serialNumber();
    model.boardProfile = m_controller.boardProfile();
    model.pciIdentity = m_controller.pciIdentity();
    model.sysfsPath = m_controller.sysfsPath();
    model.ptpDevice = m_controller.ptpDevice();
    model.ppsDevice = m_controller.ppsDevice();
    model.i2cDevice = m_controller.i2cDevice();
    model.mro50Device = m_controller.mro50Device();

    model.phcTime = m_controller.phcTime();
    model.systemTime = m_controller.systemTime();
    model.timingValid = m_controller.timingValid();
    model.offsetValid = m_controller.offsetValid();
    model.sampleWindowValid = m_controller.sampleWindowValid();
    model.offsetText = m_controller.offsetText();
    model.sampleWindowText = m_controller.sampleWindowText();
    model.timestampMethod = m_controller.timestampMethod();
    model.clockSource = m_controller.clockSource();
    model.utcTaiOffset = m_controller.utcTaiOffset();
    model.clockDrift = m_controller.clockDrift();
    model.clockOffset = m_controller.clockOffset();

    model.gnssState = m_controller.gnssState();
    model.gnssLocked = m_controller.gnssLocked();
    model.todProtocol = m_controller.todProtocol();
    model.todBaudRate = m_controller.todBaudRate();
    model.ttyGnss = m_controller.ttyGnss();
    model.ttyGnss2 = m_controller.ttyGnss2();
    model.ttyMac = m_controller.ttyMac();
    model.ttyNmea = m_controller.ttyNmea();

    model.capabilities = m_controller.capabilities();
    model.smaStates = m_controller.smaStates();
    model.generatorStates = m_controller.generatorStates();
    model.frequencyCounterStates = m_controller.frequencyCounterStates();
    model.fpgaEngineStates = m_controller.fpgaEngineStates();
    model.sensorStates = m_controller.sensorStates();
    model.ledStates = m_controller.ledStates();
    model.optionalImageContract = m_controller.optionalImageContract();

    model.error = m_controller.error();
    model.lastUpdated = m_controller.lastUpdated();
    model.sessionLog = m_controller.sessionLog();
    model.sessionLogStatus = m_controller.sessionLogStatus();

    model.oscillatordObserved = m_controller.oscillatordObserved();
    model.oscillatordAvailable = m_controller.oscillatordAvailable();
    model.oscillatordEndpoint = m_controller.oscillatordEndpoint();
    model.oscillatordVersion = m_controller.oscillatordVersion();
    model.oscillatordActionRequested = m_controller.oscillatordActionRequested();
    model.disciplineAvailable = m_controller.disciplineAvailable();
    model.disciplineStatus = m_controller.disciplineStatus();
    model.disciplineProgressDetail = m_controller.disciplineProgressDetail();
    model.holdoverReadiness = m_controller.holdoverReadiness();
    model.convergenceProgress = m_controller.convergenceProgress();
    model.oscillatordClockSummary = m_controller.oscillatordClockSummary();
    model.oscillatorSummary = m_controller.oscillatorSummary();
    model.oscillatorControlSummary = m_controller.oscillatorControlSummary();
    model.oscillatordGnssSummary = m_controller.oscillatordGnssSummary();
    model.oscillatordGnssDetail = m_controller.oscillatordGnssDetail();
    model.oscillatordAntennaSummary = m_controller.oscillatordAntennaSummary();
    model.oscillatordControlPolicy = m_controller.oscillatordControlPolicy();
    model.oscillatordError = m_controller.oscillatordError();
    return model;
}

void TuiApplication::scheduleRender()
{
    if (!m_renderTimer.isActive())
        m_renderTimer.start();
}

void TuiApplication::renderNow()
{
    const TuiModel model = collectModel();
    syncDeviceCursor(model);
    if (m_plain) {
        const bool cardReady = !model.lastUpdated.isEmpty();
        const bool serviceReady = m_page != TuiPage::Oscillatord
            || model.oscillatordObserved;
        if (!m_plainPrinted && cardReady && serviceReady)
            printPlain(model, true);
        return;
    }
    if (!m_terminal)
        return;
    const TuiFrame frame = TuiRenderer::render(model, m_page, m_deviceCursor,
        m_terminal->width(), m_terminal->height(), m_scrollOffset);
    m_scrollOffset = frame.scrollOffset;
    m_terminal->render(frame);
}

void TuiApplication::printPlain(const TuiModel &model, bool quitAfterward)
{
    if (m_plainPrinted)
        return;
    m_plainPrinted = true;
    syncDeviceCursor(model);
    QTextStream output(stdout, QIODevice::WriteOnly);
    output << TuiRenderer::toPlainText(
        TuiRenderer::render(model, m_page, m_deviceCursor, 120, 0));
    output.flush();
    if (quitAfterward)
        QCoreApplication::quit();
}

void TuiApplication::drainInput()
{
    if (!m_inputNotifier || !m_terminal)
        return;
    m_inputNotifier->setEnabled(false);
    while (const std::optional<TuiKey> key = m_terminal->readKey())
        handleKey(*key);
    m_inputNotifier->setEnabled(true);
}

void TuiApplication::handleKey(const TuiKey &key)
{
    switch (key.kind) {
    case TuiKeyKind::Left:
    case TuiKeyKind::PagePrevious:
        movePage(-1);
        return;
    case TuiKeyKind::Right:
    case TuiKeyKind::PageNext:
        movePage(1);
        return;
    case TuiKeyKind::ScrollUp:
        moveScroll(-1);
        return;
    case TuiKeyKind::ScrollDown:
        moveScroll(1);
        return;
    case TuiKeyKind::Up:
        moveDevice(-1);
        return;
    case TuiKeyKind::Down:
        moveDevice(1);
        return;
    case TuiKeyKind::Enter:
        selectDevice();
        return;
    case TuiKeyKind::Escape:
        QCoreApplication::quit();
        return;
    case TuiKeyKind::Resize:
        scheduleRender();
        return;
    case TuiKeyKind::Character:
        break;
    }

    const char32_t character = key.character >= U'A' && key.character <= U'Z'
        ? key.character + (U'a' - U'A') : key.character;
    switch (character) {
    case U'q':
        QCoreApplication::quit();
        break;
    case U'r':
        m_controller.refresh();
        scheduleRender();
        break;
    case U'o':
        m_controller.refreshOscillatord();
        scheduleRender();
        break;
    case U'x':
        m_controller.exportSessionLogToDocuments();
        scheduleRender();
        break;
    case U'c':
        m_controller.clearSessionLog();
        scheduleRender();
        break;
    case U'j':
        moveDevice(1);
        break;
    case U'k':
        moveDevice(-1);
        break;
    case U'h':
        movePage(-1);
        break;
    case U'l':
        movePage(1);
        break;
    case U'?':
        if (m_page == TuiPage::Help) {
            m_page = m_pageBeforeHelp;
        } else {
            m_pageBeforeHelp = m_page;
            m_page = TuiPage::Help;
        }
        m_scrollOffset = 0;
        scheduleRender();
        break;
    case U'1':
        m_page = TuiPage::Overview;
        m_scrollOffset = 0;
        scheduleRender();
        break;
    case U'2':
        m_page = TuiPage::TimingIo;
        m_scrollOffset = 0;
        scheduleRender();
        break;
    case U'3':
        m_page = TuiPage::Sensors;
        m_scrollOffset = 0;
        scheduleRender();
        break;
    case U'4':
        m_page = TuiPage::Gnss;
        m_scrollOffset = 0;
        scheduleRender();
        break;
    case U'5':
        m_page = TuiPage::Oscillatord;
        m_scrollOffset = 0;
        scheduleRender();
        break;
    default:
        break;
    }
}

void TuiApplication::movePage(int delta)
{
    m_page = pageAt(pageIndex(m_page) + delta);
    if (m_page != TuiPage::Help)
        m_pageBeforeHelp = m_page;
    m_scrollOffset = 0;
    scheduleRender();
}

void TuiApplication::moveScroll(int pages)
{
    const int visibleRows = m_terminal ? m_terminal->height() : 24;
    const int pageSize = qMax(1, visibleRows - 6);
    m_scrollOffset = qMax(0, m_scrollOffset + pages * pageSize);
    scheduleRender();
}

void TuiApplication::moveDevice(int delta)
{
    const QStringList devices = m_controller.availableDevices();
    const int deviceCount = static_cast<int>(devices.size());
    if (devices.isEmpty()) {
        m_deviceCursor = -1;
        m_deviceCursorInitialized = true;
        scheduleRender();
        return;
    }
    if (!m_deviceCursorInitialized || m_deviceCursor < 0
        || m_deviceCursor >= deviceCount) {
        const qsizetype selected = devices.indexOf(m_controller.selectedDevice());
        m_deviceCursor = selected >= 0 ? static_cast<int>(selected) : 0;
    }
    m_deviceCursor = (m_deviceCursor + delta) % deviceCount;
    if (m_deviceCursor < 0)
        m_deviceCursor += deviceCount;
    m_deviceCursorInitialized = true;
    scheduleRender();
}

void TuiApplication::selectDevice()
{
    const QStringList devices = m_controller.availableDevices();
    if (m_deviceCursor < 0 || m_deviceCursor >= static_cast<int>(devices.size()))
        return;
    m_controller.setSelectedDevice(devices.at(m_deviceCursor));
    scheduleRender();
}

void TuiApplication::syncDeviceCursor(const TuiModel &model)
{
    if (model.availableDevices.isEmpty()) {
        m_deviceCursor = -1;
        m_deviceCursorInitialized = true;
        return;
    }
    if (m_deviceCursorInitialized && m_deviceCursor >= 0
        && m_deviceCursor < static_cast<int>(model.availableDevices.size())) {
        return;
    }
    const qsizetype selected = model.availableDevices.indexOf(model.selectedDevice);
    m_deviceCursor = selected >= 0 ? static_cast<int>(selected) : 0;
    m_deviceCursorInitialized = true;
}
