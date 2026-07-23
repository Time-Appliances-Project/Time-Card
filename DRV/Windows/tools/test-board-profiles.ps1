[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$windows = Split-Path -Parent $PSScriptRoot
$root = (Resolve-Path (Join-Path $windows '..\..')).Path
$linuxSource = Get-Content -Raw (Join-Path $root 'DRV\Linux\ptp_ocp.c')
$driverSource = Get-Content -Raw (Join-Path $windows 'driver.c')
$headerSource = Get-Content -Raw (Join-Path $windows 'timecard.h')
$infSource = Get-Content -Raw (Join-Path $windows 'timecard.inf')
$projectSource = Get-Content -Raw (Join-Path $windows 'timecard.vcxproj')

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
Assert-Match $projectSource 'i2c_ocores\.c' `
    'OpenCores I2C implementation is not in the driver build'
Assert-Match $projectSource 'flash_altera\.c' `
    'Altera SPI implementation is not in the driver build'
Assert-Match $headerSource 'TIMECARD_SUBSYSTEM_MASK_ART' `
    'ART subsystem capability mask is missing'

Write-Host 'Board-profile tests passed (3 PCI identities, ART map, transports, and capability gating).'
