/* SPDX-License-Identifier: BSD-3-Clause */
/* Diagnostic/control utility for the OCP TimeCard Windows driver. */

#include <windows.h>
#include <winioctl.h>
#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timecard_ioctl.h"

#define FILETIME_UNIX_EPOCH_100NS 116444736000000000ull

static long parse_signed_long(const char *text, const char *name);

static unsigned __int32
parse_u32_bounded(const char *text, const char *name,
                  unsigned __int32 minimum, unsigned __int32 maximum)
{
    char *end;
    unsigned __int64 value;

    errno = 0;
    value = _strtoui64(text, &end, 0);
    if (*text == '\0' || *end != '\0' || errno == ERANGE ||
        value < minimum || value > maximum) {
        fprintf(stderr,
                "timecardctl: %s must be in the range %lu..%lu: %s\n",
                name, (unsigned long)minimum, (unsigned long)maximum, text);
        exit(2);
    }
    return (unsigned __int32)value;
}

static signed __int32
parse_i32_bounded(const char *text, const char *name,
                  signed __int32 minimum, signed __int32 maximum)
{
    char *end;
    signed __int64 value;

    errno = 0;
    value = _strtoi64(text, &end, 0);
    if (*text == '\0' || *end != '\0' || errno == ERANGE ||
        value < minimum || value > maximum) {
        fprintf(stderr,
                "timecardctl: %s must be in the range %ld..%ld: %s\n",
                name, (long)minimum, (long)maximum, text);
        exit(2);
    }
    return (signed __int32)value;
}

static signed __int64
parse_ppb_q16(const char *text)
{
    char *end;
    double value;
    double scaled;
    const double maximum = (double)TIMECARD_CLOCK_ADJUST_MAX_DRIFT_PPB;

    errno = 0;
    value = strtod(text, &end);
    if (*text == '\0' || *end != '\0' || errno == ERANGE ||
        !_finite(value) || value < -maximum || value > maximum) {
        fprintf(stderr,
                "timecardctl: drift must be a finite value between "
                "-%d and %d ppb: %s\n",
                TIMECARD_CLOCK_ADJUST_MAX_DRIFT_PPB,
                TIMECARD_CLOCK_ADJUST_MAX_DRIFT_PPB, text);
        exit(2);
    }
    scaled = value * 65536.0;
    return (signed __int64)(scaled < 0.0 ? scaled - 0.5 : scaled + 0.5);
}

static HANDLE
timecard_open(void)
{
    HANDLE handle = CreateFileW(TIMECARD_USER_DEVICE_PATH,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "timecardctl: cannot open %ls (error %lu)\n",
                TIMECARD_USER_DEVICE_PATH, GetLastError());
        fprintf(stderr, "Run elevated and verify the TimeCard driver is started.\n");
        exit(1);
    }
    return handle;
}

static int
timecard_ioctl(HANDLE handle, DWORD code, void *input, DWORD inputLength,
               void *output, DWORD outputLength, DWORD *returned)
{
    DWORD localReturned = 0;

    if (!DeviceIoControl(handle, code, input, inputLength, output,
                         outputLength, &localReturned, NULL)) {
        fprintf(stderr, "timecardctl: IOCTL 0x%08lx failed (error %lu)\n",
                code, GetLastError());
        return 1;
    }
    if (returned != NULL)
        *returned = localReturned;
    return 0;
}

static int
timecard_ioctl_exact(HANDLE handle, DWORD code, void *input,
                     DWORD inputLength, void *output, DWORD outputLength)
{
    DWORD returned = 0;

    if (timecard_ioctl(handle, code, input, inputLength, output,
                       outputLength, &returned))
        return 1;
    if (returned != outputLength) {
        fprintf(stderr,
                "timecardctl: IOCTL 0x%08lx returned %lu bytes; expected "
                "%lu\n", code, (unsigned long)returned,
                (unsigned long)outputLength);
        return 1;
    }
    return 0;
}

static void
print_card_time(const TIMECARD_TIME *time)
{
    __time64_t seconds = (__time64_t)time->Seconds;
    struct tm utc;

    if (_gmtime64_s(&utc, &seconds) == 0) {
        printf("%04d-%02d-%02d %02d:%02d:%02d.%09lu UTC",
               utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
               utc.tm_hour, utc.tm_min, utc.tm_sec,
               (unsigned long)time->Nanoseconds);
    } else {
        printf("%llu.%09lu", (unsigned long long)time->Seconds,
               (unsigned long)time->Nanoseconds);
    }
}

static int
cmd_status(HANDLE handle)
{
    TIMECARD_INFO info;

    if (timecard_ioctl(handle, IOCTL_TIMECARD_GET_INFO, NULL, 0,
                       &info, sizeof(info), NULL))
        return 1;
    printf("ABI:              %lu\n", (unsigned long)info.AbiVersion);
    printf("Driver:           %lu.%lu\n",
           (unsigned long)(info.DriverVersion >> 16),
           (unsigned long)(info.DriverVersion & 0xffff));
    printf("BAR length:       0x%08lx\n", (unsigned long)info.BarLength);
    printf("Interrupts:       %lu\n", (unsigned long)info.InterruptMessages);
    printf("Register layout:  %s\n",
           info.Layout == TIMECARD_LAYOUT_MSIX ? "MSI-X" :
           info.Layout == TIMECARD_LAYOUT_MSI ? "MSI" :
           info.Layout == TIMECARD_LAYOUT_ART ? "Orolia ART" : "Unknown");
    printf("Clock offset:     0x%08lx\n", (unsigned long)info.ClockOffset);
    printf("Clock version:    %lu.%lu.%lu (0x%08lx)\n",
           (unsigned long)(info.ClockVersion >> 24),
           (unsigned long)((info.ClockVersion >> 16) & 0xff),
           (unsigned long)(info.ClockVersion & 0xffff),
           (unsigned long)info.ClockVersion);
    if (info.ClockStatus == 0xffffffffu)
        printf("Clock status:     unavailable (requires core v1.2+)\n");
    else
        printf("Clock status:     0x%08lx (%s)\n",
               (unsigned long)info.ClockStatus,
               (info.ClockStatus & 1u) ? "in sync" : "not in sync");
    printf("Clock source:     0x%04lx\n",
           (unsigned long)(info.ClockSelect >> 16));
    printf("TOD version:      0x%08lx\n", (unsigned long)info.TodVersion);
    if (info.TodStatus == 0xffffffffu)
        printf("TOD status:       unavailable (requires core v1.2+)\n");
    else
        printf("TOD status:       0x%08lx\n",
               (unsigned long)info.TodStatus);
    printf("GNSS/UTC/leap/sat: unavailable "
           "(synthesis-optional; no capability word)\n");
    return 0;
}

static int
cmd_get(HANDLE handle)
{
    TIMECARD_CROSSTIMESTAMP timestamp;
    unsigned __int64 window;

    if (timecard_ioctl(handle, IOCTL_TIMECARD_GET_CROSSTIMESTAMP,
                       NULL, 0, &timestamp, sizeof(timestamp), NULL))
        return 1;
    print_card_time(&timestamp.CardTime);
    window = timestamp.SystemTimeAfter100ns -
             timestamp.SystemTimeBefore100ns;
    printf("\nSystem sampling window: %llu ns\n",
           (unsigned long long)(window * 100u));
    return 0;
}

static int
cmd_set_system(HANDLE handle)
{
    FILETIME fileTime;
    ULARGE_INTEGER value;
    unsigned __int64 unix100ns;
    TIMECARD_TIME time;

    GetSystemTimePreciseAsFileTime(&fileTime);
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    if (value.QuadPart < FILETIME_UNIX_EPOCH_100NS) {
        fprintf(stderr, "timecardctl: system time predates Unix epoch\n");
        return 1;
    }
    unix100ns = value.QuadPart - FILETIME_UNIX_EPOCH_100NS;
    time.Seconds = unix100ns / 10000000u;
    time.Nanoseconds = (unsigned __int32)((unix100ns % 10000000u) * 100u);
    time.Reserved = 0;

    if (timecard_ioctl(handle, IOCTL_TIMECARD_SET_TIME, &time, sizeof(time),
                       NULL, 0, NULL))
        return 1;
    printf("TimeCard set to ");
    print_card_time(&time);
    printf("\n");
    return 0;
}

static unsigned long
parse_ulong(const char *text, const char *name)
{
    char *end;
    unsigned long value = strtoul(text, &end, 0);

    if (*text == '\0' || *end != '\0') {
        fprintf(stderr, "timecardctl: invalid %s: %s\n", name, text);
        exit(2);
    }
    return value;
}

static unsigned long
parse_clock_source(const char *text)
{
    static const struct {
        const char *name;
        unsigned long value;
    } sources[] = {
        { "none", TIMECARD_CLOCK_SOURCE_NONE },
        { "tod", TIMECARD_CLOCK_SOURCE_TOD },
        { "irig", TIMECARD_CLOCK_SOURCE_IRIG },
        { "pps", TIMECARD_CLOCK_SOURCE_PPS },
        { "ptp", TIMECARD_CLOCK_SOURCE_PTP },
        { "rtc", TIMECARD_CLOCK_SOURCE_RTC },
        { "dcf", TIMECARD_CLOCK_SOURCE_DCF },
        { "ntp", TIMECARD_CLOCK_SOURCE_NTP },
        { "synce", TIMECARD_CLOCK_SOURCE_SYNCE },
        { "dyn", TIMECARD_CLOCK_SOURCE_DYN },
        { "regs", TIMECARD_CLOCK_SOURCE_REGS },
        { "ext", TIMECARD_CLOCK_SOURCE_EXT }
    };
    size_t i;

    for (i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        if (_stricmp(text, sources[i].name) == 0)
            return sources[i].value;
    }
    return parse_ulong(text, "clock source");
}

static int
cmd_clock_source(HANDLE handle, int argc, char **argv)
{
    TIMECARD_CLOCK_SOURCE_CONTROL control;

    if (argc != 3)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Source = (unsigned __int32)parse_clock_source(argv[2]);
    if (timecard_ioctl(handle, IOCTL_TIMECARD_CLOCK_SOURCE_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    printf("Clock source requested 0x%02lx, active 0x%02lx\n",
           (unsigned long)control.Source,
           (unsigned long)control.ActiveSource);
    return 0;
}

static void
print_nmea(const TIMECARD_NMEA_CONTROL *control)
{
    printf("NMEA generator: %s, %lu baud, %s polarity, transmitter %s\n",
           (control->Flags & TIMECARD_NMEA_FLAG_ENABLED) ?
               "enabled" : "disabled",
           (unsigned long)control->Baud,
           control->Polarity ? "inverted" : "normal",
           (control->Flags & TIMECARD_NMEA_FLAG_ERROR) ?
               "ERROR" : "OK");
    printf("Selector/control/status/version: %lu / 0x%08lx / 0x%08lx / 0x%08lx\n",
           (unsigned long)control->BaudSelector,
           (unsigned long)control->Control,
           (unsigned long)control->Status,
           (unsigned long)control->Version);
    if ((control->Flags & TIMECARD_NMEA_FLAG_ADVANCED_VALID) != 0u) {
        printf("Correction/local offset/GNSS/disable mask: "
               "%ld s / %ld min / %lu / 0x%02lx\n",
               (long)control->CorrectionSeconds,
               (long)control->LocalOffsetMinutes,
               (unsigned long)control->Gnss,
               (unsigned long)control->MessageDisableMask);
        printf("Message disable bits: RMC=0x01 (core 1.4+), "
               "ZDA=0x02, UTC=0x04 (core 1.6+)\n");
    }
}

static int
cmd_nmea_status(HANDLE handle)
{
    TIMECARD_NMEA_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    if (timecard_ioctl(handle, IOCTL_TIMECARD_NMEA_QUERY,
                       NULL, 0, &control, sizeof(control), NULL))
        return 1;
    print_nmea(&control);
    return 0;
}

static int
cmd_nmea_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_NMEA_CONTROL control;
    int index;
    int remaining;

    if (argc < 4 || argc > 10)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    if (_stricmp(argv[2], "on") == 0 ||
        _stricmp(argv[2], "enable") == 0) {
        control.Flags = TIMECARD_NMEA_FLAG_ENABLED;
    } else if (_stricmp(argv[2], "off") != 0 &&
               _stricmp(argv[2], "disable") != 0) {
        return 2;
    }
    control.Baud = (unsigned __int32)parse_ulong(argv[3], "NMEA baud");
    index = 4;
    if (index < argc) {
        if (_stricmp(argv[index], "inverted") == 0) {
            control.Polarity = 1;
            ++index;
        } else if (_stricmp(argv[index], "normal") == 0) {
            ++index;
        } else if (_stricmp(argv[index], "clear") == 0) {
            control.Flags |= TIMECARD_NMEA_FLAG_CLEAR_ERROR;
            ++index;
        } else {
            return 2;
        }
    }
    remaining = argc - index;
    if ((control.Flags & TIMECARD_NMEA_FLAG_CLEAR_ERROR) != 0u) {
        if (remaining != 0)
            return 2;
    } else if (remaining == 1) {
        if (_stricmp(argv[index], "clear") != 0)
            return 2;
        control.Flags |= TIMECARD_NMEA_FLAG_CLEAR_ERROR;
    } else if (remaining == 4 || remaining == 5) {
        control.Flags |= TIMECARD_NMEA_FLAG_ADVANCED_VALID;
        control.CorrectionSeconds = (signed __int32)parse_signed_long(
            argv[index], "NMEA correction seconds");
        control.LocalOffsetMinutes = (signed __int32)parse_signed_long(
            argv[index + 1], "NMEA local offset minutes");
        control.Gnss = (unsigned __int32)parse_ulong(
            argv[index + 2], "NMEA GNSS selector");
        control.MessageDisableMask = (unsigned __int32)parse_ulong(
            argv[index + 3], "NMEA message disable mask");
        if (remaining == 5) {
            if (_stricmp(argv[index + 4], "clear") != 0)
                return 2;
            control.Flags |= TIMECARD_NMEA_FLAG_CLEAR_ERROR;
        }
    } else if (remaining != 0) {
        return 2;
    }
    if (timecard_ioctl(handle, IOCTL_TIMECARD_NMEA_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_nmea(&control);
    return 0;
}

static void
print_nmea_utc(const TIMECARD_NMEA_UTC_CONTROL *control)
{
    printf("NMEA UTC information: version 0x%08lx, flags 0x%08lx\n",
           (unsigned long)control->Version,
           (unsigned long)control->Flags);
    printf("  UTC offset: %lu seconds (%s)\n",
           (unsigned long)control->UtcOffsetSeconds,
           (control->Flags & TIMECARD_NMEA_UTC_FLAG_OFFSET_VALID) != 0u ?
               "valid" : "invalid");
    printf("  leap indication: %s\n",
           (control->Flags & (TIMECARD_NMEA_UTC_FLAG_LEAP61 |
                              TIMECARD_NMEA_UTC_FLAG_LEAP59)) ==
                   (TIMECARD_NMEA_UTC_FLAG_LEAP61 |
                    TIMECARD_NMEA_UTC_FLAG_LEAP59) ?
               "invalid (both leap61 and leap59 set)" :
           (control->Flags & TIMECARD_NMEA_UTC_FLAG_LEAP61) != 0u ?
               "+1 second (61)" :
           (control->Flags & TIMECARD_NMEA_UTC_FLAG_LEAP59) != 0u ?
               "-1 second (59)" : "none");
    printf("  read/write contract: %s / %s\n",
           (control->Flags &
            TIMECARD_NMEA_UTC_FLAG_READ_SUPPORTED) != 0u ? "yes" : "no",
           (control->Flags &
            TIMECARD_NMEA_UTC_FLAG_WRITE_SUPPORTED) != 0u ? "yes" : "no");
    printf("  raw UTC/handshake: 0x%08lx / 0x%08lx\n",
           (unsigned long)control->RawUtcInfo,
           (unsigned long)control->HandshakeControl);
}

static int
cmd_nmea_utc_status(HANDLE handle)
{
    TIMECARD_NMEA_UTC_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_NMEA_UTC_QUERY,
                             NULL, 0, &control, sizeof(control)))
        return 1;
    print_nmea_utc(&control);
    return 0;
}

static int
cmd_nmea_utc_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_NMEA_UTC_CONTROL control;

    if (argc != 5)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.UtcOffsetSeconds = parse_u32_bounded(
        argv[2], "UTC offset", 0u, 0xffffu);
    if (_stricmp(argv[3], "valid") == 0) {
        control.Flags |= TIMECARD_NMEA_UTC_FLAG_OFFSET_VALID;
    } else if (_stricmp(argv[3], "invalid") != 0) {
        return 2;
    }
    if (_stricmp(argv[4], "leap61") == 0) {
        control.Flags |= TIMECARD_NMEA_UTC_FLAG_LEAP61;
    } else if (_stricmp(argv[4], "leap59") == 0) {
        control.Flags |= TIMECARD_NMEA_UTC_FLAG_LEAP59;
    } else if (_stricmp(argv[4], "none") != 0) {
        return 2;
    }
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_NMEA_UTC_SET,
                             &control, sizeof(control), &control,
                             sizeof(control)))
        return 1;
    print_nmea_utc(&control);
    return 0;
}

