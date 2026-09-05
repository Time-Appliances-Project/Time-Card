/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef TIMECARD_ABI_H
#define TIMECARD_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIMECARD_ABI_VERSION 11u
#define TIMECARD_DRIVER_VERSION 0x00000002u
#define TIMECARD_SERVICE_CLASS "TimeCardDriver"
#define TIMECARD_DRIVER_BUNDLE_ID "org.opentimeserver.timecard.macos.driver"

enum TimeCardExternalMethod {
    kTimeCardMethodGetInfo = 0,
    kTimeCardMethodGetTime = 1,
    kTimeCardMethodSetTime = 2,
    kTimeCardMethodGetCrossTimestamp = 3,
    kTimeCardMethodSMAQuery = 4,
    kTimeCardMethodSMASet = 5,
    kTimeCardMethodLEDQuery = 6,
    kTimeCardMethodLEDSet = 7,
    kTimeCardMethodI2CStatus = 8,
    kTimeCardMethodI2CProbe = 9,
    kTimeCardMethodI2CRead = 10,
    kTimeCardMethodI2CMuxQuery = 11,
    kTimeCardMethodI2CMuxSet = 12,
    kTimeCardMethodSensorQuery = 13,
    kTimeCardMethodUARTObserve = 14,
    kTimeCardMethodUARTConfigure = 15,
    kTimeCardMethodUARTRead = 16,
    kTimeCardMethodUARTWrite = 17,
    kTimeCardMethodClockControlQuery = 18,
    kTimeCardMethodClockSourceSet = 19,
    kTimeCardMethodFrequencyQuery = 20,
    kTimeCardMethodFrequencySet = 21,
    kTimeCardMethodIMUQuery = 22,
    kTimeCardMethodCount
};

enum TimeCardRegisterLayout {
    kTimeCardLayoutUnknown = 0,
    kTimeCardLayoutClassic = 1,
    kTimeCardLayoutLitePCIe = 2,
    kTimeCardLayoutART = 3,

    /* ABI v1 source-compatible aliases. These name the register map only. */
    kTimeCardLayoutMSI = kTimeCardLayoutClassic,
    kTimeCardLayoutMSIX = kTimeCardLayoutLitePCIe
};

enum TimeCardBoardProfile {
    kTimeCardBoardUnknown = 0,
    kTimeCardBoardFacebook = 1,
    kTimeCardBoardCelestica = 2,
    kTimeCardBoardOroliaART = 3,
    kTimeCardBoardADVA = 4,
    kTimeCardBoardADVAX1 = 5
};

enum TimeCardCapability {
    kTimeCardCapabilityReadClock = 1u << 0,
    kTimeCardCapabilitySetClock = 1u << 1,
    kTimeCardCapabilityCrossTimestamp = 1u << 2,
    kTimeCardCapabilityTOD = 1u << 3,
    kTimeCardCapabilitySMA = 1u << 4,
    kTimeCardCapabilityLED = 1u << 5,
    kTimeCardCapabilityI2C = 1u << 6,
    kTimeCardCapabilitySensors = 1u << 7,
    kTimeCardCapabilityUART = 1u << 8,
    kTimeCardCapabilityClockSource = 1u << 9,
    kTimeCardCapabilityFrequency = 1u << 10,
    kTimeCardCapabilityIMU = 1u << 11
};

enum TimeCardSMADirection {
    kTimeCardSMADirectionInput = 0,
    kTimeCardSMADirectionOutput = 1,
    kTimeCardSMADirectionDisabled = 2
};

enum TimeCardSMAFlag {
    kTimeCardSMAFlagPresent = 1u << 0,
    kTimeCardSMAFlagFixedDirection = 1u << 1,
    kTimeCardSMAFlagDisabled = 1u << 2,
    kTimeCardSMAFlagFixedFunction = 1u << 3
};

