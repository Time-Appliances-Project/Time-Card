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
    uint8_t revisionID;
    uint16_t vendorID;
    uint16_t deviceID;
    uint32_t advertisedMSIXVectors;
    uint64_t barSize;
    TimeCardRegisterMap registers;
    bool deviceOpen;
};

struct TimeCardUserClient_IVars {
    TimeCardDriver *driver;
};

bool
TimeCardDriver::init()
{
    if (!super::init())
        return false;

    ivars = IONewZero(TimeCardDriver_IVars, 1);
    return ivars != nullptr;
}

void
TimeCardDriver::free()
{
    if (ivars != nullptr) {
        IOLockFreeZero(ivars->registerLock);
        IOSafeDeleteNULL(ivars, TimeCardDriver_IVars, 1);
    }
    super::free();
}

bool
TimeCardUserClient::init()
{
    if (!super::init())
        return false;

    ivars = IONewZero(TimeCardUserClient_IVars, 1);
    return ivars != nullptr;
}

void
TimeCardUserClient::free()
{
    IOSafeDeleteNULL(ivars, TimeCardUserClient_IVars, 1);
    super::free();
}

static bool
ReadRegister32(IOPCIDevice *device, uint8_t memoryIndex, uint64_t offset,
               uint32_t *value)
{
    if (value == nullptr)
        return false;
    *value = UINT32_MAX;
    device->MemoryRead32(memoryIndex, offset, value);
    return *value != UINT32_MAX;
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
    uint16_t command = UINT16_MAX;
    uint16_t verifiedCommand = UINT16_MAX;
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

    ivars->vendorID = UINT16_MAX;
    ivars->deviceID = UINT16_MAX;
    ivars->revisionID = UINT8_MAX;
    ivars->pciDevice->ConfigurationRead16(kIOPCIConfigurationOffsetVendorID,
                                           &ivars->vendorID);
    ivars->pciDevice->ConfigurationRead16(kIOPCIConfigurationOffsetDeviceID,
                                           &ivars->deviceID);
    ivars->pciDevice->ConfigurationRead8(kIOPCIConfigurationOffsetRevisionID,
                                          &ivars->revisionID);
    if (ivars->vendorID == UINT16_MAX || ivars->deviceID == UINT16_MAX ||
        ivars->revisionID == UINT8_MAX ||
        TimeCardBoardProfileForDevice(ivars->vendorID, ivars->deviceID) ==
            kTimeCardBoardUnknown) {
        result = kIOReturnUnsupported;
        goto fail;
    }

    result = ivars->pciDevice->GetBARInfo(kPCIMemoryRangeBAR0,
                                           &ivars->memoryIndex,
                                           &ivars->barSize, &barType);
    if (result != kIOReturnSuccess)
        goto fail;
    if (barType == kPCIBARTypeIO) {
        result = kIOReturnUnsupported;
        goto fail;
    }

    if (ivars->pciDevice->FindPCICapability(kIOPCICapabilityIDMSIX, 0,
                                            &msixCapability) ==
        kIOReturnSuccess) {
        uint16_t messageControl = UINT16_MAX;
        ivars->pciDevice->ConfigurationRead16(msixCapability + 2,
                                               &messageControl);
        if (messageControl == UINT16_MAX) {
            result = kIOReturnNotResponding;
            goto fail;
        }
        ivars->advertisedMSIXVectors =
            TimeCardMSIXVectorCount(messageControl);
    } else {
        ivars->advertisedMSIXVectors = 0;
    }

    ivars->registers = TimeCardRegisterMapForDevice(
        ivars->vendorID, ivars->deviceID, ivars->revisionID,
        ivars->advertisedMSIXVectors);
    if (ivars->registers.layout == kTimeCardLayoutUnknown) {
        result = kIOReturnUnsupported;
        goto fail;
    }
    if (!TimeCardRegisterMapFits(ivars->barSize, &ivars->registers)) {
        result = kIOReturnBadMedia;
        goto fail;
    }

    ivars->pciDevice->ConfigurationRead16(kIOPCIConfigurationOffsetCommand,
                                           &command);
    if (command == UINT16_MAX) {
        result = kIOReturnNotResponding;
        goto fail;
    }
    command = (uint16_t)((command | kIOPCICommandMemorySpace) &
                         ~kIOPCICommandBusLead);
    ivars->pciDevice->ConfigurationWrite16(kIOPCIConfigurationOffsetCommand,
                                            command);
    ivars->pciDevice->ConfigurationRead16(kIOPCIConfigurationOffsetCommand,
                                           &verifiedCommand);
    if (verifiedCommand == UINT16_MAX ||
        (verifiedCommand & kIOPCICommandMemorySpace) == 0 ||
        (verifiedCommand & kIOPCICommandBusLead) != 0) {
        result = kIOReturnNotResponding;
        goto fail;
    }

    if (!ReadRegister32(
            ivars->pciDevice, ivars->memoryIndex,
            ivars->registers.clockOffset + kTimeCardClockVersion,
            &clockVersion) || clockVersion == 0) {
        os_log(OS_LOG_DEFAULT,
               "TimeCard: clock register probe failed at 0x%llx",
               ivars->registers.clockOffset);
        result = kIOReturnNotResponding;
        goto fail;
    }

    result = RegisterService();
    if (result != kIOReturnSuccess)
        goto fail;

    os_log(OS_LOG_DEFAULT,
           "TimeCard: started %{public}s %{public}04x:%{public}04x "
           "revision %{public}02x, BAR0 0x%llx, MSI-X capacity "
           "%{public}u, "
           "%{public}s",
           TimeCardBoardProfileName(ivars->registers.boardProfile),
           ivars->vendorID, ivars->deviceID, ivars->revisionID,
           ivars->barSize, ivars->advertisedMSIXVectors,
           TimeCardRegisterLayoutName(ivars->registers.layout));
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
    if ((ivars->registers.capabilities & kTimeCardCapabilityReadClock) == 0)
        return kIOReturnUnsupported;

    *time = {};

    IOLockLock(ivars->registerLock);
    uint32_t oldControl = 0;
    if (!ReadRegister32(
            ivars->pciDevice, ivars->memoryIndex,
            ivars->registers.clockOffset + kTimeCardClockControl,
            &oldControl)) {
        IOLockUnlock(ivars->registerLock);
        return kIOReturnNotResponding;
    }
    oldControl = TimeCardPersistentClockControl(oldControl);
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset + kTimeCardClockControl,
                    TimeCardClockReadRequestControl(oldControl));

    kern_return_t result = kIOReturnTimeout;
    for (uint32_t attempt = 0; attempt < 100; ++attempt) {
        uint32_t control = 0;
        if (!ReadRegister32(
                ivars->pciDevice, ivars->memoryIndex,
                ivars->registers.clockOffset + kTimeCardClockControl,
                &control)) {
            result = kIOReturnNotResponding;
            break;
        }
        if ((control & kTimeCardClockReadDone) != 0) {
            result = kIOReturnSuccess;
            break;
        }
        IODelay(1);
    }

    uint32_t nanoseconds = 0;
    uint32_t seconds = 0;
    if (result == kIOReturnSuccess &&
        (!ReadRegister32(
             ivars->pciDevice, ivars->memoryIndex,
             ivars->registers.clockOffset + kTimeCardClockTimeNanoseconds,
             &nanoseconds) ||
         !ReadRegister32(
             ivars->pciDevice, ivars->memoryIndex,
             ivars->registers.clockOffset + kTimeCardClockTimeSeconds,
             &seconds))) {
        result = kIOReturnNotResponding;
    }
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset + kTimeCardClockControl,
                    oldControl);
    IOLockUnlock(ivars->registerLock);

    if (result != kIOReturnSuccess)
        return result;
    if (nanoseconds >= 1000000000u)
        return kIOReturnBadMedia;
    time->seconds = seconds;
    time->nanoseconds = nanoseconds;
    return kIOReturnSuccess;
}