static int
cmd_serial(HANDLE handle)
{
    TIMECARD_IDENTITY identity;
    unsigned long i;

    RtlZeroMemory(&identity, sizeof(identity));
    if (timecard_ioctl(handle, IOCTL_TIMECARD_GET_IDENTITY,
                       NULL, 0, &identity, sizeof(identity), NULL))
        return 1;
    printf("Card serial: ");
    for (i = 0; i < TIMECARD_IDENTITY_SERIAL_LENGTH; ++i) {
        if (i != 0)
            putchar(':');
        printf("%02X", identity.Serial[i]);
    }
    printf("%s\n", (identity.Flags & TIMECARD_IDENTITY_FLAG_VALID) ?
           "" : " (invalid identity data)");
    return 0;
}

static int
cmd_uart_config(HANDLE handle, int argc, char **argv)
{
    TIMECARD_UART_CONFIG config;

    if (argc != 4)
        return 2;
    config.Port = (unsigned __int32)parse_ulong(argv[2], "port");
    config.Baud = (unsigned __int32)parse_ulong(argv[3], "baud");
    if (timecard_ioctl(handle, IOCTL_TIMECARD_UART_CONFIGURE,
                       &config, sizeof(config), NULL, 0, NULL))
        return 1;
    printf("UART %lu configured for %lu baud, 8N1\n",
           (unsigned long)config.Port, (unsigned long)config.Baud);
    return 0;
}

static int
cmd_uart_read(HANDLE handle, int argc, char **argv)
{
    TIMECARD_UART_READ_REQUEST request;
    TIMECARD_UART_TRANSFER transfer;
    DWORD returned;

    if (argc < 3 || argc > 5)
        return 2;
    request.Port = (unsigned __int32)parse_ulong(argv[2], "port");
    request.MaximumBytes = argc >= 4 ?
        (unsigned __int32)parse_ulong(argv[3], "byte count") : 256u;
    request.TimeoutMilliseconds = argc >= 5 ?
        (unsigned __int32)parse_ulong(argv[4], "timeout") : 1000u;
    request.Reserved = 0;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_UART_READ,
                       &request, sizeof(request), &transfer,
                       sizeof(transfer), &returned))
        return 1;
    fwrite(transfer.Data, 1, transfer.Length, stdout);
    if (transfer.Length == 0 || transfer.Data[transfer.Length - 1] != '\n')
        putchar('\n');
    printf("[%lu byte(s), LSR 0x%02lx]\n",
           (unsigned long)transfer.Length,
           (unsigned long)(transfer.LineStatus & 0xff));
    return 0;
}

static int
cmd_uart_read_hex(HANDLE handle, int argc, char **argv)
{
    TIMECARD_UART_READ_REQUEST request;
    TIMECARD_UART_TRANSFER transfer;
    DWORD returned;
    unsigned long i;

    if (argc < 3 || argc > 5)
        return 2;
    request.Port = (unsigned __int32)parse_ulong(argv[2], "port");
    request.MaximumBytes = argc >= 4 ?
        (unsigned __int32)parse_ulong(argv[3], "byte count") : 256u;
    request.TimeoutMilliseconds = argc >= 5 ?
        (unsigned __int32)parse_ulong(argv[4], "timeout") : 1000u;
    request.Reserved = 0;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_UART_READ,
                       &request, sizeof(request), &transfer,
                       sizeof(transfer), &returned))
        return 1;
    for (i = 0; i < transfer.Length; ++i) {
        if (i != 0)
            putchar(' ');
        printf("%02X", transfer.Data[i]);
    }
    putchar('\n');
    printf("[%lu byte(s), LSR 0x%02lx]\n",
           (unsigned long)transfer.Length,
           (unsigned long)(transfer.LineStatus & 0xff));
    return 0;
}

static int
cmd_uart_observe(HANDLE handle, int argc, char **argv)
{
    TIMECARD_UART_OBSERVE request;
    TIMECARD_UART_OBSERVE response;

    if (argc < 3 || argc > 4)
        return 2;
    RtlZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.Port = (unsigned __int32)parse_ulong(argv[2], "port");
    request.TimeoutMilliseconds = argc == 4 ?
        (unsigned __int32)parse_ulong(argv[3], "timeout") : 0u;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_UART_OBSERVE,
                       &request, sizeof(request), &response,
                       sizeof(response), NULL))
        return 1;
    printf("UART %lu: %s, LSR 0x%02lx, interrupt object(s) %lu, "
           "buffered %lu, dropped %lu\n",
           (unsigned long)response.Port,
           (response.Flags & TIMECARD_UART_OBSERVE_FLAG_ACTIVITY) ?
               "receive data ready" : "idle",
           (unsigned long)(response.LineStatus & 0xffu),
           (unsigned long)response.Reserved[0],
           (unsigned long)response.Reserved[1],
           (unsigned long)response.Reserved[2]);
    return 0;
}

static int
cmd_uart_write(HANDLE handle, int argc, char **argv)
{
    TIMECARD_UART_TRANSFER transfer;
    TIMECARD_UART_RESULT result;
    size_t length;

    if (argc != 4)
        return 2;
    RtlZeroMemory(&transfer, sizeof(transfer));
    transfer.Port = (unsigned __int32)parse_ulong(argv[2], "port");
    length = strlen(argv[3]);
    if (length > TIMECARD_UART_MAX_TRANSFER) {
        fprintf(stderr, "timecardctl: UART write is limited to %u bytes\n",
                TIMECARD_UART_MAX_TRANSFER);
        return 2;
    }
    transfer.Length = (unsigned __int32)length;
    transfer.TimeoutMilliseconds = 1000;
    memcpy(transfer.Data, argv[3], length);
    if (timecard_ioctl(
            handle, IOCTL_TIMECARD_UART_WRITE, &transfer,
            (DWORD)(FIELD_OFFSET(TIMECARD_UART_TRANSFER, Data) + length),
            &result, sizeof(result), NULL))
        return 1;
    printf("Wrote %lu byte(s), LSR 0x%02lx\n",
           (unsigned long)result.BytesTransferred,
           (unsigned long)(result.LineStatus & 0xff));
    return 0;
}

static void
print_mro50(const TIMECARD_MRO50_STATUS *status)
{
    printf("mRO-50 bridge:    %s%s\n",
           (status->Flags & TIMECARD_MRO50_FLAG_ENABLED) ?
               "enabled" : "disabled",
           (status->Flags & TIMECARD_MRO50_FLAG_LOCKED) ?
               ", locked" : ", acquiring");
    printf("Control:          0x%08lx\n",
           (unsigned long)status->Control);
    printf("Fine adjustment:  ");
    if ((status->Flags & TIMECARD_MRO50_FLAG_FINE_VALID) != 0)
        printf("0x%08lx\n", (unsigned long)status->FineAdjustment);
    else
        printf("unavailable\n");
    printf("Coarse adjustment:");
    if ((status->Flags & TIMECARD_MRO50_FLAG_COARSE_VALID) != 0)
        printf(" 0x%08lx\n", (unsigned long)status->CoarseAdjustment);
    else
        printf(" unavailable\n");
    printf("Temperature raw:  0x%08lx\n",
           (unsigned long)status->Temperature);
    printf("Board config:     0x%08lx (serial route %s)\n",
           (unsigned long)status->BoardConfig,
           (status->Flags & TIMECARD_MRO50_FLAG_SERIAL_ENABLED) ?
               "enabled" : "disabled");
}

static int
cmd_mro50_status(HANDLE handle)
{
    TIMECARD_MRO50_STATUS status;

    if (timecard_ioctl(handle, IOCTL_TIMECARD_MRO50_QUERY,
                       NULL, 0, &status, sizeof(status), NULL))
        return 1;
    print_mro50(&status);
    return 0;
}

static int
cmd_mro50_control(HANDLE handle, int argc, char **argv)
{
    TIMECARD_MRO50_CONTROL control;
    TIMECARD_MRO50_STATUS status;

    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    if (strcmp(argv[1], "mro-fine") == 0 && argc == 3) {
        control.Action = TIMECARD_MRO50_ACTION_ADJUST_FINE;
        control.Value = (unsigned __int32)parse_ulong(argv[2], "fine value");
    } else if (strcmp(argv[1], "mro-coarse") == 0 && argc == 3) {
        control.Action = TIMECARD_MRO50_ACTION_ADJUST_COARSE;
        control.Value = (unsigned __int32)parse_ulong(argv[2], "coarse value");
    } else if (strcmp(argv[1], "mro-save-coarse") == 0 && argc == 2) {
        control.Action = TIMECARD_MRO50_ACTION_SAVE_COARSE;
    } else if (strcmp(argv[1], "mro-serial") == 0 && argc == 3) {
        control.Action = TIMECARD_MRO50_ACTION_SERIAL_ENABLE;
        if (_stricmp(argv[2], "on") == 0)
            control.Value = 1u;
        else if (_stricmp(argv[2], "off") != 0)
            return 2;
    } else {
        return 2;
    }

    if (timecard_ioctl(handle, IOCTL_TIMECARD_MRO50_CONTROL,
                       &control, sizeof(control), &status,
                       sizeof(status), NULL))
        return 1;
    print_mro50(&status);
    return 0;
}

static int
cmd_flash_status(HANDLE handle)
{
    TIMECARD_FLASH_STATUS status;

    if (timecard_ioctl(handle, IOCTL_TIMECARD_FLASH_QUERY,
                       NULL, 0, &status, sizeof(status), NULL))
        return 1;
    printf("FPGA flash:        %s%s%s\n",
           (status.Flags & TIMECARD_FLASH_FLAG_PRESENT) ?
               "present" : "not present",
           (status.Flags & TIMECARD_FLASH_FLAG_IDENTIFIED) ?
               ", identified" : "",
           (status.Flags & TIMECARD_FLASH_FLAG_SUPPORTED) ?
               ", writable geometry supported" : "");
    printf("Controller offset: 0x%08lx\n",
           (unsigned long)status.Offset);
    printf("JEDEC ID:          0x%06lx\n",
           (unsigned long)(status.JedecId & 0xffffffu));
    printf("Capacity:          %lu bytes\n",
           (unsigned long)status.CapacityBytes);
    printf("Firmware offset:   0x%08lx\n",
           (unsigned long)status.FirmwareOffset);
    printf("Erase / page:      %lu / %lu bytes\n",
           (unsigned long)status.EraseSize,
           (unsigned long)status.PageSize);
    printf("Controller status: 0x%08lx\n",
           (unsigned long)status.ControllerStatus);
    printf("Flash status:      0x%08lx\n",
           (unsigned long)status.FlashStatus);
    return 0;
}

static int
cmd_hierarchy(HANDLE handle, unsigned __int32 action,
              unsigned __int32 persist)
{
    TIMECARD_HIERARCHY_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Action = action;
    control.Persist = persist;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_HIERARCHY_CONTROL,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;

    printf("Runtime subsystem devices: %s\n",
           control.RuntimeEnabled ? "enabled" : "disabled");
    printf("Enable on next start:      %s\n",
           control.Persisted ? "yes" : "no");
    if (action == TIMECARD_HIERARCHY_DISABLE && control.RuntimeEnabled)
        printf("A device restart is required to remove existing children.\n");
    return 0;
}

static void
print_sma(const TIMECARD_SMA_CONTROL *control)
{
    const char *direction = control->Direction == TIMECARD_SMA_DIRECTION_INPUT ?
        "input" : control->Direction == TIMECARD_SMA_DIRECTION_OUTPUT ?
        "output" : "disabled";

    printf("SMA %lu: %-8s function 0x%04lx, input 0x%04lx, output 0x%04lx%s\n",
           (unsigned long)control->Connector, direction,
           (unsigned long)control->Function,
           (unsigned long)control->InputMap,
           (unsigned long)control->OutputMap,
           (control->Flags & TIMECARD_SMA_FLAG_FIXED_DIRECTION) ?
               " (fixed direction)" : "");
}

static int
cmd_sma_status(HANDLE handle, int argc, char **argv)
{
    unsigned long first = 1;
    unsigned long last = TIMECARD_SMA_COUNT;
    unsigned long connector;

    if (argc > 3)
        return 2;
    if (argc == 3)
        first = last = parse_ulong(argv[2], "SMA connector");
    if (first == 0 || last > TIMECARD_SMA_COUNT)
        return 2;

    for (connector = first; connector <= last; connector++) {
        TIMECARD_SMA_CONTROL control;

        RtlZeroMemory(&control, sizeof(control));
        control.Size = sizeof(control);
        control.Connector = (unsigned __int32)connector;
        if (timecard_ioctl(handle, IOCTL_TIMECARD_SMA_QUERY,
                           &control, sizeof(control), &control,
                           sizeof(control), NULL))
            return 1;
        print_sma(&control);
    }
    return 0;
}

