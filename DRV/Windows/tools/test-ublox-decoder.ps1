param()

$ErrorActionPreference = 'Stop'
$decoderSource = Join-Path $PSScriptRoot '..\TimeCardControlCenter\UbloxStreamDecoder.cs'
Add-Type -Path $decoderSource

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "u-blox decoder test failed: $Message"
    }
}

function Join-ByteArrays {
    param([byte[][]]$Arrays)
    $length = 0
    foreach ($array in $Arrays) { $length += $array.Length }
    $result = [byte[]]::new($length)
    $offset = 0
    foreach ($array in $Arrays) {
        [Array]::Copy($array, 0, $result, $offset, $array.Length)
        $offset += $array.Length
    }
    return ,$result
}

function New-UbxFrame {
    param([byte]$Class, [byte]$Id, [byte[]]$Payload)
    $frame = [byte[]]::new($Payload.Length + 8)
    $frame[0] = 0xb5
    $frame[1] = 0x62
    $frame[2] = $Class
    $frame[3] = $Id
    $frame[4] = [byte]($Payload.Length -band 0xff)
    $frame[5] = [byte](($Payload.Length -shr 8) -band 0xff)
    [Array]::Copy($Payload, 0, $frame, 6, $Payload.Length)
    [byte]$checksumA = 0
    [byte]$checksumB = 0
    for ($index = 2; $index -lt 6 + $Payload.Length; $index++) {
        $checksumA = [byte](($checksumA + $frame[$index]) -band 0xff)
        $checksumB = [byte](($checksumB + $checksumA) -band 0xff)
    }
    $frame[6 + $Payload.Length] = $checksumA
    $frame[7 + $Payload.Length] = $checksumB
    return ,$frame
}

function New-NmeaSentence {
    param([string]$Body)
    [byte]$checksum = 0
    foreach ($value in [Text.Encoding]::ASCII.GetBytes($Body)) {
        $checksum = [byte]($checksum -bxor $value)
    }
    return ,[Text.Encoding]::ASCII.GetBytes(
        ('$' + $Body + '*' + $checksum.ToString('X2') + "`r`n"))
}

function Get-Crc24Q {
    param([byte[]]$Data)
    [uint32]$crc = 0
    foreach ($value in $Data) {
        $crc = $crc -bxor ([uint32]$value -shl 16)
        for ($bit = 0; $bit -lt 8; $bit++) {
            $crc = $crc -shl 1
            if (($crc -band 0x1000000) -ne 0) { $crc = $crc -bxor 0x1864cfb }
        }
    }
    return $crc -band 0xffffff
}

function New-RtcmFrame {
    param([byte[]]$Payload)
    $body = [byte[]]::new($Payload.Length + 3)
    $body[0] = 0xd3
    $body[1] = [byte](($Payload.Length -shr 8) -band 0x03)
    $body[2] = [byte]($Payload.Length -band 0xff)
    [Array]::Copy($Payload, 0, $body, 3, $Payload.Length)
    [uint32]$crc = Get-Crc24Q $body
    return ,(Join-ByteArrays @($body, [byte[]]@(
        [byte](($crc -shr 16) -band 0xff),
        [byte](($crc -shr 8) -band 0xff),
        [byte]($crc -band 0xff))))
}

$pvt = [byte[]]::new(92)
[BitConverter]::GetBytes([uint16]2026).CopyTo($pvt, 4)
$pvt[6] = 7
$pvt[7] = 16
$pvt[8] = 20
$pvt[9] = 56
$pvt[10] = 49
$pvt[11] = 3
$pvt[20] = 3
$pvt[21] = 1
$pvt[23] = 12
[BitConverter]::GetBytes([int32]-1220840000).CopyTo($pvt, 24)
[BitConverter]::GetBytes([int32]374220000).CopyTo($pvt, 28)
[BitConverter]::GetBytes([int32]15000).CopyTo($pvt, 36)
[BitConverter]::GetBytes([uint32]1200).CopyTo($pvt, 40)
[BitConverter]::GetBytes([int32]1250).CopyTo($pvt, 60)
[BitConverter]::GetBytes([int32]9000000).CopyTo($pvt, 64)
[BitConverter]::GetBytes([uint16]125).CopyTo($pvt, 76)

[byte[]]$ubx = New-UbxFrame 0x01 0x07 $pvt
[byte[]]$nmea = New-NmeaSentence 'GPRMC,205649.00,A,3725.3200,N,12205.0400,W,2.5,90.0,160726,,,A'
[byte[]]$rtcm = New-RtcmFrame ([byte[]]@(0x3e, 0xd0, 0x00, 0x01))
$decoder = [TimeCardControlCenter.UbloxStreamDecoder]::new()

$first = $decoder.Feed((Join-ByteArrays @([byte[]]@(0x00, 0xff, 0x55), $ubx[0..19])))
Assert-True ($first.Messages.Count -eq 0) 'a fragmented UBX frame was emitted too early'
Assert-True ($first.BufferedBytes -eq 20) 'the partial UBX frame was not retained'
Assert-True ($first.DiscardedBytes -eq 3) 'leading noise was not reported'

