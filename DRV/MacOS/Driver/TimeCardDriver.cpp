/* SPDX-License-Identifier: BSD-3-Clause */

#include <DriverKit/IOLib.h>
#include <DriverKit/OSData.h>
#include <DriverKit/IOUserServer.h>
#include <PCIDriverKit/IOPCIDevice.h>
#include <PCIDriverKit/IOPCIFamilyDefinitions.h>
#include <os/log.h>
#include <time.h>

#include "TimeCardDriver.h"
#include "TimeCardUserClient.h"
#include "TimeCardRegisters.h"

struct TimeCardDriver_IVars {
    IOPCIDevice *pciDevice;
    struct IOLock *registerLock;
    uint8_t memoryIndex;
    uint16_t vendorID;
    uint16_t deviceID;
    uint32_t interruptVectors;
    uint64_t barSize;
    TimeCardRegisterMap registers;
    bool deviceOpen;
};

struct TimeCardUserClient_IVars {
    TimeCardDriver *driver;
};

static uint32_t
ReadRegister32(IOPCIDevice *device, uint8_t memoryIndex, uint64_t offset)
{
    uint32_t value = UINT32_MAX;
    device->MemoryRead32(memoryIndex, offset, &value);
    return value;
}

static void
WriteRegister32(IOPCIDevice *device, uint8_t memoryIndex, uint64_t offset,
                uint32_t value)
{
    device->MemoryWrite32(memoryIndex, offset, value);
}

kern_return_t
IMPL(TimeCardDriver, Start)
{
    kern_return_t result = Start(provider, SUPERDISPATCH);
    uint8_t barType = 0;
    uint16_t command = 0;
    bool hasMSIX = false;
    uint64_t msixCapability = 0;
    uint32_t clockVersion = 0;

    if (result != kIOReturnSuccess)
        return result;

    ivars->pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (ivars->pciDevice == nullptr) {
        result = kIOReturnBadArgument;
        goto fail;
    }

    ivars->registerLock = IOLockAlloc();
    if (ivars->registerLock == nullptr) {
        result = kIOReturnNoMemory;
        goto fail;
    }

    result = ivars->pciDevice->Open(this, 0);
    if (result != kIOReturnSuccess)
        goto fail;
    ivars->deviceOpen = true;

    result = ivars->pciDevice->GetBARInfo(kPCIMemoryRangeBAR0,
                                           &ivars->memoryIndex,
                                           &ivars->barSize, &barType);
    if (result != kIOReturnSuccess)
        goto fail;
    if (barType == kPCIBARTypeIO) {
        result = kIOReturnUnsupported;
        goto fail;
    }

    ivars->pciDevice->ConfigurationRead16(kIOPCIConfigurationOffsetCommand,
                                           &command);
    command |= kIOPCICommandMemorySpace | kIOPCICommandBusLead;
    ivars->pciDevice->ConfigurationWrite16(kIOPCIConfigurationOffsetCommand,
                                            command);

    ivars->pciDevice->ConfigurationRead16(kIOPCIConfigurationOffsetVendorID,
                                           &ivars->vendorID);
    ivars->pciDevice->ConfigurationRead16(kIOPCIConfigurationOffsetDeviceID,
                                           &ivars->deviceID);

    if (ivars->pciDevice->FindPCICapability(kIOPCICapabilityIDMSIX, 0,
                                            &msixCapability) ==
        kIOReturnSuccess) {
        uint16_t messageControl = 0;
        ivars->pciDevice->ConfigurationRead16(msixCapability + 2,
                                               &messageControl);
        ivars->interruptVectors = TimeCardMSIXVectorCount(messageControl);
        hasMSIX = true;
    } else {
        ivars->interruptVectors = 1;
    }

    ivars->registers = TimeCardRegisterMapForInterrupts(
        hasMSIX, ivars->interruptVectors);
    if (!TimeCardRangeFits(ivars->barSize, 0,
                           ivars->registers.requiredBarSize)) {
        result = kIOReturnBadMedia;
        goto fail;
    }

    clockVersion = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockVersion);
    if (clockVersion == 0 || clockVersion == UINT32_MAX) {
        os_log(OS_LOG_DEFAULT,
               "TimeCard: clock register probe failed at 0x%llx",
               ivars->registers.clockOffset);
        result = kIOReturnNotResponding;
        goto fail;
    }

    os_log(OS_LOG_DEFAULT,
           "TimeCard: started %{public}04x:%{public}04x, BAR0 0x%llx, "
           "%{public}u vector(s), layout %{public}s",
           ivars->vendorID, ivars->deviceID, ivars->barSize,
           ivars->interruptVectors,
           ivars->registers.layout == kTimeCardLayoutMSIX ? "MSI-X" : "MSI");
    return kIOReturnSuccess;

