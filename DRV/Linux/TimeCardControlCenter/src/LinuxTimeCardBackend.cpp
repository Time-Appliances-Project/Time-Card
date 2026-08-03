#include "LinuxTimeCardBackend.h"
#include "TimingMath.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/ptp_clock.h>
#include <sys/ioctl.h>
#include <sys/timex.h>
#include <time.h>
#include <unistd.h>
#endif

namespace {
#ifdef Q_OS_LINUX
constexpr qint64 nanosecondsPerSecond = 1'000'000'000LL;
constexpr qint64 maximumSamplingWindowNanoseconds = nanosecondsPerSecond;

bool validClockTime(const ptp_clock_time &value)
{
    return value.sec > 0
        && value.sec <= std::numeric_limits<qint64>::max() / nanosecondsPerSecond
        && value.nsec < nanosecondsPerSecond;
}

qint64 clockTimeToNanoseconds(const ptp_clock_time &value)
{
    return value.sec * nanosecondsPerSecond + value.nsec;
}

qint64 timespecToNanoseconds(const timespec &value)
{
    return value.tv_sec * nanosecondsPerSecond + value.tv_nsec;
}

bool validTimespec(const timespec &value)
{
    return value.tv_sec > 0
        && value.tv_sec <= std::numeric_limits<qint64>::max() / nanosecondsPerSecond
        && value.tv_nsec >= 0
        && value.tv_nsec < nanosecondsPerSecond;
}

clockid_t fileDescriptorClockId(int fileDescriptor)
{
    constexpr clockid_t clockFd = 3;
    return ((~static_cast<clockid_t>(fileDescriptor)) << 3) | clockFd;
}
#endif

bool validDeviceNameForResource(const QString &name, const QString &resource)
{
    if (resource == QStringLiteral("ptp")) {
        static const QRegularExpression pattern(QStringLiteral("^ptp[0-9]+$"));
        return pattern.match(name).hasMatch();
    }
    if (resource == QStringLiteral("pps")) {
        static const QRegularExpression pattern(QStringLiteral("^pps[0-9]+$"));
        return pattern.match(name).hasMatch();
    }
    if (resource == QStringLiteral("i2c")) {
        static const QRegularExpression pattern(QStringLiteral("^i2c-[0-9]+$"));
        return pattern.match(name).hasMatch();
    }
    if (resource == QStringLiteral("mro50")) {
        static const QRegularExpression pattern(QStringLiteral("^mro50\\.[0-9]+$"));
        return pattern.match(name).hasMatch();
    }
    if (resource.startsWith(QStringLiteral("tty"))) {
        static const QRegularExpression pattern(
            QStringLiteral("^tty[A-Za-z0-9_.-]+$"));
        return pattern.match(name).hasMatch();
    }
    return false;
}

QString normalizeDeviceName(QString value, const QString &resource)
{
    value = value.trimmed();
    if (value.isEmpty())
        return {};
    const QString name = QFileInfo(value).fileName();
    if (!validDeviceNameForResource(name, resource))
        return {};
    return QStringLiteral("/dev/") + name;
}

QString compactValue(QString value)
{
    value.replace(QLatin1Char('\n'), QStringLiteral("; "));
    return value.simplified();
}

QString readDisplayValue(const QString &path)
{
    return compactValue(LinuxTimeCardBackend::readTextFile(path));
}

QString displayBoolean(const QString &value, const QString &on, const QString &off)
{
    if (value == QStringLiteral("1"))
        return on;
    if (value == QStringLiteral("0"))
        return off;
    return value;
}

QString labeledReading(const QString &label, const QString &value)
{
    return QStringLiteral("%1 | %2").arg(label, value);
}

bool parseDouble(const QString &value, double *result)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (ok && result)
        *result = parsed;
    return ok;
}

void appendSnapshotError(TimeCardSnapshot *snapshot, const QString &message)
{
    if (!snapshot || message.isEmpty())
        return;
    const QStringList existing = snapshot->error.split(
        QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (existing.contains(message))
        return;
    if (!snapshot->error.isEmpty())
        snapshot->error += QLatin1Char('\n');
    snapshot->error += message;
}
}

LinuxTimeCardBackend::LinuxTimeCardBackend(
    QString sysfsRoot, QString hwmonRoot, QString iioRoot, QString ledsRoot)
    : m_sysfsRoot(QDir::cleanPath(std::move(sysfsRoot))),
      m_hwmonRoot(QDir::cleanPath(std::move(hwmonRoot))),
      m_iioRoot(QDir::cleanPath(std::move(iioRoot))),
      m_ledsRoot(QDir::cleanPath(std::move(ledsRoot)))
{
}

QString LinuxTimeCardBackend::backendName() const
{
    return QStringLiteral("Linux ptp_ocp");
}

QString LinuxTimeCardBackend::selectedDevice() const
{
    return m_selectedDevice;
}

void LinuxTimeCardBackend::setSelectedDevice(const QString &deviceId)
{
    if (availableDevices().contains(deviceId) && deviceId != m_selectedDevice) {
        m_selectedDevice = deviceId;
        invalidateIdentityCache();
    }
}

