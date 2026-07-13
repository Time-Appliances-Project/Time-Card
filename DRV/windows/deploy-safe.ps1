#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$KnownGoodInf = 'oem222.inf',
    [string]$KnownGoodClass = 'System',
    [string]$ExpectedDriverVersion = '1.3',
    [int]$DeviceTimeoutSeconds = 20
)

$ErrorActionPreference = 'Stop'
$devicePattern = 'PCI\VEN_1D9B&DEV_0400*'
$package = Join-Path $PSScriptRoot 'x64\Release\timecard\timecard.inf'
$catalog = Join-Path $PSScriptRoot 'x64\Release\timecard\timecard.cat'
$driver = Join-Path $PSScriptRoot 'x64\Release\timecard\timecard.sys'
$tool = Join-Path $PSScriptRoot 'out\timecardctl.exe'
$log = Join-Path $PSScriptRoot 'deploy-safe.log'
$newInf = $null
$deviceWasRemoved = $false
$deploymentComplete = $false

function Invoke-PnpUtil {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments,
        [switch]$AllowFailure
    )

    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & pnputil.exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    $output | Out-Host
    if (-not $AllowFailure -and $exitCode -ne 0) {
        throw "pnputil $($Arguments -join ' ') failed with exit code $exitCode."
    }
    [pscustomobject]@{
        ExitCode = $exitCode
        Output = ($output | Out-String)
    }
}

function Get-TimeCardDevice {
    @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object InstanceId -Like $devicePattern)
}

function Get-DeviceInf {
    param([Parameter(Mandatory)]$Device)

    (Get-PnpDeviceProperty -InstanceId $Device.InstanceId `
        -KeyName DEVPKEY_Device_DriverInfPath).Data
}

function Get-DeviceProblemCode {
    param([Parameter(Mandatory)]$Device)

    (Get-PnpDeviceProperty -InstanceId $Device.InstanceId `
        -KeyName DEVPKEY_Device_ProblemCode).Data
}