enum TimeCardSMAFunction {
    kTimeCardSMAInput10MHz = 0x0000u,
    kTimeCardSMAInputPPS1 = 0x0001u,
    kTimeCardSMAInputPPS2 = 0x0002u,
    kTimeCardSMAInputTS1 = 0x0004u,
    kTimeCardSMAInputTS2 = 0x0008u,
    kTimeCardSMAInputIRIG = 0x0010u,
    kTimeCardSMAInputDCF = 0x0020u,
    kTimeCardSMAInputTS3 = 0x0040u,
    kTimeCardSMAInputTS4 = 0x0080u,
    kTimeCardSMAInputFREQ1 = 0x0100u,
    kTimeCardSMAInputFREQ2 = 0x0200u,
    kTimeCardSMAInputFREQ3 = 0x0400u,
    kTimeCardSMAInputFREQ4 = 0x0800u,

    kTimeCardSMAOutput10MHz = 0x0000u,
    kTimeCardSMAOutputPHC = 0x0001u,
    kTimeCardSMAOutputMAC = 0x0002u,
    kTimeCardSMAOutputGNSS1 = 0x0004u,
    kTimeCardSMAOutputGNSS2 = 0x0008u,
    kTimeCardSMAOutputIRIG = 0x0010u,
    kTimeCardSMAOutputDCF = 0x0020u,
    kTimeCardSMAOutputGEN1 = 0x0040u,
    kTimeCardSMAOutputGEN2 = 0x0080u,
    kTimeCardSMAOutputGEN3 = 0x0100u,
    kTimeCardSMAOutputGEN4 = 0x0200u,
    kTimeCardSMAOutputGND = 0x2000u,
    kTimeCardSMAOutputVCC = 0x4000u
};

enum TimeCardInfoValidField {
    kTimeCardInfoValidClockVersion = 1ull << 0,
    kTimeCardInfoValidClockStatus = 1ull << 1,
    kTimeCardInfoValidClockSelect = 1ull << 2,
    kTimeCardInfoValidTODVersion = 1ull << 3,
    kTimeCardInfoValidTODStatus = 1ull << 4,
    kTimeCardInfoValidUTCStatus = 1ull << 5,
    kTimeCardInfoValidLeap = 1ull << 6,
    kTimeCardInfoValidGNSSStatus = 1ull << 7,
    kTimeCardInfoValidSatellites = 1ull << 8
};

typedef struct TimeCardTime {
    uint64_t seconds;
    uint32_t nanoseconds;
    uint32_t reserved;
} TimeCardTime;

/* ABI v10: append-only typed timing operations. No raw register access. */
typedef struct TimeCardClockControl {
    uint32_t size;
    uint32_t source;
    uint32_t activeSource;
    uint32_t supportedSources; /* bits 0..6, bit 30 = regs, bit 31 = ext */
    uint32_t clockVersion;
    uint32_t control;
    uint32_t status;
    uint32_t reserved;
} TimeCardClockControl;

typedef struct TimeCardClockSourceRequest {
    uint32_t size;
    uint32_t source;
    uint32_t expectedSource; /* compare before writing, detects stale UI */
    uint32_t reserved;
} TimeCardClockSourceRequest;

#define TIMECARD_FREQUENCY_COUNT 4u
enum TimeCardFrequencyFlag {
    kTimeCardFrequencyPresent = 1u << 0,
    kTimeCardFrequencyEnabled = 1u << 1,
    kTimeCardFrequencyValid = 1u << 2,
    kTimeCardFrequencyError = 1u << 3,
    kTimeCardFrequencyOverrun = 1u << 4
};
typedef struct TimeCardFrequencyControl {
    uint32_t size;
    uint32_t counter; /* 1..4 */
    uint32_t integrationSeconds; /* 0 = disabled, otherwise 1..255 */
    uint32_t flags;
    uint32_t frequencyHz;
    uint32_t control;
    uint32_t status;
    uint32_t reserved;
} TimeCardFrequencyControl;
typedef struct TimeCardFrequencyRequest {
    uint32_t size;
    uint32_t counter;
    uint32_t integrationSeconds;
    uint32_t expectedControl; /* query requires both final fields zero */
} TimeCardFrequencyRequest;

/* System timestamps are nanoseconds since the Unix epoch. */
typedef struct TimeCardCrossTimestamp {
    TimeCardTime cardTime;
    uint64_t systemTimeBeforeNanoseconds;
    uint64_t systemTimeAfterNanoseconds;
} TimeCardCrossTimestamp;

