/* SPDX-License-Identifier: BSD-3-Clause */

#include <DriverKit/IOLib.h>
#include <DriverKit/OSData.h>
#include <DriverKit/IOUserServer.h>
#include <PCIDriverKit/IOPCIDevice.h>
#include <PCIDriverKit/IOPCIFamilyDefinitions.h>
#include <os/log.h>
#include <string.h>
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
    uint32_t i2cLEDAddress;
    bool i2cMuxPresent;
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

static uint8_t
ReadRegister8Raw(IOPCIDevice *device, uint8_t memoryIndex, uint64_t offset)
{
    uint8_t value = UINT8_MAX;
    device->MemoryRead8(memoryIndex, offset, &value);
    return value;
}

static void
WriteRegister8(IOPCIDevice *device, uint8_t memoryIndex, uint64_t offset,
               uint8_t value)
{
    device->MemoryWrite8(memoryIndex, offset, value);
}

static void
WriteRegister16(IOPCIDevice *device, uint8_t memoryIndex, uint64_t offset,
                uint16_t value)
{
    device->MemoryWrite16(memoryIndex, offset, value);
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

enum {
    kXIICDGIEROffset = 0x01cu,
    kXIICIISROffset = 0x020u,
    kXIICIIEROffset = 0x028u,
    kXIICResetOffset = 0x040u,
    kXIICControlOffset = 0x100u,
    kXIICStatusOffset = 0x104u,
    kXIICTransmitFIFOOffset = 0x108u,
    kXIICReceiveFIFOOffset = 0x10cu,
    kXIICTxOccupancyOffset = 0x114u,
    kXIICRxOccupancyOffset = 0x118u,
    kXIICRxDepthOffset = 0x120u,

    kXIICResetValue = 0x0000000au,
    kXIICControlEnable = 0x01u,
    kXIICControlTxFIFOReset = 0x02u,
    kXIICStatusBusBusy = 0x04u,
    kXIICStatusRxEmpty = 0x40u,
    kXIICStatusTxEmpty = 0x80u,
    kXIICInterruptArbLost = 0x01u,
    kXIICInterruptTxError = 0x02u,
    kXIICInterruptTxEmpty = 0x04u,
    kXIICInterruptRxFull = 0x08u,
    kXIICInterruptBusNotBusy = 0x10u,
    kXIICDynamicStart = 0x0100u,
    kXIICDynamicStop = 0x0200u,
    kXIICFIFODepth = 16u,

    kTimeCardI2CAddressMin = 0x08u,
    kTimeCardI2CAddressMax = 0x77u,
    kTimeCardI2CPollDelayUS = 5u,
    kTimeCardI2CDefaultPolls = 100u * 1000u / kTimeCardI2CPollDelayUS,

    kTimeCardSensorLM75BAddress1 = 0x48u,
    kTimeCardSensorLM75BAddress2 = 0x49u,
    kTimeCardSensorLM75BAddress3 = 0x4au,
    kTimeCardSensorSHT3xAddress = 0x44u,
    kTimeCardSensorICP10100Address = 0x63u,
    kTimeCardSensorBNO08xAddress = 0x4au,
    kTimeCardSHT3xStatusCommandHi = 0xf3u,
    kTimeCardSHT3xStatusCommandLo = 0x2du,
    kTimeCardSHT3xMeasureCommandHi = 0x24u,
    kTimeCardSHT3xMeasureCommandLo = 0x00u,
    kTimeCardICP10100ProductID = 0x08u,
    kTimeCardICP10100ReadIDHi = 0xefu,
    kTimeCardICP10100ReadIDLo = 0xc8u,
    kTimeCardICP10100MeasureHi = 0x50u,
    kTimeCardICP10100MeasureLo = 0x59u,

    kIS32FL3207DeviceControl = 0x00u,
    kIS32FL3207PWMBase = 0x01u,
    kIS32FL3207Update = 0x49u,
    kIS32FL3207ControlBase = 0x4au,
    kIS32FL3207GlobalCurrent = 0x6eu,
    kIS32FL3207SpreadSpectrum = 0x78u
};

typedef struct TimeCardI2CInternalTransfer {
    uint32_t controllerStatus;
    uint32_t interruptStatus;
    uint8_t data[255];
    uint32_t length;
} TimeCardI2CInternalTransfer;

static bool
TimeCardI2CAddressValid(uint32_t address)
{
    return address >= kTimeCardI2CAddressMin &&
        address <= kTimeCardI2CAddressMax;
}

static uint64_t
TimeCardI2COffset(const TimeCardRegisterMap *map, uint32_t offset)
{
    return map->i2cOffset + offset;
}

static uint8_t
TimeCardI2CRead8(IOPCIDevice *device, uint8_t memoryIndex,
                 const TimeCardRegisterMap *map, uint32_t offset)
{
    return ReadRegister8Raw(device, memoryIndex,
                            TimeCardI2COffset(map, offset));
}

static uint32_t
TimeCardI2CRead32(IOPCIDevice *device, uint8_t memoryIndex,
                  const TimeCardRegisterMap *map, uint32_t offset)
{
    return ReadRegister32Raw(device, memoryIndex,
                             TimeCardI2COffset(map, offset));
}

static void
TimeCardI2CWrite8(IOPCIDevice *device, uint8_t memoryIndex,
                  const TimeCardRegisterMap *map, uint32_t offset,
                  uint8_t value)
{
    WriteRegister8(device, memoryIndex, TimeCardI2COffset(map, offset), value);
}

static void
TimeCardI2CWrite16(IOPCIDevice *device, uint8_t memoryIndex,
                   const TimeCardRegisterMap *map, uint32_t offset,
                   uint16_t value)
{
    WriteRegister16(device, memoryIndex, TimeCardI2COffset(map, offset),
                    value);
}

static void
TimeCardI2CWrite32(IOPCIDevice *device, uint8_t memoryIndex,
                   const TimeCardRegisterMap *map, uint32_t offset,
                   uint32_t value)
{
    WriteRegister32(device, memoryIndex, TimeCardI2COffset(map, offset),
                    value);
}

static void
TimeCardI2CClearInterruptMask(IOPCIDevice *device, uint8_t memoryIndex,
                              const TimeCardRegisterMap *map, uint32_t mask)
{
    const uint32_t pending =
        TimeCardI2CRead32(device, memoryIndex, map, kXIICIISROffset) & mask;
    if (pending != 0)
        TimeCardI2CWrite32(device, memoryIndex, map, kXIICIISROffset, pending);
}

static void
TimeCardI2CClearInterrupts(IOPCIDevice *device, uint8_t memoryIndex,
                           const TimeCardRegisterMap *map)
{
    const uint32_t pending =
        TimeCardI2CRead32(device, memoryIndex, map, kXIICIISROffset);
    if (pending != 0)
        TimeCardI2CWrite32(device, memoryIndex, map, kXIICIISROffset, pending);
}

static void
TimeCardI2CArmPolling(IOPCIDevice *device, uint8_t memoryIndex,
                      const TimeCardRegisterMap *map)
{
    TimeCardI2CClearInterrupts(device, memoryIndex, map);
    TimeCardI2CWrite32(device, memoryIndex, map, kXIICIIEROffset,
                       kXIICInterruptArbLost | kXIICInterruptTxError |
                           kXIICInterruptTxEmpty | kXIICInterruptRxFull |
                           kXIICInterruptBusNotBusy);
}

static kern_return_t
TimeCardI2CReset(IOPCIDevice *device, uint8_t memoryIndex,
                 const TimeCardRegisterMap *map)
{
    TimeCardI2CWrite32(device, memoryIndex, map, kXIICDGIEROffset, 0);
    TimeCardI2CWrite32(device, memoryIndex, map, kXIICIIEROffset, 0);
    TimeCardI2CWrite32(device, memoryIndex, map, kXIICResetOffset,
                       kXIICResetValue);
    TimeCardI2CWrite8(device, memoryIndex, map, kXIICRxDepthOffset,
                      (uint8_t)(kXIICFIFODepth - 1u));
    TimeCardI2CWrite8(device, memoryIndex, map, kXIICControlOffset,
                      kXIICControlTxFIFOReset);
    TimeCardI2CWrite8(device, memoryIndex, map, kXIICControlOffset,
                      kXIICControlEnable);
    for (uint32_t i = 0; i < 255u; ++i) {
        if ((TimeCardI2CRead8(device, memoryIndex, map,
                              kXIICStatusOffset) &
             kXIICStatusRxEmpty) != 0) {
            TimeCardI2CClearInterrupts(device, memoryIndex, map);
            return kIOReturnSuccess;
        }
        (void)TimeCardI2CRead8(device, memoryIndex, map,
                               kXIICReceiveFIFOOffset);
    }
    return kIOReturnIOError;
}

static kern_return_t
TimeCardI2CWaitNotBusy(IOPCIDevice *device, uint8_t memoryIndex,
                       const TimeCardRegisterMap *map)
{
    for (uint32_t i = 0; i < 600u; ++i) {
        if ((TimeCardI2CRead8(device, memoryIndex, map,
                              kXIICStatusOffset) &
             kXIICStatusBusBusy) == 0)
            return kIOReturnSuccess;
        IODelay(kTimeCardI2CPollDelayUS);
    }
    return kIOReturnBusy;
}

static kern_return_t
TimeCardI2CPrepareTransfer(IOPCIDevice *device, uint8_t memoryIndex,
                           const TimeCardRegisterMap *map)
{
    kern_return_t result = TimeCardI2CWaitNotBusy(device, memoryIndex, map);
    if (result != kIOReturnSuccess) {
        result = TimeCardI2CReset(device, memoryIndex, map);
        if (result != kIOReturnSuccess)
            return result;
        result = TimeCardI2CWaitNotBusy(device, memoryIndex, map);
        if (result != kIOReturnSuccess)
            return result;
    }
    return TimeCardI2CReset(device, memoryIndex, map);
}

static void
TimeCardI2CQueueDynamicStart(IOPCIDevice *device, uint8_t memoryIndex,
                             const TimeCardRegisterMap *map,
                             uint16_t addressWord)
{
    TimeCardI2CWrite16(device, memoryIndex, map, kXIICTransmitFIFOOffset,
                       (uint16_t)(kXIICDynamicStart | addressWord));
}

static kern_return_t
TimeCardI2CWaitTransmitPhase(IOPCIDevice *device, uint8_t memoryIndex,
                             const TimeCardRegisterMap *map,
                             uint32_t *remainingPolls,
                             uint32_t *observedInterrupts,
                             uint8_t *controllerStatus)
{
    while (*remainingPolls != 0) {
        const uint32_t interrupts =
            TimeCardI2CRead32(device, memoryIndex, map, kXIICIISROffset);
        --(*remainingPolls);
        *observedInterrupts |= interrupts;
        *controllerStatus =
            TimeCardI2CRead8(device, memoryIndex, map, kXIICStatusOffset);
        if ((interrupts & kXIICInterruptArbLost) != 0)
            return kIOReturnIOError;
        if ((interrupts & kXIICInterruptTxError) != 0)
            return kIOReturnNoDevice;
        if ((interrupts & kXIICInterruptBusNotBusy) != 0)
            return kIOReturnIOError;
        if ((interrupts & kXIICInterruptTxEmpty) != 0) {
            TimeCardI2CClearInterruptMask(device, memoryIndex, map,
                                          kXIICInterruptTxEmpty);
            return kIOReturnSuccess;
        }
        IODelay(kTimeCardI2CPollDelayUS);
    }
    return kIOReturnTimeout;
}

static kern_return_t
TimeCardI2CWriteAttempt(IOPCIDevice *device, uint8_t memoryIndex,
                        const TimeCardRegisterMap *map, uint32_t address,
                        const uint8_t *data, uint32_t length,
                        uint32_t *controllerStatus,
                        uint32_t *interruptStatus)
{
    uint32_t remainingPolls = kTimeCardI2CDefaultPolls;
    uint32_t observedInterrupts = 0;
    uint8_t currentStatus = 0;
    uint32_t i = 0;
    kern_return_t result =
        TimeCardI2CPrepareTransfer(device, memoryIndex, map);
    if (result != kIOReturnSuccess)
        goto done;

    TimeCardI2CArmPolling(device, memoryIndex, map);
    TimeCardI2CQueueDynamicStart(device, memoryIndex, map,
                                 (uint16_t)(address << 1));
    TimeCardI2CClearInterruptMask(device, memoryIndex, map,
                                  kXIICInterruptTxEmpty |
                                      kXIICInterruptTxError |
                                      kXIICInterruptBusNotBusy);
    while (i < length) {
        uint32_t capacity = i == 0 ? kXIICFIFODepth - 1u :
                                     kXIICFIFODepth;
        uint32_t chunk = length - i;
        if (chunk > capacity)
            chunk = capacity;
        while (chunk-- != 0) {
            uint16_t value = data[i];
            if (i + 1u == length)
                value |= kXIICDynamicStop;
            TimeCardI2CWrite16(device, memoryIndex, map,
                               kXIICTransmitFIFOOffset, value);
            ++i;
        }
        if (i < length) {
            result = TimeCardI2CWaitTransmitPhase(
                device, memoryIndex, map, &remainingPolls,
                &observedInterrupts, &currentStatus);
            if (result != kIOReturnSuccess)
                goto done;
        }
    }

    result = kIOReturnTimeout;
    while (remainingPolls != 0) {
        const uint32_t interrupts =
            TimeCardI2CRead32(device, memoryIndex, map, kXIICIISROffset);
        --remainingPolls;
        observedInterrupts |= interrupts;
        currentStatus =
            TimeCardI2CRead8(device, memoryIndex, map, kXIICStatusOffset);
        if ((interrupts & kXIICInterruptArbLost) != 0) {
            result = kIOReturnIOError;
            break;
        }
        if ((interrupts & kXIICInterruptTxError) != 0) {
            result = kIOReturnNoDevice;
            break;
        }
        if ((interrupts & kXIICInterruptBusNotBusy) != 0) {
            result = kIOReturnSuccess;
            break;
        }
        IODelay(kTimeCardI2CPollDelayUS);
    }

done:
    *controllerStatus = currentStatus;
    *interruptStatus = observedInterrupts;
    (void)TimeCardI2CReset(device, memoryIndex, map);
    return result;
}

static kern_return_t
TimeCardI2CWriteLocked(IOPCIDevice *device, uint8_t memoryIndex,
                       const TimeCardRegisterMap *map, uint32_t address,
                       const uint8_t *data, uint32_t length,
                       uint32_t *controllerStatus,
                       uint32_t *interruptStatus)
{
    if (!TimeCardI2CAddressValid(address) || data == nullptr ||
        length == 0 || length > sizeof(TimeCardI2CInternalTransfer::data))
        return kIOReturnBadArgument;
    *controllerStatus = 0;
    *interruptStatus = 0;
    kern_return_t result = kIOReturnTimeout;
    for (uint32_t attempt = 0; attempt < 2u; ++attempt) {
        result = TimeCardI2CWriteAttempt(
            device, memoryIndex, map, address, data, length,
            controllerStatus, interruptStatus);
        if (result == kIOReturnSuccess || result == kIOReturnNoDevice)
            break;
    }
    return result;
}

static void
TimeCardI2CSetReceiveWatermark(IOPCIDevice *device, uint8_t memoryIndex,
                               const TimeCardRegisterMap *map,
                               uint32_t remaining)
{
    uint32_t bytes =
        remaining < kXIICFIFODepth ? remaining : kXIICFIFODepth;
    if (bytes != 0)
        TimeCardI2CWrite8(device, memoryIndex, map, kXIICRxDepthOffset,
                          (uint8_t)(bytes - 1u));
}

static void
TimeCardI2CDrainReceiveFIFO(IOPCIDevice *device, uint8_t memoryIndex,
                            const TimeCardRegisterMap *map,
                            TimeCardI2CInternalTransfer *transfer,
                            uint32_t requestedLength)
{
    uint32_t remaining = requestedLength - transfer->length;
    if ((TimeCardI2CRead8(device, memoryIndex, map, kXIICStatusOffset) &
         kXIICStatusRxEmpty) != 0)
        return;
    uint32_t available =
        (uint32_t)TimeCardI2CRead8(device, memoryIndex, map,
                                   kXIICRxOccupancyOffset) +
        1u;
    if (available > remaining)
        available = remaining;
    for (uint32_t i = 0; i < available; ++i) {
        transfer->data[transfer->length++] =
            TimeCardI2CRead8(device, memoryIndex, map,
                             kXIICReceiveFIFOOffset);
    }
    remaining = requestedLength - transfer->length;
    if (remaining != 0)
        TimeCardI2CSetReceiveWatermark(device, memoryIndex, map, remaining);
}

static kern_return_t
TimeCardI2CReadAttempt(IOPCIDevice *device, uint8_t memoryIndex,
                       const TimeCardRegisterMap *map, uint32_t address,
                       uint32_t subaddressLength, uint32_t subaddress,
                       TimeCardI2CInternalTransfer *transfer, uint32_t length)
{
    uint32_t remainingPolls = kTimeCardI2CDefaultPolls;
    uint32_t observedInterrupts = 0;
    uint8_t controllerStatus = 0;
    kern_return_t result =
        TimeCardI2CPrepareTransfer(device, memoryIndex, map);
    if (result != kIOReturnSuccess)
        goto done;

    TimeCardI2CArmPolling(device, memoryIndex, map);
    if (subaddressLength != 0) {
        TimeCardI2CQueueDynamicStart(device, memoryIndex, map,
                                     (uint16_t)(address << 1));
        TimeCardI2CClearInterruptMask(device, memoryIndex, map,
                                      kXIICInterruptTxEmpty |
                                          kXIICInterruptTxError |
                                          kXIICInterruptBusNotBusy);
        if (subaddressLength == 2u) {
            TimeCardI2CWrite16(device, memoryIndex, map,
                               kXIICTransmitFIFOOffset,
                               (uint16_t)((subaddress >> 8) & 0xffu));
        }
        TimeCardI2CWrite16(device, memoryIndex, map,
                           kXIICTransmitFIFOOffset,
                           (uint16_t)(subaddress & 0xffu));
        result = TimeCardI2CWaitTransmitPhase(
            device, memoryIndex, map, &remainingPolls, &observedInterrupts,
            &controllerStatus);
        if (result != kIOReturnSuccess)
            goto done;
    }

    TimeCardI2CSetReceiveWatermark(device, memoryIndex, map, length);
    TimeCardI2CClearInterruptMask(device, memoryIndex, map,
                                  kXIICInterruptArbLost |
                                      kXIICInterruptTxError |
                                      kXIICInterruptTxEmpty |
                                      kXIICInterruptRxFull);
    TimeCardI2CQueueDynamicStart(device, memoryIndex, map,
                                 (uint16_t)((address << 1) | 1u));
    TimeCardI2CWrite16(device, memoryIndex, map, kXIICTransmitFIFOOffset,
                       (uint16_t)(kXIICDynamicStop | length));
    TimeCardI2CClearInterruptMask(device, memoryIndex, map,
                                  kXIICInterruptBusNotBusy);

    result = kIOReturnTimeout;
    while (remainingPolls != 0) {
        const uint32_t interrupts =
            TimeCardI2CRead32(device, memoryIndex, map, kXIICIISROffset);
        observedInterrupts |= interrupts;
        controllerStatus =
            TimeCardI2CRead8(device, memoryIndex, map, kXIICStatusOffset);
        --remainingPolls;
        if ((interrupts & kXIICInterruptArbLost) != 0) {
            result = kIOReturnIOError;
            break;
        }
        if ((controllerStatus & kXIICStatusRxEmpty) == 0) {
            TimeCardI2CDrainReceiveFIFO(device, memoryIndex, map, transfer,
                                        length);
            if ((interrupts & kXIICInterruptRxFull) != 0) {
                TimeCardI2CClearInterruptMask(
                    device, memoryIndex, map,
                    kXIICInterruptRxFull | kXIICInterruptTxError);
            }
            controllerStatus =
                TimeCardI2CRead8(device, memoryIndex, map,
                                 kXIICStatusOffset);
        }
        if (transfer->length == length) {
            TimeCardI2CClearInterruptMask(device, memoryIndex, map,
                                          kXIICInterruptTxError);
            if ((interrupts & kXIICInterruptBusNotBusy) != 0 ||
                (controllerStatus & kXIICStatusBusBusy) == 0) {
                result = kIOReturnSuccess;
                break;
            }
            IODelay(kTimeCardI2CPollDelayUS);
            continue;
        }
        if ((interrupts & kXIICInterruptTxError) != 0 &&
            (interrupts & kXIICInterruptRxFull) == 0) {
            result = transfer->length == 0 ? kIOReturnNoDevice :
                                             kIOReturnIOError;
            break;
        }
        if ((interrupts & kXIICInterruptBusNotBusy) != 0 &&
            transfer->length < length) {
            result = kIOReturnIOError;
            break;
        }
        IODelay(kTimeCardI2CPollDelayUS);
    }

done:
    transfer->controllerStatus = controllerStatus;
    transfer->interruptStatus = observedInterrupts;
    (void)TimeCardI2CReset(device, memoryIndex, map);
    return result;
}

static kern_return_t
TimeCardI2CReadLocked(IOPCIDevice *device, uint8_t memoryIndex,
                      const TimeCardRegisterMap *map, uint32_t address,
                      uint32_t subaddressLength, uint32_t subaddress,
                      uint8_t *data, uint32_t length,
                      uint32_t *controllerStatus,
                      uint32_t *interruptStatus)
{
    if (!TimeCardI2CAddressValid(address) || subaddressLength > 2u ||
        data == nullptr || length == 0 ||
        length > sizeof(TimeCardI2CInternalTransfer::data))
        return kIOReturnBadArgument;

    kern_return_t result = kIOReturnTimeout;
    TimeCardI2CInternalTransfer transfer = {};
    for (uint32_t attempt = 0; attempt < 2u; ++attempt) {
        transfer.length = 0;
        transfer.controllerStatus = 0;
        transfer.interruptStatus = 0;
        result = TimeCardI2CReadAttempt(
            device, memoryIndex, map, address, subaddressLength, subaddress,
            &transfer, length);
        if (result == kIOReturnSuccess || result == kIOReturnNoDevice)
            break;
    }
    if (result == kIOReturnNoDevice && subaddressLength != 0) {
        uint8_t pointer[2] = {
            (uint8_t)((subaddress >> 8) & 0xffu),
            (uint8_t)(subaddress & 0xffu),
        };
        uint32_t localControllerStatus = 0;
        uint32_t localInterruptStatus = 0;
        result = TimeCardI2CWriteLocked(
            device, memoryIndex, map, address,
            subaddressLength == 2u ? pointer : &pointer[1],
            subaddressLength, &localControllerStatus, &localInterruptStatus);
        if (result == kIOReturnSuccess) {
            transfer.length = 0;
            transfer.controllerStatus = 0;
            transfer.interruptStatus = 0;
            result = TimeCardI2CReadAttempt(
                device, memoryIndex, map, address, 0, 0, &transfer, length);
        }
    }
    *controllerStatus = transfer.controllerStatus;
    *interruptStatus = transfer.interruptStatus;
    if (result == kIOReturnSuccess)
        memcpy(data, transfer.data, length);
    return result;
}

static kern_return_t
TimeCardI2CMuxReadLocked(IOPCIDevice *device, uint8_t memoryIndex,
                         const TimeCardRegisterMap *map,
                         uint8_t *channelMask, uint32_t *controllerStatus,
                         uint32_t *interruptStatus)
{
    uint8_t value = 0;
    const kern_return_t result = TimeCardI2CReadLocked(
        device, memoryIndex, map, kTimeCardI2CMuxAddress, 0, 0, &value, 1,
        controllerStatus, interruptStatus);
    if (result == kIOReturnSuccess)
        *channelMask = value & kTimeCardI2CMuxChannelMask;
    return result;
}

static kern_return_t
TimeCardI2CMuxWriteLocked(IOPCIDevice *device, uint8_t memoryIndex,
                          const TimeCardRegisterMap *map,
                          uint8_t channelMask, uint32_t *controllerStatus,
                          uint32_t *interruptStatus)
{
    const uint8_t value = channelMask & kTimeCardI2CMuxChannelMask;
    return TimeCardI2CWriteLocked(
        device, memoryIndex, map, kTimeCardI2CMuxAddress, &value, 1,
        controllerStatus, interruptStatus);
}

static kern_return_t
TimeCardI2CProbeAttempt(IOPCIDevice *device, uint8_t memoryIndex,
                        const TimeCardRegisterMap *map, uint32_t address,
                        uint32_t *controllerStatus,
                        uint32_t *interruptStatus)
{
    uint32_t remainingPolls = kTimeCardI2CDefaultPolls;
    uint32_t observedInterrupts = 0;
    uint8_t currentStatus = 0;
    kern_return_t result =
        TimeCardI2CPrepareTransfer(device, memoryIndex, map);
    if (result != kIOReturnSuccess)
        goto done;

    TimeCardI2CArmPolling(device, memoryIndex, map);
    TimeCardI2CQueueDynamicStart(
        device, memoryIndex, map,
        (uint16_t)(kXIICDynamicStop | (address << 1)));
    TimeCardI2CClearInterruptMask(device, memoryIndex, map,
                                  kXIICInterruptTxError |
                                      kXIICInterruptBusNotBusy);

    result = kIOReturnTimeout;
    while (remainingPolls != 0) {
        const uint32_t interrupts =
            TimeCardI2CRead32(device, memoryIndex, map, kXIICIISROffset);
        --remainingPolls;
        observedInterrupts |= interrupts;
        currentStatus =
            TimeCardI2CRead8(device, memoryIndex, map, kXIICStatusOffset);
        if ((interrupts & kXIICInterruptArbLost) != 0) {
            result = kIOReturnIOError;
            break;
        }
        if ((interrupts & kXIICInterruptTxError) != 0) {
            result = kIOReturnNoDevice;
            break;
        }
        if ((interrupts & kXIICInterruptBusNotBusy) != 0) {
            result = kIOReturnSuccess;
            break;
        }
        IODelay(kTimeCardI2CPollDelayUS);
    }

done:
    *controllerStatus = currentStatus;
    *interruptStatus = observedInterrupts;
    (void)TimeCardI2CReset(device, memoryIndex, map);
    return result;
}

static kern_return_t
TimeCardI2CProbeLocked(IOPCIDevice *device, uint8_t memoryIndex,
                       const TimeCardRegisterMap *map, uint32_t address,
                       uint32_t *controllerStatus,
                       uint32_t *interruptStatus)
{
    if (!TimeCardI2CAddressValid(address))
        return kIOReturnBadArgument;
    *controllerStatus = 0;
    *interruptStatus = 0;
    kern_return_t result = kIOReturnTimeout;
    for (uint32_t attempt = 0; attempt < 2u; ++attempt) {
        result = TimeCardI2CProbeAttempt(
            device, memoryIndex, map, address, controllerStatus,
            interruptStatus);
        if (result == kIOReturnSuccess || result == kIOReturnNoDevice)
            break;
    }
    return result;
}

static bool
TimeCardDriverHasI2C(const TimeCardRegisterMap *map)
{
    return map != nullptr &&
        (map->capabilities & kTimeCardCapabilityI2C) != 0 &&
        TimeCardRegisterMapHasI2C(map);
}

static bool
TimeCardDriverHasSensors(const TimeCardRegisterMap *map)
{
    return map != nullptr &&
        (map->capabilities & kTimeCardCapabilitySensors) != 0 &&
        TimeCardRegisterMapHasI2C(map);
}

static uint16_t
TimeCardSensorReadBE16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static int16_t
TimeCardSensorReadBESigned16(const uint8_t *data)
{
    return (int16_t)TimeCardSensorReadBE16(data);
}

static uint8_t
TimeCardSensorCrc8(const uint8_t *data, uint32_t length)
{
    uint8_t crc = 0xffu;
    for (uint32_t byteIndex = 0; byteIndex < length; ++byteIndex) {
        crc ^= data[byteIndex];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x80u) != 0 ?
                (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static bool
TimeCardSensorFrameCrcValid(const uint8_t *frame)
{
    return TimeCardSensorCrc8(frame, 2u) == frame[2];
}

static TimeCardSensorReading *
TimeCardSensorAppend(TimeCardSensorTelemetry *telemetry, uint32_t type,
                     uint32_t muxChannelMask, uint32_t address)
{
    if (telemetry->readingCount >= TIMECARD_SENSOR_MAX_READINGS)
        return nullptr;
    TimeCardSensorReading *reading =
        &telemetry->readings[telemetry->readingCount++];
    memset(reading, 0, sizeof(*reading));
    reading->size = sizeof(*reading);
    reading->type = type;
    reading->muxChannelMask = muxChannelMask;
    reading->address = address;
    return reading;
}

static bool
TimeCardSensorSelectBranchLocked(IOPCIDevice *device, uint8_t memoryIndex,
                                 const TimeCardRegisterMap *map,
                                 uint8_t channelMask,
                                 uint32_t *controllerStatus,
                                 uint32_t *interruptStatus)
{
    kern_return_t result = TimeCardI2CMuxWriteLocked(
        device, memoryIndex, map, channelMask, controllerStatus,
        interruptStatus);
    if (result != kIOReturnSuccess)
        return false;
    IODelay(10u);
    return true;
}

static bool
TimeCardLM75BReadLocked(IOPCIDevice *device, uint8_t memoryIndex,
                        const TimeCardRegisterMap *map,
                        TimeCardSensorReading *reading,
                        uint32_t *controllerStatus,
                        uint32_t *interruptStatus)
{
    uint8_t data[2] = {};
    kern_return_t result = TimeCardI2CReadLocked(
        device, memoryIndex, map, reading->address, 1u, 0x00u, data,
        sizeof(data), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess)
        return false;

    const int16_t raw = TimeCardSensorReadBESigned16(data);
    const int32_t temperature = ((int32_t)raw * 1000) / 256;
    reading->flags = kTimeCardSensorFlagPresent;
    reading->raw0 = (uint32_t)(uint16_t)raw;
    reading->temperatureMilliCelsius = temperature;
    if (temperature < -55000 || temperature > 125000)
        return false;
    reading->flags |= kTimeCardSensorFlagValid |
        kTimeCardSensorFlagConversionReady | kTimeCardSensorFlagTemperature;
    return true;
}

static bool
TimeCardSHT3xReadLocked(IOPCIDevice *device, uint8_t memoryIndex,
                        const TimeCardRegisterMap *map,
                        TimeCardSensorReading *reading,
                        uint32_t *controllerStatus,
                        uint32_t *interruptStatus)
{
    const uint8_t statusCommand[2] = {
        kTimeCardSHT3xStatusCommandHi, kTimeCardSHT3xStatusCommandLo
    };
    const uint8_t measureCommand[2] = {
        kTimeCardSHT3xMeasureCommandHi, kTimeCardSHT3xMeasureCommandLo
    };
    uint8_t statusFrame[3] = {};
    uint8_t data[6] = {};
    kern_return_t result = TimeCardI2CWriteLocked(
        device, memoryIndex, map, reading->address, statusCommand,
        sizeof(statusCommand), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess)
        return false;
    reading->flags = kTimeCardSensorFlagPresent;
    IODelay(1000u);
    result = TimeCardI2CReadLocked(
        device, memoryIndex, map, reading->address, 0u, 0u, statusFrame,
        sizeof(statusFrame), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess ||
        !TimeCardSensorFrameCrcValid(statusFrame))
        return false;
    reading->raw2 = TimeCardSensorReadBE16(statusFrame);

    result = TimeCardI2CWriteLocked(
        device, memoryIndex, map, reading->address, measureCommand,
        sizeof(measureCommand), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess)
        return false;
    IODelay(20000u);
    result = TimeCardI2CReadLocked(
        device, memoryIndex, map, reading->address, 0u, 0u, data,
        sizeof(data), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess ||
        !TimeCardSensorFrameCrcValid(&data[0]) ||
        !TimeCardSensorFrameCrcValid(&data[3]))
        return false;

    const uint32_t rawTemperature = TimeCardSensorReadBE16(data);
    const uint32_t rawHumidity = TimeCardSensorReadBE16(&data[3]);
    reading->raw0 = rawTemperature;
    reading->raw1 = rawHumidity;
    reading->temperatureMilliCelsius =
        -45000 + (int32_t)((175000ull * rawTemperature) / 65535ull);
    reading->humidityMilliPercent =
        (uint32_t)((100000ull * rawHumidity) / 65535ull);
    reading->flags |= kTimeCardSensorFlagValid |
        kTimeCardSensorFlagConfigured | kTimeCardSensorFlagConversionReady |
        kTimeCardSensorFlagHumidity | kTimeCardSensorFlagTemperature |
        kTimeCardSensorFlagCRCValid;
    return true;
}

static bool
TimeCardICP10100ReadLocked(IOPCIDevice *device, uint8_t memoryIndex,
                           const TimeCardRegisterMap *map,
                           TimeCardSensorReading *reading,
                           uint32_t *controllerStatus,
                           uint32_t *interruptStatus)
{
    const uint8_t idCommand[2] = {
        kTimeCardICP10100ReadIDHi, kTimeCardICP10100ReadIDLo
    };
    const uint8_t measureCommand[2] = {
        kTimeCardICP10100MeasureHi, kTimeCardICP10100MeasureLo
    };
    uint8_t idFrame[3] = {};
    uint8_t data[9] = {};
    kern_return_t result = TimeCardI2CWriteLocked(
        device, memoryIndex, map, reading->address, idCommand,
        sizeof(idCommand), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess)
        return false;
    reading->flags = kTimeCardSensorFlagPresent;
    IODelay(1000u);
    result = TimeCardI2CReadLocked(
        device, memoryIndex, map, reading->address, 0u, 0u, idFrame,
        sizeof(idFrame), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess ||
        !TimeCardSensorFrameCrcValid(idFrame))
        return false;
    const uint32_t productID = TimeCardSensorReadBE16(idFrame) & 0x3fu;
    reading->raw2 = productID;
    if (productID != kTimeCardICP10100ProductID)
        return false;

    result = TimeCardI2CWriteLocked(
        device, memoryIndex, map, reading->address, measureCommand,
        sizeof(measureCommand), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess)
        return false;
    IODelay(30000u);
    result = TimeCardI2CReadLocked(
        device, memoryIndex, map, reading->address, 0u, 0u, data,
        sizeof(data), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess ||
        !TimeCardSensorFrameCrcValid(&data[0]) ||
        !TimeCardSensorFrameCrcValid(&data[3]) ||
        !TimeCardSensorFrameCrcValid(&data[6]))
        return false;

    reading->pressureRaw = ((uint32_t)data[0] << 16) |
        ((uint32_t)data[1] << 8) | data[3];
    reading->raw0 = reading->pressureRaw;
    reading->raw1 = TimeCardSensorReadBE16(&data[6]);
    reading->temperatureMilliCelsius =
        -45000 + (int32_t)((175000ull * reading->raw1) / 65536ull);
    reading->flags |= kTimeCardSensorFlagValid |
        kTimeCardSensorFlagConfigured | kTimeCardSensorFlagConversionReady |
        kTimeCardSensorFlagTemperature | kTimeCardSensorFlagCRCValid |
        kTimeCardSensorFlagPressure;
    return true;
}

kern_return_t
TimeCardDriver::QuerySensors(TimeCardSensorTelemetry *telemetry)
{
    if (telemetry == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (telemetry->size < sizeof(*telemetry))
        return kIOReturnBadArgument;
    if (!TimeCardDriverHasSensors(&ivars->registers))
        return kIOReturnUnsupported;

    TimeCardSensorTelemetry local = {};
    local.size = sizeof(local);
    local.boardProfile = ivars->registers.boardProfile;
    local.capabilities = kTimeCardSensorCapabilityLM75B |
        kTimeCardSensorCapabilitySHT3x | kTimeCardSensorCapabilityICP10100;

    IOLockLock(ivars->registerLock);
    uint8_t savedMux = 0;
    uint32_t controllerStatus = 0;
    uint32_t interruptStatus = 0;
    kern_return_t result = TimeCardI2CMuxReadLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, &savedMux,
        &controllerStatus, &interruptStatus);
    if (result == kIOReturnNoDevice) {
        result = kIOReturnUnsupported;
        goto done;
    }
    if (result != kIOReturnSuccess)
        goto done;
    local.flags = kTimeCardSensorFlagPresent;
    local.muxChannelMask = savedMux;

    if (TimeCardSensorSelectBranchLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            1u, &controllerStatus, &interruptStatus)) {
        const uint32_t lm75Addresses[] = {
            kTimeCardSensorLM75BAddress1,
            kTimeCardSensorLM75BAddress2,
            kTimeCardSensorLM75BAddress3
        };
        for (uint32_t i = 0; i < 3u; ++i) {
            TimeCardSensorReading *reading = TimeCardSensorAppend(
                &local, kTimeCardSensorTypeLM75B, 1u, lm75Addresses[i]);
            if (reading != nullptr)
                (void)TimeCardLM75BReadLocked(
                    ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
                    reading, &controllerStatus, &interruptStatus);
        }
    }

    if (TimeCardSensorSelectBranchLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            2u, &controllerStatus, &interruptStatus)) {
        TimeCardSensorReading *reading = TimeCardSensorAppend(
            &local, kTimeCardSensorTypeSHT3x, 2u,
            kTimeCardSensorSHT3xAddress);
        if (reading != nullptr)
            (void)TimeCardSHT3xReadLocked(
                ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
                reading, &controllerStatus, &interruptStatus);
    }

    if (TimeCardSensorSelectBranchLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            4u, &controllerStatus, &interruptStatus)) {
        TimeCardSensorReading *reading = TimeCardSensorAppend(
            &local, kTimeCardSensorTypeICP10100, 4u,
            kTimeCardSensorICP10100Address);
        if (reading != nullptr)
            (void)TimeCardICP10100ReadLocked(
                ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
                reading, &controllerStatus, &interruptStatus);
    }

    result = TimeCardI2CMuxWriteLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, savedMux,
        &controllerStatus, &interruptStatus);
    local.restoredMuxChannelMask = savedMux;
    local.controllerStatus = controllerStatus;
    local.interruptStatus = interruptStatus;
    for (uint32_t i = 0; i < local.readingCount; ++i) {
        if ((local.readings[i].flags & kTimeCardSensorFlagValid) != 0) {
            local.flags |= kTimeCardSensorFlagValid;
            break;
        }
    }

