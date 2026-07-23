/* SPDX-License-Identifier: BSD-3-Clause */
/* Polled Altera SPI access for the Orolia ART Time Card. */

#include "timecard.h"

#define ALTERA_SPI_RXDATA     0x00u
#define ALTERA_SPI_TXDATA     0x04u
#define ALTERA_SPI_STATUS     0x08u
#define ALTERA_SPI_CONTROL    0x0cu
#define ALTERA_SPI_TARGET_SEL 0x14u

#define ALTERA_SPI_STATUS_ROE   0x008u
#define ALTERA_SPI_STATUS_TOE   0x010u
#define ALTERA_SPI_STATUS_TRDY  0x040u
#define ALTERA_SPI_STATUS_RRDY  0x080u
#define ALTERA_SPI_STATUS_ERROR 0x100u
#define ALTERA_SPI_CONTROL_SSO  0x400u

#define ALTERA_SPI_POLL_US 5u
#define ALTERA_SPI_TRANSFER_POLLS 100000u

static ULONG
TimeCardAlteraRead(PDEVICE_CONTEXT context, ULONG offset)
{
    return READ_REGISTER_ULONG(
        (volatile ULONG *)(context->Flash + offset));
}

static VOID
TimeCardAlteraWrite(PDEVICE_CONTEXT context, ULONG offset, ULONG value)
{
    WRITE_REGISTER_ULONG(
        (volatile ULONG *)(context->Flash + offset), value);
}

NTSTATUS
TimeCardAlteraFlashInitialize(PDEVICE_CONTEXT context)
{
    ULONG status;

    if (!context->HardwareReady || context->Flash == NULL)
        return STATUS_DEVICE_NOT_READY;

    TimeCardAlteraWrite(context, ALTERA_SPI_CONTROL, 0);
    TimeCardAlteraWrite(context, ALTERA_SPI_TARGET_SEL, 0);
    TimeCardAlteraWrite(context, ALTERA_SPI_STATUS, 0);
    status = TimeCardAlteraRead(context, ALTERA_SPI_STATUS);
    if ((status & ALTERA_SPI_STATUS_RRDY) != 0)
        (VOID)TimeCardAlteraRead(context, ALTERA_SPI_RXDATA);
    context->FlashFifoDepth = 1u;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardAlteraFlashTransfer(PDEVICE_CONTEXT context,
                            const UCHAR *transmit, UCHAR *receive,
                            ULONG length)
{
    ULONG i;
    NTSTATUS result = STATUS_SUCCESS;

    if (length == 0 || context->FlashFifoDepth == 0)
        return STATUS_INVALID_DEVICE_STATE;

    TimeCardAlteraWrite(context, ALTERA_SPI_TARGET_SEL, 1u);
    TimeCardAlteraWrite(context, ALTERA_SPI_CONTROL,
                        ALTERA_SPI_CONTROL_SSO);
    for (i = 0; i < length; ++i) {
        ULONG polls;
        ULONG status = 0;
        ULONG value = transmit == NULL ? 0u : transmit[i];

        for (polls = 0; polls < ALTERA_SPI_TRANSFER_POLLS; ++polls) {
            status = TimeCardAlteraRead(context, ALTERA_SPI_STATUS);
            if ((status & (ALTERA_SPI_STATUS_ROE |
                           ALTERA_SPI_STATUS_TOE |
                           ALTERA_SPI_STATUS_ERROR)) != 0) {
                result = STATUS_DEVICE_HARDWARE_ERROR;
                goto Exit;
            }
            if ((status & ALTERA_SPI_STATUS_TRDY) != 0)
                break;
            KeStallExecutionProcessor(ALTERA_SPI_POLL_US);
        }
        if ((status & ALTERA_SPI_STATUS_TRDY) == 0) {
            result = STATUS_IO_TIMEOUT;
            goto Exit;
        }

        TimeCardAlteraWrite(context, ALTERA_SPI_TXDATA, value);
        for (polls = 0; polls < ALTERA_SPI_TRANSFER_POLLS; ++polls) {
            status = TimeCardAlteraRead(context, ALTERA_SPI_STATUS);
            if ((status & (ALTERA_SPI_STATUS_ROE |
                           ALTERA_SPI_STATUS_TOE |
                           ALTERA_SPI_STATUS_ERROR)) != 0) {
                result = STATUS_DEVICE_HARDWARE_ERROR;
                goto Exit;
            }
            if ((status & ALTERA_SPI_STATUS_RRDY) != 0)
                break;
            KeStallExecutionProcessor(ALTERA_SPI_POLL_US);
        }
        if ((status & ALTERA_SPI_STATUS_RRDY) == 0) {
            result = STATUS_IO_TIMEOUT;
            goto Exit;
        }
        value = TimeCardAlteraRead(context, ALTERA_SPI_RXDATA);
        if (receive != NULL)
            receive[i] = (UCHAR)value;
    }

Exit:
    TimeCardAlteraWrite(context, ALTERA_SPI_CONTROL, 0);
    TimeCardAlteraWrite(context, ALTERA_SPI_TARGET_SEL, 0);
    return result;
}

ULONG
TimeCardAlteraFlashStatus(PDEVICE_CONTEXT context)
{
    return TimeCardAlteraRead(context, ALTERA_SPI_STATUS);
}