$second = $decoder.Feed((Join-ByteArrays @($ubx[20..($ubx.Length - 1)], $nmea, $rtcm)))
Assert-True ($second.Messages.Count -eq 3) 'mixed back-to-back messages were not separated'
Assert-True ($second.Messages[0].Name -eq 'UBX-NAV-PVT') 'NAV-PVT was not identified'
Assert-True ($second.Messages[0].ChecksumValid -eq $true) 'valid UBX checksum was rejected'
Assert-True ($second.Messages[0].Summary -match '3D fix' -and $second.Messages[0].Summary -match '12 SV') 'NAV-PVT fields were not decoded'
Assert-True ($second.Messages[1].Name -eq 'NMEA-GPRMC') 'NMEA RMC was not identified'
Assert-True ($second.Messages[1].ChecksumValid -eq $true) 'valid NMEA checksum was rejected'
Assert-True ($second.Messages[1].Details -match '37.4220000, -122.0840000') 'NMEA RMC position was not decoded'
Assert-True ($second.Messages[2].Name -eq 'RTCM3-1005') 'RTCM3 type was not decoded'
Assert-True ($second.Messages[2].ChecksumValid -eq $true) 'valid RTCM CRC was rejected'

[byte[]]$gga = New-NmeaSentence 'GNGGA,205650.00,3725.3200,N,12205.0400,W,4,18,0.6,15.2,M,-32.1,M,1.0,0001'
[byte[]]$gsa = New-NmeaSentence 'GNGSA,A,3,01,03,07,08,11,14,18,22,,,,,1.2,0.7,1.0,1'
[byte[]]$gsv = New-NmeaSentence 'GPGSV,1,1,02,01,45,123,42,22,30,250,35,1'
[byte[]]$gll = New-NmeaSentence 'GNGLL,3725.3200,N,12205.0400,W,205650.00,A,A'
[byte[]]$vtg = New-NmeaSentence 'GNVTG,90.0,T,88.0,M,2.5,N,4.63,K,A'
[byte[]]$gns = New-NmeaSentence 'GNGNS,205650.00,3725.3200,N,12205.0400,W,AR,18,0.6,15.2,-32.1,1.0,0001,V'
[byte[]]$unknown = New-NmeaSentence 'PTEST,1,2,three'
$nmeaDecoder = [TimeCardControlCenter.UbloxStreamDecoder]::new()
$nmeaFirst = $nmeaDecoder.Feed($gga[0..15])
Assert-True ($nmeaFirst.Messages.Count -eq 0 -and $nmeaFirst.BufferedBytes -eq 16) 'fragmented NMEA sentence was not buffered'
$nmeaSecond = $nmeaDecoder.Feed((Join-ByteArrays @(
    $gga[16..($gga.Length - 1)], $gsa, $gsv, $gll, $vtg, $gns, $unknown)))
Assert-True ($nmeaSecond.Messages.Count -eq 7) 'back-to-back NMEA sentences were not separated'
Assert-True ($nmeaSecond.Messages[0].Summary -match 'RTK fixed' -and $nmeaSecond.Messages[0].Summary -match '18 satellites') 'NMEA GGA quality and satellite fields were not decoded'
Assert-True ($nmeaSecond.Messages[1].Details -match 'active 01, 03, 07') 'NMEA GSA active satellites were not decoded'
Assert-True ($nmeaSecond.Messages[2].Details -match 'SV 01 elev 45' -and $nmeaSecond.Messages[2].Details -match 'C/N') 'NMEA GSV satellite blocks were not decoded'
Assert-True ($nmeaSecond.Messages[3].Summary -match 'valid' -and $nmeaSecond.Messages[3].Details -match '37.4220000') 'NMEA GLL fix was not decoded'
Assert-True ($nmeaSecond.Messages[4].Details -match '2.5 kn' -and $nmeaSecond.Messages[4].Details -match '4.63 km/h') 'NMEA VTG speed was not decoded'
Assert-True ($nmeaSecond.Messages[5].Summary -match 'autonomous.*RTK fixed' -and $nmeaSecond.Messages[5].Details -match 'altitude 15.2 m') 'NMEA GNS fix was not decoded'
Assert-True ($nmeaSecond.Messages[6].Name -eq 'NMEA-PTEST' -and $nmeaSecond.Messages[6].Details -match 'Fields: 1') 'unknown NMEA fields were not preserved'

[byte[]]$damagedNmea = $gga.Clone()
$checksumIndex = $damagedNmea.Length - 4
$damagedNmea[$checksumIndex] = if ($damagedNmea[$checksumIndex] -eq 0x30) { 0x31 } else { 0x30 }
$nmeaBad = $nmeaDecoder.Feed($damagedNmea)
Assert-True ($nmeaBad.Messages.Count -eq 1 -and $nmeaBad.Messages[0].ChecksumValid -eq $false) 'damaged NMEA checksum was not flagged'
Assert-True ($nmeaDecoder.ChecksumFailures -eq 1) 'NMEA checksum failure counter is incorrect'

[byte[]]$damaged = $ubx.Clone()
$damaged[$damaged.Length - 1] = $damaged[$damaged.Length - 1] -bxor 0xff
$third = $decoder.Feed($damaged)
Assert-True ($third.Messages.Count -eq 1) 'damaged UBX frame was not surfaced'
Assert-True ($third.Messages[0].ChecksumValid -eq $false) 'damaged UBX checksum was not flagged'
Assert-True ($decoder.ChecksumFailures -eq 1) 'checksum failure counter is incorrect'
Assert-True ($decoder.TotalMessages -eq 4) 'message counter is incorrect'

$decoder.Reset()
Assert-True ($decoder.TotalMessages -eq 0 -and $decoder.BufferedBytes -eq 0) 'reset did not clear decoder state'
Write-Host 'GNSS/NMEA stream decoder tests passed (fragmentation, decoded fields, UBX, NMEA, RTCM3, checksums, reset).'
