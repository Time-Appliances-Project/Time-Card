[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $windowsRoot 'build-oscillatord-service.cmd'
$installPath = Join-Path $windowsRoot 'install-oscillatord-service.ps1'
$uninstallPath = Join-Path $windowsRoot 'uninstall-oscillatord-service.ps1'
$projectPath = Join-Path $windowsRoot `
    'TimeCardOscillatord\TimeCardOscillatord.csproj'
$templatePath = Join-Path $windowsRoot `
    'TimeCardOscillatord\oscillatord.example.json'
$serviceLogPath = Join-Path $windowsRoot `
    'TimeCardOscillatord\ServiceLog.cs'

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Message
    )
    Assert-True ([regex]::IsMatch($Text, $Pattern,
        [Text.RegularExpressions.RegexOptions]::IgnoreCase)) $Message
}

foreach ($path in @($buildPath, $installPath, $uninstallPath)) {
    Assert-True (Test-Path -LiteralPath $path -PathType Leaf) `
        "Missing service packaging file: $path"
}
Assert-True (Test-Path -LiteralPath $projectPath -PathType Leaf) `
    "Missing Windows service project: $projectPath"
Assert-True (Test-Path -LiteralPath $templatePath -PathType Leaf) `
    "Missing safe service configuration template: $templatePath"
Assert-True (Test-Path -LiteralPath $serviceLogPath -PathType Leaf) `
    "Missing Windows service logging implementation: $serviceLogPath"

$build = Get-Content -LiteralPath $buildPath -Raw
$install = Get-Content -LiteralPath $installPath -Raw
$uninstall = Get-Content -LiteralPath $uninstallPath -Raw
$project = Get-Content -LiteralPath $projectPath -Raw
$serviceLog = Get-Content -LiteralPath $serviceLogPath -Raw
$template = Get-Content -LiteralPath $templatePath -Raw |
    ConvertFrom-Json

# Parse both PowerShell entry points without executing elevated operations.
[void][scriptblock]::Create($install)
[void][scriptblock]::Create($uninstall)

Assert-Contains $build 'TimeCardOscillatord\\TimeCardOscillatord\.csproj' `
    'The build script does not target the Windows service project.'
Assert-Contains $build '/p:PlatformTarget=x64' `
    'The Windows service build is not pinned to x64.'
Assert-Contains $build 'build-discipline-library\.cmd' `
    'The native miniCOD dependency is not built with the service.'
Assert-Contains $project `
    '<EmbeddedResource Include="\.\.\\TimeCardDiscipline\\bin\\Release\\TimeCardDiscipline\.dll">' `
    'The native miniCOD DLL is not embedded in the service.'
Assert-Contains $project `
    '<LogicalName>TimeCardControlCenter\.Native\.TimeCardDiscipline\.dll</LogicalName>' `
    'The service native-resource name does not match the loader contract.'
Assert-True (-not ($build -match
        'copy\s+/y\s+"TimeCardDiscipline\\bin\\Release\\TimeCardDiscipline\.dll"')) `
    'The service build still stages a loose miniCOD DLL.'
Assert-Contains $project '<TargetFrameworkVersion>v4\.7\.2</TargetFrameworkVersion>' `
    'The service must target .NET Framework 4.7.2.'
Assert-Contains $project '<PlatformTarget>x64</PlatformTarget>' `
    'The service project is not an x64 executable.'
Assert-Contains $project '<AssemblyName>TimeCardOscillatord</AssemblyName>' `
    'The service assembly name does not match its installer contract.'
Assert-True ($template.schemaVersion -eq 1) `
    'The service configuration template has an unsupported schema version.'
Assert-True ($template.socketAddress -eq '127.0.0.1') `
    'The service configuration must bind legacy monitoring to loopback.'
Assert-True (-not $template.monitoringAllowControl) `
    'The service configuration must deny monitoring control by default.'

Assert-Contains $install '#Requires -RunAsAdministrator' `
    'The installer is not explicitly elevated.'
Assert-Contains $install "OcpTimeCardOscillatord" `
    'The installer uses the wrong service name.'
Assert-True (-not ($install -match 'Native discipline library not found')) `
    'The service installer still requires a loose miniCOD DLL.'
