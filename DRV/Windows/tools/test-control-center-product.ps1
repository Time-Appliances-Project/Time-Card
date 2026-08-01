$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\TimeCardControlCenter\ControlCenterProduct.cs'
$productionSource = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$histogramPath = Join-Path $PSScriptRoot '..\TimeCardControlCenter\TelemetryHistogram.cs'
$histogramSource = Get-Content -LiteralPath $histogramPath -Raw -Encoding UTF8
$histogramSource = $histogramSource -replace '(?m)^using .+\r?\n', ''
$windowXaml = Get-Content -LiteralPath (Join-Path $PSScriptRoot `
    '..\TimeCardControlCenter\MainWindow.xaml') -Raw -Encoding UTF8
$windowSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot `
    '..\TimeCardControlCenter\MainWindow.xaml.cs') -Raw -Encoding UTF8
$productWindowSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot `
    '..\TimeCardControlCenter\MainWindow.Product.cs') -Raw -Encoding UTF8
$projectSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot `
    '..\TimeCardControlCenter\TimeCardControlCenter.csproj') -Raw -Encoding UTF8
$topologySource = Get-Content -LiteralPath (Join-Path $PSScriptRoot `
    '..\TimeCardControlCenter\TimeCardTopologyView.cs') -Raw -Encoding UTF8
if ($windowXaml -notmatch 'x:Name="OscillatordPage"' -or
    $windowXaml -notmatch 'OscillatordAction_Click' -or
    $windowXaml -notmatch 'CONTROL TOKEN \(NEVER STORED\)') {
    throw 'Control Center product test failed: oscillatord workspace is incomplete.'
}
if ($windowXaml -notmatch 'Viewport3D\s+x:Name="ImuOrientationViewport"' -or
    $windowXaml -notmatch 'x:Name="ImuVisualizationStatusText"') {
    throw 'Control Center product test failed: live 3D IMU viewport is missing.'
}
if ($windowXaml -match 'SensorsRefreshButton' -or
    $windowXaml -notmatch 'x:Name="SensorsSamplingDurationChart"' -or
    $windowXaml -notmatch 'SENSOR READ TIME' -or
    $windowSource -notmatch 'RecordSensorSamplingDuration' -or
    $windowSource -notmatch 'sampleTimer\.Elapsed\.TotalMilliseconds') {
    throw 'Control Center product test failed: automatic sensor sampling-duration chart is incomplete.'
}
if ($topologySource -notmatch 'GetSmaLaneWidth' -or
    $topologySource -notmatch 'SMA I/O' -or
    $topologySource -notmatch 'laneDivider' -or
    $topologySource -notmatch 'ActualWidth\s*<\s*520' -or
    $topologySource -notmatch 'nodeAreaRight\s*=\s*board\.Right\s*-\s*smaLaneWidth') {
    throw 'Control Center product test failed: spaced SMA topology lane is incomplete.'
}
if ($windowSource -notmatch 'UpdateImuCubeOrientation' -or
    $windowSource -notmatch 'UpdateImuCubeFromReading' -or
    $windowSource -notmatch 'UseShortestPath\s*=\s*true' -or
    $windowSource -notmatch 'target\.Normalize\(\)' -or
    $windowSource -notmatch 'const double half\s*=\s*0\.48' -or
    $windowSource -notmatch 'HOLD . ZERO SAMPLE') {
    throw 'Control Center product test failed: quaternion-driven IMU cube is incomplete.'
}
$cubeFaces = @('black', 'blue', 'green', 'purple', 'red', 'yellow')
foreach ($cubeFace in $cubeFaces) {
    if ($windowSource -notmatch ('logo_' + $cubeFace + '\.png') -or
        $projectSource -notmatch ('logo_' + $cubeFace + '\.png')) {
        throw "Control Center product test failed: $cubeFace IMU cube face is missing."
    }
}
if ($windowSource -notmatch 'TextureCoordinates' -or
    $windowSource -notmatch 'imageWidth\s*=\s*0\.94' -or
    $windowSource -notmatch 'StartImuCubeShowcase' -or
    $windowSource -notmatch 'seconds \* 29\.0' -or
    $windowSource -notmatch 'seconds \* 17\.0' -or
    $windowSource -notmatch 'seconds \* 11\.0' -or
    $windowSource -notmatch 'bool showcase\s*=\s*!imu\.IsPresent' -or
    $windowSource -notmatch 'ApplyImu\(new ImuSensorReading') {
    throw 'Control Center product test failed: textured six-face/no-IMU showcase is incomplete.'
}
if ($windowSource -notmatch 'AbiVersion\s*>=\s*10' -or
    $windowSource -notmatch 'driver 1\.37 / ABI 10') {
    throw 'Control Center product test failed: board-specific sensor ABI gating is missing.'
}
if ($productWindowSource -notmatch 'UpdateDemoCelesticaSensors' -or
    $productWindowSource -notmatch 'BoardProfile\s*=\s*3u' -or
    $productWindowSource -notmatch 'RawPressure\s*=\s*11477003u' -or
    $productWindowSource -notmatch '--demo-no-imu') {
    throw 'Control Center product test failed: Celestica visual demo telemetry is missing.'
}
$wpfUsings = "using System.Windows;`r`nusing System.Windows.Input;`r`nusing System.Windows.Media;`r`n"
$stubs = @'
namespace TimeCardControlCenter
{
    public enum SmaDirection : uint { Input = 0, Output = 1, Disabled = 2 }
    public sealed class TimeCardSnapshot
    {
        public string DriverVersion { get; set; }
        public uint AbiVersion { get; set; }
        public string Layout { get; set; }
        public bool GnssFixOk { get; set; }
        public bool GnssTelemetryAvailable { get; set; }
        public bool TodTelemetryAvailable { get; set; }
        public int LockedSatellites { get; set; }
        public uint UtcStatus { get; set; }
        public uint TodStatus { get; set; }
        public bool IsClockSynchronized { get; set; }
        public long OffsetNanoseconds { get; set; }
        public long SamplingWindowNanoseconds { get; set; }
        public uint ClockSource { get; set; }
    }
    public sealed class UbloxReceiverSnapshot
    {
        public bool FixValid { get; set; }
        public byte SatellitesUsed { get; set; }
    }
    public sealed class Sa53Snapshot
    {
        public bool TryBoolean(string name, out bool value) { value = true; return true; }
    }
    public sealed class Mro50Status
    {
        public bool IsLocked { get; set; }
    }
    public sealed class SensorTelemetrySnapshot
    {
        public uint ControllerStatus { get; set; }
    }
}
'@
$tests = @'
namespace TimeCardControlCenter
{
    public static class ProductTestRunner
    {
        public static string Run()
        {
            HealthReport demo = ControlCenterHealth.Evaluate(
                null, null, null, null, null, false, true);
            if (demo.Overall != HealthSeverity.Healthy || demo.Nodes.Count != 8)
                throw new Exception("Demo health graph failed.");

            TimeCardSnapshot live = new TimeCardSnapshot
            {
                DriverVersion = "1.15", AbiVersion = 8, Layout = "MSI-X",
                GnssFixOk = true, LockedSatellites = 12, UtcStatus = 1u << 8,
                GnssTelemetryAvailable = true, TodTelemetryAvailable = true,
                IsClockSynchronized = true, OffsetNanoseconds = 4,
                SamplingWindowNanoseconds = 9000
            };
            HealthReport health = ControlCenterHealth.Evaluate(
                live, null, null, null, new SensorTelemetrySnapshot(),
                true, false);
            if (health.Find("phc").Severity != HealthSeverity.Healthy ||
                health.Find("gnss").Severity != HealthSeverity.Healthy)
                throw new Exception("Live health evaluation failed.");

            TimeCardSnapshot art = new TimeCardSnapshot
            {
                DriverVersion = "1.32", AbiVersion = 9,
                Layout = "Orolia ART", IsClockSynchronized = true,
                OffsetNanoseconds = 4, SamplingWindowNanoseconds = 9000
            };
            HealthReport artHealth = ControlCenterHealth.Evaluate(
                art, null, null, new Mro50Status { IsLocked = true },
                null, true, false);
            if (artHealth.Find("atomic").Severity != HealthSeverity.Healthy ||
                artHealth.Find("tod").Severity != HealthSeverity.Informational)
                throw new Exception("Orolia ART capability health evaluation failed.");

            IList<ConfigurationProfile> profiles = BuiltInProfiles.Create();
            if (profiles.Count < 5 || !profiles.Any(item => item.HasNmea) ||
                !profiles.Any(item => item.Sma.Count == 4))
                throw new Exception("Built-in profile catalog failed.");

            TelemetrySession session = new TelemetrySession(60);
            session.IsRecording = true;
            for (int index = 0; index < 75; index++)
                session.Add(new TelemetryPoint
                {
                    TimestampUtc = DateTime.UtcNow.AddSeconds(index),
                    OffsetNanoseconds = index, SamplingWindowNanoseconds = 9000 + index,
                    SatellitesSeen = 20, SatellitesLocked = 15,
                    BoardTemperatureCelsius = 38.5, AtomicPhase = 0.1,
                    VibrationMetersPerSecondSquared = 0.04
                });
            if (session.Points.Count != 60 ||
                !session.ToCsv().StartsWith("timestamp_utc", StringComparison.Ordinal) ||
                !session.ToJson().Contains("\"offsetNanoseconds\"") ||
                !session.ToJson().Contains("\"vibrationMetersPerSecondSquared\""))
                throw new Exception("Telemetry retention/export failed.");

            if (Math.Abs(TelemetryMath.VectorMagnitude(3, 4, 12) - 13) > 0.0001 ||
                Math.Abs(TelemetryMath.RootMeanSquare(new[] { 3.0, 4.0 }) -
                    Math.Sqrt(12.5)) > 0.0001)
                throw new Exception("Vibration telemetry math failed.");

            ImuOrientationFilter orientationFilter = new ImuOrientationFilter(3);
            if (!orientationFilter.ShouldAccept(true, false) ||
                orientationFilter.ShouldAccept(true, true) ||
                orientationFilter.ShouldAccept(true, true) ||
                !orientationFilter.ShouldAccept(true, true))
                throw new Exception("IMU zero-orientation debounce failed.");
            if (!orientationFilter.ShouldAccept(true, false) ||
                orientationFilter.ShouldAccept(false, false) ||
                !orientationFilter.HasAcceptedSample)
                throw new Exception("IMU invalid-orientation hold failed.");
            orientationFilter.Reset();
            if (orientationFilter.HasAcceptedSample ||
                !orientationFilter.ShouldAccept(true, true))
                throw new Exception("IMU initial identity orientation failed.");

            double icpPressure;
            if (!CelesticaSensorMath.TryCompensateIcp10100(
                    8000000u, 40000u,
                    new[] { 10000, 20000, 30000, 4000 }, out icpPressure) ||
                Math.Abs(icpPressure - 78224.1342565) > 0.001 ||
                CelesticaSensorMath.TryCompensateIcp10100(
                    1u, 1u, null, out icpPressure))
                throw new Exception("ICP-10100 factory compensation failed.");

            TelemetryHistogram histogram = new TelemetryHistogram();
            for (int value = 1; value <= 100; value++)
                histogram.AddSample(value);
            if (histogram.SampleCount != 100 ||
                Math.Abs(histogram.Median - 50.5) > 0.0001 ||
                Math.Abs(histogram.Percentile95 - 95.05) > 0.0001 ||
                Math.Abs(histogram.Percentile99 - 99.01) > 0.0001)
                throw new Exception("Cross-timestamp histogram statistics failed.");

            ConfigurationProfile roundTrip = profiles[0];
            XmlSerializer serializer = new XmlSerializer(typeof(ConfigurationProfile));
            using (MemoryStream stream = new MemoryStream())
            {
                serializer.Serialize(stream, roundTrip);
                stream.Position = 0;
                ConfigurationProfile restored = (ConfigurationProfile)serializer.Deserialize(stream);
                if (restored.Name != roundTrip.Name || restored.ClockSource != roundTrip.ClockSource)
                    throw new Exception("Profile serialization failed.");
            }
            ControlCenterSettings settings = new ControlCenterSettings
            {
                Theme = "Midnight blue", RefreshSeconds = .5,
                LastKnownGoodProfile = roundTrip
            };
            serializer = new XmlSerializer(typeof(ControlCenterSettings));
            using (MemoryStream stream = new MemoryStream())
            {
                serializer.Serialize(stream, settings);
                stream.Position = 0;
                ControlCenterSettings restored =
                    (ControlCenterSettings)serializer.Deserialize(stream);
                if (restored.Theme != settings.Theme ||
                    restored.LastKnownGoodProfile.Name != roundTrip.Name)
                    throw new Exception("Settings serialization failed.");
            }
            return "Control Center product tests passed (health graph, spaced SMA topology lane, profiles, XML round-trip, telemetry retention/export, histogram percentiles, vibration math, automatic sensor acquisition-latency chart, textured six-face 3D IMU view, no-IMU showcase, IMU zero-sample debounce, ICP-10100 compensation).";
        }
    }
}
'@

Add-Type -AssemblyName WindowsBase
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName System.Xaml
$references = @('System.dll', 'System.Core.dll', 'System.Xml.dll',
    [System.Windows.DependencyObject].Assembly.Location,
    [System.Windows.Media.Brush].Assembly.Location,
    [System.Windows.FrameworkElement].Assembly.Location,
    [System.Xaml.XamlReader].Assembly.Location) | Select-Object -Unique
Add-Type -TypeDefinition ($wpfUsings + $productionSource + "`r`n" + $histogramSource +
    "`r`n" + $stubs + "`r`n" + $tests) `
    -ReferencedAssemblies $references -Language CSharp
[TimeCardControlCenter.ProductTestRunner]::Run()
