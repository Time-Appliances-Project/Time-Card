[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipRuntimeSmoke
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$windowsRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$serviceRoot = Join-Path $windowsRoot 'TimeCardOscillatord'
$buildPath = Join-Path $windowsRoot 'build-oscillatord-service.cmd'
$executablePath = Join-Path $serviceRoot `
    'bin\Release\TimeCardOscillatord.exe'
$templatePath = Join-Path $serviceRoot 'oscillatord.example.json'
$runtimePath = Join-Path $serviceRoot 'OscillatordRuntime.cs'
$monitoringPath = Join-Path $serviceRoot 'MonitoringServer.cs'
$configurationPath = Join-Path $serviceRoot 'OscillatordConfiguration.cs'
$windowsServicePath = Join-Path $serviceRoot 'OscillatordWindowsService.cs'
$timePublisherPath = Join-Path $serviceRoot 'WindowsTimeSamplePublisher.cs'
$timeProviderPath = Join-Path $windowsRoot `
    'TimeCardTimeProvider\timecard_time_provider.c'
$timeProviderExportsPath = Join-Path $windowsRoot `
    'TimeCardTimeProvider\timecard_time_provider.def'
$timeProviderBuildPath = Join-Path $windowsRoot 'build-time-provider.cmd'
$serviceBuildPath = Join-Path $windowsRoot 'build-oscillatord-service.cmd'
$installPath = Join-Path $windowsRoot 'install-oscillatord-service.ps1'
$uninstallPath = Join-Path $windowsRoot 'uninstall-oscillatord-service.ps1'
$gnssGoldenTestPath = Join-Path $PSScriptRoot `
    'test-native-gnss-session.ps1'
$atomicGoldenTestPath = Join-Path $PSScriptRoot 'test-sa5x-manager.ps1'
$rtcmGoldenTestPath = Join-Path $PSScriptRoot `
    'test-native-rtcm-publisher.ps1'
$nativeDisciplinePath = Join-Path $windowsRoot `
    'TimeCardControlCenter\NativeDiscipline.cs'
$timeCardClientPath = Join-Path $windowsRoot `
    'TimeCardControlCenter\TimeCardClient.cs'
$ioctlHeaderPath = Join-Path $windowsRoot 'include\timecard_ioctl.h'

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw "oscillatord service test failed: $Message"
    }
}

function Assert-Matches {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Message
    )
    Assert-True ([regex]::IsMatch($Text, $Pattern,
        [Text.RegularExpressions.RegexOptions]::IgnoreCase)) $Message
}

function Assert-NotMatches {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Message
    )
    Assert-True (-not [regex]::IsMatch($Text, $Pattern,
        [Text.RegularExpressions.RegexOptions]::IgnoreCase)) $Message
}

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value.IndexOf('"') -ge 0) {
        throw 'A test process argument unexpectedly contains a quote.'
    }
    return '"' + $Value + '"'
}

function Invoke-TestProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string]$Arguments
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.Arguments = $Arguments
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    Assert-True $process.Start() "could not start $FilePath"
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        Assert-True ($process.WaitForExit(30000)) `
            "$FilePath did not exit within 30 seconds"
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            StdOut = $stdout.GetAwaiter().GetResult()
            StdErr = $stderr.GetAwaiter().GetResult()
        }
    } finally {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Get-FreeTcpPort {
    $listener = [Net.Sockets.TcpListener]::new(
        [Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try {
        return ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
}

function Invoke-TcpJson {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Json
    )
    $client = [Net.Sockets.TcpClient]::new()
    $client.ReceiveTimeout = 5000
    $client.SendTimeout = 5000
    try {
        $client.Connect([Net.IPAddress]::Loopback, $Port)
        $stream = $client.GetStream()
        $request = [Text.Encoding]::UTF8.GetBytes($Json)
        $stream.Write($request, 0, $request.Length)
        $stream.Flush()
        $response = [IO.MemoryStream]::new()
        try {
            $buffer = [byte[]]::new(4096)
            while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $response.Write($buffer, 0, $count)
                Assert-True ($response.Length -le 1048576) `
                    'TCP response exceeded the documented protocol limit'
            }
            return [Text.Encoding]::UTF8.GetString($response.ToArray())
        } finally {
            $response.Dispose()
        }
    } finally {
        $client.Dispose()
    }
}

function Invoke-PipeJson {
    param(
        [Parameter(Mandatory = $true)][string]$PipeName,
        [Parameter(Mandatory = $true)][string]$Json
    )
    $pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $PipeName,
        [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::None,
        [Security.Principal.TokenImpersonationLevel]::Impersonation)
    try {
        $pipe.Connect(5000)
        $request = [Text.Encoding]::UTF8.GetBytes($Json)
        $pipe.Write($request, 0, $request.Length)
        $pipe.Flush()
        $response = [IO.MemoryStream]::new()
        try {
            $buffer = [byte[]]::new(4096)
            while (($count = $pipe.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $response.Write($buffer, 0, $count)
                Assert-True ($response.Length -le 1048576) `
                    'named-pipe response exceeded the protocol limit'
            }
            return [Text.Encoding]::UTF8.GetString($response.ToArray())
        } finally {
            $response.Dispose()
        }
    } finally {
        $pipe.Dispose()
    }
}

foreach ($path in @($buildPath, $templatePath, $runtimePath,
        $monitoringPath, $configurationPath, $windowsServicePath,
        $timePublisherPath, $timeProviderPath, $timeProviderExportsPath,
        $timeProviderBuildPath, $installPath, $uninstallPath,
        $gnssGoldenTestPath, $atomicGoldenTestPath, $rtcmGoldenTestPath,
        $nativeDisciplinePath, $ioctlHeaderPath)) {
    Assert-True (Test-Path -LiteralPath $path -PathType Leaf) `
        "required service source is missing: $path"
}

if (-not $SkipBuild) {
    Write-Host '=== Building native Windows oscillatord ==='
    & $buildPath release
    Assert-True ($LASTEXITCODE -eq 0) `
        "the native Windows service build exited with $LASTEXITCODE"
}
Assert-True (Test-Path -LiteralPath $executablePath -PathType Leaf) `
    "service executable is missing: $executablePath"
Assert-True (Test-Path -LiteralPath (Join-Path (Split-Path $executablePath) `
        'TimeCardDiscipline.dll') -PathType Leaf) `
    'the native miniCOD DLL is not beside the service executable'
Assert-True (Test-Path -LiteralPath (Join-Path (Split-Path $executablePath) `
        'TimeCardTimeProvider.dll') -PathType Leaf) `
    'the native W32Time provider DLL is not beside the service executable'

$runtimeSource = Get-Content -LiteralPath $runtimePath -Raw
$monitoringSource = Get-Content -LiteralPath $monitoringPath -Raw
$configurationSource = Get-Content -LiteralPath $configurationPath -Raw
$windowsServiceSource = Get-Content -LiteralPath $windowsServicePath -Raw
$timePublisherSource = Get-Content -LiteralPath $timePublisherPath -Raw
$timeProviderSource = Get-Content -LiteralPath $timeProviderPath -Raw
$timeProviderExports = Get-Content -LiteralPath $timeProviderExportsPath -Raw
$serviceBuildSource = Get-Content -LiteralPath $serviceBuildPath -Raw
$installSource = Get-Content -LiteralPath $installPath -Raw
$uninstallSource = Get-Content -LiteralPath $uninstallPath -Raw
$nativeSource = Get-Content -LiteralPath $nativeDisciplinePath -Raw
$timeCardClientSource = Get-Content -LiteralPath $timeCardClientPath -Raw
$ioctlSource = Get-Content -LiteralPath $ioctlHeaderPath -Raw
$executableSource = [string]::Join("`n", @(
    Get-ChildItem -LiteralPath $serviceRoot -Filter '*.cs' -File |
        ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }
))