Assert-Contains $install `
    "obsoleteNames\s*=\s*@\('TimeCardDiscipline\.dll'[^)]*'TimeCardDiscipline\.pdb'" `
    'The service upgrade does not remove obsolete loose miniCOD files.'
Assert-Contains $install `
    '\$obsoleteNames\s+-notcontains\s+\$_\.Name' `
    'The service installer can re-copy an obsolete loose miniCOD file.'
Assert-Contains $install 'foreach\s*\(\$obsoleteName\s+in\s+\$obsoleteNames\)' `
    'The service upgrade does not delete every obsolete loose miniCOD file.'
Assert-Contains $install "eventSource = 'OCP Time Card Oscillatord'" `
    'The installer registers the wrong Event Log source.'
Assert-Contains $serviceLog `
    'EventSource = "OCP Time Card Oscillatord"' `
    'The service and installer Event Log sources do not match.'
Assert-Contains $install "start=', 'delayed-auto" `
    'The service is not configured for delayed automatic start.'
Assert-Contains $install `
    'New-Service[\s\S]{0,180}-BinaryPathName\s+\$binaryPath[\s\S]{0,180}-StartupType\s+Automatic' `
    'Initial service creation does not preserve the quoted executable path safely.'
Assert-True (-not ($install -match
        "Invoke-ServiceControl\s+@\('create'")) `
    'The installer still forwards its quoted binPath through sc.exe create.'
Assert-Contains $install "obj=', 'LocalSystem" `
    'The service is not configured to use LocalSystem.'
Assert-Contains $install 'restart/5000/restart/15000/restart/60000' `
    'The expected SCM recovery actions are missing.'
Assert-Contains $install 'ProgramW6432' `
    'The installer does not resolve native Program Files safely.'
Assert-Contains $install 'ProgramData.*OCP Time Card\\Oscillatord' `
    'The installer does not use the product ProgramData directory.'
Assert-Contains $install 'Assert-PathBelow' `
    'The installer does not constrain its managed directories.'
Assert-Contains $install 'if \(-not \(Test-Path -LiteralPath \$configPath\)\)' `
    'The installer can overwrite an existing service configuration.'
Assert-Contains $install "'\*S-1-5-18:\(OI\)\(CI\)F'" `
    'SYSTEM is not granted explicit service-data ownership.'
Assert-Contains $install "'\*S-1-5-32-544:\(OI\)\(CI\)F'" `
    'Administrators are not granted explicit service-data ownership.'
Assert-Contains $install 'switch\]\$EnableWindowsTimeProvider' `
    'W32Time provider registration is not an explicit install opt-in.'
Assert-Contains $install `
    'registeredProviderPath\.StartsWith\(\$productPrefix[\s\S]{0,120}OrdinalIgnoreCase' `
    'The installer does not constrain W32Time shutdown to a product-owned provider.'
Assert-Contains $install `
    'registeredProviderOwnedByProduct[\s\S]{0,180}Stop-ServiceForUpdate\s+\$windowsTime' `
    'The installer can replace its provider without first stopping W32Time.'
Assert-Contains $install `
    'try\s*\{[\s\S]{0,800}Copy-Item[\s\S]{0,200}\}\s*finally\s*\{[\s\S]{0,800}Start-Service\s+-InputObject\s+\$windowsTime' `
    'W32Time prior-state restoration is not protected by the provider-copy finally path.'

Assert-Contains $uninstall '#Requires -RunAsAdministrator' `
    'The uninstaller is not explicitly elevated.'
Assert-Contains $uninstall 'Assert-ExactRemovalPath' `
    'Recursive removal targets are not verified exactly.'
Assert-Contains $uninstall 'if \(\$PurgeData\)' `
    'ProgramData deletion is not gated by -PurgeData.'
Assert-Contains $uninstall 'Preserved service configuration and calibration data' `
    'The default preservation behavior is not explicit.'

foreach ($forbidden in @('bcdedit', 'testsigning', 'pnputil')) {
    Assert-True (-not ($install -match $forbidden) -and
        -not ($uninstall -match $forbidden)) `
        "User-mode service tooling must not invoke $forbidden."
}

Write-Host 'oscillatord Windows service packaging contract passed.'
