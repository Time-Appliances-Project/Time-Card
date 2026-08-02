#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [switch]$StageOnly,
    [switch]$KeepHierarchyDisabled,
    [switch]$EnableTestSigning
)

$ErrorActionPreference = 'Stop'
$package = Join-Path $PSScriptRoot 'x64\Release\timecard\timecard.inf'
$tool = Join-Path $PSScriptRoot 'out\timecardctl.exe'
$log = Join-Path $PSScriptRoot 'install.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    if (-not (Test-Path -LiteralPath $package)) {
        throw "Driver package not found: $package. Run build.cmd first."
    }

    $secureBoot = Get-ItemPropertyValue `
        'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' `
        -Name UEFISecureBootEnabled -ErrorAction SilentlyContinue
    if ($secureBoot -eq 1) {
        throw 'Secure Boot is enabled. A local test-signed driver cannot load.'
    }

    if ($EnableTestSigning) {
        Write-Warning 'Enabling Windows test-signing changes system-wide boot security and requires a reboot.'
        & bcdedit.exe /set testsigning on
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit failed with exit code $LASTEXITCODE"
        }
    } else {
        Write-Host 'Leaving Windows boot-signing policy unchanged.'
        Write-Host 'If pnputil rejects this local package, either use a Microsoft-signed package or explicitly rerun with -EnableTestSigning.'
    }

    Write-Host 'Staging the TimeCard driver package...'
    if ($StageOnly) {
        $pnpOutput = & pnputil.exe /add-driver $package 2>&1
    } else {
        $pnpOutput = & pnputil.exe /add-driver $package /install 2>&1
    }
    $pnpOutput | Out-Host
    $pnpExitCode = $LASTEXITCODE
    $rebootRequired = ($pnpOutput -join "`n") -match
        'reboot is needed|reboot.*required'
    if ($pnpExitCode -ne 0 -and $pnpExitCode -ne 3010 -and
        -not $rebootRequired) {
        throw "pnputil failed with exit code $pnpExitCode"
    }

    $replacementPending = $pnpExitCode -eq 3010 -or $rebootRequired
    if (-not $StageOnly -and -not $KeepHierarchyDisabled -and
        -not $replacementPending) {
        $presentController = Get-PnpDevice -PresentOnly | Where-Object {
            $_.InstanceId -like 'PCI\VEN_1D9B&DEV_0400*' -or
            $_.InstanceId -like 'PCI\VEN_18D4&DEV_1008*' -or
            $_.InstanceId -like 'PCI\VEN_1AD7&DEV_A000*'
        } | Select-Object -First 1
        if ($presentController) {
            if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
                throw "Control tool not found: $tool. Run build.cmd first."
            }
            $statusOutput = & $tool status 2>&1
            $statusExitCode = $LASTEXITCODE
            $statusOutput | Out-Host
            if ($statusExitCode -ne 0 -or
                ($statusOutput -join "`n") -notmatch
                    'ABI:\s+(1[5-9]|[2-9][0-9]+)\b') {
                throw 'The installed controller is not yet running ABI 15 or newer. Reboot before enabling its subsystem hierarchy.'
            }
            & $tool hierarchy-persist
            if ($LASTEXITCODE -ne 0) {
                throw 'The subsystem hierarchy could not be enabled and persisted.'
            }
            pnputil.exe /scan-devices | Out-Host
            Start-Sleep -Seconds 1
            $phcPresent = Get-PnpDevice -PresentOnly | Where-Object {
                $_.InstanceId -like 'TIMECARD\PHC\*'
            } | Select-Object -First 1
            if (-not $phcPresent) {
                Write-Host 'Restarting the Time Card controller to finish the legacy hierarchy migration...'
                & pnputil.exe /restart-device $presentController.InstanceId |
                    Out-Host
                if ($LASTEXITCODE -ne 0) {
                    throw 'The controller could not be restarted after enabling its subsystem hierarchy.'
                }
                pnputil.exe /scan-devices | Out-Host
            }
        }
    }

    Write-Host ''
    if ($replacementPending) {
        Write-Host 'Driver package staged. Windows requires a reboot to replace the'
        Write-Host 'currently loaded kernel driver.'
    } else {
        Write-Host 'Driver package staged. Confirm the running version with'
        Write-Host '.\out\timecardctl.exe status from an Administrator prompt.'
    }
    if ($KeepHierarchyDisabled) {
        Write-Host 'The subsystem hierarchy was left disabled by request.'
    } else {
        Write-Host 'After the new driver loads, run verify.ps1 -ExpectHierarchy as Administrator.'
    }
}
finally {
    Stop-Transcript | Out-Null
}