Write-Host '=== Checking native-service safety invariants ==='
Assert-Matches $ioctlSource '#define\s+TIMECARD_ABI_VERSION\s+15u' `
    'the service build is not paired with the current driver ABI'
Assert-Matches $ioctlSource 'IOCTL_TIMECARD_DISCIPLINE_LEASE' `
    'the driver discipline-lease IOCTL is missing'
Assert-Matches $nativeSource 'AbiVersion\s*<\s*14u' `
    'the engine does not reject drivers without the safe lease ABI'
Assert-Matches $nativeSource 'AcquireDisciplineLease\s*\(' `
    'the engine does not acquire exclusive driver ownership'
Assert-Matches $nativeSource 'ReleaseDisciplineLease\s*\(' `
    'the engine does not release exclusive driver ownership'
Assert-Matches $nativeSource 'SetPhaseMeter\s*\(false' `
    'the engine has no phase-capture fail-safe cleanup'
Assert-Matches $nativeSource `
    'HasDirectMro50\s*\|\|\s*!capabilities\.HasPairedPhaseMeter' `
    'miniCOD is not gated on both mRO-50 and paired phase capabilities'
Assert-Matches $nativeSource 'Stopwatch\.GetTimestamp\s*\(' `
    'discipline freshness and persistence must use monotonic time'
Assert-Matches $nativeSource 'Cards"\s*,\s*cardKey' `
    'discipline persistence is not isolated per card'
Assert-Matches $nativeSource 'payload\[23\]\s*>\s*3u' `
    'GNSS validity does not enforce a useful NAV-PVT satellite count'
Assert-Matches $nativeSource 'SurveyDurationSeconds\s*>=\s*1200u' `
    'survey validity does not enforce the upstream minimum duration'
Assert-Matches $nativeSource `
    'new\s+NativeGnssSessionManager\(client,\s*0u' `
    'native discipline is not using the associated-epoch GNSS session'
Assert-Matches $nativeSource 'RunPassiveAsync\(gnssCancellation\.Token\)' `
    'native discipline does not run the continuous passive GNSS reader'
Assert-Matches $nativeSource 'snapshot\.LatestAssociatedEpoch' `
    'native discipline does not consume associated GNSS epochs'
Assert-Matches $nativeSource `
    'PreviousQuantizationErrorPicoseconds\s*\?\?\s*0' `
    'native discipline does not preserve upstream qErr(n-1) semantics'
Assert-Matches $nativeSource 'AlignPhcFromGnssAsync\s*\(' `
    'native discipline does not run the cold-start PHC alignment sequence'
Assert-Matches $nativeSource `
    'SetClockUtc\(recommendation\.TargetUnixSeconds' `
    'PHC startup alignment does not apply validated GNSS UTC'
Assert-Matches $windowsServiceSource 'class\s+OscillatordWindowsService\s*:\s*ServiceBase' `
    'the native process is not hosted as a Windows Service'
Assert-Matches $windowsServiceSource 'PowerBroadcastStatus\.Suspend' `
    'suspend-safe service handling is missing'
Assert-Matches $windowsServiceSource 'PowerBroadcastStatus\.Resume' `
    'resume handling is missing'
Assert-Matches $runtimeSource `
    'HasDirectMro50\s*&&\s*\r?\n\s*candidateCapabilities\.HasPairedPhaseMeter' `
    'runtime capability gating does not protect non-ART variants'
Assert-Matches $runtimeSource 'WAITING_FOR_DEVICE' `
    'card hot-plug/removal retry state is missing'
Assert-Matches $runtimeSource 'FixedTimeEquals\s*\(' `
    'TCP control tokens are not compared with the fixed-time helper'
Assert-Matches $runtimeSource 'Sha256\s*\(parameters\.Data\)' `
    'EEPROM monitoring does not return integrity metadata'
Assert-NotMatches $runtimeSource 'SetClockFromSystem\s*\(' `
    'oscillator discipline must not repeatedly step Windows wall time'
Assert-NotMatches $executableSource `
    '(wsl\.exe|bash\.exe|Process\.Start\s*\(|/dev/ptp|chronyd|ntpshm)' `
    'the native service contains a Linux/WSL process dependency'
Assert-Matches $monitoringSource 'MaximumMessageBytes\s*=\s*1024\s*\*\s*1024' `
    'monitoring does not have the 1 MiB message limit'
Assert-Matches $monitoringSource 'CancelAfter\(TimeSpan\.FromSeconds\(5\)\)' `
    'monitoring requests do not have a five-second deadline'
Assert-Matches $monitoringSource 'parsed\.Request\s*<\s*0\s*\|\|\s*parsed\.Request\s*>\s*13' `
    'monitoring does not reject request numbers outside the protocol enum'
Assert-Matches $monitoringSource 'BuiltinAdministratorsSid' `
    'the named-pipe ACL is missing Administrators'
Assert-Matches $monitoringSource 'AuthenticatedUserSid' `
    'authenticated local users cannot read service status'
Assert-Matches $monitoringSource 'IsClientAdministrator\s*\(' `
    'the named-pipe control path does not verify administrator identity'