static int
cmd_sma_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_SMA_CONTROL control;

    if (argc < 4 || argc > 5)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Connector =
        (unsigned __int32)parse_ulong(argv[2], "SMA connector");
    if (strcmp(argv[3], "input") == 0)
        control.Direction = TIMECARD_SMA_DIRECTION_INPUT;
    else if (strcmp(argv[3], "output") == 0)
        control.Direction = TIMECARD_SMA_DIRECTION_OUTPUT;
    else if (strcmp(argv[3], "disabled") == 0)
        control.Direction = TIMECARD_SMA_DIRECTION_DISABLED;
    else
        return 2;

    if (control.Direction == TIMECARD_SMA_DIRECTION_DISABLED) {
        if (argc != 4)
            return 2;
    } else {
        if (argc != 5)
            return 2;
        control.Function =
            (unsigned __int32)parse_ulong(argv[4], "SMA function");
    }

    if (timecard_ioctl(handle, IOCTL_TIMECARD_SMA_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_sma(&control);
    return 0;
}

static int
cmd_i2c_status(HANDLE handle)
{
    TIMECARD_I2C_STATUS status;

    if (timecard_ioctl(handle, IOCTL_TIMECARD_I2C_GET_STATUS,
                       NULL, 0, &status, sizeof(status), NULL))
        return 1;
    printf("I2C controller:    %s%s\n",
           (status.Flags & TIMECARD_I2C_FLAG_PRESENT) ? "present" : "absent",
           (status.Flags & TIMECARD_I2C_FLAG_ENABLED) ? ", enabled" : "");
    printf("Register offset:   0x%08lx\n", (unsigned long)status.Offset);
    printf("CR / SR:           0x%02lx / 0x%02lx%s\n",
           (unsigned long)status.Control, (unsigned long)status.Status,
           (status.Flags & TIMECARD_I2C_FLAG_BUS_BUSY) ? " (bus busy)" : "");
    printf("ISR / IER:         0x%08lx / 0x%08lx\n",
           (unsigned long)status.InterruptStatus,
           (unsigned long)status.InterruptEnable);
    printf("TX / RX occupancy: %lu / %lu\n",
           (unsigned long)status.TxFifoOccupancy,
           (unsigned long)status.RxFifoOccupancy);
    printf("Known devices:     0x%02lx\n",
           (unsigned long)status.KnownDeviceMask);
    printf("Last transfer:     CR 0x%02lx -> 0x%02lx, "
           "SR 0x%02lx -> 0x%02lx\n",
           (unsigned long)(status.Reserved[0] & 0xffu),
           (unsigned long)((status.Reserved[0] >> 16) & 0xffu),
           (unsigned long)((status.Reserved[0] >> 8) & 0xffu),
           (unsigned long)((status.Reserved[0] >> 24) & 0xffu));
    printf("Last events/TFO:   0x%04lx, %lu -> %lu\n",
           (unsigned long)(status.Reserved[1] & 0xffffu),
           (unsigned long)((status.Reserved[1] >> 16) & 0xffu),
           (unsigned long)((status.Reserved[1] >> 24) & 0xffu));
    return 0;
}

static const char *
board_profile_name(unsigned long profile)
{
    switch (profile) {
    case TIMECARD_BOARD_PROFILE_FB: return "OCP/FPGA";
    case TIMECARD_BOARD_PROFILE_ART: return "Orolia/Safran ART";
    case TIMECARD_BOARD_PROFILE_CELESTICA: return "Celestica";
    default: return "Unknown";
    }
}

static const char *
oscillator_name(unsigned long oscillator)
{
    switch (oscillator) {
    case TIMECARD_OSCILLATOR_NONE: return "None detected";
    case TIMECARD_OSCILLATOR_UART: return "UART/protocol probe required";
    case TIMECARD_OSCILLATOR_MRO50: return "Safran mRO-50";
    case TIMECARD_OSCILLATOR_SA53: return "Microchip SA53";
    default: return "Unknown";
    }
}

static void
print_capability(unsigned __int64 flags, unsigned __int64 flag,
                 const char *name)
{
    printf("  %-31s %s\n", name, (flags & flag) != 0 ? "yes" : "no");
}

static int
cmd_capabilities(HANDLE handle)
{
    TIMECARD_CAPABILITIES capabilities;

    RtlZeroMemory(&capabilities, sizeof(capabilities));
    capabilities.Size = sizeof(capabilities);
    if (timecard_ioctl(handle, IOCTL_TIMECARD_GET_CAPABILITIES,
                       NULL, 0, &capabilities, sizeof(capabilities), NULL))
        return 1;

    printf("ABI:                   %lu\n",
           (unsigned long)capabilities.AbiVersion);
    printf("Board profile:         %s (%lu)\n",
           board_profile_name(capabilities.BoardProfile),
           (unsigned long)capabilities.BoardProfile);
    printf("Oscillator:            %s (%lu)\n",
           oscillator_name(capabilities.OscillatorType),
           (unsigned long)capabilities.OscillatorType);
    printf("Capability mask:       0x%016llx\n",
           (unsigned long long)capabilities.Flags);
    print_capability(capabilities.Flags, TIMECARD_CAP_PHC,
                     "Precision hardware clock");
    print_capability(capabilities.Flags, TIMECARD_CAP_GNSS_UART,
                     "GNSS UART");
    print_capability(capabilities.Flags, TIMECARD_CAP_ATOMIC_UART,
                     "Atomic-clock UART");
    print_capability(capabilities.Flags, TIMECARD_CAP_PAIRED_PHASE_METER,
                     "Paired PPS phase meter");
    print_capability(capabilities.Flags, TIMECARD_CAP_MRO50_DIRECT,
                     "Direct mRO-50 control");
    print_capability(capabilities.Flags, TIMECARD_CAP_PHC_PHASE_ADJUST,
                     "Bounded PHC phase adjustment");
    print_capability(capabilities.Flags, TIMECARD_CAP_DISCIPLINE_PARAMETERS,
                     "Discipline EEPROM parameters");
    print_capability(capabilities.Flags, TIMECARD_CAP_TEMPERATURE_TELEMETRY,
                     "Oscillator temperature");
    print_capability(capabilities.Flags, TIMECARD_CAP_HARDWARE_DISCIPLINE,
                     "Hardware oscillator discipline");
    if ((capabilities.Flags & TIMECARD_CAP_PAIRED_PHASE_METER) != 0) {
        printf("Reference/oscillator:  PPS %lu / PPS %lu\n",
               (unsigned long)capabilities.ReferencePpsIndex,
               (unsigned long)capabilities.OscillatorPpsIndex);
    }
    if ((capabilities.Flags & TIMECARD_CAP_MRO50_DIRECT) != 0) {
        printf("Fine range:            %lu..%lu\n",
               (unsigned long)capabilities.FineMinimum,
               (unsigned long)capabilities.FineMaximum);
        printf("Coarse range:          %lu..%lu\n",
               (unsigned long)capabilities.CoarseMinimum,
               (unsigned long)capabilities.CoarseMaximum);
    }
    return 0;
}

static int
cmd_phase_status(HANDLE handle)
{
    TIMECARD_PHASE_SAMPLE sample;

    RtlZeroMemory(&sample, sizeof(sample));
    sample.Size = sizeof(sample);
    if (timecard_ioctl(handle, IOCTL_TIMECARD_PHASE_QUERY,
                       NULL, 0, &sample, sizeof(sample), NULL))
        return 1;

    printf("Present:               %s\n",
           (sample.Flags & TIMECARD_PHASE_FLAG_PRESENT) != 0 ? "yes" : "no");
    printf("Capture enabled:       %s\n",
           (sample.Flags & TIMECARD_PHASE_FLAG_ENABLED) != 0 ? "yes" : "no");
    printf("Reference PPS:         counter %lu, error 0x%08lx, %s\n",
           (unsigned long)sample.ReferenceCounter,
           (unsigned long)sample.ReferenceError,
           (sample.Flags & TIMECARD_PHASE_FLAG_REFERENCE_VALID) != 0 ?
               "valid" : "not valid");
    printf("Oscillator PPS:        counter %lu, error 0x%08lx, %s\n",
           (unsigned long)sample.OscillatorCounter,
           (unsigned long)sample.OscillatorError,
           (sample.Flags & TIMECARD_PHASE_FLAG_OSCILLATOR_VALID) != 0 ?
               "valid" : "not valid");
    if ((sample.Flags & TIMECARD_PHASE_FLAG_PHASE_VALID) != 0) {
        printf("Phase (osc-ref):       %lld ns\n",
               (long long)sample.PhaseNanoseconds);
        printf("Reference time:        ");
        print_card_time(&sample.ReferenceTime);
        printf("\nOscillator time:       ");
        print_card_time(&sample.OscillatorTime);
        printf("\n");
    } else {
        printf("Phase (osc-ref):       not valid\n");
    }
    return 0;
}

static int
cmd_phase_control(HANDLE handle, unsigned long action)
{
    TIMECARD_PHASE_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Action = (unsigned __int32)action;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_PHASE_CONTROL,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    printf("Phase capture %s.\n", control.Enabled ? "enabled" : "disabled");
    return 0;
}

static int
cmd_phc_adjust(HANDLE handle, int argc, char **argv)
{
    TIMECARD_PHC_ADJUST adjust;
    char *end;
    __int64 value;

    if (argc != 3)
        return 2;
    value = _strtoi64(argv[2], &end, 0);
    if (*argv[2] == '\0' || *end != '\0' ||
        value < -499999999ll || value > 499999999ll) {
        fprintf(stderr,
                "timecardctl: PHC adjustment must be -499999999..499999999 ns\n");
        return 2;
    }
    RtlZeroMemory(&adjust, sizeof(adjust));
    adjust.Size = sizeof(adjust);
    adjust.OffsetNanoseconds = value;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_PHC_ADJUST,
                       &adjust, sizeof(adjust), &adjust,
                       sizeof(adjust), NULL))
        return 1;
    printf("PHC adjusted by %lld ns; resulting time ", (long long)value);
    print_card_time(&adjust.ResultingTime);
    printf("\n");
    return 0;
}

static int
cmd_discipline_read(HANDLE handle, int argc, char **argv)
{
    TIMECARD_DISCIPLINE_BLOB blob;
    FILE *file;

    if (argc != 3)
        return 2;
    RtlZeroMemory(&blob, sizeof(blob));
    blob.Size = sizeof(blob);
    if (timecard_ioctl(handle, IOCTL_TIMECARD_DISCIPLINE_READ,
                       NULL, 0, &blob, sizeof(blob), NULL))
        return 1;
    if (blob.Length != TIMECARD_DISCIPLINE_EEPROM_SIZE) {
        fprintf(stderr, "timecardctl: driver returned unexpected EEPROM length %lu\n",
                (unsigned long)blob.Length);
        return 1;
    }
    if (fopen_s(&file, argv[2], "wb") != 0 || file == NULL) {
        fprintf(stderr, "timecardctl: cannot create %s\n", argv[2]);
        return 1;
    }
    if (fwrite(blob.Data, 1, blob.Length, file) != blob.Length) {
        fprintf(stderr, "timecardctl: failed writing %s\n", argv[2]);
        fclose(file);
        return 1;
    }
    fclose(file);
    printf("Saved %lu-byte discipline image to %s (%s).\n",
           (unsigned long)blob.Length, argv[2],
           (blob.Flags & TIMECARD_DISCIPLINE_FLAG_VALID) != 0 ?
               "valid version-1 headers" : "headers not initialized");
    return 0;
}

static int
cmd_discipline_write(HANDLE handle, int argc, char **argv)
{
    TIMECARD_DISCIPLINE_BLOB blob;
    FILE *file;
    size_t length;

    if (argc != 3)
        return 2;
    if (fopen_s(&file, argv[2], "rb") != 0 || file == NULL) {
        fprintf(stderr, "timecardctl: cannot open %s\n", argv[2]);
        return 1;
    }
    RtlZeroMemory(&blob, sizeof(blob));
    blob.Size = sizeof(blob);
    blob.Length = TIMECARD_DISCIPLINE_EEPROM_SIZE;
    length = fread(blob.Data, 1, sizeof(blob.Data), file);
    if (length != sizeof(blob.Data) || fgetc(file) != EOF) {
        fprintf(stderr, "timecardctl: image must be exactly %u bytes\n",
                TIMECARD_DISCIPLINE_EEPROM_SIZE);
        fclose(file);
        return 1;
    }
    fclose(file);
    if (blob.Data[0] != 'O' || blob.Data[1] != 1u ||
        blob.Data[0x90] != 'O' || blob.Data[0x91] != 1u) {
        fprintf(stderr,
                "timecardctl: refusing image without version-1 config and temperature headers\n");
        return 1;
    }
    if (timecard_ioctl(handle, IOCTL_TIMECARD_DISCIPLINE_WRITE,
                       &blob, sizeof(blob), &blob, sizeof(blob), NULL))
        return 1;
    printf("Restored and verified %u-byte discipline image from %s.\n",
           TIMECARD_DISCIPLINE_EEPROM_SIZE, argv[2]);
    return 0;
}

static int
cmd_i2c_mux(HANDLE handle, int argc, char **argv)
{
    TIMECARD_I2C_MUX_CONTROL control;
    DWORD code;

    if (argc < 2 || argc > 3)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    if (argc == 3) {
        control.ChannelMask =
            (unsigned __int32)parse_ulong(argv[2], "I2C mux mask");
        if ((control.ChannelMask & ~TIMECARD_I2C_MUX_CHANNEL_MASK) != 0)
            return 2;
        code = IOCTL_TIMECARD_I2C_MUX_SET;
    } else {
        code = IOCTL_TIMECARD_I2C_MUX_QUERY;
    }
    if (timecard_ioctl(handle, code,
                       argc == 3 ? &control : NULL,
                       argc == 3 ? sizeof(control) : 0,
                       &control, sizeof(control), NULL))
        return 1;
    printf("PCA9546A: %s, channel mask 0x%02lx, SR 0x%02lx, ISR 0x%08lx\n",
           control.Present ? "present" : "not present",
           (unsigned long)control.ChannelMask,
           (unsigned long)control.ControllerStatus,
           (unsigned long)control.InterruptStatus);
    return control.Present ? 0 : 1;
}

static int
cmd_i2c_scan(HANDLE handle)
{
    unsigned long address;
    unsigned long found = 0;

    printf("Read-only address scan (7-bit addresses):\n");
    for (address = 0x08; address <= 0x77; ++address) {
        TIMECARD_I2C_PROBE probe;

        RtlZeroMemory(&probe, sizeof(probe));
        probe.Size = sizeof(probe);
        probe.Address = (unsigned __int32)address;
        if (timecard_ioctl(handle, IOCTL_TIMECARD_I2C_PROBE,
                           &probe, sizeof(probe), &probe,
                           sizeof(probe), NULL))
            return 1;
        if (probe.Present) {
            const char *name = address == 0x50 ? "board EEPROM" :
                               address == 0x58 ? "MAC EEPROM" : "device";
            printf("  0x%02lx  %s\n", address, name);
            ++found;
        }
    }
    printf("%lu device(s) acknowledged.\n", found);
    return 0;
}

static void
print_i2c_data(const unsigned char *data, unsigned long length)
{
    unsigned long offset;

    for (offset = 0; offset < length; offset += 16) {
        unsigned long i;

        printf("%04lx  ", offset);
        for (i = 0; i < 16; ++i) {
            if (offset + i < length)
                printf("%02x ", data[offset + i]);
            else
                printf("   ");
        }
        printf(" ");
        for (i = 0; i < 16 && offset + i < length; ++i) {
            unsigned char value = data[offset + i];

            putchar(value >= 0x20 && value <= 0x7e ? value : '.');
        }
        putchar('\n');
    }
}

static int
cmd_i2c_read(HANDLE handle, int argc, char **argv)
{
    TIMECARD_I2C_READ_REQUEST request;
    TIMECARD_I2C_TRANSFER transfer;

    if (argc < 5 || argc > 6)
        return 2;
    RtlZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.Address = (unsigned __int32)parse_ulong(argv[2], "I2C address");
    request.Subaddress =
        (unsigned __int32)parse_ulong(argv[3], "I2C subaddress");
    request.Length = (unsigned __int32)parse_ulong(argv[4], "byte count");
    request.SubaddressLength = argc == 6 ?
        (unsigned __int32)parse_ulong(argv[5], "subaddress byte count") : 1u;
    request.TimeoutMilliseconds = 100u;
    if (request.Address < 0x08 || request.Address > 0x77 ||
        request.SubaddressLength > 2 || request.Length == 0 ||
        request.Length > TIMECARD_I2C_MAX_TRANSFER) {
        return 2;
    }
    if (timecard_ioctl(handle, IOCTL_TIMECARD_I2C_READ,
                       &request, sizeof(request), &transfer,
                       sizeof(transfer), NULL))
        return 1;
    printf("I2C 0x%02lx, subaddress 0x%04lx (%lu byte%s):\n",
           (unsigned long)request.Address,
           (unsigned long)request.Subaddress,
           (unsigned long)request.SubaddressLength,
           request.SubaddressLength == 1 ? "" : "s");
    print_i2c_data(transfer.Data, transfer.Length);
    printf("[%lu byte(s), SR 0x%02lx, ISR 0x%08lx]\n",
           (unsigned long)transfer.Length,
           (unsigned long)transfer.ControllerStatus,
           (unsigned long)transfer.InterruptStatus);
    return 0;
}

static void
print_sensor_presence(const char *name, unsigned long flags)
{
    printf("%-18s %s%s%s\n", name,
           (flags & TIMECARD_SENSOR_FLAG_PRESENT) ? "present" : "not present",
           (flags & TIMECARD_SENSOR_FLAG_VALID) ? ", valid" : "",
           (flags & TIMECARD_SENSOR_FLAG_CONFIGURED) ? ", configured" : "");
}

static int
icp10100_pressure_pa(const TIMECARD_ICP10100_READING *reading,
                     double *pressure)
{
    double t;
    double quadratic;
    double s1;
    double s2;
    double s3;
    double denominator;
    double c;
    double a;
    double b;

    if ((reading->Flags & TIMECARD_SENSOR_FLAG_VALID) == 0)
        return 0;
    t = (double)reading->RawTemperature - 32768.0;
    quadratic = t * t / 16777216.0;
    s1 = 3.5 * 1048576.0 + reading->Otp[0] * quadratic;
    s2 = 2048.0 * reading->Otp[3] + reading->Otp[1] * quadratic;
    s3 = 11.5 * 1048576.0 + reading->Otp[2] * quadratic;
    denominator = s3 * (45000.0 - 80000.0) +
                  s1 * (80000.0 - 105000.0) +
                  s2 * (105000.0 - 45000.0);
    if (denominator > -0.000001 && denominator < 0.000001)
        return 0;
    c = (s1 * s2 * (45000.0 - 80000.0) +
         s2 * s3 * (80000.0 - 105000.0) +
         s3 * s1 * (105000.0 - 45000.0)) / denominator;
    if (s1 - s2 > -0.000001 && s1 - s2 < 0.000001)
        return 0;
    a = (45000.0 * s1 - 80000.0 * s2 - 35000.0 * c) /
        (s1 - s2);
    b = (45000.0 - a) * (s1 + c);
    *pressure = a + b / (c + reading->RawPressure);
    return *pressure >= 10000.0 && *pressure <= 130000.0;
}

