#pragma once

#include "TuiRenderer.h"
#include "UnixSignalBridge.h"

#include <QObject>
#include <QTimer>

class AppController;
class NcursesTerminal;
class QSocketNotifier;
struct TuiKey;

class TuiApplication final : public QObject {
    Q_OBJECT

public:
    TuiApplication(
        AppController &controller,
        NcursesTerminal *terminal,
        bool plain,
        TuiPage initialPage,
        QObject *parent = nullptr);

    bool start(QString *error = nullptr);

private:
    TuiModel collectModel() const;
    void scheduleRender();
    void renderNow();
    void printPlain(const TuiModel &model, bool quitAfterward);
    void drainInput();
    void handleKey(const TuiKey &key);
    void movePage(int delta);
    void moveScroll(int pages);
    void moveDevice(int delta);
    void selectDevice();
    void syncDeviceCursor(const TuiModel &model);

    AppController &m_controller;
    NcursesTerminal *m_terminal = nullptr;
    bool m_plain = false;
    bool m_plainPrinted = false;
    TuiPage m_page = TuiPage::Overview;
    TuiPage m_pageBeforeHelp = TuiPage::Overview;
    int m_deviceCursor = -1;
    int m_scrollOffset = 0;
    bool m_deviceCursorInitialized = false;
    QTimer m_renderTimer;
    QTimer m_plainFallbackTimer;
    QSocketNotifier *m_inputNotifier = nullptr;
    UnixSignalBridge m_signalBridge;
};
