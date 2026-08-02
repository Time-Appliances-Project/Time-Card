#Requires -Version 5.1
#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot `
        'TimeCardOscillatord\bin\Release'),
    [switch]$SkipBuild,
    [switch]$NoStart,
    [switch]$EnableWindowsTimeProvider
)

$ErrorActionPreference = 'Stop'
$serviceName = 'OcpTimeCardOscillatord'
$displayName = 'OCP Time Card Oscillator Discipline'
$eventSource = 'OCP Time Card Oscillatord'
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

function Assert-PathBelow {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Description
    )
    $candidate = Get-NormalizedPath $Path
    $parent = Get-NormalizedPath $Root
    $prefix = $parent + [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($prefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must remain below $parent. Resolved path: $candidate"
    }
    return $candidate
}

function Invoke-ServiceControl {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $sc = Join-Path $env:SystemRoot 'System32\sc.exe'
    & $sc @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "sc.exe $($Arguments[0]) failed with exit code $LASTEXITCODE."
    }
}

function Stop-ServiceForUpdate {
    param([Parameter(Mandatory = $true)]$Service)
    if ($Service.Status -eq 'Stopped') {
        return
    }
    Stop-Service -InputObject $Service -Force
    $Service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
}

$installDirectory = Assert-PathBelow `
    (Join-Path $programFilesRoot 'OCP Time Card\Oscillatord') `
    $programFilesRoot 'The service installation directory'
$dataDirectory = Assert-PathBelow `
    (Join-Path $env:ProgramData 'OCP Time Card\Oscillatord') `
    $env:ProgramData 'The service data directory'
$configPath = Join-Path $dataDirectory 'oscillatord.json'
$logsDirectory = Join-Path $dataDirectory 'Logs'
$cardsDirectory = Join-Path $dataDirectory 'Cards'
$timeProviderName = 'OcpTimeCard'
$timeProviderKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\TimeProviders\' +
    $timeProviderName

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-oscillatord-service.cmd') release
    if ($LASTEXITCODE -ne 0) {
        throw "The service build failed with exit code $LASTEXITCODE."
    }
}

$SourceDirectory = Get-NormalizedPath $SourceDirectory
$sourceExecutable = Join-Path $SourceDirectory 'TimeCardOscillatord.exe'
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "Service executable not found: $sourceExecutable"
}
# An existing W32Time process can have the in-tree provider DLL mapped. Only
# pause W32Time when this exact provider registration resolves below our
# product directory; an unrelated or externally managed registration must not
# be disturbed. The prior running state is restored even if deployment fails.
$registeredProviderOwnedByProduct = $false
if (Test-Path -LiteralPath $timeProviderKey) {
    $registeredProviderDll = (Get-ItemProperty -LiteralPath $timeProviderKey `
        -Name DllName -ErrorAction SilentlyContinue).DllName
    if ($registeredProviderDll) {
        try {
            $registeredProviderPath = Get-NormalizedPath $registeredProviderDll
            $productPrefix = $installDirectory +
                [IO.Path]::DirectorySeparatorChar
            $registeredProviderOwnedByProduct =
                $registeredProviderPath.StartsWith($productPrefix,
                    [StringComparison]::OrdinalIgnoreCase)
        } catch {
            Write-Warning ('The existing OcpTimeCard provider path could not ' +
                'be resolved; W32Time will not be stopped: ' +
                $_.Exception.Message)
        }
    }
}
$windowsTime = Get-Service -Name W32Time -ErrorAction SilentlyContinue
$windowsTimeWasRunning = $windowsTime -and
    $windowsTime.Status -ne 'Stopped'
$windowsTimeStoppedForUpdate = $false

$existingService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($existingService) {
    Write-Host "Stopping $displayName for an in-place update..."
    Stop-ServiceForUpdate $existingService
}

New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $logsDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $cardsDirectory -Force | Out-Null

# Only the service and administrators may alter configuration, logs, tokens,
# or card-specific calibration backups. Status is exposed through local IPC.
$icacls = Join-Path $env:SystemRoot 'System32\icacls.exe'
& $icacls $dataDirectory /inheritance:r `
    /grant:r '*S-1-5-18:(OI)(CI)F' `
    '*S-1-5-32-544:(OI)(CI)F' | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Unable to secure the service data directory (icacls $LASTEXITCODE)."
}

$extensions = @('.exe', '.dll', '.config', '.json', '.md', '.txt', '.pdb')
$obsoleteNames = @('TimeCardDiscipline.dll', 'TimeCardDiscipline.pdb')
$releaseFiles = Get-ChildItem -LiteralPath $SourceDirectory -File |
    Where-Object {
        $extensions -contains $_.Extension.ToLowerInvariant() -and
        $obsoleteNames -notcontains $_.Name
    }
try {
    if ($registeredProviderOwnedByProduct -and $windowsTimeWasRunning) {
        Write-Host 'Stopping Windows Time to update its loaded Time Card provider...'
        $windowsTimeStoppedForUpdate = $true
        Stop-ServiceForUpdate $windowsTime
    }
    foreach ($obsoleteName in $obsoleteNames) {
        $obsoletePath = Join-Path $installDirectory $obsoleteName
        if (Test-Path -LiteralPath $obsoletePath -PathType Leaf) {
            Remove-Item -LiteralPath $obsoletePath -Force
        }
    }
    foreach ($file in $releaseFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination `
            (Join-Path $installDirectory $file.Name) -Force
    }
} finally {
    if ($windowsTimeStoppedForUpdate -and $windowsTimeWasRunning) {
        $windowsTime = Get-Service -Name W32Time -ErrorAction SilentlyContinue
        if ($windowsTime) {
            if ($windowsTime.Status -eq 'StopPending') {
                $windowsTime.WaitForStatus('Stopped',
                    [TimeSpan]::FromSeconds(30))
                $windowsTime.Refresh()
            }
            if ($windowsTime.Status -ne 'Running') {
                Start-Service -InputObject $windowsTime
                $windowsTime.WaitForStatus('Running',
                    [TimeSpan]::FromSeconds(30))
            }
        }
        $windowsTimeStoppedForUpdate = $false
    }
}