static int
cmd_sensors(HANDLE handle)
{
    TIMECARD_SENSOR_TELEMETRY telemetry;
    unsigned long validCount = 0;

    RtlZeroMemory(&telemetry, sizeof(telemetry));
    if (timecard_ioctl(handle, IOCTL_TIMECARD_SENSOR_QUERY,
                       NULL, 0, &telemetry, sizeof(telemetry), NULL))
        return 1;

    printf("Sensor branch:     %s (prior mux 0x%02lx)\n",
           (telemetry.Flags & TIMECARD_SENSOR_FLAG_PRESENT) ?
               "available" : "unavailable",
           (unsigned long)telemetry.MuxChannelMask);
    printf("Controller/events: 0x%02lx / 0x%08lx\n",
           (unsigned long)telemetry.ControllerStatus,
           (unsigned long)telemetry.InterruptStatus);
    printf("Board profile:     %s\n",
           telemetry.BoardProfile == TIMECARD_BOARD_PROFILE_CELESTICA ?
               "Celestica R4006" :
           telemetry.BoardProfile == TIMECARD_BOARD_PROFILE_ART ?
               "Orolia ART" : "Meta/Facebook");
    if (telemetry.BoardProfile == TIMECARD_BOARD_PROFILE_CELESTICA) {
        unsigned long index;
        double pressurePa;

        for (index = 0; index < TIMECARD_SENSOR_LM75B_COUNT; ++index) {
            char name[24];

            _snprintf_s(name, sizeof(name), _TRUNCATE,
                        "LM75B sensor %lu", index + 1u);
            print_sensor_presence(
                name, telemetry.BoardTemperature[index].Flags);
            if ((telemetry.BoardTemperature[index].Flags &
                 TIMECARD_SENSOR_FLAG_VALID) != 0) {
                printf("  address/temp:    0x%02lx / %.3f C\n",
                       (unsigned long)telemetry.BoardTemperature[index].Address,
                       telemetry.BoardTemperature[index].TemperatureMilliCelsius /
                           1000.0);
                ++validCount;
            }
        }
        print_sensor_presence("SHT3x", telemetry.Humidity.Flags);
        if ((telemetry.Humidity.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0) {
            printf("  temperature/RH:  %.3f C / %.3f %% (CRC OK)\n",
                   telemetry.Humidity.TemperatureMilliCelsius / 1000.0,
                   telemetry.Humidity.HumidityMilliPercent / 1000.0);
            ++validCount;
        }
        print_sensor_presence("ICP-10100", telemetry.Pressure.Flags);
        if (icp10100_pressure_pa(&telemetry.Pressure, &pressurePa)) {
            printf("  pressure/temp:   %.2f hPa / %.3f C (CRC + OTP OK)\n",
                   pressurePa / 100.0,
                   telemetry.Pressure.TemperatureMilliCelsius / 1000.0);
            ++validCount;
        }
    } else {
        print_sensor_presence(
            telemetry.Environment.ChipId == 0x58u ? "BMP280" : "BME280",
            telemetry.Environment.Flags);
        if ((telemetry.Environment.Flags & TIMECARD_SENSOR_FLAG_PRESENT) != 0) {
            printf("  chip/status:     0x%02lx / 0x%02lx%s\n",
                   (unsigned long)telemetry.Environment.ChipId,
                   (unsigned long)telemetry.Environment.Status,
                   (telemetry.Environment.Flags &
                    TIMECARD_SENSOR_FLAG_HUMIDITY) ? " (humidity capable)" : "");
            printf("  raw T/P/H:       %ld / %lu / %lu\n",
                   (long)telemetry.Environment.RawTemperature,
                   (unsigned long)telemetry.Environment.RawPressure,
                   (unsigned long)telemetry.Environment.RawHumidity);
        }
        print_sensor_presence("INA219 +12 V", telemetry.Rail12V.Flags);
        if ((telemetry.Rail12V.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0)
            printf("  %lu mV, %ld mA, %ld mW\n",
                   (unsigned long)telemetry.Rail12V.BusMillivolts,
                   (long)telemetry.Rail12V.CurrentMilliamps,
                   (long)telemetry.Rail12V.PowerMilliwatts);
        print_sensor_presence("INA219 +5 V", telemetry.Rail5V.Flags);
        if ((telemetry.Rail5V.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0)
            printf("  %lu mV, %ld mA, %ld mW\n",
                   (unsigned long)telemetry.Rail5V.BusMillivolts,
                   (long)telemetry.Rail5V.CurrentMilliamps,
                   (long)telemetry.Rail5V.PowerMilliwatts);
        print_sensor_presence("INA219 +3.3 V", telemetry.Rail3V3.Flags);
        if ((telemetry.Rail3V3.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0)
            printf("  %lu mV, %ld mA, %ld mW\n",
                   (unsigned long)telemetry.Rail3V3.BusMillivolts,
                   (long)telemetry.Rail3V3.CurrentMilliamps,
                   (long)telemetry.Rail3V3.PowerMilliwatts);
    }
    print_sensor_presence(
        telemetry.Imu.ChipId == TIMECARD_SENSOR_BNO08X_CHIP_ID ?
            "BNO08x IMU" : "BNO055 IMU",
        telemetry.Imu.Flags);
    if ((telemetry.Imu.Flags & TIMECARD_SENSOR_FLAG_PRESENT) != 0) {
        printf("  chip/mode/error: 0x%02lx / 0x%02lx / 0x%02lx\n",
               (unsigned long)telemetry.Imu.ChipId,
               (unsigned long)telemetry.Imu.OperationMode,
               (unsigned long)telemetry.Imu.SystemError);
        if ((telemetry.Imu.Flags &
             TIMECARD_SENSOR_FLAG_TEMPERATURE) != 0) {
            if ((telemetry.Imu.Flags &
                 TIMECARD_SENSOR_FLAG_TEMPERATURE_Q7) != 0) {
                printf("  temperature:     %.2f C (SH-2 report 0x0e)\n",
                       (double)telemetry.Imu.Temperature / 128.0);
            } else {
                printf("  temperature:     %ld C\n",
                       (long)telemetry.Imu.Temperature);
            }
        } else {
            printf("  temperature:     unavailable\n");
        }
        printf("  accel XYZ:       %ld / %ld / %ld\n",
               (long)telemetry.Imu.AccelerationX,
               (long)telemetry.Imu.AccelerationY,
               (long)telemetry.Imu.AccelerationZ);
        printf("  linear XYZ:      %ld / %ld / %ld\n",
               (long)telemetry.Imu.LinearAccelerationX,
               (long)telemetry.Imu.LinearAccelerationY,
               (long)telemetry.Imu.LinearAccelerationZ);
        printf("  gravity XYZ:     %ld / %ld / %ld\n",
               (long)telemetry.Imu.GravityX,
               (long)telemetry.Imu.GravityY,
               (long)telemetry.Imu.GravityZ);
        printf("  gyro XYZ:        %ld / %ld / %ld\n",
               (long)telemetry.Imu.GyroscopeX,
               (long)telemetry.Imu.GyroscopeY,
               (long)telemetry.Imu.GyroscopeZ);
        printf("  magnetic XYZ:    %ld / %ld / %ld\n",
               (long)telemetry.Imu.MagneticX,
               (long)telemetry.Imu.MagneticY,
               (long)telemetry.Imu.MagneticZ);
        printf("  quaternion WXYZ: %ld / %ld / %ld / %ld\n",
               (long)telemetry.Imu.QuaternionW,
               (long)telemetry.Imu.QuaternionX,
               (long)telemetry.Imu.QuaternionY,
               (long)telemetry.Imu.QuaternionZ);
    }
    if (telemetry.BoardProfile != TIMECARD_BOARD_PROFILE_CELESTICA) {
        if ((telemetry.Environment.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0)
            ++validCount;
        if ((telemetry.Rail12V.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0)
            ++validCount;
        if ((telemetry.Rail5V.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0)
            ++validCount;
        if ((telemetry.Rail3V3.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0)
            ++validCount;
    }
    if ((telemetry.Imu.Flags & TIMECARD_SENSOR_FLAG_VALID) != 0)
        ++validCount;
    if (validCount == 0) {
        fprintf(stderr, "timecardctl: no valid sensor samples were returned\n");
        return 1;
    }
    printf("%lu valid sensor block(s).\n", validCount);
    return 0;
}

static void
print_led(const TIMECARD_LED_CONTROL *control)
{
    printf("LED %lu: %s%s, RGB %lu/%lu/%lu, current %lu",
           (unsigned long)control->Led + 1u,
           (control->Flags & TIMECARD_LED_FLAG_PRESENT) ?
               "present" : "not present",
           (control->Flags & TIMECARD_LED_FLAG_ENABLED) ?
               ", enabled" : "",
           (unsigned long)control->Red,
           (unsigned long)control->Green,
           (unsigned long)control->Blue,
           (unsigned long)control->GlobalCurrent);
    if ((control->Flags & TIMECARD_LED_FLAG_FAULT_VALID) != 0) {
        printf(", open 0x%05lx, short 0x%05lx",
               (unsigned long)control->OpenOutputMask,
               (unsigned long)control->ShortOutputMask);
    }
    printf("\n");
}

static int
cmd_led_status(HANDLE handle, int argc, char **argv)
{
    unsigned long first = 1;
    unsigned long last = TIMECARD_LED_COUNT;
    unsigned long led;

    if (argc > 3)
        return 2;
    if (argc == 3) {
        first = parse_ulong(argv[2], "LED");
        if (first == 0 || first > TIMECARD_LED_COUNT)
            return 2;
        last = first;
    }
    for (led = first; led <= last; ++led) {
        TIMECARD_LED_CONTROL control;

        RtlZeroMemory(&control, sizeof(control));
        control.Size = sizeof(control);
        control.Led = (unsigned __int32)(led - 1u);
        if (timecard_ioctl(handle, IOCTL_TIMECARD_LED_QUERY,
                           &control, sizeof(control), &control,
                           sizeof(control), NULL))
            return 1;
        print_led(&control);
    }
    return 0;
}

static int
cmd_led_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_LED_CONTROL control;

    if (argc < 6 || argc > 7)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Led =
        (unsigned __int32)parse_ulong(argv[2], "LED");
    control.Red =
        (unsigned __int32)parse_ulong(argv[3], "red");
    control.Green =
        (unsigned __int32)parse_ulong(argv[4], "green");
    control.Blue =
        (unsigned __int32)parse_ulong(argv[5], "blue");
    control.GlobalCurrent = argc == 7 ?
        (unsigned __int32)parse_ulong(argv[6], "global current") : 96u;
    if (control.Led == 0 || control.Led > TIMECARD_LED_COUNT ||
        control.Red > 255u || control.Green > 255u ||
        control.Blue > 255u || control.GlobalCurrent == 0 ||
        control.GlobalCurrent > TIMECARD_LED_MAX_GLOBAL_CURRENT) {
        return 2;
    }
    --control.Led;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_LED_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_led(&control);
    return 0;
}

static int
cmd_led_test(HANDLE handle)
{
    TIMECARD_LED_CONTROL saved[TIMECARD_LED_COUNT];
    TIMECARD_LED_CONTROL control;
    unsigned long led;
    int failed = 0;

    RtlZeroMemory(saved, sizeof(saved));
    for (led = 0; led < TIMECARD_LED_COUNT; ++led) {
        saved[led].Size = sizeof(saved[led]);
        saved[led].Led = (unsigned __int32)led;
        if (timecard_ioctl(handle, IOCTL_TIMECARD_LED_QUERY,
                           &saved[led], sizeof(saved[led]), &saved[led],
                           sizeof(saved[led]), NULL))
            return 1;
    }

    printf("Forcing all 18 IS32FL3207 outputs on for three seconds...\n");
    for (led = 0; led < TIMECARD_LED_COUNT; ++led) {
        RtlZeroMemory(&control, sizeof(control));
        control.Size = sizeof(control);
        control.Led = (unsigned __int32)led;
        control.Red = 255u;
        control.Green = 255u;
        control.Blue = 255u;
        control.GlobalCurrent = TIMECARD_LED_MAX_GLOBAL_CURRENT;
        if (led == 0)
            control.Flags = TIMECARD_LED_FLAG_RESET_TEST;
        if (timecard_ioctl(handle, IOCTL_TIMECARD_LED_SET,
                           &control, sizeof(control), &control,
                           sizeof(control), NULL)) {
            failed = 1;
            goto Restore;
        }
        if (led == 0 &&
            (control.Flags & TIMECARD_LED_FLAG_SDB_HIGH) == 0) {
            fprintf(stderr,
                    "timecardctl: IS32FL3207 SDB is low; check R109 and U6\n");
            failed = 1;
        }
    }

    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Led = 0;
    control.Red = 255u;
    control.Green = 255u;
    control.Blue = 255u;
    control.GlobalCurrent = TIMECARD_LED_MAX_GLOBAL_CURRENT;
    control.Flags = TIMECARD_LED_FLAG_DC_TEST;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_LED_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL)) {
        failed = 1;
        goto Restore;
    }
    print_led(&control);
    if ((control.Flags & TIMECARD_LED_FLAG_FAULT_VALID) == 0) {
        printf("Warning: LED fault diagnostics unavailable\n");
    } else if ((control.OpenOutputMask | control.ShortOutputMask) != 0) {
        printf("Warning: LED diagnostic mask: open 0x%05lx, short 0x%05lx\n",
               (unsigned long)control.OpenOutputMask,
               (unsigned long)control.ShortOutputMask);
    }
    Sleep(3000u);

Restore:
    for (led = 0; led < TIMECARD_LED_COUNT; ++led) {
        RtlZeroMemory(&control, sizeof(control));
        control.Size = sizeof(control);
        control.Led = (unsigned __int32)led;
        control.Red = saved[led].Red;
        control.Green = saved[led].Green;
        control.Blue = saved[led].Blue;
        control.GlobalCurrent = saved[led].GlobalCurrent != 0 ?
            saved[led].GlobalCurrent : 1u;
        if (timecard_ioctl(handle, IOCTL_TIMECARD_LED_SET,
                           &control, sizeof(control), &control,
                           sizeof(control), NULL))
            failed = 1;
    }
    printf("Previous LED colors restored.\n");
    return failed;
}

static long
parse_signed_long(const char *text, const char *name)
{
    char *end;
    long value = strtol(text, &end, 0);

    if (*text == '\0' || *end != '\0') {
        fprintf(stderr, "timecardctl: invalid %s: %s\n", name, text);
        exit(2);
    }
    return value;
}

static unsigned __int64
parse_unsigned64(const char *text, const char *name)
{
    char *end;
    unsigned __int64 value = _strtoui64(text, &end, 0);

    if (*text == '\0' || *end != '\0') {
        fprintf(stderr, "timecardctl: invalid %s: %s\n", name, text);
        exit(2);
    }
    return value;
}

static int
parse_on_off(const char *text, unsigned __int32 enabledFlag,
             unsigned __int32 *flags)
{
    if (_stricmp(text, "on") == 0 || _stricmp(text, "enable") == 0) {
        *flags |= enabledFlag;
        return 0;
    }
    return _stricmp(text, "off") == 0 ||
           _stricmp(text, "disable") == 0 ? 0 : 1;
}

static unsigned long
parse_pps_core(const char *text)
{
    if (_stricmp(text, "master") == 0 || _stricmp(text, "output") == 0)
        return TIMECARD_PPS_CORE_MASTER;
    if (_stricmp(text, "slave") == 0 || _stricmp(text, "input") == 0)
        return TIMECARD_PPS_CORE_SLAVE;
    return parse_ulong(text, "PPS core");
}

static unsigned long
parse_timecode_format(const char *text)
{
    if (_stricmp(text, "irig") == 0)
        return TIMECARD_TIMECODE_FORMAT_IRIG;
    if (_stricmp(text, "dcf") == 0)
        return TIMECARD_TIMECODE_FORMAT_DCF;
    return parse_ulong(text, "timecode format");
}

static unsigned long
parse_timecode_role(const char *text)
{
    if (_stricmp(text, "master") == 0 || _stricmp(text, "output") == 0)
        return TIMECARD_TIMECODE_ROLE_MASTER;
    if (_stricmp(text, "slave") == 0 || _stricmp(text, "input") == 0)
        return TIMECARD_TIMECODE_ROLE_SLAVE;
    return parse_ulong(text, "timecode role");
}

static int
cmd_fpga_status(HANDLE handle)
{
    TIMECARD_FPGA_CAPABILITIES capabilities;
    TIMECARD_FPGA_IMAGE_INFO image;
    BOOL imageQueryOk;
    DWORD returned;

    RtlZeroMemory(&capabilities, sizeof(capabilities));
    if (timecard_ioctl(handle, IOCTL_TIMECARD_GET_FPGA_CAPABILITIES,
                       NULL, 0, &capabilities, sizeof(capabilities), NULL))
        return 1;
    printf("FPGA ABI/core/features: %lu / 0x%08lx / 0x%08lx\n",
           (unsigned long)capabilities.AbiVersion,
           (unsigned long)capabilities.CoreMask,
           (unsigned long)capabilities.FeatureFlags);
    printf("Known gaps:             0x%08lx\n",
           (unsigned long)capabilities.KnownGaps);
    if ((capabilities.KnownGaps &
         TIMECARD_FPGA_GAP_TIMESTAMP_INTERRUPTS) != 0)
        printf("  - Windows signal-timestamper interrupt path\n");
    if ((capabilities.KnownGaps &
         TIMECARD_FPGA_GAP_CONFIGURATION_SLAVE) != 0)
        printf("  - Configuration Slave discovery ROM\n");
    if ((capabilities.KnownGaps &
         TIMECARD_FPGA_GAP_OPTIONAL_CLOCK_REGISTERS) != 0)
        printf("  - synthesis-optional Clock telemetry registers\n");
    if ((capabilities.KnownGaps &
         TIMECARD_FPGA_GAP_TOD_MASTER_UTC_HANDSHAKE) != 0)
        printf("  - synthesis-optional ToD Master UTC handshake\n");
    if ((capabilities.KnownGaps &
         TIMECARD_FPGA_GAP_SYNTHESIS_FEATURE_REPORTING) != 0)
        printf("  - per-bitstream synthesis feature reporting\n");
    printf("Layout / board profile: %lu / %lu\n",
           (unsigned long)capabilities.Layout,
           (unsigned long)capabilities.BoardProfile);
    if (capabilities.AbiVersion < 13u) {
        printf("FPGA image identity:    requires ABI 13 or newer\n");
    } else if (capabilities.Layout == TIMECARD_LAYOUT_ART) {
        printf("FPGA image identity:    unsupported on ART (no standard static register)\n");
    } else {
        RtlZeroMemory(&image, sizeof(image));
        returned = 0;
        imageQueryOk = DeviceIoControl(
            handle, IOCTL_TIMECARD_FPGA_IMAGE_QUERY, NULL, 0,
            &image, sizeof(image), &returned, NULL);
        if (imageQueryOk && returned == sizeof(image)) {
            printf("FPGA image identity:    %s %lu.%lu%s\n",
                   (image.Flags &
                    TIMECARD_FPGA_IMAGE_FLAG_FPGA_FIRMWARE) != 0 ?
                       "FPGA" : "SOM",
                   (unsigned long)(image.ImageVersion >> 8),
                   (unsigned long)(image.ImageVersion & 0xffu),
                   (image.Flags & TIMECARD_FPGA_IMAGE_FLAG_LOADER) != 0 ?
                       " (loader encoding)" : "");
            printf("Image raw/tag/register: 0x%08lx / %lu / 0x%08lx\n",
                   (unsigned long)image.RawVersion,
                   (unsigned long)image.ImageTag,
                   (unsigned long)image.RegisterOffset);
        } else if (!imageQueryOk) {
            printf("FPGA image identity:    unavailable (error %lu)\n",
                   GetLastError());
        } else {
            printf("FPGA image identity:    invalid response size %lu\n",
                   (unsigned long)returned);
        }
    }
    return 0;
}

static int
cmd_clock_telemetry(HANDLE handle)
{
    TIMECARD_CLOCK_TELEMETRY telemetry;

    RtlZeroMemory(&telemetry, sizeof(telemetry));
    if (timecard_ioctl(handle, IOCTL_TIMECARD_CLOCK_TELEMETRY_QUERY,
                       NULL, 0, &telemetry, sizeof(telemetry), NULL))
        return 1;
    printf("Clock flags/version:    0x%08lx / 0x%08lx\n",
           (unsigned long)telemetry.Flags,
           (unsigned long)telemetry.Version);
    printf("Control/status/select:  0x%08lx / 0x%08lx / 0x%08lx\n",
           (unsigned long)telemetry.Control,
           (unsigned long)telemetry.Status,
           (unsigned long)telemetry.Select);
    printf("In-sync threshold:      %lu ns\n",
           (unsigned long)telemetry.InSyncThreshold);
    if ((telemetry.Flags &
         TIMECARD_CLOCK_TELEMETRY_FLAG_SERVO_AVAILABLE) != 0) {
        printf("Servo offset P/I:       %lu / %lu\n",
               (unsigned long)telemetry.ServoOffsetP,
               (unsigned long)telemetry.ServoOffsetI);
        printf("Servo drift P/I:        %lu / %lu\n",
               (unsigned long)telemetry.ServoDriftP,
               (unsigned long)telemetry.ServoDriftI);
    }
    if ((telemetry.Flags & TIMECARD_CLOCK_TELEMETRY_FLAG_LOG_AVAILABLE) != 0) {
        printf("Last offset/drift:      %ld ns / %ld ppb\n",
               (long)telemetry.StatusOffsetNanoseconds,
               (long)telemetry.StatusDriftPpb);
    }
    if ((telemetry.Flags &
         TIMECARD_CLOCK_TELEMETRY_FLAG_FRACTIONAL_LOG) != 0) {
        printf("Offset/drift fraction:  0x%04lx / 0x%04lx\n",
               (unsigned long)telemetry.StatusOffsetFraction,
               (unsigned long)telemetry.StatusDriftFraction);
    }
    return 0;
}

static void
print_pps(const TIMECARD_PPS_CONTROL *control)
{
    printf("PPS %s: %s, %s, delay %ld ns, width %lu ms\n",
           control->Core == TIMECARD_PPS_CORE_MASTER ? "master" : "slave",
           (control->Flags & TIMECARD_PPS_FLAG_ENABLED) != 0 ?
               "enabled" : "disabled",
           control->Polarity == TIMECARD_PPS_POLARITY_ACTIVE_HIGH ?
               "active-high" : "active-low",
           (long)control->CableDelayNanoseconds,
           (unsigned long)control->PulseWidthMilliseconds);
    printf("  flags/control/status/version: 0x%08lx / 0x%08lx / 0x%08lx / 0x%08lx\n",
           (unsigned long)control->Flags,
           (unsigned long)control->Control,
           (unsigned long)control->Status,
           (unsigned long)control->Version);
}

static int
query_pps(HANDLE handle, unsigned long core)
{
    TIMECARD_PPS_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Core = (unsigned __int32)core;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_PPS_QUERY,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_pps(&control);
    return 0;
}

static int
cmd_pps_status(HANDLE handle, int argc, char **argv)
{
    if (argc == 3)
        return query_pps(handle, parse_pps_core(argv[2]));
    if (argc != 2)
        return 2;
    return query_pps(handle, TIMECARD_PPS_CORE_MASTER) |
           query_pps(handle, TIMECARD_PPS_CORE_SLAVE);
}

static int
cmd_pps_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_PPS_CONTROL control;

    if (argc < 7 || argc > 8)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Core = (unsigned __int32)parse_pps_core(argv[2]);
    if (parse_on_off(argv[3], TIMECARD_PPS_FLAG_ENABLED, &control.Flags))
        return 2;
    if (_stricmp(argv[4], "active-high") == 0)
        control.Polarity = TIMECARD_PPS_POLARITY_ACTIVE_HIGH;
    else if (_stricmp(argv[4], "active-low") != 0)
        return 2;
    control.CableDelayNanoseconds =
        (signed __int32)parse_signed_long(argv[5], "PPS cable delay");
    control.PulseWidthMilliseconds =
        (unsigned __int32)parse_ulong(argv[6], "PPS pulse width");
    if (argc == 8) {
        if (_stricmp(argv[7], "clear") != 0)
            return 2;
        control.Flags |= TIMECARD_PPS_FLAG_CLEAR_ERRORS;
    }
    if (timecard_ioctl(handle, IOCTL_TIMECARD_PPS_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_pps(&control);
    return 0;
}

static void
print_timecode(const TIMECARD_TIMECODE_CONTROL *control)
{
    printf("%s %s: %s, correction %ld s, delay %ld ns\n",
           control->Format == TIMECARD_TIMECODE_FORMAT_IRIG ? "IRIG" : "DCF",
           control->Role == TIMECARD_TIMECODE_ROLE_MASTER ? "master" : "slave",
           (control->Flags & TIMECARD_TIMECODE_FLAG_ENABLED) != 0 ?
               "enabled" : "disabled",
           (long)control->CorrectionSeconds,
           (long)control->DelayNanoseconds);
    printf("  mode/code/control-bits/bit-position: %lu / %lu / 0x%08lx / %lu\n",
           (unsigned long)control->Mode, (unsigned long)control->Code,
           (unsigned long)control->ControlBits,
           (unsigned long)control->BitPosition);
    if (control->Format == TIMECARD_TIMECODE_FORMAT_IRIG) {
        printf("  amplitude modulation: ");
        if ((control->Flags & TIMECARD_TIMECODE_FLAG_AM_WRITABLE) != 0u)
            printf("%s\n", control->AmplitudeModulation != 0u ?
                   "enabled" : "disabled");
        else
            printf("unavailable (exact-image contract/core revision)\n");
        printf("  manual year: ");
        if ((control->Flags & TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE) != 0u)
            printf("%lu\n", (unsigned long)control->ManualYear);
        else
            printf("unavailable (IRIG slave exact-image contract/core revision)\n");
    }
    printf("  flags/control/status/version: 0x%08lx / 0x%08lx / 0x%08lx / 0x%08lx\n",
           (unsigned long)control->Flags,
           (unsigned long)control->Control,
           (unsigned long)control->Status,
           (unsigned long)control->Version);
}

static int
query_timecode(HANDLE handle, unsigned long format, unsigned long role)
{
    TIMECARD_TIMECODE_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Format = (unsigned __int32)format;
    control.Role = (unsigned __int32)role;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_TIMECODE_QUERY,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_timecode(&control);
    return 0;
}

static int
cmd_timecode_status(HANDLE handle, int argc, char **argv)
{
    if (argc == 4) {
        return query_timecode(handle, parse_timecode_format(argv[2]),
                              parse_timecode_role(argv[3]));
    }
    if (argc != 2)
        return 2;
    return query_timecode(handle, TIMECARD_TIMECODE_FORMAT_IRIG,
                          TIMECARD_TIMECODE_ROLE_MASTER) |
           query_timecode(handle, TIMECARD_TIMECODE_FORMAT_IRIG,
                          TIMECARD_TIMECODE_ROLE_SLAVE) |
           query_timecode(handle, TIMECARD_TIMECODE_FORMAT_DCF,
                          TIMECARD_TIMECODE_ROLE_MASTER) |
           query_timecode(handle, TIMECARD_TIMECODE_FORMAT_DCF,
                          TIMECARD_TIMECODE_ROLE_SLAVE);
}

static int
cmd_timecode_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_TIMECODE_CONTROL control;
    TIMECARD_TIMECODE_CONTROL current;
    int optional;
    int controlBitsSeen = 0;
    int amplitudeSeen = 0;
    int yearSeen = 0;
    int clearSeen = 0;

    if (argc < 9 || argc > 15)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Format = (unsigned __int32)parse_timecode_format(argv[2]);
    control.Role = (unsigned __int32)parse_timecode_role(argv[3]);

    /* Preserve the IRIG control field unless the caller replaces it. */
    RtlZeroMemory(&current, sizeof(current));
    current.Size = sizeof(current);
    current.Format = control.Format;
    current.Role = control.Role;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_TIMECODE_QUERY,
                       &current, sizeof(current), &current,
                       sizeof(current), NULL))
        return 1;
    control.ControlBits = current.ControlBits;
    control.AmplitudeModulation = current.AmplitudeModulation;
    control.ManualYear = current.ManualYear;

    if (parse_on_off(argv[4], TIMECARD_TIMECODE_FLAG_ENABLED,
                     &control.Flags))
        return 2;
    control.CorrectionSeconds =
        (signed __int32)parse_signed_long(argv[5], "time correction");
    control.DelayNanoseconds =
        (signed __int32)parse_signed_long(argv[6], "propagation delay");
    control.Mode = (unsigned __int32)parse_ulong(argv[7], "IRIG mode");
    control.Code = (unsigned __int32)parse_ulong(argv[8], "IRIG code");

    optional = 9;
    while (optional < argc) {
        if (_stricmp(argv[optional], "--am") == 0) {
            if (amplitudeSeen || optional + 1 >= argc ||
                control.Format != TIMECARD_TIMECODE_FORMAT_IRIG ||
                (current.Flags &
                 TIMECARD_TIMECODE_FLAG_AM_WRITABLE) == 0u)
                return 2;
            if (_stricmp(argv[optional + 1], "on") == 0 ||
                _stricmp(argv[optional + 1], "enable") == 0) {
                control.AmplitudeModulation = 1u;
            } else if (_stricmp(argv[optional + 1], "off") == 0 ||
                       _stricmp(argv[optional + 1], "disable") == 0) {
                control.AmplitudeModulation = 0u;
            } else {
                return 2;
            }
            amplitudeSeen = 1;
            optional += 2;
        } else if (_stricmp(argv[optional], "--year") == 0) {
            if (yearSeen || optional + 1 >= argc ||
                control.Format != TIMECARD_TIMECODE_FORMAT_IRIG ||
                control.Role != TIMECARD_TIMECODE_ROLE_SLAVE ||
                (current.Flags &
                 TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE) == 0u)
                return 2;
            control.ManualYear = parse_u32_bounded(
                argv[optional + 1], "IRIG manual year", 1970u, 2069u);
            yearSeen = 1;
            optional += 2;
        } else if (_stricmp(argv[optional], "clear") == 0) {
            if (clearSeen)
                return 2;
            control.Flags |= TIMECARD_TIMECODE_FLAG_CLEAR_ERRORS;
            clearSeen = 1;
            ++optional;
        } else {
            if (controlBitsSeen ||
                control.Format != TIMECARD_TIMECODE_FORMAT_IRIG ||
                control.Role != TIMECARD_TIMECODE_ROLE_MASTER)
                return 2;
            control.ControlBits = parse_u32_bounded(
                argv[optional], "IRIG control bits", 0u, 0x07ffffffu);
            controlBitsSeen = 1;
            ++optional;
        }
    }
    if (control.Format != TIMECARD_TIMECODE_FORMAT_IRIG &&
        (amplitudeSeen || yearSeen || controlBitsSeen))
            return 2;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_TIMECODE_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_timecode(&control);
    return 0;
}

static void
print_tod(const TIMECARD_TOD_CONTROL *control)
{
    static const char *protocols[] = {
        "NMEA", "UBX", "TSIP", "ESIP", "PFEC"
    };
    const char *protocol = control->Protocol < ARRAYSIZE(protocols) ?
        protocols[control->Protocol] : "invalid";

    printf("TOD slave: %s, %s, GNSS %lu, %lu baud, %s polarity\n",
           (control->Flags & TIMECARD_TOD_FLAG_ENABLED) != 0 ?
               "enabled" : "disabled",
           protocol, (unsigned long)control->Gnss,
           (unsigned long)control->Baud,
           control->Polarity != 0 ? "inverted" : "normal");
    printf("  correction/disable mask: %ld s / 0x%02lx\n",
           (long)control->CorrectionSeconds,
           (unsigned long)control->MessageDisableMask);
    printf("  flags/control/status/version: 0x%08lx / 0x%08lx / 0x%08lx / 0x%08lx\n",
           (unsigned long)control->Flags,
           (unsigned long)control->Control,
           (unsigned long)control->Status,
           (unsigned long)control->Version);
    if ((control->Flags & TIMECARD_TOD_FLAG_UTC_TELEMETRY_VALID) != 0u) {
        printf("  UTC status/time-to-leap: 0x%08lx / %ld s\n",
               (unsigned long)control->UtcStatus,
               (long)control->TimeToLeapSeconds);
    } else {
        printf("  UTC telemetry: not read "
               "(exact-image contract or protocol revision does not permit access)\n");
    }
    if ((control->Flags & TIMECARD_TOD_FLAG_GNSS_TELEMETRY_VALID) != 0u) {
        printf("  GNSS status/satellites: 0x%08lx / 0x%08lx\n",
               (unsigned long)control->GnssStatus,
               (unsigned long)control->Satellites);
    } else {
        printf("  GNSS telemetry: not read "
               "(exact-image contract or protocol revision does not permit access)\n");
    }
}

static int
cmd_tod_status(HANDLE handle)
{
    TIMECARD_TOD_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    if (timecard_ioctl(handle, IOCTL_TIMECARD_TOD_QUERY,
                       NULL, 0, &control, sizeof(control), NULL))
        return 1;
    print_tod(&control);
    return 0;
}

static unsigned long
parse_tod_protocol(const char *text)
{
    if (_stricmp(text, "nmea") == 0)
        return TIMECARD_TOD_PROTOCOL_NMEA;
    if (_stricmp(text, "ubx") == 0)
        return TIMECARD_TOD_PROTOCOL_UBX;
    if (_stricmp(text, "tsip") == 0)
        return TIMECARD_TOD_PROTOCOL_TSIP;
    if (_stricmp(text, "esip") == 0)
        return TIMECARD_TOD_PROTOCOL_ESIP;
    if (_stricmp(text, "pfec") == 0)
        return TIMECARD_TOD_PROTOCOL_PFEC;
    return parse_ulong(text, "TOD protocol");
}

static int
cmd_tod_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_TOD_CONTROL control;

    if (argc < 9 || argc > 10)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    if (parse_on_off(argv[2], TIMECARD_TOD_FLAG_ENABLED, &control.Flags))
        return 2;
    control.Protocol = (unsigned __int32)parse_tod_protocol(argv[3]);
    control.Gnss = (unsigned __int32)parse_ulong(argv[4], "GNSS selector");
    control.Baud = (unsigned __int32)parse_ulong(argv[5], "TOD baud");
    if (_stricmp(argv[6], "inverted") == 0)
        control.Polarity = 1u;
    else if (_stricmp(argv[6], "normal") != 0)
        return 2;
    control.CorrectionSeconds =
        (signed __int32)parse_signed_long(argv[7], "TOD correction");
    control.MessageDisableMask =
        (unsigned __int32)parse_ulong(argv[8], "message disable mask");
    if (argc == 10) {
        if (_stricmp(argv[9], "clear") != 0)
            return 2;
        control.Flags |= TIMECARD_TOD_FLAG_CLEAR_ERRORS;
    }
    if (timecard_ioctl(handle, IOCTL_TIMECARD_TOD_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_tod(&control);
    return 0;
}

static void
print_signal(const TIMECARD_SIGNAL_CONTROL *control)
{
    printf("Signal generator %lu: %s, period %llu ns, pulse %llu ns, phase %llu ns\n",
           (unsigned long)control->Generator,
           (control->Flags & TIMECARD_SIGNAL_FLAG_ENABLED) != 0 ?
               "enabled" : "disabled",
           (unsigned long long)control->PeriodNanoseconds,
           (unsigned long long)control->PulseNanoseconds,
           (unsigned long long)control->PhaseNanoseconds);
    printf("  polarity/repeat/cable/status/version: %s / %lu / %lu ns / 0x%08lx / 0x%08lx%s%s\n",
           (control->Flags & TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH) != 0 ?
               "active-high" : "active-low",
           (unsigned long)control->RepeatCount,
           (unsigned long)control->CableDelayNanoseconds,
           (unsigned long)control->Status,
           (unsigned long)control->Version,
           (control->Flags & TIMECARD_SIGNAL_FLAG_ERROR) != 0 ?
               " ERROR" : "",
           (control->Flags & TIMECARD_SIGNAL_FLAG_TIME_JUMP) != 0 ?
               " TIME_JUMP" : "");
    printf("  start: %llu.%09lu PHC seconds\n",
           (unsigned long long)control->StartSeconds,
           (unsigned long)control->StartNanoseconds);
}

static int
parse_signal_polarity(const char *text, unsigned __int32 *flags)
{
    if (_stricmp(text, "active-high") == 0 ||
        _stricmp(text, "inverted") == 0) {
        *flags |= TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH;
        return 0;
    }
    if (_stricmp(text, "active-low") == 0 ||
        _stricmp(text, "normal") == 0)
        return 0;
    return 1;
}

static int
query_signal(HANDLE handle, unsigned long generator)
{
    TIMECARD_SIGNAL_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Generator = (unsigned __int32)generator;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_SIGNAL_QUERY,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_signal(&control);
    return 0;
}

static int
cmd_signal_status(HANDLE handle, int argc, char **argv)
{
    unsigned long generator;
    int result = 0;

    if (argc == 3)
        return query_signal(handle, parse_ulong(argv[2], "generator"));
    if (argc != 2)
        return 2;
    for (generator = 1; generator <= TIMECARD_SIGNAL_COUNT; ++generator)
        result |= query_signal(handle, generator);
    return result;
}

static int
cmd_signal_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_SIGNAL_CONTROL control;

    if (argc != 10)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Generator =
        (unsigned __int32)parse_ulong(argv[2], "generator");
    if (parse_on_off(argv[3], TIMECARD_SIGNAL_FLAG_ENABLED, &control.Flags))
        return 2;
    control.PeriodNanoseconds = parse_unsigned64(argv[4], "period");
    control.PulseNanoseconds = parse_unsigned64(argv[5], "pulse width");
    control.PhaseNanoseconds = parse_unsigned64(argv[6], "phase");
    if (parse_signal_polarity(argv[7], &control.Flags))
        return 2;
    control.RepeatCount =
        (unsigned __int32)parse_ulong(argv[8], "repeat count");
    control.CableDelayNanoseconds =
        (unsigned __int32)parse_ulong(argv[9], "cable delay");
    if (timecard_ioctl(handle, IOCTL_TIMECARD_SIGNAL_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_signal(&control);
    return 0;
}

static int
cmd_signal_set_at(HANDLE handle, int argc, char **argv)
{
    TIMECARD_SIGNAL_CONTROL control;

    if (argc != 11)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Generator =
        (unsigned __int32)parse_ulong(argv[2], "generator");
    if (parse_on_off(argv[3], TIMECARD_SIGNAL_FLAG_ENABLED, &control.Flags))
        return 2;
    control.Flags |= TIMECARD_SIGNAL_FLAG_ABSOLUTE_START;
    control.PeriodNanoseconds = parse_unsigned64(argv[4], "period");
    control.PulseNanoseconds = parse_unsigned64(argv[5], "pulse width");
    control.StartSeconds = parse_unsigned64(argv[6], "start PHC seconds");
    control.StartNanoseconds =
        (unsigned __int32)parse_ulong(argv[7], "start nanoseconds");
    if (parse_signal_polarity(argv[8], &control.Flags))
        return 2;
    control.RepeatCount =
        (unsigned __int32)parse_ulong(argv[9], "repeat count");
    control.CableDelayNanoseconds =
        (unsigned __int32)parse_ulong(argv[10], "cable delay");
    if (timecard_ioctl(handle, IOCTL_TIMECARD_SIGNAL_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_signal(&control);
    return 0;
}

static int
cmd_signal_clear(HANDLE handle, int argc, char **argv)
{
    TIMECARD_SIGNAL_CONTROL control;

    if (argc != 3)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Generator =
        (unsigned __int32)parse_ulong(argv[2], "generator");
    control.Flags = TIMECARD_SIGNAL_FLAG_CLEAR_STATUS;
    if (timecard_ioctl(handle, IOCTL_TIMECARD_SIGNAL_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_signal(&control);
    return 0;
}

static const char *
timestamp_channel_name(unsigned long channel)
{
    static const char *names[TIMECARD_TIMESTAMP_COUNT] = {
        "GNSS1", "TS1", "TS2", "TS3", "TS4", "PHC/PPS"
    };

    return channel < TIMECARD_TIMESTAMP_COUNT ? names[channel] : "invalid";
}

static unsigned long
parse_timestamp_channel(const char *text)
{
    if (_stricmp(text, "gnss") == 0 || _stricmp(text, "gnss1") == 0)
        return TIMECARD_TIMESTAMP_GNSS1;
    if (_stricmp(text, "ts1") == 0)
        return TIMECARD_TIMESTAMP_TS1;
    if (_stricmp(text, "ts2") == 0)
        return TIMECARD_TIMESTAMP_TS2;
    if (_stricmp(text, "ts3") == 0)
        return TIMECARD_TIMESTAMP_TS3;
    if (_stricmp(text, "ts4") == 0)
        return TIMECARD_TIMESTAMP_TS4;
    if (_stricmp(text, "phc") == 0 || _stricmp(text, "pps") == 0)
        return TIMECARD_TIMESTAMP_PHC;
    return parse_u32_bounded(text, "timestamp channel", 0u,
                             TIMECARD_TIMESTAMP_COUNT - 1u);
}

static void
print_timestamp_control(const TIMECARD_TIMESTAMP_CONTROL *control)
{
    printf("Timestamp %lu (%s): %s, %s edge",
           (unsigned long)control->Channel,
           timestamp_channel_name(control->Channel),
           (control->Flags & TIMECARD_TIMESTAMP_FLAG_ENABLED) != 0u ?
               "enabled" : "disabled",
           control->Polarity == TIMECARD_TIMESTAMP_POLARITY_RISING ?
               "rising" : "falling");
    if ((control->Flags &
         TIMECARD_TIMESTAMP_FLAG_CABLE_DELAY_WRITABLE) != 0u)
        printf(", cable delay %lu ns\n",
               (unsigned long)control->CableDelayNanoseconds);
    else
        printf(", cable delay unavailable on this core layout\n");
    printf("  version/status/flags: 0x%08lx / 0x%08lx / 0x%08lx\n",
           (unsigned long)control->Version,
           (unsigned long)control->Status,
           (unsigned long)control->Flags);
    printf("  IRQ available/mask/pending: %s / %lu / %lu\n",
           (control->Flags & TIMECARD_TIMESTAMP_FLAG_IRQ_AVAILABLE) != 0u ?
               "yes" : "no",
           (unsigned long)control->InterruptMask,
           (unsigned long)control->Interrupt);
    printf("  queue depth/dropped: %lu / %lu; event/timestamp count: %lu / %lu\n",
           (unsigned long)control->QueueDepth,
           (unsigned long)control->DroppedEvents,
           (unsigned long)control->EventCount,
           (unsigned long)control->TimestampCount);
    if ((control->Flags & TIMECARD_TIMESTAMP_FLAG_EVENT_VALID) != 0u) {
        printf("  latest: ");
        print_card_time(&control->Time);
        printf("\n");
    } else {
        printf("  latest: no valid timestamp captured\n");
    }
    if ((control->Flags & TIMECARD_TIMESTAMP_FLAG_DATA_AVAILABLE) != 0u) {
        printf("  payload width/value: %lu bits / 0x%08lx%s\n",
               (unsigned long)control->DataWidth,
               (unsigned long)control->Data,
               (control->Flags &
                TIMECARD_TIMESTAMP_FLAG_DATA_TRUNCATED) != 0u ?
                   " (display truncated)" : "");
    }
}

static int
query_timestamp(HANDLE handle, unsigned long channel)
{
    TIMECARD_TIMESTAMP_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Channel = (unsigned __int32)channel;
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_TIMESTAMP_QUERY,
                             &control, sizeof(control), &control,
                             sizeof(control)))
        return 1;
    print_timestamp_control(&control);
    return 0;
}

static int
cmd_timestamp_status(HANDLE handle, int argc, char **argv)
{
    unsigned long channel;
    int result = 0;

    if (argc == 3)
        return query_timestamp(handle, parse_timestamp_channel(argv[2]));
    if (argc != 2)
        return 2;
    for (channel = 0; channel < TIMECARD_TIMESTAMP_COUNT; ++channel)
        result |= query_timestamp(handle, channel);
    return result;
}

static int
cmd_timestamp_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_TIMESTAMP_CONTROL control;
    int index;

    if (argc < 6 || argc > 8)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    control.Size = sizeof(control);
    control.Channel =
        (unsigned __int32)parse_timestamp_channel(argv[2]);
    if (parse_on_off(argv[3], TIMECARD_TIMESTAMP_FLAG_ENABLED,
                     &control.Flags))
        return 2;
    if (_stricmp(argv[4], "rising") == 0 ||
        _stricmp(argv[4], "normal") == 0) {
        control.Polarity = TIMECARD_TIMESTAMP_POLARITY_RISING;
    } else if (_stricmp(argv[4], "falling") == 0 ||
               _stricmp(argv[4], "inverted") == 0) {
        control.Polarity = TIMECARD_TIMESTAMP_POLARITY_FALLING;
    } else {
        return 2;
    }
    control.CableDelayNanoseconds = parse_u32_bounded(
        argv[5], "timestamp cable delay", 0u, 0xffffu);
    for (index = 6; index < argc; ++index) {
        if (_stricmp(argv[index], "clear-error") == 0 &&
            (control.Flags & TIMECARD_TIMESTAMP_FLAG_CLEAR_ERROR) == 0u) {
            control.Flags |= TIMECARD_TIMESTAMP_FLAG_CLEAR_ERROR;
        } else if (_stricmp(argv[index], "clear-queue") == 0 &&
                   (control.Flags &
                    TIMECARD_TIMESTAMP_FLAG_CLEAR_QUEUE) == 0u) {
            control.Flags |= TIMECARD_TIMESTAMP_FLAG_CLEAR_QUEUE;
        } else {
            return 2;
        }
    }
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_TIMESTAMP_SET,
                             &control, sizeof(control), &control,
                             sizeof(control)))
        return 1;
    print_timestamp_control(&control);
    return 0;
}

static void
print_timestamp_event(const TIMECARD_TIMESTAMP_EVENT *event,
                      unsigned long index)
{
    unsigned long dataWords;
    unsigned long word;

    printf("  [%lu] ", index);
    if ((event->Flags & TIMECARD_TIMESTAMP_FLAG_EVENT_VALID) != 0u)
        print_card_time(&event->Time);
    else
        printf("invalid time");
    printf("; event/count/error/flags %lu / %lu / 0x%08lx / 0x%08lx\n",
           (unsigned long)event->EventCount,
           (unsigned long)event->TimestampCount,
           (unsigned long)event->Error,
           (unsigned long)event->Flags);
    if ((event->Flags & TIMECARD_TIMESTAMP_FLAG_DATA_VALID) == 0u)
        return;
    if (event->DataWidth > TIMECARD_TIMESTAMP_MAX_DATA_BYTES * 8u)
        dataWords = TIMECARD_TIMESTAMP_MAX_DATA_BYTES / 4u;
    else
        dataWords = (event->DataWidth + 31u) / 32u;
    printf("       payload %lu bits, least-significant word first:",
           (unsigned long)event->DataWidth);
    for (word = 0; word < dataWords; ++word)
        printf(" %08lX", (unsigned long)event->Data[word]);
    if ((event->Flags & TIMECARD_TIMESTAMP_FLAG_DATA_TRUNCATED) != 0u)
        printf(" (truncated to %u bits)",
               TIMECARD_TIMESTAMP_MAX_DATA_BYTES * 8u);
    printf("\n");
}

static int
cmd_timestamp_read(HANDLE handle, int argc, char **argv)
{
    TIMECARD_TIMESTAMP_BATCH batch;
    unsigned long index;

    if (argc < 3 || argc > 4)
        return 2;
    RtlZeroMemory(&batch, sizeof(batch));
    batch.Size = sizeof(batch);
    batch.Channel =
        (unsigned __int32)parse_timestamp_channel(argv[2]);
    batch.MaximumEvents = argc == 4 ?
        parse_u32_bounded(argv[3], "event count", 1u,
                          TIMECARD_TIMESTAMP_MAX_BATCH) :
        TIMECARD_TIMESTAMP_MAX_BATCH;
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_TIMESTAMP_READ,
                             &batch, sizeof(batch), &batch, sizeof(batch)))
        return 1;
    if (batch.Count > TIMECARD_TIMESTAMP_MAX_BATCH) {
        fprintf(stderr,
                "timecardctl: driver returned an invalid timestamp event "
                "count (%lu)\n", (unsigned long)batch.Count);
        return 1;
    }
    printf("Timestamp %lu (%s): drained %lu event(s), %lu dropped\n",
           (unsigned long)batch.Channel,
           timestamp_channel_name(batch.Channel),
           (unsigned long)batch.Count,
           (unsigned long)batch.DroppedEvents);
    for (index = 0; index < batch.Count; ++index)
        print_timestamp_event(&batch.Events[index], index);
    return 0;
}

static void
print_q16_ppb(signed __int64 value)
{
    unsigned __int64 magnitude;
    unsigned __int64 whole;
    unsigned __int64 fraction;

    magnitude = value < 0 ? (unsigned __int64)(-value) :
                            (unsigned __int64)value;
    whole = magnitude >> 16;
    fraction = ((magnitude & 0xffffu) * 1000000u + 32768u) >> 16;
    if (fraction == 1000000u) {
        ++whole;
        fraction = 0u;
    }
    printf("%s%llu.%06llu", value < 0 ? "-" : "",
           (unsigned long long)whole, (unsigned long long)fraction);
}

static void
print_clock_adjustment(const TIMECARD_CLOCK_ADJUSTMENT *adjustment)
{
    printf("Clock adjustment: version 0x%08lx, flags 0x%08lx\n",
           (unsigned long)adjustment->Version,
           (unsigned long)adjustment->Flags);
    printf("  control/select: 0x%08lx / 0x%08lx\n",
           (unsigned long)adjustment->Control,
           (unsigned long)adjustment->Select);
    printf("  smooth offset: %ld ns over %lu ns\n",
           (long)adjustment->OffsetNanoseconds,
           (unsigned long)adjustment->OffsetIntervalNanoseconds);
    printf("  smooth drift:  ");
    print_q16_ppb(adjustment->DriftPpbQ16);
    printf(" ppb over %lu ns%s\n",
           (unsigned long)adjustment->DriftIntervalNanoseconds,
           (adjustment->Flags &
            TIMECARD_CLOCK_ADJUST_FLAG_FRACTIONAL_DRIFT) != 0u ?
               " (Q16.16 supported)" : " (integer core)");
    printf("  in-sync threshold: %lu ns; applied flags: 0x%08lx\n",
           (unsigned long)adjustment->InSyncThresholdNanoseconds,
           (unsigned long)adjustment->AppliedFlags);
}

static int
cmd_clock_adjust_status(HANDLE handle)
{
    TIMECARD_CLOCK_ADJUSTMENT adjustment;

    RtlZeroMemory(&adjustment, sizeof(adjustment));
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_CLOCK_ADJUST_QUERY,
                             NULL, 0, &adjustment, sizeof(adjustment)))
        return 1;
    print_clock_adjustment(&adjustment);
    return 0;
}

