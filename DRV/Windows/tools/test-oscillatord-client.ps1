param()

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot '..\TimeCardControlCenter\OscillatordClient.cs'
Add-Type -Path $source -ReferencedAssemblies @(
    'System.dll', 'System.Core.dll', 'System.Runtime.Serialization.dll', 'System.Xml.dll')

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "oscillatord client test failed: $Message"
    }
}

$json = @'
{
  "service":"oscillatord",
  "version":"3.10.0",
  "protocol_version":1,
  "control_enabled":true,
  "Action requested":"None",
  "clock":{"class":"LOCKED","offset":-9223372036},
  "disciplining":{"status":"LOCK_HIGH_RESOLUTION","current_phase_convergence_count":42,"valid_phase_convergence_threshold":50,"convergence_progress":84.0,"ready_for_holdover":true},
  "oscillator":{"model":"mRO50","fine_ctrl":4294967295,"coarse_ctrl":3000000000,"lock":true,"temperature":41.25},
  "gnss":{"fix":3,"fixOk":true,"antenna_power":1,"antenna_status":2,"lsChange":0,"leap_seconds":18,"satellites_count":17,"survey_in_position_error":0.42,"time_accuracy":8}
}
'@

$snapshot = [TimeCardControlCenter.OscillatordClient]::ParseResponse($json)
Assert-True ($snapshot.Version -eq '3.10.0') 'service version was not decoded'
Assert-True ($snapshot.ProtocolVersion -eq 1) 'protocol version was not decoded'
Assert-True $snapshot.ControlEnabled 'control policy was not decoded'
Assert-True ($snapshot.Clock.OffsetNanoseconds -eq -9223372036) '64-bit phase offset was truncated'
Assert-True ($snapshot.Oscillator.FineControl -eq 4294967295) 'unsigned fine control was truncated'
Assert-True ($snapshot.Oscillator.CoarseControl -eq 3000000000) 'unsigned coarse control was truncated'
Assert-True $snapshot.Oscillator.Locked 'oscillator lock was not decoded'
Assert-True ($snapshot.Disciplining.ConvergenceProgress -eq 84.0) 'convergence was not decoded'
Assert-True $snapshot.Disciplining.ReadyForHoldover 'holdover state was not decoded'
Assert-True ($snapshot.Gnss.Satellites -eq 17) 'GNSS satellites were not decoded'

$invalidFailed = $false
try {
    [TimeCardControlCenter.OscillatordClient]::ParseResponse('{broken') | Out-Null
} catch {
    $invalidFailed = $true
}
Assert-True $invalidFailed 'malformed JSON was accepted'

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
try {
    $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    $client = [TimeCardControlCenter.OscillatordClient]::new([TimeSpan]::FromSeconds(2))
    $acceptTask = $listener.AcceptTcpClientAsync()
    $requestTask = $client.RequestAsync('127.0.0.1', $port,
        [TimeCardControlCenter.OscillatordRequest]::Status, 'test-token')
    $peer = $acceptTask.GetAwaiter().GetResult()
    try {
        $stream = $peer.GetStream()
        $requestBuffer = [byte[]]::new(512)
        $requestLength = $stream.Read($requestBuffer, 0, $requestBuffer.Length)
        $wireRequest = [Text.Encoding]::UTF8.GetString($requestBuffer, 0, $requestLength)
        Assert-True ($wireRequest -match '"request":0') 'status request was not serialized'
        Assert-True ($wireRequest -match '"token":"test-token"') 'control token was not serialized'
        $responseBytes = [Text.Encoding]::UTF8.GetBytes($json)
        $stream.Write($responseBytes, 0, $responseBytes.Length)
        $stream.Flush()
        $networkSnapshot = $requestTask.GetAwaiter().GetResult()
        Assert-True ($networkSnapshot.Oscillator.Model -eq 'mRO50') 'TCP response was not decoded'
    } finally {
        $peer.Dispose()
    }
} finally {
    $listener.Stop()
}

'oscillatord client tests passed'
