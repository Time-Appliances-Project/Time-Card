/* SPDX-License-Identifier: BSD-3-Clause */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "TimeCardABI.h"
#include "TimeCardRegisters.h"

#define TIMECARD_MIN_COMPATIBLE_ABI_VERSION 7u
#define TIMECARD_UART_ABI_VERSION 8u
#define TIMECARD_UART_WRITE_ABI_VERSION 9u
#define TIMECARD_UART_CAPTURE_MAX_BYTES 65536u

static void
print_usage(FILE *stream)
{
    fprintf(stream,
            "usage: timecardctl status\n"
            "       timecardctl get\n"
            "       timecardctl set-card-from-system\n"
            "       timecardctl clock-control\n"
            "       timecardctl clock-source source [expected-source]\n"
            "       timecardctl frequency [counter]\n"
            "       timecardctl pps [core: 1=output, 2=input]\n"
            "       timecardctl frequency-set counter seconds\n"
            "       timecardctl sma [connector]\n"
            "       timecardctl sma-set connector input|output function\n"
            "       timecardctl sma-set connector disabled\n"
            "       timecardctl led [led]\n"
            "       timecardctl led-set led red green blue [current]\n"
            "       timecardctl led-sma-auto\n"
            "       timecardctl led-gnss-auto\n"
            "       timecardctl led-auto\n"
            "       timecardctl i2c-status\n"
            "       timecardctl i2c-scan\n"
            "       timecardctl i2c-read address [subaddress [length "
            "[subaddress-length]]]\n"
            "       timecardctl i2c-mux [channel-mask]\n"
            "       timecardctl sensors\n"
            "       timecardctl imu [start|stop]\n"
            "       timecardctl uart-observe port [timeout-ms]\n"
            "       timecardctl uart-config port baud\n"
            "       timecardctl uart-read port [max-bytes [timeout-ms]]\n"
            "       timecardctl uart-capture port [seconds [baud]]\n"
            "       timecardctl uart-write-hex port hex-string [timeout-ms]\n"
            "       timecardctl ubx-poll-read port mon-ver|mon-hw|mon-hw2|"
            "nav-status|nav-pvt|nav-dop|nav-clock|nav-timegps|nav-timeutc|"
            "nav-timels|nav-sat|nav-svin|tim-tp [baud [timeout-ms]]\n");
}

static io_connect_t
open_timecard(void)
{
    CFMutableDictionaryRef matching = IOServiceMatching("IOUserService");
    CFMutableDictionaryRef propertyMatch = NULL;
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_service_t service;
    io_service_t secondService;
    io_connect_t connection = IO_OBJECT_NULL;
    kern_return_t result;

    if (matching == NULL) {
        fprintf(stderr, "timecardctl: cannot create an IOKit match request\n");
        exit(1);
    }

    propertyMatch = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (propertyMatch == NULL) {
        CFRelease(matching);
        fprintf(stderr, "timecardctl: cannot create an IOKit property match\n");
        exit(1);
    }
    CFDictionarySetValue(propertyMatch, CFSTR("IOUserClass"),
                         CFSTR(TIMECARD_SERVICE_CLASS));
    CFDictionarySetValue(matching, CFSTR(kIOPropertyMatchKey), propertyMatch);
    CFRelease(propertyMatch);

    result = IOServiceGetMatchingServices(kIOMainPortDefault, matching,
                                           &iterator);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "timecardctl: service enumeration failed: 0x%08x\n",
                result);
        exit(1);
    }

    service = IOIteratorNext(iterator);
    if (service == IO_OBJECT_NULL) {
        IOObjectRelease(iterator);
        fprintf(stderr,
                "timecardctl: Time Card service not found; install the driver "
                "and verify PCI enumeration\n");
        exit(1);
    }
    secondService = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (secondService != IO_OBJECT_NULL) {
        IOObjectRelease(secondService);
        IOObjectRelease(service);
        fprintf(stderr,
                "timecardctl: multiple Time Cards found; explicit device "
                "selection is required before opening one\n");
        exit(1);
    }

    result = IOServiceOpen(service, mach_task_self(), 0, &connection);
    IOObjectRelease(service);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "timecardctl: IOServiceOpen failed: 0x%08x\n", result);
        exit(1);
    }
    return connection;
}

static int
call_output(io_connect_t connection, uint32_t selector, void *output,
            size_t expectedSize)
{
    size_t outputSize = expectedSize;
    kern_return_t result = IOConnectCallStructMethod(
        connection, selector, NULL, 0, output, &outputSize);

    if (result != KERN_SUCCESS) {
        fprintf(stderr, "timecardctl: method %u failed: 0x%08x\n", selector,
                result);
        return 1;
    }
    if (outputSize != expectedSize) {
        fprintf(stderr,
                "timecardctl: method %u returned %zu bytes, expected %zu\n",
                selector, outputSize, expectedSize);
        return 1;
    }
    return 0;
}

static int
call_inout(io_connect_t connection, uint32_t selector, void *input,
           size_t inputSize, void *output, size_t expectedSize)
{
    size_t outputSize = expectedSize;
    kern_return_t result = IOConnectCallStructMethod(
        connection, selector, input, inputSize, output, &outputSize);

    if (result != KERN_SUCCESS) {
        fprintf(stderr, "timecardctl: method %u failed: 0x%08x\n", selector,
                result);
        return 1;
    }
    if (outputSize != expectedSize) {
        fprintf(stderr,
                "timecardctl: method %u returned %zu bytes, expected %zu\n",
                selector, outputSize, expectedSize);
        return 1;
    }
    return 0;
}

static kern_return_t
call_inout_result(io_connect_t connection, uint32_t selector, void *input,
                  size_t inputSize, void *output, size_t expectedSize)
{
    size_t outputSize = expectedSize;
    kern_return_t result = IOConnectCallStructMethod(
        connection, selector, input, inputSize, output, &outputSize);

    if (result == KERN_SUCCESS && outputSize != expectedSize)
        return kIOReturnBadMedia;
    return result;
}

static int
report_unsupported_abi(uint32_t abiVersion, uint32_t minimumVersion,
                       const char *feature)
{
    if (abiVersion < minimumVersion) {
        fprintf(stderr,
                "timecardctl: %s requires driver ABI %u or newer; active "
                "ABI is %u\n",
                feature, minimumVersion, abiVersion);
    } else {
        fprintf(stderr,
                "timecardctl: unsupported driver ABI %u; this build "
                "supports ABI %u through %u\n",
                abiVersion, minimumVersion, TIMECARD_ABI_VERSION);
    }
    return 1;
}

static bool
driver_abi_supported(uint32_t abiVersion, uint32_t minimumVersion)
{
    return abiVersion >= minimumVersion &&
        abiVersion <= TIMECARD_ABI_VERSION;
}

static int
require_driver_abi(io_connect_t connection, uint32_t minimumVersion,
                   const char *feature)
{
    TimeCardInfo info = {0};
    if (call_output(connection, kTimeCardMethodGetInfo, &info, sizeof(info)))
        return 1;
    if (!driver_abi_supported(info.abiVersion, minimumVersion))
        return report_unsupported_abi(
            info.abiVersion, minimumVersion, feature);
    return 0;
}

static void
print_card_time(const TimeCardTime *cardTime)
{
    time_t seconds = (time_t)cardTime->seconds;
    struct tm utc;

    if (gmtime_r(&seconds, &utc) != NULL) {
        printf("%04d-%02d-%02d %02d:%02d:%02d.%09u UTC",
               utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
               utc.tm_hour, utc.tm_min, utc.tm_sec, cardTime->nanoseconds);
    } else {
        printf("%" PRIu64 ".%09u", cardTime->seconds,
               cardTime->nanoseconds);
    }
}

static const char *
gnss_fix_name(uint32_t code)
{
    switch (code) {
    case 0:
        return "No fix";
    case 1:
        return "Dead reckoning";
    case 2:
        return "2-D fix";
    case 3:
        return "3-D fix";
    case 4:
        return "GNSS + dead reckoning";
    default:
        return "Unknown";
    }
}