Assert-Matches $monitoringSource 'IsLocalClient\s*\(pipe\)' `
    'the named-pipe endpoint does not reject remote clients'
Assert-Matches $monitoringSource `
    'SemaphoreSlim\(16,\s*16\)[\s\S]*handlers\.WaitAsync\(0,\s*token\)' `
    'the TCP endpoint does not bound concurrent request handlers'
Assert-Matches $monitoringSource `
    'HashSet<TcpClient>\s+activeClients[\s\S]*CloseActiveClients\s*\(' `
    'TCP shutdown does not own and close accepted clients'
Assert-Matches $monitoringSource `
    'HashSet<Task>\s+activeHandlers[\s\S]*AwaitHandlersAsync\s*\(' `
    'TCP shutdown does not await accepted-client handlers'
Assert-Matches $monitoringSource `
    'NamedPipeMonitoringServer[\s\S]*async\s+Task\s+StopAsync\s*\([\s\S]*await\s+activeWorker' `
    'named-pipe shutdown does not await its worker'
Assert-Matches $runtimeSource `
    'StopCoreAsync\(source\)\.GetAwaiter\(\)\.GetResult\(\)' `
    'runtime startup does not perform transactional rollback'
Assert-Matches $windowsServiceSource `
    'catch[\s\S]*startedRuntime\.StopAsync\(\)' `
    'Windows Service startup does not roll back a partial runtime'
Assert-Matches $configurationSource `
    'MonitoringControlToken\.Length\s*<\s*32' `
    'TCP mutation tokens are not required to have sufficient length'
Assert-Matches $configurationSource 'Unknown oscillatord setting' `
    'configuration keys are not validated strictly'
Assert-Matches $serviceBuildSource 'build-time-provider\.cmd' `
    'the W32Time input provider is not built with the service release'
Assert-Matches $serviceBuildSource 'TimeCardTimeProvider\.dll' `
    'the W32Time input provider is not staged with the service release'
Assert-Matches $timePublisherSource `
    'Global\\{2}OcpTimeCard\.TimeSample\.v1' `
    'the native sample publisher uses the wrong shared mapping'
Assert-Matches $timeProviderSource `
    'Global\\{2}OcpTimeCard\.TimeSample\.v1' `
    'the W32Time provider uses the wrong shared mapping'
Assert-Matches $timePublisherSource 'Thread\.MemoryBarrier\s*\(' `
    'the sample publisher does not use a seqlock memory barrier'
Assert-Matches $timeCardClientSource `
    'tickBefore\s*=\s*GetTickCount64\(\)[\s\S]{0,300}IoctlGetCrossTimestamp[\s\S]{0,300}tickAfter\s*=\s*GetTickCount64\(\)' `
    'the W32Time freshness epoch is not captured around the cross timestamp'
Assert-Matches $timePublisherSource `
    'view\.Write\(48,\s*card\.SampleTickMilliseconds\)' `
    'the W32Time bridge freshness is anchored to publication instead of capture'
Assert-Matches $timePublisherSource `
    'MaximumSampleAgeMilliseconds[\s\S]{0,400}Invalidate\(\)' `
    'the publisher does not reject a cross timestamp delayed before publication'
Assert-Matches $timePublisherSource 'LocalSystemSid' `
    'the W32Time sample bridge does not grant its publisher access'
Assert-Matches $timePublisherSource 'LocalServiceSid' `
    'the W32Time sample bridge does not grant the W32Time identity read access'
Assert-Matches (Get-Content -LiteralPath $timeProviderBuildPath -Raw) `
    'vcvars64\.bat' `
    'the W32Time provider build is not pinned to the x64 toolchain'
Assert-Matches $timeProviderSource `
    'before\s*==\s*after\s*&&\s*\(after\s*&\s*1u\)\s*==\s*0u' `
    'the W32Time provider does not verify a stable seqlock generation'
Assert-Matches $timeProviderSource 'TC_SAMPLE_MAX_AGE_MS\s+5000ull' `
    'the W32Time provider does not reject stale samples after five seconds'
Assert-Matches $timeProviderSource 'TC_MAX_OFFSET_100NS\s+\(86400ll' `
    'the W32Time provider does not bound a malformed phase offset'
Assert-Matches $timeProviderSource `
    'TC_TIME_FLAG_PRESENT\s*\|\s*\r?\n\s*TC_TIME_FLAG_SYNCHRONIZED' `
    'the W32Time provider can publish an unsynchronized card sample'
Assert-Matches $timeProviderSource `
    'callbacks->dwSize\s*<\s*TC_REQUIRED_CALLBACK_SIZE' `
    'the W32Time provider accepts a truncated callback table'
Assert-Matches $timeProviderSource `
    'case\s+TPC_TimeJumped[\s\S]{0,900}RejectSamplesThroughTick' `
    'the W32Time provider does not discard samples after a system time jump'
Assert-Matches $timeProviderSource `
    'bridge->SystemTickMilliseconds\s*<=\s*\(uint64_t\)reject_through' `
    'the W32Time time-jump barrier is not enforced against bridge freshness'
Assert-Matches $timeProviderSource `
    'AcquireSRWLockShared\(&TcProviderRundownLock\)[\s\S]*ReleaseSRWLockShared' `
    'TimeProvCommand is not protected by provider-context rundown'
Assert-Matches $timeProviderSource `
    'WaitForSingleObject\(context->Worker,\s*INFINITE\)[\s\S]{0,500}AcquireSRWLockExclusive' `
    'TimeProvClose can free provider context while a command is active'
foreach ($export in @('TimeProvOpen', 'TimeProvCommand', 'TimeProvClose')) {
    Assert-Matches $timeProviderExports ('\b' + $export + '\b') `
        "the W32Time provider is missing export $export"
}
Assert-Matches $installSource 'switch\]\$EnableWindowsTimeProvider' `
    'W32Time provider registration is not an explicit install opt-in'
Assert-Matches $installSource `
    'W32Time\\TimeProviders\\' `
    'the installer does not use a dedicated W32Time provider key'
Assert-Matches $installSource `
    "timeProviderName\s*=\s*'OcpTimeCard'" `
    'the installer uses the wrong W32Time provider name'
Assert-Matches $installSource 'InputProvider[\s\S]{0,100}DWord[\s\S]{0,40}1' `
    'the installer does not register a W32Time input provider'
Assert-Matches $installSource `
    'registeredProviderPath\.StartsWith\(\$productPrefix[\s\S]{0,120}OrdinalIgnoreCase' `
    'the installer can stop W32Time for a provider outside the product directory'
Assert-Matches $installSource `
    'registeredProviderOwnedByProduct[\s\S]{0,180}Stop-ServiceForUpdate\s+\$windowsTime' `
    'the installer can overwrite a loaded product-owned W32Time provider'
