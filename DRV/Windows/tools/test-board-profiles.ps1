[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$windows = Split-Path -Parent $PSScriptRoot
$root = (Resolve-Path (Join-Path $windows '..\..')).Path
$linuxSource = Get-Content -Raw (Join-Path $root 'DRV\Linux\ptp_ocp.c')
$driverSource = Get-Content -Raw (Join-Path $windows 'driver.c')
$busSource = Get-Content -Raw (Join-Path $windows 'bus.c')
$i2cSource = Get-Content -Raw (Join-Path $windows 'i2c.c')
$serialSource = Get-Content -Raw (Join-Path $windows 'serial.c')
$headerSource = Get-Content -Raw (Join-Path $windows 'timecard.h')
$infSource = Get-Content -Raw (Join-Path $windows 'timecard.inf')
$projectSource = Get-Content -Raw (Join-Path $windows 'timecard.vcxproj')
$abiSource = Get-Content -Raw (Join-Path $windows 'include\timecard_ioctl.h')
$mroSource = Get-Content -Raw (Join-Path $windows 'mro50.c')
$verifySource = Get-Content -Raw (Join-Path $windows 'verify.ps1')

function Assert-Match {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Description
    )
    if ($Text -notmatch $Pattern) {
        throw "Board-profile test failed: $Description"
    }
}

Assert-Match $linuxSource 'PCI_DEVICE_DATA\(FACEBOOK,\s*TIMECARD' `
    'Linux Meta/Facebook PCI identity is missing'
Assert-Match $linuxSource 'PCI_DEVICE_DATA\(CELESTICA,\s*TIMECARD' `
    'Linux Celestica PCI identity is missing'
Assert-Match $linuxSource 'PCI_DEVICE_DATA\(OROLIA,\s*ARTCARD' `
    'Linux Orolia ART PCI identity is missing'

Assert-Match $infSource 'PCI\\VEN_1D9B&DEV_0400' `
    'Windows Meta/Facebook PCI match is missing'
Assert-Match $infSource 'PCI\\VEN_18D4&DEV_1008' `
    'Windows Celestica PCI match is missing'
Assert-Match $infSource 'PCI\\VEN_1AD7&DEV_A000' `
    'Windows Orolia ART PCI match is missing'

$linuxArtOffsets = @(
    '\.offset\s*=\s*0x01000000,\s*\.size\s*=\s*0x10000',
    '\.offset\s*=\s*0x00160000\s*\+\s*0x1000',
    '\.offset\s*=\s*0x00190000,\s*\.irq_vec\s*=\s*7',
    '\.offset\s*=\s*0x003C0000,\s*\.size\s*=\s*0x1000',
    '\.offset\s*=\s*0x350000,\s*\.size\s*=\s*0x100',
    '\.offset\s*=\s*0x00310000,\s*\.size\s*=\s*0x10000'
)
foreach ($pattern in $linuxArtOffsets) {
    Assert-Match $linuxSource $pattern `
        "Expected ART resource $pattern is no longer present in Linux"
}

$artOffsets = [ordered]@{
    TIMECARD_CLOCK_OFFSET_ART = '0x01000000u'
    TIMECARD_UART_GNSS_OFFSET_ART = '0x00161000u'
    TIMECARD_UART_MAC_OFFSET_ART = '0x00190000u'
    TIMECARD_SMA_OFFSET_ART = '0x003c0000u'
    TIMECARD_I2C_OFFSET_ART = '0x00350000u'
    TIMECARD_FLASH_OFFSET_ART = '0x00310000u'
}
foreach ($entry in $artOffsets.GetEnumerator()) {
    Assert-Match $headerSource (
        '#define\s+' + [regex]::Escape($entry.Key) + '\s+' +
        [regex]::Escape($entry.Value)) `
        "Windows ART offset $($entry.Key) does not match Linux"
}

Assert-Match $driverSource 'TIMECARD_I2C_CONTROLLER_OCORES' `
    'ART does not select the OpenCores I2C transport'
Assert-Match $driverSource 'TIMECARD_FLASH_CONTROLLER_ALTERA' `
    'ART does not select the Altera SPI transport'
Assert-Match $driverSource ([regex]::Escape("hardwareIds[i - 1u] != L'\\'")) `
    'PCI parser does not accept the leading PCI\VEN_ field'
Assert-Match $headerSource 'TIMECARD_MRO50_OFFSET_ART\s+0x00340000u' `
    'ART direct mRO-50 bridge offset is missing'
Assert-Match $headerSource 'TIMECARD_BOARD_CONFIG_OFFSET_ART\s+0x00210000u' `
    'ART board-configuration offset is missing'
Assert-Match $abiSource 'TIMECARD_ABI_VERSION\s+10u' `
    'Celestica sensor support did not advance the public ABI'
Assert-Match $mroSource 'TimeCardMro50Query' `
    'ART mRO-50 query implementation is missing'
Assert-Match $projectSource 'i2c_ocores\.c' `
    'OpenCores I2C implementation is not in the driver build'
Assert-Match $projectSource 'flash_altera\.c' `
    'Altera SPI implementation is not in the driver build'
Assert-Match $projectSource 'mro50\.c' `
    'mRO-50 implementation is not in the driver build'