static int
cmd_clock_adjust_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_CLOCK_ADJUSTMENT adjustment;
    int index = 2;

    if (argc < 4 || argc > 10)
        return 2;
    RtlZeroMemory(&adjustment, sizeof(adjustment));
    adjustment.Size = sizeof(adjustment);
    while (index < argc) {
        if (_stricmp(argv[index], "offset") == 0 &&
            (adjustment.Flags &
             TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET) == 0u) {
            if (index + 2 >= argc)
                return 2;
            adjustment.OffsetNanoseconds = parse_i32_bounded(
                argv[index + 1], "smooth offset",
                -TIMECARD_CLOCK_ADJUST_MAX_OFFSET_NS,
                TIMECARD_CLOCK_ADJUST_MAX_OFFSET_NS);
            adjustment.OffsetIntervalNanoseconds = parse_u32_bounded(
                argv[index + 2], "offset interval", 1u, 0xffffffffu);
            adjustment.Flags |= TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET;
            index += 3;
        } else if (_stricmp(argv[index], "drift") == 0 &&
                   (adjustment.Flags &
                    TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT) == 0u) {
            if (index + 2 >= argc)
                return 2;
            adjustment.DriftPpbQ16 = parse_ppb_q16(argv[index + 1]);
            adjustment.DriftIntervalNanoseconds = parse_u32_bounded(
                argv[index + 2], "drift interval", 1u, 0xffffffffu);
            adjustment.Flags |= TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT;
            index += 3;
        } else if (_stricmp(argv[index], "threshold") == 0 &&
                   (adjustment.Flags &
                    TIMECARD_CLOCK_ADJUST_FLAG_APPLY_THRESHOLD) == 0u) {
            if (index + 1 >= argc)
                return 2;
            adjustment.InSyncThresholdNanoseconds = parse_u32_bounded(
                argv[index + 1], "in-sync threshold", 0u, 1000000000u);
            adjustment.Flags |= TIMECARD_CLOCK_ADJUST_FLAG_APPLY_THRESHOLD;
            index += 2;
        } else {
            return 2;
        }
    }
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_CLOCK_ADJUST_SET,
                             &adjustment, sizeof(adjustment), &adjustment,
                             sizeof(adjustment)))
        return 1;
    print_clock_adjustment(&adjustment);
    return 0;
}