static int
command_status(io_connect_t connection)
{
    TimeCardInfo info = {0};

    if (call_output(connection, kTimeCardMethodGetInfo, &info, sizeof(info)))
        return 1;
    if (!driver_abi_supported(
            info.abiVersion, TIMECARD_MIN_COMPATIBLE_ABI_VERSION))
        return report_unsupported_abi(
            info.abiVersion, TIMECARD_MIN_COMPATIBLE_ABI_VERSION, "status");

    printf("PCI device:       %04x:%04x\n", info.vendorID, info.deviceID);
    printf("User-client ABI:  %u\n", info.abiVersion);
    printf("PCI revision:     %02x\n", info.pciRevision & 0xffu);
    printf("Board profile:    %s\n",
           TimeCardBoardProfileName(info.boardProfile));
    printf("Driver version:   %u.%u\n", info.driverVersion >> 16,
           info.driverVersion & 0xffffu);
    printf("BAR0 size:        0x%" PRIx64 "\n", info.barSize);
    if (info.advertisedMSIXVectors != 0) {
        printf("MSI-X capacity:   %u advertised (not configured)\n",
               info.advertisedMSIXVectors);
    } else {
        printf("MSI-X capacity:   not advertised; interrupts not configured\n");
    }
    printf("Register layout:  %s\n",
           TimeCardRegisterLayoutName(info.layout));
    printf("Capabilities:     %s%s%s%s%s%s%s%s%s%s%s%s\n",
           (info.capabilities & kTimeCardCapabilityReadClock) != 0 ?
               "clock-read" : "no-clock-read",
           (info.capabilities & kTimeCardCapabilitySetClock) != 0 ?
               ", clock-set" : "",
           (info.capabilities & kTimeCardCapabilityCrossTimestamp) != 0 ?
               ", cross-timestamp" : "",
           (info.capabilities & kTimeCardCapabilityTOD) != 0 ?
               ", TOD" : "",
           (info.capabilities & kTimeCardCapabilitySMA) != 0 ?
               ", SMA" : "",
           (info.capabilities & kTimeCardCapabilityLED) != 0 ?
               ", LEDs" : "",
           (info.capabilities & kTimeCardCapabilityI2C) != 0 ?
               ", I2C" : "",
           (info.capabilities & kTimeCardCapabilitySensors) != 0 ?
               ", sensors" : "",
           (info.capabilities & kTimeCardCapabilityUART) != 0 ?
               ", UART" : "",
           (info.capabilities & kTimeCardCapabilityClockSource) != 0 ?
               ", clock-source" : "",
           (info.capabilities & kTimeCardCapabilityFrequency) != 0 ?
               ", frequency-counters" : "",
           (info.capabilities & kTimeCardCapabilityIMU) != 0 ?
               ", fused-IMU" : "");
    printf("Clock offset:     0x%" PRIx64 "\n", info.clockOffset);
    if ((info.validFields & kTimeCardInfoValidClockVersion) != 0)
        printf("Clock version:    0x%08x\n", info.clockVersion);
    else
        printf("Clock version:    unavailable\n");
    if ((info.validFields & kTimeCardInfoValidClockStatus) != 0)
        printf("Clock status:     0x%08x (%s)\n", info.clockStatus,
               (info.clockStatus & 1u) ? "in sync" : "not in sync");
    else
        printf("Clock status:     unavailable for this core version\n");
    if ((info.validFields & kTimeCardInfoValidClockSelect) != 0)
        printf("Clock source:     0x%02x\n",
               TimeCardConfiguredClockSource(info.clockSelect));
    else
        printf("Clock source:     unavailable\n");

    if ((info.capabilities & kTimeCardCapabilityTOD) == 0) {
        printf("TOD block:        not present on this board profile\n");
    } else {
        printf("TOD offset:       0x%" PRIx64 "\n", info.todOffset);
        if ((info.validFields & kTimeCardInfoValidTODVersion) != 0)
            printf("TOD version:      0x%08x\n", info.todVersion);
        else
            printf("TOD version:      unavailable\n");
        if ((info.validFields & kTimeCardInfoValidTODStatus) != 0)
            printf("TOD status:       0x%08x\n", info.todStatus);
        else
            printf("TOD status:       unavailable for this core version\n");
        if ((info.validFields & kTimeCardInfoValidUTCStatus) != 0 &&
            (info.validFields & kTimeCardInfoValidLeap) != 0) {
            const bool utcValid = (info.utcStatus & (1u << 8)) != 0;
            const bool leapValid = (info.utcStatus & (1u << 16)) != 0;
            printf("UTC status:       0x%08x (%s, offset +%" PRIu32
                   " s)\n", info.utcStatus,
                   utcValid ? "valid" : "not valid",
                   info.utcStatus & 0xffu);
            printf("Leap status:      0x%08x (%s, next %" PRId32
                   " s)\n", info.leap,
                   leapValid ? "valid" : "not valid",
                   (int32_t)info.leap);
        } else {
            printf("UTC/leap:         not exposed by this FPGA image\n");
        }
        if ((info.validFields & kTimeCardInfoValidGNSSStatus) != 0 &&
            (info.validFields & kTimeCardInfoValidSatellites) != 0) {
            const bool fixValid = (info.gnssStatus & (1u << 28)) != 0;
            const bool fixOk = (info.gnssStatus & (1u << 16)) != 0;
            const uint32_t fixCode = (info.gnssStatus >> 17) & 0xffu;
            const bool satelliteValid =
                (info.satellites & (1u << 16)) != 0;
            printf("GNSS status:      0x%08x (%s, %s%s)\n",
                   info.gnssStatus, gnss_fix_name(fixCode),
                   fixOk ? "fix OK" : "fix not asserted",
                   fixValid ? "" : ", validity bit clear");
            printf("Satellites:       0x%08x (seen %" PRIu32
                   ", locked %" PRIu32 ", %s)\n",
                   info.satellites, info.satellites & 0xffu,
                   (info.satellites >> 8) & 0xffu,
                   satelliteValid ? "valid" : "not valid");
        } else {
            printf("GNSS summary:     not exposed by this FPGA image\n");
        }
    }
    return 0;
}

static const char *
sma_direction_name(uint32_t direction)
{
    switch (direction) {
    case kTimeCardSMADirectionInput:
        return "input";
    case kTimeCardSMADirectionOutput:
        return "output";
    case kTimeCardSMADirectionDisabled:
        return "disabled";
    default:
        return "unknown";
    }
}

static void
print_sma(const TimeCardSMAControl *control)
{
    printf("SMA %u: %-8s function 0x%04x, input 0x%04x, output 0x%04x%s%s\n",
           control->connector, sma_direction_name(control->direction),
           control->function, control->inputMap, control->outputMap,
           (control->flags & kTimeCardSMAFlagFixedDirection) != 0 ?
               " (fixed direction)" : "",
           (control->flags & kTimeCardSMAFlagDisabled) != 0 ?
               " (disabled)" : "");
}

static unsigned long
parse_ulong(const char *text, const char *name)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        fprintf(stderr, "timecardctl: invalid %s: %s\n", name, text);
        exit(2);
    }
    return value;
}

static uint64_t
monotonic_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000ull +
        (uint64_t)now.tv_nsec / 1000000ull;
}

static const char *
uart_port_name(uint32_t port)
{
    switch (port) {
    case TIMECARD_UART_GNSS:
        return "GNSS";
    case TIMECARD_UART_GNSS2:
        return "GNSS2";
    case TIMECARD_UART_MAC:
        return "MAC/atomic";
    case TIMECARD_UART_NMEA:
        return "NMEA";
    default:
        return "unknown";
    }
}

static uint32_t
parse_uart_port(const char *text)
{
    unsigned long port = parse_ulong(text, "UART port");
    if (port >= TIMECARD_UART_COUNT) {
        fprintf(stderr, "timecardctl: UART port must be 0 through %u\n",
                TIMECARD_UART_COUNT - 1u);
        exit(2);
    }
    return (uint32_t)port;
}

static int
hex_value(char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static uint32_t
parse_uart_hex_bytes(const char *text, uint8_t *data)
{
    uint32_t length = 0;
    int highNibble = -1;

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '0' &&
            (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            ++i;
            continue;
        }
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' ||
            text[i] == ',' || text[i] == ':' || text[i] == '-')
            continue;

        const int value = hex_value(text[i]);
        if (value < 0) {
            fprintf(stderr, "timecardctl: invalid hex byte string: %s\n",
                    text);
            exit(2);
        }
        if (highNibble < 0) {
            highNibble = value;
            continue;
        }
        if (length >= TIMECARD_UART_MAX_TRANSFER) {
            fprintf(stderr,
                    "timecardctl: UART write length must be 1 through %u "
                    "bytes\n",
                    TIMECARD_UART_MAX_TRANSFER);
            exit(2);
        }
        data[length++] = (uint8_t)((highNibble << 4) | value);
        highNibble = -1;
    }

    if (highNibble >= 0) {
        fprintf(stderr, "timecardctl: odd number of hex digits: %s\n", text);
        exit(2);
    }
    if (length == 0) {
        fprintf(stderr, "timecardctl: UART write requires at least one byte\n");
        exit(2);
    }
    return length;
}

typedef struct TimeCardUBXPollSpec {
    const char *name;
    uint8_t messageClass;
    uint8_t messageID;
} TimeCardUBXPollSpec;