Assert-Matches $installSource `
    'finally\s*\{[\s\S]{0,800}Start-Service\s+-InputObject\s+\$windowsTime' `
    'the installer does not restore W32Time after provider-copy success or failure'
Assert-Matches $uninstallSource `
    'Refusing to remove a W32Time provider whose DLL is outside' `
    'W32Time unregistration is not constrained to the product DLL'

$templateText = Get-Content -LiteralPath $templatePath -Raw
$template = $templateText | ConvertFrom-Json
Assert-True ($template.schemaVersion -eq 1) `
    'the example configuration has the wrong schema version'
Assert-True ($template.socketAddress -eq '127.0.0.1') `
    'the example TCP endpoint must be loopback-only'
Assert-True (-not $template.monitoringAllowControl) `
    'the example must keep TCP mutation disabled'
Assert-True ([string]::IsNullOrEmpty($template.devicePath) -and
    [string]::IsNullOrEmpty($template.deviceSerial)) `
    'the example must leave stable card selectors opt-in'
Assert-True $template.windowsTimePublisher `
    'the example must make W32Time samples available for opt-in registration'

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('timecard-oscillatord-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null
$resolvedTempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$resolvedTemporaryRoot = [IO.Path]::GetFullPath($temporaryRoot)
Assert-True ($resolvedTemporaryRoot.StartsWith($resolvedTempBase,
        [StringComparison]::OrdinalIgnoreCase) -and
    ([IO.Path]::GetFileName($resolvedTemporaryRoot) -like
        'timecard-oscillatord-test-*')) `
    'temporary test directory resolved outside the expected temp root'

