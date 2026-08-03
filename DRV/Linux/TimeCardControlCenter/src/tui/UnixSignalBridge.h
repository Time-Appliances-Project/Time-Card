#pragma once

#include <QObject>

#include <signal.h>

class QSocketNotifier;

class UnixSignalBridge final : public QObject {
    Q_OBJECT

public:
    explicit UnixSignalBridge(QObject *parent = nullptr);
    ~UnixSignalBridge() override;

    bool start();

signals:
    void terminationRequested();
    void resizeRequested();

private:
    void drain();

    QSocketNotifier *m_notifier = nullptr;
    int m_readFd = -1;
    int m_writeFd = -1;
    struct sigaction m_oldInterrupt {};
    struct sigaction m_oldTerminate {};
    struct sigaction m_oldWindowChange {};
    bool m_handlersInstalled = false;
};