static const TimeCardUBXPollSpec kTimeCardUBXPolls[] = {
    {"mon-ver", 0x0au, 0x04u},
    {"mon-hw", 0x0au, 0x09u},
    {"mon-hw2", 0x0au, 0x0bu},
    {"nav-status", 0x01u, 0x03u},
    {"nav-pvt", 0x01u, 0x07u},
    {"nav-dop", 0x01u, 0x04u},
    {"nav-clock", 0x01u, 0x22u},
    {"nav-timegps", 0x01u, 0x20u},
    {"nav-timeutc", 0x01u, 0x21u},
    {"nav-timels", 0x01u, 0x26u},
    {"nav-sat", 0x01u, 0x35u},
    {"nav-svin", 0x01u, 0x3bu},
    {"tim-tp", 0x0du, 0x01u},
};

static const TimeCardUBXPollSpec *
find_ubx_poll(const char *name)
{
    for (size_t i = 0;
         i < sizeof(kTimeCardUBXPolls) / sizeof(kTimeCardUBXPolls[0]);
         ++i) {
        if (strcasecmp(name, kTimeCardUBXPolls[i].name) == 0)
            return &kTimeCardUBXPolls[i];
    }
    return NULL;
}

static void
build_ubx_poll(const TimeCardUBXPollSpec *poll, uint8_t packet[8])
{
    packet[0] = 0xb5u;
    packet[1] = 0x62u;
    packet[2] = poll->messageClass;
    packet[3] = poll->messageID;
    packet[4] = 0x00u;
    packet[5] = 0x00u;

    uint8_t checksumA = 0;
    uint8_t checksumB = 0;
    for (size_t i = 2; i < 6; ++i) {
        checksumA = (uint8_t)(checksumA + packet[i]);
        checksumB = (uint8_t)(checksumB + checksumA);
    }
    packet[6] = checksumA;
    packet[7] = checksumB;
}

static void
print_uart_observation(const TimeCardUARTObserve *observe)
{
    printf("UART %u (%s):    %s, %s, LSR 0x%02x, timeout %u ms\n",
           observe->port, uart_port_name(observe->port),
           (observe->flags & kTimeCardUARTObserveFlagPresent) != 0 ?
               "present" : "not-present",
           (observe->flags & kTimeCardUARTObserveFlagActivity) != 0 ?
               "activity" : "idle",
           observe->lineStatus & 0xffu, observe->timeoutMilliseconds);
}

static int
command_uart_observe(io_connect_t connection, int argc, char **argv)
{
    if (argc < 3 || argc > 4)
        return 2;
    if (require_driver_abi(connection, TIMECARD_UART_ABI_VERSION,
                           "UART access"))
        return 1;
    TimeCardUARTObserve observe = {
        .size = sizeof(observe),
        .port = parse_uart_port(argv[2]),
        .timeoutMilliseconds = argc == 4 ?
            (uint32_t)parse_ulong(argv[3], "UART timeout") : 100u,
    };
    if (call_inout(connection, kTimeCardMethodUARTObserve,
                   &observe, sizeof(observe), &observe, sizeof(observe)))
        return 1;
    print_uart_observation(&observe);
    return 0;
}

static int
command_uart_config(io_connect_t connection, int argc, char **argv)
{
    if (argc != 4)
        return 2;
    if (require_driver_abi(connection, TIMECARD_UART_ABI_VERSION,
                           "UART access"))
        return 1;
    TimeCardUARTConfig config = {
        .port = parse_uart_port(argv[2]),
        .baud = (uint32_t)parse_ulong(argv[3], "UART baud"),
    };
    kern_return_t result = IOConnectCallStructMethod(
        connection, kTimeCardMethodUARTConfigure,
        &config, sizeof(config), NULL, NULL);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "timecardctl: UART configure failed: 0x%08x\n",
                result);
        return 1;
    }
    printf("UART %u (%s):    configured for %u baud, 8N1\n",
           config.port, uart_port_name(config.port), config.baud);
    return 0;
}

static void
print_uart_bytes(const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i += 16u) {
        const uint32_t lineLength =
            length - i > 16u ? 16u : length - i;
        printf("%04x:", i);
        for (uint32_t j = 0; j < lineLength; ++j)
            printf(" %02x", data[i + j]);
        for (uint32_t j = lineLength; j < 16u; ++j)
            printf("   ");
        printf("  ");
        for (uint32_t j = 0; j < lineLength; ++j) {
            const uint8_t byte = data[i + j];
            putchar(byte >= 0x20u && byte <= 0x7eu ? byte : '.');
        }
        putchar('\n');
    }
}

static void
print_uart_transfer(const TimeCardUARTTransfer *transfer)
{
    printf("UART %u (%s):    read %u byte%s, LSR 0x%02x, timeout %u ms\n",
           transfer->port, uart_port_name(transfer->port), transfer->length,
           transfer->length == 1 ? "" : "s",
           transfer->lineStatus & 0xffu, transfer->timeoutMilliseconds);
    print_uart_bytes(transfer->data, transfer->length);
}

static int
command_uart_read(io_connect_t connection, int argc, char **argv)
{
    if (argc < 3 || argc > 5)
        return 2;
    if (require_driver_abi(connection, TIMECARD_UART_ABI_VERSION,
                           "UART access"))
        return 1;
    TimeCardUARTReadRequest request = {
        .port = parse_uart_port(argv[2]),
        .maximumBytes = argc >= 4 ?
            (uint32_t)parse_ulong(argv[3], "UART read length") : 128u,
        .timeoutMilliseconds = argc >= 5 ?
            (uint32_t)parse_ulong(argv[4], "UART timeout") : 100u,
    };
    if (request.maximumBytes == 0 ||
        request.maximumBytes > TIMECARD_UART_MAX_TRANSFER)
        return 2;
    TimeCardUARTTransfer transfer = {0};
    if (call_inout(connection, kTimeCardMethodUARTRead,
                   &request, sizeof(request), &transfer, sizeof(transfer)))
        return 1;
    print_uart_transfer(&transfer);
    return 0;
}

static int
command_uart_capture(io_connect_t connection, int argc, char **argv)
{
    if (argc < 3 || argc > 5)
        return 2;
    if (require_driver_abi(connection, TIMECARD_UART_ABI_VERSION,
                           "UART capture"))
        return 1;

    const uint32_t port = parse_uart_port(argv[2]);
    const unsigned long seconds = argc >= 4 ?
        parse_ulong(argv[3], "UART capture seconds") : 5u;
    if (seconds == 0 || seconds > 60u) {
        fprintf(stderr,
                "timecardctl: UART capture seconds must be 1 through 60\n");
        return 2;
    }

    TimeCardUARTConfig config = {
        .port = port,
        .baud = argc >= 5 ?
            (uint32_t)parse_ulong(argv[4], "UART baud") : 115200u,
    };
    kern_return_t result = IOConnectCallStructMethod(
        connection, kTimeCardMethodUARTConfigure,
        &config, sizeof(config), NULL, NULL);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "timecardctl: UART configure failed: 0x%08x\n",
                result);
        return 1;
    }

    printf("UART %u (%s):    capturing for %lu s at %u baud\n",
           port, uart_port_name(port), seconds, config.baud);

    uint8_t captured[TIMECARD_UART_CAPTURE_MAX_BYTES];
    uint32_t capturedLength = 0;
    uint32_t readWindows = 0;
    uint32_t emptyWindows = 0;
    uint32_t lastLineStatus = 0;
    const uint64_t started = monotonic_milliseconds();
    const uint64_t deadline = started + seconds * 1000ull;

    while (monotonic_milliseconds() < deadline &&
           capturedLength < TIMECARD_UART_CAPTURE_MAX_BYTES) {
        const uint64_t now = monotonic_milliseconds();
        if (now >= deadline)
            break;
        const uint64_t remaining = deadline - now;
        const uint32_t chunk =
            remaining > 250ull ? 250u : (uint32_t)remaining;
        TimeCardUARTReadRequest request = {
            .port = port,
            .maximumBytes = TIMECARD_UART_MAX_TRANSFER,
            .timeoutMilliseconds = chunk == 0u ? 1u : chunk,
        };
        if (TIMECARD_UART_CAPTURE_MAX_BYTES - capturedLength <
            TIMECARD_UART_MAX_TRANSFER) {
            request.maximumBytes =
                TIMECARD_UART_CAPTURE_MAX_BYTES - capturedLength;
        }

        TimeCardUARTTransfer response = {0};
        if (call_inout(connection, kTimeCardMethodUARTRead,
                       &request, sizeof(request),
                       &response, sizeof(response)))
            return 1;
        ++readWindows;
        lastLineStatus = response.lineStatus;
        if (response.length == 0u) {
            ++emptyWindows;
            continue;
        }

        uint32_t copyLength = response.length;
        if (copyLength > TIMECARD_UART_CAPTURE_MAX_BYTES - capturedLength)
            copyLength = TIMECARD_UART_CAPTURE_MAX_BYTES - capturedLength;
        memcpy(&captured[capturedLength], response.data, copyLength);
        capturedLength += copyLength;
    }

    const uint64_t stopped = monotonic_milliseconds();
    printf(
        "UART %u (%s):    captured %u byte%s in %.2f s, "
        "%u read window%s, %u empty, LSR 0x%02x%s\n",
        port, uart_port_name(port), capturedLength,
        capturedLength == 1u ? "" : "s",
        (double)(stopped - started) / 1000.0,
        readWindows, readWindows == 1u ? "" : "s",
        emptyWindows, lastLineStatus & 0xffu,
        capturedLength >= TIMECARD_UART_CAPTURE_MAX_BYTES ?
            ", byte limit reached" : "");
    if (capturedLength != 0u)
        print_uart_bytes(captured, capturedLength);
    return 0;
}