Assert-Match $headerSource 'TIMECARD_SUBSYSTEM_MASK_ART' `
    'ART subsystem capability mask is missing'
Assert-Match $headerSource 'TIMECARD_BOARD_CELESTICA\s+3u' `
    'Celestica does not have a distinct board profile'
Assert-Match $driverSource `
    'PciVendorId\s*==\s*0x18d4u[\s\S]*TIMECARD_BOARD_CELESTICA' `
    'Celestica PCI identity does not select its sensor profile'
Assert-Match $abiSource 'TIMECARD_SENSOR_CAP_LM75B' `
    'Celestica LM75B capability is missing from the ABI'
Assert-Match $abiSource 'TIMECARD_SENSOR_CAP_SHT3X' `
    'Celestica SHT3x capability is missing from the ABI'
Assert-Match $abiSource 'TIMECARD_SENSOR_CAP_ICP10100' `
    'Celestica ICP-10100 capability is missing from the ABI'
Assert-Match $i2cSource `
    'TIMECARD_I2C_MUX_CELESTICA_LM75B[\s\S]*TimeCardLm75bReadLocked' `
    'Celestica LM75B channel is not sampled'
Assert-Match $i2cSource `
    'TIMECARD_I2C_MUX_CELESTICA_SHT3X[\s\S]*TimeCardSht3xReadLocked' `
    'Celestica SHT3x channel is not sampled'
Assert-Match $i2cSource `
    'TIMECARD_I2C_MUX_CELESTICA_ICP10100[\s\S]*TimeCardIcp10100ReadLocked' `
    'Celestica ICP-10100 channel is not sampled'
Assert-Match $i2cSource 'TimeCardSensorCrc8' `
    'Sensirion/TDK CRC validation is missing'
Assert-Match $i2cSource '0xc5u,\s*0x95u,\s*0x00u,\s*0x66u,\s*0x9cu' `
    'ICP-10100 OTP pointer sequence is missing'
Assert-Match $i2cSource `
    'BoardProfile\s*==\s*TIMECARD_BOARD_ART[\s\S]*Capabilities\s*=\s*0u' `
    'ART incorrectly advertises an environmental sensor population'
Assert-Match $i2cSource `
    'celesticaMap\[TIMECARD_LED_COUNT\][\s\S]*4u,\s*5u,\s*0u,\s*1u,\s*2u,\s*3u' `
    'Celestica GPS/SMA LED order does not match sheet 26'
Assert-Match $i2cSource `
    'TIMECARD_BOARD_CELESTICA[\s\S]*logicalLed\s*!=\s*TIMECARD_LED_GNSS2' `
    'Celestica unused OUT16-18 group is not capability-gated'
Assert-Match $verifySource `
    'hasCelesticaProfile[\s\S]*ABI:\\s\+10[\s\S]*Celestica R4006' `
    'live verification does not reject an old driver on Celestica'
Assert-Match $busSource '\*enabled\s*=\s*TRUE;[\s\S]*STATUS_OBJECT_NAME_NOT_FOUND' `
    'new supported cards do not default to the full Device Manager hierarchy'
Assert-Match $i2cSource 'TimeCardMonitorBranchResolveLocked' `
    'environment and power monitors are not discovered across board-variant mux routes'
Assert-Match $i2cSource 'AXI-IIC dynamic repeated START' `
    'register sensors do not have the STOP-then-START compatibility fallback'
Assert-Match $i2cSource `
    'TimeCardI2cWriteLocked\([\s\S]*readRequest\.SubaddressLength\s*=\s*0' `
    'sensor fallback does not perform a STOP-terminated pointer write before the raw read'
Assert-Match $headerSource 'I2cEnvironmentMuxMask' `
    'environment-sensor mux route is not cached independently'
Assert-Match $headerSource 'I2cPowerMuxMask' `
    'power-monitor mux route is not cached independently'
Assert-Match $i2cSource (
    'static const UCHAR candidates\[\][\s\S]*' +
    'TIMECARD_I2C_MUX_CHANNEL_SENSORS') `
    'V9 SENS_I2C is no longer the first sensor-route candidate'
Assert-Match $i2cSource 'imuMask\s*=\s*\(UCHAR\)context->I2cSensorMuxMask' `
    'Rev00 alternate IMU route is not preserved independently'
Assert-Match $serialSource `
    'msiMessages\[TIMECARD_UART_COUNT\]\s*=\s*\{\s*3u,\s*4u,\s*5u,\s*10u\s*\}' `
    'MSI UART vectors no longer match the Linux resource map'
Assert-Match $serialSource `
    'msixMessages\[TIMECARD_UART_COUNT\]\s*=\s*\{\s*35u,\s*36u,\s*37u,\s*42u\s*\}' `
    'MSI-X UART vectors no longer match the Linux resource map'
Assert-Match $serialSource 'TIMECARD_UART_RX_RING_SIZE' `
    'interrupt-backed UART receive buffering is missing'
Assert-Match $serialSource 'TIMECARD_UART_BURST_IDLE_US' `
    'burst-aware UART polling fallback is missing'

Write-Host 'Board-profile tests passed (3 PCI identities, Celestica sensors, ART map, transports, independent sensor-route discovery, hierarchy default, UART buffering, and capability gating).'