function Wait-TimeCardDevice {
    param(
        [Parameter(Mandatory)]
        [string]$ExpectedInf,
        [Parameter(Mandatory)]
        [string]$ExpectedClass
    )

    $deadline = (Get-Date).AddSeconds($DeviceTimeoutSeconds)
    do {
        $devices = @(Get-TimeCardDevice)
        if ($devices.Count -eq 1) {
            $candidate = $devices[0]
            $activeInf = Get-DeviceInf $candidate
            $problem = Get-DeviceProblemCode $candidate
            if ($candidate.Status -eq 'OK' -and $problem -eq 0 -and
                $activeInf -ieq $ExpectedInf -and
                $candidate.Class -ieq $ExpectedClass) {
                return $candidate
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    if ($devices.Count -eq 1) {
        $candidate = $devices[0]
        $activeInf = Get-DeviceInf $candidate
        $problem = Get-DeviceProblemCode $candidate
        throw "Time Card health check timed out: status=$($candidate.Status), " +
            "problem=$problem, class=$($candidate.Class), INF=$activeInf."
    }
    throw "Expected exactly one present Time Card, found $($devices.Count)."
}

function Get-TimeCardClassPackages {
    $result = Invoke-PnpUtil -Arguments @('/enum-drivers', '/class', 'TimeCard')
    @([regex]::Matches($result.Output,
            '(?im)^Published Name:\s+(oem\d+\.inf)\s*$') |
        ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() } |
        Sort-Object -Unique)
}

function Invoke-ControlTool {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    # Native stderr is output, not a PowerShell failure. In Windows
    # PowerShell 5.1, redirecting it while ErrorActionPreference is Stop can
    # otherwise turn a successful UART diagnostic into a terminating error.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $tool @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    $output | Out-Host
    if ($exitCode -ne 0) {
        throw "timecardctl $($Arguments -join ' ') failed with exit code $exitCode."
    }
    ($output | Out-String)
}

function Restore-KnownGoodDriver {
    Write-Warning "Deployment failed. Restoring $KnownGoodInf..."

    $present = @(Get-TimeCardDevice)
    foreach ($device in $present) {
        Invoke-PnpUtil -Arguments @('/remove-device', $device.InstanceId) `
            -AllowFailure | Out-Null
    }

    if ($newInf) {
        Invoke-PnpUtil -Arguments @('/delete-driver', $newInf, '/uninstall',
            '/force') -AllowFailure | Out-Null
    }

    Invoke-PnpUtil -Arguments @('/scan-devices') | Out-Null
    $restored = Wait-TimeCardDevice -ExpectedInf $KnownGoodInf `
        -ExpectedClass $KnownGoodClass
    $restored | Format-List Status, Class, FriendlyName, InstanceId | Out-Host
    Invoke-ControlTool -Arguments @('status') | Out-Null
    Invoke-ControlTool -Arguments @('get') | Out-Null
    Write-Host "Rollback succeeded; $KnownGoodInf is active and healthy."
}

Start-Transcript -Path $log -Force | Out-Null
try {
    foreach ($path in @($package, $catalog, $driver, $tool)) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Required build output not found: $path. Run build.cmd first."
        }
    }

    foreach ($path in @($catalog, $driver)) {
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne 'Valid') {
            throw "Signature validation failed for ${path}: $($signature.Status)."
        }
    }

    $allDrivers = (Invoke-PnpUtil -Arguments @('/enum-drivers')).Output
    if ($allDrivers -notmatch
        "(?im)^Published Name:\s+$([regex]::Escape($KnownGoodInf))\s*$") {
        throw "Known-good rollback package $KnownGoodInf is not staged."
    }
    $unsafeClassGuid = '{49842651-EF23-47D4-BDF6-017A675C87AD}'
    $unsafePackages = @($allDrivers -split
        '(?m)(?=^Published Name:)' | Where-Object {
            $_ -match "(?im)^Class GUID:\s+$([regex]::Escape($unsafeClassGuid))\s*$"
        })
    if ($unsafePackages.Count -ne 0) {
        throw "Legacy unsafe Time Card class $unsafeClassGuid is still staged; refusing deployment."
    }

    $current = @(Get-TimeCardDevice)
    if ($current.Count -ne 1) {
        throw "Expected exactly one present Time Card, found $($current.Count)."
    }
    $current = $current[0]
    $currentInf = Get-DeviceInf $current
    $currentProblem = Get-DeviceProblemCode $current
    if ($current.Status -ne 'OK' -or $currentProblem -ne 0 -or
        $currentInf -ine $KnownGoodInf -or
        $current.Class -ine $KnownGoodClass) {
        throw "Pre-deployment state is not the known-good controller: " +
            "status=$($current.Status), problem=$currentProblem, " +
            "class=$($current.Class), INF=$currentInf."
    }
    Write-Host "Pre-deployment check passed: $KnownGoodInf is healthy."

    $beforePackages = @(Get-TimeCardClassPackages)
    $unexpectedPackages = @($beforePackages | Where-Object {
            $_ -ine $KnownGoodInf
        })
    if ($unexpectedPackages.Count -ne 0) {
        throw "Unexpected TimeCard-class packages are already staged " +
            "($($unexpectedPackages -join ', ')). Refusing an ambiguous deployment."
    }

    Write-Host 'Staging the signed controller-only package...'
    Invoke-PnpUtil -Arguments @('/add-driver', $package) | Out-Null
    $afterPackages = @(Get-TimeCardClassPackages)
    $addedPackages = @($afterPackages | Where-Object {
            $_ -notin $beforePackages
        })
    if ($addedPackages.Count -ne 1) {
        throw "Could not uniquely identify the staged package; found: " +
            "$($addedPackages -join ', ')."
    }
    $newInf = $addedPackages[0]
    if ($newInf -ieq $KnownGoodInf) {
        throw 'The staged package unexpectedly matches the rollback package.'
    }
    Write-Host "New package is $newInf. Subsystem children default to disabled."

    Write-Host 'Removing the current device instance so the old driver unloads...'
    Invoke-PnpUtil -Arguments @('/remove-device', $current.InstanceId) | Out-Null
    $deviceWasRemoved = $true

    Write-Host 'Rescanning PCI devices and starting the new controller...'
    Invoke-PnpUtil -Arguments @('/scan-devices') | Out-Null
    $started = Wait-TimeCardDevice -ExpectedInf $newInf `
        -ExpectedClass 'TimeCard'
    $started | Format-List Status, Class, FriendlyName, InstanceId | Out-Host

    $statusOutput = Invoke-ControlTool -Arguments @('status')
    if ($statusOutput -notmatch
        "(?im)^Driver:\s+$([regex]::Escape($ExpectedDriverVersion))\s*`$") {
        throw "The loaded controller did not report driver implementation " +
            "$ExpectedDriverVersion."
    }
    Invoke-ControlTool -Arguments @('get') | Out-Null
    Invoke-ControlTool -Arguments @('uart-config', '0', '115200') | Out-Null
    Invoke-ControlTool -Arguments @('uart-read', '0', '64', '1000') | Out-Null
    $hierarchy = Invoke-ControlTool -Arguments @('hierarchy-status')
    if ($hierarchy -notmatch
        '(?im)^Runtime subsystem devices:\s+disabled\s*$' -or
        $hierarchy -notmatch
        '(?im)^Enable on next start:\s+no\s*$') {
        throw 'The subsystem hierarchy was not safely disabled by default.'
    }

    $deploymentComplete = $true
    Write-Host ''
    Write-Host "Controller-only deployment succeeded with $newInf."
    Write-Host 'PHC and GNSS UART checks passed; hierarchy remains disabled.'
}
catch {
    $failure = $_
    Write-Host "DEPLOYMENT ERROR: $($failure.Exception.Message)" `
        -ForegroundColor Red
    if ($newInf -or $deviceWasRemoved) {
        try {
            Restore-KnownGoodDriver
        }
        catch {
            throw "Deployment failed: $failure Rollback also failed: $_"
        }
    }
    throw $failure
}
finally {
    Stop-Transcript | Out-Null
}

if (-not $deploymentComplete) {
    exit 1
}
