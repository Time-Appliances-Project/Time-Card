#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$ExpectedInf = 'oem233.inf',
    [string]$FallbackInf = 'oem226.inf',
    [int]$DeviceTimeoutSeconds = 25
)

$ErrorActionPreference = 'Stop'
$parentPattern = 'PCI\VEN_1D9B&DEV_0400*'
$tool = Join-Path $PSScriptRoot 'out\timecardctl.exe'
$log = Join-Path $PSScriptRoot 'persist-hierarchy-safe.log'
$persistenceAttempted = $false
$completed = $false
$expectedChildren = @(
    'TIMECARD\PHC\*',
    'TIMECARD\TOD\*',
    'TIMECARD\UART_GNSS\*',
    'TIMECARD\UART_GNSS2\*',
    'TIMECARD\UART_MAC\*',
    'TIMECARD\UART_NMEA\*',
    'TIMECARD\SMA\*',
    'TIMECARD\TIMING_IO\*',
    'TIMECARD\I2C\*',
    'TIMECARD\FLASH\*',
    'TIMECARD\PTM\*'
)

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
    param([Parameter(Mandatory)][string]$Inf)

    $parents = @(Get-TimeCardParent)
    if ($parents.Count -ne 1) {
        throw "Expected one Time Card PCI controller, found $($parents.Count)."
    }
    $parent = $parents[0]
    $problem = Get-ProblemCode $parent
    $driverInf = Get-DriverInf $parent
    if ($parent.Status -ne 'OK' -or $problem -ne 0 -or
        $parent.Class -ine 'TimeCard' -or $driverInf -ine $Inf) {
        throw "Controller is unhealthy: status=$($parent.Status), " +
            "problem=$problem, class=$($parent.Class), INF=$driverInf."
    }
    $parent
}

function Assert-HealthyChildren {
    param([Parameter(Mandatory)]$Parent)

    $children = @(Get-PnpDevice -PresentOnly -Class TimeCard `
            -ErrorAction SilentlyContinue |
        Where-Object InstanceId -Like 'TIMECARD\*')
    if ($children.Count -ne $expectedChildren.Count) {
        throw "Expected $($expectedChildren.Count) subsystem devices, " +
            "found $($children.Count)."
    }

    foreach ($pattern in $expectedChildren) {
        $matches = @($children | Where-Object InstanceId -Like $pattern)
        if ($matches.Count -ne 1) {
            throw "Expected one subsystem matching $pattern, found " +
                "$($matches.Count)."
        }
        $child = $matches[0]
        $problem = Get-ProblemCode $child
        $inf = Get-DriverInf $child
        $reportedParent = (Get-PnpDeviceProperty -InstanceId $child.InstanceId `
            -KeyName DEVPKEY_Device_Parent).Data
        if ($child.Status -ne 'OK' -or $problem -ne 0 -or
            $child.Class -ine 'TimeCard' -or $inf -ine $ExpectedInf -or
            $reportedParent -ine $Parent.InstanceId) {
            throw "Subsystem $($child.InstanceId) is unhealthy: " +
                "status=$($child.Status), problem=$problem, INF=$inf, " +
                "parent=$reportedParent."
        }
    }
    $children
}

