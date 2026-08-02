/* SPDX-License-Identifier: BSD-3-Clause */
/* Public IOCTL ABI for the OCP TimeCard Windows driver. */

#ifndef TIMECARD_IOCTL_H
#define TIMECARD_IOCTL_H

#if defined(_KERNEL_MODE)
#include <devioctl.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define TIMECARD_NT_DEVICE_NAME   L"\\Device\\TimeCard0"
#define TIMECARD_DOS_DEVICE_NAME  L"\\DosDevices\\TimeCard0"
#define TIMECARD_USER_DEVICE_PATH L"\\\\.\\TimeCard0"

#define TIMECARD_IOCTL_INDEX 0x800u
#define TIMECARD_IOCTL(n, access) \
    CTL_CODE(FILE_DEVICE_UNKNOWN, TIMECARD_IOCTL_INDEX + (n), \
             METHOD_BUFFERED, (access))

#define IOCTL_TIMECARD_GET_TIME \
    TIMECARD_IOCTL(0, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_SET_TIME \
    TIMECARD_IOCTL(1, FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_GET_INFO \
    TIMECARD_IOCTL(2, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_GET_CROSSTIMESTAMP \
    TIMECARD_IOCTL(3, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_UART_CONFIGURE \
    TIMECARD_IOCTL(4, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_UART_READ \
    TIMECARD_IOCTL(5, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_UART_WRITE \
    TIMECARD_IOCTL(6, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_HIERARCHY_CONTROL \
    TIMECARD_IOCTL(7, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_SMA_QUERY \
    TIMECARD_IOCTL(8, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_SMA_SET \
    TIMECARD_IOCTL(9, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_I2C_GET_STATUS \
    TIMECARD_IOCTL(10, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_I2C_PROBE \
    TIMECARD_IOCTL(11, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_I2C_READ \
    TIMECARD_IOCTL(12, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_CLOCK_SOURCE_SET \
    TIMECARD_IOCTL(13, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_NMEA_QUERY \
    TIMECARD_IOCTL(14, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_NMEA_SET \
    TIMECARD_IOCTL(15, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_GET_IDENTITY \
    TIMECARD_IOCTL(16, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_SIGNAL_QUERY \
    TIMECARD_IOCTL(17, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_SIGNAL_SET \
    TIMECARD_IOCTL(18, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FREQUENCY_QUERY \
    TIMECARD_IOCTL(19, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FREQUENCY_SET \
    TIMECARD_IOCTL(20, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FLASH_QUERY \
    TIMECARD_IOCTL(21, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_FLASH_READ \
    TIMECARD_IOCTL(22, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FLASH_ERASE \
    TIMECARD_IOCTL(23, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FLASH_PROGRAM \
    TIMECARD_IOCTL(24, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_UART_OBSERVE \
    TIMECARD_IOCTL(25, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_I2C_MUX_QUERY \
    TIMECARD_IOCTL(26, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_I2C_MUX_SET \
    TIMECARD_IOCTL(27, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_LED_QUERY \
    TIMECARD_IOCTL(28, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_LED_SET \
    TIMECARD_IOCTL(29, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_SENSOR_QUERY \
    TIMECARD_IOCTL(30, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_MRO50_QUERY \
    TIMECARD_IOCTL(31, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_MRO50_CONTROL \
    TIMECARD_IOCTL(32, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_GET_CAPABILITIES \
    TIMECARD_IOCTL(33, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_PHASE_QUERY \
    TIMECARD_IOCTL(34, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_PHASE_CONTROL \
    TIMECARD_IOCTL(35, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_PHC_ADJUST \
    TIMECARD_IOCTL(36, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_DISCIPLINE_READ \
    TIMECARD_IOCTL(37, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_DISCIPLINE_WRITE \
    TIMECARD_IOCTL(38, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_GET_FPGA_CAPABILITIES \
    TIMECARD_IOCTL(39, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_CLOCK_TELEMETRY_QUERY \
    TIMECARD_IOCTL(40, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_PPS_QUERY \
    TIMECARD_IOCTL(41, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_PPS_SET \
    TIMECARD_IOCTL(42, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_TIMECODE_QUERY \
    TIMECARD_IOCTL(43, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_TIMECODE_SET \
    TIMECARD_IOCTL(44, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_TOD_QUERY \
    TIMECARD_IOCTL(45, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_TOD_SET \
    TIMECARD_IOCTL(46, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FPGA_IMAGE_QUERY \
    TIMECARD_IOCTL(47, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_DISCIPLINE_LEASE \
    TIMECARD_IOCTL(48, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_TIMESTAMP_QUERY \
    TIMECARD_IOCTL(49, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_TIMESTAMP_SET \
    TIMECARD_IOCTL(50, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_TIMESTAMP_READ \
    TIMECARD_IOCTL(51, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_CLOCK_ADJUST_QUERY \
    TIMECARD_IOCTL(52, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_CLOCK_ADJUST_SET \
    TIMECARD_IOCTL(53, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_CORE_INVENTORY_QUERY \
    TIMECARD_IOCTL(54, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_SIGNAL_EVENT_READ \
    TIMECARD_IOCTL(55, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FPGA_CONTRACT_QUERY \
    TIMECARD_IOCTL(56, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_FPGA_CONTRACT_SET \
    TIMECARD_IOCTL(57, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_NMEA_UTC_QUERY \
    TIMECARD_IOCTL(58, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_NMEA_UTC_SET \
    TIMECARD_IOCTL(59, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_CLOCK_ADVANCED_QUERY \
    TIMECARD_IOCTL(60, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_CLOCK_ADVANCED_SET \
    TIMECARD_IOCTL(61, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define TIMECARD_ABI_VERSION 15u
#define TIMECARD_LAYOUT_MSI  1u
#define TIMECARD_LAYOUT_MSIX 2u
#define TIMECARD_LAYOUT_ART  3u
#define TIMECARD_BOARD_PROFILE_FB        1u
#define TIMECARD_BOARD_PROFILE_ART       2u
#define TIMECARD_BOARD_PROFILE_CELESTICA 3u
#define TIMECARD_UART_COUNT  4u
#define TIMECARD_UART_MAX_TRANSFER 256u
#define TIMECARD_I2C_MAX_TRANSFER 255u

#define TIMECARD_UART_GNSS  0u
#define TIMECARD_UART_GNSS2 1u
#define TIMECARD_UART_MAC   2u
#define TIMECARD_UART_NMEA  3u

#define TIMECARD_MRO50_FLAG_PRESENT        (1u << 0)
#define TIMECARD_MRO50_FLAG_ENABLED        (1u << 1)
#define TIMECARD_MRO50_FLAG_LOCKED         (1u << 2)
#define TIMECARD_MRO50_FLAG_FINE_VALID     (1u << 3)
#define TIMECARD_MRO50_FLAG_COARSE_VALID   (1u << 4)
#define TIMECARD_MRO50_FLAG_SERIAL_ENABLED (1u << 5)

#define TIMECARD_MRO50_ACTION_QUERY         0u
#define TIMECARD_MRO50_ACTION_ADJUST_FINE   1u
#define TIMECARD_MRO50_ACTION_ADJUST_COARSE 2u
#define TIMECARD_MRO50_ACTION_SAVE_COARSE   3u
#define TIMECARD_MRO50_ACTION_SERIAL_ENABLE 4u
#define TIMECARD_MRO50_FINE_MINIMUM          0u
#define TIMECARD_MRO50_FINE_MAXIMUM          4800u
#define TIMECARD_MRO50_COARSE_MINIMUM        0u
#define TIMECARD_MRO50_COARSE_MAXIMUM        0x003fffffu

/* Capability discovery keeps user mode away from variant-specific MMIO. */
#define TIMECARD_CAP_PHC                    (1ull << 0)
#define TIMECARD_CAP_GNSS_UART              (1ull << 1)
#define TIMECARD_CAP_ATOMIC_UART            (1ull << 2)
#define TIMECARD_CAP_PAIRED_PHASE_METER     (1ull << 3)
#define TIMECARD_CAP_MRO50_DIRECT            (1ull << 4)
#define TIMECARD_CAP_PHC_PHASE_ADJUST        (1ull << 5)
#define TIMECARD_CAP_DISCIPLINE_PARAMETERS   (1ull << 6)
#define TIMECARD_CAP_TEMPERATURE_TELEMETRY   (1ull << 7)
#define TIMECARD_CAP_HARDWARE_DISCIPLINE     (1ull << 8)

#define TIMECARD_OSCILLATOR_NONE       0u
#define TIMECARD_OSCILLATOR_UART       1u
#define TIMECARD_OSCILLATOR_MRO50      2u
#define TIMECARD_OSCILLATOR_SA53       3u

#define TIMECARD_PHASE_FLAG_PRESENT           (1u << 0)
#define TIMECARD_PHASE_FLAG_ENABLED           (1u << 1)
#define TIMECARD_PHASE_FLAG_REFERENCE_VALID   (1u << 2)
#define TIMECARD_PHASE_FLAG_OSCILLATOR_VALID  (1u << 3)
#define TIMECARD_PHASE_FLAG_PHASE_VALID       (1u << 4)

#define TIMECARD_PHASE_CONTROL_DISABLE 0u
#define TIMECARD_PHASE_CONTROL_ENABLE  1u

#define TIMECARD_DISCIPLINE_EEPROM_SIZE 512u
#define TIMECARD_DISCIPLINE_FLAG_PRESENT (1u << 0)
#define TIMECARD_DISCIPLINE_FLAG_VALID   (1u << 1)

#define TIMECARD_DISCIPLINE_LEASE_QUERY   0u
#define TIMECARD_DISCIPLINE_LEASE_ACQUIRE 1u
#define TIMECARD_DISCIPLINE_LEASE_RELEASE 2u
#define TIMECARD_DISCIPLINE_LEASE_ACTIVE  (1u << 0)
#define TIMECARD_DISCIPLINE_LEASE_OWNER   (1u << 1)

#define TIMECARD_HIERARCHY_QUERY   0u
#define TIMECARD_HIERARCHY_ENABLE  1u
#define TIMECARD_HIERARCHY_DISABLE 2u

#define TIMECARD_SMA_COUNT 4u

#define TIMECARD_SMA_DIRECTION_INPUT    0u
#define TIMECARD_SMA_DIRECTION_OUTPUT   1u
#define TIMECARD_SMA_DIRECTION_DISABLED 2u

#define TIMECARD_SMA_FLAG_PRESENT         (1u << 0)
#define TIMECARD_SMA_FLAG_FIXED_DIRECTION (1u << 1)
#define TIMECARD_SMA_FLAG_DISABLED        (1u << 2)

#define TIMECARD_I2C_FLAG_PRESENT  (1u << 0)
#define TIMECARD_I2C_FLAG_ENABLED  (1u << 1)
#define TIMECARD_I2C_FLAG_BUS_BUSY (1u << 2)
#define TIMECARD_I2C_FLAG_RX_EMPTY (1u << 3)
#define TIMECARD_I2C_FLAG_TX_EMPTY (1u << 4)

#define TIMECARD_I2C_DEVICE_BOARD_EEPROM (1u << 0)
#define TIMECARD_I2C_DEVICE_MAC_EEPROM   (1u << 1)
#define TIMECARD_BOARD_EEPROM_ADDRESS    0x50u
#define TIMECARD_IDENTITY_ADDRESS        0x58u
#define TIMECARD_IDENTITY_EUI48_OFFSET   0x9au

/* U27, PCA9546A at 0x70.  More than one downstream branch may be selected. */
#define TIMECARD_I2C_MUX_ADDRESS         0x70u
#define TIMECARD_I2C_MUX_CHANNEL_MAC     (1u << 0)
#define TIMECARD_I2C_MUX_CHANNEL_SENSORS (1u << 1)
#define TIMECARD_I2C_MUX_CHANNEL_ANADC   (1u << 2)
#define TIMECARD_I2C_MUX_CHANNEL_DC      (1u << 3)
#define TIMECARD_I2C_MUX_CHANNEL_MASK    0x0fu

/* Celestica R4006-G0001-03-SC Rev 2.0 fixed TCA9546A channel map. */
#define TIMECARD_I2C_MUX_CELESTICA_LM75B    (1u << 0)
#define TIMECARD_I2C_MUX_CELESTICA_SHT3X    (1u << 1)
#define TIMECARD_I2C_MUX_CELESTICA_ICP10100 (1u << 2)
#define TIMECARD_I2C_MUX_CELESTICA_BNO08X   (1u << 3)

/*
 * U6, IS32FL3207 on the sensor branch.  The schematic's 0x6e label is
 * the 8-bit write address; Windows and the XIIC API use 7-bit address 0x37.
 */
#define TIMECARD_LED_ADDRESS_MIN 0x34u
#define TIMECARD_LED_ADDRESS_MAX 0x37u
#define TIMECARD_LED_ADDRESS 0x37u
#define TIMECARD_LED_COUNT 6u
#define TIMECARD_LED_GNSS1 0u
#define TIMECARD_LED_GNSS2 1u
#define TIMECARD_LED_IO1   2u
#define TIMECARD_LED_IO2   3u
#define TIMECARD_LED_IO3   4u
#define TIMECARD_LED_IO4   5u
#define TIMECARD_LED_FLAG_PRESENT (1u << 0)
#define TIMECARD_LED_FLAG_ENABLED (1u << 1)
#define TIMECARD_LED_FLAG_FAULT_VALID (1u << 2)
#define TIMECARD_LED_FLAG_DC_TEST (1u << 3)
#define TIMECARD_LED_FLAG_RESET_TEST (1u << 4)
#define TIMECARD_LED_FLAG_SDB_HIGH (1u << 5)
#define TIMECARD_LED_MAX_GLOBAL_CURRENT 128u
#define TIMECARD_LED_OUTPUT_MASK ((1u << 18) - 1u)

/* Dedicated telemetry devices on PCA9546A channel 1 (SENS_I2C). */
#define TIMECARD_SENSOR_BME280_ADDRESS 0x76u
#define TIMECARD_SENSOR_BME280_ADDRESS_ALTERNATE 0x77u
#define TIMECARD_SENSOR_BNO055_ADDRESS 0x29u
#define TIMECARD_SENSOR_BNO055_ADDRESS_ALTERNATE 0x28u
#define TIMECARD_SENSOR_BNO08X_ADDRESS 0x4au
#define TIMECARD_SENSOR_BNO08X_ADDRESS_ALTERNATE 0x4bu
#define TIMECARD_SENSOR_BNO08X_CHIP_ID 0x80u
#define TIMECARD_SENSOR_INA219_12V_ADDRESS 0x40u
#define TIMECARD_SENSOR_INA219_5V_ADDRESS 0x41u
#define TIMECARD_SENSOR_INA219_3V3_ADDRESS 0x44u
#define TIMECARD_SENSOR_INA219_SHUNT_MICROOHMS 2000u
#define TIMECARD_SENSOR_FLAG_PRESENT          (1u << 0)
#define TIMECARD_SENSOR_FLAG_VALID            (1u << 1)
#define TIMECARD_SENSOR_FLAG_CONFIGURED       (1u << 2)
#define TIMECARD_SENSOR_FLAG_CONVERSION_READY (1u << 3)
#define TIMECARD_SENSOR_FLAG_OVERFLOW         (1u << 4)
#define TIMECARD_SENSOR_FLAG_EXTERNAL_CLOCK   (1u << 5)
#define TIMECARD_SENSOR_FLAG_HUMIDITY         (1u << 6)
#define TIMECARD_SENSOR_FLAG_TEMPERATURE      (1u << 7)
#define TIMECARD_SENSOR_FLAG_TEMPERATURE_Q7   (1u << 8)
#define TIMECARD_SENSOR_FLAG_CRC_VALID        (1u << 9)

#define TIMECARD_SENSOR_CAP_BME280   (1u << 0)
#define TIMECARD_SENSOR_CAP_INA219   (1u << 1)
#define TIMECARD_SENSOR_CAP_BNO055   (1u << 2)
#define TIMECARD_SENSOR_CAP_BNO08X   (1u << 3)
#define TIMECARD_SENSOR_CAP_LM75B    (1u << 4)
#define TIMECARD_SENSOR_CAP_SHT3X    (1u << 5)
#define TIMECARD_SENSOR_CAP_ICP10100 (1u << 6)

#define TIMECARD_SENSOR_LM75B_COUNT 3u
#define TIMECARD_SENSOR_LM75B_1_ADDRESS 0x48u
#define TIMECARD_SENSOR_LM75B_2_ADDRESS 0x49u
#define TIMECARD_SENSOR_LM75B_3_ADDRESS 0x4au
#define TIMECARD_SENSOR_SHT3X_ADDRESS 0x44u
#define TIMECARD_SENSOR_ICP10100_ADDRESS 0x63u

#define TIMECARD_CLOCK_SOURCE_NONE 0x00u
#define TIMECARD_CLOCK_SOURCE_TOD  0x01u
#define TIMECARD_CLOCK_SOURCE_IRIG 0x02u
#define TIMECARD_CLOCK_SOURCE_PPS  0x03u
#define TIMECARD_CLOCK_SOURCE_PTP  0x04u
#define TIMECARD_CLOCK_SOURCE_RTC  0x05u
#define TIMECARD_CLOCK_SOURCE_DCF  0x06u
#define TIMECARD_CLOCK_SOURCE_NTP  0x07u
#define TIMECARD_CLOCK_SOURCE_SYNCE 0x08u
#define TIMECARD_CLOCK_SOURCE_DYN  0xfdu
#define TIMECARD_CLOCK_SOURCE_REGS 0xfeu
#define TIMECARD_CLOCK_SOURCE_EXT  0xffu

#define TIMECARD_CLOCK_ADJUST_FLAG_PRESENT            (1u << 0)
#define TIMECARD_CLOCK_ADJUST_FLAG_FRACTIONAL_DRIFT   (1u << 1)
#define TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET       (1u << 16)
#define TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT        (1u << 17)
#define TIMECARD_CLOCK_ADJUST_FLAG_APPLY_THRESHOLD    (1u << 18)
#define TIMECARD_CLOCK_ADJUST_MAX_OFFSET_NS 1000000000
#define TIMECARD_CLOCK_ADJUST_MAX_DRIFT_PPB 1000000

/*
 * Optional Adjustable Clock features.  These registers depend on FPGA
 * synthesis generics, so the driver exposes them only after an exact image
 * contract has been activated for the current card and image.
 */
#define TIMECARD_CLOCK_ADVANCED_FLAG_PRESENT          (1u << 0)
#define TIMECARD_CLOCK_ADVANCED_FLAG_RATE_LIMITERS    (1u << 1)
#define TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER         (1u << 2)
#define TIMECARD_CLOCK_ADVANCED_FLAG_OUTLIER_FILTERS  (1u << 3)
#define TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_FACTORS    (1u << 4)
#define TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_LOG        (1u << 5)
#define TIMECARD_CLOCK_ADVANCED_FLAG_AGING            (1u << 6)
#define TIMECARD_CLOCK_ADVANCED_FLAG_DYNAMIC_CONTROL  (1u << 7)
#define TIMECARD_CLOCK_ADVANCED_FLAG_REVERT           (1u << 8)
#define TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER_READY   (1u << 9)
#define TIMECARD_CLOCK_ADVANCED_FLAG_AGING_READY      (1u << 10)

#define TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS   (1u << 0)
#define TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER        (1u << 1)
#define TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS (1u << 2)
#define TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS   (1u << 3)
#define TIMECARD_CLOCK_ADVANCED_APPLY_AGING           (1u << 4)
#define TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL         (1u << 5)
#define TIMECARD_CLOCK_ADVANCED_APPLY_ALL             0x0000003fu

#define TIMECARD_CLOCK_LIMITER_ENABLE                 (1u << 31)
#define TIMECARD_CLOCK_LIMITER_VALUE_MASK             0x7fffffffu
#define TIMECARD_CLOCK_ADVANCED_CONTROL_HOLDOVER      (1u << 16)
#define TIMECARD_CLOCK_ADVANCED_CONTROL_AGING         (1u << 18)
#define TIMECARD_CLOCK_ADVANCED_CONTROL_REVERT        (1u << 19)
#define TIMECARD_CLOCK_ADVANCED_CONTROL_MASK          \
    (TIMECARD_CLOCK_ADVANCED_CONTROL_HOLDOVER | \
     TIMECARD_CLOCK_ADVANCED_CONTROL_AGING | \
     TIMECARD_CLOCK_ADVANCED_CONTROL_REVERT)

#define TIMECARD_NMEA_FLAG_PRESENT (1u << 0)
#define TIMECARD_NMEA_FLAG_ENABLED (1u << 1)
#define TIMECARD_NMEA_FLAG_ADVANCED_VALID (1u << 2)
#define TIMECARD_NMEA_FLAG_ERROR (1u << 3)
#define TIMECARD_NMEA_FLAG_CLEAR_ERROR (1u << 31)
#define TIMECARD_NMEA_GNSS_DEFAULT     0u
#define TIMECARD_NMEA_GNSS_COMBINED    1u
#define TIMECARD_NMEA_GNSS_GPS         2u
#define TIMECARD_NMEA_GNSS_GLONASS     3u
#define TIMECARD_NMEA_GNSS_GALILEO     4u
#define TIMECARD_NMEA_GNSS_BEIDOU      5u
#define TIMECARD_NMEA_GNSS_PROPRIETARY 15u
#define TIMECARD_NMEA_DISABLE_RMC (1u << 0)
#define TIMECARD_NMEA_DISABLE_ZDA (1u << 1)
#define TIMECARD_NMEA_DISABLE_UTC (1u << 2)

#define TIMECARD_NMEA_UTC_FLAG_PRESENT        (1u << 0)
#define TIMECARD_NMEA_UTC_FLAG_READ_SUPPORTED (1u << 1)
#define TIMECARD_NMEA_UTC_FLAG_WRITE_SUPPORTED (1u << 2)
#define TIMECARD_NMEA_UTC_FLAG_LEAP61         (1u << 3)
#define TIMECARD_NMEA_UTC_FLAG_LEAP59         (1u << 4)
#define TIMECARD_NMEA_UTC_FLAG_OFFSET_VALID   (1u << 5)
/* Public UART polarity is logical; gateware's raw encoding is the inverse. */
#define TIMECARD_UART_POLARITY_NORMAL   0u
#define TIMECARD_UART_POLARITY_INVERTED 1u

#define TIMECARD_IDENTITY_FLAG_PRESENT (1u << 0)
#define TIMECARD_IDENTITY_FLAG_VALID   (1u << 1)
#define TIMECARD_IDENTITY_SERIAL_LENGTH 6u

#define TIMECARD_SIGNAL_COUNT 4u
#define TIMECARD_SIGNAL_FLAG_PRESENT  (1u << 0)
#define TIMECARD_SIGNAL_FLAG_ENABLED  (1u << 1)
#define TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH (1u << 2)
/* Source-compatible legacy name; bit 2 has always selected active-high. */
#define TIMECARD_SIGNAL_FLAG_INVERTED TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH
#define TIMECARD_SIGNAL_FLAG_ERROR     (1u << 3)
#define TIMECARD_SIGNAL_FLAG_TIME_JUMP (1u << 4)
/* Set request only: use StartSeconds/StartNanoseconds instead of phase alignment. */
#define TIMECARD_SIGNAL_FLAG_ABSOLUTE_START (1u << 5)
/* Set request only: clear the documented sticky Error/TimeJump status bits. */
#define TIMECARD_SIGNAL_FLAG_CLEAR_STATUS (1u << 6)
#define TIMECARD_SIGNAL_FLAG_COMPLETION_IRQ_AVAILABLE (1u << 7)
#define TIMECARD_SIGNAL_FLAG_COMPLETION_PENDING       (1u << 8)
#define TIMECARD_SIGNAL_FLAG_COMPLETION_OVERFLOW      (1u << 9)

#define TIMECARD_SIGNAL_EVENT_QUEUE_LENGTH 32u
#define TIMECARD_SIGNAL_EVENT_MAX_BATCH    16u
#define TIMECARD_SIGNAL_EVENT_FLAG_COMPLETED (1u << 0)
#define TIMECARD_SIGNAL_EVENT_FLAG_ERROR     (1u << 1)
#define TIMECARD_SIGNAL_EVENT_FLAG_TIME_JUMP (1u << 2)
#define TIMECARD_SIGNAL_EVENT_FLAG_OVERFLOW  (1u << 3)

/*
 * The standard Meta/Celestica image contains five Signal Timestamper inputs
 * plus the PHC/PPS timestamp channel.  Channel numbers below deliberately
 * match Linux PTP_EXTTS indices 0..5.
 */
#define TIMECARD_TIMESTAMP_COUNT 6u
#define TIMECARD_TIMESTAMP_QUEUE_LENGTH 128u
#define TIMECARD_TIMESTAMP_MAX_DATA_BYTES 32u
#define TIMECARD_TIMESTAMP_MAX_BATCH 16u

#define TIMECARD_TIMESTAMP_GNSS1 0u
#define TIMECARD_TIMESTAMP_TS1   1u
#define TIMECARD_TIMESTAMP_TS2   2u
#define TIMECARD_TIMESTAMP_TS3   3u
#define TIMECARD_TIMESTAMP_TS4   4u
#define TIMECARD_TIMESTAMP_PHC   5u

/* Public polarity is logical: zero rising/normal, one falling/inverted. */
#define TIMECARD_TIMESTAMP_POLARITY_RISING  0u
#define TIMECARD_TIMESTAMP_POLARITY_FALLING 1u

#define TIMECARD_TIMESTAMP_FLAG_PRESENT          (1u << 0)
#define TIMECARD_TIMESTAMP_FLAG_ENABLED          (1u << 1)
#define TIMECARD_TIMESTAMP_FLAG_DROP_ERROR       (1u << 2)
#define TIMECARD_TIMESTAMP_FLAG_EVENT_VALID      (1u << 3)
#define TIMECARD_TIMESTAMP_FLAG_DATA_VALID       (1u << 4)
#define TIMECARD_TIMESTAMP_FLAG_IRQ_AVAILABLE    (1u << 5)
#define TIMECARD_TIMESTAMP_FLAG_QUEUE_OVERFLOW   (1u << 6)
#define TIMECARD_TIMESTAMP_FLAG_DATA_TRUNCATED   (1u << 7)
#define TIMECARD_TIMESTAMP_FLAG_CABLE_DELAY_WRITABLE (1u << 8)
#define TIMECARD_TIMESTAMP_FLAG_COUNTERS_AVAILABLE   (1u << 9)
#define TIMECARD_TIMESTAMP_FLAG_DATA_AVAILABLE       (1u << 10)
/* Set-request-only write-one-to-clear operations. */
#define TIMECARD_TIMESTAMP_FLAG_CLEAR_ERROR      (1u << 30)
#define TIMECARD_TIMESTAMP_FLAG_CLEAR_QUEUE      (1u << 31)

/* Documented FPGA cores exposed by the standard Meta/Celestica maps. */
#define TIMECARD_FPGA_CORE_PPS_MASTER       (1u << 0)
#define TIMECARD_FPGA_CORE_PPS_SLAVE        (1u << 1)
#define TIMECARD_FPGA_CORE_IRIG_MASTER      (1u << 2)
#define TIMECARD_FPGA_CORE_IRIG_SLAVE       (1u << 3)
#define TIMECARD_FPGA_CORE_DCF_MASTER       (1u << 4)
#define TIMECARD_FPGA_CORE_DCF_SLAVE        (1u << 5)
#define TIMECARD_FPGA_CORE_TOD_SLAVE        (1u << 6)
#define TIMECARD_FPGA_CORE_TOD_MASTER       (1u << 7)
#define TIMECARD_FPGA_CORE_SIGNAL_GENERATOR (1u << 8)
#define TIMECARD_FPGA_CORE_FREQUENCY_INPUT  (1u << 9)
#define TIMECARD_FPGA_CORE_SIGNAL_TIMESTAMPER (1u << 10)

#define TIMECARD_FPGA_FEATURE_PPS_CONFIGURATION      (1u << 0)
#define TIMECARD_FPGA_FEATURE_TIMECODE_CONFIGURATION (1u << 1)
#define TIMECARD_FPGA_FEATURE_TOD_CONFIGURATION      (1u << 2)
#define TIMECARD_FPGA_FEATURE_CLOCK_TELEMETRY        (1u << 3)
#define TIMECARD_FPGA_FEATURE_SIGNAL_REPEAT_COUNT    (1u << 4)
#define TIMECARD_FPGA_FEATURE_SIGNAL_CABLE_DELAY     (1u << 5)
#define TIMECARD_FPGA_FEATURE_SIGNAL_STATUS          (1u << 6)
#define TIMECARD_FPGA_FEATURE_TIMESTAMP_CAPTURE      (1u << 7)
#define TIMECARD_FPGA_FEATURE_STATIC_CORE_INVENTORY  (1u << 8)
#define TIMECARD_FPGA_FEATURE_SIGNAL_COMPLETION_EVENTS (1u << 9)
#define TIMECARD_FPGA_FEATURE_EXACT_IMAGE_CONTRACT      (1u << 10)
#define TIMECARD_FPGA_FEATURE_CLOCK_SMOOTH_ADJUST       (1u << 11)
#define TIMECARD_FPGA_FEATURE_CLOCK_ADVANCED_CONFIGURATION (1u << 12)
#define TIMECARD_FPGA_FEATURE_TOD_MASTER_UTC            (1u << 13)
#define TIMECARD_FPGA_FEATURE_TIMECODE_ADVANCED         (1u << 14)

#define TIMECARD_FPGA_CONTRACT_ACKNOWLEDGEMENT 0x54434d46u /* "TCMF" */
#define TIMECARD_FPGA_CONTRACT_CLOCK_SERVO_LOG    (1u << 0)
#define TIMECARD_FPGA_CONTRACT_CLOCK_ADVANCED     (1u << 1)
#define TIMECARD_FPGA_CONTRACT_TOD_TELEMETRY      (1u << 2)
#define TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ  (1u << 3)
#define TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_WRITE (1u << 4)
#define TIMECARD_FPGA_CONTRACT_IRIG_MASTER_AM     (1u << 5)
#define TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_AM      (1u << 6)
#define TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_YEAR    (1u << 7)
#define TIMECARD_FPGA_CONTRACT_NTP_SOURCE         (1u << 8)
#define TIMECARD_FPGA_CONTRACT_ART_TIMESTAMP_EXTENDED (1u << 9)
#define TIMECARD_FPGA_CONTRACT_SYNCE_SOURCE       (1u << 10)
#define TIMECARD_FPGA_CONTRACT_DYNAMIC_SOURCE     (1u << 11)
#define TIMECARD_FPGA_CONTRACT_ALL_FLAGS          0x00000fffu

#define TIMECARD_FPGA_CONTRACT_FLAG_IMAGE_PRESENT (1u << 0)
#define TIMECARD_FPGA_CONTRACT_FLAG_EXACT_MATCH   (1u << 1)
#define TIMECARD_FPGA_CONTRACT_FLAG_ACTIVE        (1u << 2)

/* Trusted, static replacement for an FPGA Configuration Slave ROM. */
#define TIMECARD_CORE_INVENTORY_MAX 32u
#define TIMECARD_CORE_INTERRUPT_NONE 0xffffffffu
#define TIMECARD_CORE_TYPE_CLOCK             1u
#define TIMECARD_CORE_TYPE_IMAGE_IDENTITY    2u
#define TIMECARD_CORE_TYPE_SIGNAL_TIMESTAMPER 3u
#define TIMECARD_CORE_TYPE_PPS_MASTER        4u
#define TIMECARD_CORE_TYPE_PPS_SLAVE         5u
#define TIMECARD_CORE_TYPE_TOD_SLAVE         6u
#define TIMECARD_CORE_TYPE_TOD_MASTER        7u
#define TIMECARD_CORE_TYPE_IRIG_MASTER       8u
#define TIMECARD_CORE_TYPE_IRIG_SLAVE        9u
#define TIMECARD_CORE_TYPE_DCF_MASTER       10u
#define TIMECARD_CORE_TYPE_DCF_SLAVE        11u
#define TIMECARD_CORE_TYPE_SIGNAL_GENERATOR 12u
#define TIMECARD_CORE_TYPE_FREQUENCY_INPUT  13u

#define TIMECARD_CORE_FLAG_TRUSTED_PROFILE   (1u << 0)
#define TIMECARD_CORE_FLAG_REGISTER_MAPPED   (1u << 1)
#define TIMECARD_CORE_FLAG_VERSION_VALID     (1u << 2)
#define TIMECARD_CORE_FLAG_INTERRUPT         (1u << 3)
#define TIMECARD_CORE_FLAG_OPERATOR_ONLY     (1u << 4)
#define TIMECARD_CORE_FLAG_CURRENT_LAYOUT    (1u << 5)
#define TIMECARD_CORE_FLAG_BASIC_SURFACE     (1u << 6)
#define TIMECARD_CORE_FLAG_SYNTHESIS_UNKNOWN (1u << 7)

#define TIMECARD_INVENTORY_FLAG_STATIC_PROFILE  (1u << 0)
#define TIMECARD_INVENTORY_FLAG_NO_CONFIG_SLAVE (1u << 1)
#define TIMECARD_INVENTORY_FLAG_IMAGE_PRESENT   (1u << 2)
#define TIMECARD_INVENTORY_FLAG_SYNTHESIS_UNKNOWN (1u << 3)

/* Hardware/image-contract limitations that software cannot safely infer. */
#define TIMECARD_FPGA_GAP_TIMESTAMP_INTERRUPTS       (1u << 0)
#define TIMECARD_FPGA_GAP_CONFIGURATION_SLAVE        (1u << 1)
#define TIMECARD_FPGA_GAP_OPTIONAL_CLOCK_REGISTERS   (1u << 2)
#define TIMECARD_FPGA_GAP_TOD_MASTER_UTC_HANDSHAKE   (1u << 3)
#define TIMECARD_FPGA_GAP_SYNTHESIS_FEATURE_REPORTING (1u << 4)

/* Read-only identity decoded from the trusted static image-version resource. */
#define TIMECARD_FPGA_IMAGE_FLAG_PRESENT       (1u << 0)
#define TIMECARD_FPGA_IMAGE_FLAG_LOADER        (1u << 1)
#define TIMECARD_FPGA_IMAGE_FLAG_FPGA_FIRMWARE (1u << 2)

#define TIMECARD_CLOCK_TELEMETRY_FLAG_PRESENT          (1u << 0)
#define TIMECARD_CLOCK_TELEMETRY_FLAG_IN_SYNC          (1u << 1)
#define TIMECARD_CLOCK_TELEMETRY_FLAG_IN_HOLDOVER      (1u << 2)
#define TIMECARD_CLOCK_TELEMETRY_FLAG_SERVO_AVAILABLE  (1u << 3)
#define TIMECARD_CLOCK_TELEMETRY_FLAG_LOG_AVAILABLE    (1u << 4)
#define TIMECARD_CLOCK_TELEMETRY_FLAG_FRACTIONAL_LOG   (1u << 5)

#define TIMECARD_PPS_CORE_MASTER 1u
#define TIMECARD_PPS_CORE_SLAVE  2u
#define TIMECARD_PPS_POLARITY_ACTIVE_LOW  0u
#define TIMECARD_PPS_POLARITY_ACTIVE_HIGH 1u
#define TIMECARD_PPS_FLAG_PRESENT              (1u << 0)
#define TIMECARD_PPS_FLAG_ENABLED              (1u << 1)
#define TIMECARD_PPS_FLAG_ERROR                (1u << 2)
#define TIMECARD_PPS_FLAG_FILTER_ERROR         (1u << 3)
#define TIMECARD_PPS_FLAG_SUPERVISION_ERROR    (1u << 4)
#define TIMECARD_PPS_FLAG_PULSE_WIDTH_WRITABLE (1u << 5)
#define TIMECARD_PPS_FLAG_CLEAR_ERRORS         (1u << 31)

#define TIMECARD_TIMECODE_FORMAT_IRIG 1u
#define TIMECARD_TIMECODE_FORMAT_DCF  2u
#define TIMECARD_TIMECODE_ROLE_MASTER 1u
#define TIMECARD_TIMECODE_ROLE_SLAVE  2u
#define TIMECARD_IRIG_MODE_NONE 0u
#define TIMECARD_IRIG_MODE_B    1u
#define TIMECARD_IRIG_MODE_G    2u
#define TIMECARD_TIMECODE_FLAG_PRESENT               (1u << 0)
#define TIMECARD_TIMECODE_FLAG_ENABLED               (1u << 1)
#define TIMECARD_TIMECODE_FLAG_ERROR                 (1u << 2)
#define TIMECARD_TIMECODE_FLAG_DELAY_WRITABLE        (1u << 3)
#define TIMECARD_TIMECODE_FLAG_CONTROL_BITS_WRITABLE (1u << 4)
#define TIMECARD_TIMECODE_FLAG_AM_WRITABLE           (1u << 5)
#define TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE         (1u << 6)
#define TIMECARD_TIMECODE_FLAG_CLEAR_ERRORS          (1u << 31)

#define TIMECARD_TOD_PROTOCOL_NMEA 0u
#define TIMECARD_TOD_PROTOCOL_UBX  1u
#define TIMECARD_TOD_PROTOCOL_TSIP 2u
#define TIMECARD_TOD_PROTOCOL_ESIP 3u
#define TIMECARD_TOD_PROTOCOL_PFEC 4u
#define TIMECARD_TOD_GNSS_ALL      0u
#define TIMECARD_TOD_GNSS_COMBINED 1u
#define TIMECARD_TOD_GNSS_GPS      2u
#define TIMECARD_TOD_GNSS_GLONASS  3u
#define TIMECARD_TOD_GNSS_GALILEO  4u
#define TIMECARD_TOD_GNSS_BEIDOU   5u
#define TIMECARD_TOD_DISABLE_RMC        (1u << 0)
#define TIMECARD_TOD_DISABLE_ZDA        (1u << 1)
#define TIMECARD_TOD_DISABLE_STATUS     (1u << 2)
#define TIMECARD_TOD_DISABLE_POSITION   (1u << 3)
#define TIMECARD_TOD_DISABLE_SATELLITES (1u << 4)
#define TIMECARD_TOD_DISABLE_ESIP_CRW   (1u << 5)
#define TIMECARD_TOD_DISABLE_ESIP_CRY   (1u << 6)
#define TIMECARD_TOD_DISABLE_ESIP_CRJ   (1u << 7)
#define TIMECARD_TOD_FLAG_PRESENT       (1u << 0)
#define TIMECARD_TOD_FLAG_ENABLED       (1u << 1)
#define TIMECARD_TOD_FLAG_PARSE_ERROR   (1u << 2)
#define TIMECARD_TOD_FLAG_CHECKSUM_ERROR (1u << 3)
#define TIMECARD_TOD_FLAG_UART_ERROR    (1u << 4)
#define TIMECARD_TOD_FLAG_UTC_TELEMETRY_VALID  (1u << 5)
#define TIMECARD_TOD_FLAG_GNSS_TELEMETRY_VALID (1u << 6)
#define TIMECARD_TOD_FLAG_CLEAR_ERRORS  (1u << 31)

#define TIMECARD_FREQUENCY_COUNT 4u
#define TIMECARD_FREQUENCY_FLAG_PRESENT (1u << 0)
#define TIMECARD_FREQUENCY_FLAG_ENABLED (1u << 1)
#define TIMECARD_FREQUENCY_FLAG_VALID   (1u << 2)
#define TIMECARD_FREQUENCY_FLAG_ERROR   (1u << 3)
#define TIMECARD_FREQUENCY_FLAG_OVERRUN (1u << 4)

#define TIMECARD_FLASH_MAX_TRANSFER 256u
#define TIMECARD_FLASH_FIRMWARE_OFFSET 0x00400000u
#define TIMECARD_FLASH_ERASE_SIZE 4096u
#define TIMECARD_FLASH_PAGE_SIZE 256u
#define TIMECARD_FLASH_FLAG_PRESENT    (1u << 0)
#define TIMECARD_FLASH_FLAG_IDENTIFIED (1u << 1)
#define TIMECARD_FLASH_FLAG_SUPPORTED  (1u << 2)
#define TIMECARD_FLASH_FLAG_FOUR_BYTE  (1u << 3)

#define TIMECARD_UART_OBSERVE_FLAG_PRESENT  (1u << 0)
#define TIMECARD_UART_OBSERVE_FLAG_ACTIVITY (1u << 1)

/* Function selectors match the FPGA SMA maps and the Linux ptp_ocp ABI. */
#define TIMECARD_SMA_INPUT_10MHZ 0x0000u
#define TIMECARD_SMA_INPUT_PPS1  0x0001u
#define TIMECARD_SMA_INPUT_PPS2  0x0002u
#define TIMECARD_SMA_INPUT_TS1   0x0004u
#define TIMECARD_SMA_INPUT_TS2   0x0008u
#define TIMECARD_SMA_INPUT_IRIG  0x0010u
#define TIMECARD_SMA_INPUT_DCF   0x0020u
#define TIMECARD_SMA_INPUT_TS3   0x0040u
#define TIMECARD_SMA_INPUT_TS4   0x0080u
#define TIMECARD_SMA_INPUT_FREQ1 0x0100u
#define TIMECARD_SMA_INPUT_FREQ2 0x0200u
#define TIMECARD_SMA_INPUT_FREQ3 0x0400u
#define TIMECARD_SMA_INPUT_FREQ4 0x0800u

#define TIMECARD_SMA_OUTPUT_10MHZ 0x0000u
#define TIMECARD_SMA_OUTPUT_PHC   0x0001u
#define TIMECARD_SMA_OUTPUT_MAC   0x0002u
#define TIMECARD_SMA_OUTPUT_GNSS1 0x0004u
#define TIMECARD_SMA_OUTPUT_GNSS2 0x0008u
#define TIMECARD_SMA_OUTPUT_IRIG  0x0010u
#define TIMECARD_SMA_OUTPUT_DCF   0x0020u
#define TIMECARD_SMA_OUTPUT_GEN1  0x0040u
#define TIMECARD_SMA_OUTPUT_GEN2  0x0080u
#define TIMECARD_SMA_OUTPUT_GEN3  0x0100u
#define TIMECARD_SMA_OUTPUT_GEN4  0x0200u
#define TIMECARD_SMA_OUTPUT_GND   0x2000u
#define TIMECARD_SMA_OUTPUT_VCC   0x4000u

typedef struct _TIMECARD_TIME {
    unsigned __int64 Seconds;
    unsigned __int32 Nanoseconds;
    unsigned __int32 Reserved;
} TIMECARD_TIME;

/* System time values use Windows FILETIME units: 100 ns since 1601 UTC. */
typedef struct _TIMECARD_CROSSTIMESTAMP {
    TIMECARD_TIME CardTime;
    unsigned __int64 SystemTimeBefore100ns;
    unsigned __int64 SystemTimeAfter100ns;
} TIMECARD_CROSSTIMESTAMP;

typedef struct _TIMECARD_INFO {
    unsigned __int32 AbiVersion;
    unsigned __int32 DriverVersion;
    unsigned __int32 Layout;
    unsigned __int32 InterruptMessages;
    unsigned __int32 BarLength;
    unsigned __int32 ClockOffset;
    unsigned __int32 ClockVersion;
    unsigned __int32 ClockStatus;
    unsigned __int32 ClockSelect;
    unsigned __int32 TodVersion;
    unsigned __int32 TodStatus;
    unsigned __int32 UtcStatus;
    unsigned __int32 Leap;
    unsigned __int32 GnssStatus;
    unsigned __int32 Satellites;
} TIMECARD_INFO;

typedef struct _TIMECARD_UART_CONFIG {
    unsigned __int32 Port;
    unsigned __int32 Baud;
} TIMECARD_UART_CONFIG;

typedef struct _TIMECARD_UART_READ_REQUEST {
    unsigned __int32 Port;
    unsigned __int32 MaximumBytes;
    unsigned __int32 TimeoutMilliseconds;
    unsigned __int32 Reserved;
} TIMECARD_UART_READ_REQUEST;

typedef struct _TIMECARD_UART_TRANSFER {
    unsigned __int32 Port;
    unsigned __int32 Length;
    unsigned __int32 TimeoutMilliseconds;
    unsigned __int32 LineStatus;
    unsigned char Data[TIMECARD_UART_MAX_TRANSFER];
} TIMECARD_UART_TRANSFER;

typedef struct _TIMECARD_UART_RESULT {
    unsigned __int32 BytesTransferred;
    unsigned __int32 LineStatus;
} TIMECARD_UART_RESULT;

typedef struct _TIMECARD_HIERARCHY_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Action;
    unsigned __int32 Persist;
    unsigned __int32 RuntimeEnabled;
    unsigned __int32 Persisted;
    unsigned __int32 Reserved[3];
} TIMECARD_HIERARCHY_CONTROL;

typedef struct _TIMECARD_SMA_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Connector;
    unsigned __int32 Direction;
    unsigned __int32 Function;
    unsigned __int32 Flags;
    unsigned __int32 InputMap;
    unsigned __int32 OutputMap;
    unsigned __int32 Reserved;
} TIMECARD_SMA_CONTROL;

typedef struct _TIMECARD_I2C_STATUS {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Offset;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 InterruptStatus;
    unsigned __int32 InterruptEnable;
    unsigned __int32 TxFifoOccupancy;
    unsigned __int32 RxFifoOccupancy;
    unsigned __int32 KnownDeviceMask;
    unsigned __int32 Reserved[2];
} TIMECARD_I2C_STATUS;

typedef struct _TIMECARD_I2C_PROBE {
    unsigned __int32 Size;
    unsigned __int32 Address;
    unsigned __int32 Present;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    unsigned __int32 Reserved[3];
} TIMECARD_I2C_PROBE;

/* Subaddress is transmitted most-significant byte first. */
typedef struct _TIMECARD_I2C_READ_REQUEST {
    unsigned __int32 Size;
    unsigned __int32 Address;
    unsigned __int32 SubaddressLength;
    unsigned __int32 Subaddress;
    unsigned __int32 Length;
    unsigned __int32 TimeoutMilliseconds;
    unsigned __int32 Reserved[2];
} TIMECARD_I2C_READ_REQUEST;

typedef struct _TIMECARD_I2C_TRANSFER {
    unsigned __int32 Size;
    unsigned __int32 Address;
    unsigned __int32 Length;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    unsigned char Data[TIMECARD_I2C_MAX_TRANSFER];
} TIMECARD_I2C_TRANSFER;

typedef struct _TIMECARD_I2C_MUX_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 ChannelMask;
    unsigned __int32 Present;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    unsigned __int32 Reserved[3];
} TIMECARD_I2C_MUX_CONTROL;

typedef struct _TIMECARD_LED_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Led;
    unsigned __int32 Flags;
    unsigned __int32 Red;
    unsigned __int32 Green;
    unsigned __int32 Blue;
    unsigned __int32 GlobalCurrent;
    unsigned __int32 MuxChannelMask;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    /* 18-bit OUT1..OUT18 diagnostic masks from registers 0x72..0x74. */
    unsigned __int32 OpenOutputMask;
    unsigned __int32 ShortOutputMask;
} TIMECARD_LED_CONTROL;

/* BME280 raw sample and factory calibration; all ABI fields are 32-bit. */
typedef struct _TIMECARD_BME280_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 ChipId;
    unsigned __int32 Status;
    signed __int32 RawTemperature;
    unsigned __int32 RawPressure;
    unsigned __int32 RawHumidity;
    unsigned __int32 DigT1;
    signed __int32 DigT2;
    signed __int32 DigT3;
    unsigned __int32 DigP1;
    signed __int32 DigP2;
    signed __int32 DigP3;
    signed __int32 DigP4;
    signed __int32 DigP5;
    signed __int32 DigP6;
    signed __int32 DigP7;
    signed __int32 DigP8;
    signed __int32 DigP9;
    unsigned __int32 DigH1;
    signed __int32 DigH2;
    unsigned __int32 DigH3;
    signed __int32 DigH4;
    signed __int32 DigH5;
    signed __int32 DigH6;
} TIMECARD_BME280_READING;

typedef struct _TIMECARD_INA219_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Address;
    unsigned __int32 BusMillivolts;
    signed __int32 ShuntMicrovolts;
    signed __int32 CurrentMilliamps;
    signed __int32 PowerMilliwatts;
    unsigned __int32 Configuration;
    unsigned __int32 RawBus;
} TIMECARD_INA219_READING;

typedef struct _TIMECARD_LM75B_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Address;
    signed __int32 RawTemperature;
    signed __int32 TemperatureMilliCelsius;
    unsigned __int32 Reserved[3];
} TIMECARD_LM75B_READING;

typedef struct _TIMECARD_SHT3X_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Address;
    unsigned __int32 Status;
    unsigned __int32 RawTemperature;
    unsigned __int32 RawHumidity;
    signed __int32 TemperatureMilliCelsius;
    unsigned __int32 HumidityMilliPercent;
} TIMECARD_SHT3X_READING;

typedef struct _TIMECARD_ICP10100_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Address;
    unsigned __int32 ProductId;
    unsigned __int32 RawPressure;
    unsigned __int32 RawTemperature;
    signed __int32 TemperatureMilliCelsius;
    signed __int32 Otp[4];
    unsigned __int32 Reserved;
} TIMECARD_ICP10100_READING;

/* BNO055 raw axes use the scale selected by UnitSelection. */
typedef struct _TIMECARD_BNO055_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 ChipId;
    unsigned __int32 OperationMode;
    unsigned __int32 PowerMode;
    unsigned __int32 UnitSelection;
    unsigned __int32 SystemStatus;
    unsigned __int32 SystemError;
    unsigned __int32 Calibration;
    unsigned __int32 SelfTest;
    unsigned __int32 SystemClockStatus;
    signed __int32 Temperature;
    signed __int32 AccelerationX;
    signed __int32 AccelerationY;
    signed __int32 AccelerationZ;
    signed __int32 MagneticX;
    signed __int32 MagneticY;
    signed __int32 MagneticZ;
    signed __int32 GyroscopeX;
    signed __int32 GyroscopeY;
    signed __int32 GyroscopeZ;
    signed __int32 Heading;
    signed __int32 Roll;
    signed __int32 Pitch;
    signed __int32 QuaternionW;
    signed __int32 QuaternionX;
    signed __int32 QuaternionY;
    signed __int32 QuaternionZ;
    signed __int32 LinearAccelerationX;
    signed __int32 LinearAccelerationY;
    signed __int32 LinearAccelerationZ;
    signed __int32 GravityX;
    signed __int32 GravityY;
    signed __int32 GravityZ;
} TIMECARD_BNO055_READING;

typedef struct _TIMECARD_SENSOR_TELEMETRY {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 MuxChannelMask;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    TIMECARD_BME280_READING Environment;
    TIMECARD_INA219_READING Rail12V;
    TIMECARD_INA219_READING Rail5V;
    TIMECARD_INA219_READING Rail3V3;
    TIMECARD_BNO055_READING Imu;
    unsigned __int32 BoardProfile;
    unsigned __int32 Capabilities;
    TIMECARD_LM75B_READING BoardTemperature[TIMECARD_SENSOR_LM75B_COUNT];
    TIMECARD_SHT3X_READING Humidity;
    TIMECARD_ICP10100_READING Pressure;
} TIMECARD_SENSOR_TELEMETRY;

/*
 * Orolia/Safran ART mRO-50 FPGA bridge. Temperature is the raw gateware
 * telemetry word; its physical scaling depends on the installed ART image.
 */
typedef struct _TIMECARD_MRO50_STATUS {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Control;
    unsigned __int32 FineAdjustment;
    unsigned __int32 CoarseAdjustment;
    unsigned __int32 Temperature;
    unsigned __int32 BoardConfig;
    unsigned __int32 Reserved;
} TIMECARD_MRO50_STATUS;

typedef struct _TIMECARD_MRO50_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Action;
    unsigned __int32 Value;
    unsigned __int32 Reserved;
} TIMECARD_MRO50_CONTROL;

typedef struct _TIMECARD_CAPABILITIES {
    unsigned __int32 Size;
    unsigned __int32 AbiVersion;
    unsigned __int64 Flags;
    unsigned __int32 BoardProfile;
    unsigned __int32 OscillatorType;
    unsigned __int32 ReferencePpsIndex;
    unsigned __int32 OscillatorPpsIndex;
    unsigned __int32 FineMinimum;
    unsigned __int32 FineMaximum;
    unsigned __int32 CoarseMinimum;
    unsigned __int32 CoarseMaximum;
    unsigned __int32 Reserved[4];
} TIMECARD_CAPABILITIES;

/* Latest PPS timestamps latched by gateware. Phase is oscillator - reference. */
typedef struct _TIMECARD_PHASE_SAMPLE {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 ReferenceCounter;
    unsigned __int32 OscillatorCounter;
    TIMECARD_TIME ReferenceTime;
    TIMECARD_TIME OscillatorTime;
    signed __int64 PhaseNanoseconds;
    unsigned __int32 ReferenceError;
    unsigned __int32 OscillatorError;
    unsigned __int32 Reserved[4];
} TIMECARD_PHASE_SAMPLE;

typedef struct _TIMECARD_PHASE_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Action;
    unsigned __int32 ReferencePolarity;
    unsigned __int32 OscillatorPolarity;
    unsigned __int32 Enabled;
    unsigned __int32 Reserved[3];
} TIMECARD_PHASE_CONTROL;

typedef struct _TIMECARD_PHC_ADJUST {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    signed __int64 OffsetNanoseconds;
    TIMECARD_TIME ResultingTime;
    unsigned __int32 Reserved[4];
} TIMECARD_PHC_ADJUST;

/* Raw ART EEPROM map: config at 0x000, temperature table at 0x090. */
typedef struct _TIMECARD_DISCIPLINE_BLOB {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Length;
    unsigned __int32 Reserved;
    unsigned char Data[TIMECARD_DISCIPLINE_EEPROM_SIZE];
} TIMECARD_DISCIPLINE_BLOB;

typedef struct _TIMECARD_DISCIPLINE_LEASE {
    unsigned __int32 Size;
    unsigned __int32 Action;
    unsigned __int32 Flags;
    unsigned __int32 Reserved0;
    unsigned __int64 Reserved[2];
} TIMECARD_DISCIPLINE_LEASE;

typedef struct _TIMECARD_CLOCK_SOURCE_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Source;
    unsigned __int32 ActiveSource;
    unsigned __int32 Reserved[5];
} TIMECARD_CLOCK_SOURCE_CONTROL;

/* DriftPpbQ16 is signed ppb in Q16.16 fixed-point representation. */
typedef struct _TIMECARD_CLOCK_ADJUSTMENT {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Version;
    unsigned __int32 Control;
    unsigned __int32 Select;
    signed __int32 OffsetNanoseconds;
    unsigned __int32 OffsetIntervalNanoseconds;
    signed __int64 DriftPpbQ16;
    unsigned __int32 DriftIntervalNanoseconds;
    unsigned __int32 InSyncThresholdNanoseconds;
    unsigned __int32 AppliedFlags;
    unsigned __int32 Reserved[7];
} TIMECARD_CLOCK_ADJUSTMENT;

/* Raw fixed-point/register values are retained to avoid lossy round trips. */
typedef struct _TIMECARD_CLOCK_ADVANCED_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Version;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 ApplyFlags;
    unsigned __int32 OffsetRateLimiter;
    unsigned __int32 DriftRateLimiterQ16;
    unsigned __int32 AgingConfiguration;
    unsigned __int32 HoldoverConfiguration;
    unsigned __int32 OffsetOutlierFilter;
    unsigned __int32 DriftOutlierFilter;
    unsigned __int32 DynamicControl;
    unsigned __int32 ServoOffsetP;
    unsigned __int32 ServoOffsetI;
    unsigned __int32 ServoDriftP;
    unsigned __int32 ServoDriftI;
    unsigned __int32 StatusOffset;
    unsigned __int32 StatusDrift;
    unsigned __int32 StatusOffsetFraction;
    unsigned __int32 StatusDriftFraction;
    unsigned __int32 StatusHoldover;
    unsigned __int32 StatusHoldoverFraction;
    unsigned __int32 StatusHoldoverSamples;
    unsigned __int32 StatusOffsetOutliers;
    unsigned __int32 StatusDriftOutliers;
    unsigned __int32 StatusAgingLow;
    unsigned __int32 StatusAgingHigh;
    unsigned __int32 StatusAgingSamples;
    unsigned __int32 Reserved[3];
} TIMECARD_CLOCK_ADVANCED_CONTROL;

typedef struct _TIMECARD_NMEA_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Baud;
    unsigned __int32 BaudSelector;
    unsigned __int32 Polarity;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 Version;
    signed __int32 CorrectionSeconds;
    signed __int32 LocalOffsetMinutes;
    unsigned __int32 Gnss;
    unsigned __int32 MessageDisableMask;
} TIMECARD_NMEA_CONTROL;

typedef struct _TIMECARD_NMEA_UTC_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Version;
    unsigned __int32 RawUtcInfo;
    unsigned __int32 UtcOffsetSeconds;
    unsigned __int32 HandshakeControl;
    unsigned __int32 Reserved[10];
} TIMECARD_NMEA_UTC_CONTROL;

typedef struct _TIMECARD_IDENTITY {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned char Serial[TIMECARD_IDENTITY_SERIAL_LENGTH];
    unsigned char Reserved[2];
} TIMECARD_IDENTITY;

/* Generator numbers are 1 through 4. Times are expressed in nanoseconds. */
typedef struct _TIMECARD_SIGNAL_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Generator;
    unsigned __int32 Flags;
    unsigned __int32 Status;
    unsigned __int32 Version;
    unsigned __int32 RepeatCount;
    unsigned __int32 StartNanoseconds;
    unsigned __int32 CableDelayNanoseconds;
    unsigned __int64 PeriodNanoseconds;
    unsigned __int64 PulseNanoseconds;
    unsigned __int64 PhaseNanoseconds;
    unsigned __int64 StartSeconds;
} TIMECARD_SIGNAL_CONTROL;

typedef struct _TIMECARD_SIGNAL_EVENT {
    unsigned __int64 SystemInterruptTime100ns;
    unsigned __int64 Sequence;
    unsigned __int32 Generator;
    unsigned __int32 Flags;
    unsigned __int32 Status;
    unsigned __int32 Reserved;
} TIMECARD_SIGNAL_EVENT;

/* Input and output share this METHOD_BUFFERED structure. */
typedef struct _TIMECARD_SIGNAL_EVENT_BATCH {
    unsigned __int32 Size;
    unsigned __int32 Generator;
    unsigned __int32 MaximumEvents;
    unsigned __int32 Count;
    unsigned __int32 DroppedEvents;
    unsigned __int32 Flags;
    unsigned __int32 Reserved[2];
    TIMECARD_SIGNAL_EVENT Events[TIMECARD_SIGNAL_EVENT_MAX_BATCH];
} TIMECARD_SIGNAL_EVENT_BATCH;

/*
 * One interrupt snapshot from a Signal Timestamper.  Snapshot payloads are
 * bounded to 256 bits even if a custom image synthesizes a wider data bus;
 * DataWidth preserves the hardware value and DATA_TRUNCATED reports clipping.
 */
typedef struct _TIMECARD_TIMESTAMP_EVENT {
    TIMECARD_TIME Time;
    unsigned __int32 TimestampCount;
    unsigned __int32 EventCount;
    unsigned __int32 Error;
    unsigned __int32 DataWidth;
    unsigned __int32 Flags;
    unsigned __int32 Data[TIMECARD_TIMESTAMP_MAX_DATA_BYTES / 4u];
    unsigned __int32 Reserved[3];
} TIMECARD_TIMESTAMP_EVENT;

typedef struct _TIMECARD_TIMESTAMP_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Channel;
    unsigned __int32 Flags;
    unsigned __int32 Status;
    unsigned __int32 Version;
    unsigned __int32 Polarity;
    unsigned __int32 CableDelayNanoseconds;
    unsigned __int32 Interrupt;
    unsigned __int32 InterruptMask;
    unsigned __int32 EventCount;
    unsigned __int32 TimestampCount;
    unsigned __int32 QueueDepth;
    unsigned __int32 DroppedEvents;
    unsigned __int32 DataWidth;
    TIMECARD_TIME Time;
    unsigned __int32 Data;
    unsigned __int32 Reserved;
} TIMECARD_TIMESTAMP_CONTROL;

/* Input and output share this METHOD_BUFFERED structure. */
typedef struct _TIMECARD_TIMESTAMP_BATCH {
    unsigned __int32 Size;
    unsigned __int32 Channel;
    unsigned __int32 MaximumEvents;
    unsigned __int32 Count;
    unsigned __int32 DroppedEvents;
    unsigned __int32 Flags;
    unsigned __int32 Reserved[2];
    TIMECARD_TIMESTAMP_EVENT Events[TIMECARD_TIMESTAMP_MAX_BATCH];
} TIMECARD_TIMESTAMP_BATCH;

/* CoreMask is the trusted board profile's expected map; query confirms a core. */
typedef struct _TIMECARD_FPGA_CAPABILITIES {
    unsigned __int32 Size;
    unsigned __int32 AbiVersion;
    unsigned __int32 CoreMask;
    unsigned __int32 FeatureFlags;
    unsigned __int32 KnownGaps;
    unsigned __int32 Layout;
    unsigned __int32 BoardProfile;
    unsigned __int32 Reserved[9];
} TIMECARD_FPGA_CAPABILITIES;

typedef struct _TIMECARD_CORE_DESCRIPTOR {
    unsigned __int32 Type;
    unsigned __int32 Instance;
    unsigned __int32 RegisterOffset;
    unsigned __int32 RegisterSpan;
    unsigned __int32 InterruptMessage;
    unsigned __int32 Version;
    unsigned __int32 Flags;
    unsigned __int32 Reserved;
} TIMECARD_CORE_DESCRIPTOR;

/*
 * The published Time Card images have no Configuration Slave descriptor ROM.
 * This inventory reports only the exact static board maps already trusted by
 * the driver; it never sweeps or probes guessed BAR addresses.
 */
typedef struct _TIMECARD_CORE_INVENTORY {
    unsigned __int32 Size;
    unsigned __int32 AbiVersion;
    unsigned __int32 Count;
    unsigned __int32 Flags;
    unsigned __int32 BoardProfile;
    unsigned __int32 Layout;
    unsigned __int32 RawImageVersion;
    unsigned __int32 Reserved;
    TIMECARD_CORE_DESCRIPTOR Cores[TIMECARD_CORE_INVENTORY_MAX];
} TIMECARD_CORE_INVENTORY;

/*
 * An explicit, session-scoped synthesis contract.  Set is accepted only when
 * RawImageVersion exactly matches the driver's trusted image-identity word;
 * unknown images remain conservative and every optional access revalidates
 * the match.  The acknowledgement value is required for writes.
 */
typedef struct _TIMECARD_FPGA_IMAGE_CONTRACT {
    unsigned __int32 Size;
    unsigned __int32 AbiVersion;
    unsigned __int32 RawImageVersion;
    unsigned __int32 CapabilityFlags;
    unsigned __int32 EffectiveFlags;
    unsigned __int32 StatusFlags;
    unsigned __int32 BoardProfile;
    unsigned __int32 Layout;
    unsigned __int32 Acknowledgement;
    unsigned __int32 Reserved[7];
} TIMECARD_FPGA_IMAGE_CONTRACT;

/*
 * The driver reads exactly one 32-bit word from the documented MSI/MSI-X
 * image-version resource.  ART layouts are deliberately unsupported because
 * they do not publish the same trusted static resource map.
 */
typedef struct _TIMECARD_FPGA_IMAGE_INFO {
    unsigned __int32 Size;
    unsigned __int32 AbiVersion;
    unsigned __int32 Flags;
    unsigned __int32 RawVersion;
    unsigned __int32 ImageTag;
    unsigned __int32 ImageVersion;
    unsigned __int32 Layout;
    unsigned __int32 BoardProfile;
    unsigned __int32 RegisterOffset;
    unsigned __int32 Reserved[7];
} TIMECARD_FPGA_IMAGE_INFO;

typedef struct _TIMECARD_CLOCK_TELEMETRY {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Version;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 Select;
    unsigned __int32 CoreMask;
    unsigned __int32 KnownGaps;
    unsigned __int32 InSyncThreshold;
    unsigned __int32 ServoOffsetP;
    unsigned __int32 ServoOffsetI;
    unsigned __int32 ServoDriftP;
    unsigned __int32 ServoDriftI;
    signed __int32 StatusOffsetNanoseconds;
    signed __int32 StatusDriftPpb;
    unsigned __int32 StatusOffsetFraction;
    unsigned __int32 StatusDriftFraction;
    unsigned __int32 Reserved[3];
} TIMECARD_CLOCK_TELEMETRY;

typedef struct _TIMECARD_PPS_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Core;
    unsigned __int32 Flags;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 Version;
    unsigned __int32 Polarity;
    unsigned __int32 PulseWidthMilliseconds;
    signed __int32 CableDelayNanoseconds;
    unsigned __int32 Reserved[7];
} TIMECARD_PPS_CONTROL;

typedef struct _TIMECARD_TIMECODE_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Format;
    unsigned __int32 Role;
    unsigned __int32 Flags;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 Version;
    unsigned __int32 Mode;
    unsigned __int32 Code;
    signed __int32 CorrectionSeconds;
    signed __int32 DelayNanoseconds;
    unsigned __int32 ControlBits;
    unsigned __int32 BitPosition;
    unsigned __int32 AmplitudeModulation;
    unsigned __int32 ManualYear;
    unsigned __int32 Reserved[5];
} TIMECARD_TIMECODE_CONTROL;

typedef struct _TIMECARD_TOD_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 Version;
    unsigned __int32 Protocol;
    unsigned __int32 Gnss;
    unsigned __int32 Baud;
    unsigned __int32 BaudSelector;
    unsigned __int32 Polarity;
    signed __int32 CorrectionSeconds;
    unsigned __int32 MessageDisableMask;
    unsigned __int32 UtcStatus;
    signed __int32 TimeToLeapSeconds;
    unsigned __int32 GnssStatus;
    unsigned __int32 Satellites;
    unsigned __int32 Reserved[4];
} TIMECARD_TOD_CONTROL;

/* Counter numbers are 1 through 4; zero integration seconds disables it. */
typedef struct _TIMECARD_FREQUENCY_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Counter;
    unsigned __int32 Flags;
    unsigned __int32 IntegrationSeconds;
    unsigned __int32 FrequencyHz;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 Reserved;
} TIMECARD_FREQUENCY_CONTROL;

typedef struct _TIMECARD_FLASH_STATUS {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Offset;
    unsigned __int32 JedecId;
    unsigned __int32 CapacityBytes;
    unsigned __int32 FirmwareOffset;
    unsigned __int32 EraseSize;
    unsigned __int32 PageSize;
    unsigned __int32 ControllerStatus;
    unsigned __int32 FlashStatus;
    unsigned __int32 FifoDepth;
    unsigned __int32 Reserved[5];
} TIMECARD_FLASH_STATUS;

/* Flash offsets are relative to TIMECARD_FLASH_FIRMWARE_OFFSET. */
typedef struct _TIMECARD_FLASH_RANGE {
    unsigned __int32 Size;
    unsigned __int32 Offset;
    unsigned __int32 Length;
    unsigned __int32 Reserved;
} TIMECARD_FLASH_RANGE;

typedef struct _TIMECARD_FLASH_TRANSFER {
    unsigned __int32 Size;
    unsigned __int32 Offset;
    unsigned __int32 Length;
    unsigned __int32 Status;
    unsigned char Data[TIMECARD_FLASH_MAX_TRANSFER];
} TIMECARD_FLASH_TRANSFER;

typedef struct _TIMECARD_FLASH_RESULT {
    unsigned __int32 Size;
    unsigned __int32 Offset;
    unsigned __int32 Length;
    unsigned __int32 Status;
} TIMECARD_FLASH_RESULT;

typedef struct _TIMECARD_UART_OBSERVE {
    unsigned __int32 Size;
    unsigned __int32 Port;
    unsigned __int32 TimeoutMilliseconds;
    unsigned __int32 Flags;
    unsigned __int32 LineStatus;
    unsigned __int32 Reserved[3];
} TIMECARD_UART_OBSERVE;

#endif /* TIMECARD_IOCTL_H */