static void
print_limiter(const char *name, unsigned __int32 raw, int fractional)
{
    unsigned __int32 magnitude =
        raw & TIMECARD_CLOCK_LIMITER_VALUE_MASK;

    printf("  %-22s 0x%08lx (%s, ", name, (unsigned long)raw,
           (raw & TIMECARD_CLOCK_LIMITER_ENABLE) != 0u ?
               "enabled" : "disabled");
    if (fractional) {
        print_q16_ppb((signed __int64)magnitude);
        printf(" ppb magnitude)\n");
    } else {
        printf("%lu ns magnitude)\n", (unsigned long)magnitude);
    }
}

static void
print_clock_advanced(const TIMECARD_CLOCK_ADVANCED_CONTROL *control)
{
    printf("Advanced Clock: version 0x%08lx, flags 0x%08lx\n",
           (unsigned long)control->Version,
           (unsigned long)control->Flags);
    printf("  control/status/apply: 0x%08lx / 0x%08lx / 0x%08lx\n",
           (unsigned long)control->Control,
           (unsigned long)control->Status,
           (unsigned long)control->ApplyFlags);
    printf("  holdover/aging ready: %s / %s\n",
           (control->Flags &
            TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER_READY) != 0u ?
               "yes" : "no",
           (control->Flags &
            TIMECARD_CLOCK_ADVANCED_FLAG_AGING_READY) != 0u ?
               "yes" : "no");
    if ((control->Flags &
         TIMECARD_CLOCK_ADVANCED_FLAG_RATE_LIMITERS) != 0u) {
        print_limiter("offset rate limiter", control->OffsetRateLimiter, 0);
        print_limiter("drift rate limiter", control->DriftRateLimiterQ16, 1);
    }
    if ((control->Flags & TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER) != 0u) {
        printf("  holdover config:      0x%08lx\n",
               (unsigned long)control->HoldoverConfiguration);
        printf("  holdover status:      0x%08lx + 0x%08lx; %lu samples\n",
               (unsigned long)control->StatusHoldover,
               (unsigned long)control->StatusHoldoverFraction,
               (unsigned long)control->StatusHoldoverSamples);
    }
    if ((control->Flags &
         TIMECARD_CLOCK_ADVANCED_FLAG_OUTLIER_FILTERS) != 0u) {
        print_limiter("offset outlier filter",
                      control->OffsetOutlierFilter, 0);
        print_limiter("drift outlier filter",
                      control->DriftOutlierFilter, 0);
        printf("  rejected offset/drift samples: %lu / %lu\n",
               (unsigned long)control->StatusOffsetOutliers,
               (unsigned long)control->StatusDriftOutliers);
    }
    if ((control->Flags &
         TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_FACTORS) != 0u) {
        printf("  servo offset P/I:     0x%04lx / 0x%04lx\n",
               (unsigned long)control->ServoOffsetP,
               (unsigned long)control->ServoOffsetI);
        printf("  servo drift P/I:      0x%04lx / 0x%04lx\n",
               (unsigned long)control->ServoDriftP,
               (unsigned long)control->ServoDriftI);
    }
    if ((control->Flags & TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_LOG) != 0u) {
        printf("  servo offset log:     0x%08lx + 0x%08lx\n",
               (unsigned long)control->StatusOffset,
               (unsigned long)control->StatusOffsetFraction);
        printf("  servo drift log:      0x%08lx + 0x%08lx\n",
               (unsigned long)control->StatusDrift,
               (unsigned long)control->StatusDriftFraction);
    }
    if ((control->Flags & TIMECARD_CLOCK_ADVANCED_FLAG_AGING) != 0u) {
        printf("  aging config:         0x%08lx\n",
               (unsigned long)control->AgingConfiguration);
        printf("  aging status:         0x%08lx%08lx; %lu samples\n",
               (unsigned long)control->StatusAgingHigh,
               (unsigned long)control->StatusAgingLow,
               (unsigned long)control->StatusAgingSamples);
    }
    if ((control->Flags &
         TIMECARD_CLOCK_ADVANCED_FLAG_DYNAMIC_CONTROL) != 0u) {
        printf("  dynamic control:      0x%08lx\n",
               (unsigned long)control->DynamicControl);
    }
    printf("  control modes:        holdover=%s, aging=%s, revert=%s\n",
           (control->Control &
            TIMECARD_CLOCK_ADVANCED_CONTROL_HOLDOVER) != 0u ? "on" : "off",
           (control->Control &
            TIMECARD_CLOCK_ADVANCED_CONTROL_AGING) != 0u ? "on" : "off",
           (control->Control &
            TIMECARD_CLOCK_ADVANCED_CONTROL_REVERT) != 0u ? "on" : "off");
}

