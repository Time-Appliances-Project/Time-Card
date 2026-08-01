param()

$ErrorActionPreference = 'Stop'
$windows = Split-Path -Parent $PSScriptRoot
$assemblyPath = Join-Path $windows 'TimeCardControlCenter\bin\Release\TimeCardControlCenter.exe'
if (-not (Test-Path $assemblyPath)) {
    throw 'Build the Release Control Center before running the F9T parser test.'
}
$assembly = [Reflection.Assembly]::LoadFrom($assemblyPath)
$snapshotType = $assembly.GetType('TimeCardControlCenter.UbloxReceiverSnapshot', $true)
$clientType = $assembly.GetType('TimeCardControlCenter.UbloxClient', $true)

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "F9T test failed: $Message" }
}

function New-Snapshot {
    $dictionaryType = [Collections.Generic.Dictionary``2].MakeGenericType(
        [uint32], [uint64])
    $listType = [Collections.Generic.List``1].MakeGenericType([string])
    $constructor = $snapshotType.GetConstructors(
        [Reflection.BindingFlags]'Instance,NonPublic')[0]
    return $constructor.Invoke(@(
        [Activator]::CreateInstance($dictionaryType),
        [Activator]::CreateInstance($listType)))
}

function Invoke-Parser {
    param([string]$Name, [byte[]]$Payload, $Snapshot)
    $method = $clientType.GetMethod($Name,
        [Reflection.BindingFlags]'Static,NonPublic')
    Assert-True ($null -ne $method) "parser $Name is missing"
    $null = $method.Invoke($null, @($Payload, $Snapshot))
}

function Write-AsciiField {
    param([byte[]]$Data, [int]$Offset, [int]$Length, [string]$Value)
    $bytes = [Text.Encoding]::ASCII.GetBytes($Value)
    [Array]::Copy($bytes, 0, $Data, $Offset, [Math]::Min($Length, $bytes.Length))
}

$snapshot = New-Snapshot
$version = [byte[]]::new(160)
Write-AsciiField $version 0 30 'TIM 1.32'
Write-AsciiField $version 30 10 '00190000'
Write-AsciiField $version 40 30 'FWVER=TIM 1.32'
Write-AsciiField $version 70 30 'PROTVER=29.31'
Write-AsciiField $version 100 30 'MOD=ZED-F9T'
Write-AsciiField $version 130 30 'GPS;GLO;GAL;BDS'
Invoke-Parser 'ParseVersion' $version $snapshot
Assert-True $snapshot.IsF9TimingReceiver 'ZED-F9T MON-VER identity was not recognized'
Assert-True ($snapshot.Module -eq 'ZED-F9T') 'module identity was not retained'

$survey = [byte[]]::new(28)
[BitConverter]::GetBytes([uint32]900).CopyTo($survey, 0)
[BitConverter]::GetBytes([uint32]2500).CopyTo($survey, 16)
[BitConverter]::GetBytes([uint32]740).CopyTo($survey, 20)
$survey[24] = 1; $survey[25] = 0
Invoke-Parser 'ParseSurveyIn' $survey $snapshot
Assert-True ($snapshot.SurveyInValid -and -not $snapshot.SurveyInActive) `
    'completed survey-in status was not decoded'
Assert-True ([Math]::Abs($snapshot.SurveyInMeanAccuracyMillimeters - 50) -lt 0.01) `
    'survey-in variance was not converted to millimeters'

$rf = [byte[]]::new(52)
$rf[1] = 2
$rf[4] = 0; $rf[5] = 1; $rf[6] = 2; $rf[7] = 1
[BitConverter]::GetBytes([uint16]120).CopyTo($rf, 16)
[BitConverter]::GetBytes([uint16]3500).CopyTo($rf, 18)
$rf[20] = 4
$rf[28] = 1; $rf[29] = 2; $rf[30] = 2; $rf[31] = 1
[BitConverter]::GetBytes([uint16]130).CopyTo($rf, 40)
[BitConverter]::GetBytes([uint16]3600).CopyTo($rf, 42)
$rf[44] = 18
Invoke-Parser 'ParseRfStatus' $rf $snapshot
Assert-True ($snapshot.RfBlocks.Count -eq 2) 'dual-band MON-RF blocks were not decoded'
Assert-True ($snapshot.RfBlocks[1].JammingState -eq 2 -and
    $snapshot.RfBlocks[1].JammingIndicator -eq 18) 'RF interference fields are incorrect'

$gps = [byte[]]::new(16)
[BitConverter]::GetBytes([uint32]123456789).CopyTo($gps, 0)
[BitConverter]::GetBytes([int32]250000).CopyTo($gps, 4)
[BitConverter]::GetBytes([int16]2429).CopyTo($gps, 8)
$gps[10] = 18; $gps[11] = 7
[BitConverter]::GetBytes([uint32]20).CopyTo($gps, 12)
Invoke-Parser 'ParseGpsTime' $gps $snapshot
Assert-True ($snapshot.GpsTimeValid -and $snapshot.GpsWeek -eq 2429 -and
    $snapshot.GpsUtcLeapSeconds -eq 18) 'NAV-TIMEGPS fields are incorrect'

$leap = [byte[]]::new(24)
$leap[8] = 2; $leap[9] = 18; $leap[10] = 2; $leap[11] = 1
[BitConverter]::GetBytes([int32]86400).CopyTo($leap, 12)
[BitConverter]::GetBytes([uint16]2430).CopyTo($leap, 16)
[BitConverter]::GetBytes([uint16]1).CopyTo($leap, 18)
$leap[23] = 3
Invoke-Parser 'ParseLeapSeconds' $leap $snapshot
Assert-True ($snapshot.CurrentLeapSecondsValid -and
    $snapshot.LeapSecondEventValid -and $snapshot.LeapSecondChange -eq 1) `
    'NAV-TIMELS event fields are incorrect'

$xaml = Get-Content -Raw -Encoding UTF8 (Join-Path $windows 'TimeCardControlCenter\MainWindow.xaml')
$clientSource = Get-Content -Raw -Encoding UTF8 `
    (Join-Path $windows 'TimeCardControlCenter\UbloxClient.cs')
Assert-True ($xaml -match 'UbloxTimingPanel') 'F9T timing workspace is missing'
Assert-True ($xaml -match 'UbloxTimeGpsRateTextBox' -and
    $xaml -match 'UbloxTimeLsRateTextBox') 'TIMEGPS/TIMELS message controls are missing'
Assert-True ($clientSource -match 'retryMessages' -and
    $clientSource -match 'between navigation epochs') `
    'passive F9T discovery does not retry an epoch-boundary capture'

Write-Host 'F9T tests passed (identity, survey-in, dual-band RF, GPS time, leap event, and workspace controls).'
