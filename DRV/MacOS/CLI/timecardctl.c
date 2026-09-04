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
            "       timecardctl sensors\n");
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
    printf("Capabilities:     %s%s%s%s%s%s%s%s\n",
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
               ", sensors" : "");
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
    if (info.abiVersion != TIMECARD_ABI_VERSION) {
        fprintf(stderr, "timecardctl: unsupported driver ABI %u\n",
                info.abiVersion);
        return 1;
    }

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
    else {
        print_usage(stderr);
        status = 2;
    }

    IOServiceClose(connection);
    return status;
}
