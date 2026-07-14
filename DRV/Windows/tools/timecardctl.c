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
    printf("NMEA generator: %s, %lu baud, %s polarity\n",
           (control->Flags & TIMECARD_NMEA_FLAG_ENABLED) ?
               "enabled" : "disabled",
           (unsigned long)control->Baud,
           control->Polarity ? "inverted" : "normal");
    printf("Selector/control/status/version: %lu / 0x%08lx / 0x%08lx / 0x%08lx\n",
           (unsigned long)control->BaudSelector,
           (unsigned long)control->Control,
           (unsigned long)control->Status,
           (unsigned long)control->Version);
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

    if (argc < 4 || argc > 5)
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
    if (argc == 5) {
        if (_stricmp(argv[4], "inverted") == 0)
            control.Polarity = 1;
        else if (_stricmp(argv[4], "normal") != 0)
            return 2;
    }
    if (timecard_ioctl(handle, IOCTL_TIMECARD_NMEA_SET,
                       &control, sizeof(control), &control,
                       sizeof(control), NULL))
        return 1;
    print_nmea(&control);
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
    return 0;
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
usage(void)
{
    fprintf(stderr,
            "usage:\n"
            "  timecardctl status\n"
            "  timecardctl get\n"
            "  timecardctl set-system\n"
            "  timecardctl clock-source <none|tod|irig|pps|ptp|rtc|dcf|regs|ext>\n"
            "  timecardctl serial\n"
            "  timecardctl nmea-status\n"
            "  timecardctl nmea-set <on|off> <baud> [normal|inverted]\n"
            "  timecardctl uart-config <port 0..3> <baud>\n"
            "  timecardctl uart-read <port> [bytes] [timeout-ms]\n"
            "  timecardctl uart-write <port> <text>\n"
            "  timecardctl hierarchy-status\n"
            "  timecardctl hierarchy-enable\n"
            "  timecardctl hierarchy-persist\n"
            "  timecardctl hierarchy-disable\n"
            "  timecardctl sma-status [connector 1..4]\n"
            "  timecardctl sma-set <connector> <input|output> <function>\n"
            "  timecardctl sma-set <connector> disabled\n"
            "  timecardctl i2c-status\n"
            "  timecardctl i2c-scan\n"
            "  timecardctl i2c-read <address> <subaddress> <bytes> [subaddress-bytes 0..2]\n"
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
    else if (strcmp(argv[1], "clock-source") == 0)
        result = cmd_clock_source(handle, argc, argv);
    else if (strcmp(argv[1], "serial") == 0 && argc == 2)
        result = cmd_serial(handle);
    else if (strcmp(argv[1], "nmea-status") == 0 && argc == 2)
        result = cmd_nmea_status(handle);
    else if (strcmp(argv[1], "nmea-set") == 0)
        result = cmd_nmea_set(handle, argc, argv);
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
    else if (strcmp(argv[1], "sma-status") == 0)
        result = cmd_sma_status(handle, argc, argv);
    else if (strcmp(argv[1], "sma-set") == 0)
        result = cmd_sma_set(handle, argc, argv);
    else if (strcmp(argv[1], "i2c-status") == 0 && argc == 2)
        result = cmd_i2c_status(handle);
    else if (strcmp(argv[1], "i2c-scan") == 0 && argc == 2)
        result = cmd_i2c_scan(handle);
    else if (strcmp(argv[1], "i2c-read") == 0)
        result = cmd_i2c_read(handle, argc, argv);
    CloseHandle(handle);

    if (result == 2)
        usage();
    return result;
}
