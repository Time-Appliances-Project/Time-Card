$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'VS 2022 C++ tools are required'}
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
Push-Location $root
try {
    & msbuild.exe tcarddiag.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /p:RunCodeAnalysis=true /fl /flp:logfile=build.log /nologo /v:minimal
    if($LASTEXITCODE){throw 'Diagnostic driver build failed'}
    Write-Host 'Native diagnostic build passed'
    $source=Join-Path $root 'x64/Release/tcarddiag'
    $out=Join-Path $root 'lab-package'
    $driver=Join-Path $out 'Driver'
    $null=New-Item -ItemType Directory -Path $driver
    Copy-Item (Join-Path $source 'tcarddiag.sys'),(Join-Path $source 'tcarddiag.inf') -Destination $driver
    Copy-Item (Join-Path $root 'x64/Release/tcarddiag.pdb'),(Join-Path $root 'driver.c'),(Join-Path $root 'README.md') -Destination $out
    $sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $signtool=Get-ChildItem $sdk -Recurse -File -Filter signtool.exe|Where-Object FullName -Like '*\x64\signtool.exe'|Sort-Object FullName -Descending|Select-Object -First 1 -ExpandProperty FullName
    $inf2cat=Join-Path $root '../packages/Microsoft.Windows.WDK.x64.10.0.26100.6584/c/bin/10.0.26100.0/x86/inf2cat.exe'
    if(!$signtool -or !(Test-Path $inf2cat)){throw 'Missing pinned signing tools'}
    $commit=(& git rev-parse HEAD).Trim()
    Write-Host 'Creating the ephemeral lab certificate'
    $cert=New-SelfSignedCertificate -Type CodeSigningCert -KeyAlgorithm RSA -KeyLength 2048 -KeyExportPolicy NonExportable -HashAlgorithm SHA256 -CertStoreLocation Cert:/CurrentUser/My -Subject ('CN=Time Card PCI Diagnostic '+$commit.Substring(0,12)) -NotAfter ([DateTime]::Now.AddMonths(1))
    try {
        $cer=Join-Path $out 'TimeCard-PciDiagnostic.cer'
        $null=Export-Certificate -Cert $cert -FilePath $cer
        Write-Host 'Trusting the certificate in the ephemeral build machine store'
        $null=Import-Certificate -FilePath $cer -CertStoreLocation Cert:/LocalMachine/Root
        Write-Host 'Signing the diagnostic SYS'
        & $signtool sign /fd SHA256 /s My /sha1 $cert.Thumbprint (Join-Path $driver 'tcarddiag.sys')
        if($LASTEXITCODE){throw 'SYS test signing failed'}
        & $inf2cat "/driver:$driver" '/os:10_CO_X64,10_NI_X64,10_GE_X64,10_25H2_X64' /uselocaltime
        if($LASTEXITCODE){throw 'Catalog generation failed'}
        & $signtool sign /fd SHA256 /s My /sha1 $cert.Thumbprint (Join-Path $driver 'tcarddiag.cat')
        if($LASTEXITCODE){throw 'Catalog test signing failed'}
        foreach($name in 'tcarddiag.sys','tcarddiag.cat'){
            $sig=Get-AuthenticodeSignature (Join-Path $driver $name)
            if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Thumbprint -ne $cert.Thumbprint){throw 'Signature verification failed'}
        }
        Write-Host 'Both code signatures verified'
        $m=[ordered]@{Purpose='Lab-only PCI rebalance isolation, no functional Time Card services';Commit=$commit;Version='0.1.0.0';Author='Ahmad Byagowi';CertificateThumbprint=$cert.Thumbprint;HardwareTested=$false;Files=@(Get-ChildItem $out -Recurse -File|Sort-Object FullName|ForEach-Object {[ordered]@{Path=$_.FullName.Substring($out.Length+1);SHA256=(Get-FileHash $_.FullName).Hash}})}
        $m|ConvertTo-Json -Depth 5|Set-Content (Join-Path $out 'Build.json') -Encoding utf8
    } finally {
        Remove-Item ('Cert:/LocalMachine/Root/'+$cert.Thumbprint)
        Remove-Item ('Cert:/CurrentUser/My/'+$cert.Thumbprint) -DeleteKey
    }
} finally {Pop-Location}