static int
command_uart_write_hex(io_connect_t connection, int argc, char **argv)
{
    if (argc < 4 || argc > 5)
        return 2;
    if (require_driver_abi(connection, TIMECARD_UART_WRITE_ABI_VERSION,
                           "UART write"))
        return 1;

    TimeCardUARTTransfer request = {
        .port = parse_uart_port(argv[2]),
        .timeoutMilliseconds = argc == 5 ?
            (uint32_t)parse_ulong(argv[4], "UART timeout") : 100u,
    };
    request.length = parse_uart_hex_bytes(argv[3], request.data);

    TimeCardUARTTransfer response = {0};
    if (call_inout(connection, kTimeCardMethodUARTWrite,
                   &request, sizeof(request), &response, sizeof(response)))
        return 1;
    printf(
        "UART %u (%s):    wrote %u/%u byte%s, LSR 0x%02x, timeout %u ms\n",
        response.port, uart_port_name(response.port), response.length,
        request.length, request.length == 1 ? "" : "s",
        response.lineStatus & 0xffu, response.timeoutMilliseconds);
    return response.length == request.length ? 0 : 1;
}

static int
command_ubx_poll_read(io_connect_t connection, int argc, char **argv)
{
    if (argc < 4 || argc > 6)
        return 2;
    if (require_driver_abi(connection, TIMECARD_UART_WRITE_ABI_VERSION,
                           "UBX poll/read"))
        return 1;

    const uint32_t port = parse_uart_port(argv[2]);
    const TimeCardUBXPollSpec *poll = find_ubx_poll(argv[3]);
    if (poll == NULL) {
        fprintf(stderr,
                "timecardctl: unknown UBX poll '%s'; expected mon-ver, "
                "mon-hw, mon-hw2, nav-status, nav-pvt, nav-dop, nav-clock, "
                "nav-timegps, nav-timeutc, nav-timels, nav-sat, nav-svin, "
                "or tim-tp\n",
                argv[3]);
        return 2;
    }

    TimeCardUARTConfig config = {
        .port = port,
        .baud = argc >= 5 ?
            (uint32_t)parse_ulong(argv[4], "UART baud") : 115200u,
    };
    kern_return_t result = IOConnectCallStructMethod(
        connection, kTimeCardMethodUARTConfigure,
        &config, sizeof(config), NULL, NULL);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "timecardctl: UART configure failed: 0x%08x\n",
                result);
        return 1;
    }

    uint8_t packet[8];
    build_ubx_poll(poll, packet);

    TimeCardUARTTransfer writeRequest = {
        .port = port,
        .length = sizeof(packet),
        .timeoutMilliseconds = 500u,
    };
    memcpy(writeRequest.data, packet, sizeof(packet));
    TimeCardUARTTransfer writeResponse = {0};
    if (call_inout(connection, kTimeCardMethodUARTWrite,
                   &writeRequest, sizeof(writeRequest),
                   &writeResponse, sizeof(writeResponse)))
        return 1;

    printf("UBX %s poll:   ", poll->name);
    for (size_t i = 0; i < sizeof(packet); ++i)
        printf("%s%02x", i == 0 ? "" : " ", packet[i]);
    putchar('\n');
    printf(
        "UART %u (%s):    wrote %u/%u bytes, LSR 0x%02x\n",
        writeResponse.port, uart_port_name(writeResponse.port),
        writeResponse.length, writeRequest.length,
        writeResponse.lineStatus & 0xffu);
    if (writeResponse.length != writeRequest.length)
        return 1;

    const uint32_t timeoutMilliseconds = argc >= 6 ?
        (uint32_t)parse_ulong(argv[5], "UBX read timeout") : 1500u;
    uint32_t elapsed = 0;
    TimeCardUARTTransfer lastRead = {0};
    while (elapsed < timeoutMilliseconds) {
        const uint32_t remaining = timeoutMilliseconds - elapsed;
        const uint32_t chunk = remaining > 250u ? 250u : remaining;
        TimeCardUARTReadRequest readRequest = {
            .port = port,
            .maximumBytes = TIMECARD_UART_MAX_TRANSFER,
            .timeoutMilliseconds = chunk,
        };
        TimeCardUARTTransfer readResponse = {0};
        if (call_inout(connection, kTimeCardMethodUARTRead,
                       &readRequest, sizeof(readRequest),
                       &readResponse, sizeof(readResponse)))
            return 1;
        lastRead = readResponse;
        if (readResponse.length != 0u) {
            print_uart_transfer(&readResponse);
            return 0;
        }
        elapsed += chunk;
    }

    printf("UBX %s poll:   no response bytes within %u ms, last LSR 0x%02x\n",
           poll->name, timeoutMilliseconds, lastRead.lineStatus & 0xffu);
    return 0;
}

static int
command_sma(io_connect_t connection, int argc, char **argv)
{
    unsigned long first = 1;
    unsigned long last = TIMECARD_SMA_COUNT;
    if (argc > 3)
        return 2;
    if (argc == 3)
        first = last = parse_ulong(argv[2], "SMA connector");
    if (first == 0 || last > TIMECARD_SMA_COUNT)
        return 2;

    for (unsigned long connector = first; connector <= last; connector++) {
        TimeCardSMAControl control = {
            .size = sizeof(control),
            .connector = (uint32_t)connector,
        };
        if (call_inout(connection, kTimeCardMethodSMAQuery,
                       &control, sizeof(control), &control,
                       sizeof(control)))
            return 1;
        print_sma(&control);
    }
    return 0;
}

static int
command_sma_set(io_connect_t connection, int argc, char **argv)
{
    if (argc < 4 || argc > 5)
        return 2;

    TimeCardSMAControl control = {
        .size = sizeof(control),
        .connector = (uint32_t)parse_ulong(argv[2], "SMA connector"),
    };
    if (strcmp(argv[3], "input") == 0)
        control.direction = kTimeCardSMADirectionInput;
    else if (strcmp(argv[3], "output") == 0)
        control.direction = kTimeCardSMADirectionOutput;
    else if (strcmp(argv[3], "disabled") == 0)
        control.direction = kTimeCardSMADirectionDisabled;
    else
        return 2;

    if (control.direction == kTimeCardSMADirectionDisabled) {
        if (argc != 4)
            return 2;
    } else {
        if (argc != 5)
            return 2;
        control.function = (uint32_t)parse_ulong(argv[4], "SMA function");
    }

    if (call_inout(connection, kTimeCardMethodSMASet,
                   &control, sizeof(control), &control, sizeof(control)))
        return 1;
    print_sma(&control);
    return 0;
}

static const char *
led_name(uint32_t led)
{
    switch (led) {
    case TIMECARD_LED_GNSS1:
        return "GNSS 1";
    case TIMECARD_LED_GNSS2:
        return "GNSS 2";
    case TIMECARD_LED_SMA1:
        return "SMA 1";
    case TIMECARD_LED_SMA2:
        return "SMA 2";
    case TIMECARD_LED_SMA3:
        return "SMA 3";
    case TIMECARD_LED_SMA4:
        return "SMA 4";
    default:
        return "unknown";
    }
}

