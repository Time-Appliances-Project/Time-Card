$ErrorActionPreference = 'Stop'

$root = Join-Path $PSScriptRoot '..'
$selectionPath = Join-Path $root 'TimeCardControlCenter\TimeCardDeviceSelection.cs'
$selectionSource = Get-Content -LiteralPath $selectionPath -Raw -Encoding UTF8
$clientSource = Get-Content -LiteralPath (Join-Path $root 'TimeCardControlCenter\TimeCardClient.cs') -Raw -Encoding UTF8
$settingsSource = Get-Content -LiteralPath (Join-Path $root 'TimeCardControlCenter\ControlCenterProduct.cs') -Raw -Encoding UTF8
$windowSource = Get-Content -LiteralPath (Join-Path $root 'TimeCardControlCenter\MainWindow.xaml.cs') -Raw -Encoding UTF8
$deviceWindowSource = Get-Content -LiteralPath (Join-Path $root 'TimeCardControlCenter\MainWindow.Devices.cs') -Raw -Encoding UTF8
$windowXaml = Get-Content -LiteralPath (Join-Path $root 'TimeCardControlCenter\MainWindow.xaml') -Raw -Encoding UTF8

if ($clientSource -notmatch
        'public\s+TimeCardClient\s*\(\s*string\s+selectedDevicePath\s*\)' -or
    $clientSource -notmatch
        'public\s+static\s+IList<TimeCardDeviceDescriptor>\s+EnumerateDevices' -or
    $clientSource -notmatch
        'SetupDiGetClassDevs[\s\S]*throw new Win32Exception') {
    throw 'Multi-card test failed: public exact-path inventory behavior is incomplete.'
}
if ($clientSource -notmatch
        'LegacyDevicePath\s*=\s*DevicePathPrefix\s*\+\s*"0"' -or
    $clientSource -notmatch
        'legacyFallback\s*=\s*paths\.Count\s*==\s*0' -or
    $clientSource -notmatch 'paths\.Add\(LegacyDevicePath\)' -or
    $clientSource -notmatch 'win32\.NativeErrorCode\s*==\s*2' -or
    $clientSource -notmatch 'win32\.NativeErrorCode\s*==\s*3') {
    throw 'Multi-card test failed: legacy single-card discovery fallback is incomplete.'
}
if ($settingsSource -notmatch 'SelectedDeviceSerial' -or
    $settingsSource -notmatch 'SelectedDevicePath') {
    throw 'Multi-card test failed: stable card selection is not persisted.'
}
if ($windowXaml -notmatch 'x:Name="CardSelector"' -or
    $windowXaml -notmatch 'CardSelector_SelectionChanged' -or
    $windowXaml -notmatch 'x:Name="CardRescanButton"' -or
    $windowXaml -notmatch 'RescanCards_Click') {
    throw 'Multi-card test failed: selector/rescan UI is incomplete.'
}
if ($deviceWindowSource -notmatch 'deviceSessionGeneration' -or
    $deviceWindowSource -notmatch 'TryCaptureDeviceSession' -or
    $deviceWindowSource -notmatch 'IsDeviceSessionCurrent' -or
    $deviceWindowSource -notmatch 'StopUartMonitorAndWaitAsync' -or
    $deviceWindowSource -notmatch 'StopNativeDisciplineAsync' -or
    $deviceWindowSource -notmatch 'flashUpdating' -or
    $deviceWindowSource -notmatch 'criticalConfigurationWrite') {
    throw 'Multi-card test failed: safe switch/session retirement is incomplete.'
}
if ($windowSource -notmatch
        'activeClient\.GetSnapshot\(\)[\s\S]*IsDeviceSessionCurrent' -or
    $windowSource -notmatch 'ActiveDevicePath\(\)' -or
    $windowSource -notmatch 'ActiveDeviceSerial\(\)') {
    throw 'Multi-card test failed: central stale-result suppression or diagnostics are incomplete.'
}

