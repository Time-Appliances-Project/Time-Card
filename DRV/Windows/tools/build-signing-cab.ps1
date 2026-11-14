[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$windows = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $windows
$evidence = Join-Path $windows 'build-evidence'
if (Test-Path -LiteralPath $evidence) { throw 'Use a clean build-evidence directory.' }
New-Item -ItemType Directory -Path $evidence | Out-Null
Start-Transcript -Path (Join-Path $evidence 'build.txt') | Out-Null
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products * -version '[17.0,18.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio 2022 C++ tools were not found.' }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64'
    $record = [ordered]@{
        SourceCommit = (& git rev-parse HEAD)
        Configuration = 'Release'
        Platform = 'x64'
        DriverVersion = '1.50.0.0'
        Abi = 15
        Kmdf = '1.33'
        MinimumDesktopBuild = 22000
        CatalogTargets = '10_CO_X64,10_NI_X64,10_GE_X64,10_25H2_X64'
        SdkWdkNuget = '10.0.26100.6584'
        VisualStudioVersion = $env:VisualStudioVersion
        CompilerVersion = $env:VCToolsVersion
        RunnerImageVersion = $env:ImageVersion
        RunUrl = "https://github.com/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
        StartedUtc = [DateTime]::UtcNow.ToString('o')
        Signed = $false
        HardwareTested = $false
    }
    $record | ConvertTo-Json | Set-Content (Join-Path $evidence 'build-environment.json') -Encoding utf8
    & (Join-Path $PSScriptRoot 'test-board-profiles.ps1')
    & (Join-Path $PSScriptRoot 'test-signing-metadata.ps1')
    & (Join-Path $PSScriptRoot 'test-fpga-advanced.ps1')
    New-Item -ItemType Directory -Force -Path out | Out-Null
    & cl /nologo /W4 /WX /O2 /Iinclude /Foout\timecardctl.obj /Feout\timecardctl.exe tools\timecardctl.c
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostic CLI build failed.' }
    & (Join-Path $windows 'package-release.ps1')
    $driverInfo = (Get-Item -LiteralPath 'submission\TimeCard\timecard.sys').VersionInfo
    if ($driverInfo.Comments -cne 'Windows driver author: Ahmad Byagowi') {
        throw 'The packaged driver is missing its Windows author credit.'
    }
    & python (Join-Path $PSScriptRoot 'validate-signing-package.py') `
        (Join-Path $windows 'submission\TimeCard') `
        --output (Join-Path $evidence 'binary-validation.json')
    if ($LASTEXITCODE -ne 0) { throw 'PE/PDB package validation failed.' }
    $submission = Join-Path $windows 'submission'
    $files = @(Get-ChildItem -LiteralPath (Join-Path $submission 'TimeCard') -File)
    $files += Get-Item -LiteralPath (Join-Path $submission 'TimeCard-1.50.0.0-x64.cab')
    $hashes = foreach ($file in $files | Sort-Object FullName) {
        $relative = $file.FullName.Substring($submission.Length + 1).Replace('\', '/')
        '{0}  {1}' -f (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
    }
    $hashes | Set-Content (Join-Path $submission 'SHA256SUMS.txt') -Encoding ascii
    Write-Host 'Unsigned CAB build and structural validation passed. No signing or hardware testing performed.'
} finally {
    Stop-Transcript | Out-Null
}