done:
    IOLockUnlock(ivars->registerLock);
    if (result == kIOReturnSuccess)
        *telemetry = local;
    return result;
}

kern_return_t
TimeCardDriver::GetI2CStatus(TimeCardI2CStatus *status)
{
    if (status == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (status->size < sizeof(*status))
        return kIOReturnBadArgument;
    if (!TimeCardDriverHasI2C(&ivars->registers))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    TimeCardI2CStatus response = {};
    response.size = sizeof(response);
    response.offset = ivars->registers.i2cOffset;
    response.control = TimeCardI2CRead8(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        kXIICControlOffset);
    response.status = TimeCardI2CRead8(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        kXIICStatusOffset);
    response.interruptStatus = TimeCardI2CRead32(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        kXIICIISROffset);
    response.interruptEnable = TimeCardI2CRead32(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        kXIICIIEROffset);
    response.txFifoOccupancy = TimeCardI2CRead8(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        kXIICTxOccupancyOffset);
    response.rxFifoOccupancy = TimeCardI2CRead8(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        kXIICRxOccupancyOffset);
    response.flags = kTimeCardI2CFlagPresent;
    if ((response.control & kXIICControlEnable) != 0)
        response.flags |= kTimeCardI2CFlagEnabled;
    if ((response.status & kXIICStatusBusBusy) != 0)
        response.flags |= kTimeCardI2CFlagBusBusy;
    if ((response.status & kXIICStatusRxEmpty) != 0)
        response.flags |= kTimeCardI2CFlagRxEmpty;
    if ((response.status & kXIICStatusTxEmpty) != 0)
        response.flags |= kTimeCardI2CFlagTxEmpty;
    if (ivars->i2cLEDAddress != 0)
        response.knownDeviceMask |= kTimeCardI2CKnownDeviceLED;
    if (ivars->i2cMuxPresent)
        response.knownDeviceMask |= kTimeCardI2CKnownDeviceMux;
    IOLockUnlock(ivars->registerLock);

    *status = response;
    return kIOReturnSuccess;
}

kern_return_t
TimeCardDriver::ProbeI2C(TimeCardI2CProbe *probe)
{
    if (probe == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (probe->size < sizeof(*probe) ||
        !TimeCardI2CAddressValid(probe->address))
        return kIOReturnBadArgument;
    if (!TimeCardDriverHasI2C(&ivars->registers))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    TimeCardI2CProbe response = {};
    response.size = sizeof(response);
    response.address = probe->address;
    kern_return_t result = TimeCardI2CProbeLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        probe->address, &response.controllerStatus,
        &response.interruptStatus);
    if (result == kIOReturnNoDevice) {
        response.present = 0;
        result = kIOReturnSuccess;
    } else if (result == kIOReturnSuccess) {
        response.present = 1;
    }
    IOLockUnlock(ivars->registerLock);

    if (result == kIOReturnSuccess)
        *probe = response;
    return result;
}

kern_return_t
TimeCardDriver::ReadI2C(const TimeCardI2CReadRequest *request,
                        TimeCardI2CTransfer *response)
{
    if (request == nullptr || response == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (request->size < sizeof(*request) ||
        !TimeCardI2CAddressValid(request->address) ||
        request->subaddressLength > 2u || request->length == 0 ||
        request->length > TIMECARD_I2C_MAX_TRANSFER)
        return kIOReturnBadArgument;
    if (!TimeCardDriverHasI2C(&ivars->registers))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    TimeCardI2CTransfer local = {};
    local.size = sizeof(local);
    local.address = request->address;
    local.length = request->length;
    kern_return_t result = TimeCardI2CReadLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        request->address, request->subaddressLength, request->subaddress,
        local.data, request->length, &local.controllerStatus,
        &local.interruptStatus);
    IOLockUnlock(ivars->registerLock);

    if (result == kIOReturnSuccess)
        *response = local;
    return result;
}

kern_return_t
TimeCardDriver::QueryI2CMux(TimeCardI2CMuxControl *control)
{
    if (control == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (control->size < sizeof(*control))
        return kIOReturnBadArgument;
    if (!TimeCardDriverHasI2C(&ivars->registers))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    TimeCardI2CMuxControl response = {};
    response.size = sizeof(response);
    uint8_t channelMask = 0;
    kern_return_t result = TimeCardI2CMuxReadLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        &channelMask, &response.controllerStatus,
        &response.interruptStatus);
    if (result == kIOReturnNoDevice) {
        response.present = 0;
        ivars->i2cMuxPresent = false;
        result = kIOReturnSuccess;
    } else if (result == kIOReturnSuccess) {
        response.present = 1;
        response.channelMask = channelMask;
        ivars->i2cMuxPresent = true;
    }
    IOLockUnlock(ivars->registerLock);

    if (result == kIOReturnSuccess)
        *control = response;
    return result;
}

kern_return_t
TimeCardDriver::SetI2CMux(const TimeCardI2CMuxControl *request,
                          TimeCardI2CMuxControl *response)
{
    if (request == nullptr || response == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (request->size < sizeof(*request) ||
        (request->channelMask & ~kTimeCardI2CMuxChannelMask) != 0)
        return kIOReturnBadArgument;
    if (!TimeCardDriverHasI2C(&ivars->registers))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    uint32_t controllerStatus = 0;
    uint32_t interruptStatus = 0;
    kern_return_t result = TimeCardI2CMuxWriteLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
        (uint8_t)request->channelMask, &controllerStatus, &interruptStatus);
    TimeCardI2CMuxControl local = {};
    local.size = sizeof(local);
    local.channelMask = request->channelMask;
    local.controllerStatus = controllerStatus;
    local.interruptStatus = interruptStatus;
    if (result == kIOReturnSuccess) {
        uint8_t channelMask = 0;
        result = TimeCardI2CMuxReadLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            &channelMask, &local.controllerStatus, &local.interruptStatus);
        if (result == kIOReturnSuccess) {
            local.present = 1;
            local.channelMask = channelMask;
            ivars->i2cMuxPresent = true;
        }
    }
    IOLockUnlock(ivars->registerLock);

    if (result == kIOReturnSuccess)
        *response = local;
    return result;
}

