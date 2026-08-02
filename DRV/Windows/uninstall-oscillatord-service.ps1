#Requires -Version 5.1
#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [switch]$PurgeData
)

$ErrorActionPreference = 'Stop'
$serviceName = 'OcpTimeCardOscillatord'
$displayName = 'OCP Time Card Oscillator Discipline'
$eventSource = 'OCP Time Card Oscillatord'
$timeProviderKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\TimeProviders\OcpTimeCard'
$programFilesRoot = if ($env:ProgramW6432) {
    $env:ProgramW6432
} else {
    $env:ProgramFiles
}
if (-not $programFilesRoot -or -not $env:ProgramData) {
    throw 'Program Files or ProgramData could not be located.'
}

function Get-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [IO.Path]::GetFullPath($Path).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-ExactRemovalPath {
    param(
        [Parameter(Mandatory = $true)][string]$Candidate,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$Description
    )
    $resolvedCandidate = Get-NormalizedPath $Candidate
    $resolvedExpected = Get-NormalizedPath $Expected
    if (-not $resolvedCandidate.Equals($resolvedExpected,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected $Description path: $resolvedCandidate"
    }
    return $resolvedCandidate
}

function Invoke-ServiceControl {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $sc = Join-Path $env:SystemRoot 'System32\sc.exe'
    & $sc @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "sc.exe $($Arguments[0]) failed with exit code $LASTEXITCODE."
    }
}

$expectedInstallDirectory = Join-Path $programFilesRoot `
    'OCP Time Card\Oscillatord'
$expectedDataDirectory = Join-Path $env:ProgramData `
    'OCP Time Card\Oscillatord'
$installDirectory = Assert-ExactRemovalPath $expectedInstallDirectory `
    $expectedInstallDirectory 'installation'
$dataDirectory = Assert-ExactRemovalPath $expectedDataDirectory `
    $expectedDataDirectory 'data'

if (Test-Path -LiteralPath $timeProviderKey) {
    $registeredDll = (Get-ItemProperty -LiteralPath $timeProviderKey `
        -Name DllName -ErrorAction SilentlyContinue).DllName
    if ($registeredDll -and (Get-NormalizedPath $registeredDll).StartsWith(
            $installDirectory + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $timeProviderKey -Recurse -Force
        $windowsTime = Get-Service -Name W32Time -ErrorAction SilentlyContinue
        if ($windowsTime -and $windowsTime.Status -ne 'Stopped') {
            Restart-Service -InputObject $windowsTime -Force
            $windowsTime = Get-Service -Name W32Time
            $windowsTime.WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
        }
        Write-Host 'Unregistered the OCP Time Card W32Time provider.'
    } else {
        throw ('Refusing to remove a W32Time provider whose DLL is outside ' +
            "the product directory: $registeredDll")
    }
}

$service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -ne 'Stopped') {
        Write-Host "Stopping $displayName..."
        Stop-Service -InputObject $service -Force
        $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
    }
    Invoke-ServiceControl @('delete', $serviceName)
}

# Both recursive targets were resolved and compared to their exact expected
# product directories above. No computed parent or wildcard is ever removed.
if (Test-Path -LiteralPath $installDirectory) {
    Remove-Item -LiteralPath $installDirectory -Recurse -Force
    Write-Host "Removed service files: $installDirectory"
}

if ($PurgeData) {
    if (Test-Path -LiteralPath $dataDirectory) {
        Remove-Item -LiteralPath $dataDirectory -Recurse -Force
        Write-Host "Removed service configuration and calibration data: $dataDirectory"
    }
} else {
    Write-Host "Preserved service configuration and calibration data: $dataDirectory"
    Write-Host 'Run again with -PurgeData only if permanent data removal is intended.'
}

if ([Diagnostics.EventLog]::SourceExists($eventSource)) {
    Remove-EventLog -Source $eventSource
}

Write-Host "$displayName was uninstalled."
