#include "UnixSignalBridge.h"

#include <QSocketNotifier>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t signalWriteFd = -1;

void signalHandler(int signalNumber)
{
    const int savedErrno = errno;
    const unsigned char byte = static_cast<unsigned char>(signalNumber);
    if (signalWriteFd >= 0) {
        const ssize_t bytesWritten =
            ::write(static_cast<int>(signalWriteFd), &byte, sizeof(byte));
        (void)bytesWritten;
    }
    errno = savedErrno;
}

bool makeNonBlocking(int descriptor)
{
    const int flags = fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace

UnixSignalBridge::UnixSignalBridge(QObject *parent)
    : QObject(parent)
{
}

UnixSignalBridge::~UnixSignalBridge()
{
    if (m_handlersInstalled) {
        sigaction(SIGINT, &m_oldInterrupt, nullptr);
        sigaction(SIGTERM, &m_oldTerminate, nullptr);
        sigaction(SIGWINCH, &m_oldWindowChange, nullptr);
    }
    if (signalWriteFd == m_writeFd)
        signalWriteFd = -1;
    delete m_notifier;
    if (m_readFd >= 0)
        ::close(m_readFd);
    if (m_writeFd >= 0)
        ::close(m_writeFd);
}

bool UnixSignalBridge::start()
{
    if (m_notifier)
        return true;

    int descriptors[2] {-1, -1};
    if (::pipe(descriptors) != 0)
        return false;
    m_readFd = descriptors[0];
    m_writeFd = descriptors[1];
    if (!makeNonBlocking(m_readFd) || !makeNonBlocking(m_writeFd))
        return false;

    struct sigaction action {};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    signalWriteFd = m_writeFd;
    if (sigaction(SIGINT, &action, &m_oldInterrupt) != 0) {
        signalWriteFd = -1;
        return false;
    }
    if (sigaction(SIGTERM, &action, &m_oldTerminate) != 0) {
        sigaction(SIGINT, &m_oldInterrupt, nullptr);
        signalWriteFd = -1;
        return false;
    }
    if (sigaction(SIGWINCH, &action, &m_oldWindowChange) != 0) {
        sigaction(SIGTERM, &m_oldTerminate, nullptr);
        sigaction(SIGINT, &m_oldInterrupt, nullptr);
        signalWriteFd = -1;
        return false;
    }
    m_handlersInstalled = true;

    m_notifier = new QSocketNotifier(m_readFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated,
        this, [this] { drain(); });
    return true;
}

void UnixSignalBridge::drain()
{
    if (!m_notifier)
        return;
    m_notifier->setEnabled(false);
    unsigned char buffer[32];
    bool terminate = false;
    bool resize = false;
    ssize_t count = 0;
    while ((count = ::read(m_readFd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t index = 0; index < count; ++index) {
            terminate = terminate || buffer[index] == SIGINT || buffer[index] == SIGTERM;
            resize = resize || buffer[index] == SIGWINCH;
        }
    }
    m_notifier->setEnabled(true);
    if (resize)
        emit resizeRequested();
    if (terminate)
        emit terminationRequested();
}
