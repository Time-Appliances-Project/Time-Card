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

static uint32_t
ReadRegister32Raw(IOPCIDevice *device, uint8_t memoryIndex, uint64_t offset)
{
    uint32_t value = UINT32_MAX;
    device->MemoryRead32(memoryIndex, offset, &value);
    return value;
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

static uint64_t
TimeCardSMAInputRegisterOffset(const TimeCardRegisterMap *map,
                               uint32_t connector)
{
    return connector > 2u ? map->smaMap2Offset : map->smaMap1Offset;
}

static uint64_t
TimeCardSMAOutputRegisterOffset(const TimeCardRegisterMap *map,
                                uint32_t connector)
{
    return connector > 2u ? map->smaMap1Offset + 4u :
                            map->smaMap2Offset + 4u;
}

static bool
TimeCardSMAReadHalf(IOPCIDevice *device, uint8_t memoryIndex,
                    uint64_t offset, uint32_t connector, uint32_t *value)
{
    if (value == nullptr)
        return false;
    const uint32_t raw = ReadRegister32Raw(device, memoryIndex, offset);
    const uint32_t shift = (connector & 1u) != 0 ? 0u : 16u;
    *value = (raw >> shift) & 0xffffu;
    return true;
}

static bool
TimeCardSMAWriteHalf(IOPCIDevice *device, uint8_t memoryIndex,
                     uint64_t offset, uint32_t connector, uint32_t value)
{
    uint32_t raw = 0;
    if (!ReadRegister32(device, memoryIndex, offset, &raw))
        return false;
    const uint32_t shift = (connector & 1u) != 0 ? 0u : 16u;
    const uint32_t preserveMask =
        shift == 0u ? 0xffff0000u : 0x0000ffffu;
    raw = (raw & preserveMask) | ((value & 0xffffu) << shift);
    WriteRegister32(device, memoryIndex, offset, raw);
    return true;
}

static bool
TimeCardSMAInputFunctionValid(uint32_t function)
{
    switch (function) {
    case kTimeCardSMAInput10MHz:
    case kTimeCardSMAInputPPS1:
    case kTimeCardSMAInputPPS2:
    case kTimeCardSMAInputTS1:
    case kTimeCardSMAInputTS2:
    case kTimeCardSMAInputIRIG:
    case kTimeCardSMAInputDCF:
    case kTimeCardSMAInputTS3:
    case kTimeCardSMAInputTS4:
    case kTimeCardSMAInputFREQ1:
    case kTimeCardSMAInputFREQ2:
    case kTimeCardSMAInputFREQ3:
    case kTimeCardSMAInputFREQ4:
        return true;
    default:
        return false;
    }
}

static bool
TimeCardSMAOutputFunctionValid(uint32_t function)
{
    switch (function) {
    case kTimeCardSMAOutput10MHz:
    case kTimeCardSMAOutputPHC:
    case kTimeCardSMAOutputMAC:
    case kTimeCardSMAOutputGNSS1:
    case kTimeCardSMAOutputGNSS2:
    case kTimeCardSMAOutputIRIG:
    case kTimeCardSMAOutputDCF:
    case kTimeCardSMAOutputGEN1:
    case kTimeCardSMAOutputGEN2:
    case kTimeCardSMAOutputGEN3:
    case kTimeCardSMAOutputGEN4:
    case kTimeCardSMAOutputGND:
    case kTimeCardSMAOutputVCC:
        return true;
    default:
        return false;
    }
}

static uint32_t
TimeCardSMADefaultDirection(uint32_t connector)
{
    return connector <= 2u ? kTimeCardSMADirectionInput :
                             kTimeCardSMADirectionOutput;
}

static uint32_t
TimeCardSMADefaultFunction(uint32_t connector)
{
    return (connector & 1u) != 0 ? kTimeCardSMAInput10MHz :
                                   kTimeCardSMAInputPPS1;
}

static void
TimeCardSMAFixedQuery(uint32_t connector, TimeCardSMAControl *control)
{
    const uint32_t direction = TimeCardSMADefaultDirection(connector);
    const uint32_t function = TimeCardSMADefaultFunction(connector);

    *control = {};
    control->size = sizeof(*control);
    control->connector = connector;
    control->direction = direction;
    control->function = function;
    control->flags = kTimeCardSMAFlagPresent |
        kTimeCardSMAFlagFixedDirection | kTimeCardSMAFlagFixedFunction;
    if (direction == kTimeCardSMADirectionInput)
        control->inputMap = function;
    else
        control->outputMap = function;
}

static uint32_t
TimeCardARTSMADefaultFunction(uint32_t connector)
{
    static const uint32_t defaults[TIMECARD_SMA_COUNT] = {
        kTimeCardSMAInputTS2,
        kTimeCardSMAInputPPS1,
        kTimeCardSMAOutputIRIG,
        kTimeCardSMAOutputMAC,
    };
    return defaults[connector - 1u];
}

static uint32_t
TimeCardARTSMAFunctionDirection(uint32_t function)
{
    return function == kTimeCardSMAInputPPS1 ||
                   function == kTimeCardSMAInputTS2 ?
               kTimeCardSMADirectionInput :
               kTimeCardSMADirectionOutput;
}

static bool
TimeCardARTSMAFunctionValid(uint32_t direction, uint32_t function)
{
    if (direction == kTimeCardSMADirectionInput)
        return function == kTimeCardSMAInputPPS1 ||
            function == kTimeCardSMAInputTS2;
    if (direction == kTimeCardSMADirectionOutput)
        return function == kTimeCardSMAOutputMAC ||
            function == kTimeCardSMAOutputGNSS1 ||
            function == kTimeCardSMAOutputIRIG;
    return false;
}

static kern_return_t
TimeCardARTSMAQueryLocked(IOPCIDevice *device, uint8_t memoryIndex,
                          const TimeCardRegisterMap *map,
                          uint32_t connector, TimeCardSMAControl *control)
{
    uint32_t raw = 0;
    if (!ReadRegister32(device, memoryIndex,
                        map->artSMAOffset + (connector - 1u) * 4u, &raw))
        return kIOReturnNotResponding;

    uint32_t function = raw & 0xffu;
    const bool fixed = function == 0u;
    if (fixed)
        function = TimeCardARTSMADefaultFunction(connector);
    const uint32_t direction = TimeCardARTSMAFunctionDirection(function);

    *control = {};
    control->size = sizeof(*control);
    control->connector = connector;
    control->direction = direction;
    control->function = function;
    control->flags = kTimeCardSMAFlagPresent;
    if (fixed)
        control->flags |= kTimeCardSMAFlagFixedDirection;
    if (direction == kTimeCardSMADirectionInput)
        control->inputMap = function;
    else
        control->outputMap = function;
    control->reserved = raw;
    return kIOReturnSuccess;
}

static kern_return_t
TimeCardSMAQueryLocked(IOPCIDevice *device, uint8_t memoryIndex,
                       const TimeCardRegisterMap *map,
                       uint32_t connector, TimeCardSMAControl *control)
{
    const uint32_t map1Raw =
        ReadRegister32Raw(device, memoryIndex, map->smaMap1Offset);
    const uint32_t map2OutputRaw =
        ReadRegister32Raw(device, memoryIndex, map->smaMap2Offset + 4u);
    if (map1Raw == UINT32_MAX) {
        TimeCardSMAFixedQuery(connector, control);
        control->reserved = map1Raw;
        return kIOReturnSuccess;
    }

    uint32_t inputMap = 0;
    uint32_t outputMap = 0;
    if (!TimeCardSMAReadHalf(
            device, memoryIndex, TimeCardSMAInputRegisterOffset(map, connector),
            connector, &inputMap) ||
        !TimeCardSMAReadHalf(
            device, memoryIndex, TimeCardSMAOutputRegisterOffset(map, connector),
            connector, &outputMap)) {
        return kIOReturnNotResponding;
    }
    if (inputMap == UINT16_MAX || outputMap == UINT16_MAX) {
        TimeCardSMAFixedQuery(connector, control);
        control->inputMap = inputMap;
        control->outputMap = outputMap;
        control->reserved = map2OutputRaw;
        return kIOReturnSuccess;
    }

    const bool fixedDirection = map2OutputRaw == UINT32_MAX;
    const bool inputEnabled = (inputMap & kTimeCardSMAEnable) != 0;
    const bool outputEnabled = (outputMap & kTimeCardSMAEnable) != 0;
    uint32_t direction = kTimeCardSMADirectionDisabled;
    if (fixedDirection) {
        direction = connector <= 2u ? kTimeCardSMADirectionInput :
                                      kTimeCardSMADirectionOutput;
    } else if (connector <= 2u) {
        direction = inputEnabled ? kTimeCardSMADirectionInput :
                    outputEnabled ? kTimeCardSMADirectionOutput :
                                    kTimeCardSMADirectionDisabled;
    } else {
        direction = outputEnabled ? kTimeCardSMADirectionOutput :
                    inputEnabled ? kTimeCardSMADirectionInput :
                                   kTimeCardSMADirectionDisabled;
    }

    *control = {};
    control->size = sizeof(*control);
    control->connector = connector;
    control->direction = direction;
    control->inputMap = inputMap;
    control->outputMap = outputMap;
    control->flags = kTimeCardSMAFlagPresent;
    if (fixedDirection)
        control->flags |= kTimeCardSMAFlagFixedDirection;
    if (direction == kTimeCardSMADirectionDisabled)
        control->flags |= kTimeCardSMAFlagDisabled;
    if (direction == kTimeCardSMADirectionInput)
        control->function = inputMap & kTimeCardSMASelectMask;
    else if (direction == kTimeCardSMADirectionOutput)
        control->function = outputMap & kTimeCardSMASelectMask;
    control->reserved = map2OutputRaw;
    return kIOReturnSuccess;
}

kern_return_t
TimeCardDriver::QuerySMA(TimeCardSMAControl *control)
{
    if (control == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (control->size < sizeof(*control) || control->connector == 0 ||
        control->connector > TIMECARD_SMA_COUNT)
        return kIOReturnBadArgument;
    if ((ivars->registers.capabilities & kTimeCardCapabilitySMA) == 0 ||
        !TimeCardRegisterMapHasSMA(&ivars->registers))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    const kern_return_t result =
        ivars->registers.artSMAOffset != 0 ?
            TimeCardARTSMAQueryLocked(ivars->pciDevice, ivars->memoryIndex,
                                      &ivars->registers, control->connector,
                                      control) :
            TimeCardSMAQueryLocked(ivars->pciDevice, ivars->memoryIndex,
                                   &ivars->registers, control->connector,
                                   control);
    IOLockUnlock(ivars->registerLock);
    return result;
}

kern_return_t
TimeCardDriver::SetSMA(const TimeCardSMAControl *request,
                       TimeCardSMAControl *response)
{
    if (request == nullptr || response == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (request->size < sizeof(*request) || request->connector == 0 ||
        request->connector > TIMECARD_SMA_COUNT ||
        request->direction > kTimeCardSMADirectionDisabled)
        return kIOReturnBadArgument;
    if ((ivars->registers.capabilities & kTimeCardCapabilitySMA) == 0 ||
        !TimeCardRegisterMapHasSMA(&ivars->registers))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    kern_return_t result = kIOReturnSuccess;
    if (ivars->registers.artSMAOffset != 0) {
        uint32_t raw = 0;
        const uint64_t offset =
            ivars->registers.artSMAOffset + (request->connector - 1u) * 4u;
        if (!ReadRegister32(ivars->pciDevice, ivars->memoryIndex, offset,
                            &raw)) {
            result = kIOReturnNotResponding;
            goto done;
        }
        const uint32_t supported = raw >> 16;
        if ((raw & 0xffu) == 0 ||
            request->direction == kTimeCardSMADirectionDisabled) {
            result = kIOReturnUnsupported;
            goto done;
        }
        if (!TimeCardARTSMAFunctionValid(request->direction,
                                         request->function)) {
            result = kIOReturnBadArgument;
            goto done;
        }
        if ((supported & request->function) == 0) {
            result = kIOReturnUnsupported;
            goto done;
        }
        raw = (raw & 0xff00u) | (request->function & 0xffu);
        WriteRegister32(ivars->pciDevice, ivars->memoryIndex, offset, raw);
        result = TimeCardARTSMAQueryLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            request->connector, response);
    } else {
        if (request->direction == kTimeCardSMADirectionInput &&
            !TimeCardSMAInputFunctionValid(request->function)) {
            result = kIOReturnBadArgument;
            goto done;
        }
        if (request->direction == kTimeCardSMADirectionOutput &&
            !TimeCardSMAOutputFunctionValid(request->function)) {
            result = kIOReturnBadArgument;
            goto done;
        }

        result = TimeCardSMAQueryLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            request->connector, response);
        if (result != kIOReturnSuccess)
            goto done;

        const bool fixedDirection =
            (response->flags & kTimeCardSMAFlagFixedDirection) != 0;
        const bool fixedFunction =
            (response->flags & kTimeCardSMAFlagFixedFunction) != 0;
        const uint32_t fixedMode =
            TimeCardSMADefaultDirection(request->connector);
        if (fixedDirection && request->direction != fixedMode) {
            result = kIOReturnUnsupported;
            goto done;
        }
        if (fixedFunction && request->function != response->function) {
            result = kIOReturnUnsupported;
            goto done;
        }
        if (fixedFunction)
            goto done;

        const uint64_t inputOffset =
            TimeCardSMAInputRegisterOffset(&ivars->registers,
                                           request->connector);
        const uint64_t outputOffset =
            TimeCardSMAOutputRegisterOffset(&ivars->registers,
                                            request->connector);
        if (request->direction == kTimeCardSMADirectionDisabled) {
            if (!TimeCardSMAWriteHalf(ivars->pciDevice, ivars->memoryIndex,
                                      inputOffset, request->connector, 0) ||
                !TimeCardSMAWriteHalf(ivars->pciDevice, ivars->memoryIndex,
                                      outputOffset, request->connector, 0)) {
                result = kIOReturnNotResponding;
                goto done;
            }
        } else if (request->direction == kTimeCardSMADirectionInput) {
            if (!fixedDirection &&
                response->direction == kTimeCardSMADirectionOutput &&
                !TimeCardSMAWriteHalf(ivars->pciDevice, ivars->memoryIndex,
                                      outputOffset, request->connector, 0)) {
                result = kIOReturnNotResponding;
                goto done;
            }
            uint32_t value = request->function;
            if (!fixedDirection)
                value |= kTimeCardSMAEnable;
            if (!TimeCardSMAWriteHalf(ivars->pciDevice, ivars->memoryIndex,
                                      inputOffset, request->connector, value)) {
                result = kIOReturnNotResponding;
                goto done;
            }
        } else {
            if (!fixedDirection &&
                response->direction == kTimeCardSMADirectionInput &&
                !TimeCardSMAWriteHalf(ivars->pciDevice, ivars->memoryIndex,
                                      inputOffset, request->connector, 0)) {
                result = kIOReturnNotResponding;
                goto done;
            }
            uint32_t value = request->function;
            if (!fixedDirection)
                value |= kTimeCardSMAEnable;
            if (!TimeCardSMAWriteHalf(ivars->pciDevice, ivars->memoryIndex,
                                      outputOffset, request->connector, value)) {
                result = kIOReturnNotResponding;
                goto done;
            }
        }

        result = TimeCardSMAQueryLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            request->connector, response);
    }

    if (result == kIOReturnSuccess &&
        (response->direction != request->direction ||
         (request->direction != kTimeCardSMADirectionDisabled &&
          response->function != request->function))) {
        result = kIOReturnBadMedia;
    }

done:
    IOLockUnlock(ivars->registerLock);
    return result;
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

static kern_return_t
SMAQueryAction(OSObject *, void *reference,
               IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    if (arguments->structureInput == nullptr)
        return kIOReturnBadArgument;
    auto *request = static_cast<const TimeCardSMAControl *>(
        arguments->structureInput->getBytesNoCopy());
    TimeCardSMAControl response = *request;
    const kern_return_t result = driver->QuerySMA(&response);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&response, sizeof(response));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
SMASetAction(OSObject *, void *reference,
             IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    if (arguments->structureInput == nullptr)
        return kIOReturnBadArgument;
    auto *request = static_cast<const TimeCardSMAControl *>(
        arguments->structureInput->getBytesNoCopy());
    TimeCardSMAControl response = {};
    const kern_return_t result = driver->SetSMA(request, &response);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&response, sizeof(response));
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
    {SMAQueryAction, false, 0, sizeof(TimeCardSMAControl), 0,
     sizeof(TimeCardSMAControl)},
    {SMASetAction, false, 0, sizeof(TimeCardSMAControl), 0,
     sizeof(TimeCardSMAControl)},
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