static bool
TimeCardLEDLogicalIndexValid(uint32_t led)
{
    return led < TIMECARD_LED_COUNT;
}

static uint32_t
TimeCardLEDPhysicalIndex(const TimeCardRegisterMap *map, uint32_t logicalLED)
{
    static const uint32_t classicMap[TIMECARD_LED_COUNT] = {
        4u, 5u, 2u, 3u, 0u, 1u
    };
    static const uint32_t celesticaMap[TIMECARD_LED_COUNT] = {
        4u, 5u, 0u, 1u, 2u, 3u
    };
    if (map->boardProfile == kTimeCardBoardCelestica)
        return celesticaMap[logicalLED];
    if (map->boardProfile == kTimeCardBoardFacebook &&
        map->layout == kTimeCardLayoutClassic)
        return classicMap[logicalLED];
    return logicalLED;
}

static bool
TimeCardLEDChannelSwapsRedGreen(const TimeCardRegisterMap *map)
{
    return (map->boardProfile == kTimeCardBoardFacebook &&
            map->layout == kTimeCardLayoutClassic) ||
        map->boardProfile == kTimeCardBoardCelestica;
}

static bool
TimeCardLEDFitted(const TimeCardRegisterMap *map, uint32_t logicalLED)
{
    return map->boardProfile != kTimeCardBoardCelestica ||
        logicalLED != TIMECARD_LED_GNSS2;
}