function Wait-HealthyPersistentHierarchy {
    $deadline = (Get-Date).AddSeconds($DeviceTimeoutSeconds)
    do {
        try {
            $parent = Assert-HealthyParent -Inf $ExpectedInf
            $children = @(Assert-HealthyChildren -Parent $parent)
            return [pscustomobject]@{
                Parent = $parent
                Children = $children
            }
        }
        catch {
            $lastError = $_
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "Persistent hierarchy health check timed out: $lastError"
}

function Restart-TimeCard {
    $parents = @(Get-TimeCardParent)
    if ($parents.Count -ne 1) {
        throw "Expected one Time Card to restart, found $($parents.Count)."
    }
    # Restart preserves the devnode's hardware registry key. Remove/rescan
    # intentionally deletes that key, including EnableSubsystemDevices.
    Invoke-PnpUtil -Arguments @('/restart-device', $parents[0].InstanceId) |
        Out-Null
}

function Wait-ControllerOnly {
    param([Parameter(Mandatory)][string]$Inf)

    $deadline = (Get-Date).AddSeconds($DeviceTimeoutSeconds)
    do {
        try {
            $parent = Assert-HealthyParent -Inf $Inf
            $children = @(Get-PnpDevice -PresentOnly -Class TimeCard `
                    -ErrorAction SilentlyContinue |
                Where-Object InstanceId -Like 'TIMECARD\*')
            if ($children.Count -eq 0) {
                return $parent
            }
        }
        catch {
            $lastError = $_
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "Controller-only recovery timed out: $lastError"
}

function Recover-ControllerOnly {
    Write-Warning 'Clearing hierarchy persistence and returning to controller-only mode.'
    Invoke-TimeCardCtl -Arguments @('hierarchy-disable') `
        -AllowFailure | Out-Null
    Restart-TimeCard
    try {
        Wait-ControllerOnly -Inf $ExpectedInf | Out-Null
        Write-Host "$ExpectedInf recovered in controller-only mode."
        return
    }
    catch {
        Write-Warning "Direct cleanup failed: $_ Falling back to $FallbackInf."
    }

    $parents = @(Get-TimeCardParent)
    foreach ($parent in $parents) {
        Invoke-PnpUtil -Arguments @('/remove-device', $parent.InstanceId) `
            -AllowFailure | Out-Null
    }
    Invoke-PnpUtil -Arguments @('/delete-driver', $ExpectedInf, '/uninstall') `
        -AllowFailure | Out-Null
    Invoke-PnpUtil -Arguments @('/scan-devices') | Out-Null

    $deadline = (Get-Date).AddSeconds($DeviceTimeoutSeconds)
    do {
        try {
            $fallback = Assert-HealthyParent -Inf $FallbackInf
            break
        }
        catch {
            $lastError = $_
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    if (-not $fallback) {
        throw "Fallback controller did not start: $lastError"
    }

    Invoke-TimeCardCtl -Arguments @('hierarchy-disable') | Out-Null
    Restart-TimeCard
    Wait-ControllerOnly -Inf $FallbackInf | Out-Null
    Write-Host "$FallbackInf recovered in controller-only mode."
}

Start-Transcript -Path $log -Force | Out-Null
try {
    $parent = Assert-HealthyParent -Inf $ExpectedInf
    $children = @(Assert-HealthyChildren -Parent $parent)
    $before = Invoke-TimeCardCtl -Arguments @('hierarchy-status')
    if ($before -notmatch
        '(?im)^Runtime subsystem devices:\s+enabled\s*$' -or
        $before -notmatch '(?im)^Enable on next start:\s+no\s*$') {
        throw 'Expected a healthy live, nonpersistent hierarchy before persistence.'
    }

    Write-Host 'Persisting the verified hierarchy...'
    $persistenceAttempted = $true
    $persisted = Invoke-TimeCardCtl -Arguments @('hierarchy-persist')
    if ($persisted -notmatch
        '(?im)^Runtime subsystem devices:\s+enabled\s*$' -or
        $persisted -notmatch '(?im)^Enable on next start:\s+yes\s*$') {
        throw 'The hierarchy persistence setting was not accepted.'
    }

    Write-Host 'Restarting only the Time Card to exercise its next-start path...'
    Restart-TimeCard
    $state = Wait-HealthyPersistentHierarchy
    $state.Children | Sort-Object FriendlyName |
        Format-Table -AutoSize Status, FriendlyName, InstanceId | Out-Host

    $after = Invoke-TimeCardCtl -Arguments @('hierarchy-status')
    if ($after -notmatch
        '(?im)^Runtime subsystem devices:\s+enabled\s*$' -or
        $after -notmatch '(?im)^Enable on next start:\s+yes\s*$') {
        throw 'Hierarchy did not remain enabled after the device restart.'
    }
    $status = Invoke-TimeCardCtl -Arguments @('status')
    if ($status -notmatch '(?im)^Driver:\s+1\.3\s*$') {
        throw 'Driver 1.3 was not active after the device restart.'
    }
    Invoke-TimeCardCtl -Arguments @('get') | Out-Null
    Invoke-TimeCardCtl -Arguments @('uart-config', '0', '115200') | Out-Null
    Invoke-TimeCardCtl -Arguments @('uart-read', '0', '64', '1000') | Out-Null

    $completed = $true
    Write-Host ''
    Write-Host 'Persistent hierarchy test passed after a full Time Card restart.'
}
catch {
    $failure = $_
    Write-Host "PERSISTENCE ERROR: $($failure.Exception.Message)" `
        -ForegroundColor Red
    if ($persistenceAttempted) {
        try {
            Recover-ControllerOnly
        }
        catch {
            throw "Persistence test failed: $failure Recovery also failed: $_"
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
