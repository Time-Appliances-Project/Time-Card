#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [switch]$StageOnly
)

$ErrorActionPreference = 'Stop'
$package = Join-Path $PSScriptRoot 'x64\Release\timecard\timecard.inf'
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

    Write-Host 'Enabling Windows test-signing for the next boot...'
    & bcdedit.exe /set testsigning on
    if ($LASTEXITCODE -ne 0) {
        throw "bcdedit failed with exit code $LASTEXITCODE"
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

    Write-Host ''
    if ($pnpExitCode -eq 3010 -or $rebootRequired) {
        Write-Host 'Driver package staged. Windows requires a reboot to replace the'
        Write-Host 'currently loaded kernel driver.'
    } else {
        Write-Host 'Driver package staged. Confirm the running version with'
        Write-Host '.\out\timecardctl.exe status from an Administrator prompt.'
    }
    Write-Host 'After the new driver loads, run verify.ps1 as Administrator.'
}
finally {
    Stop-Transcript | Out-Null
}