static int
cmd_clock_advanced_status(HANDLE handle)
{
    TIMECARD_CLOCK_ADVANCED_CONTROL control;

    RtlZeroMemory(&control, sizeof(control));
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_CLOCK_ADVANCED_QUERY,
                             NULL, 0, &control, sizeof(control)))
        return 1;
    print_clock_advanced(&control);
    return 0;
}

static unsigned __int32
parse_clock_advanced_modes(const char *text)
{
    char buffer[128];
    char *context;
    char *token;
    unsigned __int32 value = 0u;

    if (_stricmp(text, "none") == 0)
        return 0u;
    if (strlen(text) >= sizeof(buffer)) {
        fprintf(stderr, "timecardctl: advanced Clock mode list is too long\n");
        exit(2);
    }
    strcpy_s(buffer, sizeof(buffer), text);
    token = strtok_s(buffer, ",", &context);
    while (token != NULL) {
        unsigned __int32 flag;

        if (_stricmp(token, "holdover") == 0)
            flag = TIMECARD_CLOCK_ADVANCED_CONTROL_HOLDOVER;
        else if (_stricmp(token, "aging") == 0)
            flag = TIMECARD_CLOCK_ADVANCED_CONTROL_AGING;
        else if (_stricmp(token, "revert") == 0)
            flag = TIMECARD_CLOCK_ADVANCED_CONTROL_REVERT;
        else {
            fprintf(stderr,
                    "timecardctl: unknown advanced Clock mode: %s\n",
                    token);
            exit(2);
        }
        if ((value & flag) != 0u) {
            fprintf(stderr,
                    "timecardctl: duplicate advanced Clock mode: %s\n",
                    token);
            exit(2);
        }
        value |= flag;
        token = strtok_s(NULL, ",", &context);
    }
    if (value == 0u) {
        fprintf(stderr, "timecardctl: empty advanced Clock mode list\n");
        exit(2);
    }
    return value;
}

static int
cmd_clock_advanced_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_CLOCK_ADVANCED_CONTROL control;
    int index = 2;

    if (argc < 4 || argc > 19)
        return 2;
    RtlZeroMemory(&control, sizeof(control));
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_CLOCK_ADVANCED_QUERY,
                             NULL, 0, &control, sizeof(control)))
        return 1;
    control.Size = sizeof(control);
    control.ApplyFlags = 0u;
    while (index < argc) {
        if (_stricmp(argv[index], "rate-limiters") == 0 &&
            (control.ApplyFlags &
             TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS) == 0u) {
            if (index + 2 >= argc)
                return 2;
            control.OffsetRateLimiter = parse_u32_bounded(
                argv[index + 1], "offset rate-limiter register", 0u,
                0xffffffffu);
            control.DriftRateLimiterQ16 = parse_u32_bounded(
                argv[index + 2], "drift rate-limiter register", 0u,
                0xffffffffu);
            control.ApplyFlags |=
                TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS;
            index += 3;
        } else if (_stricmp(argv[index], "holdover") == 0 &&
                   (control.ApplyFlags &
                    TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER) == 0u) {
            if (index + 1 >= argc)
                return 2;
            control.HoldoverConfiguration = parse_u32_bounded(
                argv[index + 1], "holdover configuration", 0u,
                0xffffffffu);
            control.ApplyFlags |= TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER;
            index += 2;
        } else if (_stricmp(argv[index], "outlier-filters") == 0 &&
                   (control.ApplyFlags &
                    TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS) == 0u) {
            if (index + 2 >= argc)
                return 2;
            control.OffsetOutlierFilter = parse_u32_bounded(
                argv[index + 1], "offset outlier-filter register", 0u,
                0xffffffffu);
            control.DriftOutlierFilter = parse_u32_bounded(
                argv[index + 2], "drift outlier-filter register", 0u,
                0xffffffffu);
            control.ApplyFlags |=
                TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS;
            index += 3;
        } else if (_stricmp(argv[index], "servo") == 0 &&
                   (control.ApplyFlags &
                    TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) == 0u) {
            if (index + 4 >= argc)
                return 2;
            control.ServoOffsetP = parse_u32_bounded(
                argv[index + 1], "servo offset P", 0u, 0xffffu);
            control.ServoOffsetI = parse_u32_bounded(
                argv[index + 2], "servo offset I", 0u, 0xffffu);
            control.ServoDriftP = parse_u32_bounded(
                argv[index + 3], "servo drift P", 0u, 0xffffu);
            control.ServoDriftI = parse_u32_bounded(
                argv[index + 4], "servo drift I", 0u, 0xffffu);
            control.ApplyFlags |=
                TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS;
            index += 5;
        } else if (_stricmp(argv[index], "aging") == 0 &&
                   (control.ApplyFlags &
                    TIMECARD_CLOCK_ADVANCED_APPLY_AGING) == 0u) {
            if (index + 1 >= argc)
                return 2;
            control.AgingConfiguration = parse_u32_bounded(
                argv[index + 1], "aging configuration", 0u, 0xffffffffu);
            control.ApplyFlags |= TIMECARD_CLOCK_ADVANCED_APPLY_AGING;
            index += 2;
        } else if (_stricmp(argv[index], "control") == 0 &&
                   (control.ApplyFlags &
                    TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL) == 0u) {
            unsigned __int32 modes;

            if (index + 1 >= argc)
                return 2;
            modes = parse_clock_advanced_modes(argv[index + 1]);
            control.Control =
                (control.Control & ~TIMECARD_CLOCK_ADVANCED_CONTROL_MASK) |
                modes;
            control.ApplyFlags |= TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL;
            index += 2;
        } else {
            return 2;
        }
    }
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_CLOCK_ADVANCED_SET,
                             &control, sizeof(control), &control,
                             sizeof(control)))
        return 1;
    print_clock_advanced(&control);
    return 0;
}

static const char *
core_type_name(unsigned long type)
{
    switch (type) {
    case TIMECARD_CORE_TYPE_CLOCK: return "Clock";
    case TIMECARD_CORE_TYPE_IMAGE_IDENTITY: return "Image identity";
    case TIMECARD_CORE_TYPE_SIGNAL_TIMESTAMPER: return "Signal timestamper";
    case TIMECARD_CORE_TYPE_PPS_MASTER: return "PPS master";
    case TIMECARD_CORE_TYPE_PPS_SLAVE: return "PPS slave";
    case TIMECARD_CORE_TYPE_TOD_SLAVE: return "ToD slave";
    case TIMECARD_CORE_TYPE_TOD_MASTER: return "ToD master";
    case TIMECARD_CORE_TYPE_IRIG_MASTER: return "IRIG master";
    case TIMECARD_CORE_TYPE_IRIG_SLAVE: return "IRIG slave";
    case TIMECARD_CORE_TYPE_DCF_MASTER: return "DCF master";
    case TIMECARD_CORE_TYPE_DCF_SLAVE: return "DCF slave";
    case TIMECARD_CORE_TYPE_SIGNAL_GENERATOR: return "Signal generator";
    case TIMECARD_CORE_TYPE_FREQUENCY_INPUT: return "Frequency input";
    default: return "Unknown";
    }
}

static int
cmd_fpga_inventory(HANDLE handle)
{
    TIMECARD_CORE_INVENTORY inventory;
    unsigned long index;

    RtlZeroMemory(&inventory, sizeof(inventory));
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_CORE_INVENTORY_QUERY,
                             NULL, 0, &inventory, sizeof(inventory)))
        return 1;
    printf("Static FPGA inventory: ABI %lu, %lu core descriptor(s)\n",
           (unsigned long)inventory.AbiVersion,
           (unsigned long)inventory.Count);
    printf("  board/layout/image/flags: %lu / %lu / 0x%08lx / 0x%08lx\n",
           (unsigned long)inventory.BoardProfile,
           (unsigned long)inventory.Layout,
           (unsigned long)inventory.RawImageVersion,
           (unsigned long)inventory.Flags);
    printf("  This is the driver's trusted static board map; no guessed BAR "
           "addresses were probed.\n");
    for (index = 0; index < inventory.Count &&
                    index < TIMECARD_CORE_INVENTORY_MAX; ++index) {
        const TIMECARD_CORE_DESCRIPTOR *core = &inventory.Cores[index];

        printf("  [%2lu] %-19s #%lu offset 0x%08lx span 0x%05lx ",
               index, core_type_name(core->Type),
               (unsigned long)core->Instance,
               (unsigned long)core->RegisterOffset,
               (unsigned long)core->RegisterSpan);
        if (core->InterruptMessage == TIMECARD_CORE_INTERRUPT_NONE)
            printf("IRQ - ");
        else
            printf("IRQ %lu ", (unsigned long)core->InterruptMessage);
        printf("version 0x%08lx flags 0x%02lx\n",
               (unsigned long)core->Version,
               (unsigned long)core->Flags);
    }
    if (inventory.Count > TIMECARD_CORE_INVENTORY_MAX) {
        fprintf(stderr, "timecardctl: driver returned an invalid inventory "
                        "count (%lu)\n", (unsigned long)inventory.Count);
        return 1;
    }
    return 0;
}

static int
cmd_signal_events(HANDLE handle, int argc, char **argv)
{
    TIMECARD_SIGNAL_EVENT_BATCH batch;
    unsigned long index;

    if (argc < 3 || argc > 4)
        return 2;
    RtlZeroMemory(&batch, sizeof(batch));
    batch.Size = sizeof(batch);
    batch.Generator = parse_u32_bounded(
        argv[2], "signal generator", 1u, TIMECARD_SIGNAL_COUNT);
    batch.MaximumEvents = argc == 4 ?
        parse_u32_bounded(argv[3], "event count", 1u,
                          TIMECARD_SIGNAL_EVENT_MAX_BATCH) :
        TIMECARD_SIGNAL_EVENT_MAX_BATCH;
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_SIGNAL_EVENT_READ,
                             &batch, sizeof(batch), &batch, sizeof(batch)))
        return 1;
    if (batch.Count > TIMECARD_SIGNAL_EVENT_MAX_BATCH) {
        fprintf(stderr,
                "timecardctl: driver returned an invalid signal event "
                "count (%lu)\n", (unsigned long)batch.Count);
        return 1;
    }
    printf("Signal generator %lu: drained %lu completion event(s), "
           "%lu dropped\n",
           (unsigned long)batch.Generator,
           (unsigned long)batch.Count,
           (unsigned long)batch.DroppedEvents);
    for (index = 0; index < batch.Count; ++index) {
        const TIMECARD_SIGNAL_EVENT *event = &batch.Events[index];
        unsigned __int64 seconds =
            event->SystemInterruptTime100ns / 10000000u;
        unsigned __int64 fraction =
            event->SystemInterruptTime100ns % 10000000u;

        printf("  [%lu] sequence %llu, interrupt time %llu.%07llu s, "
               "flags/status 0x%08lx / 0x%08lx\n",
               index, (unsigned long long)event->Sequence,
               (unsigned long long)seconds,
               (unsigned long long)fraction,
               (unsigned long)event->Flags,
               (unsigned long)event->Status);
    }
    return 0;
}

static const struct {
    const char *Name;
    unsigned __int32 Flag;
} contract_capabilities[] = {
    { "clock-servo-log", TIMECARD_FPGA_CONTRACT_CLOCK_SERVO_LOG },
    { "clock-advanced", TIMECARD_FPGA_CONTRACT_CLOCK_ADVANCED },
    { "tod-telemetry", TIMECARD_FPGA_CONTRACT_TOD_TELEMETRY },
    { "tod-utc-read", TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ },
    { "tod-utc-write", TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_WRITE },
    { "irig-master-am", TIMECARD_FPGA_CONTRACT_IRIG_MASTER_AM },
    { "irig-slave-am", TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_AM },
    { "irig-slave-year", TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_YEAR },
    { "ntp-source", TIMECARD_FPGA_CONTRACT_NTP_SOURCE },
    { "art-timestamp-extended",
      TIMECARD_FPGA_CONTRACT_ART_TIMESTAMP_EXTENDED },
    { "synce-source", TIMECARD_FPGA_CONTRACT_SYNCE_SOURCE },
    { "dynamic-source", TIMECARD_FPGA_CONTRACT_DYNAMIC_SOURCE }
};

static void
print_contract_capabilities(unsigned __int32 flags, const char *prefix)
{
    size_t index;
    int found = 0;

    printf("%s", prefix);
    for (index = 0; index < sizeof(contract_capabilities) /
                              sizeof(contract_capabilities[0]); ++index) {
        if ((flags & contract_capabilities[index].Flag) == 0u)
            continue;
        printf("%s%s", found ? "," : "",
               contract_capabilities[index].Name);
        found = 1;
    }
    if (!found)
        printf("none");
    printf("\n");
}

static unsigned __int32
parse_contract_capabilities(const char *text)
{
    char buffer[512];
    char *context;
    char *token;
    unsigned __int32 flags = 0u;

    if (_stricmp(text, "none") == 0)
        return 0u;
    if (strlen(text) >= sizeof(buffer)) {
        fprintf(stderr, "timecardctl: FPGA capability list is too long\n");
        exit(2);
    }
    strcpy_s(buffer, sizeof(buffer), text);
    token = strtok_s(buffer, ",", &context);
    while (token != NULL) {
        size_t index;
        int found = 0;

        for (index = 0; index < sizeof(contract_capabilities) /
                                  sizeof(contract_capabilities[0]); ++index) {
            if (_stricmp(token, contract_capabilities[index].Name) != 0)
                continue;
            if ((flags & contract_capabilities[index].Flag) != 0u) {
                fprintf(stderr,
                        "timecardctl: duplicate FPGA capability: %s\n",
                        token);
                exit(2);
            }
            flags |= contract_capabilities[index].Flag;
            found = 1;
            break;
        }
        if (!found) {
            fprintf(stderr,
                    "timecardctl: unknown FPGA capability: %s\n", token);
            exit(2);
        }
        token = strtok_s(NULL, ",", &context);
    }
    if (flags == 0u) {
        fprintf(stderr, "timecardctl: empty FPGA capability list\n");
        exit(2);
    }
    return flags;
}

