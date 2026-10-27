[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'submission'),
    [switch]$SkipBuild,
    [string]$CertificateThumbprint,
    [string]$TimestampUrl
)

$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'timecard.vcxproj'
$buildRoot = Join-Path $PSScriptRoot 'x64\Release'
$packageRoot = Join-Path $buildRoot 'timecard'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd(
    [IO.Path]::DirectorySeparatorChar
)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $OutputDirectory.StartsWith(
        $projectRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be a child of the Windows driver directory.'
}
$driverFolder = Join-Path $OutputDirectory 'TimeCard'
$sourceInf = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'timecard.inf') -Raw
if ($sourceInf -notmatch '(?m)^DriverVer\s*=\s*\d{2}/\d{2}/\d{4},(\d+\.\d+\.\d+\.\d+)\s*$') {
    throw 'The source INF must contain an explicit four-part DriverVer.'
}
$driverVersion = $Matches[1]
$cabName = "TimeCard-$driverVersion-x64.cab"
$cabPath = Join-Path $OutputDirectory $cabName
if (Test-Path -LiteralPath $OutputDirectory) {
    throw 'OutputDirectory already exists. Choose a fresh submission directory.'
}

function Find-MSBuild {
    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installationPath = & $vswhere -latest -products * `
            -requires Microsoft.Component.MSBuild -property installationPath
        if ($installationPath) {
            $candidate = Join-Path $installationPath `
                'MSBuild\Current\Bin\amd64\MSBuild.exe'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    throw 'MSBuild was not found. Install Visual Studio and the Windows Driver Kit.'
}

function Find-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $kitsBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $candidate = Get-ChildItem -Path $kitsBin -Filter signtool.exe `
        -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object FullName -Match '\\x64\\signtool\.exe$' |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if ($candidate) {
        return $candidate
    }

    throw 'SignTool was not found. Install the Windows Driver Kit.'
}

if ($CertificateThumbprint -and -not $TimestampUrl) {
    throw '-TimestampUrl is required when -CertificateThumbprint is used.'
}
if ($TimestampUrl -and -not $CertificateThumbprint) {
    throw '-CertificateThumbprint is required when -TimestampUrl is used.'
}

if (-not $SkipBuild) {
    $msbuild = Find-MSBuild
    & $msbuild $project /t:Rebuild /p:Configuration=Release /p:Platform=x64 `
        /p:SignMode=Off /p:EnableInf2Cat=true /p:RunCodeAnalysis=true `
        /fl /flp:logfile=timecard-release.log `
        /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
}

$iconFiles = @(
    'timecard-atomic.ico',
    'timecard-flash.ico',
    'timecard-gnss.ico',
    'timecard-gnss2.ico',
    'timecard-i2c.ico',
    'timecard-nmea.ico',
    'timecard-phc.ico',
    'timecard-ptm.ico',
    'timecard-sma.ico',
    'timecard-timing.ico',
    'timecard-tod.ico'
)
$files = @(
    (Join-Path $packageRoot 'timecard.inf'),
    (Join-Path $packageRoot 'timecard.sys'),
    (Join-Path $packageRoot 'timecard.cat'),
    (Join-Path $packageRoot 'timecard.ico'),
    (Join-Path $buildRoot 'timecard.pdb')
)
$files += $iconFiles | ForEach-Object { Join-Path $packageRoot $_ }
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Release input is missing: $file"
    }
}

New-Item -ItemType Directory -Path $driverFolder -Force | Out-Null
Copy-Item -LiteralPath $files -Destination $driverFolder
$packagedInf = Get-Content -LiteralPath (Join-Path $driverFolder 'timecard.inf') -Raw
if ($packagedInf -notmatch ('DriverVer\s*=\s*\d{2}/\d{2}/\d{4},' +
        [regex]::Escape($driverVersion) + '\s*(\r?\n|$)')) {
    throw 'Packaged INF version does not match the source.'
}
$fileVersion = (Get-Item -LiteralPath (Join-Path $driverFolder 'timecard.sys')).VersionInfo.FileVersion
if ($fileVersion -ne $driverVersion) {
    throw "SYS version $fileVersion does not match INF version $driverVersion."
}
$signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $driverFolder 'timecard.sys')
if ($signature.Status -ne 'NotSigned') {
    throw 'Expected an unsigned Release SYS, not a previously signed or test-signed image.'
}

$ddfPath = Join-Path $OutputDirectory 'TimeCard.ddf'
$ddfLines = @(
    '.OPTION EXPLICIT',
    '.Set CabinetFileCountThreshold=0',
    '.Set FolderFileCountThreshold=0',
    '.Set FolderSizeThreshold=0',
    '.Set MaxCabinetSize=0',
    '.Set MaxDiskFileCount=0',
    '.Set MaxDiskSize=0',
    '.Set CompressionType=MSZIP',
    '.Set Cabinet=on',
    '.Set Compress=on',
    ".Set CabinetNameTemplate=$cabName",
    ".Set DiskDirectoryTemplate=`"$OutputDirectory`"",
    ".Set RptFileName=`"$(Join-Path $OutputDirectory 'TimeCard.rpt')`"",
    ".Set InfFileName=`"$(Join-Path $OutputDirectory 'TimeCard-cab.inf')`"",
    '.Set DestinationDir=TimeCard'
)
foreach ($file in Get-ChildItem -LiteralPath $driverFolder -File) {
    $ddfLines += '"{0}"' -f $file.FullName
}
Set-Content -LiteralPath $ddfPath -Value $ddfLines -Encoding ASCII

& makecab.exe /F $ddfPath
if ($LASTEXITCODE -ne 0) {
    throw "MakeCab failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $cabPath)) {
    throw "MakeCab did not create the expected file: $cabPath"
}

# Ensure the CAB contains the exact 16 validated input files under TimeCard/.
$roundtrip = Join-Path $OutputDirectory 'cab-roundtrip'
New-Item -ItemType Directory -Path $roundtrip | Out-Null
& expand.exe $cabPath '-F:*' $roundtrip
if ($LASTEXITCODE -ne 0) { throw 'CAB extraction verification failed.' }
$expectedFiles = @(Get-ChildItem -LiteralPath $driverFolder -File)
$extractedFiles = @(Get-ChildItem -LiteralPath $roundtrip -Recurse -File)
if ($extractedFiles.Count -ne $expectedFiles.Count) { throw 'CAB file count mismatch.' }
foreach ($file in $expectedFiles) {
    $extracted = Join-Path $roundtrip ('TimeCard\' + $file.Name)
    if (-not (Test-Path -LiteralPath $extracted) -or
        (Get-FileHash -LiteralPath $extracted -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
        throw "CAB content mismatch: $($file.Name)"
    }
}

if ($CertificateThumbprint) {
    $signTool = Find-SignTool
    & $signTool sign /sha1 $CertificateThumbprint /fd sha256 `
        /tr $TimestampUrl /td sha256 /v $cabPath
    if ($LASTEXITCODE -ne 0) {
        throw "SignTool failed with exit code $LASTEXITCODE."
    }
    & $signTool verify /pa /v $cabPath
    if ($LASTEXITCODE -ne 0) {
        throw 'The submission CAB signature could not be verified.'
    }
}

Write-Host ''
Write-Host "HLK driver folder: $driverFolder"
Write-Host "Attestation CAB:  $cabPath"
if (-not $CertificateThumbprint) {
    Write-Host 'CAB status:         unsigned (sign with a registered certificate before upload)'
}
