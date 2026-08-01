#include "LinuxTimeCardBackend.h"
#include "TimingMath.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

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
}

LinuxTimeCardBackend::LinuxTimeCardBackend(QString sysfsRoot)
    : m_sysfsRoot(QDir::cleanPath(std::move(sysfsRoot)))
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
        snapshot->error = QStringLiteral("The Time Card does not expose a PTP device link.");
        return;
    }

    const QByteArray path = QFile::encodeName(snapshot->ptpDevice);
    const int fileDescriptor = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fileDescriptor < 0) {
        snapshot->error = QStringLiteral("Cannot open %1: %2")
            .arg(snapshot->ptpDevice, QString::fromLocal8Bit(std::strerror(errno)));
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
        snapshot->error = QStringLiteral("The PTP clock did not return a timestamp sample.");
        return;
    }

    snapshot->timingValid = true;
    snapshot->phcTaiNanoseconds = phcTai;
    snapshot->systemUtcNanoseconds = systemUtc;
    snapshot->sampleWindowValid = sampleWindowValid;
    snapshot->sampleWindowNanoseconds = sampleWindow;
    snapshot->timestampMethod = method;
#else
    snapshot->error = QStringLiteral("PHC sampling is available only on Linux.");
#endif
}

void LinuxTimeCardBackend::addCapabilityIfPresent(
    const QString &cardPathValue, const QString &relativePath,
    const QString &capability, QStringList *capabilities)
{
    if (QFileInfo::exists(QDir(cardPathValue).filePath(relativePath)))
        capabilities->append(capability);
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
    addCapabilityIfPresent(snapshot.sysfsPath, QStringLiteral("sma1"),
        QStringLiteral("SMA"), &snapshot.capabilities);
    addCapabilityIfPresent(snapshot.sysfsPath, QStringLiteral("gen1"),
        QStringLiteral("Signal generators"), &snapshot.capabilities);
    addCapabilityIfPresent(snapshot.sysfsPath, QStringLiteral("freq1"),
        QStringLiteral("Frequency counters"), &snapshot.capabilities);
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
        snapshot.clockDriftValid = true;
        snapshot.clockDriftNanoseconds = value;
    }
    if (readInteger(QDir(snapshot.sysfsPath).filePath(QStringLiteral("clock_status_offset")), &value)) {
        snapshot.clockOffsetValid = true;
        snapshot.clockOffsetNanoseconds = value;
    }

    samplePhc(&snapshot);
    if (snapshot.timingValid && snapshot.utcTaiOffsetValid) {
        const TimingDerivedValues derived = deriveTaiAwareTiming(
            snapshot.phcTaiNanoseconds,
            snapshot.systemUtcNanoseconds,
            snapshot.utcTaiOffsetSeconds);
        snapshot.phcUtcNanoseconds = derived.phcUtcNanoseconds;
        snapshot.offsetNanoseconds = derived.offsetNanoseconds;
        snapshot.offsetValid = true;
    } else if (snapshot.timingValid && snapshot.error.isEmpty()) {
        snapshot.error = QStringLiteral(
            "UTC-TAI offset is unavailable; the PHC offset is intentionally not calculated.");
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
        snapshot.error = snapshot.error.isEmpty()
            ? warning : snapshot.error + QLatin1Char('\n') + warning;
    }

    return snapshot;
}