static void
print_fpga_contract(const TIMECARD_FPGA_IMAGE_CONTRACT *contract)
{
    printf("FPGA exact-image contract: ABI %lu, raw image 0x%08lx\n",
           (unsigned long)contract->AbiVersion,
           (unsigned long)contract->RawImageVersion);
    printf("  board/layout/status: %lu / %lu / 0x%08lx%s%s%s\n",
           (unsigned long)contract->BoardProfile,
           (unsigned long)contract->Layout,
           (unsigned long)contract->StatusFlags,
           (contract->StatusFlags &
            TIMECARD_FPGA_CONTRACT_FLAG_IMAGE_PRESENT) != 0u ?
               " image-present" : "",
           (contract->StatusFlags &
            TIMECARD_FPGA_CONTRACT_FLAG_EXACT_MATCH) != 0u ?
               " exact-match" : "",
           (contract->StatusFlags &
            TIMECARD_FPGA_CONTRACT_FLAG_ACTIVE) != 0u ?
               " active" : "");
    printf("  declared/effective: 0x%08lx / 0x%08lx\n",
           (unsigned long)contract->CapabilityFlags,
           (unsigned long)contract->EffectiveFlags);
    print_contract_capabilities(contract->CapabilityFlags,
                                "  declared names: ");
    print_contract_capabilities(contract->EffectiveFlags,
                                "  effective names: ");
}

static int
cmd_fpga_contract_status(HANDLE handle)
{
    TIMECARD_FPGA_IMAGE_CONTRACT contract;

    RtlZeroMemory(&contract, sizeof(contract));
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_FPGA_CONTRACT_QUERY,
                             NULL, 0, &contract, sizeof(contract)))
        return 1;
    print_fpga_contract(&contract);
    return 0;
}

static int
cmd_fpga_contract_set(HANDLE handle, int argc, char **argv)
{
    TIMECARD_FPGA_IMAGE_CONTRACT current;
    TIMECARD_FPGA_IMAGE_CONTRACT request;
    unsigned __int32 rawImage;
    unsigned __int32 capabilities;

    if (argc != 5 || _stricmp(argv[4], "acknowledge") != 0)
        return 2;
    rawImage = parse_u32_bounded(argv[2], "raw FPGA image", 1u,
                                 0xffffffffu);
    capabilities = parse_contract_capabilities(argv[3]);
    if ((capabilities &
         TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_WRITE) != 0u &&
        (capabilities &
         TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ) == 0u) {
        fprintf(stderr, "timecardctl: tod-utc-write requires tod-utc-read\n");
        return 2;
    }
    RtlZeroMemory(&current, sizeof(current));
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_FPGA_CONTRACT_QUERY,
                             NULL, 0, &current, sizeof(current)))
        return 1;
    if ((current.StatusFlags &
         TIMECARD_FPGA_CONTRACT_FLAG_IMAGE_PRESENT) == 0u) {
        fprintf(stderr, "timecardctl: no trusted image identity is present; "
                        "a contract cannot be activated\n");
        return 1;
    }
    if (current.RawImageVersion != rawImage) {
        fprintf(stderr,
                "timecardctl: exact-image mismatch: hardware is 0x%08lx, "
                "request named 0x%08lx\n",
                (unsigned long)current.RawImageVersion,
                (unsigned long)rawImage);
        return 1;
    }
    RtlZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.AbiVersion = TIMECARD_ABI_VERSION;
    request.RawImageVersion = rawImage;
    request.CapabilityFlags = capabilities;
    request.BoardProfile = current.BoardProfile;
    request.Layout = current.Layout;
    request.Acknowledgement = TIMECARD_FPGA_CONTRACT_ACKNOWLEDGEMENT;
    if (timecard_ioctl_exact(handle, IOCTL_TIMECARD_FPGA_CONTRACT_SET,
                             &request, sizeof(request), &request,
                             sizeof(request)))
        return 1;
    print_fpga_contract(&request);
    return 0;
}

static void
usage(void)
{
    fprintf(stderr,
            "usage:\n"
            "  timecardctl status\n"
            "  timecardctl capabilities\n"
            "  timecardctl fpga-status\n"
            "  timecardctl clock-telemetry\n"
            "  timecardctl pps-status [master|slave]\n"
            "  timecardctl pps-set <master|slave> <on|off> <active-high|active-low> <cable-ns> <pulse-ms> [clear]\n"
            "  timecardctl timecode-status [irig|dcf] [master|slave]\n"
            "  timecardctl timecode-set <irig|dcf> <master|slave> <on|off> <correction-s> <delay-ns> <mode> <code> [control-bits] [--am <on|off>] [--year <1970..2069>] [clear]\n"
            "  timecardctl tod-status\n"
            "  timecardctl tod-set <on|off> <nmea|ubx|tsip|esip|pfec> <gnss 0..5> <baud> <normal|inverted> <correction-s> <disable-mask> [clear]\n"
            "  timecardctl signal-status [generator 1..4]\n"
            "  timecardctl signal-set <generator> <on|off> <period-ns> <pulse-ns> <phase-ns> <active-high|active-low> <repeat> <cable-ns>\n"
            "  timecardctl signal-set-at <generator> <on|off> <period-ns> <pulse-ns> <start-phc-seconds> <start-ns> <active-high|active-low> <repeat> <cable-ns>\n"
            "  timecardctl signal-clear <generator 1..4>\n"
            "  timecardctl signal-events <generator 1..4> [events 1..16]\n"
            "  timecardctl timestamp-status [gnss1|ts1|ts2|ts3|ts4|phc|0..5]\n"
            "  timecardctl timestamp-set <channel> <on|off> <rising|falling> <cable-ns 0..65535> [clear-error] [clear-queue]\n"
            "  timecardctl timestamp-read <channel> [events 1..16]\n"
            "  timecardctl clock-adjust-status\n"
            "  timecardctl clock-adjust-set <clause> [<clause> ...]\n"
            "    clauses: offset <signed-ns> <window-ns>; drift <signed-ppb> <window-ns>; threshold <ns>\n"
            "  timecardctl clock-advanced-status\n"
            "  timecardctl clock-advanced-set <clause> [<clause> ...]\n"
            "    clauses: rate-limiters <offset-raw> <drift-q16-raw>; holdover <raw>; outlier-filters <offset-raw> <drift-raw>\n"
            "             servo <offset-p> <offset-i> <drift-p> <drift-i>; aging <raw>; control <none|holdover[,aging][,revert]>\n"
            "  timecardctl fpga-inventory\n"
            "  timecardctl fpga-contract-status\n"
            "  timecardctl fpga-contract-set <raw-image-version> <none|capability[,capability...]> acknowledge\n"
            "  timecardctl get\n"
            "  timecardctl set-system\n"
            "  timecardctl phase-status\n"
            "  timecardctl phase-enable\n"
            "  timecardctl phase-disable\n"
            "  timecardctl phc-adjust <signed-nanoseconds>\n"
            "  timecardctl discipline-read <512-byte-image>\n"
            "  timecardctl discipline-write <512-byte-image>\n"
            "  timecardctl clock-source <none|tod|irig|pps|ptp|rtc|dcf|ntp|synce|dyn|regs|ext>\n"
            "  timecardctl serial\n"
            "  timecardctl nmea-status\n"
            "  timecardctl nmea-set <on|off> <baud> [normal|inverted] [clear]\n"
            "  timecardctl nmea-set <on|off> <baud> <normal|inverted> <correction-s> <local-offset-minutes> <gnss 0..5|15> <disable-mask> [clear]\n"
            "  timecardctl nmea-utc-status\n"
            "  timecardctl nmea-utc-set <offset-seconds 0..65535> <valid|invalid> <none|leap61|leap59>\n"
            "  timecardctl uart-config <port 0..3> <baud>\n"
            "  timecardctl uart-read <port> [bytes] [timeout-ms]\n"
            "  timecardctl uart-read-hex <port> [bytes] [timeout-ms]\n"
            "  timecardctl uart-observe <port> [timeout-ms]\n"
            "  timecardctl uart-write <port> <text>\n"
            "  timecardctl mro-status\n"
            "  timecardctl mro-fine <value>\n"
            "  timecardctl mro-coarse <value>\n"
            "  timecardctl mro-save-coarse\n"
            "  timecardctl mro-serial <on|off>\n"
            "  timecardctl flash-status\n"
            "  timecardctl hierarchy-status\n"
            "  timecardctl hierarchy-enable\n"
            "  timecardctl hierarchy-persist\n"
            "  timecardctl hierarchy-disable\n"
            "  timecardctl sma-status [connector 1..4]\n"
            "  timecardctl sma-set <connector> <input|output> <function>\n"
            "  timecardctl sma-set <connector> disabled\n"
            "  timecardctl i2c-status\n"
            "  timecardctl i2c-mux [channel-mask 0..15]\n"
            "  timecardctl i2c-scan\n"
            "  timecardctl i2c-read <address> <subaddress> <bytes> [subaddress-bytes 0..2]\n"
            "  timecardctl sensors\n"
            "  timecardctl led-status [LED 1..6]\n"
            "  timecardctl led-set <LED 1..6> <red> <green> <blue> [current 1..128]\n"
            "  timecardctl led-test\n"
            "ports: 0=GNSS, 1=GNSS2, 2=MAC, 3=NMEA\n"
            "contract capabilities: clock-servo-log,clock-advanced,tod-telemetry,\n"
            "  tod-utc-read,tod-utc-write,irig-master-am,irig-slave-am,\n"
            "  irig-slave-year,ntp-source,art-timestamp-extended,synce-source,\n"
            "  dynamic-source\n");
}

int
main(int argc, char **argv)
{
    HANDLE handle;
    int result = 2;

    if (argc < 2) {
        usage();
        return 2;
    }
    handle = timecard_open();
    if (strcmp(argv[1], "status") == 0 && argc == 2)
        result = cmd_status(handle);
    else if (strcmp(argv[1], "capabilities") == 0 && argc == 2)
        result = cmd_capabilities(handle);
    else if (strcmp(argv[1], "fpga-status") == 0 && argc == 2)
        result = cmd_fpga_status(handle);
    else if (strcmp(argv[1], "clock-telemetry") == 0 && argc == 2)
        result = cmd_clock_telemetry(handle);
    else if (strcmp(argv[1], "pps-status") == 0)
        result = cmd_pps_status(handle, argc, argv);
    else if (strcmp(argv[1], "pps-set") == 0)
        result = cmd_pps_set(handle, argc, argv);
    else if (strcmp(argv[1], "timecode-status") == 0)
        result = cmd_timecode_status(handle, argc, argv);
    else if (strcmp(argv[1], "timecode-set") == 0)
        result = cmd_timecode_set(handle, argc, argv);
    else if (strcmp(argv[1], "tod-status") == 0 && argc == 2)
        result = cmd_tod_status(handle);
    else if (strcmp(argv[1], "tod-set") == 0)
        result = cmd_tod_set(handle, argc, argv);
    else if (strcmp(argv[1], "signal-status") == 0)
        result = cmd_signal_status(handle, argc, argv);
    else if (strcmp(argv[1], "signal-set") == 0)
        result = cmd_signal_set(handle, argc, argv);
    else if (strcmp(argv[1], "signal-set-at") == 0)
        result = cmd_signal_set_at(handle, argc, argv);
    else if (strcmp(argv[1], "signal-clear") == 0)
        result = cmd_signal_clear(handle, argc, argv);
    else if (strcmp(argv[1], "signal-events") == 0)
        result = cmd_signal_events(handle, argc, argv);
    else if (strcmp(argv[1], "timestamp-status") == 0)
        result = cmd_timestamp_status(handle, argc, argv);
    else if (strcmp(argv[1], "timestamp-set") == 0)
        result = cmd_timestamp_set(handle, argc, argv);
    else if (strcmp(argv[1], "timestamp-read") == 0)
        result = cmd_timestamp_read(handle, argc, argv);
    else if (strcmp(argv[1], "clock-adjust-status") == 0 && argc == 2)
        result = cmd_clock_adjust_status(handle);
    else if (strcmp(argv[1], "clock-adjust-set") == 0)
        result = cmd_clock_adjust_set(handle, argc, argv);
    else if (strcmp(argv[1], "clock-advanced-status") == 0 && argc == 2)
        result = cmd_clock_advanced_status(handle);
    else if (strcmp(argv[1], "clock-advanced-set") == 0)
        result = cmd_clock_advanced_set(handle, argc, argv);
    else if (strcmp(argv[1], "fpga-inventory") == 0 && argc == 2)
        result = cmd_fpga_inventory(handle);
    else if (strcmp(argv[1], "fpga-contract-status") == 0 && argc == 2)
        result = cmd_fpga_contract_status(handle);
    else if (strcmp(argv[1], "fpga-contract-set") == 0)
        result = cmd_fpga_contract_set(handle, argc, argv);
    else if (strcmp(argv[1], "get") == 0 && argc == 2)
        result = cmd_get(handle);
    else if (strcmp(argv[1], "set-system") == 0 && argc == 2)
        result = cmd_set_system(handle);
    else if (strcmp(argv[1], "phase-status") == 0 && argc == 2)
        result = cmd_phase_status(handle);
    else if (strcmp(argv[1], "phase-enable") == 0 && argc == 2)
        result = cmd_phase_control(handle, TIMECARD_PHASE_CONTROL_ENABLE);
    else if (strcmp(argv[1], "phase-disable") == 0 && argc == 2)
        result = cmd_phase_control(handle, TIMECARD_PHASE_CONTROL_DISABLE);
    else if (strcmp(argv[1], "phc-adjust") == 0)
        result = cmd_phc_adjust(handle, argc, argv);
    else if (strcmp(argv[1], "discipline-read") == 0)
        result = cmd_discipline_read(handle, argc, argv);
    else if (strcmp(argv[1], "discipline-write") == 0)
        result = cmd_discipline_write(handle, argc, argv);
    else if (strcmp(argv[1], "clock-source") == 0)
        result = cmd_clock_source(handle, argc, argv);
    else if (strcmp(argv[1], "serial") == 0 && argc == 2)
        result = cmd_serial(handle);
    else if (strcmp(argv[1], "nmea-status") == 0 && argc == 2)
        result = cmd_nmea_status(handle);
    else if (strcmp(argv[1], "nmea-set") == 0)
        result = cmd_nmea_set(handle, argc, argv);
    else if (strcmp(argv[1], "nmea-utc-status") == 0 && argc == 2)
        result = cmd_nmea_utc_status(handle);
    else if (strcmp(argv[1], "nmea-utc-set") == 0)
        result = cmd_nmea_utc_set(handle, argc, argv);
    else if (strcmp(argv[1], "uart-config") == 0)
        result = cmd_uart_config(handle, argc, argv);
    else if (strcmp(argv[1], "uart-read") == 0)
        result = cmd_uart_read(handle, argc, argv);
    else if (strcmp(argv[1], "uart-read-hex") == 0)
        result = cmd_uart_read_hex(handle, argc, argv);
    else if (strcmp(argv[1], "uart-observe") == 0)
        result = cmd_uart_observe(handle, argc, argv);
    else if (strcmp(argv[1], "uart-write") == 0)
        result = cmd_uart_write(handle, argc, argv);
    else if (strcmp(argv[1], "mro-status") == 0 && argc == 2)
        result = cmd_mro50_status(handle);
    else if (strncmp(argv[1], "mro-", 4) == 0)
        result = cmd_mro50_control(handle, argc, argv);
    else if (strcmp(argv[1], "flash-status") == 0 && argc == 2)
        result = cmd_flash_status(handle);
    else if (strcmp(argv[1], "hierarchy-status") == 0 && argc == 2)
        result = cmd_hierarchy(handle, TIMECARD_HIERARCHY_QUERY, 0);
    else if (strcmp(argv[1], "hierarchy-enable") == 0 && argc == 2)
        result = cmd_hierarchy(handle, TIMECARD_HIERARCHY_ENABLE, 0);
    else if (strcmp(argv[1], "hierarchy-persist") == 0 && argc == 2)
        result = cmd_hierarchy(handle, TIMECARD_HIERARCHY_ENABLE, 1);
    else if (strcmp(argv[1], "hierarchy-disable") == 0 && argc == 2)
        result = cmd_hierarchy(handle, TIMECARD_HIERARCHY_DISABLE, 1);
    else if (strcmp(argv[1], "sma-status") == 0)
        result = cmd_sma_status(handle, argc, argv);
    else if (strcmp(argv[1], "sma-set") == 0)
        result = cmd_sma_set(handle, argc, argv);
    else if (strcmp(argv[1], "i2c-status") == 0 && argc == 2)
        result = cmd_i2c_status(handle);
    else if (strcmp(argv[1], "i2c-mux") == 0)
        result = cmd_i2c_mux(handle, argc, argv);
    else if (strcmp(argv[1], "i2c-scan") == 0 && argc == 2)
        result = cmd_i2c_scan(handle);
    else if (strcmp(argv[1], "i2c-read") == 0)
        result = cmd_i2c_read(handle, argc, argv);
    else if (strcmp(argv[1], "sensors") == 0 && argc == 2)
        result = cmd_sensors(handle);
    else if (strcmp(argv[1], "led-status") == 0)
        result = cmd_led_status(handle, argc, argv);
    else if (strcmp(argv[1], "led-set") == 0)
        result = cmd_led_set(handle, argc, argv);
    else if (strcmp(argv[1], "led-test") == 0 && argc == 2)
        result = cmd_led_test(handle);
    CloseHandle(handle);

    if (result == 2)
        usage();
    return result;
}