typedef struct TimeCardInfo {
    uint32_t abiVersion;
    uint32_t driverVersion;
    uint16_t vendorID;
    uint16_t deviceID;
    uint32_t layout;
    uint32_t advertisedMSIXVectors;
    uint64_t barSize;
    uint64_t clockOffset;
    uint64_t todOffset;
    uint32_t clockVersion;
    uint32_t clockStatus;
    uint32_t clockSelect;
    uint32_t todVersion;
    uint32_t todStatus;
    uint32_t utcStatus;
    uint32_t leap;
    uint32_t gnssStatus;
    uint32_t satellites;
    uint32_t boardProfile;
    uint32_t capabilities;
    uint64_t validFields;
    uint32_t pciRevision;
    uint32_t reserved;
} TimeCardInfo;

#define TIMECARD_SMA_COUNT 4u

typedef struct TimeCardSMAControl {
    uint32_t size;
    uint32_t connector;
    uint32_t direction;
    uint32_t function;
    uint32_t flags;
    uint32_t inputMap;
    uint32_t outputMap;
    uint32_t reserved;
} TimeCardSMAControl;

#define TIMECARD_LED_COUNT 6u
#define TIMECARD_LED_GNSS1 0u
#define TIMECARD_LED_GNSS2 1u
#define TIMECARD_LED_SMA1 2u
#define TIMECARD_LED_SMA2 3u
#define TIMECARD_LED_SMA3 4u
#define TIMECARD_LED_SMA4 5u

enum TimeCardLEDFlag {
    kTimeCardLEDFlagPresent = 1u << 0,
    kTimeCardLEDFlagEnabled = 1u << 1,
    kTimeCardLEDFlagFaultValid = 1u << 2
};

typedef struct TimeCardLEDControl {
    uint32_t size;
    uint32_t led;
    uint32_t flags;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t globalCurrent;
    uint32_t muxChannelMask;
    uint32_t controllerStatus;
    uint32_t interruptStatus;
    uint32_t openOutputMask;
    uint32_t shortOutputMask;
} TimeCardLEDControl;

#define TIMECARD_I2C_MAX_TRANSFER 255u

enum TimeCardI2CFlag {
    kTimeCardI2CFlagPresent = 1u << 0,
    kTimeCardI2CFlagEnabled = 1u << 1,
    kTimeCardI2CFlagBusBusy = 1u << 2,
    kTimeCardI2CFlagRxEmpty = 1u << 3,
    kTimeCardI2CFlagTxEmpty = 1u << 4
};

enum TimeCardI2CKnownDevice {
    kTimeCardI2CKnownDeviceMux = 1u << 0,
    kTimeCardI2CKnownDeviceLED = 1u << 1
};

typedef struct TimeCardI2CStatus {
    uint32_t size;
    uint32_t flags;
    uint64_t offset;
    uint32_t control;
    uint32_t status;
    uint32_t interruptStatus;
    uint32_t interruptEnable;
    uint32_t txFifoOccupancy;
    uint32_t rxFifoOccupancy;
    uint32_t knownDeviceMask;
    uint32_t reserved;
} TimeCardI2CStatus;

typedef struct TimeCardI2CProbe {
    uint32_t size;
    uint32_t address;
    uint32_t present;
    uint32_t controllerStatus;
    uint32_t interruptStatus;
    uint32_t reserved[3];
} TimeCardI2CProbe;

typedef struct TimeCardI2CReadRequest {
    uint32_t size;
    uint32_t address;
    uint32_t subaddressLength;
    uint32_t subaddress;
    uint32_t length;
    uint32_t reserved[3];
} TimeCardI2CReadRequest;

typedef struct TimeCardI2CTransfer {
    uint32_t size;
    uint32_t address;
    uint32_t length;
    uint32_t controllerStatus;
    uint32_t interruptStatus;
    uint8_t data[256];
} TimeCardI2CTransfer;

typedef struct TimeCardI2CMuxControl {
    uint32_t size;
    uint32_t present;
    uint32_t channelMask;
    uint32_t controllerStatus;
    uint32_t interruptStatus;
    uint32_t reserved[3];
} TimeCardI2CMuxControl;

#define TIMECARD_UART_COUNT 4u
#define TIMECARD_UART_MAX_TRANSFER 256u
#define TIMECARD_UART_GNSS 0u
#define TIMECARD_UART_GNSS2 1u
#define TIMECARD_UART_MAC 2u
#define TIMECARD_UART_NMEA 3u