QStringList LinuxTimeCardBackend::availableDevices() const
{
    QDir directory(m_sysfsRoot);
    const QStringList candidates = directory.entryList(
        {QStringLiteral("ocp*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QStringList devices;
    for (const QString &candidate : candidates) {
        bool numeric = candidate.size() > 3;
        for (qsizetype index = 3; numeric && index < candidate.size(); ++index)
            numeric = candidate.at(index).isDigit();
        if (numeric)
            devices.append(candidate);
    }
    return devices;
}

QString LinuxTimeCardBackend::sysfsRoot() const
{
    return m_sysfsRoot;
}

QString LinuxTimeCardBackend::cardPath(const QString &deviceId) const
{
    return QDir(m_sysfsRoot).filePath(deviceId);
}

QString LinuxTimeCardBackend::readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.read(4096)).trimmed();
}

QString LinuxTimeCardBackend::resolveDeviceNode(const QString &entryPath)
{
    const QFileInfo entry(entryPath);
    const QString resource = entry.fileName();
    QString target = entry.symLinkTarget();
    if (!target.isEmpty())
        return normalizeDeviceName(target, resource);

    const QString content = readTextFile(entryPath);
    if (!content.isEmpty())
        return normalizeDeviceName(content, resource);

    if (entry.exists() && entry.fileName().startsWith(QStringLiteral("ptp")))
        return normalizeDeviceName(entry.fileName(), resource);
    return {};
}

bool LinuxTimeCardBackend::readInteger(const QString &path, qint64 *value)
{
    bool ok = false;
    const qint64 parsed = readTextFile(path).toLongLong(&ok, 0);
    if (ok && value)
        *value = parsed;
    return ok;
}

bool LinuxTimeCardBackend::readOptionalTextFile(const QString &path, QString *value)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray contents = file.read(4096);
    if (contents.isEmpty() && file.error() != QFileDevice::NoError)
        return false;
    if (value)
        *value = QString::fromUtf8(contents).trimmed();
    return true;
}

qint64 LinuxTimeCardBackend::currentSystemUtcNanoseconds()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

void LinuxTimeCardBackend::discoverPciIdentity(
    const QString &cardPathValue, TimeCardSnapshot *snapshot)
{
    QString current = QFileInfo(cardPathValue).canonicalFilePath();
    if (current.isEmpty())
        current = QFileInfo(cardPathValue).absoluteFilePath();

    QDir directory(current);
    while (!directory.isRoot()) {
        const QString vendorPath = directory.filePath(QStringLiteral("vendor"));
        const QString devicePath = directory.filePath(QStringLiteral("device"));
        if (QFileInfo::exists(vendorPath) && QFileInfo::exists(devicePath)) {
            snapshot->pciAddress = directory.dirName();
            snapshot->pciVendor = readTextFile(vendorPath);
            snapshot->pciDevice = readTextFile(devicePath);
            return;
        }
        if (!directory.cdUp())
            break;
    }
}