kern_return_t
TimeCardDriver::SetTime(const TimeCardTime *time)
{
    if (time == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if ((ivars->registers.capabilities & kTimeCardCapabilitySetClock) == 0)
        return kIOReturnUnsupported;
    if (time->seconds > UINT32_MAX || time->nanoseconds >= 1000000000u ||
        time->reserved != 0)
        return kIOReturnBadArgument;

    IOLockLock(ivars->registerLock);
    uint32_t select = 0;
    uint32_t oldControl = 0;
    if (!ReadRegister32(
            ivars->pciDevice, ivars->memoryIndex,
            ivars->registers.clockOffset + kTimeCardClockSelect, &select) ||
        !ReadRegister32(
            ivars->pciDevice, ivars->memoryIndex,
            ivars->registers.clockOffset + kTimeCardClockControl,
            &oldControl)) {
        IOLockUnlock(ivars->registerLock);
        return kIOReturnNotResponding;
    }
    oldControl = TimeCardPersistentClockControl(oldControl);
    const uint32_t configuredSource = TimeCardConfiguredClockSource(select);
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
                    TimeCardClockAdjustRequestControl(oldControl));
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset + kTimeCardClockSelect,
                    configuredSource);
    WriteRegister32(ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.clockOffset + kTimeCardClockControl,
                    oldControl);
    uint32_t restoredSelect = 0;
    const bool sourceRestored = ReadRegister32(
        ivars->pciDevice, ivars->memoryIndex,
        ivars->registers.clockOffset + kTimeCardClockSelect,
        &restoredSelect) &&
        TimeCardConfiguredClockSource(restoredSelect) == configuredSource;
    IOLockUnlock(ivars->registerLock);
    return sourceRestored ? kIOReturnSuccess : kIOReturnNotResponding;
}

