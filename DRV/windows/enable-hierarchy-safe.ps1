#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$ExpectedInf = 'oem233.inf',
    [int]$DeviceTimeoutSeconds = 20
)

$ErrorActionPreference = 'Stop'
$parentPattern = 'PCI\VEN_1D9B&DEV_0400*'
$tool = Join-Path $PSScriptRoot 'out\timecardctl.exe'
$log = Join-Path $PSScriptRoot 'enable-hierarchy-safe.log'
$enableAttempted = $false
$completed = $false
$expectedChildren = [ordered]@{
    'TIMECARD\PHC\*'        = 'Precision Hardware Clock (PHC)'
    'TIMECARD\TOD\*'        = 'GNSS Time-of-Day Engine'
    'TIMECARD\UART_GNSS\*'  = 'GNSS Receiver (UART)'
    'TIMECARD\UART_GNSS2\*' = 'Secondary GNSS Receiver (UART)'
    'TIMECARD\UART_MAC\*'   = 'Atomic Clock (UART)'
    'TIMECARD\UART_NMEA\*'  = 'NMEA Output (UART)'
    'TIMECARD\SMA\*'        = 'SMA Inputs and Outputs'
    'TIMECARD\TIMING_IO\*'  = 'Timestamp and Signal Generators'
    'TIMECARD\I2C\*'        = 'I2C Controller'
    'TIMECARD\FLASH\*'      = 'FPGA Firmware and SPI Flash'
    'TIMECARD\PTM\*'        = 'PCIe Precision Time Measurement (PTM)'
}

function Invoke-NativeTool {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,
        [Parameter(Mandatory)]
        [string[]]$Arguments,
        [switch]$AllowFailure
    )

    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $FilePath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    $output | Out-Host
    if (-not $AllowFailure -and $exitCode -ne 0) {
        throw "$FilePath $($Arguments -join ' ') failed with exit code $exitCode."
    }
    ($output | Out-String)
}

function Invoke-TimeCardCtl {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments,
        [switch]$AllowFailure
    )

    Invoke-NativeTool -FilePath $tool -Arguments $Arguments `
        -AllowFailure:$AllowFailure
}

function Invoke-PnpUtil {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments,
        [switch]$AllowFailure
    )

    Invoke-NativeTool -FilePath 'pnputil.exe' -Arguments $Arguments `
        -AllowFailure:$AllowFailure
}

function Get-TimeCardParent {
    @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object InstanceId -Like $parentPattern)
}

function Get-ProblemCode {
    param([Parameter(Mandatory)]$Device)

    (Get-PnpDeviceProperty -InstanceId $Device.InstanceId `
        -KeyName DEVPKEY_Device_ProblemCode).Data
}

function Get-DriverInf {
    param([Parameter(Mandatory)]$Device)

    (Get-PnpDeviceProperty -InstanceId $Device.InstanceId `
        -KeyName DEVPKEY_Device_DriverInfPath).Data
}

function Assert-HealthyParent {
    $parents = @(Get-TimeCardParent)
    if ($parents.Count -ne 1) {
        throw "Expected one Time Card PCI controller, found $($parents.Count)."
    }
    $parent = $parents[0]
    $problem = Get-ProblemCode $parent
    $inf = Get-DriverInf $parent
    if ($parent.Status -ne 'OK' -or $problem -ne 0 -or
        $parent.Class -ine 'TimeCard' -or $inf -ine $ExpectedInf) {
        throw "Controller is unhealthy: status=$($parent.Status), " +
            "problem=$problem, class=$($parent.Class), INF=$inf."
    }
    $parent
}