static void
print_led(const TimeCardLEDControl *control)
{
    printf("LED %u (%s): %s%s, RGB %u/%u/%u, current %u",
           control->led + 1u, led_name(control->led),
           (control->flags & kTimeCardLEDFlagPresent) != 0 ?
               "present" : "not present",
           (control->flags & kTimeCardLEDFlagEnabled) != 0 ?
               ", enabled" : "",
           control->red, control->green, control->blue,
           control->globalCurrent);
    if ((control->flags & kTimeCardLEDFlagFaultValid) != 0) {
        printf(", open 0x%05x, short 0x%05x",
               control->openOutputMask & kTimeCardLEDOutputMask,
               control->shortOutputMask & kTimeCardLEDOutputMask);
    }
    printf(", mux 0x%02x, controller 0x%02x, interrupts 0x%02x\n",
           control->muxChannelMask, control->controllerStatus,
           control->interruptStatus);
}

static int
command_led(io_connect_t connection, int argc, char **argv)
{
    unsigned long first = 1;
    unsigned long last = TIMECARD_LED_COUNT;
    if (argc > 3)
        return 2;
    if (argc == 3)
        first = last = parse_ulong(argv[2], "LED");
    if (first == 0 || last > TIMECARD_LED_COUNT)
        return 2;

    for (unsigned long led = first; led <= last; led++) {
        TimeCardLEDControl control = {
            .size = sizeof(control),
            .led = (uint32_t)(led - 1u),
        };
        if (call_inout(connection, kTimeCardMethodLEDQuery,
                       &control, sizeof(control), &control,
                       sizeof(control)))
            return 1;
        print_led(&control);
    }
    return 0;
}

static int
command_led_set(io_connect_t connection, int argc, char **argv)
{
    if (argc < 6 || argc > 7)
        return 2;

    TimeCardLEDControl control = {
        .size = sizeof(control),
        .led = (uint32_t)(parse_ulong(argv[2], "LED") - 1u),
        .red = (uint32_t)parse_ulong(argv[3], "red"),
        .green = (uint32_t)parse_ulong(argv[4], "green"),
        .blue = (uint32_t)parse_ulong(argv[5], "blue"),
        .globalCurrent = argc == 7 ?
            (uint32_t)parse_ulong(argv[6], "current") : 96u,
    };
    if (control.led >= TIMECARD_LED_COUNT || control.red > UINT8_MAX ||
        control.green > UINT8_MAX || control.blue > UINT8_MAX ||
        control.globalCurrent > kTimeCardLEDMaxGlobalCurrent) {
        return 2;
    }

    if (call_inout(connection, kTimeCardMethodLEDSet,
                   &control, sizeof(control), &control, sizeof(control)))
        return 1;
    print_led(&control);
    return 0;
}

static void
sma_led_color(const TimeCardSMAControl *sma, TimeCardLEDControl *led)
{
    led->red = 130u;
    led->green = 55u;
    led->blue = 0u;
    led->globalCurrent = 96u;
    if ((sma->flags & kTimeCardSMAFlagDisabled) != 0 ||
        sma->direction == kTimeCardSMADirectionDisabled) {
        led->red = 50u;
        led->green = 35u;
        led->blue = 0u;
    } else if (sma->direction == kTimeCardSMADirectionInput) {
        led->red = 0u;
        led->green = 65u;
        led->blue = 180u;
    } else if (sma->direction == kTimeCardSMADirectionOutput) {
        led->red = 0u;
        led->green = 165u;
        led->blue = 30u;
    }
}

static void
gnss1_led_color(const TimeCardInfo *info, TimeCardLEDControl *led,
                const char **state)
{
    led->led = TIMECARD_LED_GNSS1;
    led->globalCurrent = 96u;
    led->red = 145u;
    led->green = 64u;
    led->blue = 0u;
    *state = "STATUS UNKNOWN";

    if ((info->validFields & kTimeCardInfoValidGNSSStatus) != 0 &&
        (info->validFields & kTimeCardInfoValidSatellites) != 0 &&
        info->gnssStatus != UINT32_MAX && info->satellites != UINT32_MAX) {
        const bool fixValid = (info->gnssStatus & (1u << 28)) != 0;
        const bool fixOk = (info->gnssStatus & (1u << 16)) != 0;
        const bool satValid = (info->satellites & (1u << 16)) != 0;
        const bool anySatellite = (info->satellites & 0xffu) != 0;

        if (fixValid && fixOk) {
            led->red = 0u;
            led->green = 180u;
            led->blue = 30u;
            *state = "FIX LOCKED";
        } else if (satValid && anySatellite) {
            led->red = 170u;
            led->green = 78u;
            led->blue = 0u;
            *state = "SEARCHING";
        } else {
            led->red = 180u;
            led->green = 0u;
            led->blue = 0u;
            *state = "NO FIX";
        }
        return;
    }

    if ((info->validFields & kTimeCardInfoValidClockStatus) != 0 &&
        (info->clockStatus & 1u) != 0) {
        led->red = 0u;
        led->green = 180u;
        led->blue = 30u;
        *state = "CLOCK SYNC FALLBACK";
    }
}

static void
gnss2_led_color(TimeCardLEDControl *led, const char **state)
{
    led->led = TIMECARD_LED_GNSS2;
    led->globalCurrent = 96u;
    led->red = 145u;
    led->green = 64u;
    led->blue = 0u;
    *state = "STATUS UNKNOWN";
}

static int
set_policy_led(io_connect_t connection, TimeCardLEDControl *led,
               const char *prefix, bool optional)
{
    led->size = sizeof(*led);
    kern_return_t result = call_inout_result(
        connection, kTimeCardMethodLEDSet, led, sizeof(*led), led,
        sizeof(*led));
    if (result == KERN_SUCCESS) {
        printf("%s -> ", prefix);
        print_led(led);
        return 0;
    }
    if (optional && result == kIOReturnUnsupported) {
        printf("%s -> LED not fitted on this board\n", prefix);
        return 0;
    }
    fprintf(stderr, "timecardctl: method %u failed: 0x%08x\n",
            kTimeCardMethodLEDSet, result);
    return 1;
}

static int
command_led_sma_auto(io_connect_t connection, int argc, char **argv)
{
    (void)argv;
    if (argc != 2)
        return 2;
    for (uint32_t connector = 1; connector <= TIMECARD_SMA_COUNT;
         connector++) {
        TimeCardSMAControl sma = {
            .size = sizeof(sma),
            .connector = connector,
        };
        TimeCardLEDControl led = {
            .size = sizeof(led),
            .led = TIMECARD_LED_SMA1 + connector - 1u,
        };
        if (call_inout(connection, kTimeCardMethodSMAQuery,
                       &sma, sizeof(sma), &sma, sizeof(sma)))
            return 1;
        sma_led_color(&sma, &led);
        char prefix[32];
        snprintf(prefix, sizeof(prefix), "SMA %u %-8s", connector,
                 sma_direction_name(sma.direction));
        if (set_policy_led(connection, &led, prefix, false))
            return 1;
    }
    return 0;
}

static int
command_led_gnss_auto(io_connect_t connection, int argc, char **argv)
{
    (void)argv;
    TimeCardInfo info = {0};
    TimeCardLEDControl led = {0};
    const char *state = NULL;

    if (argc != 2)
        return 2;
    if (call_output(connection, kTimeCardMethodGetInfo, &info, sizeof(info)))
        return 1;
    if (!driver_abi_supported(
            info.abiVersion, TIMECARD_MIN_COMPATIBLE_ABI_VERSION))
        return report_unsupported_abi(
            info.abiVersion, TIMECARD_MIN_COMPATIBLE_ABI_VERSION,
            "GNSS LED policy");

    gnss1_led_color(&info, &led, &state);
    char prefix1[64];
    snprintf(prefix1, sizeof(prefix1), "GNSS 1 %s", state);
    if (set_policy_led(connection, &led, prefix1, false))
        return 1;

    led = (TimeCardLEDControl){0};
    gnss2_led_color(&led, &state);
    char prefix2[64];
    snprintf(prefix2, sizeof(prefix2), "GNSS 2 %s", state);
    if (set_policy_led(connection, &led, prefix2, true))
        return 1;
    return 0;
}

static int
command_led_auto(io_connect_t connection, int argc, char **argv)
{
    (void)argv;
    if (argc != 2)
        return 2;
    char *gnssArgv[] = {"timecardctl", "led-gnss-auto"};
    char *smaArgv[] = {"timecardctl", "led-sma-auto"};
    if (command_led_gnss_auto(connection, 2, gnssArgv))
        return 1;
    return command_led_sma_auto(connection, 2, smaArgv);
}

static const char *
i2c_bool(bool value)
{
    return value ? "yes" : "no";
}

static void
print_i2c_status_flags(uint32_t flags)
{
    printf("%s%s%s%s%s",
           (flags & kTimeCardI2CFlagPresent) != 0 ? "present" :
                                                     "not-present",
           (flags & kTimeCardI2CFlagEnabled) != 0 ? ", enabled" : "",
           (flags & kTimeCardI2CFlagBusBusy) != 0 ? ", bus-busy" : "",
           (flags & kTimeCardI2CFlagRxEmpty) != 0 ? ", rx-empty" : "",
           (flags & kTimeCardI2CFlagTxEmpty) != 0 ? ", tx-empty" : "");
}