enum TimeCardUARTObserveFlag {
    kTimeCardUARTObserveFlagPresent = 1u << 0,
    kTimeCardUARTObserveFlagActivity = 1u << 1
};

typedef struct TimeCardUARTConfig {
    uint32_t port;
    uint32_t baud;
} TimeCardUARTConfig;

typedef struct TimeCardUARTReadRequest {
    uint32_t port;
    uint32_t maximumBytes;
    uint32_t timeoutMilliseconds;
    uint32_t reserved;
} TimeCardUARTReadRequest;

typedef struct TimeCardUARTTransfer {
    uint32_t port;
    uint32_t length;
    uint32_t timeoutMilliseconds;
    uint32_t lineStatus;
    uint8_t data[TIMECARD_UART_MAX_TRANSFER];
} TimeCardUARTTransfer;

typedef struct TimeCardUARTObserve {
    uint32_t size;
    uint32_t port;
    uint32_t timeoutMilliseconds;
    uint32_t flags;
    uint32_t lineStatus;
    uint32_t reserved[3];
} TimeCardUARTObserve;

#define TIMECARD_SENSOR_MAX_READINGS 16u

/* ABI v11: opt-in fused motion. mode 0 polls, 1 starts/re-subscribes,
 * 2 stops BNO08x reports. No generic I2C write or clock control is exposed. */
typedef struct TimeCardIMURequest {
    uint32_t size;
    uint32_t mode;
    uint32_t reserved[2];
} TimeCardIMURequest;
enum TimeCardIMUFlag {
    kTimeCardIMUPresent = 1u << 0,
    kTimeCardIMUConfigured = 1u << 1,
    kTimeCardIMURotation = 1u << 2,
    kTimeCardIMUAcceleration = 1u << 3,
    kTimeCardIMULinearAcceleration = 1u << 4,
    kTimeCardIMUGravity = 1u << 5,
    kTimeCardIMUGyroscope = 1u << 6,
    kTimeCardIMUMagnetic = 1u << 7,
    kTimeCardIMUTemperature = 1u << 8,
    kTimeCardIMUIncomplete = 1u << 9,
    kTimeCardIMUMuxRestored = 1u << 10,
    kTimeCardIMUReset = 1u << 11
};
typedef struct TimeCardIMUTelemetry {
    uint32_t size, flags, type, muxChannelMask;
    uint32_t address, restoredMuxChannelMask, controllerStatus, interruptStatus;
    uint32_t sampleSequence, reportCount, calibration, systemStatus;
    int32_t quaternionQ14[4]; /* x, y, z, w */
    int32_t accelerationQ8[3]; /* m/s2 */
    int32_t linearAccelerationQ8[3]; /* m/s2, gravity removed */
    int32_t gravityQ8[3]; /* m/s2 */
    int32_t gyroscopeQ9[3]; /* radians/s */
    int32_t magneticQ4[3]; /* microtesla */
    int32_t temperatureQ7; /* Celsius */
    uint32_t reserved[4];
} TimeCardIMUTelemetry;

enum TimeCardSensorType {
    kTimeCardSensorTypeUnknown = 0,
    kTimeCardSensorTypeLM75B = 1,
    kTimeCardSensorTypeSHT3x = 2,
    kTimeCardSensorTypeICP10100 = 3,
    kTimeCardSensorTypeBME280 = 4,
    kTimeCardSensorTypeINA219 = 5,
    kTimeCardSensorTypeBNO08x = 6,
    kTimeCardSensorTypeBNO055 = 7
};

enum TimeCardSensorFlag {
    kTimeCardSensorFlagPresent = 1u << 0,
    kTimeCardSensorFlagValid = 1u << 1,
    kTimeCardSensorFlagConfigured = 1u << 2,
    kTimeCardSensorFlagConversionReady = 1u << 3,
    kTimeCardSensorFlagOverflow = 1u << 4,
    kTimeCardSensorFlagHumidity = 1u << 6,
    kTimeCardSensorFlagTemperature = 1u << 7,
    kTimeCardSensorFlagCRCValid = 1u << 9,
    kTimeCardSensorFlagPressure = 1u << 10,
    kTimeCardSensorFlagCalibrated = 1u << 11,
    kTimeCardSensorFlagIMU = 1u << 12
};