function Wait-HealthyHierarchy {
    param([Parameter(Mandatory)]$Parent)

    $deadline = (Get-Date).AddSeconds($DeviceTimeoutSeconds)
    do {
        $children = @(Get-PnpDevice -PresentOnly -Class TimeCard `
                -ErrorAction SilentlyContinue |
            Where-Object InstanceId -Like 'TIMECARD\*')
        if ($children.Count -eq $expectedChildren.Count) {
            break
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    if ($children.Count -ne $expectedChildren.Count) {
        throw "Expected $($expectedChildren.Count) subsystem devices, " +
            "found $($children.Count)."
    }

    foreach ($pattern in $expectedChildren.Keys) {
        $matches = @($children | Where-Object InstanceId -Like $pattern)
        if ($matches.Count -ne 1) {
            throw "Expected one $($expectedChildren[$pattern]) device, " +
                "found $($matches.Count)."
        }
        $child = $matches[0]
        $problem = Get-ProblemCode $child
        $inf = Get-DriverInf $child
        $reportedParent = (Get-PnpDeviceProperty -InstanceId $child.InstanceId `
            -KeyName DEVPKEY_Device_Parent).Data
        if ($child.Status -ne 'OK' -or $problem -ne 0 -or
            $child.Class -ine 'TimeCard' -or $inf -ine $ExpectedInf -or
            $reportedParent -ine $Parent.InstanceId) {
            throw "$($expectedChildren[$pattern]) is unhealthy: " +
                "status=$($child.Status), problem=$problem, class=$($child.Class), " +
                "INF=$inf, parent=$reportedParent."
        }
    }
    $children
}

function Clear-LiveHierarchy {
    Write-Warning 'Clearing hierarchy persistence and restarting only the Time Card.'
    Invoke-TimeCardCtl -Arguments @('hierarchy-disable') `
        -AllowFailure | Out-Null
    $parents = @(Get-TimeCardParent)
    foreach ($parent in $parents) {
        Invoke-PnpUtil -Arguments @('/remove-device', $parent.InstanceId) `
            -AllowFailure | Out-Null
    }
    Invoke-PnpUtil -Arguments @('/scan-devices') | Out-Null

    $deadline = (Get-Date).AddSeconds($DeviceTimeoutSeconds)
    do {
        try {
            $parent = Assert-HealthyParent
            $children = @(Get-PnpDevice -PresentOnly -Class TimeCard `
                    -ErrorAction SilentlyContinue |
                Where-Object InstanceId -Like 'TIMECARD\*')
            if ($children.Count -eq 0) {
                Write-Host 'Hierarchy cleanup succeeded; controller remains healthy.'
                return
            }
        }
        catch {
            # The parent can be temporarily absent while PCI is rescanned.
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw 'Could not return the Time Card to controller-only state.'
}

Start-Transcript -Path $log -Force | Out-Null
try {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Control tool not found: $tool"
    }

    $parent = Assert-HealthyParent
    $status = Invoke-TimeCardCtl -Arguments @('status')
    if ($status -notmatch '(?im)^Driver:\s+1\.3\s*$') {
        throw 'The controller did not report driver implementation 1.3.'
    }
    $before = Invoke-TimeCardCtl -Arguments @('hierarchy-status')
    if ($before -notmatch
        '(?im)^Runtime subsystem devices:\s+disabled\s*$' -or
        $before -notmatch '(?im)^Enable on next start:\s+no\s*$') {
        throw 'Hierarchy is not in the expected disabled, nonpersistent state.'
    }

    Write-Host 'Enabling subsystem devices live (not persistent)...'
    $enableAttempted = $true
    Invoke-TimeCardCtl -Arguments @('hierarchy-enable') | Out-Null
    $children = @(Wait-HealthyHierarchy -Parent $parent)
    $children | Sort-Object FriendlyName |
        Format-Table -AutoSize Status, FriendlyName, InstanceId | Out-Host

    $after = Invoke-TimeCardCtl -Arguments @('hierarchy-status')
    if ($after -notmatch
        '(?im)^Runtime subsystem devices:\s+enabled\s*$' -or
        $after -notmatch '(?im)^Enable on next start:\s+no\s*$') {
        throw 'Hierarchy is not live and nonpersistent after enable.'
    }
    Invoke-TimeCardCtl -Arguments @('get') | Out-Null
    Invoke-TimeCardCtl -Arguments @('uart-config', '0', '115200') | Out-Null
    Invoke-TimeCardCtl -Arguments @('uart-read', '0', '64', '1000') | Out-Null

    $completed = $true
    Write-Host ''
    Write-Host 'All 11 subsystem devices are healthy and owned by the Time Card.'
    Write-Host 'The hierarchy is live but is not enabled for the next start.'
}
catch {
    $failure = $_
    Write-Host "HIERARCHY ERROR: $($failure.Exception.Message)" `
        -ForegroundColor Red
    if ($enableAttempted) {
        try {
            Clear-LiveHierarchy
        }
        catch {
            throw "Hierarchy test failed: $failure Cleanup also failed: $_"
        }
    }
    throw $failure
}
finally {
    Stop-Transcript | Out-Null
}

if (-not $completed) {
    exit 1
}
