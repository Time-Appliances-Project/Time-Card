#include "AppController.h"
#include "LinuxTimeCardBackend.h"
#include "MockTimeCardBackend.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Time Card Control Center"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Open Time Server"));
    QGuiApplication::setDesktopFileName(
        QStringLiteral("org.opentimeserver.TimeCardControlCenter"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Linux control and telemetry dashboard for the OCP Time Card"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption mockOption(
        QStringLiteral("mock"), QStringLiteral("Use the built-in hardware simulation."));
    QCommandLineOption sysfsRootOption(
        QStringLiteral("sysfs-root"),
        QStringLiteral("Override /sys/class/timecard for testing."),
        QStringLiteral("path"),
        qEnvironmentVariable("TIMECARD_SYSFS_ROOT", QStringLiteral("/sys/class/timecard")));
    QCommandLineOption hwmonRootOption(
        QStringLiteral("hwmon-root"),
        QStringLiteral("Override /sys/class/hwmon for testing."),
        QStringLiteral("path"),
        qEnvironmentVariable("TIMECARD_HWMON_ROOT", QStringLiteral("/sys/class/hwmon")));
    QCommandLineOption iioRootOption(
        QStringLiteral("iio-root"),
        QStringLiteral("Override /sys/bus/iio/devices for testing."),
        QStringLiteral("path"),
        qEnvironmentVariable("TIMECARD_IIO_ROOT", QStringLiteral("/sys/bus/iio/devices")));
    QCommandLineOption ledsRootOption(
        QStringLiteral("leds-root"),
        QStringLiteral("Override /sys/class/leds for testing."),
        QStringLiteral("path"),
        qEnvironmentVariable("TIMECARD_LEDS_ROOT", QStringLiteral("/sys/class/leds")));
    QCommandLineOption oscillatordHostOption(
        QStringLiteral("oscillatord-host"),
        QStringLiteral("oscillatord monitoring host."),
        QStringLiteral("host"),
        qEnvironmentVariable("TIMECARD_OSCILLATORD_HOST", QStringLiteral("127.0.0.1")));
    QCommandLineOption oscillatordPortOption(
        QStringLiteral("oscillatord-port"),
        QStringLiteral("oscillatord monitoring port."),
        QStringLiteral("port"),
        qEnvironmentVariable("TIMECARD_OSCILLATORD_PORT", QStringLiteral("2958")));
    QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Capture the rendered dashboard and exit."),
        QStringLiteral("path"));
    QCommandLineOption pageOption(
        QStringLiteral("page"),
        QStringLiteral("Open overview, timing-io, sensors, gnss, or oscillatord."),
        QStringLiteral("name"),
        QStringLiteral("overview"));
    QCommandLineOption quitAfterOption(
        QStringLiteral("quit-after"),
        QStringLiteral("Exit after the specified number of milliseconds."),
        QStringLiteral("milliseconds"));

    parser.addOptions({mockOption, sysfsRootOption, hwmonRootOption, iioRootOption,
        ledsRootOption, oscillatordHostOption, oscillatordPortOption,
        screenshotOption, pageOption, quitAfterOption});
    parser.process(application);

    bool portOk = false;
    const int portValue = parser.value(oscillatordPortOption).toInt(&portOk);
    if (!portOk || portValue <= 0 || portValue > 65535) {
        QTextStream(stderr) << "Invalid --oscillatord-port; expected 1 through 65535.\n";
        return 2;
    }
    const quint16 oscillatordPort = static_cast<quint16>(portValue);

    const QString requestedPage = parser.value(pageOption).trimmed().toLower();
    int pageIndex = 0;
    if (requestedPage == QStringLiteral("timing")
        || requestedPage == QStringLiteral("timing-io")
        || requestedPage == QStringLiteral("io")
        || requestedPage == QStringLiteral("fpga")) {
        pageIndex = 1;
    } else if (requestedPage == QStringLiteral("sensors")) {
        pageIndex = 2;
    } else if (requestedPage == QStringLiteral("gnss")) {
        pageIndex = 3;
    } else if (requestedPage == QStringLiteral("oscillatord")
        || requestedPage == QStringLiteral("oscillator")) {
        pageIndex = 4;
    } else if (requestedPage != QStringLiteral("overview")) {
        QTextStream(stderr)
            << "Invalid --page; expected overview, timing-io, sensors, gnss, or oscillatord.\n";
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

    AppController controller(
        std::move(backend), parser.value(oscillatordHostOption), oscillatordPort);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    const QUrl mainUrl(QStringLiteral(
        "qrc:/qt/qml/TimeCard/ControlCenter/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(mainUrl);

    if (engine.rootObjects().isEmpty())
        return 1;

    engine.rootObjects().constFirst()->setProperty("currentPage", pageIndex);

    const QString screenshotPath = parser.value(screenshotOption);
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(1600, &application, [&engine, screenshotPath] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
            if (!window) {
                QCoreApplication::exit(2);
                return;
            }
            const QFileInfo output(screenshotPath);
            QDir().mkpath(output.absolutePath());
            const bool saved = window->grabWindow().save(output.absoluteFilePath());
            QCoreApplication::exit(saved ? 0 : 2);
        });
    } else if (parser.isSet(quitAfterOption)) {
        bool timeoutOk = false;
        const int timeout = parser.value(quitAfterOption).toInt(&timeoutOk);
        if (timeoutOk && timeout >= 0)
            QTimer::singleShot(timeout, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