if ($EnableWindowsTimeProvider) {
    $providerDll = Join-Path $installDirectory 'TimeCardTimeProvider.dll'
    if (-not (Test-Path -LiteralPath $providerDll -PathType Leaf)) {
        throw "Windows Time provider not found after deployment: $providerDll"
    }
    New-Item -Path $timeProviderKey -Force | Out-Null
    New-ItemProperty -Path $timeProviderKey -Name DllName `
        -PropertyType String -Value $providerDll -Force | Out-Null
    New-ItemProperty -Path $timeProviderKey -Name Enabled `
        -PropertyType DWord -Value 1 -Force | Out-Null
    New-ItemProperty -Path $timeProviderKey -Name InputProvider `
        -PropertyType DWord -Value 1 -Force | Out-Null
    Write-Host 'Registered the OCP Time Card as a W32Time hardware input provider.'
}

$destinationExecutable = Join-Path $installDirectory 'TimeCardOscillatord.exe'
if (-not (Test-Path -LiteralPath $destinationExecutable -PathType Leaf)) {
    throw 'The installed service executable is missing after the copy.'
}

# Never overwrite an operator's configuration during install or upgrade.
if (-not (Test-Path -LiteralPath $configPath)) {
    $configurationTemplate = @(
        'oscillatord.example.json',
        'oscillatord.default.json',
        'oscillatord.json.example'
    ) | ForEach-Object { Join-Path $SourceDirectory $_ } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    if ($configurationTemplate) {
        Copy-Item -LiteralPath $configurationTemplate `
            -Destination $configPath
        Write-Host "Created initial configuration: $configPath"
    } else {
        Write-Warning ('No configuration template was present. The service ' +
            'will use its safe built-in defaults until oscillatord.json is created.')
    }
} else {
    Write-Host "Preserved existing configuration: $configPath"
}

$binaryPath = '"' + $destinationExecutable + '" --service'
if ($existingService) {
    Invoke-ServiceControl @('config', $serviceName,
        'binPath=', $binaryPath,
        'start=', 'delayed-auto',
        'obj=', 'LocalSystem',
        'DisplayName=', $displayName)
} else {
    Invoke-ServiceControl @('create', $serviceName,
        'binPath=', $binaryPath,
        'start=', 'delayed-auto',
        'obj=', 'LocalSystem',
        'DisplayName=', $displayName)
}
Invoke-ServiceControl @('description', $serviceName,
    'Disciplines and monitors OCP Time Card oscillators on native Windows.')
Invoke-ServiceControl @('failure', $serviceName,
    'reset=', '86400',
    'actions=', 'restart/5000/restart/15000/restart/60000')
Invoke-ServiceControl @('failureflag', $serviceName, '1')
Invoke-ServiceControl @('sidtype', $serviceName, 'unrestricted')

if (-not [Diagnostics.EventLog]::SourceExists($eventSource)) {
    New-EventLog -LogName Application -Source $eventSource
}

if (-not $NoStart) {
    Start-Service -Name $serviceName
    $installedService = Get-Service -Name $serviceName
    $installedService.WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
    Write-Host "$displayName is running."
} else {
    Write-Host "$displayName was installed but not started."
}


if ($EnableWindowsTimeProvider) {
    # Registration is deliberately opt-in because it changes the computer's
    # system-time source policy.  Reload W32Time only after the publisher
    # service is available (or has been intentionally installed stopped).
    $windowsTime = Get-Service -Name W32Time -ErrorAction SilentlyContinue
    if ($windowsTime -and -not $NoStart) {
        if ($windowsTime.Status -eq 'Stopped') {
            Start-Service -InputObject $windowsTime
        } else {
            Restart-Service -InputObject $windowsTime -Force
        }
        $windowsTime = Get-Service -Name W32Time
        $windowsTime.WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
        & (Join-Path $env:SystemRoot 'System32\w32tm.exe') /resync /rediscover |
            Out-Host
    } elseif ($NoStart) {
        Write-Host ('W32Time registration is ready; start the oscillator ' +
            'service, then restart W32Time to activate it.')
    }
}

Write-Host "Service files: $installDirectory"
Write-Host "Service data:  $dataDirectory"
