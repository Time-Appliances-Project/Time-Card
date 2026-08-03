#include "AppController.h"
#include "LinuxTimeCardBackend.h"
#include "MockTimeCardBackend.h"
#include "NcursesTerminal.h"
#include "TuiApplication.h"
#include "TuiRenderer.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("timecard-control-center-tui"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Open Time Server"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Qt Core terminal dashboard for the OCP Time Card"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption mockOption(
        QStringLiteral("mock"), QStringLiteral("Use the built-in hardware simulation."));
    const QCommandLineOption sysfsRootOption(
        QStringLiteral("sysfs-root"),
        QStringLiteral("Override /sys/class/timecard for testing."),
        QStringLiteral("path"),
        qEnvironmentVariable("TIMECARD_SYSFS_ROOT", QStringLiteral("/sys/class/timecard")));
    const QCommandLineOption hwmonRootOption(
        QStringLiteral("hwmon-root"),
        QStringLiteral("Override /sys/class/hwmon for testing."),
        QStringLiteral("path"),
        qEnvironmentVariable("TIMECARD_HWMON_ROOT", QStringLiteral("/sys/class/hwmon")));
    const QCommandLineOption iioRootOption(
        QStringLiteral("iio-root"),
        QStringLiteral("Override /sys/bus/iio/devices for testing."),
        QStringLiteral("path"),
        qEnvironmentVariable("TIMECARD_IIO_ROOT", QStringLiteral("/sys/bus/iio/devices")));
    const QCommandLineOption ledsRootOption(
        QStringLiteral("leds-root"),
        QStringLiteral("Override /sys/class/leds for testing."),
        QStringLiteral("path"),
        qEnvironmentVariable("TIMECARD_LEDS_ROOT", QStringLiteral("/sys/class/leds")));
    const QCommandLineOption oscillatordHostOption(
        QStringLiteral("oscillatord-host"),
        QStringLiteral("oscillatord monitoring host."),
        QStringLiteral("host"),
        qEnvironmentVariable("TIMECARD_OSCILLATORD_HOST", QStringLiteral("127.0.0.1")));
    const QCommandLineOption oscillatordPortOption(
        QStringLiteral("oscillatord-port"),
        QStringLiteral("oscillatord monitoring port."),
        QStringLiteral("port"),
        qEnvironmentVariable("TIMECARD_OSCILLATORD_PORT", QStringLiteral("2958")));
    const QCommandLineOption plainOption(
        QStringLiteral("plain"),
        QStringLiteral("Print one snapshot without terminal control sequences and exit."));
    const QCommandLineOption pageOption(
        QStringLiteral("page"),
        QStringLiteral("Open overview, timing-io, sensors, gnss, oscillatord, or help."),
        QStringLiteral("name"),
        QStringLiteral("overview"));
    const QCommandLineOption quitAfterOption(
        QStringLiteral("quit-after"),
        QStringLiteral("Exit after the specified number of milliseconds."),
        QStringLiteral("milliseconds"));

    parser.addOptions({mockOption, sysfsRootOption, hwmonRootOption, iioRootOption,
        ledsRootOption, oscillatordHostOption, oscillatordPortOption, plainOption,
        pageOption, quitAfterOption});
    parser.process(application);

    bool portOk = false;
    const int portValue = parser.value(oscillatordPortOption).toInt(&portOk);
    if (!portOk || portValue <= 0 || portValue > 65535) {
        QTextStream(stderr) << "Invalid --oscillatord-port; expected 1 through 65535.\n";
        return 2;
    }

    int quitAfter = -1;
    if (parser.isSet(quitAfterOption)) {
        bool timeoutOk = false;
        quitAfter = parser.value(quitAfterOption).toInt(&timeoutOk);
        if (!timeoutOk || quitAfter < 0) {
            QTextStream(stderr)
                << "Invalid --quit-after; expected a non-negative number of milliseconds.\n";
            return 2;
        }
    }

    const QString requestedPage = parser.value(pageOption).trimmed().toLower();
    const QStringList validPages {
        QStringLiteral("overview"), QStringLiteral("timing"),
        QStringLiteral("timing-io"), QStringLiteral("io"),
        QStringLiteral("sensors"), QStringLiteral("gnss"),
        QStringLiteral("oscillatord"), QStringLiteral("oscillator"),
        QStringLiteral("help"),
    };
    if (!validPages.contains(requestedPage)) {
        QTextStream(stderr)
            << "Invalid --page; expected overview, timing-io, sensors, gnss, oscillatord, or help.\n";
        return 2;
    }

    std::unique_ptr<TimeCardBackend> backend;
    if (parser.isSet(mockOption)) {
        backend = std::make_unique<MockTimeCardBackend>();
    } else {
        backend = std::make_unique<LinuxTimeCardBackend>(
            parser.value(sysfsRootOption), parser.value(hwmonRootOption),
            parser.value(iioRootOption), parser.value(ledsRootOption));
    }

    const bool plain = parser.isSet(plainOption);
    NcursesTerminal terminal;
    if (!plain) {
        QString terminalError;
        if (!terminal.initialize(&terminalError)) {
            QTextStream(stderr) << "Cannot start terminal UI: " << terminalError << "\n";
            return 2;
        }
    }

    AppController controller(std::move(backend), parser.value(oscillatordHostOption),
        static_cast<quint16>(portValue));

    TuiApplication tui(controller, plain ? nullptr : &terminal, plain,
        TuiRenderer::pageFromName(requestedPage));
    QString tuiError;
    if (!tui.start(&tuiError)) {
        terminal.shutdown();
        QTextStream(stderr) << "Cannot start terminal UI: " << tuiError << "\n";
        return 2;
    }

    if (quitAfter >= 0)
        QTimer::singleShot(quitAfter, &application, &QCoreApplication::quit);

    const int exitCode = application.exec();
    terminal.shutdown();
    return exitCode;
}
