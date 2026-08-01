/* SPDX-License-Identifier: BSD-3-Clause */
/* IOCTL dispatch for the OCP TimeCard Windows driver. */

#include "timecard.h"

static NTSTATUS
TimeCardGetInput(WDFREQUEST request, size_t minimum, PVOID *buffer,
                 size_t *length)
{
    return WdfRequestRetrieveInputBuffer(request, minimum, buffer, length);
}

static NTSTATUS
TimeCardGetOutput(WDFREQUEST request, size_t minimum, PVOID *buffer)
{
    return WdfRequestRetrieveOutputBuffer(request, minimum, buffer, NULL);
}

VOID
TimeCardEvtIoDeviceControl(WDFQUEUE queue, WDFREQUEST request,
                           size_t outputBufferLength,
                           size_t inputBufferLength, ULONG ioControlCode)
{
    PDEVICE_CONTEXT context = DeviceGetContext(WdfIoQueueGetDevice(queue));
    size_t information = 0;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(outputBufferLength);

    switch (ioControlCode) {
    case IOCTL_TIMECARD_GET_TIME:
    {
        TIMECARD_TIME *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardGetTime(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_SET_TIME:
    {
        TIMECARD_TIME *input;
        TIMECARD_TIME value;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (NT_SUCCESS(status)) {
            value = *input;
            status = TimeCardSetTime(context, &value);
        }
        break;
    }

    case IOCTL_TIMECARD_GET_INFO:
    {
        TIMECARD_INFO *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardGetInfo(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_GET_CROSSTIMESTAMP:
    {
        TIMECARD_CROSSTIMESTAMP *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardGetCrossTimestamp(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_UART_CONFIGURE:
    {
        TIMECARD_UART_CONFIG *input;
        TIMECARD_UART_CONFIG config;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (NT_SUCCESS(status)) {
            config = *input;
            status = TimeCardUartConfigure(context, &config);
        }
        break;
    }

    case IOCTL_TIMECARD_UART_READ:
    {
        TIMECARD_UART_READ_REQUEST *input;
        TIMECARD_UART_READ_REQUEST readRequest;
        TIMECARD_UART_TRANSFER *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        readRequest = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardUartRead(context, &readRequest, output);
            if (NT_SUCCESS(status)) {
                information = FIELD_OFFSET(TIMECARD_UART_TRANSFER, Data) +
                              output->Length;
            }
        }
        break;
    }

    case IOCTL_TIMECARD_UART_WRITE:
    {
        TIMECARD_UART_TRANSFER *input;
        TIMECARD_UART_TRANSFER transfer;
        TIMECARD_UART_RESULT *output;
        size_t actualInputLength;

        status = TimeCardGetInput(
            request, FIELD_OFFSET(TIMECARD_UART_TRANSFER, Data),
            (PVOID *)&input, &actualInputLength);
        if (!NT_SUCCESS(status))
            break;
        if (actualInputLength > sizeof(transfer))
            actualInputLength = sizeof(transfer);
        RtlZeroMemory(&transfer, sizeof(transfer));
        RtlCopyMemory(&transfer, input, actualInputLength);

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardUartWrite(context, &transfer,
                                       (ULONG)inputBufferLength, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_HIERARCHY_CONTROL:
    {
        TIMECARD_HIERARCHY_CONTROL *input;
        TIMECARD_HIERARCHY_CONTROL requestValue;
        TIMECARD_HIERARCHY_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardControlHierarchy(context, &requestValue,
                                               output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_SMA_QUERY:
    {
        TIMECARD_SMA_CONTROL *input;
        TIMECARD_SMA_CONTROL requestValue;
        TIMECARD_SMA_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardSmaQuery(context, requestValue.Connector,
                                      output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_SMA_SET:
    {
        TIMECARD_SMA_CONTROL *input;
        TIMECARD_SMA_CONTROL requestValue;
        TIMECARD_SMA_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardSmaSet(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_I2C_GET_STATUS:
    {
        TIMECARD_I2C_STATUS *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardI2cGetStatus(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_I2C_PROBE:
    {
        TIMECARD_I2C_PROBE *input;
        TIMECARD_I2C_PROBE requestValue;
        TIMECARD_I2C_PROBE *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        if (requestValue.Size < sizeof(requestValue)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardI2cProbe(context, requestValue.Address,
                                      output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_I2C_READ:
    {
        TIMECARD_I2C_READ_REQUEST *input;
        TIMECARD_I2C_READ_REQUEST requestValue;
        TIMECARD_I2C_TRANSFER *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardI2cRead(context, &requestValue, output);
            if (NT_SUCCESS(status)) {
                information = FIELD_OFFSET(TIMECARD_I2C_TRANSFER, Data) +
                              output->Length;
            }
        }
        break;
    }

    case IOCTL_TIMECARD_I2C_MUX_QUERY:
    {
        TIMECARD_I2C_MUX_CONTROL *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardI2cMuxQuery(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_I2C_MUX_SET:
    {
        TIMECARD_I2C_MUX_CONTROL *input;
        TIMECARD_I2C_MUX_CONTROL requestValue;
        TIMECARD_I2C_MUX_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardI2cMuxSet(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_LED_QUERY:
    {
        TIMECARD_LED_CONTROL *input;
        TIMECARD_LED_CONTROL requestValue;
        TIMECARD_LED_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardLedQuery(context, requestValue.Led, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_LED_SET:
    {
        TIMECARD_LED_CONTROL *input;
        TIMECARD_LED_CONTROL requestValue;
        TIMECARD_LED_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardLedSet(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_SENSOR_QUERY:
    {
        TIMECARD_SENSOR_TELEMETRY *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardSensorQuery(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_CLOCK_SOURCE_SET:
    {
        TIMECARD_CLOCK_SOURCE_CONTROL *input;
        TIMECARD_CLOCK_SOURCE_CONTROL requestValue;
        TIMECARD_CLOCK_SOURCE_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardSetClockSource(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_NMEA_QUERY:
    {
        TIMECARD_NMEA_CONTROL *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardNmeaQuery(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_NMEA_SET:
    {
        TIMECARD_NMEA_CONTROL *input;
        TIMECARD_NMEA_CONTROL requestValue;
        TIMECARD_NMEA_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardNmeaSet(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_GET_IDENTITY:
    {
        TIMECARD_IDENTITY *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardGetIdentity(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_SIGNAL_QUERY:
    {
        TIMECARD_SIGNAL_CONTROL *input;
        TIMECARD_SIGNAL_CONTROL requestValue;
        TIMECARD_SIGNAL_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardSignalQuery(context, requestValue.Generator,
                                         output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_SIGNAL_SET:
    {
        TIMECARD_SIGNAL_CONTROL *input;
        TIMECARD_SIGNAL_CONTROL requestValue;
        TIMECARD_SIGNAL_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardSignalSet(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_FREQUENCY_QUERY:
    {
        TIMECARD_FREQUENCY_CONTROL *input;
        TIMECARD_FREQUENCY_CONTROL requestValue;
        TIMECARD_FREQUENCY_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardFrequencyQuery(context, requestValue.Counter,
                                            output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_FREQUENCY_SET:
    {
        TIMECARD_FREQUENCY_CONTROL *input;
        TIMECARD_FREQUENCY_CONTROL requestValue;
        TIMECARD_FREQUENCY_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardFrequencySet(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_FLASH_QUERY:
    {
        TIMECARD_FLASH_STATUS *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardFlashQuery(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_FLASH_READ:
    {
        TIMECARD_FLASH_RANGE *input;
        TIMECARD_FLASH_RANGE requestValue;
        TIMECARD_FLASH_TRANSFER *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardFlashRead(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = FIELD_OFFSET(TIMECARD_FLASH_TRANSFER, Data) +
                              output->Length;
        }
        break;
    }

    case IOCTL_TIMECARD_FLASH_ERASE:
    {
        TIMECARD_FLASH_RANGE *input;
        TIMECARD_FLASH_RANGE requestValue;
        TIMECARD_FLASH_RESULT *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardFlashErase(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_FLASH_PROGRAM:
    {
        TIMECARD_FLASH_TRANSFER *input;
        TIMECARD_FLASH_TRANSFER requestValue;
        TIMECARD_FLASH_RESULT *output;
        size_t actualInputLength = 0;
        size_t requiredInputLength;

        status = TimeCardGetInput(
            request, FIELD_OFFSET(TIMECARD_FLASH_TRANSFER, Data),
            (PVOID *)&input, &actualInputLength);
        if (!NT_SUCCESS(status))
            break;
        if (actualInputLength > sizeof(requestValue)) {
            status = STATUS_INVALID_BUFFER_SIZE;
            break;
        }
        RtlZeroMemory(&requestValue, sizeof(requestValue));
        RtlCopyMemory(&requestValue, input, actualInputLength);
        if (requestValue.Length > TIMECARD_FLASH_MAX_TRANSFER) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        requiredInputLength =
            FIELD_OFFSET(TIMECARD_FLASH_TRANSFER, Data) +
            requestValue.Length;
        if (requestValue.Size < requiredInputLength ||
            actualInputLength < requiredInputLength) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardFlashProgram(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_UART_OBSERVE:
    {
        TIMECARD_UART_OBSERVE *input;
        TIMECARD_UART_OBSERVE requestValue;
        TIMECARD_UART_OBSERVE *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardUartObserve(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_MRO50_QUERY:
    {
        TIMECARD_MRO50_STATUS *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardMro50Query(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_MRO50_CONTROL:
    {
        TIMECARD_MRO50_CONTROL *input;
        TIMECARD_MRO50_CONTROL requestValue;
        TIMECARD_MRO50_STATUS *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardMro50Control(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_GET_CAPABILITIES:
    {
        TIMECARD_CAPABILITIES *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardGetCapabilities(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_PHASE_QUERY:
    {
        TIMECARD_PHASE_SAMPLE *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardPhaseQuery(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_PHASE_CONTROL:
    {
        TIMECARD_PHASE_CONTROL *input;
        TIMECARD_PHASE_CONTROL requestValue;
        TIMECARD_PHASE_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardPhaseControl(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_PHC_ADJUST:
    {
        TIMECARD_PHC_ADJUST *input;
        TIMECARD_PHC_ADJUST requestValue;
        TIMECARD_PHC_ADJUST *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardAdjustPhc(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_DISCIPLINE_READ:
    {
        TIMECARD_DISCIPLINE_BLOB *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardDisciplineRead(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_DISCIPLINE_WRITE:
    {
        TIMECARD_DISCIPLINE_BLOB *input;
        TIMECARD_DISCIPLINE_BLOB requestValue;
        TIMECARD_DISCIPLINE_BLOB *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;
        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardDisciplineWrite(context, &requestValue, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(request, status, information);
}