static int
command_i2c_status(io_connect_t connection, int argc, char **argv)
{
    (void)argv;
    if (argc != 2)
        return 2;
    TimeCardI2CStatus status = {.size = sizeof(status)};
    if (call_output(connection, kTimeCardMethodI2CStatus, &status,
                    sizeof(status)))
        return 1;
    printf("I2C offset:        0x%" PRIx64 "\n", status.offset);
    printf("I2C flags:         ");
    print_i2c_status_flags(status.flags);
    putchar('\n');
    printf("I2C control:       0x%02x\n", status.control);
    printf("I2C status:        0x%02x\n", status.status);
    printf("I2C interrupts:    status 0x%02x, enable 0x%02x\n",
           status.interruptStatus, status.interruptEnable);
    printf("I2C FIFO:          TX %u, RX %u\n", status.txFifoOccupancy,
           status.rxFifoOccupancy);
    printf("Known devices:     mux %s, LED %s\n",
           i2c_bool((status.knownDeviceMask & kTimeCardI2CKnownDeviceMux) != 0),
           i2c_bool((status.knownDeviceMask & kTimeCardI2CKnownDeviceLED) != 0));
    return 0;
}

static int
command_i2c_scan(io_connect_t connection, int argc, char **argv)
{
    (void)argv;
    if (argc != 2)
        return 2;
    printf("I2C devices:");
    unsigned found = 0;
    for (uint32_t address = 0x08u; address <= 0x77u; address++) {
        TimeCardI2CProbe probe = {
            .size = sizeof(probe),
            .address = address,
        };
        if (call_inout(connection, kTimeCardMethodI2CProbe,
                       &probe, sizeof(probe), &probe, sizeof(probe)))
            return 1;
        if (probe.present != 0) {
            printf(" 0x%02x", address);
            found++;
        }
    }
    if (found == 0)
        printf(" none");
    putchar('\n');
    return 0;
}

static void
print_i2c_transfer(const TimeCardI2CTransfer *transfer)
{
    printf("I2C 0x%02x read %u byte%s, controller 0x%02x, interrupts 0x%02x\n",
           transfer->address, transfer->length,
           transfer->length == 1 ? "" : "s", transfer->controllerStatus,
           transfer->interruptStatus);
    for (uint32_t i = 0; i < transfer->length; i++) {
        if ((i % 16u) == 0)
            printf("%04x:", i);
        printf(" %02x", transfer->data[i]);
        if ((i % 16u) == 15u || i + 1u == transfer->length)
            putchar('\n');
    }
}

static int
command_i2c_read(io_connect_t connection, int argc, char **argv)
{
    if (argc < 3 || argc > 6)
        return 2;
    TimeCardI2CReadRequest request = {
        .size = sizeof(request),
        .address = (uint32_t)parse_ulong(argv[2], "I2C address"),
        .subaddress = argc >= 4 ?
            (uint32_t)parse_ulong(argv[3], "I2C subaddress") : 0,
        .length = argc >= 5 ?
            (uint32_t)parse_ulong(argv[4], "I2C read length") : 1,
        .subaddressLength = argc >= 6 ?
            (uint32_t)parse_ulong(argv[5], "I2C subaddress length") :
            (argc >= 4 ? 1u : 0u),
    };
    if (request.address < 0x08u || request.address > 0x77u ||
        request.subaddressLength > 2u || request.length == 0 ||
        request.length > TIMECARD_I2C_MAX_TRANSFER)
        return 2;
    TimeCardI2CTransfer transfer = {0};
    if (call_inout(connection, kTimeCardMethodI2CRead,
                   &request, sizeof(request), &transfer, sizeof(transfer)))
        return 1;
    print_i2c_transfer(&transfer);
    return 0;
}

static void
print_i2c_mux(const TimeCardI2CMuxControl *control)
{
    if (control->present == 0) {
        printf("I2C mux:           not present");
    } else {
        printf("I2C mux:           channel mask 0x%02x",
               control->channelMask);
    }
    printf(", controller 0x%02x, interrupts 0x%02x\n",
           control->controllerStatus, control->interruptStatus);
}

static int
command_i2c_mux(io_connect_t connection, int argc, char **argv)
{
    if (argc > 3)
        return 2;
    TimeCardI2CMuxControl control = {
        .size = sizeof(control),
        .channelMask = argc == 3 ?
            (uint32_t)parse_ulong(argv[2], "I2C mux channel mask") : 0,
    };
    if (argc == 2) {
        if (call_output(connection, kTimeCardMethodI2CMuxQuery, &control,
                        sizeof(control)))
            return 1;
    } else {
        if ((control.channelMask & ~kTimeCardI2CMuxChannelMask) != 0)
            return 2;
        if (call_inout(connection, kTimeCardMethodI2CMuxSet,
                       &control, sizeof(control), &control,
                       sizeof(control)))
            return 1;
    }
    print_i2c_mux(&control);
    return 0;
}

static const char *
sensor_type_name(uint32_t type)
{
    switch (type) {
    case kTimeCardSensorTypeLM75B:
        return "LM75B";
    case kTimeCardSensorTypeSHT3x:
        return "SHT3x";
    case kTimeCardSensorTypeICP10100:
        return "ICP-10100";
    case kTimeCardSensorTypeBME280:
        return "BME280/BMP280";
    case kTimeCardSensorTypeINA219:
        return "INA219";
    case kTimeCardSensorTypeBNO08x:
        return "BNO08x";
    case kTimeCardSensorTypeBNO055:
        return "BNO055";
    default:
        return "unknown";
    }
}

static void
print_sensor_capabilities(uint32_t capabilities)
{
    const struct {
        uint32_t flag;
        const char *name;
    } names[] = {
        {kTimeCardSensorCapabilityBME280, "BME280/BMP280"},
        {kTimeCardSensorCapabilityINA219, "INA219"},
        {kTimeCardSensorCapabilityBNO055, "BNO055"},
        {kTimeCardSensorCapabilityBNO08x, "BNO08x"},
        {kTimeCardSensorCapabilityLM75B, "LM75B"},
        {kTimeCardSensorCapabilitySHT3x, "SHT3x"},
        {kTimeCardSensorCapabilityICP10100, "ICP-10100"},
    };
    const char *separator = "";

    if (capabilities == 0) {
        printf("none");
        return;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if ((capabilities & names[i].flag) == 0)
            continue;
        printf("%s%s", separator, names[i].name);
        separator = ", ";
    }
}

static void
print_sensor_presence(const TimeCardSensorReading *reading)
{
    printf("%-18s mux 0x%02x addr 0x%02x: %s%s%s%s%s%s\n",
           sensor_type_name(reading->type), reading->muxChannelMask,
           reading->address,
           (reading->flags & kTimeCardSensorFlagPresent) != 0 ?
               "present" : "not present",
           (reading->flags & kTimeCardSensorFlagValid) != 0 ?
               ", valid" : "",
           (reading->flags & kTimeCardSensorFlagConfigured) != 0 ?
               ", configured" : "",
           (reading->flags & kTimeCardSensorFlagCRCValid) != 0 ?
               ", CRC OK" : "",
           (reading->flags & kTimeCardSensorFlagCalibrated) != 0 ?
               ", calibrated" : "",
           (reading->flags & kTimeCardSensorFlagIMU) != 0 ?
               ", IMU" : "");
}

static void
print_imu_probe_detail(const TimeCardSensorReading *reading)
{
    if ((reading->flags & kTimeCardSensorFlagIMU) == 0)
        return;

    if (reading->type == kTimeCardSensorTypeBNO08x) {
        const uint32_t channel = (reading->raw2 >> 8) & 0xffu;
        const uint32_t sequence = reading->raw2 & 0xffu;
        if ((reading->flags & kTimeCardSensorFlagPresent) != 0) {
            printf("  SHTP header:     length %" PRIu32
                   ", channel %" PRIu32 ", sequence %" PRIu32 "\n",
                   reading->raw0, channel, sequence);
        } else {
            printf("  SHTP probe:      result 0x%08" PRIx32
                   ", controller 0x%02" PRIx32 "\n",
                   reading->raw0, reading->raw1 & 0xffu);
        }
        return;
    }

    if (reading->type == kTimeCardSensorTypeBNO055) {
        if ((reading->flags & kTimeCardSensorFlagPresent) != 0) {
            printf("  chip ID:         0x%02" PRIx32 "%s\n",
                   reading->raw0 & 0xffu,
                   (reading->flags & kTimeCardSensorFlagValid) != 0 ?
                       " (valid)" : " (unexpected)");
        } else {
            printf("  chip ID probe:   no response, controller 0x%02" PRIx32
                   "\n", reading->raw1 & 0xffu);
        }
    }
}

