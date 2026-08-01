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

static void
print_usage(FILE *stream)
{
    fprintf(stream,
            "usage: timecardctl status\n"
            "       timecardctl get\n"
            "       timecardctl set-system\n");
}

static io_connect_t
open_timecard(void)
{
    CFMutableDictionaryRef matching = IOServiceMatching("IOUserService");
    CFMutableDictionaryRef propertyMatch = NULL;
    io_service_t service;
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

    service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    if (service == IO_OBJECT_NULL) {
        fprintf(stderr,
                "timecardctl: Time Card service not found; install the driver "
                "and verify PCI enumeration\n");
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
    printf("Driver version:   %u.%u\n", info.driverVersion >> 16,
           info.driverVersion & 0xffffu);
    printf("BAR0 size:        0x%" PRIx64 "\n", info.barSize);
    printf("Interrupts:       %u\n", info.interruptVectors);
    printf("Register layout:  %s\n",
           info.layout == kTimeCardLayoutMSIX ? "MSI-X" : "MSI");
    printf("Clock offset:     0x%" PRIx64 "\n", info.clockOffset);
    printf("Clock version:    0x%08x\n", info.clockVersion);
    printf("Clock status:     0x%08x (%s)\n", info.clockStatus,
           (info.clockStatus & 1u) ? "in sync" : "not in sync");
    printf("Clock source:     0x%04x\n", info.clockSelect >> 16);
    printf("TOD/GNSS status:  0x%08x / 0x%08x\n", info.todStatus,
           info.gnssStatus);
    printf("UTC/leap:         0x%08x / 0x%08x\n", info.utcStatus,
           info.leap);
    printf("Satellites:       0x%08x\n", info.satellites);
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
command_set_system(io_connect_t connection)
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

    if (argc != 2) {
        print_usage(stderr);
        return 2;
    }

    connection = open_timecard();
    if (strcmp(argv[1], "status") == 0)
        status = command_status(connection);
    else if (strcmp(argv[1], "get") == 0)
        status = command_get(connection);
    else if (strcmp(argv[1], "set-system") == 0)
        status = command_set_system(connection);
    else {
        print_usage(stderr);
        status = 2;
    }

    IOServiceClose(connection);
    return status;
}
