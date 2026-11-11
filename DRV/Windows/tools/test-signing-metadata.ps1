$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$inf = Get-Content (Join-Path $root 'timecard.inf') -Raw
$project = Get-Content (Join-Path $root 'timecard.vcxproj') -Raw
$resource = Get-Content (Join-Path $root 'timecard.rc') -Raw
$header = Get-Content (Join-Path $root 'include\timecard_ioctl.h') -Raw
if ($inf -notmatch '(?m)^%ProviderName%\s*=\s*Standard,NTamd64\.10\.0\.1\.\.22000\s*$' -or
    $inf -notmatch '\[Standard\.NTamd64\.10\.0\.1\.\.22000\]') {
    throw 'Both INF decorations must require Windows 11 desktop build 22000.'
}
if ($inf -match '19041|NTamd64\.10\.0\.\.\.') { throw 'Unrestricted/downlevel INF model found.' }
if ($inf -notmatch 'KmdfLibraryVersion\s*=\s*1\.33' -or
    $project -notmatch '<KMDF_VERSION_MINOR>33</KMDF_VERSION_MINOR>' -or
    $project -notmatch '<_NT_TARGET_VERSION>0xA00000B</_NT_TARGET_VERSION>') {
    throw 'Framework, library baseline, and INF must agree.'
}
if ($inf -notmatch 'DriverVer\s*=\s*11/11/2026,1\.45\.0\.0' -or
    $resource -notmatch 'FILEVERSION 1,45,0,0' -or
    $header -notmatch 'TIMECARD_ABI_VERSION 15u') {
    throw 'Driver version/resource/ABI contract mismatch.'
}
foreach ($id in @('PCI\VEN_1D9B&DEV_0400','PCI\VEN_18D4&DEV_1008','PCI\VEN_2163&DEV_1008&SUBSYS_000710EE&REV_02','PCI\VEN_1AD7&DEV_A000')) {
    if (-not $inf.Contains($id)) { throw "PCI identity missing: $id" }
}
Write-Host 'Signing metadata checks passed: 1.45.0.0, ABI 15, Windows 11 desktop x64, KMDF 1.33.'