static bool
icp10100_pressure_pascals(uint32_t rawPressure, uint32_t rawTemperature,
                          const int32_t otp[4], double *pressurePascals)
{
    const double t = (double)rawTemperature - 32768.0;
    const double quadratic = t * t / 16777216.0;
    const double s1 = 3.5 * 1048576.0 + otp[0] * quadratic;
    const double s2 = 2048.0 * otp[3] + otp[1] * quadratic;
    const double s3 = 11.5 * 1048576.0 + otp[2] * quadratic;
    const double denominator = s3 * (45000.0 - 80000.0) +
        s1 * (80000.0 - 105000.0) + s2 * (105000.0 - 45000.0);
    if (denominator > -0.000001 && denominator < 0.000001)
        return false;
    const double c = (s1 * s2 * (45000.0 - 80000.0) +
        s2 * s3 * (80000.0 - 105000.0) +
        s3 * s1 * (105000.0 - 45000.0)) / denominator;
    if (s1 - s2 > -0.000001 && s1 - s2 < 0.000001)
        return false;
    const double a = (45000.0 * s1 - 80000.0 * s2 - 35000.0 * c) /
        (s1 - s2);
    const double b = (45000.0 - a) * (s1 + c);
    const double pressure = a + b / (c + rawPressure);
    if (!(pressure >= 10000.0 && pressure <= 130000.0))
        return false;
    *pressurePascals = pressure;
    return true;
}

static int
command_sensors(io_connect_t connection, int argc, char **argv)
{
    (void)argv;
    if (argc != 2)
        return 2;
    TimeCardSensorTelemetry telemetry = {.size = sizeof(telemetry)};
    if (call_output(connection, kTimeCardMethodSensorQuery, &telemetry,
                    sizeof(telemetry)))
        return 1;

    printf("Sensor branch:     %s (prior mux 0x%02x, restored 0x%02x)\n",
           (telemetry.flags & kTimeCardSensorFlagPresent) != 0 ?
               "available" : "unavailable",
           telemetry.muxChannelMask, telemetry.restoredMuxChannelMask);
    printf("Controller/events: 0x%02x / 0x%08x\n",
           telemetry.controllerStatus, telemetry.interruptStatus);
    printf("Board profile:     %s\n",
           TimeCardBoardProfileName(telemetry.boardProfile));
    printf("Capabilities:      ");
    print_sensor_capabilities(telemetry.capabilities);
    printf("\n");

    unsigned validCount = 0;
    for (uint32_t i = 0; i < telemetry.readingCount &&
         i < TIMECARD_SENSOR_MAX_READINGS; i++) {
        const TimeCardSensorReading *reading = &telemetry.readings[i];
        print_sensor_presence(reading);
        print_imu_probe_detail(reading);
        if ((reading->flags & kTimeCardSensorFlagTemperature) != 0) {
            printf("  temperature:     %.3f C\n",
                   reading->temperatureMilliCelsius / 1000.0);
        }
        if ((reading->flags & kTimeCardSensorFlagHumidity) != 0) {
            printf("  humidity:        %.3f %%RH\n",
                   reading->humidityMilliPercent / 1000.0);
        }
        if ((reading->flags & kTimeCardSensorFlagPressure) != 0) {
            double pressurePascals = 0.0;
            if ((reading->flags & kTimeCardSensorFlagCalibrated) != 0 &&
                icp10100_pressure_pascals(
                    reading->pressureRaw, reading->raw1,
                    telemetry.icp10100Otp, &pressurePascals)) {
                printf("  pressure:        %.3f hPa\n",
                       pressurePascals / 100.0);
            } else {
                printf("  pressure raw:    0x%06x\n", reading->pressureRaw);
            }
        }
        if ((reading->flags & kTimeCardSensorFlagValid) != 0)
            validCount++;
    }
    if (validCount == 0) {
        fprintf(stderr, "timecardctl: no valid sensor samples were returned\n");
        return 1;
    }
    printf("%u valid sensor block(s).\n", validCount);
    return 0;
}

static int
command_get(io_connect_t connection)
{
    TimeCardCrossTimestamp timestamp = {0};

    if (call_output(connection, kTimeCardMethodGetCrossTimestamp,
                    &timestamp, sizeof(timestamp)))
        return 1;
    print_card_time(&timestamp.cardTime);
    printf("\nSystem sampling window: %" PRIu64 " ns\n",
           timestamp.systemTimeAfterNanoseconds -
               timestamp.systemTimeBeforeNanoseconds);
    return 0;
}

static int
command_set_card_from_system(io_connect_t connection)
{
    struct timespec now;
    TimeCardTime cardTime = {0};
    kern_return_t result;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        fprintf(stderr, "timecardctl: clock_gettime failed: %s\n",
                strerror(errno));
        return 1;
    }
    cardTime.seconds = (uint64_t)now.tv_sec;
    cardTime.nanoseconds = (uint32_t)now.tv_nsec;

    result = IOConnectCallStructMethod(connection, kTimeCardMethodSetTime,
                                       &cardTime, sizeof(cardTime), NULL, NULL);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "timecardctl: set-time failed: 0x%08x\n", result);
        return 1;
    }
    printf("Time Card set to ");
    print_card_time(&cardTime);
    putchar('\n');
    return 0;
}

static int
require_timing_capability(io_connect_t connection, uint64_t capability)
{
    TimeCardInfo info = {0};
    if (call_output(connection, kTimeCardMethodGetInfo, &info, sizeof(info))) return 1;
    const uint32_t minimumABI = capability == kTimeCardCapabilityPPS ? 12u : 10u;
    if (!driver_abi_supported(info.abiVersion, minimumABI))
        return report_unsupported_abi(info.abiVersion, minimumABI, "timing control");
    if ((info.capabilities & capability) == 0) {
        fprintf(stderr, "timecardctl: timing feature unavailable for this FPGA image. "
                "No optional timing registers were probed.\n");
        return 1;
    }
    return 0;
}

static void
print_clock_control(const TimeCardClockControl *clock)
{
    printf("Configured source: 0x%02x\nActive source:     0x%02x\n"
           "Supported mask:    0x%08x\nClock version:     0x%08x\n"
           "Clock control:     0x%08x\nClock status:      0x%08x\n",
           clock->source, clock->activeSource, clock->supportedSources,
           clock->clockVersion, clock->control, clock->status);
}

static int
command_clock_control(io_connect_t connection, int argc, char **argv, bool set)
{
    if ((!set && argc != 2) || (set && (argc < 3 || argc > 4))) return 2;
    unsigned long source = set ? parse_ulong(argv[2], "clock source") : 0;
    unsigned long expected = set && argc == 4 ? parse_ulong(argv[3], "expected source") : 0;
    if (source > 255 || expected > 255 || (set && TimeCardClockSourceBit((uint32_t)source) == 0)) {
        fprintf(stderr, "timecardctl: source must be 0...6, 0xfe, or 0xff\n");
        return 2;
    }
    if (require_timing_capability(connection, kTimeCardCapabilityClockSource)) return 1;
    TimeCardClockControl response = {0};
    if (call_output(connection, kTimeCardMethodClockControlQuery, &response, sizeof(response))) return 1;
    if (set) {
        TimeCardClockSourceRequest request = {
            .size = sizeof(request), .source = (uint32_t)source,
            .expectedSource = argc == 4 ? (uint32_t)expected : response.source,
        };
        if (call_inout(connection, kTimeCardMethodClockSourceSet, &request, sizeof(request),
                       &response, sizeof(response))) return 1;
        printf("Clock source readback verified.\n");
    }
    print_clock_control(&response);
    return 0;
}