$tests = @'
namespace TimeCardControlCenter
{
    public static class MultiCardSelectionTestRunner
    {
        private static TimeCardDeviceDescriptor Card(string path,
            string serial, bool accessible)
        {
            return new TimeCardDeviceDescriptor(path, serial,
                "Test Time Card", 14, accessible, string.Empty);
        }

        public static string Run()
        {
            TimeCardDeviceDescriptor first = Card(
                @"\\?\pci#card-a#{8315a67a}", "00:11:22:33:44:55", true);
            TimeCardDeviceDescriptor second = Card(
                @"\\?\pci#card-b#{8315a67a}", "66:77:88:99:AA:BB", true);
            var original = new List<TimeCardDeviceDescriptor> {
                first, second };
            var reordered = new List<TimeCardDeviceDescriptor> {
                second, first };

            TimeCardDeviceDescriptor selected =
                TimeCardDeviceSelection.SelectPreferred(reordered,
                    "00-11-22-33-44-55", first.DevicePath, false);
            if (!object.ReferenceEquals(selected, first))
                throw new Exception(
                    "Serial selection did not survive inventory reorder.");

            selected = TimeCardDeviceSelection.SelectPreferred(original,
                string.Empty, second.DevicePath.ToUpperInvariant(), false);
            if (!object.ReferenceEquals(selected, second))
                throw new Exception("Exact path selection failed.");

            selected = TimeCardDeviceSelection.SelectPreferred(original,
                "AA:BB:CC:DD:EE:FF", first.DevicePath, false);
            if (selected != null)
                throw new Exception(
                    "Missing persisted serial must not silently select another card.");

            selected = TimeCardDeviceSelection.SelectPreferred(original,
                string.Empty, string.Empty, true);
            if (!object.ReferenceEquals(selected, first))
                throw new Exception("First-use accessible fallback failed.");

            TimeCardDeviceDescriptor inaccessible = Card(
                @"\\?\pci#card-c#{8315a67a}", string.Empty, false);
            selected = TimeCardDeviceSelection.SelectPreferred(
                new List<TimeCardDeviceDescriptor> { inaccessible, second },
                string.Empty, string.Empty, true);
            if (!object.ReferenceEquals(selected, second))
                throw new Exception(
                    "Fallback did not prefer an accessible card.");

            TimeCardDeviceDescriptor duplicate = Card(
                @"\\?\pci#card-d#{8315a67a}", first.SerialNumber, true);
            var duplicates = new List<TimeCardDeviceDescriptor> {
                first, duplicate };
            if (TimeCardDeviceSelection.SelectPreferred(duplicates,
                    first.SerialNumber, string.Empty, false) != null)
                throw new Exception(
                    "Duplicate serial selection was not rejected as ambiguous.");
            selected = TimeCardDeviceSelection.SelectPreferred(duplicates,
                first.SerialNumber, duplicate.DevicePath, false);
            if (!object.ReferenceEquals(selected, duplicate))
                throw new Exception(
                    "Exact path did not disambiguate duplicate serials.");

            TimeCardDeviceDescriptor reenumerated = Card(
                @"\\?\pci#new-path#{8315a67a}", first.SerialNumber, true);
            if (!TimeCardDeviceSelection.SameDevice(first, reenumerated))
                throw new Exception(
                    "Stable serial identity did not survive path replacement.");
            if (TimeCardDeviceSelection.NormalizeSerial(
                    "00:11:22:33:44:GG").Length != 0)
                throw new Exception("Invalid serial was accepted.");
            return "Multi-card Control Center selection tests passed.";
        }
    }
}
'@

Add-Type -TypeDefinition ($selectionSource + [Environment]::NewLine + $tests) -ReferencedAssemblies @('System.dll', 'System.Core.dll') -Language CSharp
[TimeCardControlCenter.MultiCardSelectionTestRunner]::Run()