static uint32_t
TimeCardLEDOutputMaskForLogical(const TimeCardRegisterMap *map,
                                uint32_t logicalLED)
{
    const uint32_t physical = TimeCardLEDPhysicalIndex(map, logicalLED);
    uint32_t red = physical * 3u;
    uint32_t green = red + 1u;
    uint32_t blue = red + 2u;
    if (TimeCardLEDChannelSwapsRedGreen(map)) {
        const uint32_t swap = red;
        red = green;
        green = swap;
    }
    return (1u << red) | (1u << green) | (1u << blue);
}

static uint32_t
TimeCardLEDPWMRegisterBase(const TimeCardRegisterMap *map,
                           uint32_t logicalLED)
{
    return kIS32FL3207PWMBase +
        TimeCardLEDPhysicalIndex(map, logicalLED) * 6u;
}

static uint32_t
TimeCardLEDScalingRegisterBase(const TimeCardRegisterMap *map,
                               uint32_t logicalLED)
{
    return kIS32FL3207ControlBase +
        TimeCardLEDPhysicalIndex(map, logicalLED) * 3u;
}

static kern_return_t
TimeCardLEDWriteRegisterLocked(IOPCIDevice *device, uint8_t memoryIndex,
                               const TimeCardRegisterMap *map,
                               uint32_t address, uint8_t reg, uint8_t value,
                               uint32_t *controllerStatus,
                               uint32_t *interruptStatus)
{
    const uint8_t data[2] = {reg, value};
    return TimeCardI2CWriteLocked(device, memoryIndex, map, address, data,
                                  sizeof(data), controllerStatus,
                                  interruptStatus);
}