fail:
    if (ivars->deviceOpen) {
        ivars->pciDevice->Close(this, 0);
        ivars->deviceOpen = false;
    }
    IOLockFreeZero(ivars->registerLock);
    ivars->pciDevice = nullptr;
    Stop(provider, SUPERDISPATCH);
    return result;
}

kern_return_t
IMPL(TimeCardDriver, Stop)
{
    if (ivars->deviceOpen && ivars->pciDevice != nullptr) {
        ivars->pciDevice->Close(this, 0);
        ivars->deviceOpen = false;
    }
    IOLockFreeZero(ivars->registerLock);
    ivars->pciDevice = nullptr;
    return Stop(provider, SUPERDISPATCH);
}

kern_return_t
IMPL(TimeCardDriver, NewUserClient)
{
    if (type != 0 || userClient == nullptr)
        return kIOReturnBadArgument;

    IOService *service = nullptr;
    kern_return_t result = Create(this, "UserClientProperties", &service);
    if (result != kIOReturnSuccess)
        return result;

    *userClient = OSDynamicCast(IOUserClient, service);
    if (*userClient == nullptr) {
        service->release();
        return kIOReturnUnsupported;
    }
    return kIOReturnSuccess;
}

kern_return_t
TimeCardDriver::GetTime(TimeCardTime *time)
{
    if (time == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;

    IOLockLock(ivars->registerLock);
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset + kTimeCardClockControl,
                    kTimeCardClockReadRequest | kTimeCardClockEnable);

    kern_return_t result = kIOReturnTimeout;
    for (uint32_t attempt = 0; attempt < 100; ++attempt) {
        const uint32_t control = ReadRegister32(
            ivars->pciDevice, ivars->memoryIndex,
            ivars->registers.clockOffset + kTimeCardClockControl);
        if ((control & kTimeCardClockReadDone) != 0) {
            result = kIOReturnSuccess;
            break;
        }
        IODelay(1);
    }

    time->nanoseconds = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockTimeNanoseconds);
    time->seconds = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockTimeSeconds);
    time->reserved = 0;
    IOLockUnlock(ivars->registerLock);

    if (time->nanoseconds >= 1000000000u)
        return kIOReturnBadMedia;
    return result;
}

kern_return_t
TimeCardDriver::SetTime(const TimeCardTime *time)
{
    if (time == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (time->seconds > UINT32_MAX || time->nanoseconds >= 1000000000u)
        return kIOReturnBadArgument;

    IOLockLock(ivars->registerLock);
    const uint32_t select = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockSelect);
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset + kTimeCardClockSelect,
                    kTimeCardClockRegisterSource);
    WriteRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockAdjustNanoseconds,
        time->nanoseconds);
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset +
                        kTimeCardClockAdjustSeconds,
                    (uint32_t)time->seconds);
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset + kTimeCardClockControl,
                    kTimeCardClockAdjustTime | kTimeCardClockEnable);
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset + kTimeCardClockSelect,
                    select >> 16);
    IOLockUnlock(ivars->registerLock);
    return kIOReturnSuccess;
}

