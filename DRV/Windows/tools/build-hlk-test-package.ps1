[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'submission\TimeCard'
$lab = Join-Path $root 'hlk-test'
if (Test-Path $lab) { throw "Build output already exists at $lab" }
$driver = New-Item -ItemType Directory -Path (Join-Path $lab 'Driver')
Get-ChildItem $source -File | Where-Object Extension -In '.inf','.sys','.ico' |
    Copy-Item -Destination $driver.FullName
Copy-Item (Join-Path $root 'out\timecardctl.exe'),(Join-Path $PSScriptRoot 'Test-WiWiHardware.ps1') -Destination $lab
$inf2cat = Join-Path $root 'packages\Microsoft.Windows.WDK.x64.10.0.26100.6584\c\bin\10.0.26100.0\x86\inf2cat.exe'
$sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$signtool = Get-ChildItem $sdkBin -Filter signtool.exe -File -Recurse |
    Where-Object FullName -Like '*\x64\signtool.exe' |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool -or -not (Test-Path $inf2cat)) { throw 'Missing SDK SignTool or pinned WDK Inf2Cat.' }
$commit = (& git -C $root rev-parse HEAD).Trim()
$cert = New-SelfSignedCertificate -Type CodeSigningCert -KeyAlgorithm RSA -KeyLength 2048 `
    -KeyExportPolicy NonExportable -HashAlgorithm SHA256 -CertStoreLocation Cert:\CurrentUser\My `
    -Subject "CN=Time Card HLK Test $($commit.Substring(0,12))" -NotAfter ([DateTime]::Now.AddMonths(3))
try {
    $sys = Join-Path $driver.FullName 'timecard.sys'
    $cat = Join-Path $driver.FullName 'timecard.cat'
    & $signtool sign /fd SHA256 /s My /sha1 $cert.Thumbprint $sys
    if ($LASTEXITCODE) { throw 'SYS test signing failed.' }
    & $inf2cat "/driver:$($driver.FullName)" '/os:10_CO_X64,10_NI_X64,10_GE_X64,10_25H2_X64' /uselocaltime
    if ($LASTEXITCODE) { throw 'Test catalog generation failed.' }
    & $signtool sign /fd SHA256 /s My /sha1 $cert.Thumbprint $cat
    if ($LASTEXITCODE) { throw 'Catalog test signing failed.' }
    foreach ($path in $sys,$cat) {
        if ((Get-AuthenticodeSignature -LiteralPath $path).SignerCertificate.Thumbprint -ne $cert.Thumbprint) {
            throw "Unexpected signer on $path"
        }
    }
    Export-Certificate -Cert $cert -FilePath (Join-Path $lab 'TimeCard-HLK-Test.cer') | Out-Null
    $manifest = [ordered]@{
        Commit = $commit
        CertificateThumbprint = $cert.Thumbprint
        ExpiresUtc = $cert.NotAfter.ToUniversalTime().ToString('o')
        UnsignedSysSHA256 = (Get-FileHash (Join-Path $source 'timecard.sys')).Hash
        Files = @(Get-ChildItem $lab -Recurse -File | Sort-Object FullName | ForEach-Object {
            [ordered]@{ Path = $_.FullName.Substring($lab.Length+1); SHA256 = (Get-FileHash $_.FullName).Hash }
        })
    }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $lab 'Build.json') -Encoding utf8
} finally {
    Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey
}
