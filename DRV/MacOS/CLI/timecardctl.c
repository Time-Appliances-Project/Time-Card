/* SPDX-License-Identifier: BSD-3-Clause */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "TimeCardABI.h"
#include "TimeCardRegisters.h"

static void
print_usage(FILE *stream)
{
    fprintf(stream,
            "usage: timecardctl status\n"
            "       timecardctl get\n"
            "       timecardctl set-card-from-system\n"
            "       timecardctl sma [connector]\n"
            "       timecardctl sma-set connector input|output function\n"
            "       timecardctl sma-set connector disabled\n");
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

static int
command_status(io_connect_t connection)
{
    TimeCardInfo info = {0};

    if (call_output(connection, kTimeCardMethodGetInfo, &info, sizeof(info)))
        return 1;
    if (info.abiVersion != TIMECARD_ABI_VERSION) {
        fprintf(stderr, "timecardctl: unsupported driver ABI %u\n",
                info.abiVersion);
        return 1;
    }

    printf("PCI device:       %04x:%04x\n", info.vendorID, info.deviceID);
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
    printf("Capabilities:     %s%s%s%s%s\n",
           (info.capabilities & kTimeCardCapabilityReadClock) != 0 ?
               "clock-read" : "no-clock-read",
           (info.capabilities & kTimeCardCapabilitySetClock) != 0 ?
               ", clock-set" : "",
           (info.capabilities & kTimeCardCapabilityCrossTimestamp) != 0 ?
               ", cross-timestamp" : "",
           (info.capabilities & kTimeCardCapabilityTOD) != 0 ?
               ", TOD" : "",
           (info.capabilities & kTimeCardCapabilitySMA) != 0 ?
               ", SMA" : "");
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
    }
    printf("Optional GNSS:    gated pending an exact FPGA image contract\n");
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
    else if (strcmp(argv[1], "sma") == 0)
        status = command_sma(connection, argc, argv);
    else if (strcmp(argv[1], "sma-set") == 0)
        status = command_sma_set(connection, argc, argv);
    else {
        print_usage(stderr);
        status = 2;
    }

    IOServiceClose(connection);
    return status;
}