static kern_return_t
TimeCardLEDSelectSensorsBranchLocked(IOPCIDevice *device, uint8_t memoryIndex,
                                     const TimeCardRegisterMap *map,
                                     uint8_t *savedMux,
                                     bool *restoreMux,
                                     uint32_t *controllerStatus,
                                     uint32_t *interruptStatus)
{
    *savedMux = 0;
    *restoreMux = false;
    kern_return_t result = TimeCardI2CMuxReadLocked(
        device, memoryIndex, map, savedMux, controllerStatus,
        interruptStatus);
    if (result == kIOReturnNoDevice)
        return kIOReturnSuccess;
    if (result != kIOReturnSuccess)
        return result;
    if (*savedMux == kTimeCardI2CMuxChannelSensors)
        return kIOReturnSuccess;
    result = TimeCardI2CMuxWriteLocked(
        device, memoryIndex, map, kTimeCardI2CMuxChannelSensors,
        controllerStatus, interruptStatus);
    if (result == kIOReturnSuccess)
        *restoreMux = true;
    return result;
}

static void
TimeCardLEDRestoreBranchLocked(IOPCIDevice *device, uint8_t memoryIndex,
                               const TimeCardRegisterMap *map,
                               uint8_t savedMux, bool restoreMux,
                               uint32_t *controllerStatus,
                               uint32_t *interruptStatus)
{
    if (restoreMux) {
        (void)TimeCardI2CMuxWriteLocked(device, memoryIndex, map, savedMux,
                                        controllerStatus, interruptStatus);
    }
}

