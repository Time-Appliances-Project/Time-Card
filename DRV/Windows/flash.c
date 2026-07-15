/* SPDX-License-Identifier: BSD-3-Clause */
/* Guarded Xilinx SPI / SPI-NOR access for FPGA firmware updates. */

#include "timecard.h"

#define XSPI_DGIER_OFFSET 0x01cu
#define XSPI_IIER_OFFSET  0x028u
#define XSPI_RESETR_OFFSET 0x040u
#define XSPI_CR_OFFSET    0x060u
#define XSPI_SR_OFFSET    0x064u
#define XSPI_TXD_OFFSET   0x068u
#define XSPI_RXD_OFFSET   0x06cu
#define XSPI_SSR_OFFSET   0x070u

#define XSPI_RESET_VALUE 0x0000000au
#define XSPI_CR_ENABLE 0x002u
#define XSPI_CR_MASTER 0x004u
#define XSPI_CR_TXFIFO_RESET 0x020u
#define XSPI_CR_RXFIFO_RESET 0x040u
#define XSPI_CR_MANUAL_SELECT 0x080u
#define XSPI_CR_TRANS_INHIBIT 0x100u
#define XSPI_SR_RX_EMPTY 0x001u
#define XSPI_SR_TX_EMPTY 0x004u
#define XSPI_SR_TX_FULL  0x008u
#define XSPI_SR_MODE_FAULT 0x010u

#define SPI_NOR_WRITE_ENABLE 0x06u
#define SPI_NOR_READ_STATUS  0x05u
#define SPI_NOR_READ_ID      0x9fu
#define SPI_NOR_RESET_ENABLE 0x66u
#define SPI_NOR_RESET        0x99u
#define SPI_NOR_READ         0x03u
#define SPI_NOR_PAGE_PROGRAM 0x02u
#define SPI_NOR_ERASE_4K     0x20u
#define SPI_NOR_READ_4BYTE         0x13u
#define SPI_NOR_PAGE_PROGRAM_4BYTE 0x12u
#define SPI_NOR_ERASE_4K_4BYTE     0x21u

#define SPI_NOR_STATUS_BUSY 0x01u
#define SPI_NOR_STATUS_WEL  0x02u
#define TIMECARD_FLASH_TRANSFER_POLL_US 5u
#define TIMECARD_FLASH_TRANSFER_POLLS 100000u
#define TIMECARD_FLASH_PROGRAM_TIMEOUT_MS 2000u
#define TIMECARD_FLASH_ERASE_TIMEOUT_MS 10000u

static ULONG
TimeCardFlashRead32(PDEVICE_CONTEXT context, ULONG offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(context->Flash + offset));
}

static VOID
TimeCardFlashWrite32(PDEVICE_CONTEXT context, ULONG offset, ULONG value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(context->Flash + offset), value);
}