static int
command_frequency(io_connect_t connection, int argc, char **argv, bool set)
{
    if ((!set && (argc < 2 || argc > 3)) || (set && argc != 4)) return 2;
    unsigned long first = argc >= 3 ? parse_ulong(argv[2], "counter") : 1;
    unsigned long last = argc >= 3 ? first : TIMECARD_FREQUENCY_COUNT;
    unsigned long seconds = set ? parse_ulong(argv[3], "integration seconds") : 0;
    if (first < 1 || last > 4 || seconds > 255) {
        fprintf(stderr, "timecardctl: counter must be 1...4; interval 0...255 seconds (0 disables)\n");
        return 2;
    }
    if (require_timing_capability(connection, kTimeCardCapabilityFrequency)) return 1;
    for (unsigned long counter = first; counter <= last; ++counter) {
        TimeCardFrequencyRequest request = {.size = sizeof(request), .counter = (uint32_t)counter};
        TimeCardFrequencyControl response = {0};
        if (call_inout(connection, kTimeCardMethodFrequencyQuery, &request, sizeof(request),
                       &response, sizeof(response))) return 1;
        if (set) {
            request.integrationSeconds = (uint32_t)seconds;
            request.expectedControl = response.control;
            if (call_inout(connection, kTimeCardMethodFrequencySet, &request, sizeof(request),
                           &response, sizeof(response))) return 1;
        }
        const bool enabled = (response.flags & kTimeCardFrequencyEnabled) != 0;
        const bool error = (response.flags & kTimeCardFrequencyError) != 0;
        const bool overrun = (response.flags & kTimeCardFrequencyOverrun) != 0;
        const bool valid = enabled && response.integrationSeconds > 0 && !error && !overrun &&
            (response.flags & kTimeCardFrequencyValid) != 0;
        printf("Counter %lu: %s, integration %u s, control 0x%08x, status 0x%08x\n",
               counter, !enabled ? "disabled" : error ? "error" : overrun ? "overrun" :
               valid ? "valid" : "waiting", response.integrationSeconds, response.control, response.status);
        if (valid) printf("Frequency: %u Hz\n", response.frequencyHz);
    }
    return 0;
}

static int command_pps(io_connect_t connection, int argc, char **argv)
{
    if (argc < 2 || argc > 3) return 2;
    const unsigned long first = argc == 3 ? parse_ulong(argv[2], "PPS core") : 1;
    const unsigned long last = argc == 3 ? first : 2;
    if (first < 1 || last > 2) return 2;
    if (require_timing_capability(connection, kTimeCardCapabilityPPS)) return 1;
    for (unsigned long core = first; core <= last; ++core) {
        TimeCardPPSQuery request = {.size = sizeof(request), .core = (uint32_t)core};
        TimeCardPPSState out = {0};
        if (call_inout(connection, kTimeCardMethodPPSQuery, &request, sizeof(request), &out, sizeof(out))) return 1;
        if (out.size != sizeof(out) || out.core != core || out.reserved) return 1;
        printf("PPS %lu (%s): version 0x%08x, valid fields 0x%02x, writable fields 0x%02x\n",
               core, core == 1 ? "output" : "input", out.version, out.validFields, out.writableFields);
        if (!out.validFields) { printf("  Core absent or version unrecognized; settings not probed.\n"); continue; }
        printf("  Engine: %s, control 0x%08x\n", out.control & 1 ? "enabled" : "disabled", out.control);
        if (out.validFields & kTimeCardPPSStatus) printf("  Status: 0x%08x\n", out.status);
        if (out.validFields & kTimeCardPPSPolarity) printf("  Polarity: active %s\n", out.polarity & 1 ? "high" : "low");
        const uint32_t width = out.pulseWidth & 0x3ff;
        if ((out.validFields & kTimeCardPPSWidth) && width >= 1 && width <= 999) printf("  Pulse width: %u ms\n", width);
        else printf("  Pulse width: unavailable\n");
        if (out.validFields & kTimeCardPPSDelay) {
            const int64_t magnitude = out.cableDelayRaw & out.maximumDelay;
            printf("  Cable delay: %" PRId64 " ns (limit +/-%u)\n", out.cableDelayRaw & 0x80000000u ? -magnitude : magnitude, out.maximumDelay);
        }
    }
    return 0;
}

static int command_imu(io_connect_t connection, int argc, char **argv)
{
    TimeCardIMURequest request = { .size = sizeof(request) };
    TimeCardIMUTelemetry response = {0};
    if (argc > 3) return 2;
    if (argc == 3) {
        if (strcmp(argv[2], "start") == 0) request.mode = 1;
        else if (strcmp(argv[2], "stop") == 0) request.mode = 2;
        else return 2;
    }
    if (call_inout(connection, kTimeCardMethodIMUQuery, &request, sizeof(request), &response, sizeof(response))) return 1;
    if (response.size != sizeof(response)) return 1;
    printf("IMU: %s, flags 0x%04x, route 0x%02x/0x%02x, restored mux 0x%02x\n",
           sensor_type_name(response.type), response.flags, response.muxChannelMask, response.address, response.restoredMuxChannelMask);
    printf("Sequence: %u, reports: %u, calibration: 0x%02x, system: 0x%04x\n",
           response.sampleSequence, response.reportCount, response.calibration, response.systemStatus);
    if ((response.flags & (kTimeCardIMUPresent | kTimeCardIMUConfigured | kTimeCardIMUMuxRestored | kTimeCardIMUReset)) !=
        (kTimeCardIMUPresent | kTimeCardIMUConfigured | kTimeCardIMUMuxRestored)) return 0;
    if (response.flags & kTimeCardIMURotation)
        printf("Quaternion x/y/z/w: %.6f %.6f %.6f %.6f\n", response.quaternionQ14[0]/16384.0,
               response.quaternionQ14[1]/16384.0, response.quaternionQ14[2]/16384.0, response.quaternionQ14[3]/16384.0);
    if (response.flags & kTimeCardIMULinearAcceleration)
        printf("Linear acceleration m/s2: %.6f %.6f %.6f\n", response.linearAccelerationQ8[0]/256.0,
               response.linearAccelerationQ8[1]/256.0, response.linearAccelerationQ8[2]/256.0);
    return 0;
}

int
main(int argc, char **argv)
{
    io_connect_t connection;
    int status;

    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }

    connection = open_timecard();
    if (strcmp(argv[1], "status") == 0 && argc == 2)
        status = command_status(connection);
    else if (strcmp(argv[1], "get") == 0 && argc == 2)
        status = command_get(connection);
    else if (strcmp(argv[1], "set-card-from-system") == 0 && argc == 2)
        status = command_set_card_from_system(connection);
    else if (strcmp(argv[1], "clock-control") == 0)
        status = command_clock_control(connection, argc, argv, false);
    else if (strcmp(argv[1], "clock-source") == 0)
        status = command_clock_control(connection, argc, argv, true);
    else if (strcmp(argv[1], "frequency") == 0)
        status = command_frequency(connection, argc, argv, false);
    else if (strcmp(argv[1], "pps") == 0)
        status = command_pps(connection, argc, argv);
    else if (strcmp(argv[1], "frequency-set") == 0)
        status = command_frequency(connection, argc, argv, true);
    else if (strcmp(argv[1], "sma") == 0)
        status = command_sma(connection, argc, argv);
    else if (strcmp(argv[1], "sma-set") == 0)
        status = command_sma_set(connection, argc, argv);
    else if (strcmp(argv[1], "led") == 0)
        status = command_led(connection, argc, argv);
    else if (strcmp(argv[1], "led-set") == 0)
        status = command_led_set(connection, argc, argv);
    else if (strcmp(argv[1], "led-sma-auto") == 0)
        status = command_led_sma_auto(connection, argc, argv);
    else if (strcmp(argv[1], "led-gnss-auto") == 0)
        status = command_led_gnss_auto(connection, argc, argv);
    else if (strcmp(argv[1], "led-auto") == 0)
        status = command_led_auto(connection, argc, argv);
    else if (strcmp(argv[1], "i2c-status") == 0)
        status = command_i2c_status(connection, argc, argv);
    else if (strcmp(argv[1], "i2c-scan") == 0)
        status = command_i2c_scan(connection, argc, argv);
    else if (strcmp(argv[1], "i2c-read") == 0)
        status = command_i2c_read(connection, argc, argv);
    else if (strcmp(argv[1], "i2c-mux") == 0)
        status = command_i2c_mux(connection, argc, argv);
    else if (strcmp(argv[1], "sensors") == 0)
        status = command_sensors(connection, argc, argv);
    else if (strcmp(argv[1], "imu") == 0)
        status = command_imu(connection, argc, argv);
    else if (strcmp(argv[1], "uart-observe") == 0)
        status = command_uart_observe(connection, argc, argv);
    else if (strcmp(argv[1], "uart-config") == 0)
        status = command_uart_config(connection, argc, argv);
    else if (strcmp(argv[1], "uart-read") == 0)
        status = command_uart_read(connection, argc, argv);
    else if (strcmp(argv[1], "uart-capture") == 0)
        status = command_uart_capture(connection, argc, argv);
    else if (strcmp(argv[1], "uart-write-hex") == 0)
        status = command_uart_write_hex(connection, argc, argv);
    else if (strcmp(argv[1], "ubx-poll-read") == 0)
        status = command_ubx_poll_read(connection, argc, argv);
    else {
        print_usage(stderr);
        status = 2;
    }

    IOServiceClose(connection);
    return status;
}