static kern_return_t
TimeCardLEDResolveAddressLocked(TimeCardDriver_IVars *ivars,
                                uint32_t *address,
                                uint32_t *controllerStatus,
                                uint32_t *interruptStatus)
{
    static const uint32_t candidates[] = {0x37u, 0x36u, 0x35u, 0x34u};
    if (ivars->i2cLEDAddress != 0) {
        *address = ivars->i2cLEDAddress;
        return kIOReturnSuccess;
    }
    for (uint32_t candidate : candidates) {
        uint8_t deviceControl = 0xffu;
        uint8_t globalCurrent = 0xffu;
        kern_return_t result = TimeCardI2CReadLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            candidate, 1, kIS32FL3207DeviceControl, &deviceControl, 1,
            controllerStatus, interruptStatus);
        if (result != kIOReturnSuccess)
            continue;
        result = TimeCardI2CReadLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
            candidate, 1, kIS32FL3207GlobalCurrent, &globalCurrent, 1,
            controllerStatus, interruptStatus);
        if (result == kIOReturnSuccess && (deviceControl & 0x88u) == 0) {
            ivars->i2cLEDAddress = candidate;
            *address = candidate;
            return kIOReturnSuccess;
        }
    }
    return kIOReturnNoDevice;
}

static void
TimeCardLEDEncodeColor(const TimeCardRegisterMap *map,
                       const TimeCardLEDControl *control, uint8_t *rgb)
{
    rgb[0] = (uint8_t)control->red;
    rgb[1] = (uint8_t)control->green;
    rgb[2] = (uint8_t)control->blue;
    if (TimeCardLEDChannelSwapsRedGreen(map)) {
        const uint8_t swap = rgb[0];
        rgb[0] = rgb[1];
        rgb[1] = swap;
    }
}