void LinuxTimeCardBackend::samplePhc(TimeCardSnapshot *snapshot)
{
    snapshot->systemUtcNanoseconds = currentSystemUtcNanoseconds();

#ifdef Q_OS_LINUX
    if (snapshot->ptpDevice.isEmpty()) {
        appendSnapshotError(snapshot,
            QStringLiteral("The Time Card does not expose a PTP device link."));
        return;
    }

    const QByteArray path = QFile::encodeName(snapshot->ptpDevice);
    const int fileDescriptor = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fileDescriptor < 0) {
        appendSnapshotError(snapshot, QStringLiteral("Cannot open %1: %2")
            .arg(snapshot->ptpDevice, QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }

    qint64 phcTai = 0;
    qint64 systemUtc = 0;
    qint64 sampleWindow = 0;
    bool sampleWindowValid = false;
    QString method;

#ifdef PTP_SYS_OFFSET_PRECISE
    ptp_clock_caps capabilities {};
    if (::ioctl(fileDescriptor, PTP_CLOCK_GETCAPS, &capabilities) == 0
        && capabilities.cross_timestamping) {
        ptp_sys_offset_precise precise {};
        if (::ioctl(fileDescriptor, PTP_SYS_OFFSET_PRECISE, &precise) == 0) {
            if (validClockTime(precise.device)
                && validClockTime(precise.sys_realtime)) {
                phcTai = clockTimeToNanoseconds(precise.device);
                systemUtc = clockTimeToNanoseconds(precise.sys_realtime);
                method = QStringLiteral("PTP_SYS_OFFSET_PRECISE");
            }
        }
    }
#endif

#ifdef PTP_SYS_OFFSET_EXTENDED
    if (method.isEmpty()) {
        ptp_sys_offset_extended extended {};
        extended.n_samples = 5;
        if (::ioctl(fileDescriptor, PTP_SYS_OFFSET_EXTENDED, &extended) == 0) {
            qint64 bestWindow = std::numeric_limits<qint64>::max();
            for (unsigned int index = 0; index < extended.n_samples; ++index) {
                if (!validClockTime(extended.ts[index][0])
                    || !validClockTime(extended.ts[index][1])
                    || !validClockTime(extended.ts[index][2])) {
                    continue;
                }
                const qint64 before = clockTimeToNanoseconds(extended.ts[index][0]);
                const qint64 device = clockTimeToNanoseconds(extended.ts[index][1]);
                const qint64 after = clockTimeToNanoseconds(extended.ts[index][2]);
                const qint64 window = after - before;
                if (window >= 0
                    && window <= maximumSamplingWindowNanoseconds
                    && window < bestWindow) {
                    bestWindow = window;
                    phcTai = device;
                    systemUtc = before + window / 2;
                    sampleWindow = window;
                    sampleWindowValid = true;
                }
            }
            if (bestWindow != std::numeric_limits<qint64>::max())
                method = QStringLiteral("PTP_SYS_OFFSET_EXTENDED");
        }
    }
#endif

    if (method.isEmpty()) {
        timespec before {};
        timespec device {};
        timespec after {};
        if (::clock_gettime(CLOCK_REALTIME, &before) == 0
            && ::clock_gettime(fileDescriptorClockId(fileDescriptor), &device) == 0
            && ::clock_gettime(CLOCK_REALTIME, &after) == 0
            && validTimespec(before)
            && validTimespec(device)
            && validTimespec(after)) {
            const qint64 beforeNs = timespecToNanoseconds(before);
            const qint64 afterNs = timespecToNanoseconds(after);
            const qint64 window = afterNs - beforeNs;
            if (window >= 0 && window <= maximumSamplingWindowNanoseconds) {
                phcTai = timespecToNanoseconds(device);
                systemUtc = beforeNs + window / 2;
                sampleWindow = window;
                sampleWindowValid = true;
                method = QStringLiteral("Bracketed clock_gettime");
            }
        }
    }

    ::close(fileDescriptor);

    if (method.isEmpty()) {
        appendSnapshotError(snapshot,
            QStringLiteral("The PTP clock did not return a timestamp sample."));
        return;
    }

    snapshot->timingValid = true;
    snapshot->phcTaiNanoseconds = phcTai;
    snapshot->systemUtcNanoseconds = systemUtc;
    snapshot->sampleWindowValid = sampleWindowValid;
    snapshot->sampleWindowNanoseconds = sampleWindow;
    snapshot->timestampMethod = method;
#else
    appendSnapshotError(snapshot,
        QStringLiteral("PHC sampling is available only on Linux."));
#endif
}

void LinuxTimeCardBackend::addCapabilityIfPresent(
    const QString &cardPathValue, const QString &relativePath,
    const QString &capability, QStringList *capabilities)
{
    if (QFileInfo::exists(QDir(cardPathValue).filePath(relativePath)))
        capabilities->append(capability);
}

QString LinuxTimeCardBackend::inferBoardProfile(const TimeCardSnapshot &snapshot)
{
    if (snapshot.pciVendor.compare(QStringLiteral("0x1ad7"), Qt::CaseInsensitive) == 0
        && snapshot.pciDevice.compare(QStringLiteral("0xa000"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Orolia/Safran ART");
    }
    if (snapshot.r4006TopologyDetected) {
        return QStringLiteral("R4006-compatible peripheral profile");
    }
    if (snapshot.pciVendor.compare(QStringLiteral("0x18d4"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Celestica Time Card");
    if (snapshot.pciVendor.compare(QStringLiteral("0x1d9b"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("OCP Time Card");
    return QStringLiteral("Time Card");
}

bool LinuxTimeCardBackend::subsystemEntryBelongsToCard(
    const QString &entryPath, const QString &pciAddress) const
{
    if (pciAddress.isEmpty())
        return m_sysfsRoot != QStringLiteral("/sys/class/timecard");

    QString canonical = QFileInfo(QDir(entryPath).filePath(QStringLiteral("device")))
                            .canonicalFilePath();
    if (canonical.isEmpty())
        canonical = QFileInfo(entryPath).canonicalFilePath();
    if (canonical.isEmpty())
        canonical = QFileInfo(entryPath).absoluteFilePath();
    return canonical.contains(QStringLiteral("/") + pciAddress + QLatin1Char('/'))
        || canonical.endsWith(QStringLiteral("/") + pciAddress)
        || entryPath.contains(pciAddress);
}

void LinuxTimeCardBackend::readTimingIo(TimeCardSnapshot *snapshot) const
{
    const QDir card(snapshot->sysfsPath);
    for (int index = 1; index <= 4; ++index) {
        QString value;
        if (readOptionalTextFile(card.filePath(QStringLiteral("sma%1").arg(index)), &value)
            && !value.isEmpty()) {
            snapshot->smaStates.append(labeledReading(
                QStringLiteral("SMA%1").arg(index), compactValue(value)));
        }
    }

    for (int index = 1; index <= 4; ++index) {
        const QDir generator(card.filePath(QStringLiteral("gen%1").arg(index)));
        QStringList details;
        QString value;
        if (readOptionalTextFile(generator.filePath(QStringLiteral("running")), &value)) {
            details.append(displayBoolean(value, QStringLiteral("running"),
                QStringLiteral("stopped")));
        }
        if (readOptionalTextFile(generator.filePath(QStringLiteral("period")), &value))
            details.append(QStringLiteral("period %1 ns").arg(value));
        if (readOptionalTextFile(generator.filePath(QStringLiteral("duty")), &value))
            details.append(QStringLiteral("duty %1%").arg(value));
        if (readOptionalTextFile(generator.filePath(QStringLiteral("phase")), &value))
            details.append(QStringLiteral("phase %1 ns").arg(value));
        if (readOptionalTextFile(generator.filePath(QStringLiteral("polarity")), &value)) {
            details.append(value == QStringLiteral("1")
                ? QStringLiteral("active high") : QStringLiteral("active low"));
        }
        if (readOptionalTextFile(generator.filePath(QStringLiteral("repeat_count")), &value)) {
            details.append(value == QStringLiteral("0")
                ? QStringLiteral("continuous")
                : QStringLiteral("repeat %1").arg(value));
        }
        if (readOptionalTextFile(generator.filePath(QStringLiteral("cable_delay")), &value))
            details.append(QStringLiteral("cable %1 ns").arg(value));
        if (readOptionalTextFile(generator.filePath(QStringLiteral("start")), &value)
            && !value.isEmpty()) {
            details.append(QStringLiteral("start %1 TAI").arg(compactValue(value)));
        }
        if (!details.isEmpty()) {
            snapshot->generatorStates.append(labeledReading(
                QStringLiteral("GEN%1").arg(index), details.join(QStringLiteral(" | "))));
        }
    }

    for (int index = 1; index <= 4; ++index) {
        const QDir counter(card.filePath(QStringLiteral("freq%1").arg(index)));
        QString seconds;
        QString frequency;
        const bool secondsReadable = readOptionalTextFile(
            counter.filePath(QStringLiteral("seconds")), &seconds);
        const bool frequencyReadable = readOptionalTextFile(
            counter.filePath(QStringLiteral("frequency")), &frequency);
        if (!secondsReadable && !frequencyReadable)
            continue;
        if (seconds == QStringLiteral("0"))
            frequency = QStringLiteral("disabled");
        else if (frequency.isEmpty())
            frequency = QStringLiteral("waiting for sample");
        else {
            bool numeric = false;
            frequency.toULongLong(&numeric);
            if (numeric)
                frequency += QStringLiteral(" Hz");
            else if (frequency == QStringLiteral("error")
                || frequency == QStringLiteral("overrun")) {
                appendSnapshotError(snapshot,
                    QStringLiteral("Frequency counter FREQ%1 reports %2.")
                        .arg(index)
                        .arg(frequency));
            }
        }
        QString detail = frequency;
        if (!seconds.isEmpty())
            detail += QStringLiteral(" | gate %1 s").arg(seconds);
        snapshot->frequencyCounterStates.append(labeledReading(
            QStringLiteral("FREQ%1").arg(index), detail));
    }

    if (!snapshot->smaStates.isEmpty()
        && !snapshot->capabilities.contains(QStringLiteral("SMA"))) {
        snapshot->capabilities.append(QStringLiteral("SMA"));
    }
    if (!snapshot->generatorStates.isEmpty()
        && !snapshot->capabilities.contains(QStringLiteral("Signal generators"))) {
        snapshot->capabilities.append(QStringLiteral("Signal generators"));
    }
    if (!snapshot->frequencyCounterStates.isEmpty()
        && !snapshot->capabilities.contains(QStringLiteral("Frequency counters"))) {
        snapshot->capabilities.append(QStringLiteral("Frequency counters"));
    }
}

void LinuxTimeCardBackend::readFpgaEngines(TimeCardSnapshot *snapshot) const
{
    const QDir card(snapshot->sysfsPath);
    readOptionalTextFile(card.filePath(QStringLiteral("optional_image_contract")),
        &snapshot->optionalImageContract);
    if (snapshot->optionalImageContract.contains(QStringLiteral("targeted=1"))
        && !snapshot->optionalImageContract.contains(QStringLiteral("match=1"))) {
        appendSnapshotError(snapshot,
            QStringLiteral("The optional FPGA image contract is targeted but does not match"));
    }

    auto readAttributes = [&card, snapshot](
                              const QList<QPair<QString, QString>> &attributes) {
        QStringList details;
        for (const auto &attribute : attributes) {
            QString value;
            if (LinuxTimeCardBackend::readOptionalTextFile(
                    card.filePath(attribute.second), &value)) {
                QString displayValue = compactValue(value);
                if (attribute.second == QStringLiteral("nmea_uart_polarity")
                    || attribute.second == QStringLiteral("tod_uart_polarity")) {
                    displayValue = displayBoolean(displayValue,
                        QStringLiteral("normal"), QStringLiteral("inverted"));
                }
                details.append(QStringLiteral("%1 %2").arg(
                    attribute.first, displayValue));
                if (attribute.second.endsWith(QStringLiteral("error"))
                    || attribute.second.endsWith(QStringLiteral("errors"))) {
                    bool numeric = false;
                    const qulonglong errors = value.toULongLong(&numeric, 0);
                    if (numeric && errors != 0) {
                        appendSnapshotError(snapshot,
                            QStringLiteral("FPGA %1 reports %2")
                                .arg(attribute.second, displayValue));
                    }
                }
            }
        }
        return details;
    };
    auto appendEngine = [snapshot](const QString &label, const QStringList &details) {
        if (!details.isEmpty())
            snapshot->fpgaEngineStates.append(labeledReading(
                label, details.join(QStringLiteral(" | "))));
    };

    appendEngine(QStringLiteral("PPS"), readAttributes({
        {QStringLiteral("external polarity"), QStringLiteral("external_pps_polarity")},
        {QStringLiteral("pulse width ms"), QStringLiteral("external_pps_pulse_width")},
        {QStringLiteral("external cable ns"), QStringLiteral("external_pps_cable_delay")},
        {QStringLiteral("internal polarity"), QStringLiteral("internal_pps_polarity")},
        {QStringLiteral("internal cable ns"), QStringLiteral("internal_pps_cable_delay")},
    }));
    appendEngine(QStringLiteral("NMEA output"), readAttributes({
        {QStringLiteral("enabled"), QStringLiteral("nmea_enable")},
        {QStringLiteral("baud"), QStringLiteral("nmea_baud_rate")},
        {QStringLiteral("GNSS"), QStringLiteral("nmea_gnss")},
        {QStringLiteral("polarity"), QStringLiteral("nmea_uart_polarity")},
        {QStringLiteral("correction s"), QStringLiteral("nmea_correction_seconds")},
        {QStringLiteral("local offset min"), QStringLiteral("nmea_local_offset_minutes")},
        {QStringLiteral("message mask"), QStringLiteral("nmea_message_disable_mask")},
        {QStringLiteral("errors"), QStringLiteral("nmea_errors")},
    }));
    appendEngine(QStringLiteral("ToD parser"), readAttributes({
        {QStringLiteral("protocol"), QStringLiteral("tod_protocol")},
        {QStringLiteral("GNSS"), QStringLiteral("tod_gnss")},
        {QStringLiteral("baud"), QStringLiteral("tod_baud_rate")},
        {QStringLiteral("polarity"), QStringLiteral("tod_uart_polarity")},
        {QStringLiteral("correction s"), QStringLiteral("tod_correction")},
        {QStringLiteral("message mask"), QStringLiteral("tod_message_disable_mask")},
        {QStringLiteral("errors"), QStringLiteral("tod_errors")},
    }));
    appendEngine(QStringLiteral("IRIG/DCF"), readAttributes({
        {QStringLiteral("output mode"), QStringLiteral("irig_output_mode")},
        {QStringLiteral("input mode"), QStringLiteral("irig_input_mode")},
        {QStringLiteral("B mode"), QStringLiteral("irig_b_mode")},
        {QStringLiteral("output AM"), QStringLiteral("irig_output_am")},
        {QStringLiteral("input code"), QStringLiteral("irig_input_code")},
        {QStringLiteral("manual year"), QStringLiteral("irig_input_manual_year")},
        {QStringLiteral("input AM"), QStringLiteral("irig_input_am")},
        {QStringLiteral("control bits"), QStringLiteral("irig_output_control_bits")},
        {QStringLiteral("input cable ns"), QStringLiteral("irig_input_cable_delay")},
        {QStringLiteral("DCF air delay ns"), QStringLiteral("dcf_input_air_delay")},
        {QStringLiteral("DCF bit"), QStringLiteral("dcf_input_bit_position")},
        {QStringLiteral("output error"), QStringLiteral("irig_output_error")},
        {QStringLiteral("input error"), QStringLiteral("irig_input_error")},
        {QStringLiteral("DCF output error"), QStringLiteral("dcf_output_error")},
        {QStringLiteral("DCF input error"), QStringLiteral("dcf_input_error")},
    }));

    if (!snapshot->fpgaEngineStates.isEmpty())
        snapshot->capabilities.append(QStringLiteral("FPGA engine status"));
}

void LinuxTimeCardBackend::readStandardSensors(TimeCardSnapshot *snapshot) const
{
    struct HwmonExpectation {
        QString clientPath;
        QString driverName;
        QString label;
        bool humidity = false;
    };

    const QString adapterPath = QFileInfo(
        QDir(snapshot->sysfsPath).filePath(QStringLiteral("i2c")))
                                    .canonicalFilePath();
    static const QRegularExpression adapterPattern(QStringLiteral("^i2c-([0-9]+)$"));
    const QRegularExpressionMatch adapterMatch =
        adapterPattern.match(QFileInfo(adapterPath).fileName());
    QString muxPath;
    if (adapterMatch.hasMatch()) {
        muxPath = QDir(adapterPath).filePath(
            adapterMatch.captured(1) + QStringLiteral("-0070"));
    }

    const auto clientPath = [&muxPath](int channel, const QString &address) {
        if (muxPath.isEmpty())
            return QString();
        const QString channelPath = QFileInfo(QDir(muxPath).filePath(
            QStringLiteral("channel-%1").arg(channel))).canonicalFilePath();
        const QRegularExpressionMatch channelMatch = adapterPattern.match(
            QFileInfo(channelPath).fileName());
        if (!channelMatch.hasMatch())
            return QString();
        return QFileInfo(QDir(channelPath).filePath(
            channelMatch.captured(1) + QStringLiteral("-00") + address))
            .canonicalFilePath();
    };

    const QList<HwmonExpectation> expectedHwmon {
        {clientPath(0, QStringLiteral("48")), QStringLiteral("lm75"),
            QStringLiteral("LM75B 0x48"), false},
        {clientPath(0, QStringLiteral("49")), QStringLiteral("lm75"),
            QStringLiteral("LM75B 0x49"), false},
        {clientPath(0, QStringLiteral("4a")), QStringLiteral("lm75"),
            QStringLiteral("LM75B 0x4a"), false},
        {clientPath(1, QStringLiteral("44")), QStringLiteral("sht3x"),
            QStringLiteral("SHT3x 0x44"), true},
    };
    const QString expectedIcpClient = clientPath(2, QStringLiteral("63"));
    snapshot->r4006TopologyDetected = !muxPath.isEmpty()
        && QFileInfo::exists(muxPath)
        && std::all_of(expectedHwmon.cbegin(), expectedHwmon.cend(),
            [](const HwmonExpectation &expected) {
                return !expected.clientPath.isEmpty()
                    && QFileInfo::exists(expected.clientPath);
            })
        && !expectedIcpClient.isEmpty() && QFileInfo::exists(expectedIcpClient);

    const auto subsystemDevicePath = [](const QString &entryPath) {
        QString path = QFileInfo(QDir(entryPath).filePath(QStringLiteral("device")))
                           .canonicalFilePath();
        if (path.isEmpty())
            path = QFileInfo(entryPath).canonicalFilePath();
        return QDir::cleanPath(path);
    };
    const auto belongsToClient = [&subsystemDevicePath](
                                     const QString &entryPath,
                                     const QString &expectedClient) {
        if (expectedClient.isEmpty())
            return false;
        const QString actual = subsystemDevicePath(entryPath);
        const QString expected = QDir::cleanPath(expectedClient);
        return actual == expected
            || actual.startsWith(expected + QLatin1Char('/'));
    };
    const auto readMeasurement = [snapshot](
                                     const QString &path,
                                     const QString &label,
                                     double *result) {
        QString text;
        if (!LinuxTimeCardBackend::readOptionalTextFile(path, &text)) {
            appendSnapshotError(snapshot,
                QStringLiteral("Sensor read failed for %1").arg(label));
            return false;
        }
        if (!parseDouble(text, result)) {
            appendSnapshotError(snapshot,
                QStringLiteral("Sensor returned an invalid value for %1").arg(label));
            return false;
        }
        return true;
    };

    const QDir hwmonRoot(m_hwmonRoot);
    const QStringList monitors = hwmonRoot.entryList(
        {QStringLiteral("hwmon*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &monitorName : monitors) {
        const QDir monitor(hwmonRoot.filePath(monitorName));
        const auto expected = std::find_if(
            expectedHwmon.cbegin(), expectedHwmon.cend(),
            [&monitor, &belongsToClient](const HwmonExpectation &candidate) {
                return belongsToClient(monitor.path(), candidate.clientPath);
            });
        if (expected == expectedHwmon.cend())
            continue;
        const QString driverName = readDisplayValue(monitor.filePath(QStringLiteral("name")));
        if (!driverName.startsWith(expected->driverName, Qt::CaseInsensitive))
            continue;

        const QStringList temperatureInputs = monitor.entryList(
            {QStringLiteral("temp*_input")}, QDir::Files, QDir::Name);
        for (const QString &input : temperatureInputs) {
            const QString base = input.left(input.size() - QStringLiteral("_input").size());
            const QString label = temperatureInputs.size() == 1
                ? expected->label
                : QStringLiteral("%1 %2").arg(expected->label, base.mid(4));
            double raw = 0.0;
            if (!readMeasurement(monitor.filePath(input), label, &raw))
                continue;
            snapshot->sensorStates.append(labeledReading(label,
                QStringLiteral("%1 C").arg(raw / 1000.0, 0, 'f', 2)));
        }

        if (expected->humidity) {
            const QStringList humidityInputs = monitor.entryList(
                {QStringLiteral("humidity*_input")}, QDir::Files, QDir::Name);
            for (const QString &input : humidityInputs) {
                double raw = 0.0;
                if (!readMeasurement(monitor.filePath(input),
                        QStringLiteral("SHT3x 0x44 humidity"), &raw)) {
                    continue;
                }
                snapshot->sensorStates.append(labeledReading(
                    QStringLiteral("SHT3x 0x44 humidity"),
                    QStringLiteral("%1%").arg(raw / 1000.0, 0, 'f', 2)));
            }
        }
        if (expected->humidity) {
            QString temperatureAlarm;
            QString humidityAlarm;
            const bool temperatureReadable = readOptionalTextFile(
                monitor.filePath(QStringLiteral("temp1_alarm")), &temperatureAlarm);
            const bool humidityReadable = readOptionalTextFile(
                monitor.filePath(QStringLiteral("humidity1_alarm")), &humidityAlarm);
            if (temperatureReadable || humidityReadable) {
                QStringList alarms;
                if (temperatureReadable) {
                    alarms.append(temperatureAlarm == QStringLiteral("0")
                        ? QStringLiteral("temperature clear")
                        : QStringLiteral("temperature alarm"));
                }
                if (humidityReadable) {
                    alarms.append(humidityAlarm == QStringLiteral("0")
                        ? QStringLiteral("humidity clear")
                        : QStringLiteral("humidity alarm"));
                }
                snapshot->sensorStates.append(labeledReading(
                    QStringLiteral("SHT3x alarms"), alarms.join(QStringLiteral(" | "))));
            }
        }
    }

    const QDir iioRoot(m_iioRoot);
    const QStringList devices = iioRoot.entryList(
        {QStringLiteral("iio:device*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &deviceName : devices) {
        const QDir device(iioRoot.filePath(deviceName));
        if (!belongsToClient(device.path(), expectedIcpClient))
            continue;
        if (readDisplayValue(device.filePath(QStringLiteral("name")))
                .compare(QStringLiteral("icp10100"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        double pressure = 0.0;
        if (readMeasurement(device.filePath(QStringLiteral("in_pressure_input")),
                QStringLiteral("ICP-10100 pressure"), &pressure)) {
            snapshot->sensorStates.append(labeledReading(QStringLiteral("ICP-10100 pressure"),
                QStringLiteral("%1 kPa").arg(pressure, 0, 'f', 3)));
        }
        double rawTemperature = 0.0;
        if (readMeasurement(device.filePath(QStringLiteral("in_temp_raw")),
                QStringLiteral("ICP-10100 temperature"), &rawTemperature)) {
            const double celsius = -45.0 + 175.0 * rawTemperature / 65536.0;
            snapshot->sensorStates.append(labeledReading(
                QStringLiteral("ICP-10100 temperature"),
                QStringLiteral("%1 C").arg(celsius, 0, 'f', 2)));
        }
    }

    if (!snapshot->sensorStates.isEmpty())
        snapshot->capabilities.append(QStringLiteral("Sensors"));
}

void LinuxTimeCardBackend::readStandardLeds(TimeCardSnapshot *snapshot) const
{
    QString prefix;
    static const QRegularExpression bdfPattern(
        QStringLiteral("^([0-9a-fA-F]{4}):([0-9a-fA-F]{2}):([0-9a-fA-F]{2})\\.([0-7])$"));
    const QRegularExpressionMatch match = bdfPattern.match(snapshot->pciAddress);
    if (match.hasMatch()) {
        prefix = QStringLiteral("timecard-%1-%2-%3-%4:rgb:indicator-")
            .arg(match.captured(1).toLower(), match.captured(2).toLower(),
                match.captured(3).toLower(), match.captured(4));
    }

    const QDir ledsRoot(m_ledsRoot);
    const QString filter = prefix.isEmpty()
        ? QStringLiteral("timecard-*:rgb:indicator-*") : prefix + QLatin1Char('*');
    const QStringList leds = ledsRoot.entryList(
        {filter}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &ledName : leds) {
        if (!prefix.isEmpty() && !ledName.startsWith(prefix))
            continue;
        if (prefix.isEmpty()
            && !subsystemEntryBelongsToCard(ledsRoot.filePath(ledName), snapshot->pciAddress)) {
            continue;
        }
        const QDir led(ledsRoot.filePath(ledName));
        QString brightness;
        QString maximum;
        QString components;
        QString intensity;
        if (!readOptionalTextFile(led.filePath(QStringLiteral("brightness")), &brightness)
            || !readOptionalTextFile(led.filePath(QStringLiteral("max_brightness")), &maximum)
            || !readOptionalTextFile(led.filePath(QStringLiteral("multi_index")), &components)
            || !readOptionalTextFile(led.filePath(QStringLiteral("multi_intensity")), &intensity)) {
            continue;
        }
        const QString connector = ledName.section(QStringLiteral("indicator-"), -1).toUpper();
        const QString rgb = components.simplified() == QStringLiteral("red green blue")
            ? QStringLiteral("RGB %1").arg(intensity.simplified())
            : QStringLiteral("%1 %2").arg(components.simplified(), intensity.simplified());
        snapshot->ledStates.append(labeledReading(connector,
            QStringLiteral("brightness %1/%2 | %3").arg(brightness, maximum, rgb)));
    }

    if (!snapshot->ledStates.isEmpty())
        snapshot->capabilities.append(QStringLiteral("Status LEDs"));
}

QString LinuxTimeCardBackend::ttyDevice(
    const QString &cardPathValue, const QString &name) const
{
    const QString grouped = QDir(cardPathValue).filePath(QStringLiteral("tty/") + name);
    QString device = resolveDeviceNode(grouped);
    if (!device.isEmpty())
        return device;
    return resolveDeviceNode(QDir(cardPathValue).filePath(name));
}

void LinuxTimeCardBackend::invalidateIdentityCache()
{
    m_cachedIdentityDevice.clear();
    m_cachedIdentity = {};
    m_identityAge.invalidate();
}

void LinuxTimeCardBackend::refreshIdentityCache()
{
    TimeCardSnapshot snapshot;
    snapshot.connected = true;
    snapshot.deviceId = m_selectedDevice;
    snapshot.sysfsPath = cardPath(m_selectedDevice);
    snapshot.ptpDevice = resolveDeviceNode(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("ptp")));
    snapshot.ppsDevice = resolveDeviceNode(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("pps")));
    snapshot.i2cDevice = resolveDeviceNode(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("i2c")));
    snapshot.mro50Device = resolveDeviceNode(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("mro50")));
    snapshot.serialNumber = readTextFile(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("serialnum")));

    snapshot.ttyGnss = ttyDevice(snapshot.sysfsPath, QStringLiteral("ttyGNSS"));
    snapshot.ttyGnss2 = ttyDevice(snapshot.sysfsPath, QStringLiteral("ttyGNSS2"));
    snapshot.ttyMac = ttyDevice(snapshot.sysfsPath, QStringLiteral("ttyMAC"));
    snapshot.ttyNmea = ttyDevice(snapshot.sysfsPath, QStringLiteral("ttyNMEA"));

    discoverPciIdentity(snapshot.sysfsPath, &snapshot);

    addCapabilityIfPresent(snapshot.sysfsPath, QStringLiteral("ptp"),
        QStringLiteral("PHC"), &snapshot.capabilities);
    if (QFileInfo::exists(QDir(snapshot.sysfsPath).filePath(QStringLiteral("gnss_sync")))
        || !snapshot.ttyGnss.isEmpty()) {
        snapshot.capabilities.append(QStringLiteral("GNSS"));
    }
    addCapabilityIfPresent(snapshot.sysfsPath, QStringLiteral("pps"),
        QStringLiteral("PPS"), &snapshot.capabilities);
    if (!snapshot.ttyGnss.isEmpty() || !snapshot.ttyGnss2.isEmpty()
        || !snapshot.ttyMac.isEmpty() || !snapshot.ttyNmea.isEmpty()) {
        snapshot.capabilities.append(QStringLiteral("UART"));
    }
    addCapabilityIfPresent(snapshot.sysfsPath, QStringLiteral("mro50"),
        QStringLiteral("mRO-50"), &snapshot.capabilities);
    addCapabilityIfPresent(snapshot.sysfsPath, QStringLiteral("i2c"),
        QStringLiteral("I2C"), &snapshot.capabilities);
    addCapabilityIfPresent(snapshot.sysfsPath, QStringLiteral("config"),
        QStringLiteral("SPI flash configuration"), &snapshot.capabilities);

    m_cachedIdentity = std::move(snapshot);
    m_cachedIdentityDevice = m_selectedDevice;
    m_identityAge.start();
}

TimeCardSnapshot LinuxTimeCardBackend::readSnapshot()
{
    TimeCardSnapshot snapshot;
    snapshot.backendName = backendName();
    snapshot.availableDevices = availableDevices();

    if (snapshot.availableDevices.isEmpty()) {
        m_selectedDevice.clear();
        invalidateIdentityCache();
        snapshot.error = QStringLiteral("No ptp_ocp Time Card was found in %1.").arg(m_sysfsRoot);
        snapshot.systemUtcNanoseconds = currentSystemUtcNanoseconds();
        return snapshot;
    }

    if (!snapshot.availableDevices.contains(m_selectedDevice)) {
        m_selectedDevice = snapshot.availableDevices.constFirst();
        invalidateIdentityCache();
    }
    const QStringList devices = snapshot.availableDevices;

    const qint64 identityCacheLifetime = m_cachedIdentity.ptpDevice.isEmpty()
        ? 2'000 : 60'000;
    if (m_cachedIdentityDevice != m_selectedDevice
        || !m_identityAge.isValid()
        || m_identityAge.hasExpired(identityCacheLifetime)) {
        refreshIdentityCache();
    }

    snapshot = m_cachedIdentity;
    snapshot.backendName = backendName();
    snapshot.availableDevices = devices;
    snapshot.clockSource = readTextFile(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("clock_source")));
    snapshot.gnssState = readTextFile(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("gnss_sync")));
    snapshot.gnssLocked = snapshot.gnssState.startsWith(QStringLiteral("SYNC"));
    snapshot.todProtocol = readTextFile(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("tod_protocol")));
    snapshot.todBaudRate = readTextFile(
        QDir(snapshot.sysfsPath).filePath(QStringLiteral("tod_baud_rate")));

    qint64 value = 0;
    if (readInteger(QDir(snapshot.sysfsPath).filePath(QStringLiteral("utc_tai_offset")), &value)
        && value >= 0 && value <= std::numeric_limits<int>::max()) {
        snapshot.cardUtcTaiOffsetValid = true;
        snapshot.cardUtcTaiOffsetSeconds = static_cast<int>(value);
    }

    bool kernelTaiOffsetValid = false;
    int kernelTaiOffsetSeconds = 0;
#ifdef Q_OS_LINUX
    timex systemTimeState {};
    if (::adjtimex(&systemTimeState) >= 0 && systemTimeState.tai > 0) {
        kernelTaiOffsetValid = true;
        kernelTaiOffsetSeconds = systemTimeState.tai;
    }
#endif

    const TaiOffsetSelection taiOffset = selectTaiOffset(
        kernelTaiOffsetValid,
        kernelTaiOffsetSeconds,
        snapshot.cardUtcTaiOffsetValid,
        snapshot.cardUtcTaiOffsetSeconds);
    snapshot.utcTaiOffsetValid = taiOffset.valid;
    snapshot.utcTaiOffsetSeconds = taiOffset.seconds;
    snapshot.utcTaiOffsetFromKernel = taiOffset.source == TaiOffsetSource::Kernel;

    if (readInteger(QDir(snapshot.sysfsPath).filePath(QStringLiteral("clock_status_drift")), &value)) {
        snapshot.clockDriftPpbValid = true;
        snapshot.clockDriftPartsPerBillion = value;
    }
    if (readInteger(QDir(snapshot.sysfsPath).filePath(QStringLiteral("clock_status_offset")), &value)) {
        snapshot.clockOffsetValid = true;
        snapshot.clockOffsetNanoseconds = value;
    }

    readTimingIo(&snapshot);
    readFpgaEngines(&snapshot);
    readStandardSensors(&snapshot);
    readStandardLeds(&snapshot);
    snapshot.boardProfile = inferBoardProfile(snapshot);

    samplePhc(&snapshot);
    if (snapshot.timingValid && snapshot.utcTaiOffsetValid) {
        const TimingDerivedValues derived = deriveTaiAwareTiming(
            snapshot.phcTaiNanoseconds,
            snapshot.systemUtcNanoseconds,
            snapshot.utcTaiOffsetSeconds);
        snapshot.phcUtcNanoseconds = derived.phcUtcNanoseconds;
        snapshot.offsetNanoseconds = derived.offsetNanoseconds;
        snapshot.offsetValid = true;
    } else if (snapshot.timingValid) {
        appendSnapshotError(&snapshot, QStringLiteral(
            "UTC-TAI offset is unavailable; the PHC offset is intentionally not calculated."));
    }

    if (snapshot.utcTaiOffsetFromKernel
        && snapshot.cardUtcTaiOffsetValid
        && snapshot.cardUtcTaiOffsetSeconds > 0
        && snapshot.cardUtcTaiOffsetSeconds != snapshot.utcTaiOffsetSeconds) {
        const QString warning = QStringLiteral(
            "Kernel UTC-TAI offset (%1 s) differs from the Time Card attribute (%2 s); "
            "the kernel value is used for the PHC comparison.")
            .arg(snapshot.utcTaiOffsetSeconds)
            .arg(snapshot.cardUtcTaiOffsetSeconds);
        appendSnapshotError(&snapshot, warning);
    }

    return snapshot;
}