kern_return_t
TimeCardDriver::GetCrossTimestamp(TimeCardCrossTimestamp *timestamp)
{
    if (timestamp == nullptr)
        return kIOReturnBadArgument;
    if ((ivars->registers.capabilities &
         kTimeCardCapabilityCrossTimestamp) == 0)
        return kIOReturnUnsupported;

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
    info->advertisedMSIXVectors = ivars->advertisedMSIXVectors;
    info->barSize = ivars->barSize;
    info->clockOffset = ivars->registers.clockOffset;
    info->todOffset = ivars->registers.todOffset;
    info->boardProfile = ivars->registers.boardProfile;
    info->capabilities = ivars->registers.capabilities;
    info->pciRevision = ivars->revisionID;

    IOLockLock(ivars->registerLock);
    if (!ReadRegister32(
            ivars->pciDevice, ivars->memoryIndex,
            ivars->registers.clockOffset + kTimeCardClockVersion,
            &info->clockVersion) || info->clockVersion == 0) {
        IOLockUnlock(ivars->registerLock);
        return kIOReturnNotResponding;
    }
    info->validFields |= kTimeCardInfoValidClockVersion;

    if (info->clockVersion >= kTimeCardClockVersionStatus &&
        ReadRegister32(
            ivars->pciDevice, ivars->memoryIndex,
            ivars->registers.clockOffset + kTimeCardClockStatus,
            &info->clockStatus)) {
        info->validFields |= kTimeCardInfoValidClockStatus;
    }
    if (ReadRegister32(
            ivars->pciDevice, ivars->memoryIndex,
            ivars->registers.clockOffset + kTimeCardClockSelect,
            &info->clockSelect)) {
        info->validFields |= kTimeCardInfoValidClockSelect;
    }
    if (TimeCardRegisterMapHasTOD(&ivars->registers)) {
        if (ReadRegister32(
                ivars->pciDevice, ivars->memoryIndex,
                ivars->registers.todOffset + kTimeCardTodVersion,
                &info->todVersion) && info->todVersion != 0) {
            info->validFields |= kTimeCardInfoValidTODVersion;
            if (info->todVersion >= kTimeCardTODVersionStatus &&
                ReadRegister32(
                    ivars->pciDevice, ivars->memoryIndex,
                    ivars->registers.todOffset + kTimeCardTodStatus,
                    &info->todStatus)) {
                info->validFields |= kTimeCardInfoValidTODStatus;
            }
        }
    }
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

    result = RegisterService();
    if (result != kIOReturnSuccess) {
        ivars->driver = nullptr;
        Stop(provider, SUPERDISPATCH);
        return result;
    }
    return kIOReturnSuccess;
}

kern_return_t
IMPL(TimeCardUserClient, Stop)
{
    ivars->driver = nullptr;
    return Stop(provider, SUPERDISPATCH);
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