static VOID
TimeCardFlashDelayMilliseconds(ULONG milliseconds)
{
    LARGE_INTEGER interval;

    interval.QuadPart = -10000;
    while (milliseconds-- != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

static ULONG
TimeCardFlashBaseControl(VOID)
{
    return XSPI_CR_MANUAL_SELECT | XSPI_CR_MASTER | XSPI_CR_ENABLE |
           XSPI_CR_TRANS_INHIBIT;
}

static NTSTATUS
TimeCardFlashInitialize(PDEVICE_CONTEXT context)
{
    ULONG depth = 0;
    ULONG control = TimeCardFlashBaseControl();

    if (!context->HardwareReady || context->Flash == NULL)
        return STATUS_DEVICE_NOT_READY;

    TimeCardFlashWrite32(context, XSPI_RESETR_OFFSET, XSPI_RESET_VALUE);
    TimeCardFlashWrite32(context, XSPI_DGIER_OFFSET, 0);
    TimeCardFlashWrite32(context, XSPI_IIER_OFFSET, 0);
    TimeCardFlashWrite32(context, XSPI_SSR_OFFSET, 0xffffffffu);
    TimeCardFlashWrite32(context, XSPI_CR_OFFSET,
                         control | XSPI_CR_TXFIFO_RESET |
                         XSPI_CR_RXFIFO_RESET);
    TimeCardFlashWrite32(context, XSPI_CR_OFFSET, control);

    /* Detect one-, 16-, or 256-byte FIFOs while the transmitter is inhibited. */
    while (depth < TIMECARD_FLASH_MAX_TRANSFER) {
        ULONG status = TimeCardFlashRead32(context, XSPI_SR_OFFSET);

        if ((status & XSPI_SR_TX_FULL) != 0)
            break;
        TimeCardFlashWrite32(context, XSPI_TXD_OFFSET, 0);
        ++depth;
    }
    if (depth == 0)
        return STATUS_DEVICE_HARDWARE_ERROR;

    TimeCardFlashWrite32(context, XSPI_RESETR_OFFSET, XSPI_RESET_VALUE);
    TimeCardFlashWrite32(context, XSPI_DGIER_OFFSET, 0);
    TimeCardFlashWrite32(context, XSPI_IIER_OFFSET, 0);
    TimeCardFlashWrite32(context, XSPI_SSR_OFFSET, 0xffffffffu);
    TimeCardFlashWrite32(context, XSPI_CR_OFFSET,
                         control | XSPI_CR_TXFIFO_RESET |
                         XSPI_CR_RXFIFO_RESET);
    TimeCardFlashWrite32(context, XSPI_CR_OFFSET, control);
    context->FlashFifoDepth = depth;
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardFlashSpiTransfer(PDEVICE_CONTEXT context, const UCHAR *transmit,
                         UCHAR *receive, ULONG length)
{
    ULONG control = TimeCardFlashBaseControl();
    ULONG offset = 0;
    NTSTATUS result = STATUS_SUCCESS;

    if (length == 0 || context->FlashFifoDepth == 0)
        return STATUS_INVALID_DEVICE_STATE;

    TimeCardFlashWrite32(context, XSPI_CR_OFFSET,
                         control | XSPI_CR_TXFIFO_RESET |
                         XSPI_CR_RXFIFO_RESET);
    TimeCardFlashWrite32(context, XSPI_CR_OFFSET, control);
    TimeCardFlashWrite32(context, XSPI_SSR_OFFSET, 0xfffffffeu);

    while (offset < length) {
        ULONG chunk = length - offset;
        ULONG i;
        ULONG polls;
        ULONG status = 0;

        if (chunk > context->FlashFifoDepth)
            chunk = context->FlashFifoDepth;
        for (i = 0; i < chunk; ++i) {
            ULONG value = transmit == NULL ? 0u : transmit[offset + i];
            TimeCardFlashWrite32(context, XSPI_TXD_OFFSET, value);
        }

        TimeCardFlashWrite32(context, XSPI_CR_OFFSET,
                             control & ~XSPI_CR_TRANS_INHIBIT);
        for (polls = 0; polls < TIMECARD_FLASH_TRANSFER_POLLS; ++polls) {
            status = TimeCardFlashRead32(context, XSPI_SR_OFFSET);
            if ((status & XSPI_SR_MODE_FAULT) != 0) {
                result = STATUS_DEVICE_HARDWARE_ERROR;
                goto Exit;
            }
            if ((status & XSPI_SR_TX_EMPTY) != 0)
                break;
            KeStallExecutionProcessor(TIMECARD_FLASH_TRANSFER_POLL_US);
        }
        TimeCardFlashWrite32(context, XSPI_CR_OFFSET, control);
        if ((status & XSPI_SR_TX_EMPTY) == 0) {
            result = STATUS_IO_TIMEOUT;
            goto Exit;
        }

        for (i = 0; i < chunk; ++i) {
            for (polls = 0; polls < TIMECARD_FLASH_TRANSFER_POLLS; ++polls) {
                status = TimeCardFlashRead32(context, XSPI_SR_OFFSET);
                if ((status & XSPI_SR_RX_EMPTY) == 0)
                    break;
                KeStallExecutionProcessor(TIMECARD_FLASH_TRANSFER_POLL_US);
            }
            if ((status & XSPI_SR_RX_EMPTY) != 0) {
                result = STATUS_IO_TIMEOUT;
                goto Exit;
            }
            status = TimeCardFlashRead32(context, XSPI_RXD_OFFSET);
            if (receive != NULL)
                receive[offset + i] = (UCHAR)status;
        }
        offset += chunk;
    }

Exit:
    TimeCardFlashWrite32(context, XSPI_CR_OFFSET, control);
    TimeCardFlashWrite32(context, XSPI_SSR_OFFSET, 0xffffffffu);
    return result;
}

static NTSTATUS
TimeCardFlashSimpleCommand(PDEVICE_CONTEXT context, UCHAR command)
{
    UCHAR received;

    return TimeCardFlashSpiTransfer(context, &command, &received, 1u);
}

static NTSTATUS
TimeCardFlashReadStatusRegister(PDEVICE_CONTEXT context, UCHAR *flashStatus)
{
    UCHAR transmit[2] = { SPI_NOR_READ_STATUS, 0 };
    UCHAR receive[2];
    NTSTATUS status;

    status = TimeCardFlashSpiTransfer(context, transmit, receive,
                                      RTL_NUMBER_OF(transmit));
    if (NT_SUCCESS(status))
        *flashStatus = receive[1];
    return status;
}

static NTSTATUS
TimeCardFlashWaitReady(PDEVICE_CONTEXT context, ULONG timeoutMilliseconds,
                       UCHAR *flashStatus)
{
    ULONG elapsed;
    NTSTATUS status;

    for (elapsed = 0; elapsed < timeoutMilliseconds; ++elapsed) {
        status = TimeCardFlashReadStatusRegister(context, flashStatus);
        if (!NT_SUCCESS(status))
            return status;
        if ((*flashStatus & SPI_NOR_STATUS_BUSY) == 0)
            return STATUS_SUCCESS;
        TimeCardFlashDelayMilliseconds(1);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
TimeCardFlashWriteEnable(PDEVICE_CONTEXT context, UCHAR *flashStatus)
{
    NTSTATUS status = TimeCardFlashSimpleCommand(context,
                                                 SPI_NOR_WRITE_ENABLE);

    if (!NT_SUCCESS(status))
        return status;
    status = TimeCardFlashReadStatusRegister(context, flashStatus);
    if (!NT_SUCCESS(status))
        return status;
    return (*flashStatus & SPI_NOR_STATUS_WEL) != 0 ?
           STATUS_SUCCESS : STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS
TimeCardFlashIdentify(PDEVICE_CONTEXT context)
{
    UCHAR transmit[4] = { SPI_NOR_READ_ID, 0, 0, 0 };
    UCHAR receive[4];
    ULONGLONG capacity;
    ULONG capacityCode;
    NTSTATUS status;

    if (context->FlashJedecId != 0 && context->FlashCapacity != 0)
        return STATUS_SUCCESS;

    status = TimeCardFlashInitialize(context);
    if (!NT_SUCCESS(status))
        return status;

    /* FPGA configuration can leave some flashes in a non-default protocol. */
    (VOID)TimeCardFlashSimpleCommand(context, SPI_NOR_RESET_ENABLE);
    (VOID)TimeCardFlashSimpleCommand(context, SPI_NOR_RESET);
    KeStallExecutionProcessor(50);

    status = TimeCardFlashSpiTransfer(context, transmit, receive,
                                      RTL_NUMBER_OF(transmit));
    if (!NT_SUCCESS(status))
        return status;
    context->FlashJedecId = ((ULONG)receive[1] << 16) |
                            ((ULONG)receive[2] << 8) | receive[3];
    if (context->FlashJedecId == 0 ||
        context->FlashJedecId == 0x00ffffffu) {
        context->FlashJedecId = 0;
        return STATUS_NO_SUCH_DEVICE;
    }

    capacityCode = receive[3];
    if (capacityCode < 20u || capacityCode > 31u) {
        context->FlashJedecId = 0;
        return STATUS_NOT_SUPPORTED;
    }
    capacity = 1ull << capacityCode;
    if (capacity > MAXULONG || capacity <= TIMECARD_FLASH_FIRMWARE_OFFSET) {
        context->FlashJedecId = 0;
        return STATUS_NOT_SUPPORTED;
    }
    context->FlashCapacity = (ULONG)capacity;
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardFlashValidateRange(PDEVICE_CONTEXT context,
                           const TIMECARD_FLASH_RANGE *request,
                           ULONG maximumLength, PULONG absoluteOffset)
{
    ULONG available;

    if (request->Size < sizeof(*request) || request->Length == 0 ||
        request->Length > maximumLength)
        return STATUS_INVALID_PARAMETER;
    available = context->FlashCapacity - TIMECARD_FLASH_FIRMWARE_OFFSET;
    if (request->Offset > available || request->Length >
        available - request->Offset)
        return STATUS_INVALID_PARAMETER;
    *absoluteOffset = TIMECARD_FLASH_FIRMWARE_OFFSET + request->Offset;
    return STATUS_SUCCESS;
}

static ULONG
TimeCardFlashBuildAddressCommand(PDEVICE_CONTEXT context, UCHAR opcode3,
                                 UCHAR opcode4, ULONG address, UCHAR *command)
{
    if (context->FlashCapacity > 0x01000000u) {
        command[0] = opcode4;
        command[1] = (UCHAR)(address >> 24);
        command[2] = (UCHAR)(address >> 16);
        command[3] = (UCHAR)(address >> 8);
        command[4] = (UCHAR)address;
        return 5u;
    }
    command[0] = opcode3;
    command[1] = (UCHAR)(address >> 16);
    command[2] = (UCHAR)(address >> 8);
    command[3] = (UCHAR)address;
    return 4u;
}

NTSTATUS
TimeCardFlashQuery(PDEVICE_CONTEXT context, TIMECARD_FLASH_STATUS *status)
{
    UCHAR flashStatus = 0;
    NTSTATUS result;

    RtlZeroMemory(status, sizeof(*status));
    status->Size = sizeof(*status);
    status->Offset = context->FlashOffset;
    status->FirmwareOffset = TIMECARD_FLASH_FIRMWARE_OFFSET;
    status->EraseSize = TIMECARD_FLASH_ERASE_SIZE;
    status->PageSize = TIMECARD_FLASH_PAGE_SIZE;
    if (!context->HardwareReady || context->Flash == NULL)
        return STATUS_DEVICE_NOT_READY;
    status->Flags = TIMECARD_FLASH_FLAG_PRESENT;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    result = TimeCardFlashIdentify(context);
    if (NT_SUCCESS(result)) {
        status->Flags |= TIMECARD_FLASH_FLAG_IDENTIFIED |
                         TIMECARD_FLASH_FLAG_SUPPORTED;
        if (context->FlashCapacity > 0x01000000u)
            status->Flags |= TIMECARD_FLASH_FLAG_FOUR_BYTE;
        status->JedecId = context->FlashJedecId;
        status->CapacityBytes = context->FlashCapacity;
        status->FifoDepth = context->FlashFifoDepth;
        result = TimeCardFlashReadStatusRegister(context, &flashStatus);
        status->FlashStatus = flashStatus;
        status->ControllerStatus =
            TimeCardFlashRead32(context, XSPI_SR_OFFSET);
    }
    WdfWaitLockRelease(context->RegisterLock);
    return result;
}

NTSTATUS
TimeCardFlashRead(PDEVICE_CONTEXT context,
                  const TIMECARD_FLASH_RANGE *request,
                  TIMECARD_FLASH_TRANSFER *transfer)
{
    UCHAR transmit[TIMECARD_FLASH_MAX_TRANSFER + 5u];
    UCHAR receive[TIMECARD_FLASH_MAX_TRANSFER + 5u];
    ULONG absoluteOffset;
    ULONG commandLength;
    NTSTATUS status;

    if (!context->HardwareReady || context->Flash == NULL)
        return STATUS_DEVICE_NOT_READY;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardFlashIdentify(context);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardFlashValidateRange(
        context, request, TIMECARD_FLASH_MAX_TRANSFER, &absoluteOffset);
    if (!NT_SUCCESS(status))
        goto Exit;

    RtlZeroMemory(transmit, sizeof(transmit));
    commandLength = TimeCardFlashBuildAddressCommand(
        context, SPI_NOR_READ, SPI_NOR_READ_4BYTE, absoluteOffset, transmit);
    status = TimeCardFlashSpiTransfer(
        context, transmit, receive, commandLength + request->Length);
    if (NT_SUCCESS(status)) {
        RtlZeroMemory(transfer, sizeof(*transfer));
        transfer->Size = sizeof(*transfer);
        transfer->Offset = request->Offset;
        transfer->Length = request->Length;
        RtlCopyMemory(transfer->Data, receive + commandLength,
                      request->Length);
    }

Exit:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardFlashErase(PDEVICE_CONTEXT context,
                   const TIMECARD_FLASH_RANGE *request,
                   TIMECARD_FLASH_RESULT *result)
{
    UCHAR command[5];
    UCHAR receive[5];
    UCHAR flashStatus = 0;
    ULONG absoluteOffset;
    ULONG commandLength;
    NTSTATUS status;

    if (!context->HardwareReady || context->Flash == NULL)
        return STATUS_DEVICE_NOT_READY;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardFlashIdentify(context);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardFlashValidateRange(
        context, request, TIMECARD_FLASH_ERASE_SIZE, &absoluteOffset);
    if (!NT_SUCCESS(status))
        goto Exit;
    if (request->Length != TIMECARD_FLASH_ERASE_SIZE ||
        (request->Offset % TIMECARD_FLASH_ERASE_SIZE) != 0) {
        status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    status = TimeCardFlashWriteEnable(context, &flashStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    commandLength = TimeCardFlashBuildAddressCommand(
        context, SPI_NOR_ERASE_4K, SPI_NOR_ERASE_4K_4BYTE,
        absoluteOffset, command);
    status = TimeCardFlashSpiTransfer(context, command, receive,
                                      commandLength);
    if (NT_SUCCESS(status))
        status = TimeCardFlashWaitReady(
            context, TIMECARD_FLASH_ERASE_TIMEOUT_MS, &flashStatus);
    if (NT_SUCCESS(status)) {
        RtlZeroMemory(result, sizeof(*result));
        result->Size = sizeof(*result);
        result->Offset = request->Offset;
        result->Length = request->Length;
        result->Status = flashStatus;
    }

Exit:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardFlashProgram(PDEVICE_CONTEXT context,
                     const TIMECARD_FLASH_TRANSFER *request,
                     TIMECARD_FLASH_RESULT *result)
{
    TIMECARD_FLASH_RANGE range;
    UCHAR transmit[TIMECARD_FLASH_MAX_TRANSFER + 5u];
    UCHAR receive[TIMECARD_FLASH_MAX_TRANSFER + 5u];
    UCHAR flashStatus = 0;
    ULONG absoluteOffset;
    ULONG commandLength;
    NTSTATUS status;

    if (!context->HardwareReady || context->Flash == NULL)
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&range, sizeof(range));
    range.Size = sizeof(range);
    range.Offset = request->Offset;
    range.Length = request->Length;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardFlashIdentify(context);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardFlashValidateRange(
        context, &range, TIMECARD_FLASH_MAX_TRANSFER, &absoluteOffset);
    if (!NT_SUCCESS(status))
        goto Exit;
    if (request->Size < FIELD_OFFSET(TIMECARD_FLASH_TRANSFER, Data) +
                        request->Length ||
        (absoluteOffset & (TIMECARD_FLASH_PAGE_SIZE - 1u)) +
            request->Length > TIMECARD_FLASH_PAGE_SIZE) {
        status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    status = TimeCardFlashWriteEnable(context, &flashStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    commandLength = TimeCardFlashBuildAddressCommand(
        context, SPI_NOR_PAGE_PROGRAM, SPI_NOR_PAGE_PROGRAM_4BYTE,
        absoluteOffset, transmit);
    RtlCopyMemory(transmit + commandLength, request->Data, request->Length);
    status = TimeCardFlashSpiTransfer(
        context, transmit, receive, commandLength + request->Length);
    if (NT_SUCCESS(status))
        status = TimeCardFlashWaitReady(
            context, TIMECARD_FLASH_PROGRAM_TIMEOUT_MS, &flashStatus);
    if (NT_SUCCESS(status)) {
        RtlZeroMemory(result, sizeof(*result));
        result->Size = sizeof(*result);
        result->Offset = request->Offset;
        result->Length = request->Length;
        result->Status = flashStatus;
    }

Exit:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}
