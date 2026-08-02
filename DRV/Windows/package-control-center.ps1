#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$windowsRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd(
    [IO.Path]::DirectorySeparatorChar)
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $windowsRoot 'out'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $OutputDirectory.StartsWith(
        $windowsRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be below the Windows driver directory.'
}

if (-not $SkipBuild) {
    & (Join-Path $windowsRoot 'build-gui.cmd') release
    if ($LASTEXITCODE -ne 0) {
        throw "Control Center build failed with exit code $LASTEXITCODE."
    }
}

$release = Join-Path $windowsRoot 'TimeCardControlCenter\bin\Release'
$executable = Join-Path $release 'TimeCardControlCenter.exe'
$notices = Join-Path $release 'THIRD_PARTY_NOTICES.md'
$miniCodLicense = Join-Path $release 'MINICOD-LICENSE.txt'
$projectReadme = Join-Path $windowsRoot 'TimeCardControlCenter\README.md'
$projectLicense = Join-Path (Split-Path -Parent (Split-Path -Parent `
    $windowsRoot)) 'LICENSE.md'
$inputs = @($executable, $notices, $miniCodLicense, $projectReadme,
    $projectLicense)
foreach ($inputFile in $inputs) {
    if (-not (Test-Path -LiteralPath $inputFile -PathType Leaf)) {
        throw "Control Center package input is missing: $inputFile"
    }
}
if (Test-Path -LiteralPath (Join-Path $release `
        'TimeCardDiscipline.dll')) {
    throw 'A loose TimeCardDiscipline.dll remains in the Release output.'
}

$assembly = [Reflection.Assembly]::Load(
    [IO.File]::ReadAllBytes($executable))
$nativeResource = 'TimeCardControlCenter.Native.TimeCardDiscipline.dll'
if ($assembly.GetManifestResourceNames() -notcontains $nativeResource) {
    throw 'The Control Center does not contain its native miniCOD resource.'
}
$version = [Version](Get-Item -LiteralPath $executable).VersionInfo.FileVersion
$versionLabel = '{0}.{1}.{2}' -f $version.Major, $version.Minor,
    $version.Build

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$archivePath = Join-Path $OutputDirectory `
    "TimeCardControlCenter-$versionLabel-x64.zip"
$oldStandalone = Join-Path $OutputDirectory `
    "TimeCardControlCenter-$versionLabel-x64.exe"
if (Test-Path -LiteralPath $oldStandalone) {
    Remove-Item -LiteralPath $oldStandalone -Force
}
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}
Compress-Archive -LiteralPath $inputs -DestinationPath $archivePath `
    -CompressionLevel Optimal

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $actual = @($archive.Entries | ForEach-Object { $_.FullName } |
        Sort-Object)
    $expected = @('LICENSE.md', 'MINICOD-LICENSE.txt', 'README.md',
        'THIRD_PARTY_NOTICES.md', 'TimeCardControlCenter.exe') | Sort-Object
    if (Compare-Object -ReferenceObject $expected -DifferenceObject $actual) {
        throw 'The Control Center archive has unexpected or missing files.'
    }
    if ($archive.Entries | Where-Object {
            $_.FullName.EndsWith('.dll',
                [StringComparison]::OrdinalIgnoreCase) }) {
        throw 'The Control Center archive contains a loose DLL.'
    }
} finally {
    $archive.Dispose()
}

$archiveHash = (Get-FileHash -LiteralPath $archivePath `
    -Algorithm SHA256).Hash
Write-Host ''
Write-Host "Portable ZIP: $archivePath"
Write-Host "SHA-256:      $archiveHash"