try {
    $stagedRelease = Join-Path $temporaryRoot 'release'
    [IO.Directory]::CreateDirectory($stagedRelease) | Out-Null
    Get-ChildItem -LiteralPath (Split-Path $executablePath) -File |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination `
                (Join-Path $stagedRelease $_.Name)
        }
    $testExecutablePath = Join-Path $stagedRelease `
        'TimeCardOscillatord.exe'
    Assert-True (Test-Path -LiteralPath $testExecutablePath -PathType Leaf) `
        'the isolated service-test copy was not created'

    Write-Host '=== Running native GNSS, RTCM, and atomic-clock golden suites ==='
    foreach ($goldenTest in @($gnssGoldenTestPath,
            $atomicGoldenTestPath, $rtcmGoldenTestPath)) {
        $golden = Invoke-TestProcess `
            (Join-Path $PSHOME 'powershell.exe') (
                '-NoLogo -NoProfile -ExecutionPolicy Bypass -File ' +
                (Quote-ProcessArgument $goldenTest))
        Assert-True ($golden.ExitCode -eq 0) `
            ((Split-Path $goldenTest -Leaf) + ' failed: ' +
                $golden.StdErr + $golden.StdOut)
        if (-not [string]::IsNullOrWhiteSpace($golden.StdOut)) {
            Write-Host $golden.StdOut.Trim()
        }
    }

    Write-Host '=== Validating configuration and migration ==='
    $validCli = Invoke-TestProcess $testExecutablePath (
        '--validate-config --config ' + (Quote-ProcessArgument $templatePath))
    Assert-True ($validCli.ExitCode -eq 0) `
        ("valid example failed CLI validation: " + $validCli.StdErr)
    Assert-Matches $validCli.StdOut 'Configuration valid:' `
        'the validation CLI did not confirm the resolved configuration'

    # Byte-array loading prevents this PowerShell process from locking either
    # the real build output or the temporary executable during reflection.
    $assembly = [Reflection.Assembly]::Load(
        [IO.File]::ReadAllBytes($testExecutablePath))
    $configurationType = $assembly.GetType(
        'TimeCardControlCenter.OscillatordConfiguration', $true)
    $loadConfiguration = $configurationType.GetMethod('Load',
        [Reflection.BindingFlags]'Public,Static')
    Assert-True ($null -ne $loadConfiguration) `
        'configuration loader was not found in the service assembly'

    function Invoke-ConfigurationLoad {
        param([Parameter(Mandatory = $true)][string]$Path)
        return $loadConfiguration.Invoke($null, [object[]]@($Path))
    }

    function Write-ConfigurationVariant {
        param(
            [Parameter(Mandatory = $true)][string]$Name,
            [Parameter(Mandatory = $true)][scriptblock]$Mutation
        )
        $value = $templateText | ConvertFrom-Json
        & $Mutation $value
        $path = Join-Path $temporaryRoot $Name
        Write-Utf8NoBom $path ($value | ConvertTo-Json -Depth 8)
        return $path
    }

    function Assert-ConfigurationRejected {
        param(
            [Parameter(Mandatory = $true)][string]$Path,
            [Parameter(Mandatory = $true)][string]$Description
        )
        $rejected = $false
        try {
            Invoke-ConfigurationLoad $Path | Out-Null
        } catch {
            $rejected = $true
        }
        Assert-True $rejected "invalid configuration accepted: $Description"
    }

    $invalidConfigurations = @(
        [pscustomobject]@{
            Description = 'unsupported schema version'
            Path = Write-ConfigurationVariant 'bad-schema.json' {
                param($value) $value.schemaVersion = 2
            }
        },
        [pscustomobject]@{
            Description = 'all functions disabled'
            Path = Write-ConfigurationVariant 'bad-disabled.json' {
                param($value)
                $value.disciplining = $false
                $value.monitoring = $false
            }
        },
        [pscustomobject]@{
            Description = 'unknown key'
            Path = Write-ConfigurationVariant 'bad-unknown.json' {
                param($value)
                $value | Add-Member -NotePropertyName 'disciplineTypo' `
                    -NotePropertyValue 1
            }
        },
        [pscustomobject]@{
            Description = 'short TCP control token'
            Path = Write-ConfigurationVariant 'bad-token.json' {
                param($value)
                $value.monitoringAllowControl = $true
                $value.monitoringControlToken = 'too-short'
            }
        },
        [pscustomobject]@{
            Description = 'unsafe pipe name'
            Path = Write-ConfigurationVariant 'bad-pipe.json' {
                param($value) $value.namedPipe = 'bad\pipe'
            }
        },
        [pscustomobject]@{
            Description = 'phase resolution outside bounds'
            Path = Write-ConfigurationVariant 'bad-phase.json' {
                param($value) $value.phaseResolutionNs = 0
            }
        },
        [pscustomobject]@{
            Description = 'startup alignment timeout outside bounds'
            Path = Write-ConfigurationVariant 'bad-startup-timeout.json' {
                param($value) $value.startupAlignmentTimeoutSeconds = 9
            }
        },
        [pscustomobject]@{
            Description = 'startup alignment settling time outside bounds'
            Path = Write-ConfigurationVariant 'bad-startup-settling.json' {
                param($value) $value.startupAlignmentSettlingSeconds = 61
            }
        },
        [pscustomobject]@{
            Description = 'wrong JSON type'
            Path = Write-ConfigurationVariant 'bad-type.json' {
                param($value) $value.monitoring = 'yes'
            }
        },
        [pscustomobject]@{
            Description = 'negative device index'
            Path = Write-ConfigurationVariant 'bad-device.json' {
                param($value) $value.deviceIndex = -1
            }
        },
        [pscustomobject]@{
            Description = 'arbitrary device path'
            Path = Write-ConfigurationVariant 'bad-device-path.json' {
                param($value) $value.devicePath = 'C:\Windows\System32'
            }
        },
        [pscustomobject]@{
            Description = 'malformed card serial'
            Path = Write-ConfigurationVariant 'bad-device-serial.json' {
                param($value) $value.deviceSerial = 'not-a-mac'
            }
        }
    )
    $malformedPath = Join-Path $temporaryRoot 'malformed.json'
    Write-Utf8NoBom $malformedPath '{not-json'
    $invalidConfigurations += [pscustomobject]@{
        Description = 'malformed JSON'
        Path = $malformedPath
    }
    foreach ($invalid in $invalidConfigurations) {
        Assert-ConfigurationRejected $invalid.Path $invalid.Description
    }

    $invalidCli = Invoke-TestProcess $testExecutablePath (
        '--validate-config --config ' +
        (Quote-ProcessArgument $invalidConfigurations[2].Path))
    Assert-True ($invalidCli.ExitCode -ne 0) `
        'invalid configuration returned a successful CLI exit code'
    Assert-Matches $invalidCli.StdErr 'Unknown oscillatord setting' `
        'the validation CLI did not explain the unknown setting'

    $legacyPath = Join-Path $temporaryRoot 'oscillatord.conf'
    $importedPath = Join-Path $temporaryRoot 'imported.json'
    Write-Utf8NoBom $legacyPath @'
# representative upstream settings
disciplining=true
monitoring=true
timecard-index=3
socket-address=127.0.0.1
socket-port=2968
reactivity_max=44
tracking_only=true
phc=/dev/ptp0
'@
    $import = Invoke-TestProcess $testExecutablePath (
        '--import-config ' + (Quote-ProcessArgument $legacyPath) +
        ' --config ' + (Quote-ProcessArgument $importedPath))
    Assert-True ($import.ExitCode -eq 0) `
        ("legacy import failed: " + $import.StdErr)
    Invoke-ConfigurationLoad $importedPath | Out-Null
    $imported = Get-Content -LiteralPath $importedPath -Raw |
        ConvertFrom-Json
    Assert-True ($imported.deviceIndex -eq 3) `
        'legacy timecard-index was not migrated'
    Assert-True ($imported.socketPort -eq 2968) `
        'legacy socket-port was not migrated'
    Assert-True ($imported.reactivityMax -eq 44) `
        'legacy miniCOD reactivity was not migrated'
    Assert-True $imported.trackingOnly `
        'legacy tracking_only was not migrated'
    Assert-True (-not ($imported.PSObject.Properties.Name -contains 'phc')) `
        'a Linux-only PHC device path leaked into Windows configuration'

    $invalidLegacyPath = Join-Path $temporaryRoot 'invalid-oscillatord.conf'
    $protectedImportPath = Join-Path $temporaryRoot 'protected-import.json'
    Copy-Item -LiteralPath $templatePath -Destination $protectedImportPath
    $protectedImportBefore = Get-Content -LiteralPath $protectedImportPath -Raw
    Write-Utf8NoBom $invalidLegacyPath @'
disciplining=true
monitoring=true
phase_resolution_ns=0
'@
    $invalidImport = Invoke-TestProcess $testExecutablePath (
        '--import-config ' + (Quote-ProcessArgument $invalidLegacyPath) +
        ' --config ' + (Quote-ProcessArgument $protectedImportPath))
    Assert-True ($invalidImport.ExitCode -ne 0) `
        'an invalid legacy import unexpectedly succeeded'
    Assert-True ((Get-Content -LiteralPath $protectedImportPath -Raw) -ceq
        $protectedImportBefore) `
        'an invalid legacy import replaced the existing configuration'
    Assert-True (@(Get-ChildItem -LiteralPath $temporaryRoot -Filter `
        'protected-import.json.tmp-*').Count -eq 0) `
        'an invalid legacy import left a temporary file behind'

    Write-Host '=== Checking stable multi-card selection and persistence keys ==='
    $selectorType = $assembly.GetType(
        'TimeCardControlCenter.TimeCardDeviceSelector', $true)
    $selectPath = $selectorType.GetMethod('SelectPath',
        [Reflection.BindingFlags]'Public,Static')
    Assert-True ($null -ne $selectPath) `
        'the stable Time Card selector was not found'
    $pathA = '\\?\pci#ven_1d9b&dev_0400#slot-a'
    $pathB = '\\?\pci#ven_1d9b&dev_0400#slot-b'
    $availablePaths = [string[]]@($pathA, $pathB)
    $serialLookup = @{
        $pathA = '00:11:22:33:44:55'
        $pathB = 'AA:BB:CC:DD:EE:FF'
    }
    $serialResolver = [Func[string,string]] {
        param([string]$path)
        return [string]$serialLookup[$path]
    }
    function Invoke-DeviceSelection {
        param([string]$Path, [string]$Serial)
        return $selectPath.Invoke($null, [object[]]@(
            $availablePaths, $Path, $Serial, $serialResolver))
    }
    $pathAUpper = $pathA.ToUpperInvariant()
    Assert-True ((Invoke-DeviceSelection $pathAUpper '') -ceq
        $pathA) 'devicePath selection is not case-insensitive and exact'
    Assert-True ((Invoke-DeviceSelection '' 'aa-bb-cc-dd-ee-ff') -ceq
        $pathB) 'deviceSerial selection did not resolve the matching card'
    Assert-True ((Invoke-DeviceSelection $pathB 'AA:BB:CC:DD:EE:FF') -ceq
        $pathB) 'combined path/serial selection did not cross-check correctly'
    $selectionRejected = $false
    try {
        Invoke-DeviceSelection $pathA 'AA:BB:CC:DD:EE:FF' | Out-Null
    } catch {
        $selectionRejected = $true
    }
    Assert-True $selectionRejected `
        'a path/serial mismatch was not rejected fail-closed'
    $serialLookup[$pathB] = $serialLookup[$pathA]
    $selectionRejected = $false
    try {
        Invoke-DeviceSelection '' $serialLookup[$pathA] | Out-Null
    } catch {
        $selectionRejected = $true
    }
    Assert-True $selectionRejected `
        'duplicate card serials were not rejected as ambiguous'

    $engineType = $assembly.GetType(
        'TimeCardControlCenter.NativeDisciplineEngine', $true)
    $stableCardKey = $engineType.GetMethod('StableCardKey',
        [Reflection.BindingFlags]'Static,NonPublic')
    Assert-True ($null -ne $stableCardKey) `
        'the card-specific persistence key function was not found'
    $serialKey = $stableCardKey.Invoke($null,
        [object[]]@('00:11:22:33:44:55', $pathA))
    $pathKeyA = $stableCardKey.Invoke($null, [object[]]@($null, $pathA))
    $pathKeyACase = $stableCardKey.Invoke($null,
        [object[]]@($null, $pathA.ToUpperInvariant()))
    $pathKeyB = $stableCardKey.Invoke($null, [object[]]@($null, $pathB))
    $malformedSerialKey = $stableCardKey.Invoke($null,
        [object[]]@('..\shared-profile', $pathA))
    Assert-True ($serialKey -ceq '00-11-22-33-44-55') `
        'valid hardware serials no longer retain their persistence key'
    Assert-True ($pathKeyA -ceq $pathKeyACase -and
        $pathKeyA -cne $pathKeyB) `
        'device-path fallback persistence keys are not stable and isolated'
    Assert-True ($malformedSerialKey -ceq $pathKeyA) `
        'a malformed hardware serial escaped the device-path hash isolation'

    Write-Host '=== Checking protocol framing and request enum ==='
    $requestType = $assembly.GetType(
        'TimeCardControlCenter.OscillatordRequest', $true)
    $expectedRequests = [ordered]@{
        Status = 0
        Calibration = 1
        GnssStart = 2
        GnssStop = 3
        GnssSoftReset = 4
        GnssHardReset = 5
        GnssColdReset = 6
        ReadEeprom = 7
        SaveEeprom = 8
        FakeHoldoverStart = 9
        FakeHoldoverStop = 10
        MroCoarseIncrement = 11
        MroCoarseDecrement = 12
        ResetUbloxSerial = 13
    }
    $actualNames = [Enum]::GetNames($requestType)
    Assert-True ($actualNames.Count -eq $expectedRequests.Count) `
        'the request enum changed without a protocol-version update'
    foreach ($name in $expectedRequests.Keys) {
        Assert-True ($actualNames -contains $name) `
            "monitoring request $name is missing"
        $actual = [Convert]::ToInt32([Enum]::Parse($requestType, $name))
        Assert-True ($actual -eq $expectedRequests[$name]) `
            "monitoring request $name has the wrong wire value"
    }

    $wireType = $assembly.GetType(
        'TimeCardControlCenter.MonitoringWire', $true)
    $maximumField = $wireType.GetField('MaximumMessageBytes',
        [Reflection.BindingFlags]'Public,Static')
    $maximumBytes = [int]$maximumField.GetRawConstantValue()
    Assert-True ($maximumBytes -eq 1048576) `
        'wire message limit is not exactly 1 MiB'
    $readRequest = $wireType.GetMethod('ReadRequestAsync',
        [Reflection.BindingFlags]'Public,Static')
    $writeResponse = $wireType.GetMethod('WriteResponseAsync',
        [Reflection.BindingFlags]'Public,Static')
    Assert-True ($null -ne $readRequest -and $null -ne $writeResponse) `
        'monitoring wire methods were not found'

    function Invoke-WireRead {
        param([Parameter(Mandatory = $true)][byte[]]$Bytes)
        $stream = [IO.MemoryStream]::new($Bytes, $false)
        try {
            $arguments = [object[]]::new(2)
            $arguments[0] = $stream
            $arguments[1] = [Threading.CancellationToken]::None
            $task = $readRequest.Invoke($null, $arguments)
            return $task.GetAwaiter().GetResult()
        } finally {
            $stream.Dispose()
        }
    }

    foreach ($number in 0..13) {
        $wire = Invoke-WireRead ([Text.Encoding]::UTF8.GetBytes(
            ('{{"request":{0},"token":"test"}}' -f $number)))
        Assert-True ($wire.Request -eq $number) `
            "wire decoder changed request $number"
        Assert-True ($wire.Token -eq 'test') `
            "wire decoder lost the token for request $number"
    }
    foreach ($badRequest in @(-1, 14)) {
        $rejected = $false
        try {
            Invoke-WireRead ([Text.Encoding]::UTF8.GetBytes(
                ('{{"request":{0}}}' -f $badRequest))) | Out-Null
        } catch {
            $rejected = $true
        }
        Assert-True $rejected `
            "wire decoder accepted request number $badRequest"
    }
    $malformedRejected = $false
    try {
        Invoke-WireRead ([Text.Encoding]::UTF8.GetBytes('{')) | Out-Null
    } catch {
        $malformedRejected = $true
    }
    Assert-True $malformedRejected 'wire decoder accepted incomplete JSON'

    $oversized = [byte[]]::new($maximumBytes + 1)
    for ($index = 0; $index -lt $oversized.Length; ++$index) {
        $oversized[$index] = 0x20
    }
    $oversizedRejected = $false
    try {
        Invoke-WireRead $oversized | Out-Null
    } catch {
        $oversizedRejected = $true
    }
    Assert-True $oversizedRejected `
        'wire decoder accepted a message larger than 1 MiB'

    $snapshotType = $assembly.GetType(
        'TimeCardControlCenter.OscillatordSnapshot', $true)
    $snapshot = [Activator]::CreateInstance($snapshotType)
    $snapshot.Service = 'oscillatord-windows'
    $snapshot.Version = 'test'
    $snapshot.ProtocolVersion = 1
    $snapshot.ServiceState = 'NO_HARDWARE_TEST'
    $responseStream = [IO.MemoryStream]::new()
    try {
        $arguments = [object[]]::new(3)
        $arguments[0] = $responseStream
        $arguments[1] = $snapshot
        $arguments[2] = [Threading.CancellationToken]::None
        $writeTask = $writeResponse.Invoke($null, $arguments)
        [void]$writeTask.GetAwaiter().GetResult()
        Assert-True ($responseStream.Length -le $maximumBytes) `
            'ordinary status serialization exceeded the protocol limit'
        $decodedResponse = [Text.Encoding]::UTF8.GetString(
            $responseStream.ToArray()) | ConvertFrom-Json
        Assert-True ($decodedResponse.service -eq 'oscillatord-windows') `
            'wire response lost the service identity'
        Assert-True ($decodedResponse.protocol_version -eq 1) `
            'wire response lost the protocol version'
        Assert-True ($decodedResponse.service_state -eq 'NO_HARDWARE_TEST') `
            'wire response lost the native service state'
    } finally {
        $responseStream.Dispose()
    }

    Write-Host '=== Checking exact u-blox CFG-RST frames ==='
    $runtimeType = $assembly.GetType(
        'TimeCardControlCenter.OscillatordRuntime', $true)
    $buildUbx = $runtimeType.GetMethod('BuildUbxFrame',
        [Reflection.BindingFlags]'NonPublic,Static')
    Assert-True ($null -ne $buildUbx) `
        'the native u-blox frame builder was not found'
    $resetVectors = @(
        [pscustomobject]@{
            Name = 'start'
            Payload = [byte[]]@(0x01, 0x00, 0x09, 0x00)
            Expected = 'B5-62-06-04-04-00-01-00-09-00-18-7A'
        },
        [pscustomobject]@{
            Name = 'stop'
            Payload = [byte[]]@(0x01, 0x00, 0x08, 0x00)
            Expected = 'B5-62-06-04-04-00-01-00-08-00-17-78'
        },
        [pscustomobject]@{
            Name = 'software reset'
            Payload = [byte[]]@(0x01, 0x00, 0x01, 0x00)
            Expected = 'B5-62-06-04-04-00-01-00-01-00-10-6A'
        },
        [pscustomobject]@{
            Name = 'hardware reset'
            Payload = [byte[]]@(0x01, 0x00, 0x04, 0x00)
            Expected = 'B5-62-06-04-04-00-01-00-04-00-13-70'
        },
        [pscustomobject]@{
            Name = 'cold reset'
            Payload = [byte[]]@(0xFF, 0xFF, 0x02, 0x00)
            Expected = 'B5-62-06-04-04-00-FF-FF-02-00-0E-61'
        }
    )
    foreach ($vector in $resetVectors) {
        $arguments = [object[]]::new(3)
        $arguments[0] = [byte]0x06
        $arguments[1] = [byte]0x04
        $arguments[2] = $vector.Payload
        $frame = [byte[]]$buildUbx.Invoke($null, $arguments)
        $actual = [BitConverter]::ToString($frame)
        Assert-True ($actual -ceq $vector.Expected) `
            ("wrong UBX-CFG-RST {0} frame: {1}" -f $vector.Name, $actual)
    }
    $resetMappings = @(
        @('GnssStart', '0x0001', '0x09'),
        @('GnssStop', '0x0001', '0x08'),
        @('GnssSoftReset', '0x0001', '0x01'),
        @('GnssHardReset', '0x0001', '0x04'),
        @('GnssColdReset', '0xffff', '0x02')
    )
    foreach ($mapping in $resetMappings) {
        $pattern = 'case\s+OscillatordRequest\.' + $mapping[0] +
            ':\s*SendGnssReset\(' + $mapping[1] + ',\s*' +
            $mapping[2] + '\);'
        Assert-Matches $runtimeSource $pattern `
            ("request {0} is not mapped to the pinned u-blox payload" -f
                $mapping[0])
    }

    if (-not $SkipRuntimeSmoke) {
        Write-Host '=== Running no-device hot-plug and IPC smoke test ==='
        $port = Get-FreeTcpPort
        $pipeName = 'OcpTimeCard.Oscillatord.Test.' +
            [Guid]::NewGuid().ToString('N')
        $runtimeConfigPath = Write-ConfigurationVariant `
            'runtime-no-device.json' {
                param($value)
                $value.disciplining = $false
                $value.monitoring = $true
                $value.deviceIndex = 31
                $value.deviceRetrySeconds = 1
                $value.socketAddress = '127.0.0.1'
                $value.socketPort = $port
                $value.monitoringAllowControl = $false
                $value.monitoringControlToken = ''
                $value.namedPipe = $pipeName
                if ($value.PSObject.Properties.Name -contains
                    'windowsTimePublisher') {
                    $value.windowsTimePublisher = $false
                }
            }
        $runtimeConfiguration = Invoke-ConfigurationLoad $runtimeConfigPath

        # Inject an impossible device suffix after validating the public range.
        # This proves no-device startup without opening any real Time Card,
        # even on a host that happens to have many cards installed.
        $deviceIndexField = $configurationType.GetField(
            '<DeviceIndex>k__BackingField',
            [Reflection.BindingFlags]'NonPublic,Instance')
        Assert-True ($null -ne $deviceIndexField) `
            'could not isolate the runtime smoke test from physical hardware'
        $deviceIndexField.SetValue($runtimeConfiguration, [uint32]::MaxValue)

        $logType = $assembly.GetType(
            'TimeCardControlCenter.ServiceLog', $true)
        $testLog = [Runtime.Serialization.FormatterServices]::GetUninitializedObject(
            $logType)
        $privateInstance = [Reflection.BindingFlags]'NonPublic,Instance'
        $logType.GetField('gate', $privateInstance).SetValue(
            $testLog, [object]::new())
        $logType.GetField('path', $privateInstance).SetValue(
            $testLog, (Join-Path $temporaryRoot 'runtime-smoke.log'))
        $logType.GetField('maximumBytes', $privateInstance).SetValue(
            $testLog, [int64]65536)
        $logType.GetField('retainedFiles', $privateInstance).SetValue(
            $testLog, 1)
        $runtimeConstructor = $runtimeType.GetConstructors(
            [Reflection.BindingFlags]'Public,NonPublic,Instance') |
            Where-Object { $_.GetParameters().Count -eq 2 } |
            Select-Object -First 1
        Assert-True ($null -ne $runtimeConstructor) `
            'native runtime constructor was not found'
        $runtime = $runtimeConstructor.Invoke([object[]]@(
            $runtimeConfiguration, $testLog))
        $stalledTcp = $null
        $stalledPipe = $null
        $runtimeStopped = $false
        try {
            $runtimeType.GetMethod('Start').Invoke($runtime, $null)
            $deadline = [DateTime]::UtcNow.AddSeconds(5)
            $tcpStatus = $null
            do {
                try {
                    $tcpJson = Invoke-TcpJson $port '{"request":0}'
                    $tcpStatus = $tcpJson | ConvertFrom-Json
                } catch {
                    Start-Sleep -Milliseconds 50
                    continue
                }
                if ($tcpStatus.service_state -eq 'WAITING_FOR_DEVICE') {
                    break
                }
                Start-Sleep -Milliseconds 50
            } while ([DateTime]::UtcNow -lt $deadline)
            Assert-True ($null -ne $tcpStatus) `
                'the no-device TCP endpoint never returned status'
            Assert-True ($tcpStatus.service -eq 'oscillatord-windows') `
                'the TCP endpoint is not served by the native runtime'
            Assert-True ($tcpStatus.protocol_version -eq 1) `
                'the live TCP endpoint has the wrong protocol version'
            Assert-True ($tcpStatus.service_state -eq 'WAITING_FOR_DEVICE') `
                'the runtime did not survive an absent Time Card'
            Assert-True (-not $tcpStatus.control_enabled) `
                'the live TCP endpoint unexpectedly enabled control'

            $denied = Invoke-TcpJson $port '{"request":1}' |
                ConvertFrom-Json
            Assert-Matches ([string]$denied.error) `
                '(control is disabled|token is invalid)' `
                'read-only TCP accepted a state-changing request'

            $invalid = Invoke-TcpJson $port '{"request":14}' |
                ConvertFrom-Json
            Assert-Matches ([string]$invalid.error) `
                'request number is invalid' `
                'the live endpoint accepted an invalid request number'

            $pipeStatus = Invoke-PipeJson $pipeName '{"request":0}' |
                ConvertFrom-Json
            Assert-True ($pipeStatus.service -eq 'oscillatord-windows') `
                'the protected local pipe did not return native status'
            Assert-True ($pipeStatus.service_state -eq 'WAITING_FOR_DEVICE') `
                'the local pipe did not preserve the no-device state'

            Write-Host '=== Exercising IPC quiescence and restart ==='
            $stalledTcp = [Net.Sockets.TcpClient]::new()
            $stalledTcp.Connect([Net.IPAddress]::Loopback, $port)
            $partial = [Text.Encoding]::UTF8.GetBytes('{')
            $stalledTcp.GetStream().Write($partial, 0, $partial.Length)
            $stalledTcp.GetStream().Flush()

            $stalledPipe = [IO.Pipes.NamedPipeClientStream]::new('.',
                $pipeName, [IO.Pipes.PipeDirection]::InOut,
                [IO.Pipes.PipeOptions]::None,
                [Security.Principal.TokenImpersonationLevel]::Impersonation)
            $stalledPipe.Connect(5000)
            $stalledPipe.Write($partial, 0, $partial.Length)
            $stalledPipe.Flush()

            $shutdownTimer = [Diagnostics.Stopwatch]::StartNew()
            $stopTask = $runtimeType.GetMethod('StopAsync').Invoke(
                $runtime, $null)
            [void]$stopTask.GetAwaiter().GetResult()
            $shutdownTimer.Stop()
            $runtimeStopped = $true
            Assert-True ($shutdownTimer.Elapsed -lt [TimeSpan]::FromSeconds(5)) `
                'shutdown waited for stalled clients instead of closing them'

            $tcpClosed = $stalledTcp.Client.Poll(1000000,
                [Net.Sockets.SelectMode]::SelectRead) -and
                $stalledTcp.Available -eq 0
            Assert-True $tcpClosed `
                'the stopped TCP server left an accepted client connected'
            $pipeClosed = $false
            try {
                $probe = [byte[]]::new(1)
                $pendingRead = $stalledPipe.BeginRead($probe, 0, 1,
                    $null, $null)
                if ($pendingRead.AsyncWaitHandle.WaitOne(1000)) {
                    $pipeClosed = $stalledPipe.EndRead($pendingRead) -eq 0
                }
            } catch {
                $pipeClosed = $true
            }
            Assert-True $pipeClosed `
                'the stopped named-pipe server left its client connected'
            $stalledTcp.Dispose()
            $stalledTcp = $null
            $stalledPipe.Dispose()
            $stalledPipe = $null

            # Occupying the configured endpoint forces a mid-start failure.
            # Releasing it and restarting the same object proves rollback
            # cleared every lifecycle field and listener.
            $blocker = [Net.Sockets.TcpListener]::new(
                [Net.IPAddress]::Loopback, $port)
            $blocker.Start()
            try {
                $startRejected = $false
                try {
                    $runtimeType.GetMethod('Start').Invoke($runtime, $null)
                } catch {
                    $startRejected = $true
                }
                Assert-True $startRejected `
                    'runtime startup unexpectedly ignored an occupied port'
                $rollbackStop = $runtimeType.GetMethod('StopAsync').Invoke(
                    $runtime, $null)
                [void]$rollbackStop.GetAwaiter().GetResult()
            } finally {
                $blocker.Stop()
            }

            $runtimeType.GetMethod('Start').Invoke($runtime, $null)
            $runtimeStopped = $false
            $deadline = [DateTime]::UtcNow.AddSeconds(5)
            $restartedStatus = $null
            do {
                try {
                    $restartedStatus = (Invoke-TcpJson $port '{"request":0}') |
                        ConvertFrom-Json
                } catch {
                    Start-Sleep -Milliseconds 50
                }
            } while ($null -eq $restartedStatus -and
                [DateTime]::UtcNow -lt $deadline)
            Assert-True ($null -ne $restartedStatus -and
                $restartedStatus.service -eq 'oscillatord-windows') `
                'runtime did not restart after transactional startup rollback'
            $restartedPipe = Invoke-PipeJson $pipeName '{"request":0}' |
                ConvertFrom-Json
            Assert-True ($restartedPipe.service -eq 'oscillatord-windows') `
                'named-pipe worker did not restart after quiescent shutdown'
        } finally {
            if ($null -ne $stalledTcp) {
                $stalledTcp.Dispose()
            }
            if ($null -ne $stalledPipe) {
                $stalledPipe.Dispose()
            }
            if (-not $runtimeStopped) {
                $stopTask = $runtimeType.GetMethod('StopAsync').Invoke(
                    $runtime, $null)
                [void]$stopTask.GetAwaiter().GetResult()
            }
        }
    }
} finally {
    # The resolved target was constrained to our uniquely prefixed child of
    # the system temporary directory before this recursive cleanup.
    if (Test-Path -LiteralPath $resolvedTemporaryRoot) {
        Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force
    }
}

Write-Host 'oscillatord native Windows service tests passed.'