enum TimeCardSensorCapability {
    kTimeCardSensorCapabilityBME280 = 1u << 0,
    kTimeCardSensorCapabilityINA219 = 1u << 1,
    kTimeCardSensorCapabilityBNO055 = 1u << 2,
    kTimeCardSensorCapabilityBNO08x = 1u << 3,
    kTimeCardSensorCapabilityLM75B = 1u << 4,
    kTimeCardSensorCapabilitySHT3x = 1u << 5,
    kTimeCardSensorCapabilityICP10100 = 1u << 6
};

typedef struct TimeCardSensorReading {
    uint32_t size;
    uint32_t type;
    uint32_t flags;
    uint32_t muxChannelMask;
    uint32_t address;
    int32_t temperatureMilliCelsius;
    uint32_t humidityMilliPercent;
    uint32_t pressureRaw;
    uint32_t raw0;
    uint32_t raw1;
    uint32_t raw2;
    uint32_t reserved;
} TimeCardSensorReading;

typedef struct TimeCardSensorTelemetry {
    uint32_t size;
    uint32_t flags;
    uint32_t boardProfile;
    uint32_t capabilities;
    uint32_t muxChannelMask;
    uint32_t restoredMuxChannelMask;
    uint32_t controllerStatus;
    uint32_t interruptStatus;
    uint32_t readingCount;
    int32_t icp10100Otp[4];
    uint32_t reserved[3];
    TimeCardSensorReading readings[TIMECARD_SENSOR_MAX_READINGS];
} TimeCardSensorTelemetry;

#ifdef __cplusplus
}

static_assert(sizeof(TimeCardTime) == 16, "TimeCardTime ABI changed");
static_assert(sizeof(TimeCardIMURequest) == 16, "IMU request ABI changed");
static_assert(sizeof(TimeCardIMUTelemetry) == 144, "IMU telemetry ABI changed");
static_assert(sizeof(TimeCardClockControl) == 32, "Clock control ABI changed");
static_assert(sizeof(TimeCardClockSourceRequest) == 16, "Clock request ABI changed");
static_assert(sizeof(TimeCardFrequencyControl) == 32, "Frequency ABI changed");
static_assert(sizeof(TimeCardFrequencyRequest) == 16, "Frequency request ABI changed");
static_assert(sizeof(TimeCardCrossTimestamp) == 32,
              "TimeCardCrossTimestamp ABI changed");
static_assert(sizeof(TimeCardInfo) == 112, "TimeCardInfo ABI changed");
static_assert(sizeof(TimeCardSMAControl) == 32,
              "TimeCardSMAControl ABI changed");
static_assert(sizeof(TimeCardLEDControl) == 48,
              "TimeCardLEDControl ABI changed");
static_assert(sizeof(TimeCardI2CStatus) == 48,
              "TimeCardI2CStatus ABI changed");
static_assert(sizeof(TimeCardI2CProbe) == 32,
              "TimeCardI2CProbe ABI changed");
static_assert(sizeof(TimeCardI2CReadRequest) == 32,
              "TimeCardI2CReadRequest ABI changed");
static_assert(sizeof(TimeCardI2CTransfer) == 276,
              "TimeCardI2CTransfer ABI changed");
static_assert(sizeof(TimeCardI2CMuxControl) == 32,
              "TimeCardI2CMuxControl ABI changed");
static_assert(sizeof(TimeCardUARTConfig) == 8,
              "TimeCardUARTConfig ABI changed");
static_assert(sizeof(TimeCardUARTReadRequest) == 16,
              "TimeCardUARTReadRequest ABI changed");
static_assert(sizeof(TimeCardUARTTransfer) == 272,
              "TimeCardUARTTransfer ABI changed");
static_assert(sizeof(TimeCardUARTObserve) == 32,
              "TimeCardUARTObserve ABI changed");
static_assert(sizeof(TimeCardSensorReading) == 48,
              "TimeCardSensorReading ABI changed");
static_assert(sizeof(TimeCardSensorTelemetry) == 832,
              "TimeCardSensorTelemetry ABI changed");
#endif

#endif /* TIMECARD_ABI_H */
