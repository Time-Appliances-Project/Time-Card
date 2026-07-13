/* SPDX-License-Identifier: BSD-3-Clause */
/* Diagnostic/control utility for the OCP TimeCard Windows driver. */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timecard_ioctl.h"

#define FILETIME_UNIX_EPOCH_100NS 116444736000000000ull

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
           info.Layout == TIMECARD_LAYOUT_MSIX ? "MSI-X" : "MSI");
    printf("Clock offset:     0x%08lx\n", (unsigned long)info.ClockOffset);
    printf("Clock version:    %lu.%lu.%lu (0x%08lx)\n",
           (unsigned long)(info.ClockVersion >> 24),
           (unsigned long)((info.ClockVersion >> 16) & 0xff),
           (unsigned long)(info.ClockVersion & 0xffff),
           (unsigned long)info.ClockVersion);
    printf("Clock status:     0x%08lx (%s)\n",
           (unsigned long)info.ClockStatus,
           (info.ClockStatus & 1u) ? "in sync" : "not in sync");
    printf("Clock source:     0x%04lx\n",
           (unsigned long)(info.ClockSelect >> 16));
    printf("TOD version:      0x%08lx\n", (unsigned long)info.TodVersion);
    printf("TOD/GNSS status:  0x%08lx / 0x%08lx\n",
           (unsigned long)info.TodStatus, (unsigned long)info.GnssStatus);
    printf("UTC/leap:         0x%08lx / 0x%08lx\n",
           (unsigned long)info.UtcStatus, (unsigned long)info.Leap);
    printf("Satellites:       0x%08lx\n", (unsigned long)info.Satellites);
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
    fprintf(stderr, "[%lu byte(s), LSR 0x%02lx]\n",
            (unsigned long)transfer.Length,
            (unsigned long)(transfer.LineStatus & 0xff));
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
usage(void)
{
    fprintf(stderr,
            "usage:\n"
            "  timecardctl status\n"
            "  timecardctl get\n"
            "  timecardctl set-system\n"
            "  timecardctl uart-config <port 0..3> <baud>\n"
            "  timecardctl uart-read <port> [bytes] [timeout-ms]\n"
            "  timecardctl uart-write <port> <text>\n"
            "  timecardctl hierarchy-status\n"
            "  timecardctl hierarchy-enable\n"
            "  timecardctl hierarchy-persist\n"
            "  timecardctl hierarchy-disable\n"
            "ports: 0=GNSS, 1=GNSS2, 2=MAC, 3=NMEA\n");
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
    else if (strcmp(argv[1], "get") == 0 && argc == 2)
        result = cmd_get(handle);
    else if (strcmp(argv[1], "set-system") == 0 && argc == 2)
        result = cmd_set_system(handle);
    else if (strcmp(argv[1], "uart-config") == 0)
        result = cmd_uart_config(handle, argc, argv);
    else if (strcmp(argv[1], "uart-read") == 0)
        result = cmd_uart_read(handle, argc, argv);
    else if (strcmp(argv[1], "uart-write") == 0)
        result = cmd_uart_write(handle, argc, argv);
    else if (strcmp(argv[1], "hierarchy-status") == 0 && argc == 2)
        result = cmd_hierarchy(handle, TIMECARD_HIERARCHY_QUERY, 0);
    else if (strcmp(argv[1], "hierarchy-enable") == 0 && argc == 2)
        result = cmd_hierarchy(handle, TIMECARD_HIERARCHY_ENABLE, 0);
    else if (strcmp(argv[1], "hierarchy-persist") == 0 && argc == 2)
        result = cmd_hierarchy(handle, TIMECARD_HIERARCHY_ENABLE, 1);
    else if (strcmp(argv[1], "hierarchy-disable") == 0 && argc == 2)
        result = cmd_hierarchy(handle, TIMECARD_HIERARCHY_DISABLE, 1);
    CloseHandle(handle);

    if (result == 2)
        usage();
    return result;
}
