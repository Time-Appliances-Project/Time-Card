#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9.-]{3,50}$')][string]$IdentityName,
    [Parameter(Mandatory)][string]$Publisher,
    [Parameter(Mandatory)][string]$PublisherDisplayName,
    [string]$MakeAppx,
    [switch]$SkipBuild,
    [switch]$QualificationPackage
)
$ErrorActionPreference='Stop'
if(-not $Publisher.StartsWith('CN=',[StringComparison]::OrdinalIgnoreCase)){throw 'Use the exact Publisher distinguished name from Partner Center.'}
if(-not $SkipBuild){
    & (Join-Path $PSScriptRoot 'build-gui.cmd') release
    if($LASTEXITCODE -ne 0){throw 'Control Center build failed'}
}
if(-not $MakeAppx){
    $MakeAppx=Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\makeappx.exe" -ErrorAction SilentlyContinue|Sort-Object FullName -Descending|Select-Object -First 1 -ExpandProperty FullName
}
if(-not $MakeAppx -or -not (Test-Path -LiteralPath $MakeAppx)){throw 'Windows SDK x64 MakeAppx.exe is required'}
$release=Join-Path $PSScriptRoot 'TimeCardControlCenter\bin\Release'
$exe=Join-Path $release 'TimeCardControlCenter.exe'
$version=[Version](Get-Item $exe).VersionInfo.FileVersion
if($version.Revision -ne 0){throw 'Microsoft Store requires the final package version component to be zero'}
$tag=$version.ToString()
$suffix=if($QualificationPackage){'-qualification-unsigned'}else{'-store-unsigned'}
$out=Join-Path $PSScriptRoot 'out'
$null=New-Item -ItemType Directory -Path $out -Force
$stage=Join-Path $out ('msix-stage-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path (Join-Path $stage 'Assets') -Force
Copy-Item $exe,(Join-Path $release 'THIRD_PARTY_NOTICES.md'),(Join-Path $release 'MINICOD-LICENSE.txt') $stage
Copy-Item (Join-Path $PSScriptRoot '..\..\LICENSE.md') $stage
Copy-Item (Join-Path $PSScriptRoot 'TimeCardControlCenter\README.md') $stage
Copy-Item (Join-Path $PSScriptRoot 'TimeCardControlCenter\Store\PrivacyPolicy.txt') $stage
Add-Type -AssemblyName System.Drawing
$source=[Drawing.Image]::FromFile((Join-Path $PSScriptRoot 'assets\timecard-app-icon.png'))
try{
    foreach($size in @(44,50,150)){
        $bitmap=New-Object Drawing.Bitmap $size,$size
        $graphics=[Drawing.Graphics]::FromImage($bitmap)
        try{
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $scale=[Math]::Min(($size*0.8)/$source.Width,($size*0.8)/$source.Height)
            $w=[int]($source.Width*$scale);$h=[int]($source.Height*$scale)
            $graphics.DrawImage($source,[int](($size-$w)/2),[int](($size-$h)/2),$w,$h)
            $bitmap.Save((Join-Path $stage "Assets\Logo$size.png"),[Drawing.Imaging.ImageFormat]::Png)
        }finally{$graphics.Dispose();$bitmap.Dispose()}
    }
}finally{$source.Dispose()}
$identityXml=[Security.SecurityElement]::Escape($IdentityName)
$publisherXml=[Security.SecurityElement]::Escape($Publisher)
$displayXml=[Security.SecurityElement]::Escape($PublisherDisplayName)
$manifest=@"
<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10" xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10" xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities" IgnorableNamespaces="uap rescap">
  <Identity Name="$identityXml" Publisher="$publisherXml" Version="$tag" ProcessorArchitecture="x64" />
  <Properties><DisplayName>TimeCardControlCenter</DisplayName><PublisherDisplayName>$displayXml</PublisherDisplayName><Description>Time Card hardware dashboard and diagnostics</Description><Logo>Assets\Logo50.png</Logo></Properties>
  <Dependencies><TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.19041.0" MaxVersionTested="10.0.26200.0" /></Dependencies>
  <Resources><Resource Language="en-us" /></Resources>
  <Applications><Application Id="ControlCenter" Executable="TimeCardControlCenter.exe" EntryPoint="Windows.FullTrustApplication"><uap:VisualElements DisplayName="TimeCardControlCenter" Description="Time Card hardware dashboard and diagnostics" BackgroundColor="#081725" Square150x150Logo="Assets\Logo150.png" Square44x44Logo="Assets\Logo44.png" /></Application></Applications>
  <Capabilities><rescap:Capability Name="runFullTrust" /><rescap:Capability Name="allowElevation" /></Capabilities>
</Package>
"@
$manifest|Set-Content -LiteralPath (Join-Path $stage 'AppxManifest.xml') -Encoding UTF8
$package=Join-Path $out "TimeCardControlCenter-$tag-x64$suffix.msix"
if(Test-Path $package){throw 'Package already exists; use a new version or review the existing output'}
& $MakeAppx pack /d $stage /p $package /o
if($LASTEXITCODE -ne 0){throw 'MSIX schema or package validation failed'}
[ordered]@{Package=$package;SHA256=(Get-FileHash $package).Hash;IdentityName=$IdentityName;Publisher=$Publisher;Version=$tag;Unsigned=$true;QualificationOnly=[bool]$QualificationPackage;StoreElevationApprovalRequired=$true;DriverIncluded=$false;Manifest=(Join-Path $stage 'AppxManifest.xml')}|ConvertTo-Json|Set-Content ($package+'.json') -Encoding UTF8
Write-Output "Validated unsigned MSIX: $package"