static kern_return_t
TimeCardLEDReadStateLocked(TimeCardDriver_IVars *ivars,
                           uint32_t logicalLED,
                           TimeCardLEDControl *response,
                           uint32_t *controllerStatus,
                           uint32_t *interruptStatus)
{
    uint32_t address = 0;
    uint8_t raw[6] = {};
    uint8_t global = 0;
    kern_return_t result = TimeCardLEDResolveAddressLocked(
        ivars, &address, controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess)
        return result;
    const uint32_t base = TimeCardLEDPWMRegisterBase(&ivars->registers,
                                                     logicalLED);
    result = TimeCardI2CReadLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, address, 1,
        base, raw, sizeof(raw), controllerStatus, interruptStatus);
    if (result != kIOReturnSuccess)
        return result;
    result = TimeCardI2CReadLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, address, 1,
        kIS32FL3207GlobalCurrent, &global, 1, controllerStatus,
        interruptStatus);
    if (result != kIOReturnSuccess)
        return result;

    *response = {};
    response->size = sizeof(*response);
    response->led = logicalLED;
    response->flags = kTimeCardLEDFlagPresent | kTimeCardLEDFlagEnabled;
    if (TimeCardLEDChannelSwapsRedGreen(&ivars->registers)) {
        response->red = raw[2];
        response->green = raw[0];
    } else {
        response->red = raw[0];
        response->green = raw[2];
    }
    response->blue = raw[4];
    response->globalCurrent = global;
    response->muxChannelMask = kTimeCardI2CMuxChannelSensors;
    response->controllerStatus = *controllerStatus;
    response->interruptStatus = *interruptStatus;
    response->openOutputMask =
        TimeCardLEDOutputMaskForLogical(&ivars->registers, logicalLED);
    response->shortOutputMask = 0;
    return kIOReturnSuccess;
}

kern_return_t
TimeCardDriver::QueryLED(TimeCardLEDControl *control)
{
    if (control == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (control->size < sizeof(*control) ||
        !TimeCardLEDLogicalIndexValid(control->led))
        return kIOReturnBadArgument;
    if ((ivars->registers.capabilities & kTimeCardCapabilityLED) == 0 ||
        !TimeCardRegisterMapHasLED(&ivars->registers))
        return kIOReturnUnsupported;
    if (!TimeCardLEDFitted(&ivars->registers, control->led))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    uint8_t savedMux = 0;
    bool restoreMux = false;
    uint32_t controllerStatus = 0;
    uint32_t interruptStatus = 0;
    kern_return_t result = TimeCardLEDSelectSensorsBranchLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, &savedMux,
        &restoreMux, &controllerStatus, &interruptStatus);
    if (result == kIOReturnSuccess) {
        TimeCardLEDControl response = {};
        result = TimeCardLEDReadStateLocked(
            ivars, control->led, &response, &controllerStatus,
            &interruptStatus);
        if (result == kIOReturnSuccess)
            *control = response;
    }
    TimeCardLEDRestoreBranchLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, savedMux,
        restoreMux, &controllerStatus, &interruptStatus);
    IOLockUnlock(ivars->registerLock);
    return result;
}