kern_return_t
TimeCardDriver::GetCrossTimestamp(TimeCardCrossTimestamp *timestamp)
{
    if (timestamp == nullptr)
        return kIOReturnBadArgument;

    timestamp->systemTimeBeforeNanoseconds =
        clock_gettime_nsec_np(CLOCK_REALTIME);
    const kern_return_t result = GetTime(&timestamp->cardTime);
    timestamp->systemTimeAfterNanoseconds =
        clock_gettime_nsec_np(CLOCK_REALTIME);
    return result;
}

kern_return_t
TimeCardDriver::GetInfo(TimeCardInfo *info)
{
    if (info == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;

    *info = {};
    info->abiVersion = TIMECARD_ABI_VERSION;
    info->driverVersion = TIMECARD_DRIVER_VERSION;
    info->vendorID = ivars->vendorID;
    info->deviceID = ivars->deviceID;
    info->layout = ivars->registers.layout;
    info->interruptVectors = ivars->interruptVectors;
    info->barSize = ivars->barSize;
    info->clockOffset = ivars->registers.clockOffset;
    info->todOffset = ivars->registers.todOffset;

    IOLockLock(ivars->registerLock);
    info->clockVersion = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockVersion);
    info->clockStatus = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockStatus);
    info->clockSelect = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockSelect);
    info->todVersion = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.todOffset + kTimeCardTodVersion);
    info->todStatus = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.todOffset + kTimeCardTodStatus);
    info->utcStatus = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.todOffset + kTimeCardTodUtcStatus);
    info->leap = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.todOffset + kTimeCardTodLeap);
    info->gnssStatus = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.todOffset + kTimeCardTodGnssStatus);
    info->satellites = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.todOffset + kTimeCardTodSatellites);
    IOLockUnlock(ivars->registerLock);
    return kIOReturnSuccess;
}

static kern_return_t
GetInfoAction(OSObject *, void *reference,
              IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    TimeCardInfo info = {};
    const kern_return_t result = driver->GetInfo(&info);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput = OSData::withBytes(&info, sizeof(info));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
GetTimeAction(OSObject *, void *reference,
              IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    TimeCardTime time = {};
    const kern_return_t result = driver->GetTime(&time);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput = OSData::withBytes(&time, sizeof(time));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
SetTimeAction(OSObject *, void *reference,
              IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    if (arguments->structureInput == nullptr)
        return kIOReturnBadArgument;
    auto *time = static_cast<const TimeCardTime *>(
        arguments->structureInput->getBytesNoCopy());
    return driver->SetTime(time);
}

static kern_return_t
GetCrossTimestampAction(OSObject *, void *reference,
                        IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    TimeCardCrossTimestamp timestamp = {};
    const kern_return_t result = driver->GetCrossTimestamp(&timestamp);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&timestamp, sizeof(timestamp));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static const IOUserClientMethodDispatch kTimeCardDispatch[
    kTimeCardMethodCount] = {
    {GetInfoAction, false, 0, 0, 0, sizeof(TimeCardInfo)},
    {GetTimeAction, false, 0, 0, 0, sizeof(TimeCardTime)},
    {SetTimeAction, false, 0, sizeof(TimeCardTime), 0, 0},
    {GetCrossTimestampAction, false, 0, 0, 0,
     sizeof(TimeCardCrossTimestamp)},
};

kern_return_t
IMPL(TimeCardUserClient, Start)
{
    kern_return_t result = Start(provider, SUPERDISPATCH);
    if (result != kIOReturnSuccess)
        return result;

    ivars->driver = OSDynamicCast(TimeCardDriver, provider);
    if (ivars->driver == nullptr) {
        Stop(provider, SUPERDISPATCH);
        return kIOReturnBadArgument;
    }
    return kIOReturnSuccess;
}

kern_return_t
TimeCardUserClient::ExternalMethod(
    uint64_t selector,
    IOUserClientMethodArguments *arguments,
    const IOUserClientMethodDispatch *,
    OSObject *,
    void *)
{
    if (selector >= kTimeCardMethodCount || ivars->driver == nullptr)
        return kIOReturnUnsupported;

    return IOUserClient::ExternalMethod(selector, arguments,
                                        &kTimeCardDispatch[selector], this,
                                        ivars->driver);
}