kern_return_t
TimeCardDriver::SetLED(const TimeCardLEDControl *request,
                       TimeCardLEDControl *response)
{
    if (request == nullptr || response == nullptr || !ivars->deviceOpen)
        return kIOReturnNotReady;
    if (request->size < sizeof(*request) ||
        !TimeCardLEDLogicalIndexValid(request->led) ||
        request->red > UINT8_MAX || request->green > UINT8_MAX ||
        request->blue > UINT8_MAX ||
        request->globalCurrent > kTimeCardLEDMaxGlobalCurrent)
        return kIOReturnBadArgument;
    if ((ivars->registers.capabilities & kTimeCardCapabilityLED) == 0 ||
        !TimeCardRegisterMapHasLED(&ivars->registers))
        return kIOReturnUnsupported;
    if (!TimeCardLEDFitted(&ivars->registers, request->led))
        return kIOReturnUnsupported;

    IOLockLock(ivars->registerLock);
    uint8_t savedMux = 0;
    bool restoreMux = false;
    uint32_t controllerStatus = 0;
    uint32_t interruptStatus = 0;
    uint32_t address = 0;
    kern_return_t result = TimeCardLEDSelectSensorsBranchLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, &savedMux,
        &restoreMux, &controllerStatus, &interruptStatus);
    if (result != kIOReturnSuccess)
        goto done;
    result = TimeCardLEDResolveAddressLocked(
        ivars, &address, &controllerStatus, &interruptStatus);
    if (result != kIOReturnSuccess)
        goto done;
    result = TimeCardLEDWriteRegisterLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, address,
        kIS32FL3207DeviceControl, 0x01u, &controllerStatus,
        &interruptStatus);
    if (result != kIOReturnSuccess)
        goto done;

    {
        const uint8_t current = request->globalCurrent == 0 ?
            kTimeCardLEDMaxGlobalCurrent : (uint8_t)request->globalCurrent;
        uint8_t rgb[3] = {};
        TimeCardLEDEncodeColor(&ivars->registers, request, rgb);
        const uint8_t scaleBase = (uint8_t)TimeCardLEDScalingRegisterBase(
            &ivars->registers, request->led);
        const uint8_t pwmBase = (uint8_t)TimeCardLEDPWMRegisterBase(
            &ivars->registers, request->led);
        result = TimeCardLEDWriteRegisterLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers, address,
            kIS32FL3207GlobalCurrent, current, &controllerStatus,
            &interruptStatus);
        if (result != kIOReturnSuccess)
            goto done;
        for (uint32_t channel = 0; channel < 3u; ++channel) {
            result = TimeCardLEDWriteRegisterLocked(
                ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
                address, (uint8_t)(scaleBase + channel), 0xffu,
                &controllerStatus, &interruptStatus);
            if (result != kIOReturnSuccess)
                goto done;
        }
        const uint8_t pwm[6] = {rgb[0], 0, rgb[1], 0, rgb[2], 0};
        for (uint32_t channel = 0; channel < sizeof(pwm); ++channel) {
            result = TimeCardLEDWriteRegisterLocked(
                ivars->pciDevice, ivars->memoryIndex, &ivars->registers,
                address, (uint8_t)(pwmBase + channel), pwm[channel],
                &controllerStatus, &interruptStatus);
            if (result != kIOReturnSuccess)
                goto done;
        }
        result = TimeCardLEDWriteRegisterLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers, address,
            kIS32FL3207Update, 0x00u, &controllerStatus, &interruptStatus);
        if (result != kIOReturnSuccess)
            goto done;
        result = TimeCardLEDWriteRegisterLocked(
            ivars->pciDevice, ivars->memoryIndex, &ivars->registers, address,
            kIS32FL3207SpreadSpectrum, 0x00u, &controllerStatus,
            &interruptStatus);
        if (result != kIOReturnSuccess)
            goto done;
        result = TimeCardLEDReadStateLocked(
            ivars, request->led, response, &controllerStatus,
            &interruptStatus);
        if (result == kIOReturnSuccess &&
            (response->red != request->red ||
             response->green != request->green ||
             response->blue != request->blue ||
             response->globalCurrent != current)) {
            result = kIOReturnBadMedia;
        }
    }

done:
    TimeCardLEDRestoreBranchLocked(
        ivars->pciDevice, ivars->memoryIndex, &ivars->registers, savedMux,
        restoreMux, &controllerStatus, &interruptStatus);
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

static kern_return_t
LEDQueryAction(OSObject *, void *reference,
               IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    if (arguments->structureInput == nullptr)
        return kIOReturnBadArgument;
    auto *request = static_cast<const TimeCardLEDControl *>(
        arguments->structureInput->getBytesNoCopy());
    TimeCardLEDControl response = *request;
    const kern_return_t result = driver->QueryLED(&response);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&response, sizeof(response));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
LEDSetAction(OSObject *, void *reference,
             IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    if (arguments->structureInput == nullptr)
        return kIOReturnBadArgument;
    auto *request = static_cast<const TimeCardLEDControl *>(
        arguments->structureInput->getBytesNoCopy());
    TimeCardLEDControl response = {};
    const kern_return_t result = driver->SetLED(request, &response);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&response, sizeof(response));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
I2CStatusAction(OSObject *, void *reference,
                IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    TimeCardI2CStatus status = {sizeof(status)};
    const kern_return_t result = driver->GetI2CStatus(&status);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&status, sizeof(status));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
I2CProbeAction(OSObject *, void *reference,
               IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    if (arguments->structureInput == nullptr)
        return kIOReturnBadArgument;
    auto *request = static_cast<const TimeCardI2CProbe *>(
        arguments->structureInput->getBytesNoCopy());
    TimeCardI2CProbe response = *request;
    const kern_return_t result = driver->ProbeI2C(&response);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&response, sizeof(response));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
I2CReadAction(OSObject *, void *reference,
              IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    if (arguments->structureInput == nullptr)
        return kIOReturnBadArgument;
    auto *request = static_cast<const TimeCardI2CReadRequest *>(
        arguments->structureInput->getBytesNoCopy());
    TimeCardI2CTransfer response = {};
    const kern_return_t result = driver->ReadI2C(request, &response);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&response, sizeof(response));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
I2CMuxQueryAction(OSObject *, void *reference,
                  IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    TimeCardI2CMuxControl control = {sizeof(control)};
    const kern_return_t result = driver->QueryI2CMux(&control);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&control, sizeof(control));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
I2CMuxSetAction(OSObject *, void *reference,
                IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    if (arguments->structureInput == nullptr)
        return kIOReturnBadArgument;
    auto *request = static_cast<const TimeCardI2CMuxControl *>(
        arguments->structureInput->getBytesNoCopy());
    TimeCardI2CMuxControl response = {};
    const kern_return_t result = driver->SetI2CMux(request, &response);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&response, sizeof(response));
        if (arguments->structureOutput == nullptr)
            return kIOReturnNoMemory;
    }
    return result;
}

static kern_return_t
SensorQueryAction(OSObject *, void *reference,
                  IOUserClientMethodArguments *arguments)
{
    auto *driver = static_cast<TimeCardDriver *>(reference);
    TimeCardSensorTelemetry telemetry = {sizeof(telemetry)};
    const kern_return_t result = driver->QuerySensors(&telemetry);
    if (result == kIOReturnSuccess) {
        arguments->structureOutput =
            OSData::withBytes(&telemetry, sizeof(telemetry));
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
    {LEDQueryAction, false, 0, sizeof(TimeCardLEDControl), 0,
     sizeof(TimeCardLEDControl)},
    {LEDSetAction, false, 0, sizeof(TimeCardLEDControl), 0,
     sizeof(TimeCardLEDControl)},
    {I2CStatusAction, false, 0, 0, 0, sizeof(TimeCardI2CStatus)},
    {I2CProbeAction, false, 0, sizeof(TimeCardI2CProbe), 0,
     sizeof(TimeCardI2CProbe)},
    {I2CReadAction, false, 0, sizeof(TimeCardI2CReadRequest), 0,
     sizeof(TimeCardI2CTransfer)},
    {I2CMuxQueryAction, false, 0, 0, 0, sizeof(TimeCardI2CMuxControl)},
    {I2CMuxSetAction, false, 0, sizeof(TimeCardI2CMuxControl), 0,
     sizeof(TimeCardI2CMuxControl)},
    {SensorQueryAction, false, 0, 0, 0, sizeof(TimeCardSensorTelemetry)},
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
