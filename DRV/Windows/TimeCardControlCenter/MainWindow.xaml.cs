using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using System.Windows.Threading;

namespace TimeCardControlCenter
{
    public partial class MainWindow : Window
    {
        private readonly DispatcherTimer refreshTimer;
        private readonly SessionLogStore sessionLogStore =
            new SessionLogStore(2000);
        private const string GenericComTagPrefix = "COM:";
        private readonly string[] startupArguments;
        private TimeCardClient client;
        private TimeCardSnapshot lastSnapshot;
        private CancellationTokenSource uartMonitorCancellation;
        private readonly object genericSerialSync = new object();
        private SerialPort monitoredGenericSerialPort;
        private string monitoredGenericSerialPortName;
        private bool refreshingGenericComPorts;
        private readonly List<UartConsoleEntry> uartConsoleEntries = new List<UartConsoleEntry>();
        private readonly UbloxStreamDecoder[] uartReceiveDecoders =
        {
            new UbloxStreamDecoder(), new UbloxStreamDecoder(), null,
            new UbloxStreamDecoder()
        };
        private readonly UbloxStreamDecoder[] uartTransmitDecoders =
        {
            new UbloxStreamDecoder(), new UbloxStreamDecoder(), null,
            new UbloxStreamDecoder()
        };
        private readonly UbloxStreamDecoder genericReceiveDecoder =
            new UbloxStreamDecoder();
        private readonly UbloxStreamDecoder genericTransmitDecoder =
            new UbloxStreamDecoder();
        private int uartConsoleHistoryBytes;
        private bool refreshing;
        private bool connecting;
        private bool smaUpdatingUi;
        private bool timingRefreshing;
        private bool sensorsRefreshing;
        private QuaternionRotation3D imuCubeRotation;
        private Quaternion imuCubeCurrent = Quaternion.Identity;
        private readonly DispatcherTimer imuCubeShowcaseTimer;
        private readonly Stopwatch imuCubeShowcaseClock = new Stopwatch();
        private bool imuCubeShowcaseActive;
        private readonly ImuOrientationFilter imuOrientationFilter =
            new ImuOrientationFilter(3);
        private bool i2cRefreshing;
        private bool ledAutomationUpdating;
        private bool ledControlsUpdating;
        private bool subsystemsRefreshing;
        private bool flashUpdating;
        private FlashDeviceStatus lastFlashStatus;
        private byte[] selectedFlashImage;
        private string selectedFlashPath;
        private bool selectedFlashIsRaw;
        private byte[] lastI2cData;
        private uint lastI2cAddress;
        private uint lastI2cSubaddress;
        private uint lastI2cSubaddressLength;
        private readonly SmaConnectorState[] lastSmaLedStates = new SmaConnectorState[4];
        private readonly BoardLedColor[] lastAppliedLedColors = new BoardLedColor[6];
        private readonly bool[] boardLedHardwareFaults = new bool[6];
        private string boardLedHardwareWarning;
        private bool? secondaryGnssPresent;
        private DateTime lastSmaLedRefreshUtc = DateTime.MinValue;
        private DateTime lastSecondaryLedObservationUtc = DateTime.MinValue;
        private bool atomicRefreshing;
        private Sa53Snapshot lastSa53Snapshot;
        private Mro50Status lastMro50Status;
        private bool ubloxRefreshing;
        private UbloxReceiverSnapshot lastUbloxSnapshot;
        private uint? lastUbloxPort;
        private uint? lastUbloxBaud;
        private DateTime lastConnectionAttemptUtc = DateTime.MinValue;
        private SignalGeneratorControls[] signalGeneratorControls;
        private FrequencyCounterControls[] frequencyCounterControls;

        private sealed class SignalGeneratorControls
        {
            public TextBlock Status { get; set; }
            public CheckBox Enabled { get; set; }
            public TextBox Frequency { get; set; }
            public TextBox Duty { get; set; }
            public TextBox Phase { get; set; }
            public CheckBox ActiveHigh { get; set; }
            public TextBox Repeat { get; set; }
            public TextBox CableDelay { get; set; }
            public ComboBox StartMode { get; set; }
            public TextBox Start { get; set; }
            public ComboBox Route { get; set; }
            public TextBlock Detail { get; set; }
            public Button Clear { get; set; }
            public Button Apply { get; set; }
        }

        private sealed class FrequencyCounterControls
        {
            public TextBlock Status { get; set; }
            public TextBlock Value { get; set; }
            public TextBox Seconds { get; set; }
            public ComboBox Route { get; set; }
            public TextBlock Detail { get; set; }
            public Button Apply { get; set; }
        }

        private sealed class UartConsoleEntry
        {
            public DateTime Timestamp { get; set; }
            public uint Port { get; set; }
            public string PortLabel { get; set; }
            public bool IsGenericPort { get; set; }
            public bool HasLineStatus { get; set; }
            public string Direction { get; set; }
            public byte[] Data { get; set; }
            public uint LineStatus { get; set; }
            public UbloxDecodeBatch Decoded { get; set; }
        }

        private sealed class GenericSerialSettings
        {
            public int Baud { get; set; }
            public int DataBits { get; set; }
            public Parity Parity { get; set; }
            public StopBits StopBits { get; set; }
            public Handshake Handshake { get; set; }
            public bool DtrEnable { get; set; }
            public bool RtsEnable { get; set; }

            public override string ToString()
            {
                string parity = Parity == Parity.None ? "N" :
                    Parity.ToString().Substring(0, 1);
                string stop = StopBits == StopBits.One ? "1" :
                    StopBits == StopBits.Two ? "2" : "1.5";
                return string.Format(CultureInfo.InvariantCulture,
                    "{0} baud · {1}{2}{3} · {4}{5}{6}", Baud, DataBits,
                    parity, stop, Handshake == Handshake.None ?
                    "no flow control" : Handshake.ToString(),
                    DtrEnable ? " · DTR" : string.Empty,
                    RtsEnable ? " · RTS" : string.Empty);
            }
        }

        private sealed class I2cRefreshResult
        {
            public I2cControllerStatus Status { get; set; }
            public I2cMuxState Mux { get; set; }
            public I2cProbeResult BoardEeprom { get; set; }
            public I2cProbeResult MacEeprom { get; set; }
            public List<uint> Addresses { get; set; }
            public bool FullScan { get; set; }
            public bool IsArt { get; set; }
        }

        private sealed class BoardLedColor
        {
            public BoardLedColor(byte red, byte green, byte blue, string status)
            {
                Red = red;
                Green = green;
                Blue = blue;
                Status = status;
            }

            public byte Red { get; private set; }
            public byte Green { get; private set; }
            public byte Blue { get; private set; }
            public string Status { get; private set; }

            public bool SameColor(BoardLedColor other)
            {
                return other != null && Red == other.Red &&
                    Green == other.Green && Blue == other.Blue;
            }
        }

        private sealed class BoardLedElectricalTestResult
        {
            public BoardLedState[] Saved { get; set; }
            public BoardLedState Reset { get; set; }
            public BoardLedState Output { get; set; }
        }

        private sealed class SmaFunctionChoice
        {
            public SmaFunctionChoice(string name, uint value)
            {
                Name = name;
                Value = value;
            }

            public string Name { get; private set; }
            public uint Value { get; private set; }
            public override string ToString() { return Name; }
        }

        private static readonly SmaFunctionChoice[] SmaInputFunctions =
        {
            new SmaFunctionChoice("10 MHz reference", 0x0000),
            new SmaFunctionChoice("PPS 1", 0x0001),
            new SmaFunctionChoice("PPS 2", 0x0002),
            new SmaFunctionChoice("Timestamp 1", 0x0004),
            new SmaFunctionChoice("Timestamp 2", 0x0008),
            new SmaFunctionChoice("IRIG-B input", 0x0010),
            new SmaFunctionChoice("DCF77 input", 0x0020),
            new SmaFunctionChoice("Timestamp 3", 0x0040),
            new SmaFunctionChoice("Timestamp 4", 0x0080),
            new SmaFunctionChoice("Frequency 1", 0x0100),
            new SmaFunctionChoice("Frequency 2", 0x0200),
            new SmaFunctionChoice("Frequency 3", 0x0400),
            new SmaFunctionChoice("Frequency 4", 0x0800)
        };

        private static readonly SmaFunctionChoice[] SmaOutputFunctions =
        {
            new SmaFunctionChoice("10 MHz reference", 0x0000),
            new SmaFunctionChoice("PHC pulse", 0x0001),
            new SmaFunctionChoice("Atomic clock", 0x0002),
            new SmaFunctionChoice("GNSS 1 PPS", 0x0004),
            new SmaFunctionChoice("GNSS 2 PPS", 0x0008),
            new SmaFunctionChoice("IRIG-B output", 0x0010),
            new SmaFunctionChoice("DCF77 output", 0x0020),
            new SmaFunctionChoice("Generator 1", 0x0040),
            new SmaFunctionChoice("Generator 2", 0x0080),
            new SmaFunctionChoice("Generator 3", 0x0100),
            new SmaFunctionChoice("Generator 4", 0x0200),
            new SmaFunctionChoice("Ground", 0x2000),
            new SmaFunctionChoice("VCC", 0x4000)
        };

        private static readonly SmaFunctionChoice[] ArtSmaInputFunctions =
        {
            new SmaFunctionChoice("PPS 1", 0x0001),
            new SmaFunctionChoice("10 MHz reference", 0x0008)
        };

        private static readonly SmaFunctionChoice[] ArtSmaOutputFunctions =
        {
            new SmaFunctionChoice("Atomic clock", 0x0002),
            new SmaFunctionChoice("GNSS PPS", 0x0004),
            new SmaFunctionChoice("10 MHz reference", 0x0010)
        };

        public MainWindow()
        {
            InitializeComponent();
            InitializeImuCube();
            imuCubeShowcaseTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(40)
            };
            imuCubeShowcaseTimer.Tick += ImuCubeShowcaseTimer_Tick;
            signalGeneratorControls = new[]
            {
                new SignalGeneratorControls { Status = Generator1StatusText, Enabled = Generator1EnabledCheckBox, Frequency = Generator1FrequencyTextBox, Duty = Generator1DutyTextBox, Phase = Generator1PhaseTextBox, ActiveHigh = Generator1ActiveHighCheckBox, Repeat = Generator1RepeatTextBox, CableDelay = Generator1CableDelayTextBox, StartMode = Generator1StartModeCombo, Start = Generator1StartTextBox, Route = Generator1RouteCombo, Detail = Generator1DetailText, Clear = Generator1ClearButton, Apply = Generator1ApplyButton },
                new SignalGeneratorControls { Status = Generator2StatusText, Enabled = Generator2EnabledCheckBox, Frequency = Generator2FrequencyTextBox, Duty = Generator2DutyTextBox, Phase = Generator2PhaseTextBox, ActiveHigh = Generator2ActiveHighCheckBox, Repeat = Generator2RepeatTextBox, CableDelay = Generator2CableDelayTextBox, StartMode = Generator2StartModeCombo, Start = Generator2StartTextBox, Route = Generator2RouteCombo, Detail = Generator2DetailText, Clear = Generator2ClearButton, Apply = Generator2ApplyButton },
                new SignalGeneratorControls { Status = Generator3StatusText, Enabled = Generator3EnabledCheckBox, Frequency = Generator3FrequencyTextBox, Duty = Generator3DutyTextBox, Phase = Generator3PhaseTextBox, ActiveHigh = Generator3ActiveHighCheckBox, Repeat = Generator3RepeatTextBox, CableDelay = Generator3CableDelayTextBox, StartMode = Generator3StartModeCombo, Start = Generator3StartTextBox, Route = Generator3RouteCombo, Detail = Generator3DetailText, Clear = Generator3ClearButton, Apply = Generator3ApplyButton },
                new SignalGeneratorControls { Status = Generator4StatusText, Enabled = Generator4EnabledCheckBox, Frequency = Generator4FrequencyTextBox, Duty = Generator4DutyTextBox, Phase = Generator4PhaseTextBox, ActiveHigh = Generator4ActiveHighCheckBox, Repeat = Generator4RepeatTextBox, CableDelay = Generator4CableDelayTextBox, StartMode = Generator4StartModeCombo, Start = Generator4StartTextBox, Route = Generator4RouteCombo, Detail = Generator4DetailText, Clear = Generator4ClearButton, Apply = Generator4ApplyButton }
            };
            frequencyCounterControls = new[]
            {
                new FrequencyCounterControls { Status = Frequency1StatusText, Value = Frequency1ValueText, Seconds = Frequency1SecondsTextBox, Route = Frequency1RouteCombo, Detail = Frequency1DetailText, Apply = Frequency1ApplyButton },
                new FrequencyCounterControls { Status = Frequency2StatusText, Value = Frequency2ValueText, Seconds = Frequency2SecondsTextBox, Route = Frequency2RouteCombo, Detail = Frequency2DetailText, Apply = Frequency2ApplyButton },
                new FrequencyCounterControls { Status = Frequency3StatusText, Value = Frequency3ValueText, Seconds = Frequency3SecondsTextBox, Route = Frequency3RouteCombo, Detail = Frequency3DetailText, Apply = Frequency3ApplyButton },
                new FrequencyCounterControls { Status = Frequency4StatusText, Value = Frequency4ValueText, Seconds = Frequency4SecondsTextBox, Route = Frequency4RouteCombo, Detail = Frequency4DetailText, Apply = Frequency4ApplyButton }
            };
            startupArguments = Environment.GetCommandLineArgs();
            refreshTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            refreshTimer.Tick += RefreshTimer_Tick;
            RefreshGenericComPorts(false);
            InitializeProductFeatures();
            SetGenericSerialControlsEnabled(false);
            RefreshSessionLogView(false);
        }

        private void ApplyStartupView(string[] arguments)
        {
            string page = null;
            int? uartPort = null;
            string comPort = null;
            double requestedWidth = Width;
            double requestedHeight = Height;
            for (int index = 1; index < arguments.Length; index++)
            {
                string argument = arguments[index] ?? string.Empty;
                if (argument.StartsWith("--page=", StringComparison.OrdinalIgnoreCase))
                    page = argument.Substring(7);
                else if (argument.Equals("--page", StringComparison.OrdinalIgnoreCase) &&
                         index + 1 < arguments.Length)
                    page = arguments[++index];
                else if (argument.StartsWith("--uart-port=", StringComparison.OrdinalIgnoreCase))
                {
                    int value;
                    if (int.TryParse(argument.Substring(12), NumberStyles.Integer,
                        CultureInfo.InvariantCulture, out value) && value >= 0 && value <= 3)
                        uartPort = value;
                }
                else if (argument.StartsWith("--com-port=",
                         StringComparison.OrdinalIgnoreCase))
                    comPort = argument.Substring(11);
                else if (argument.StartsWith("--width=",
                         StringComparison.OrdinalIgnoreCase))
                    double.TryParse(argument.Substring(8), NumberStyles.Float,
                        CultureInfo.InvariantCulture, out requestedWidth);
                else if (argument.StartsWith("--height=",
                         StringComparison.OrdinalIgnoreCase))
                    double.TryParse(argument.Substring(9), NumberStyles.Float,
                        CultureInfo.InvariantCulture, out requestedHeight);
            }

            Width = Math.Max(MinWidth, Math.Min(3840, requestedWidth));
            Height = Math.Max(MinHeight, Math.Min(2160, requestedHeight));

            if (uartPort.HasValue)
                UartPortCombo.SelectedIndex = uartPort.Value;
            else if (!string.IsNullOrWhiteSpace(comPort))
                SelectComboTag(UartPortCombo, GenericComTagPrefix + comPort);
            if (string.IsNullOrWhiteSpace(page))
                return;
            if (page.Equals("Flash", StringComparison.OrdinalIgnoreCase))
            {
                ShowPage("Flash");
                return;
            }
            RadioButton navigation = page.Equals("Clock", StringComparison.OrdinalIgnoreCase) ? ClockNav :
                page.Equals("Gnss", StringComparison.OrdinalIgnoreCase) ? GnssNav :
                page.Equals("Atomic", StringComparison.OrdinalIgnoreCase) ? AtomicNav :
                page.Equals("Oscillatord", StringComparison.OrdinalIgnoreCase) ? OscillatordNav :
                page.Equals("Uart", StringComparison.OrdinalIgnoreCase) ? UartNav :
                page.Equals("Sma", StringComparison.OrdinalIgnoreCase) ? SmaNav :
                page.Equals("Timing", StringComparison.OrdinalIgnoreCase) ? TimingNav :
                page.Equals("Fpga", StringComparison.OrdinalIgnoreCase) ? FpgaNav :
                page.Equals("Sensors", StringComparison.OrdinalIgnoreCase) ? SensorsNav :
                page.Equals("I2c", StringComparison.OrdinalIgnoreCase) ? I2cNav :
                page.Equals("Telemetry", StringComparison.OrdinalIgnoreCase) ? TelemetryNav :
                page.Equals("Operations", StringComparison.OrdinalIgnoreCase) ? OperationsNav :
                page.Equals("Subsystems", StringComparison.OrdinalIgnoreCase) ? SubsystemsNav :
                page.Equals("Diagnostics", StringComparison.OrdinalIgnoreCase) ? DiagnosticsNav :
                OverviewNav;
            navigation.IsChecked = true;
            ShowPage(Convert.ToString(navigation.Tag, CultureInfo.InvariantCulture));
        }

        private async void Window_Loaded(object sender, RoutedEventArgs e)
        {
            Log("OCP Time Card Control Center started.");
            string capturePath = StartupArgumentValue(startupArguments, "--capture");
            if (!productSettings.DemoMode && string.IsNullOrWhiteSpace(capturePath) && !HasAdministratorAccess())
            {
                MessageBoxResult elevationChoice = MessageBox.Show(this,
                    "Time Card Control Center is running without administrator access. " +
                    "Some hardware features may be unavailable.\n\n" +
                    "Restart now with administrator access?",
                    "Administrator access recommended", MessageBoxButton.YesNo,
                    MessageBoxImage.Information, MessageBoxResult.Yes);
                if (elevationChoice == MessageBoxResult.Yes && RestartAsAdministrator())
                    return;
            }
            refreshTimer.Start();
            if (productSettings.DemoMode)
            {
                CardSelector.Text = "Demo telemetry (no hardware)";
                UpdateDeviceSelectionControls();
                SetConnectionState(true, "Demo telemetry", false);
                UpdateDemoProduct();
            }
            else
                await ConnectAsync();
            ApplyStartupView(startupArguments);
            if (!string.IsNullOrWhiteSpace(capturePath))
            {
                int captureDelay = 300;
                string captureDelayArgument = StartupArgumentValue(startupArguments, "--capture-delay");
                int parsedCaptureDelay;
                if (int.TryParse(captureDelayArgument, NumberStyles.Integer,
                    CultureInfo.InvariantCulture, out parsedCaptureDelay))
                    captureDelay = Math.Max(100, Math.Min(10000, parsedCaptureDelay));
                await Task.Delay(captureDelay);
                CaptureWindow(Path.GetFullPath(capturePath));
                Close();
            }
        }

        private static string StartupArgumentValue(string[] arguments, string name)
        {
            for (int index = 1; index < arguments.Length; index++)
            {
                string argument = arguments[index] ?? string.Empty;
                if (argument.StartsWith(name + "=", StringComparison.OrdinalIgnoreCase))
                    return argument.Substring(name.Length + 1);
                if (argument.Equals(name, StringComparison.OrdinalIgnoreCase) &&
                    index + 1 < arguments.Length)
                    return arguments[index + 1];
            }
            return null;
        }

        private void CaptureWindow(string path)
        {
            UpdateLayout();
            int width = Math.Max(1, (int)Math.Ceiling(RootBorder.ActualWidth));
            int height = Math.Max(1, (int)Math.Ceiling(RootBorder.ActualHeight));
            RenderTargetBitmap bitmap = new RenderTargetBitmap(width, height,
                96, 96, PixelFormats.Pbgra32);
            bitmap.Render(RootBorder);
            PngBitmapEncoder encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using (FileStream output = File.Create(path))
                encoder.Save(output);
        }

        private void Window_Closing(object sender, CancelEventArgs e)
        {
            if (!flashUpdating)
                return;
            e.Cancel = true;
            MessageBox.Show(this,
                "The FPGA flash update is still running. Keep the card powered and wait for verification to finish before closing the Control Center.",
                "Firmware update in progress", MessageBoxButton.OK,
                MessageBoxImage.Warning);
        }

        private void Window_Closed(object sender, EventArgs e)
        {
            CloseDeviceSelection();
            refreshTimer.Stop();
            imuCubeShowcaseTimer.Stop();
            StopUartMonitor();
            CloseMonitoredGenericComPort();
            if (nativeDisciplineEngine != null)
            {
                nativeDisciplineEngine.Dispose();
                nativeDisciplineEngine = null;
            }
            if (client != null)
            {
                client.Dispose();
                client = null;
            }
        }

        private async void RefreshTimer_Tick(object sender, EventArgs e)
        {
            if (productSettings != null && productSettings.DemoMode)
            {
                UpdateDemoProduct();
                return;
            }
            if (client == null)
            {
                if (!connecting && DateTime.UtcNow - lastConnectionAttemptUtc > TimeSpan.FromSeconds(5))
                    await ConnectAsync();
                return;
            }
            await RefreshSnapshotAsync(false);
            if (SensorsPage.Visibility == Visibility.Visible ||
                TelemetryPage.Visibility == Visibility.Visible)
                await RefreshSensorsAsync();
        }

        private async Task ConnectAsync()
        {
            await ConnectSelectedDeviceAsync();
        }

        private async Task RefreshSnapshotAsync(bool logSuccess)
        {
            TimeCardClient activeClient;
            int generation;
            if (refreshing || !TryCaptureDeviceSession(out activeClient,
                out generation))
                return;
            refreshing = true;
            try
            {
                TimeCardSnapshot snapshot = await Task.Run(() =>
                    activeClient.GetSnapshot());
                if (!IsDeviceSessionCurrent(activeClient, generation))
                    return;
                lastSnapshot = snapshot;
                ApplySnapshot(snapshot);
                if (logSuccess)
                    Log(string.Format("Driver {0}, ABI {1}, {2} with {3} interrupt messages.",
                        snapshot.DriverVersion, snapshot.AbiVersion, snapshot.Layout, snapshot.InterruptMessages));
            }
            catch (Exception ex)
            {
                if (!IsDeviceSessionCurrent(activeClient, generation))
                    return;
                Log("Telemetry refresh failed: " + ex.Message);
                await RetireFailedDeviceSessionAsync(activeClient, generation);
            }
            finally
            {
                refreshing = false;
            }
        }

        private void ApplySnapshot(TimeCardSnapshot snapshot)
        {
            string cardTime = FormatUtc(snapshot.CardTimeUtc);
            string systemTime = FormatUtc(snapshot.SystemTimeUtc);
            string offset = FormatNanoseconds(snapshot.OffsetNanoseconds);
            string sampleWindow = FormatNanoseconds(snapshot.SamplingWindowNanoseconds);
            Brush healthyBrush = (Brush)FindResource("AccentBrush");
            Brush warningBrush = (Brush)FindResource("GoldBrush");
            bool clockStatusAvailable = snapshot.ClockStatus != uint.MaxValue;

            SidebarDriverText.Text = "Driver " + snapshot.DriverVersion + " · ABI " + snapshot.AbiVersion;
            LastRefreshText.Text = "Sampled " + DateTime.Now.ToString("HH:mm:ss", CultureInfo.InvariantCulture);
            UpdateI2cDriverCompatibility(snapshot);
            UpdateSensorsCompatibility(snapshot);
            ConfigureAtomicWorkspaceForProfile(
                snapshot.Layout == "Orolia ART");
            ConfigureUartWorkspaceForProfile(
                snapshot.Layout == "Orolia ART");

            SyncStatusText.Text = !clockStatusAvailable ? "NOT EXPOSED" :
                snapshot.IsClockSynchronized ? "IN SYNC" : "NOT LOCKED";
            SyncStatusText.Foreground = snapshot.IsClockSynchronized ? healthyBrush : warningBrush;
            SyncDetailText.Text = clockStatusAvailable ?
                string.Format("Status 0x{0:X8}", snapshot.ClockStatus) :
                "Clock status requires core version 1.2 or newer";
            GnssFixMetricText.Text = snapshot.GnssFix;
            GnssFixMetricText.Foreground = snapshot.GnssFixOk ? healthyBrush : warningBrush;
            SatelliteText.Text = !snapshot.GnssTelemetryAvailable
                ? "Not exposed by this FPGA image"
                : snapshot.SatelliteDataValid
                ? string.Format("{0} seen · {1} locked", snapshot.SeenSatellites, snapshot.LockedSatellites)
                : "Satellite count not valid";
            InterruptText.Text = snapshot.InterruptMessages.ToString(CultureInfo.InvariantCulture);
            LayoutText.Text = snapshot.Layout + " register layout";
            ClockVersionText.Text = "v" + snapshot.ClockVersion;
            ClockSourceText.Text = ClockSourceName(snapshot.ClockSource);
            CardTimeText.Text = cardTime;
            SystemTimeText.Text = snapshot.SystemTimeUtc.ToString(
                "HH:mm:ss.fffffff 'UTC'", CultureInfo.InvariantCulture);
            OffsetText.Text = offset;
            SamplingWindowText.Text = sampleWindow;
            UpdateClockHistory(snapshot, offset, sampleWindow);
            ClockChipText.Text = !clockStatusAvailable ? "STATUS UNAVAILABLE" :
                snapshot.IsClockSynchronized ? "SYNCHRONIZED" : "LIVE · UNLOCKED";
            ClockChipText.Foreground = snapshot.IsClockSynchronized ? healthyBrush : warningBrush;
            HierarchyOverviewText.Text = snapshot.HierarchyRuntimeEnabled ? "ENABLED" : "DISABLED";

            ClockCardTimeText.Text = cardTime;
            ClockSystemTimeText.Text = systemTime;
            ClockOffsetText.Text = offset;
            ClockSyncText.Text = !clockStatusAvailable ? "NOT EXPOSED" :
                snapshot.IsClockSynchronized ? "IN SYNC" : "NOT IN SYNC";
            ClockSyncText.Foreground = snapshot.IsClockSynchronized ? healthyBrush : warningBrush;
            ClockStatusRawText.Text = clockStatusAvailable ?
                string.Format("Status register 0x{0:X8}", snapshot.ClockStatus) :
                "Status register not available before Clock core v1.2";
            ClockSamplingText.Text = sampleWindow;
            ClockVersionDetailText.Text = "v" + snapshot.ClockVersion;
            ClockSourceDetailText.Text = ClockSourceName(snapshot.ClockSource);
            ClockOffsetAddressText.Text = string.Format("0x{0:X8}", snapshot.ClockOffset);
            SelectComboTag(ClockSourceCombo, snapshot.ClockSource);
            ClockSourceApplyButton.IsEnabled = snapshot.AbiVersion >= 4;
            ClockSourceApplyStatusText.Text = snapshot.AbiVersion >= 4 ?
                "Active selector 0x" + snapshot.ClockSource.ToString("X2", CultureInfo.InvariantCulture) :
                "Install driver 1.9 / ABI 4 to change source";

            GnssFixText.Text = snapshot.GnssFix;
            GnssFixText.Foreground = snapshot.GnssFixOk ? healthyBrush : warningBrush;
            GnssFixValidityText.Text = !snapshot.GnssTelemetryAvailable ?
                "GNSS summary registers are not exposed by this FPGA image" :
                snapshot.GnssFixOk ? "Receiver reports a valid fix" :
                "Fix not currently asserted";
            SatellitesSeenText.Text = snapshot.GnssTelemetryAvailable ?
                snapshot.SeenSatellites.ToString(CultureInfo.InvariantCulture) : "—";
            SatellitesLockedText.Text = snapshot.GnssTelemetryAvailable ?
                snapshot.LockedSatellites.ToString(CultureInfo.InvariantCulture) : "—";
            SatelliteValidityText.Text = !snapshot.GnssTelemetryAvailable ?
                "Satellite summary is not available" :
                snapshot.SatelliteDataValid ? "Satellite count is valid" :
                "Satellite count not valid";
            bool utcValid = snapshot.TodTelemetryAvailable &&
                (snapshot.UtcStatus & (1u << 8)) != 0;
            int utcOffset = (int)(snapshot.UtcStatus & 0xff);
            UtcMetricText.Text = !snapshot.TodTelemetryAvailable ?
                "NOT AVAILABLE" :
                utcValid ? "UTC +" + utcOffset : "NOT VALID";
            LeapMetricText.Text = !snapshot.TodTelemetryAvailable ?
                "UTC/leap summary is not exposed by this FPGA image" :
                (snapshot.UtcStatus & (1u << 16)) != 0
                ? string.Format("Leap info valid · next {0} s", unchecked((int)snapshot.Leap))
                : "Leap information not valid";
            GnssRawText.Text = snapshot.GnssTelemetryAvailable ?
                string.Format("0x{0:X8}", snapshot.GnssStatus) : "Not exposed";
            SatelliteRawText.Text = snapshot.GnssTelemetryAvailable ?
                string.Format("0x{0:X8}", snapshot.Satellites) : "Not exposed";
            TodRawText.Text = snapshot.TodStatus != uint.MaxValue ?
                string.Format("0x{0:X8}", snapshot.TodStatus) : "Not exposed";
            UtcRawText.Text = snapshot.TodTelemetryAvailable ?
                string.Format("0x{0:X8}", snapshot.UtcStatus) : "Not exposed";
            LeapRawText.Text = snapshot.TodTelemetryAvailable ?
                string.Format("0x{0:X8}", snapshot.Leap) : "Not exposed";

            HierarchyRuntimeText.Text = snapshot.HierarchyRuntimeEnabled ? "enabled" : "disabled";
            HierarchyRuntimeText.Foreground = snapshot.HierarchyRuntimeEnabled ? healthyBrush : warningBrush;
            HierarchyPersistedText.Text = snapshot.HierarchyPersisted ? "enabled" : "disabled";
            HierarchyPersistedText.Foreground = snapshot.HierarchyPersisted ? healthyBrush : warningBrush;
            DiagnosticsSummaryText.Text = BuildDiagnostics(snapshot);
            UpdateBoardLedAutomationAsync(false);
            UpdateProductSnapshot(snapshot);
        }

        private void UpdateClockHistory(TimeCardSnapshot snapshot, string offset,
                                        string sampleWindow)
        {
            OffsetHistoryChart.AddSample(snapshot.OffsetNanoseconds);
            SamplingHistoryChart.AddSample(snapshot.SamplingWindowNanoseconds);
            ClockOffsetHistoryChart.AddSample(snapshot.OffsetNanoseconds);
            ClockSamplingHistoryChart.AddSample(
                snapshot.SamplingWindowNanoseconds);
            OffsetHistoryValueText.Text = offset;
            SamplingHistoryValueText.Text = sampleWindow;
            ClockOffsetHistoryValueText.Text = offset;
            ClockSamplingHistoryValueText.Text = sampleWindow;

            if (OffsetHistoryChart.SampleCount < 2)
            {
                OffsetHistoryScaleText.Text = string.Format(CultureInfo.InvariantCulture,
                    "COLLECTING · {0}/{1}", OffsetHistoryChart.SampleCount,
                    OffsetHistoryChart.Capacity);
            }
            else
            {
                OffsetHistoryScaleText.Text = "AUTO ZOOM " +
                    FormatNanoseconds(ToNanoseconds(
                        OffsetHistoryChart.VisibleMinimum)) + " \u2013 " +
                    FormatNanoseconds(ToNanoseconds(
                        OffsetHistoryChart.VisibleMaximum));
            }
            ClockOffsetHistoryScaleText.Text = OffsetHistoryScaleText.Text;

            if (SamplingHistoryChart.SampleCount < 2)
            {
                SamplingHistoryScaleText.Text = string.Format(CultureInfo.InvariantCulture,
                    "COLLECTING · {0}/{1}", SamplingHistoryChart.SampleCount,
                    SamplingHistoryChart.Capacity);
            }
            else
            {
                SamplingHistoryScaleText.Text = "RANGE " +
                    FormatNanoseconds(ToNanoseconds(SamplingHistoryChart.Minimum)) +
                    " \u2013 " +
                    FormatNanoseconds(ToNanoseconds(SamplingHistoryChart.Maximum));
            }
            ClockSamplingHistoryScaleText.Text = SamplingHistoryScaleText.Text;
        }

        private void ResetClockHistory()
        {
            OffsetHistoryChart.Clear();
            SamplingHistoryChart.Clear();
            ClockOffsetHistoryChart.Clear();
            ClockSamplingHistoryChart.Clear();
            OffsetHistoryValueText.Text = "\u2014";
            SamplingHistoryValueText.Text = "\u2014";
            ClockOffsetHistoryValueText.Text = "\u2014";
            ClockSamplingHistoryValueText.Text = "\u2014";
            OffsetHistoryScaleText.Text = "WAITING FOR SAMPLES";
            SamplingHistoryScaleText.Text = "WAITING FOR SAMPLES";
            ClockOffsetHistoryScaleText.Text = "WAITING FOR SAMPLES";
            ClockSamplingHistoryScaleText.Text = "WAITING FOR SAMPLES";
        }

        private static long ToNanoseconds(double value)
        {
            if (value >= long.MaxValue)
                return long.MaxValue;
            if (value <= long.MinValue)
                return long.MinValue;
            return (long)Math.Round(value, MidpointRounding.AwayFromZero);
        }

        private void SetConnectionState(bool connected, string text, bool showElevation)
        {
            ConnectionText.Text = text;
            ConnectionDot.Fill = (Brush)FindResource(connected ? "AccentBrush" : "DangerBrush");
            ElevateButton.Visibility = showElevation ? Visibility.Visible : Visibility.Collapsed;
            if (!connected)
                ResetClockHistory();
        }

        private async void Refresh_Click(object sender, RoutedEventArgs e)
        {
            if (client == null)
                await ConnectAsync();
            else
                await RefreshSnapshotAsync(true);
        }

        private async void SetClock_Click(object sender, RoutedEventArgs e)
        {
            if (!EnsureConnected())
                return;
            try
            {
                await Task.Run(() => client.SetClockFromSystem());
                Log("PHC synchronized from the system clock.");
                await RefreshSnapshotAsync(false);
            }
            catch (Exception ex)
            {
                Log("Clock synchronization failed: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Unable to set the PHC", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void SetSystemFromClock_Click(object sender,
                                                     RoutedEventArgs e)
        {
            if (!EnsureConnected())
                return;
            try
            {
                DateTime cardUtc = await Task.Run(
                    () => client.GetEstimatedClockTimeUtc());
                if (cardUtc.Year < 2020 || cardUtc.Year > 2100)
                {
                    MessageBox.Show(this,
                        "The Time Card PHC currently reports " +
                        cardUtc.ToString("yyyy-MM-dd HH:mm:ss.fff 'UTC'",
                            CultureInfo.InvariantCulture) +
                        ". Synchronize the PHC from the system clock or establish GNSS time before using it to set Windows.",
                        "Time Card UTC is not plausible", MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                    return;
                }

                string synchronizationWarning = lastSnapshot != null &&
                    !lastSnapshot.IsClockSynchronized ?
                    "\n\nThe PHC is not currently reporting synchronization." :
                    string.Empty;
                MessageBoxResult confirmation = MessageBox.Show(this,
                    "Set Windows system time from the Time Card PHC?\n\n" +
                    "Time Card: " + cardUtc.ToString(
                        "yyyy-MM-dd HH:mm:ss.fff 'UTC'",
                        CultureInfo.InvariantCulture) + "\n" +
                    "Windows:   " + DateTime.UtcNow.ToString(
                        "yyyy-MM-dd HH:mm:ss.fff 'UTC'",
                        CultureInfo.InvariantCulture) +
                    synchronizationWarning +
                    "\n\nThis changes the system clock immediately.",
                    "Confirm Windows time synchronization",
                    MessageBoxButton.YesNo, MessageBoxImage.Warning);
                if (confirmation != MessageBoxResult.Yes)
                    return;

                DateTime appliedUtc = await Task.Run(
                    () => client.SetSystemClockFromTimeCard());
                Log("Windows UTC set from the Time Card PHC at " +
                    appliedUtc.ToString("yyyy-MM-dd HH:mm:ss.fff 'UTC'",
                        CultureInfo.InvariantCulture) + ".");
                await RefreshSnapshotAsync(false);
            }
            catch (Exception ex)
            {
                Log("Windows time synchronization failed: " + ex.Message);
                MessageBox.Show(this, ex.Message,
                    "Unable to set Windows from the Time Card",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void ApplyClockSource_Click(object sender, RoutedEventArgs e)
        {
            if (!EnsureConnected())
                return;
            if (lastSnapshot == null || lastSnapshot.AbiVersion < 4)
            {
                MessageBox.Show(this, "Clock-source control requires Time Card driver 1.9 / ABI 4.",
                    "Driver update required", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            ComboBoxItem item = ClockSourceCombo.SelectedItem as ComboBoxItem;
            if (item == null)
                return;
            uint source = uint.Parse(Convert.ToString(item.Tag, CultureInfo.InvariantCulture),
                CultureInfo.InvariantCulture);
            string name = Convert.ToString(item.Content, CultureInfo.InvariantCulture);
            uint requiredContract = source == 7u ? 0x100u :
                source == 8u ? 0x400u : source == 253u ? 0x800u : 0u;
            if (requiredContract != 0u &&
                (lastFpgaContract == null ||
                 !lastFpgaContract.Allows(requiredContract)))
            {
                MessageBox.Show(this,
                    "This synthesis-optional clock source is locked. " +
                    "Activate the matching exact-image capability in the " +
                    "FPGA Engines workspace first.",
                    "Exact image contract required", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }
            if (MessageBox.Show(this,
                "Set the PHC clock source to " + name + "? The clock can lose synchronization until that source is valid.",
                "Confirm clock source", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            ClockSourceApplyButton.IsEnabled = false;
            try
            {
                uint active = await Task.Run(() => client.SetClockSource(source));
                ClockSourceApplyStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                    "Requested {0} · active {1}", ClockSourceName(source), ClockSourceName(active));
                Log("Clock source set to " + name + ".");
                await RefreshSnapshotAsync(false);
            }
            catch (Exception ex)
            {
                ClockSourceApplyStatusText.Text = "Source change failed";
                Log("Clock source change failed: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Unable to change clock source",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                ClockSourceApplyButton.IsEnabled = client != null &&
                    lastSnapshot != null && lastSnapshot.AbiVersion >= 4;
            }
        }

        private async void EnableHierarchy_Click(object sender, RoutedEventArgs e)
        {
            await ChangeHierarchyAsync(1, false, "Subsystem hierarchy enabled for this runtime session.");
        }

        private async void PersistHierarchy_Click(object sender, RoutedEventArgs e)
        {
            await ChangeHierarchyAsync(1, true, "Subsystem hierarchy enabled and persisted for the next device start.");
        }

        private async void DisableHierarchy_Click(object sender, RoutedEventArgs e)
        {
            MessageBoxResult result = MessageBox.Show(this,
                "Disable the subsystem hierarchy? Existing child devices disappear after the controller or Windows is restarted.",
                "Disable Time Card hierarchy", MessageBoxButton.YesNo, MessageBoxImage.Warning);
            if (result == MessageBoxResult.Yes)
                await ChangeHierarchyAsync(2, true, "Subsystem hierarchy persistence disabled.");
        }

        private async Task ChangeHierarchyAsync(uint action, bool persist, string message)
        {
            if (!EnsureConnected())
                return;
            try
            {
                await Task.Run(() => client.ControlHierarchy(action, persist));
                Log(message);
                await RefreshSnapshotAsync(false);
            }
            catch (Exception ex)
            {
                Log("Hierarchy control failed: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Hierarchy control failed", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void RefreshComPorts_Click(object sender, RoutedEventArgs e)
        {
            RefreshGenericComPorts(true);
        }

        private void RefreshGenericComPorts(bool userInitiated)
        {
            if (UartPortCombo == null || refreshingGenericComPorts)
                return;

            ComboBoxItem selectedItem = UartPortCombo.SelectedItem as ComboBoxItem;
            string selectedTag = selectedItem == null ? null :
                Convert.ToString(selectedItem.Tag, CultureInfo.InvariantCulture);
            try
            {
                string[] portNames = SerialPort.GetPortNames()
                    .Where(name => !string.IsNullOrWhiteSpace(name))
                    .Distinct(StringComparer.OrdinalIgnoreCase)
                    .OrderBy(ComPortNumber)
                    .ThenBy(name => name, StringComparer.OrdinalIgnoreCase)
                    .ToArray();

                refreshingGenericComPorts = true;
                foreach (ComboBoxItem item in UartPortCombo.Items
                    .OfType<ComboBoxItem>().Where(IsGenericComPortItem).ToList())
                    UartPortCombo.Items.Remove(item);
                foreach (string portName in portNames)
                {
                    UartPortCombo.Items.Add(new ComboBoxItem
                    {
                        Tag = GenericComTagPrefix + portName,
                        Content = portName + " · Windows serial port"
                    });
                }

                ComboBoxItem restored = UartPortCombo.Items
                    .OfType<ComboBoxItem>().FirstOrDefault(item =>
                        string.Equals(Convert.ToString(item.Tag,
                            CultureInfo.InvariantCulture), selectedTag,
                            StringComparison.OrdinalIgnoreCase));
                UartPortCombo.SelectedItem = restored ??
                    UartPortCombo.Items.OfType<ComboBoxItem>().FirstOrDefault();
                refreshingGenericComPorts = false;

                ComboBoxItem currentItem = UartPortCombo.SelectedItem as ComboBoxItem;
                string currentTag = currentItem == null ? null :
                    Convert.ToString(currentItem.Tag, CultureInfo.InvariantCulture);
                if (!string.Equals(selectedTag, currentTag,
                    StringComparison.OrdinalIgnoreCase))
                    UartPort_SelectionChanged(UartPortCombo, null);
                if (userInitiated && UartStatusText != null)
                {
                    UartStatusText.Text = portNames.Length == 0 ?
                        "No Windows COM ports found · Time Card UARTs remain available" :
                        string.Format(CultureInfo.InvariantCulture,
                            "Found {0} Windows COM port(s): {1}", portNames.Length,
                            string.Join(", ", portNames));
                }
            }
            catch (Exception ex)
            {
                refreshingGenericComPorts = false;
                if (userInitiated && UartStatusText != null)
                    UartStatusText.Text = "Unable to enumerate Windows COM ports";
                Log("COM-port enumeration failed: " + ex.Message);
            }
        }

        private static bool IsGenericComPortItem(ComboBoxItem item)
        {
            string tag = item == null ? null : Convert.ToString(item.Tag,
                CultureInfo.InvariantCulture);
            return tag != null && tag.StartsWith(GenericComTagPrefix,
                StringComparison.OrdinalIgnoreCase);
        }

        private static int ComPortNumber(string portName)
        {
            int number;
            return portName != null &&
                portName.StartsWith("COM", StringComparison.OrdinalIgnoreCase) &&
                int.TryParse(portName.Substring(3), NumberStyles.Integer,
                    CultureInfo.InvariantCulture, out number) ?
                number : int.MaxValue;
        }

        private async void ConfigureUart_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                uint baud = ParseUnsigned(UartBaudCombo.Text, "baud rate", 1,
                    int.MaxValue);
                string genericPort = SelectedGenericComPort();
                if (genericPort != null)
                {
                    GenericSerialSettings settings =
                        SelectedGenericSerialSettings((int)baud);
                    await Task.Run(() => ConfigureGenericComPort(genericPort,
                        settings));
                    UartStatusText.Text = string.Format(
                        CultureInfo.InvariantCulture,
                        "{0} configured · {1}", genericPort, settings);
                    Log(UartStatusText.Text + ".");
                    return;
                }
                if (!EnsureConnected())
                    return;
                uint port = SelectedUartPort();
                await Task.Run(() => client.ConfigureUart(port, baud));
                ResetUartDecoder(port);
                UartStatusText.Text = string.Format("UART {0} configured at {1} baud · 8N1", port, baud);
                Log(UartStatusText.Text + ".");
            }
            catch (Exception ex)
            {
                ShowUartError("UART configuration failed", ex);
            }
        }

        private async void ReadUart_Click(object sender, RoutedEventArgs e)
        {
            await ReadUartOnceAsync();
        }

        private async Task ReadUartOnceAsync()
        {
            try
            {
                uint bytes = ParseUnsigned(UartBytesTextBox.Text, "byte count", 1, 256);
                uint timeout = ParseUnsigned(UartTimeoutTextBox.Text, "timeout", 0, 5000);
                string genericPort = SelectedGenericComPort();
                if (genericPort != null)
                {
                    uint baud = ParseUnsigned(UartBaudCombo.Text, "baud rate", 1,
                        int.MaxValue);
                    GenericSerialSettings settings =
                        SelectedGenericSerialSettings((int)baud);
                    byte[] data = await Task.Run(() => ReadGenericComPort(
                        genericPort, settings, (int)bytes, (int)timeout));
                    AppendGenericUart(genericPort, "RX", data);
                    UartStatusText.Text = string.Format(
                        CultureInfo.InvariantCulture,
                        "Read {0} byte(s) from {1}", data.Length, genericPort);
                    return;
                }
                if (!EnsureConnected())
                    return;
                uint port = SelectedUartPort();
                UartReadResult result = await Task.Run(() => client.ReadUart(port, bytes, timeout));
                AppendUart(port, "RX", result.Data, result.LineStatus);
                UartStatusText.Text = string.Format("Read {0} byte(s) · LSR 0x{1:X2}", result.Data.Length, result.LineStatus & 0xff);
            }
            catch (Exception ex)
            {
                ShowUartError("UART read failed", ex);
            }
        }

        private async void SendUart_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                byte[] data = ParseUartSendData();
                if (data.Length == 0)
                    throw new InvalidOperationException("Enter text or hexadecimal bytes to send.");
                string genericPort = SelectedGenericComPort();
                if (genericPort != null)
                {
                    uint baud = ParseUnsigned(UartBaudCombo.Text, "baud rate", 1,
                        int.MaxValue);
                    GenericSerialSettings settings =
                        SelectedGenericSerialSettings((int)baud);
                    await Task.Run(() => WriteGenericComPort(genericPort,
                        settings, data));
                    AppendGenericUart(genericPort, "TX", data);
                    UartStatusText.Text = string.Format(
                        CultureInfo.InvariantCulture,
                        "Wrote {0} byte(s) to {1}", data.Length, genericPort);
                    return;
                }
                if (!EnsureConnected())
                    return;
                uint port = SelectedUartPort();
                UartWriteResult result = await Task.Run(() => client.WriteUart(port, data, 1000));
                AppendUart(port, "TX", data, result.LineStatus);
                UartStatusText.Text = string.Format("Wrote {0} byte(s) · LSR 0x{1:X2}",
                    result.BytesTransferred, result.LineStatus & 0xff);
            }
            catch (Exception ex)
            {
                ShowUartError("UART write failed", ex);
            }
        }

        private void MonitorUart_Click(object sender, RoutedEventArgs e)
        {
            if (uartMonitorCancellation != null)
            {
                StopUartMonitor();
                return;
            }
            string genericPort = SelectedGenericComPort();
            if (genericPort == null && !EnsureConnected())
                return;
            CancellationTokenSource monitorCancellation =
                new CancellationTokenSource();
            uartMonitorCancellation = monitorCancellation;
            MonitorButton.Content = "Stop monitor";
            UartStatusText.Text = genericPort == null ?
                "Monitoring UART " + SelectedUartPort() :
                "Monitoring " + genericPort;
            uartMonitorTask = MonitorUartAsync(monitorCancellation);
        }

        private async Task MonitorUartAsync(
            CancellationTokenSource monitorCancellation)
        {
            await Task.Yield();
            CancellationToken cancellationToken =
                monitorCancellation.Token;
            try
            {
                uint bytes = ParseUnsigned(UartBytesTextBox.Text, "byte count", 1, 256);
                string genericPort = SelectedGenericComPort();
                if (genericPort != null)
                {
                    uint baud = ParseUnsigned(UartBaudCombo.Text, "baud rate", 1,
                        int.MaxValue);
                    GenericSerialSettings settings =
                        SelectedGenericSerialSettings((int)baud);
                    await MonitorGenericComPortAsync(genericPort, settings,
                        (int)bytes, cancellationToken);
                    return;
                }
                uint port = SelectedUartPort();
                TimeCardClient activeClient;
                int generation;
                if (!TryCaptureDeviceSession(out activeClient, out generation))
                    throw new InvalidOperationException(
                        "The selected Time Card is no longer connected.");
                while (!cancellationToken.IsCancellationRequested)
                {
                    UartReadResult result = await Task.Run(() =>
                        activeClient.ReadUart(port, bytes, 250));
                    if (!IsDeviceSessionCurrent(activeClient, generation))
                        return;
                    if (result.Data.Length != 0)
                    {
                        AppendUart(port, "RX", result.Data, result.LineStatus);
                        UartStatusText.Text = string.Format("Streaming UART {0} · last packet {1} bytes", port, result.Data.Length);
                    }
                    await Task.Delay(30, cancellationToken);
                }
            }
            catch (OperationCanceledException)
            {
            }
            catch (Exception ex)
            {
                if (!cancellationToken.IsCancellationRequested)
                    ShowUartError("UART monitor stopped", ex);
            }
            finally
            {
                if (ReferenceEquals(uartMonitorCancellation,
                    monitorCancellation))
                {
                    uartMonitorCancellation.Dispose();
                    uartMonitorCancellation = null;
                    uartMonitorTask = null;
                }
                MonitorButton.Content = "Start monitor";
                UartStatusText.Text = "UART idle";
            }
        }

        private void StopUartMonitor()
        {
            if (uartMonitorCancellation != null)
                uartMonitorCancellation.Cancel();
        }

        private static SerialPort CreateGenericSerialPort(string portName,
            GenericSerialSettings settings)
        {
            SerialPort port = new SerialPort(portName);
            ApplyGenericSerialSettings(port, settings);
            port.ReadTimeout = 250;
            port.WriteTimeout = 1000;
            return port;
        }

        private static void ApplyGenericSerialSettings(SerialPort port,
            GenericSerialSettings settings)
        {
            port.BaudRate = settings.Baud;
            port.DataBits = settings.DataBits;
            port.Parity = settings.Parity;
            port.StopBits = settings.StopBits;
            port.Handshake = settings.Handshake;
            port.DtrEnable = settings.DtrEnable;
            port.RtsEnable = settings.RtsEnable;
        }

        private GenericSerialSettings SelectedGenericSerialSettings(int baud)
        {
            ComboBoxItem parityItem = UartParityCombo.SelectedItem as ComboBoxItem;
            ComboBoxItem stopItem = UartStopBitsCombo.SelectedItem as ComboBoxItem;
            ComboBoxItem handshakeItem = UartHandshakeCombo.SelectedItem as ComboBoxItem;
            int dataBits;
            if (!int.TryParse(SelectedComboText(UartDataBitsCombo),
                NumberStyles.Integer, CultureInfo.InvariantCulture, out dataBits))
                dataBits = 8;
            return new GenericSerialSettings
            {
                Baud = baud,
                DataBits = dataBits,
                Parity = (Parity)Enum.Parse(typeof(Parity),
                    Convert.ToString(parityItem.Tag, CultureInfo.InvariantCulture)),
                StopBits = (StopBits)Enum.Parse(typeof(StopBits),
                    Convert.ToString(stopItem.Tag, CultureInfo.InvariantCulture)),
                Handshake = (Handshake)Enum.Parse(typeof(Handshake),
                    Convert.ToString(handshakeItem.Tag, CultureInfo.InvariantCulture)),
                DtrEnable = UartDtrCheckBox.IsChecked == true,
                RtsEnable = UartRtsCheckBox.IsChecked == true
            };
        }

        private void ConfigureGenericComPort(string portName,
                                             GenericSerialSettings settings)
        {
            lock (genericSerialSync)
            {
                if (monitoredGenericSerialPort != null &&
                    monitoredGenericSerialPort.IsOpen &&
                    string.Equals(monitoredGenericSerialPortName, portName,
                        StringComparison.OrdinalIgnoreCase))
                {
                    ApplyGenericSerialSettings(monitoredGenericSerialPort, settings);
                    return;
                }
            }

            using (SerialPort port = CreateGenericSerialPort(portName, settings))
                port.Open();
        }

        private byte[] ReadGenericComPort(string portName,
                                          GenericSerialSettings settings,
                                          int maximumBytes, int timeoutMs)
        {
            lock (genericSerialSync)
            {
                if (monitoredGenericSerialPort != null &&
                    monitoredGenericSerialPort.IsOpen &&
                    string.Equals(monitoredGenericSerialPortName, portName,
                        StringComparison.OrdinalIgnoreCase))
                {
                    ApplyGenericSerialSettings(monitoredGenericSerialPort, settings);
                    return ReadGenericSerialBytes(monitoredGenericSerialPort,
                        maximumBytes, timeoutMs);
                }
            }

            using (SerialPort port = CreateGenericSerialPort(portName, settings))
            {
                port.Open();
                return ReadGenericSerialBytes(port, maximumBytes, timeoutMs);
            }
        }

        private static byte[] ReadGenericSerialBytes(SerialPort port,
                                                       int maximumBytes,
                                                       int timeoutMs)
        {
            if (timeoutMs == 0 && port.BytesToRead == 0)
                return new byte[0];
            port.ReadTimeout = Math.Max(1, timeoutMs);
            byte[] data = new byte[maximumBytes];
            int count = 0;
            try
            {
                count = port.Read(data, 0, maximumBytes);
                while (count < maximumBytes && port.BytesToRead != 0)
                {
                    int read = port.Read(data, count, maximumBytes - count);
                    if (read == 0)
                        break;
                    count += read;
                }
            }
            catch (TimeoutException)
            {
            }
            if (count == data.Length)
                return data;
            byte[] result = new byte[count];
            if (count != 0)
                Array.Copy(data, result, count);
            return result;
        }

        private void WriteGenericComPort(string portName,
                                         GenericSerialSettings settings,
                                         byte[] data)
        {
            lock (genericSerialSync)
            {
                if (monitoredGenericSerialPort != null &&
                    monitoredGenericSerialPort.IsOpen &&
                    string.Equals(monitoredGenericSerialPortName, portName,
                        StringComparison.OrdinalIgnoreCase))
                {
                    ApplyGenericSerialSettings(monitoredGenericSerialPort, settings);
                    monitoredGenericSerialPort.Write(data, 0, data.Length);
                    return;
                }
            }

            using (SerialPort port = CreateGenericSerialPort(portName, settings))
            {
                port.Open();
                port.Write(data, 0, data.Length);
            }
        }

        private async Task MonitorGenericComPortAsync(string portName,
            GenericSerialSettings settings,
            int maximumBytes, CancellationToken cancellationToken)
        {
            SerialPort port = CreateGenericSerialPort(portName, settings);
            try
            {
                port.Open();
                lock (genericSerialSync)
                {
                    monitoredGenericSerialPort = port;
                    monitoredGenericSerialPortName = portName;
                }
                while (!cancellationToken.IsCancellationRequested)
                {
                    byte[] data = await Task.Run(() =>
                    {
                        lock (genericSerialSync)
                        {
                            if (!port.IsOpen || port.BytesToRead == 0)
                                return new byte[0];
                            return ReadGenericSerialBytes(port, maximumBytes, 100);
                        }
                    });
                    if (data.Length != 0)
                    {
                        AppendGenericUart(portName, "RX", data);
                        UartStatusText.Text = string.Format(
                            CultureInfo.InvariantCulture,
                            "Streaming {0} · last packet {1} byte(s)",
                            portName, data.Length);
                    }
                    await Task.Delay(30, cancellationToken);
                }
            }
            finally
            {
                lock (genericSerialSync)
                {
                    if (ReferenceEquals(monitoredGenericSerialPort, port))
                    {
                        monitoredGenericSerialPort = null;
                        monitoredGenericSerialPortName = null;
                    }
                    if (port.IsOpen)
                        port.Close();
                    port.Dispose();
                }
            }
        }

        private void CloseMonitoredGenericComPort()
        {
            lock (genericSerialSync)
            {
                if (monitoredGenericSerialPort == null)
                    return;
                try
                {
                    if (monitoredGenericSerialPort.IsOpen)
                        monitoredGenericSerialPort.Close();
                }
                catch
                {
                }
            }
        }

        private void ClearUart_Click(object sender, RoutedEventArgs e)
        {
            uartConsoleEntries.Clear();
            uartConsoleHistoryBytes = 0;
            ResetAllUartDecoders();
            UartOutputTextBox.Clear();
            OnUartConsoleCleared();
            UartStatusText.Text = "UART terminal cleared";
        }

        private void UartFormat_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (UartOutputTextBox == null)
                return;
            if (UartPortCombo != null && UartPortCombo.SelectedIndex >= 0)
            {
                string mode = SelectedUartDisplayMode();
                string genericPort = SelectedGenericComPort();
                if (genericPort == null)
                {
                    uint port = SelectedUartPort();
                    if (mode == "Nmea" && port != 3)
                    {
                        SelectComboTag(UartFormatCombo, port <= 1 ?
                            "Ublox" : "Auto");
                        return;
                    }
                    if (mode == "Ublox" && port > 1)
                    {
                        SelectComboTag(UartFormatCombo, port == 3 ?
                            "Nmea" : "Auto");
                        return;
                    }
                }
            }
            RenderUartConsole();
            if (UartStatusText != null && uartConsoleEntries.Count != 0)
                UartStatusText.Text = "UART display changed to " + SelectedUartDisplayName();
        }

        private async void UartPort_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (refreshingGenericComPorts)
                return;
            StopUartMonitor();
            if (UartStatusText == null || UartPortCombo.SelectedIndex < 0)
                return;
            string genericPort = SelectedGenericComPort();
            if (genericPort != null)
            {
                SetGenericSerialControlsEnabled(true);
                UartStatusText.Text = genericPort + " selected · Windows serial port";
                if (NmeaConfigPanel != null)
                    NmeaConfigPanel.Visibility = Visibility.Collapsed;
                if (UbloxDecodePanel != null)
                    UbloxDecodePanel.Visibility = Visibility.Visible;
                UpdateGenericUartDecoderStatus(genericPort);
                return;
            }
            SetGenericSerialControlsEnabled(false);
            uint port = SelectedUartPort();
            UartStatusText.Text = "UART " + port + " selected";
            if (NmeaConfigPanel != null)
                NmeaConfigPanel.Visibility = port == 3 ? Visibility.Visible : Visibility.Collapsed;
            if (UbloxDecodePanel != null)
                UbloxDecodePanel.Visibility = port <= 1 ? Visibility.Visible : Visibility.Collapsed;
            if (port <= 1 && SelectedUartDisplayMode() == "Nmea")
                SelectComboTag(UartFormatCombo, "Ublox");
            else if (port == 2 &&
                     (SelectedUartDisplayMode() == "Ublox" ||
                      SelectedUartDisplayMode() == "Nmea"))
                SelectComboTag(UartFormatCombo, "Auto");
            else if (port == 3)
                SelectComboTag(UartFormatCombo, "Nmea");
            UpdateUartDecoderStatus(port);
            if (port == 3)
                await RefreshNmeaAsync(false);
        }

        private void SetGenericSerialControlsEnabled(bool enabled)
        {
            if (UartParityCombo == null)
                return;
            UartParityCombo.IsEnabled = enabled;
            UartDataBitsCombo.IsEnabled = enabled;
            UartStopBitsCombo.IsEnabled = enabled;
            UartHandshakeCombo.IsEnabled = enabled;
            UartDtrCheckBox.IsEnabled = enabled;
            UartRtsCheckBox.IsEnabled = enabled;
            if (!enabled)
            {
                UartParityCombo.SelectedIndex = 0;
                UartDataBitsCombo.SelectedIndex = 3;
                UartStopBitsCombo.SelectedIndex = 0;
                UartHandshakeCombo.SelectedIndex = 0;
                UartDtrCheckBox.IsChecked = false;
                UartRtsCheckBox.IsChecked = false;
            }
        }

        private async void RefreshNmea_Click(object sender, RoutedEventArgs e)
        {
            await RefreshNmeaAsync(true);
        }

        private async Task RefreshNmeaAsync(bool showError)
        {
            if (lastSnapshot != null &&
                lastSnapshot.Layout == "Orolia ART")
            {
                NmeaStatusText.Text = "NOT IMPLEMENTED ON ART";
                NmeaStatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                NmeaApplyButton.IsEnabled = false;
                NmeaRegisterText.Text =
                    "The ART FPGA profile has no ToD/NMEA sentence generator or UART 3.";
                return;
            }
            if (client == null || lastSnapshot == null || lastSnapshot.AbiVersion < 4)
            {
                NmeaStatusText.Text = "DRIVER 1.9 / ABI 4 REQUIRED";
                NmeaStatusText.Foreground = (Brush)FindResource("GoldBrush");
                NmeaApplyButton.IsEnabled = false;
                return;
            }

            try
            {
                NmeaOutputState state = await Task.Run(() => client.GetNmeaOutput());
                ApplyNmeaState(state);
            }
            catch (Exception ex)
            {
                NmeaStatusText.Text = "NMEA GENERATOR UNAVAILABLE";
                NmeaStatusText.Foreground = (Brush)FindResource("DangerBrush");
                NmeaApplyButton.IsEnabled = false;
                Log("NMEA query failed: " + ex.Message);
                if (showError)
                    MessageBox.Show(this, ex.Message, "NMEA output unavailable",
                        MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void ApplyNmea_Click(object sender, RoutedEventArgs e)
        {
            if (!EnsureConnected())
                return;
            if (lastSnapshot != null &&
                lastSnapshot.Layout == "Orolia ART")
            {
                MessageBox.Show(this,
                    "The Orolia ART FPGA profile does not implement the Time Card NMEA generator.",
                    "NMEA not implemented", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }
            try
            {
                uint baud = ParseUnsigned(SelectedComboText(NmeaBaudCombo),
                    "NMEA baud rate", 1200, 2000000);
                bool enabled = NmeaEnabledCheckBox.IsChecked == true;
                bool inverted = NmeaPolarityCheckBox.IsChecked == true;
                bool advanced = lastSnapshot != null &&
                    lastSnapshot.AbiVersion >= 13;
                int correctionSeconds = 0;
                int localOffsetMinutes = 0;
                uint gnss = 0;
                uint messageDisableMask = 0;
                if (advanced)
                {
                    correctionSeconds = (int)ParseSigned(
                        NmeaCorrectionTextBox.Text, "NMEA UTC correction",
                        -2147483647L, 2147483647L);
                    localOffsetMinutes = (int)ParseSigned(
                        NmeaLocalOffsetTextBox.Text, "NMEA local offset",
                        -839, 839);
                    gnss = (uint)SelectedComboTag(NmeaGnssCombo,
                        "NMEA talker");
                    if (NmeaDisableRmcCheckBox.IsChecked == true)
                        messageDisableMask |= 1u;
                    if (NmeaDisableZdaCheckBox.IsChecked == true)
                        messageDisableMask |= 2u;
                    if (NmeaDisableUtcCheckBox.IsChecked == true)
                        messageDisableMask |= 4u;
                }
                NmeaApplyButton.IsEnabled = false;
                bool monitorWasRunning = uartMonitorCancellation != null;
                StopUartMonitor();
                for (int attempt = 0; monitorWasRunning &&
                     uartMonitorCancellation != null && attempt < 20; attempt++)
                    await Task.Delay(50);
                NmeaOutputState state = await Task.Run(() => advanced ?
                    client.SetNmeaOutput(enabled, baud, inverted,
                        correctionSeconds, localOffsetMinutes, gnss,
                        messageDisableMask) :
                    client.SetNmeaOutput(enabled, baud, inverted));
                ApplyNmeaState(state);
                UartPortCombo.SelectedIndex = 3;
                SelectComboTag(UartFormatCombo, "Nmea");
                ResetUartDecoder(3);
                UartBaudCombo.Text = state.Baud.ToString(CultureInfo.InvariantCulture);
                Log(string.Format(CultureInfo.InvariantCulture,
                    "NMEA generator {0} at {1} baud{2}; correction {3} s, " +
                    "local {4} min, GNSS {5}, disable mask 0x{6:X2}.",
                    state.IsEnabled ? "enabled" : "disabled", state.Baud,
                    state.IsInverted ? " with inverted polarity" : string.Empty,
                    state.CorrectionSeconds, state.LocalOffsetMinutes,
                    state.Gnss, state.MessageDisableMask));
                if (state.IsEnabled && uartMonitorCancellation == null)
                    MonitorUart_Click(MonitorButton, new RoutedEventArgs());
            }
            catch (Exception ex)
            {
                Log("NMEA configuration failed: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Unable to configure NMEA output",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                NmeaApplyButton.IsEnabled = client != null &&
                    lastSnapshot != null && lastSnapshot.AbiVersion >= 4 &&
                    lastSnapshot.Layout != "Orolia ART";
            }
        }

        private void ApplyNmeaState(NmeaOutputState state)
        {
            Brush stateBrush = (Brush)FindResource(
                state.HasError ? "DangerBrush" :
                (state.IsEnabled ? "AccentBrush" : "GoldBrush"));
            NmeaStatusText.Text = state.IsPresent ?
                (state.HasError ? "TRANSMITTER ERROR · STICKY STATUS SET" :
                 (state.IsEnabled ? "ENABLED · READY TO MONITOR" : "DISABLED")) :
                "NOT PRESENT";
            NmeaStatusText.Foreground = stateBrush;
            NmeaStatusText.ToolTip = state.HasError ?
                "The ToD Master reports a sticky transmit error. Clear it " +
                "explicitly with timecardctl nmea-set ... clear after " +
                "correcting the output configuration." : null;
            NmeaEnabledCheckBox.IsChecked = state.IsEnabled;
            NmeaPolarityCheckBox.IsChecked = state.IsInverted;
            NmeaPolarityCheckBox.IsEnabled = state.SupportsPolarity;
            SelectComboText(NmeaBaudCombo,
                state.Baud.ToString(CultureInfo.InvariantCulture));
            UartBaudCombo.Text = state.Baud.ToString(CultureInfo.InvariantCulture);
            bool advanced = state.HasAdvancedConfiguration &&
                lastSnapshot != null && lastSnapshot.AbiVersion >= 13;
            NmeaAdvancedPanel.IsEnabled = advanced;
            NmeaCorrectionTextBox.Text = state.CorrectionSeconds.ToString(
                CultureInfo.InvariantCulture);
            NmeaLocalOffsetTextBox.Text = state.LocalOffsetMinutes.ToString(
                CultureInfo.InvariantCulture);
            SelectComboTag(NmeaGnssCombo, state.Gnss);
            NmeaGnssCombo.IsEnabled = advanced && state.SupportsGnss;
            NmeaDisableRmcCheckBox.IsChecked =
                (state.MessageDisableMask & 1u) != 0;
            NmeaDisableZdaCheckBox.IsChecked =
                (state.MessageDisableMask & 2u) != 0;
            NmeaDisableUtcCheckBox.IsChecked =
                (state.MessageDisableMask & 4u) != 0;
            NmeaDisableZdaCheckBox.IsEnabled = advanced;
            NmeaDisableRmcCheckBox.IsEnabled = advanced && state.SupportsRmc;
            NmeaDisableUtcCheckBox.IsEnabled = advanced && state.SupportsUtc;
            NmeaAdvancedHintText.Text = advanced ? string.Format(
                CultureInfo.InvariantCulture,
                "Core {0}.{1}: GNSS {2}, RMC {3}, proprietary UTC {4}. " +
                "All writes are disabled, applied, restored and verified.",
                state.Version >> 24, (state.Version >> 16) & 0xffu,
                state.SupportsGnss ? "available" : "fixed to GPS",
                state.SupportsRmc ? "available" : "not implemented",
                state.SupportsUtc ? "available" : "not implemented") :
                "Install driver 1.40 / ABI 13 for correction, local-zone, " +
                "GNSS talker and sentence-gate controls.";
            NmeaRegisterText.Text = string.Format(CultureInfo.InvariantCulture,
                "Control 0x{0:X8} · Status 0x{1:X8} · Version 0x{2:X8} · " +
                "UTC correction {3} s · local {4} min{5}",
                state.Control, state.Status, state.Version,
                state.CorrectionSeconds, state.LocalOffsetMinutes,
                state.HasError ? " · sticky TX error" : string.Empty);
            NmeaApplyButton.IsEnabled = state.IsPresent;
        }

        private void OpenUart_Click(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            int port;
            if (button != null && int.TryParse(Convert.ToString(button.Tag, CultureInfo.InvariantCulture), out port))
                UartPortCombo.SelectedIndex = port;
            UartNav.IsChecked = true;
        }

        private void OpenAtomic_Click(object sender, RoutedEventArgs e)
        {
            AtomicNav.IsChecked = true;
        }

        private async void RefreshUblox_Click(object sender, RoutedEventArgs e)
        {
            await RefreshUbloxAsync(true);
        }

        private async Task RefreshUbloxAsync(bool showError)
        {
            if (ubloxRefreshing)
                return;
            if (client == null)
            {
                UbloxConnectionText.Text = "DRIVER ACCESS REQUIRED";
                UbloxConnectionText.Foreground = (Brush)FindResource("GoldBrush");
                if (showError)
                    EnsureConnected();
                return;
            }

            ubloxRefreshing = true;
            StopUartMonitor();
            SetUbloxBusy(true);
            UbloxConnectionText.Text = "QUERYING UBX";
            UbloxConnectionText.Foreground = (Brush)FindResource("GoldBrush");
            try
            {
                uint port = SelectedUbloxPort();
                uint baud = SelectedUbloxBaud();
                UbloxReceiverSnapshot snapshot = await Task.Run(
                    () => new UbloxClient(client, port, baud).Refresh());
                lastUbloxSnapshot = snapshot;
                UpdateHealthExperience();
                lastUbloxPort = port;
                lastUbloxBaud = baud;
                ApplyUbloxSnapshot(snapshot);
                UbloxConnectionText.Text = snapshot.IsPassiveStream ?
                    "LIVE · PASSIVE F9T" : snapshot.ConfigurationSupported ?
                    (snapshot.Warnings.Count == 0 ? "LIVE · UBX CFG" : "LIVE · PARTIAL") :
                    "LIVE · STATUS ONLY";
                UbloxConnectionText.Foreground = (Brush)FindResource(
                    snapshot.ConfigurationSupported || snapshot.IsPassiveStream ?
                    "AccentBrush" : "GoldBrush");
                Log(string.Format(CultureInfo.InvariantCulture,
                    "u-blox UART {0} refreshed at {1} baud with {2} warning(s).",
                    port, baud, snapshot.Warnings.Count));
                foreach (string warning in snapshot.Warnings.Take(4))
                    Log("u-blox: " + warning);
            }
            catch (Exception ex)
            {
                lastUbloxSnapshot = null;
                lastUbloxPort = null;
                lastUbloxBaud = null;
                RenderUbloxSkyMap(null);
                SetUbloxConfigurationEnabled(false);
                UbloxConnectionText.Text = "RECEIVER UNAVAILABLE";
                UbloxConnectionText.Foreground = (Brush)FindResource("DangerBrush");
                Log("u-blox refresh failed: " + ex.Message);
                if (showError)
                    MessageBox.Show(this, ex.Message +
                        "\r\n\r\nVerify the selected receiver and host baud. The recommended RCB-F9T normally uses UART 0 at 115,200 baud on the Time Card.",
                        "u-blox receiver unavailable", MessageBoxButton.OK,
                        MessageBoxImage.Error);
            }
            finally
            {
                SetUbloxBusy(false);
                ubloxRefreshing = false;
            }
        }

        private void UbloxConnection_Changed(object sender, SelectionChangedEventArgs e)
        {
            if (!IsInitialized || UbloxConnectionText == null || ubloxRefreshing)
                return;
            lastUbloxSnapshot = null;
            lastUbloxPort = null;
            lastUbloxBaud = null;
            RenderUbloxSkyMap(null);
            SetUbloxConfigurationEnabled(false);
            UbloxConnectionText.Text = "REFRESH REQUIRED";
            UbloxConnectionText.Foreground = (Brush)FindResource("GoldBrush");
        }

        private void ApplyUbloxSnapshot(UbloxReceiverSnapshot snapshot)
        {
            UbloxModuleText.Text = string.IsNullOrWhiteSpace(snapshot.Module) ?
                "u-blox receiver" : snapshot.Module;
            UbloxFirmwareText.Text = string.Format(CultureInfo.InvariantCulture,
                "{0} · protocol {1}", snapshot.Firmware, snapshot.ProtocolVersion);

            UbloxFixText.Text = UbloxFixName(snapshot.FixType);
            UbloxFixText.Foreground = snapshot.FixValid ?
                (Brush)FindResource("AccentBrush") : (Brush)FindResource("GoldBrush");
            UbloxFixDetailText.Text = snapshot.FixValid ?
                (snapshot.DifferentialSolution ? "Valid differential solution" : "Valid navigation solution") :
                "Fix not currently valid";
            UbloxSatelliteText.Text = string.Format(CultureInfo.InvariantCulture,
                "{0} / {1}", snapshot.SatellitesUsed, snapshot.SatellitesVisible);
            UbloxConstellationText.Text = string.IsNullOrWhiteSpace(snapshot.ConstellationSummary) ?
                "Constellations unavailable" : snapshot.ConstellationSummary +
                string.Format(CultureInfo.InvariantCulture, " · C/N₀ {0:N1} dB-Hz", snapshot.AverageCno);
            UbloxUtcText.Text = snapshot.Utc.HasValue ?
                snapshot.Utc.Value.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture) : "—";
            UbloxTimeAccuracyText.Text = snapshot.Utc.HasValue ?
                "Estimated accuracy " + FormatNanoseconds(snapshot.TimeAccuracyNanoseconds) :
                "UTC date/time not resolved";
            UbloxAccuracyText.Text = !snapshot.FixValid &&
                snapshot.HorizontalAccuracyMillimeters == 0 ? "—" :
                (snapshot.HorizontalAccuracyMillimeters < 1000 ?
                    snapshot.HorizontalAccuracyMillimeters.ToString("N0", CultureInfo.InvariantCulture) + " mm" :
                    (snapshot.HorizontalAccuracyMillimeters / 1000.0).ToString("N3", CultureInfo.InvariantCulture) + " m");
            UbloxPositionText.Text = snapshot.FixValid ?
                string.Format(CultureInfo.InvariantCulture, "{0:F7}, {1:F7} · pDOP {2:N2}",
                    snapshot.Latitude, snapshot.Longitude, snapshot.PositionDop) :
                "Position unavailable";
            UbloxSignalSupportText.Text = "Reported support: " +
                (string.IsNullOrWhiteSpace(snapshot.SupportedGnss) ? "not reported" : snapshot.SupportedGnss);
            ApplyUbloxTimingSnapshot(snapshot);
            RenderUbloxSkyMap(snapshot);

            UbloxRatePanel.IsEnabled = snapshot.RateConfigurationSupported;
            UbloxSignalPanel.IsEnabled = snapshot.SignalConfigurationSupported;
            UbloxTimePulsePanel.IsEnabled = snapshot.TimePulseConfigurationSupported;
            UbloxMessagePanel.IsEnabled = snapshot.MessageConfigurationSupported;
            UbloxTimingPanel.IsEnabled = snapshot.TimingConfigurationSupported;
            if (!snapshot.ConfigurationSupported)
                return;

            SetConfigText(UbloxMeasurementTextBox, snapshot, UbloxClient.CfgRateMeas);
            SetConfigText(UbloxNavigationRatioTextBox, snapshot, UbloxClient.CfgRateNav);
            SelectComboTag(UbloxTimeReferenceCombo, snapshot.Config(UbloxClient.CfgRateTimeRef, 1));
            SelectComboTag(UbloxDynamicModelCombo, snapshot.Config(UbloxClient.CfgDynamicModel, 2));

            SetConfigCheck(UbloxGpsCheckBox, snapshot, UbloxClient.CfgSignalGps);
            SetConfigCheck(UbloxSbasCheckBox, snapshot, UbloxClient.CfgSignalSbas);
            SetConfigCheck(UbloxGalileoCheckBox, snapshot, UbloxClient.CfgSignalGalileo);
            SetConfigCheck(UbloxBeiDouCheckBox, snapshot, UbloxClient.CfgSignalBeiDou);
            SetConfigCheck(UbloxQzssCheckBox, snapshot, UbloxClient.CfgSignalQzss);
            SetConfigCheck(UbloxGlonassCheckBox, snapshot, UbloxClient.CfgSignalGlonass);

            SetConfigCheck(UbloxTpEnabledCheckBox, snapshot, UbloxClient.CfgTpEnabled);
            SetConfigCheck(UbloxTpSyncCheckBox, snapshot, UbloxClient.CfgTpSyncGnss);
            SetConfigCheck(UbloxTpUseLockedCheckBox, snapshot, UbloxClient.CfgTpUseLocked);
            SetConfigCheck(UbloxTpRisingCheckBox, snapshot, UbloxClient.CfgTpPolarity);
            SelectComboTag(UbloxTpTimeGridCombo, snapshot.Config(UbloxClient.CfgTpTimeGrid, 1));
            SetConfigText(UbloxTpPeriodTextBox, snapshot, UbloxClient.CfgTpPeriod);
            SetConfigText(UbloxTpLockedPeriodTextBox, snapshot, UbloxClient.CfgTpPeriodLocked);
            SetConfigText(UbloxTpLengthTextBox, snapshot, UbloxClient.CfgTpLength);
            SetConfigText(UbloxTpLockedLengthTextBox, snapshot, UbloxClient.CfgTpLengthLocked);

            SetConfigText(UbloxNavPvtRateTextBox, snapshot, UbloxClient.CfgMsgUbxNavPvtUart1);
            SetConfigText(UbloxGgaRateTextBox, snapshot, UbloxClient.CfgMsgNmeaGgaUart1);
            SetConfigText(UbloxGsaRateTextBox, snapshot, UbloxClient.CfgMsgNmeaGsaUart1);
            SetConfigText(UbloxGsvRateTextBox, snapshot, UbloxClient.CfgMsgNmeaGsvUart1);
            SetConfigText(UbloxRmcRateTextBox, snapshot, UbloxClient.CfgMsgNmeaRmcUart1);
            SetConfigText(UbloxZdaRateTextBox, snapshot, UbloxClient.CfgMsgNmeaZdaUart1);
            SetConfigText(UbloxTimeGpsRateTextBox, snapshot,
                UbloxClient.CfgMsgUbxNavTimeGpsUart1);
            SetConfigText(UbloxTimeUtcRateTextBox, snapshot,
                UbloxClient.CfgMsgUbxNavTimeUtcUart1);
            SetConfigText(UbloxTimeLsRateTextBox, snapshot,
                UbloxClient.CfgMsgUbxNavTimeLsUart1);
            SetConfigText(UbloxTimSvinRateTextBox, snapshot,
                UbloxClient.CfgMsgUbxTimSvinUart1);

            if (snapshot.TimingConfigurationSupported)
            {
                SelectComboTag(UbloxTimingModeCombo,
                    snapshot.Config(UbloxClient.CfgTmodeMode, 0));
                SetConfigText(UbloxSurveyMinimumTextBox, snapshot,
                    UbloxClient.CfgTmodeSurveyInMinimumDuration);
                if (snapshot.HasConfig(UbloxClient.CfgTmodeSurveyInAccuracyLimit))
                    UbloxSurveyLimitTextBox.Text = (snapshot.Config(
                        UbloxClient.CfgTmodeSurveyInAccuracyLimit, 0) / 100.0)
                        .ToString("F1", CultureInfo.InvariantCulture);
                UbloxFixedLatitudeTextBox.Text = (snapshot.ConfigSigned32(
                    UbloxClient.CfgTmodeLatitude, 0) * 1e-7 +
                    snapshot.ConfigSigned8(UbloxClient.CfgTmodeLatitudeHighPrecision, 0) * 1e-9)
                    .ToString("F9", CultureInfo.InvariantCulture);
                UbloxFixedLongitudeTextBox.Text = (snapshot.ConfigSigned32(
                    UbloxClient.CfgTmodeLongitude, 0) * 1e-7 +
                    snapshot.ConfigSigned8(UbloxClient.CfgTmodeLongitudeHighPrecision, 0) * 1e-9)
                    .ToString("F9", CultureInfo.InvariantCulture);
                UbloxFixedHeightTextBox.Text = (snapshot.ConfigSigned32(
                    UbloxClient.CfgTmodeHeight, 0) * 0.01 +
                    snapshot.ConfigSigned8(UbloxClient.CfgTmodeHeightHighPrecision, 0) * 0.0001)
                    .ToString("F4", CultureInfo.InvariantCulture);
                if (snapshot.HasConfig(UbloxClient.CfgTmodeFixedPositionAccuracy))
                    UbloxFixedAccuracyTextBox.Text = (snapshot.Config(
                        UbloxClient.CfgTmodeFixedPositionAccuracy, 0) / 100.0)
                        .ToString("F1", CultureInfo.InvariantCulture);
            }
        }

        private void ApplyUbloxTimingSnapshot(UbloxReceiverSnapshot snapshot)
        {
            Brush healthy = (Brush)FindResource("AccentBrush");
            Brush warning = (Brush)FindResource("GoldBrush");
            UbloxF9StatusText.Text = snapshot.IsF9TimingReceiver ?
                (snapshot.IsPassiveStream ? "F9T · PASSIVE RX" : "F9T DETECTED") :
                "GENERIC U-BLOX";
            UbloxF9StatusText.Foreground = snapshot.IsF9TimingReceiver ? healthy : warning;
            UbloxReceiverFamilyText.Text = string.IsNullOrWhiteSpace(snapshot.ReceiverFamily) ?
                "u-blox receiver" : snapshot.ReceiverFamily;
            UbloxUniqueIdText.Text = string.IsNullOrWhiteSpace(snapshot.UniqueChipId) ?
                "--" : snapshot.UniqueChipId;

            if (snapshot.SurveyInStatusSupported)
            {
                UbloxSurveyStatusText.Text = snapshot.SurveyInActive ? "IN PROGRESS" :
                    snapshot.SurveyInValid ? "VALID" : "INACTIVE";
                UbloxSurveyStatusText.Foreground = snapshot.SurveyInValid ? healthy : warning;
                UbloxSurveyDurationText.Text = string.Format(CultureInfo.InvariantCulture,
                    "{0} / {1} obs", FormatDuration(snapshot.SurveyInDurationSeconds),
                    snapshot.SurveyInObservations);
                UbloxSurveyAccuracyText.Text = snapshot.SurveyInMeanAccuracyMillimeters < 1000.0 ?
                    snapshot.SurveyInMeanAccuracyMillimeters.ToString("F1",
                        CultureInfo.InvariantCulture) + " mm" :
                    (snapshot.SurveyInMeanAccuracyMillimeters / 1000.0).ToString("F3",
                        CultureInfo.InvariantCulture) + " m";
            }
            else
            {
                UbloxSurveyStatusText.Text = "NOT AVAILABLE";
                UbloxSurveyStatusText.Foreground = warning;
                UbloxSurveyDurationText.Text = "--";
                UbloxSurveyAccuracyText.Text = "--";
            }

            UbloxGpsTimeText.Text = snapshot.GpsTimeSupported ?
                string.Format(CultureInfo.InvariantCulture,
                    "WN {0} · {1:F6} s · {2}", snapshot.GpsWeek,
                    snapshot.GpsTimeOfWeekSeconds,
                    snapshot.GpsTimeValid ? "valid" : "not valid") : "Not available";
            UbloxLeapSecondsText.Text = snapshot.LeapSecondStatusSupported ?
                string.Format(CultureInfo.InvariantCulture, "GPS-UTC {0} s · {1}",
                    snapshot.CurrentLeapSeconds,
                    snapshot.CurrentLeapSecondsValid ? "valid" : "not valid") : "Not available";
            UbloxLeapEventText.Text = !snapshot.LeapSecondEventValid ?
                "No valid event schedule" : snapshot.LeapSecondChange == 0 ?
                "No leap change scheduled" : string.Format(CultureInfo.InvariantCulture,
                    "{0:+#;-#;0} s in {1} s · WN {2}/{3}",
                    snapshot.LeapSecondChange, snapshot.SecondsToLeapSecondEvent,
                    snapshot.LeapSecondEventGpsWeek, snapshot.LeapSecondEventGpsDay);

            if (!snapshot.RfStatusSupported || snapshot.RfBlocks.Count == 0)
            {
                UbloxAntennaStatusText.Text = "Not available";
                UbloxRfStatusText.Text = "Not available";
                UbloxRfDetailText.Text = "The receiver did not return UBX-MON-RF data.";
                return;
            }
            UbloxRfBlockInfo first = snapshot.RfBlocks[0];
            byte worstJamming = snapshot.RfBlocks.Max(block => block.JammingState);
            byte highestIndicator = snapshot.RfBlocks.Max(block => block.JammingIndicator);
            UbloxAntennaStatusText.Text = UbloxAntennaName(first.AntennaStatus) +
                " · power " + UbloxAntennaPowerName(first.AntennaPower);
            UbloxAntennaStatusText.Foreground = first.AntennaStatus == 2 ? healthy : warning;
            UbloxRfStatusText.Text = UbloxJammingName(worstJamming) +
                string.Format(CultureInfo.InvariantCulture, " · indicator {0}/255",
                    highestIndicator);
            UbloxRfStatusText.Foreground = worstJamming <= 1 ? healthy : warning;
            UbloxRfDetailText.Text = string.Join(" · ", snapshot.RfBlocks.Select(block =>
                string.Format(CultureInfo.InvariantCulture,
                    "RF{0}: AGC {1}, noise {2}, jam {3}", block.BlockIdentifier,
                    block.AutomaticGainControl, block.NoisePerMillisecond,
                    block.JammingIndicator)));
        }

        private void UbloxSkyMap_SizeChanged(object sender, SizeChangedEventArgs e)
        {
            if (UbloxSkyMapCanvas == null || UbloxSatelliteListPanel == null)
                return;
            RenderUbloxSkyMap(lastUbloxSnapshot);
        }

        private void RenderUbloxSkyMap(UbloxReceiverSnapshot snapshot)
        {
            if (UbloxSkyMapCanvas == null || UbloxSatelliteListPanel == null)
                return;

            Canvas canvas = UbloxSkyMapCanvas;
            canvas.Children.Clear();
            UbloxSatelliteListPanel.Children.Clear();
            double width = canvas.ActualWidth;
            double height = canvas.ActualHeight;
            if (width < 120 || height < 120)
                return;

            double radius = Math.Max(40, Math.Min(width - 54, height - 46) / 2.0);
            double centerX = width / 2.0;
            double centerY = height / 2.0 + 3;
            SolidColorBrush gridBrush = new SolidColorBrush(Color.FromRgb(43, 76, 101));
            SolidColorBrush axisBrush = new SolidColorBrush(Color.FromRgb(31, 58, 79));
            SolidColorBrush mutedBrush = new SolidColorBrush(Color.FromRgb(143, 166, 187));
            gridBrush.Freeze();
            axisBrush.Freeze();
            mutedBrush.Freeze();

            RadialGradientBrush skyFill = new RadialGradientBrush();
            skyFill.GradientStops.Add(new GradientStop(Color.FromRgb(18, 43, 61), 0));
            skyFill.GradientStops.Add(new GradientStop(Color.FromRgb(7, 17, 31), 1));
            AddSkyMapCircle(canvas, centerX, centerY, radius, skyFill,
                gridBrush, 1.4);
            AddSkyMapLine(canvas, centerX - radius, centerY,
                centerX + radius, centerY, axisBrush, 1);
            AddSkyMapLine(canvas, centerX, centerY - radius,
                centerX, centerY + radius, axisBrush, 1);
            AddSkyMapCircle(canvas, centerX, centerY, radius * 2.0 / 3.0,
                Brushes.Transparent, gridBrush, 1);
            AddSkyMapCircle(canvas, centerX, centerY, radius / 3.0,
                Brushes.Transparent, gridBrush, 1);

            AddSkyMapText(canvas, "N", centerX - 6, centerY - radius - 24,
                Brushes.White, 12, FontWeights.Bold);
            AddSkyMapText(canvas, "E", centerX + radius + 9, centerY - 8,
                mutedBrush, 11, FontWeights.SemiBold);
            AddSkyMapText(canvas, "S", centerX - 5, centerY + radius + 7,
                mutedBrush, 11, FontWeights.SemiBold);
            AddSkyMapText(canvas, "W", centerX - radius - 23, centerY - 8,
                mutedBrush, 11, FontWeights.SemiBold);
            AddSkyMapText(canvas, "60°", centerX + 7,
                centerY - radius / 3.0 - 13, mutedBrush, 9, FontWeights.Normal);
            AddSkyMapText(canvas, "30°", centerX + 7,
                centerY - radius * 2.0 / 3.0 - 13, mutedBrush, 9,
                FontWeights.Normal);
            AddSkyMapText(canvas, "ZENITH", centerX + 7, centerY + 4,
                mutedBrush, 8, FontWeights.Normal);

            IList<UbloxSatelliteInfo> satellites = snapshot == null ?
                new List<UbloxSatelliteInfo>().AsReadOnly() : snapshot.Satellites;
            List<UbloxSatelliteInfo> plotted = satellites.Where(satellite =>
                satellite.ElevationDegrees >= 0 && satellite.ElevationDegrees <= 90 &&
                satellite.AzimuthDegrees >= 0).ToList();

            foreach (UbloxSatelliteInfo satellite in plotted
                .OrderBy(item => item.IsUsed ? 1 : 0)
                .ThenBy(item => item.CarrierToNoise))
            {
                int azimuth = ((satellite.AzimuthDegrees % 360) + 360) % 360;
                double angle = azimuth * Math.PI / 180.0;
                double distance = radius *
                    (90.0 - satellite.ElevationDegrees) / 90.0;
                double markerSize = 18.0 +
                    Math.Min(satellite.CarrierToNoise, (byte)55) / 55.0 * 12.0;
                double x = centerX + distance * Math.Sin(angle) - markerSize / 2.0;
                double y = centerY - distance * Math.Cos(angle) - markerSize / 2.0;
                Brush color = SkyMapConstellationBrush(satellite.GnssIdentifier);
                Grid marker = new Grid
                {
                    Width = markerSize,
                    Height = markerSize,
                    Opacity = satellite.CarrierToNoise == 0 ? 0.4 :
                        (satellite.IsUsed ? 1.0 : 0.78),
                    ToolTip = string.Format(CultureInfo.InvariantCulture,
                        "{0} · {1}\nElevation {2}° · azimuth {3}°\nC/N₀ {4} dB-Hz · {5} · {6}",
                        satellite.DisplayIdentifier, satellite.Constellation,
                        satellite.ElevationDegrees, azimuth,
                        satellite.CarrierToNoise,
                        satellite.IsUsed ? "used in solution" : "tracked",
                        satellite.QualityDescription)
                };
                marker.Children.Add(new System.Windows.Shapes.Ellipse
                {
                    Fill = satellite.CarrierToNoise == 0 ? Brushes.DimGray : color,
                    Stroke = satellite.IsUsed ? Brushes.White : color,
                    StrokeThickness = satellite.IsUsed ? 2.4 : 1.1
                });
                marker.Children.Add(new TextBlock
                {
                    Text = satellite.DisplayIdentifier,
                    Foreground = Brushes.White,
                    FontFamily = new FontFamily("Segoe UI Semibold"),
                    FontSize = satellite.DisplayIdentifier.Length > 3 ? 8 : 9,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                });
                Panel.SetZIndex(marker, satellite.IsUsed ? 2 : 1);
                Canvas.SetLeft(marker, x);
                Canvas.SetTop(marker, y);
                canvas.Children.Add(marker);
            }

            if (plotted.Count == 0)
                AddSkyMapText(canvas, snapshot == null ? "REFRESH RECEIVER" :
                    "NO SATELLITES ABOVE HORIZON", centerX - 79, centerY - 8,
                    mutedBrush, 10, FontWeights.SemiBold);

            PopulateUbloxSatelliteList(satellites);
            if (snapshot == null)
            {
                UbloxSkyMapStatusText.Text = "AWAITING UBX-NAV-SAT";
                UbloxSkyMapStatusText.Foreground = (Brush)FindResource("GoldBrush");
                UbloxSkyMapSummaryText.Text =
                    "Refresh the selected receiver to load satellite geometry.";
            }
            else
            {
                int used = satellites.Count(item => item.IsUsed);
                int strong = satellites.Count(item => item.CarrierToNoise >= 35);
                UbloxSkyMapStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                    "{0:N0} PLOTTED · {1:HH:mm:ss} UTC", plotted.Count,
                    snapshot.CapturedAtUtc);
                UbloxSkyMapStatusText.Foreground = (Brush)FindResource(
                    satellites.Count == 0 ? "GoldBrush" : "AccentBrush");
                UbloxSkyMapSummaryText.Text = string.Format(
                    CultureInfo.InvariantCulture,
                    "{0:N0} used · {1:N0} reported · {2:N0} at or above 35 dB-Hz. Hover a marker for receiver details.",
                    used, satellites.Count, strong);
            }
        }

        private void PopulateUbloxSatelliteList(IList<UbloxSatelliteInfo> satellites)
        {
            if (satellites.Count == 0)
            {
                UbloxSatelliteListPanel.Children.Add(new TextBlock
                {
                    Text = "No satellite records were returned.",
                    Foreground = (Brush)FindResource("MutedBrush"),
                    Margin = new Thickness(0, 8, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });
                return;
            }

            int rowIndex = 0;
            foreach (UbloxSatelliteInfo satellite in satellites
                .OrderByDescending(item => item.IsUsed)
                .ThenByDescending(item => item.CarrierToNoise)
                .ThenBy(item => item.GnssIdentifier)
                .ThenBy(item => item.SatelliteIdentifier))
            {
                Brush color = SkyMapConstellationBrush(satellite.GnssIdentifier);
                Grid row = new Grid();
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(54) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(90) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(58) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

                StackPanel identifier = new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    VerticalAlignment = VerticalAlignment.Center
                };
                identifier.Children.Add(new System.Windows.Shapes.Ellipse
                {
                    Width = 8,
                    Height = 8,
                    Fill = color,
                    Stroke = satellite.IsUsed ? Brushes.White : color,
                    StrokeThickness = satellite.IsUsed ? 1.4 : 0,
                    Margin = new Thickness(0, 0, 6, 0)
                });
                identifier.Children.Add(new TextBlock
                {
                    Text = satellite.DisplayIdentifier,
                    FontFamily = new FontFamily("Segoe UI Semibold"),
                    FontSize = 11
                });
                row.Children.Add(identifier);

                TextBlock geometry = new TextBlock
                {
                    Text = string.Format(CultureInfo.InvariantCulture,
                        "{0}° / {1}°", satellite.ElevationDegrees,
                        satellite.AzimuthDegrees),
                    Foreground = (Brush)FindResource("MutedBrush"),
                    FontFamily = new FontFamily("Cascadia Mono, Consolas"),
                    FontSize = 10,
                    VerticalAlignment = VerticalAlignment.Center
                };
                Grid.SetColumn(geometry, 1);
                row.Children.Add(geometry);

                TextBlock signal = new TextBlock
                {
                    Text = satellite.CarrierToNoise.ToString(
                        CultureInfo.InvariantCulture) + " dB",
                    Foreground = satellite.CarrierToNoise >= 35 ? color :
                        (Brush)FindResource("MutedBrush"),
                    FontSize = 10,
                    VerticalAlignment = VerticalAlignment.Center
                };
                Grid.SetColumn(signal, 2);
                row.Children.Add(signal);

                ProgressBar bar = new ProgressBar
                {
                    Minimum = 0,
                    Maximum = 60,
                    Value = Math.Min(60, (int)satellite.CarrierToNoise),
                    Height = 6,
                    Foreground = color,
                    Background = new SolidColorBrush(Color.FromRgb(21, 40, 58)),
                    BorderThickness = new Thickness(0),
                    VerticalAlignment = VerticalAlignment.Center,
                    ToolTip = satellite.QualityDescription
                };
                Grid.SetColumn(bar, 3);
                row.Children.Add(bar);

                Border rowBorder = new Border
                {
                    Background = new SolidColorBrush(rowIndex++ % 2 == 0 ?
                        Color.FromRgb(10, 23, 37) : Color.FromRgb(12, 28, 43)),
                    CornerRadius = new CornerRadius(6),
                    Padding = new Thickness(8, 6, 8, 6),
                    Margin = new Thickness(0, 0, 0, 3),
                    Child = row,
                    ToolTip = satellite.Constellation + " · " +
                        (satellite.IsUsed ? "used in solution" : "tracked")
                };
                UbloxSatelliteListPanel.Children.Add(rowBorder);
            }
        }

        private static void AddSkyMapCircle(Canvas canvas, double centerX,
            double centerY, double radius, Brush fill, Brush stroke,
            double strokeThickness)
        {
            System.Windows.Shapes.Ellipse circle =
                new System.Windows.Shapes.Ellipse
                {
                    Width = radius * 2,
                    Height = radius * 2,
                    Fill = fill,
                    Stroke = stroke,
                    StrokeThickness = strokeThickness,
                    IsHitTestVisible = false
                };
            Canvas.SetLeft(circle, centerX - radius);
            Canvas.SetTop(circle, centerY - radius);
            canvas.Children.Add(circle);
        }

        private static void AddSkyMapLine(Canvas canvas, double x1, double y1,
            double x2, double y2, Brush stroke, double strokeThickness)
        {
            canvas.Children.Add(new System.Windows.Shapes.Line
            {
                X1 = x1,
                Y1 = y1,
                X2 = x2,
                Y2 = y2,
                Stroke = stroke,
                StrokeThickness = strokeThickness,
                IsHitTestVisible = false
            });
        }

        private static void AddSkyMapText(Canvas canvas, string text,
            double left, double top, Brush foreground, double fontSize,
            FontWeight weight)
        {
            TextBlock label = new TextBlock
            {
                Text = text,
                Foreground = foreground,
                FontSize = fontSize,
                FontWeight = weight,
                IsHitTestVisible = false
            };
            Canvas.SetLeft(label, left);
            Canvas.SetTop(label, top);
            canvas.Children.Add(label);
        }

        private static Brush SkyMapConstellationBrush(byte identifier)
        {
            Color color;
            switch (identifier)
            {
                case 0: color = Color.FromRgb(105, 196, 65); break;
                case 1: color = Color.FromRgb(80, 227, 194); break;
                case 2: color = Color.FromRgb(76, 201, 240); break;
                case 3: color = Color.FromRgb(218, 181, 57); break;
                case 5: color = Color.FromRgb(203, 136, 255); break;
                case 6: color = Color.FromRgb(255, 102, 122); break;
                case 7: color = Color.FromRgb(255, 158, 100); break;
                default: color = Color.FromRgb(143, 166, 187); break;
            }
            SolidColorBrush brush = new SolidColorBrush(color);
            brush.Freeze();
            return brush;
        }

        private async void ApplyUbloxRate_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                bool? persist = ConfirmUbloxPersistence();
                if (!persist.HasValue)
                    return;
                ushort measurement = (ushort)ParseUnsigned(UbloxMeasurementTextBox.Text,
                    "measurement period", 50, ushort.MaxValue);
                ushort ratio = (ushort)ParseUnsigned(UbloxNavigationRatioTextBox.Text,
                    "navigation ratio", 1, 127);
                byte timeReference = (byte)SelectedComboTag(UbloxTimeReferenceCombo,
                    "measurement time reference");
                byte dynamicModel = (byte)SelectedComboTag(UbloxDynamicModelCombo,
                    "dynamic model");
                if ((long)measurement * ratio < 100 &&
                    MessageBox.Show(this,
                        "This configuration produces more than 10 navigation solutions per second and can overload the serial output. Continue?",
                        "High navigation rate", MessageBoxButton.YesNo,
                        MessageBoxImage.Warning) != MessageBoxResult.Yes)
                    return;
                await RunUbloxOperationAsync("u-blox navigation rate and dynamic model updated",
                    receiver => receiver.ApplyRateAndModel(measurement, ratio,
                        timeReference, dynamicModel, persist.Value));
            }
            catch (Exception ex)
            {
                ShowUbloxError("Navigation configuration was not applied", ex);
            }
        }

        private async void ApplyUbloxSignals_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                bool gps = UbloxGpsCheckBox.IsChecked == true;
                bool sbas = UbloxSbasCheckBox.IsChecked == true;
                bool galileo = UbloxGalileoCheckBox.IsChecked == true;
                bool beiDou = UbloxBeiDouCheckBox.IsChecked == true;
                bool qzss = UbloxQzssCheckBox.IsChecked == true;
                bool glonass = UbloxGlonassCheckBox.IsChecked == true;
                if (!gps && !galileo && !beiDou && !glonass)
                    throw new InvalidOperationException(
                        "Keep at least one primary constellation enabled: GPS, Galileo, BeiDou, or GLONASS.");
                bool? persist = ConfirmUbloxPersistence();
                if (!persist.HasValue)
                    return;
                await RunUbloxOperationAsync("u-blox constellation set updated",
                    receiver => receiver.ApplySignals(gps, sbas, galileo,
                        beiDou, qzss, glonass, persist.Value));
            }
            catch (Exception ex)
            {
                ShowUbloxError("Constellation configuration was not applied", ex);
            }
        }

        private async void ApplyUbloxTimePulse_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                bool enabled = UbloxTpEnabledCheckBox.IsChecked == true;
                bool sync = UbloxTpSyncCheckBox.IsChecked == true;
                bool locked = UbloxTpUseLockedCheckBox.IsChecked == true;
                bool rising = UbloxTpRisingCheckBox.IsChecked == true;
                byte grid = (byte)SelectedComboTag(UbloxTpTimeGridCombo, "time-pulse grid");
                uint period = ParseUnsigned(UbloxTpPeriodTextBox.Text,
                    "time-pulse period", 1, uint.MaxValue);
                uint lockedPeriod = ParseUnsigned(UbloxTpLockedPeriodTextBox.Text,
                    "locked time-pulse period", 1, uint.MaxValue);
                uint length = ParseUnsigned(UbloxTpLengthTextBox.Text,
                    "time-pulse width", 0, uint.MaxValue);
                uint lockedLength = ParseUnsigned(UbloxTpLockedLengthTextBox.Text,
                    "locked time-pulse width", 0, uint.MaxValue);
                if (length > period || lockedLength > lockedPeriod)
                    throw new InvalidOperationException(
                        "Each pulse width must be less than or equal to its corresponding period.");
                if ((!enabled || !rising || period != 1000000 || lockedPeriod != 1000000) &&
                    MessageBox.Show(this,
                        "This differs from the Time Card's normal enabled, rising-edge, 1 PPS timing input and may interrupt synchronization. Continue?",
                        "Nonstandard GNSS time pulse", MessageBoxButton.YesNo,
                        MessageBoxImage.Warning) != MessageBoxResult.Yes)
                    return;
                bool? persist = ConfirmUbloxPersistence();
                if (!persist.HasValue)
                    return;
                await RunUbloxOperationAsync("u-blox time-pulse configuration updated",
                    receiver => receiver.ApplyTimePulse(enabled, sync, locked,
                        rising, grid, period, lockedPeriod, length, lockedLength,
                        persist.Value));
            }
            catch (Exception ex)
            {
                ShowUbloxError("Time-pulse configuration was not applied", ex);
            }
        }

        private async void ApplyUbloxMessages_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                byte navPvt = (byte)ParseUnsigned(UbloxNavPvtRateTextBox.Text,
                    "UBX NAV-PVT rate", 0, byte.MaxValue);
                byte gga = (byte)ParseUnsigned(UbloxGgaRateTextBox.Text,
                    "NMEA GGA rate", 0, byte.MaxValue);
                byte gsa = (byte)ParseUnsigned(UbloxGsaRateTextBox.Text,
                    "NMEA GSA rate", 0, byte.MaxValue);
                byte gsv = (byte)ParseUnsigned(UbloxGsvRateTextBox.Text,
                    "NMEA GSV rate", 0, byte.MaxValue);
                byte rmc = (byte)ParseUnsigned(UbloxRmcRateTextBox.Text,
                    "NMEA RMC rate", 0, byte.MaxValue);
                byte zda = (byte)ParseUnsigned(UbloxZdaRateTextBox.Text,
                    "NMEA ZDA rate", 0, byte.MaxValue);
                byte timeGps = (byte)ParseUnsigned(UbloxTimeGpsRateTextBox.Text,
                    "UBX NAV-TIMEGPS rate", 0, byte.MaxValue);
                byte timeUtc = (byte)ParseUnsigned(UbloxTimeUtcRateTextBox.Text,
                    "UBX NAV-TIMEUTC rate", 0, byte.MaxValue);
                byte timeLs = (byte)ParseUnsigned(UbloxTimeLsRateTextBox.Text,
                    "UBX NAV-TIMELS rate", 0, byte.MaxValue);
                byte surveyIn = (byte)ParseUnsigned(UbloxTimSvinRateTextBox.Text,
                    "UBX TIM-SVIN rate", 0, byte.MaxValue);
                bool? persist = ConfirmUbloxPersistence();
                if (!persist.HasValue)
                    return;
                await RunUbloxOperationAsync("u-blox UART 1 message rates updated",
                    receiver => receiver.ApplyMessageRates(navPvt, gga, gsa,
                        gsv, rmc, zda, timeGps, timeUtc, timeLs, surveyIn,
                        persist.Value));
            }
            catch (Exception ex)
            {
                ShowUbloxError("Message-rate configuration was not applied", ex);
            }
        }

        private async void ApplyUbloxTimingMode_Click(object sender,
            RoutedEventArgs e)
        {
            try
            {
                if (lastUbloxSnapshot == null ||
                    !lastUbloxSnapshot.IsF9TimingReceiver ||
                    !lastUbloxSnapshot.TimingConfigurationSupported)
                    throw new InvalidOperationException(
                        "Refresh a ZED-F9T or RCB-F9T receiver with timing-configuration support first.");
                byte mode = (byte)SelectedComboTag(UbloxTimingModeCombo,
                    "F9T timing mode");
                uint surveyMinimum = ParseUnsigned(UbloxSurveyMinimumTextBox.Text,
                    "survey-in minimum duration", 1, uint.MaxValue);
                double surveyLimitCm = ParseInvariantDouble(
                    UbloxSurveyLimitTextBox.Text, "survey-in accuracy", 0.0001,
                    42949672.95);
                double latitude = ParseInvariantDouble(UbloxFixedLatitudeTextBox.Text,
                    "fixed latitude", -90.0, 90.0);
                double longitude = ParseInvariantDouble(UbloxFixedLongitudeTextBox.Text,
                    "fixed longitude", -180.0, 180.0);
                double height = ParseInvariantDouble(UbloxFixedHeightTextBox.Text,
                    "fixed height", -1000000.0, 1000000.0);
                double fixedAccuracyCm = ParseInvariantDouble(
                    UbloxFixedAccuracyTextBox.Text, "fixed-position accuracy",
                    0.0001, 42949672.95);
                uint surveyTenthsMillimeter = checked((uint)Math.Round(
                    surveyLimitCm * 100.0, MidpointRounding.AwayFromZero));
                uint fixedTenthsMillimeter = checked((uint)Math.Round(
                    fixedAccuracyCm * 100.0, MidpointRounding.AwayFromZero));

                string warning = mode == 2 ?
                    "Fixed timing mode will use the entered antenna-reference position. Any coordinate error directly becomes timing error. Apply this position?" :
                    mode == 1 ?
                    "Starting survey-in replaces the current timing-mode solution until both duration and accuracy limits are met. Continue?" :
                    "Disabling F9T timing mode can interrupt the Time Card's timing solution. Continue?";
                if (MessageBox.Show(this, warning, "Apply F9T timing mode",
                    MessageBoxButton.YesNo, MessageBoxImage.Warning) !=
                    MessageBoxResult.Yes)
                    return;
                bool? persist = ConfirmUbloxPersistence();
                if (!persist.HasValue)
                    return;
                await RunUbloxOperationAsync("F9T timing mode updated", receiver =>
                    receiver.ApplyTimingMode(mode, surveyMinimum,
                        surveyTenthsMillimeter, latitude, longitude, height,
                        fixedTenthsMillimeter, persist.Value));
            }
            catch (Exception ex)
            {
                ShowUbloxError("F9T timing mode was not applied", ex);
            }
        }

        private async Task RunUbloxOperationAsync(string description,
            Action<UbloxClient> operation)
        {
            if (!EnsureConnected())
                return;
            if (ubloxRefreshing)
                throw new InvalidOperationException("Wait for the current u-blox operation to finish.");
            if (lastUbloxSnapshot == null || !lastUbloxSnapshot.ConfigurationSupported)
                throw new InvalidOperationException(
                    "Refresh the receiver and confirm UBX configuration-database support first.");

            uint port = SelectedUbloxPort();
            uint baud = SelectedUbloxBaud();
            if (!lastUbloxPort.HasValue || !lastUbloxBaud.HasValue ||
                lastUbloxPort.Value != port || lastUbloxBaud.Value != baud)
                throw new InvalidOperationException(
                    "The selected receiver or host baud changed. Refresh it before applying settings.");

            ubloxRefreshing = true;
            StopUartMonitor();
            SetUbloxBusy(true);
            UbloxConnectionText.Text = "APPLYING";
            UbloxConnectionText.Foreground = (Brush)FindResource("GoldBrush");
            try
            {
                UbloxReceiverSnapshot snapshot = await Task.Run(() =>
                {
                    UbloxClient receiver = new UbloxClient(client, port, baud);
                    operation(receiver);
                    return receiver.Refresh();
                });
                lastUbloxSnapshot = snapshot;
                lastUbloxPort = port;
                lastUbloxBaud = baud;
                ApplyUbloxSnapshot(snapshot);
                UbloxConnectionText.Text = snapshot.Warnings.Count == 0 ?
                    "LIVE · UBX CFG" : "LIVE · PARTIAL";
                UbloxConnectionText.Foreground = (Brush)FindResource(
                    snapshot.Warnings.Count == 0 ? "AccentBrush" : "GoldBrush");
                Log(description + ".");
            }
            finally
            {
                SetUbloxBusy(false);
                ubloxRefreshing = false;
            }
        }

        private bool? ConfirmUbloxPersistence()
        {
            if (UbloxPersistCheckBox.IsChecked != true)
                return false;
            return MessageBox.Show(this,
                "Persist these settings to the u-blox battery-backed RAM and flash layers? Leave this option clear while experimenting to apply changes only to RAM.",
                "Persist u-blox configuration", MessageBoxButton.YesNo,
                MessageBoxImage.Warning) == MessageBoxResult.Yes ? (bool?)true : null;
        }

        private uint SelectedUbloxPort()
        {
            return (uint)SelectedComboTag(UbloxReceiverCombo, "u-blox receiver");
        }

        private uint SelectedUbloxBaud()
        {
            return ParseUnsigned(UbloxBaudCombo.Text, "u-blox host baud", 1, uint.MaxValue);
        }

        private static ulong SelectedComboTag(ComboBox combo, string name)
        {
            ComboBoxItem item = combo.SelectedItem as ComboBoxItem;
            if (item == null)
                throw new InvalidOperationException("Select a valid " + name + ".");
            return ulong.Parse(Convert.ToString(item.Tag, CultureInfo.InvariantCulture),
                CultureInfo.InvariantCulture);
        }

        private static void SelectComboTag(ComboBox combo, ulong value)
        {
            foreach (ComboBoxItem item in combo.Items.OfType<ComboBoxItem>())
            {
                ulong candidate;
                if (ulong.TryParse(Convert.ToString(item.Tag, CultureInfo.InvariantCulture),
                    NumberStyles.Integer, CultureInfo.InvariantCulture, out candidate) &&
                    candidate == value)
                {
                    combo.SelectedItem = item;
                    return;
                }
            }
        }

        private static void SelectComboTag(ComboBox combo, string value)
        {
            foreach (ComboBoxItem item in combo.Items.OfType<ComboBoxItem>())
            {
                if (string.Equals(Convert.ToString(item.Tag,
                        CultureInfo.InvariantCulture), value,
                        StringComparison.OrdinalIgnoreCase))
                {
                    combo.SelectedItem = item;
                    return;
                }
            }
        }

        private static string SelectedComboText(ComboBox combo)
        {
            ComboBoxItem item = combo.SelectedItem as ComboBoxItem;
            return item == null ? combo.Text : Convert.ToString(
                item.Content, CultureInfo.InvariantCulture);
        }

        private static void SelectComboText(ComboBox combo, string value)
        {
            foreach (ComboBoxItem item in combo.Items.OfType<ComboBoxItem>())
            {
                if (string.Equals(Convert.ToString(item.Content,
                    CultureInfo.InvariantCulture), value,
                    StringComparison.OrdinalIgnoreCase))
                {
                    combo.SelectedItem = item;
                    return;
                }
            }
            combo.Text = value;
        }

        private static string ClockSourceName(uint source)
        {
            switch (source)
            {
            case 0: return "None";
            case 1: return "Time-of-Day / GNSS";
            case 2: return "IRIG-B";
            case 3: return "External PPS";
            case 4: return "PTP";
            case 5: return "RTC";
            case 6: return "DCF77";
            case 7: return "NTP";
            case 8: return "SyncE";
            case 0xfd: return "Dynamic source";
            case 0xfe: return "Register-controlled";
            case 0xff: return "External selector";
            default: return string.Format(CultureInfo.InvariantCulture,
                "Unknown (0x{0:X2})", source);
            }
        }

        private static void SetConfigText(TextBox box,
            UbloxReceiverSnapshot snapshot, uint key)
        {
            if (snapshot.HasConfig(key))
                box.Text = snapshot.Config(key, 0).ToString(CultureInfo.InvariantCulture);
        }

        private static void SetConfigCheck(CheckBox box,
            UbloxReceiverSnapshot snapshot, uint key)
        {
            if (snapshot.HasConfig(key))
                box.IsChecked = snapshot.Config(key, 0) != 0;
        }

        private void SetUbloxConfigurationEnabled(bool enabled)
        {
            UbloxRatePanel.IsEnabled = enabled;
            UbloxSignalPanel.IsEnabled = enabled;
            UbloxTimePulsePanel.IsEnabled = enabled;
            UbloxMessagePanel.IsEnabled = enabled;
            UbloxTimingPanel.IsEnabled = enabled && lastUbloxSnapshot != null &&
                lastUbloxSnapshot.TimingConfigurationSupported;
        }

        private void SetUbloxBusy(bool busy)
        {
            UbloxRefreshButton.IsEnabled = !busy;
            UbloxReceiverCombo.IsEnabled = !busy;
            UbloxBaudCombo.IsEnabled = !busy;
        }

        private void ShowUbloxError(string title, Exception ex)
        {
            UbloxConnectionText.Text = "OPERATION FAILED";
            UbloxConnectionText.Foreground = (Brush)FindResource("DangerBrush");
            Log(title + ": " + ex.Message);
            MessageBox.Show(this, ex.Message, title, MessageBoxButton.OK,
                MessageBoxImage.Error);
        }

        private static string UbloxFixName(byte fixType)
        {
            switch (fixType)
            {
                case 1: return "DEAD RECKONING";
                case 2: return "2D FIX";
                case 3: return "3D FIX";
                case 4: return "GNSS + DR";
                case 5: return "TIME ONLY";
                default: return "NO FIX";
            }
        }

        private static string UbloxAntennaName(byte status)
        {
            switch (status)
            {
                case 0: return "initializing";
                case 1: return "unknown";
                case 2: return "OK";
                case 3: return "short circuit";
                case 4: return "open circuit";
                default: return "status " + status.ToString(CultureInfo.InvariantCulture);
            }
        }

        private static string UbloxAntennaPowerName(byte status)
        {
            switch (status)
            {
                case 0: return "off";
                case 1: return "on";
                default: return "unknown";
            }
        }

        private static string UbloxJammingName(byte status)
        {
            switch (status)
            {
                case 0: return "monitor unavailable";
                case 1: return "no significant interference";
                case 2: return "interference warning";
                case 3: return "critical interference";
                default: return "unknown";
            }
        }

        private async void RefreshAtomic_Click(object sender, RoutedEventArgs e)
        {
            await RefreshAtomicAsync(true);
        }

        private async Task RefreshAtomicAsync(bool showError)
        {
            if (atomicRefreshing)
                return;
            if (lastSnapshot != null &&
                lastSnapshot.Layout == "Orolia ART")
            {
                if (lastSnapshot.AbiVersion < 9)
                {
                    AtomicConnectionText.Text = "DRIVER 1.32 / ABI 9 REQUIRED";
                    AtomicConnectionText.Foreground =
                        (Brush)FindResource("GoldBrush");
                    AtomicRefreshButton.IsEnabled = false;
                    return;
                }
                if (client == null)
                {
                    AtomicConnectionText.Text = "DRIVER ACCESS REQUIRED";
                    AtomicConnectionText.Foreground =
                        (Brush)FindResource("GoldBrush");
                    if (showError)
                        EnsureConnected();
                    return;
                }

                atomicRefreshing = true;
                AtomicRefreshButton.IsEnabled = false;
                AtomicConnectionText.Text = "QUERYING FPGA BRIDGE";
                AtomicConnectionText.Foreground =
                    (Brush)FindResource("GoldBrush");
                try
                {
                    Mro50Status status = await Task.Run(
                        () => client.GetMro50Status());
                    lastMro50Status = status;
                    lastSa53Snapshot = null;
                    ApplyMro50Status(status);
                    UpdateHealthExperience();
                    Log("Orolia ART mRO-50 FPGA telemetry refreshed.");
                }
                catch (Exception ex)
                {
                    AtomicConnectionText.Text = "MRO-50 UNAVAILABLE";
                    AtomicConnectionText.Foreground =
                        (Brush)FindResource("DangerBrush");
                    Log("mRO-50 refresh failed: " + ex.Message);
                    if (showError)
                        MessageBox.Show(this, ex.Message,
                            "mRO-50 unavailable", MessageBoxButton.OK,
                            MessageBoxImage.Error);
                }
                finally
                {
                    AtomicRefreshButton.IsEnabled = true;
                    atomicRefreshing = false;
                }
                return;
            }
            if (client == null)
            {
                AtomicConnectionText.Text = "DRIVER ACCESS REQUIRED";
                AtomicConnectionText.Foreground = (Brush)FindResource("GoldBrush");
                if (showError)
                    EnsureConnected();
                return;
            }
            atomicRefreshing = true;
            StopUartMonitor();
            AtomicRefreshButton.IsEnabled = false;
            AtomicConnectionText.Text = "QUERYING UART 2";
            AtomicConnectionText.Foreground = (Brush)FindResource("GoldBrush");
            try
            {
                Sa53Snapshot snapshot = await Task.Run(
                    () => new Sa53Client(client).Refresh());
                lastSa53Snapshot = snapshot;
                UpdateHealthExperience();
                ApplyAtomicSnapshot(snapshot);
                AtomicConnectionText.Text = snapshot.Warnings.Count == 0 ?
                    "LIVE · C3" : "LIVE · PARTIAL";
                AtomicConnectionText.Foreground = (Brush)FindResource(
                    snapshot.Warnings.Count == 0 ? "AccentBrush" : "GoldBrush");
                Log(string.Format(CultureInfo.InvariantCulture,
                    "SA53 telemetry refreshed with {0} warning(s).", snapshot.Warnings.Count));
                foreach (string warning in snapshot.Warnings.Take(4))
                    Log("SA53: " + warning);
            }
            catch (Exception ex)
            {
                AtomicConnectionText.Text = "SA53 UNAVAILABLE";
                AtomicConnectionText.Foreground = (Brush)FindResource("DangerBrush");
                Log("SA53 refresh failed: " + ex.Message);
                if (showError)
                    MessageBox.Show(this, ex.Message +
                        "\r\n\r\nVerify that UART 2 is connected to a Microchip MAC-SA53 and is using its default 57,600-baud C3 interface.",
                        "Atomic clock unavailable", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                AtomicRefreshButton.IsEnabled = true;
                atomicRefreshing = false;
            }
        }

        private void ConfigureAtomicWorkspaceForProfile(bool isArt)
        {
            AtomicEyebrowText.Text = isArt ?
                "SAFRAN / OROLIA MRO-50" : "MICROCHIP MAC-SA53";
            AtomicTitleText.Text = isArt ?
                "ART atomic oscillator" : "Atomic clock configuration";
            AtomicDescriptionText.Text = isArt ?
                "Live lock, temperature word, and frequency steering through the ART FPGA's direct mRO-50 bridge." :
                "Live physics-package telemetry, frequency steering, 1PPS geometry, and disciplined timing control over the C3 serial interface.";
            AtomicRefreshButton.Content = isArt ? "Refresh mRO-50" : "Refresh SA53";
            AtomicLockLabelText.Text = isArt ? "OSCILLATOR LOCK" : "PHYSICS LOCK";
            AtomicTemperatureLabelText.Text = isArt ? "RAW TEMPERATURE" : "TEMPERATURE";
            AtomicSupplyLabelText.Text = isArt ? "FINE ADJUSTMENT" : "POWER SUPPLY";
            AtomicAlarmLabelText.Text = isArt ? "COARSE ADJUSTMENT" : "ACTIVE ALARMS";
            AtomicIdentityProtocolText.Text = isArt ?
                "Direct FPGA bridge at BAR + 0x00340000" :
                "UART 2 \u00B7 57,600 baud \u00B7 C3 protocol";
            AtomicTelemetryDescriptionText.Text = isArt ?
                "Read-only operating values reported by the ART gateware." :
                "Read-only operating values reported directly by the SA53.";
            AtomicSa53ControlGrid.Visibility = isArt ?
                Visibility.Collapsed : Visibility.Visible;
            AtomicMroPanel.Visibility = isArt ?
                Visibility.Visible : Visibility.Collapsed;
            AtomicSa53DisciplinePanel.Visibility = isArt ?
                Visibility.Collapsed : Visibility.Visible;
            AtomicSa53ProtectedGrid.Visibility = isArt ?
                Visibility.Collapsed : Visibility.Visible;
        }

        private void ConfigureUartWorkspaceForProfile(bool isArt)
        {
            if (UartPortCombo == null)
                return;
            string[] standard =
            {
                "0 \u00B7 Primary GNSS",
                "1 \u00B7 Secondary GNSS",
                "2 \u00B7 Atomic clock",
                "3 \u00B7 NMEA output"
            };
            string[] art =
            {
                "0 \u00B7 ART GNSS (gateware baud)",
                "1 \u00B7 Not implemented on ART",
                "2 \u00B7 mRO-50 serial (9600)",
                "3 \u00B7 No NMEA UART on ART"
            };
            for (uint port = 0; port < 4; port++)
            {
                ComboBoxItem item = UartPortCombo.Items
                    .OfType<ComboBoxItem>().FirstOrDefault(candidate =>
                        string.Equals(Convert.ToString(candidate.Tag,
                            CultureInfo.InvariantCulture),
                            port.ToString(CultureInfo.InvariantCulture),
                            StringComparison.Ordinal));
                if (item == null)
                    continue;
                item.Content = isArt ? art[port] : standard[port];
                item.IsEnabled = !isArt || port == 0 || port == 2;
            }
            if (isArt && SelectedGenericComPort() == null)
            {
                uint selected = SelectedUartPort();
                if (selected == 1 || selected == 3)
                    UartPortCombo.SelectedIndex = 0;
            }
        }

        private void ApplyMro50Status(Mro50Status status)
        {
            Brush healthy = (Brush)FindResource("AccentBrush");
            Brush warning = (Brush)FindResource("GoldBrush");
            AtomicConnectionText.Text = status.IsPresent ?
                "LIVE \u00B7 FPGA BRIDGE" : "NOT PRESENT";
            AtomicConnectionText.Foreground = status.IsPresent ?
                healthy : warning;
            AtomicLockText.Text = status.IsLocked ? "LOCKED" :
                status.IsEnabled ? "ENABLED" : "DISABLED";
            AtomicLockText.Foreground = status.IsLocked ? healthy : warning;
            AtomicLockDetailText.Text = string.Format(
                CultureInfo.InvariantCulture, "Control 0x{0:X8}",
                status.Control);

            AtomicTemperatureText.Text = string.Format(
                CultureInfo.InvariantCulture, "0x{0:X8}",
                status.TemperatureRaw);
            AtomicTemperatureDetailText.Text =
                "Raw ART gateware word; no physical scale is published";
            AtomicSupplyText.Text = status.IsFineValid ?
                status.FineAdjustment.ToString(CultureInfo.InvariantCulture) :
                "NOT VALID";
            AtomicSupplyDetailText.Text = status.IsFineValid ?
                "0x" + status.FineAdjustment.ToString(
                    "X8", CultureInfo.InvariantCulture) :
                "FPGA read did not complete";
            AtomicAlarmText.Text = status.IsCoarseValid ?
                status.CoarseAdjustment.ToString(CultureInfo.InvariantCulture) :
                "NOT VALID";
            AtomicAlarmText.Foreground = status.IsCoarseValid ?
                healthy : warning;
            AtomicAlarmDetailText.Text = status.IsCoarseValid ?
                "0x" + status.CoarseAdjustment.ToString(
                    "X8", CultureInfo.InvariantCulture) :
                "FPGA read did not complete";

            AtomicDeviceText.Text = "mRO-50";
            AtomicPartText.Text = "Safran / Orolia ART";
            AtomicSerialText.Text =
                "Not exposed by this EEPROM image";
            AtomicFirmwareText.Text = lastSnapshot == null ?
                "ART FPGA bridge" : "ART FPGA " + lastSnapshot.ClockVersion;
            AtomicHardwareText.Text = "PCI 1AD7:A000";

            AtomicRuntimeText.Text = status.IsPresent ? "FPGA bridge active" : "\u2014";
            AtomicLocktimeText.Text = "Not exposed";
            AtomicPpsDetectedText.Text = "Not exposed";
            AtomicDisciplineStateText.Text = status.IsEnabled ?
                "ENABLED" : "DISABLED";
            AtomicPhaseText.Text = "Control 0x" +
                status.Control.ToString("X8", CultureInfo.InvariantCulture);
            AtomicEffectiveTuningText.Text = status.IsFineValid ?
                status.FineAdjustment.ToString(CultureInfo.InvariantCulture) :
                "\u2014";
            AtomicDisciplineTuningText.Text = status.IsCoarseValid ?
                status.CoarseAdjustment.ToString(CultureInfo.InvariantCulture) :
                "\u2014";
            AtomicLastCorrectionText.Text = "Board config 0x" +
                status.BoardConfig.ToString("X8", CultureInfo.InvariantCulture);
            AtomicMroFineTextBox.Text = status.FineAdjustment.ToString(
                CultureInfo.InvariantCulture);
            AtomicMroCoarseTextBox.Text = status.CoarseAdjustment.ToString(
                CultureInfo.InvariantCulture);
            AtomicMroSerialRouteText.Text = status.IsSerialRouteEnabled ?
                "SERIAL ROUTE ENABLED" : "DIRECT BRIDGE ACTIVE";
            AtomicMroSerialRouteText.Foreground = status.IsPresent ?
                healthy : warning;
        }

        private async void ApplyMroFine_Click(object sender, RoutedEventArgs e)
        {
            uint value;
            if (!uint.TryParse(AtomicMroFineTextBox.Text,
                NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
            {
                MessageBox.Show(this, "Enter a valid unsigned 32-bit fine adjustment.",
                    "Invalid mRO-50 value", MessageBoxButton.OK,
                    MessageBoxImage.Warning);
                return;
            }
            await RunMro50OperationAsync(
                activeClient => activeClient.SetMro50FineAdjustment(value),
                "mRO-50 fine adjustment updated.");
        }

        private async void ApplyMroCoarse_Click(object sender, RoutedEventArgs e)
        {
            uint value;
            if (!uint.TryParse(AtomicMroCoarseTextBox.Text,
                NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
            {
                MessageBox.Show(this, "Enter a valid unsigned 32-bit coarse adjustment.",
                    "Invalid mRO-50 value", MessageBoxButton.OK,
                    MessageBoxImage.Warning);
                return;
            }
            await RunMro50OperationAsync(
                activeClient => activeClient.SetMro50CoarseAdjustment(value),
                "mRO-50 coarse adjustment updated.");
        }

        private async void SaveMroCoarse_Click(object sender, RoutedEventArgs e)
        {
            if (MessageBox.Show(this,
                "Save the current mRO-50 coarse adjustment to nonvolatile storage?",
                "Confirm mRO-50 write", MessageBoxButton.YesNo,
                MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;
            await RunMro50OperationAsync(
                activeClient => activeClient.SaveMro50CoarseAdjustment(),
                "mRO-50 coarse adjustment saved.");
        }

        private async Task RunMro50OperationAsync(
            Func<TimeCardClient, Mro50Status> operation, string success)
        {
            if (client == null || atomicRefreshing)
                return;
            atomicRefreshing = true;
            AtomicRefreshButton.IsEnabled = false;
            TimeCardClient activeClient = client;
            try
            {
                Mro50Status status = await Task.Run(
                    () => operation(activeClient));
                if (client != activeClient)
                    return;
                lastMro50Status = status;
                ApplyMro50Status(status);
                Log(success);
            }
            catch (Exception ex)
            {
                Log("mRO-50 operation failed: " + ex.Message);
                MessageBox.Show(this, ex.Message, "mRO-50 operation failed",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                AtomicRefreshButton.IsEnabled = true;
                atomicRefreshing = false;
            }
        }

        private void ApplyAtomicSnapshot(Sa53Snapshot snapshot)
        {
            AtomicDeviceText.Text = AtomicValue(snapshot, "device?");
            AtomicPartText.Text = AtomicValue(snapshot, "pn?");
            AtomicSerialText.Text = AtomicValue(snapshot, "serial?");
            AtomicFirmwareText.Text = AtomicValue(snapshot, "swrev?");
            AtomicHardwareText.Text = AtomicValue(snapshot, "hwrev?");

            bool locked;
            long progress;
            AtomicLockText.Text = snapshot.TryBoolean("Locked", out locked) ?
                (locked ? "LOCKED" : "ACQUIRING") : "—";
            AtomicLockText.Foreground = snapshot.TryBoolean("Locked", out locked) && locked ?
                (Brush)FindResource("AccentBrush") : (Brush)FindResource("GoldBrush");
            AtomicLockDetailText.Text = snapshot.TryInt64("LockProgress", out progress) ?
                string.Format(CultureInfo.InvariantCulture, "Lock progress {0}%", progress) :
                "Lock progress unavailable";

            double temperature;
            AtomicTemperatureText.Text = snapshot.TryDouble("Temperature", out temperature) ?
                (temperature / 1000.0).ToString("N2", CultureInfo.InvariantCulture) + " °C" : "—";
            double laserTemperature;
            AtomicTemperatureDetailText.Text = snapshot.TryDouble("LaserTempSet", out laserTemperature) ?
                "Laser setpoint " + (laserTemperature / 1000.0).ToString("N2", CultureInfo.InvariantCulture) + " °C" :
                "Baseplate must remain ≤ 75 °C";

            double supply;
            AtomicSupplyText.Text = snapshot.TryDouble("PowerSupply", out supply) ?
                (supply / 1000.0).ToString("N3", CultureInfo.InvariantCulture) + " V" : "—";
            double analog;
            AtomicSupplyDetailText.Text = snapshot.TryDouble("AnalogTuning", out analog) ?
                "Analog input " + analog.ToString("N0", CultureInfo.InvariantCulture) + " mV" :
                "SA53 input telemetry";

            long alarms;
            if (snapshot.TryInt64("Alarms", out alarms))
            {
                AtomicAlarmText.Text = alarms == 0 ? "NONE" : "0x" + alarms.ToString("X8", CultureInfo.InvariantCulture);
                AtomicAlarmText.Foreground = alarms == 0 ? (Brush)FindResource("AccentBrush") :
                    (Brush)FindResource("DangerBrush");
                AtomicAlarmDetailText.Text = DescribeAtomicAlarms(alarms);
            }
            else
            {
                AtomicAlarmText.Text = "—";
                AtomicAlarmDetailText.Text = "Alarm word unavailable";
            }

            long seconds;
            AtomicRuntimeText.Text = snapshot.TryInt64("TotalRuntime", out seconds) ?
                FormatDuration(seconds) : "—";
            AtomicLocktimeText.Text = snapshot.TryInt64("TotalLocktime", out seconds) ?
                FormatDuration(seconds) : "—";
            bool flag;
            AtomicPpsDetectedText.Text = snapshot.TryBoolean("PpsInDetected", out flag) ?
                (flag ? "DETECTED" : "NOT DETECTED") : "—";
            bool disciplining;
            bool disciplineLocked;
            bool jamSyncing;
            string discipline = snapshot.TryBoolean("Disciplining", out disciplining) && disciplining ?
                "ENABLED" : "DISABLED";
            if (snapshot.TryBoolean("DisciplineLocked", out disciplineLocked) && disciplineLocked)
                discipline += " · LOCKED";
            if (snapshot.TryBoolean("JamSyncing", out jamSyncing) && jamSyncing)
                discipline += " · JAMSYNC";
            AtomicDisciplineStateText.Text = discipline;
            AtomicPhaseText.Text = FormatAtomicUnit(snapshot, "Phase", " ns");
            AtomicEffectiveTuningText.Text = FormatAtomicUnit(snapshot, "EffectiveTuning", " ×10⁻¹⁵");
            AtomicDisciplineTuningText.Text = FormatAtomicUnit(snapshot, "DisciplineTuning", " ×10⁻¹⁵");
            AtomicLastCorrectionText.Text = FormatAtomicUnit(snapshot, "LastCorrection", " ×10⁻¹⁵");

            SetAtomicText(AtomicDigitalTuningTextBox, snapshot.Value("DigitalTuning"));
            SetAtomicText(AtomicPpsOffsetTextBox, snapshot.Value("PpsOffset"));
            SetAtomicText(AtomicPpsWidthTextBox, snapshot.Value("PpsWidth"));
            SetAtomicText(AtomicCableDelayTextBox, snapshot.Value("CableDelay"));
            SetAtomicText(AtomicTau0TextBox, snapshot.Value("TauPps0"));
            SetAtomicText(AtomicTau1TextBox, snapshot.Value("TauPps1"));
            SetAtomicText(AtomicPpsQerrTextBox, snapshot.Value("PpsQErr"));
            SetAtomicText(AtomicPhaseLimitTextBox, snapshot.Value("PhaseLimit"));
            SetAtomicText(AtomicThreshold0TextBox, snapshot.Value("DisciplineThresholdPps0"));
            SetAtomicText(AtomicThreshold1TextBox, snapshot.Value("DisciplineThresholdPps1"));
            SetAtomicText(AtomicTodTextBox, snapshot.Value("TimeOfDay"));

            long source;
            if (snapshot.TryInt64("PpsSource", out source) && source >= 0 && source <= 1)
                AtomicPpsSourceCombo.SelectedIndex = (int)source;
            if (snapshot.TryBoolean("Disciplining", out flag))
                AtomicDiscipliningCheckBox.IsChecked = flag;
            if (snapshot.TryBoolean("PhaseMetering", out flag))
                AtomicPhaseMeteringCheckBox.IsChecked = flag;
            if (snapshot.TryBoolean("AnalogTuningEnabled", out flag))
                AtomicAnalogEnabledCheckBox.IsChecked = flag;
        }

        private async void ApplyAtomicDigitalTuning_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                long value = ParseSigned(AtomicDigitalTuningTextBox.Text,
                    "digital tuning", -20000000, 20000000);
                await RunAtomicOperationAsync("Digital tuning set to " +
                    value.ToString(CultureInfo.InvariantCulture) + " ×10⁻¹⁵",
                    clock => clock.Set("DigitalTuning", value));
            }
            catch (Exception ex)
            {
                ShowAtomicError("Digital tuning was not changed", ex);
            }
        }

        private async void AdjustAtomicDigitalTuning_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                Button button = sender as Button;
                long delta = long.Parse(Convert.ToString(button.Tag, CultureInfo.InvariantCulture),
                    CultureInfo.InvariantCulture);
                long current = ParseSigned(AtomicDigitalTuningTextBox.Text,
                    "digital tuning", -20000000, 20000000);
                if (current + delta < -20000000 || current + delta > 20000000)
                    throw new InvalidOperationException("The tuning step would exceed the SA53 ±20,000,000 limit.");
                await RunAtomicOperationAsync("Digital tuning adjusted by " +
                    delta.ToString(CultureInfo.InvariantCulture) + " ×10⁻¹⁵",
                    clock => clock.AddDigitalTuning(delta));
            }
            catch (Exception ex)
            {
                ShowAtomicError("Digital tuning was not adjusted", ex);
            }
        }

        private async void ApplyAtomicAnalogMode_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                bool enable = AtomicAnalogEnabledCheckBox.IsChecked == true;
                if (enable)
                {
                    long digital = ParseSigned(AtomicDigitalTuningTextBox.Text,
                        "digital tuning", -20000000, 20000000);
                    if (digital != 0 || AtomicDiscipliningCheckBox.IsChecked == true)
                        throw new InvalidOperationException(
                            "Set digital tuning to zero and disable PPS disciplining before enabling analog tuning.");
                    if (MessageBox.Show(this,
                        "Analog tuning is less precise than digital tuning and should not be combined with other steering modes. Enable it now?",
                        "Enable analog tuning", MessageBoxButton.YesNo,
                        MessageBoxImage.Warning) != MessageBoxResult.Yes)
                        return;
                }
                await RunAtomicOperationAsync("Analog tuning " + (enable ? "enabled" : "disabled"),
                    clock => clock.Set("AnalogTuningEnabled", enable ? 1 : 0));
            }
            catch (Exception ex)
            {
                ShowAtomicError("Analog tuning mode was not changed", ex);
            }
        }

        private async void ApplyAtomicPps_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                long source = SelectedAtomicPpsSource();
                long cableDelay = ParseSigned(AtomicCableDelayTextBox.Text,
                    "cable delay", -500000000, 500000000);
                long offset = ParseSigned(AtomicPpsOffsetTextBox.Text,
                    "PPS output offset", -83886080, 83886080);
                long width = ParseSigned(AtomicPpsWidthTextBox.Text,
                    "PPS output width", 100, 83886080);
                RequireTenNanosecondStep(offset, "PPS output offset");
                RequireTenNanosecondStep(width, "PPS output width");
                await RunAtomicOperationAsync("1PPS source and output geometry updated", clock =>
                {
                    clock.Set("PpsSource", source);
                    clock.Set("CableDelay", cableDelay);
                    clock.Set("PpsOffset", offset);
                    clock.Set("PpsWidth", width);
                });
            }
            catch (Exception ex)
            {
                ShowAtomicError("1PPS settings were not fully applied", ex);
            }
        }

        private void SetAtomicTauPreset_Click(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            string value = Convert.ToString(button.Tag, CultureInfo.InvariantCulture);
            AtomicTau0TextBox.Text = value;
            AtomicTau1TextBox.Text = value;
        }

        private async void ApplyAtomicDiscipline_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                bool disciplining = AtomicDiscipliningCheckBox.IsChecked == true;
                bool metering = AtomicPhaseMeteringCheckBox.IsChecked == true;
                if (disciplining && metering)
                    throw new InvalidOperationException(
                        "PPS disciplining and phase metering cannot be enabled at the same time.");
                bool wasDisciplining;
                if (disciplining && (lastSa53Snapshot == null ||
                    !lastSa53Snapshot.TryBoolean("Disciplining", out wasDisciplining) || !wasDisciplining))
                {
                    if (MessageBox.Show(this,
                        "Enabling disciplining starts with a JamSync and can produce an immediate 1PPS phase step. Continue?",
                        "Enable PPS disciplining", MessageBoxButton.YesNo,
                        MessageBoxImage.Warning) != MessageBoxResult.Yes)
                        return;
                }
                long source = SelectedAtomicPpsSource();
                long tau0 = ParseSigned(AtomicTau0TextBox.Text, "PPS 0 tau", 10, 45000);
                long tau1 = ParseSigned(AtomicTau1TextBox.Text, "PPS 1 tau", 10, 45000);
                long phaseLimit = ParseSigned(AtomicPhaseLimitTextBox.Text,
                    "phase limit", -1000000, 1000000);
                long qerr = ParseSigned(AtomicPpsQerrTextBox.Text,
                    "PPS quantization error", -1000000, 1000000);
                long threshold0 = ParseSigned(AtomicThreshold0TextBox.Text,
                    "PPS 0 lock threshold", 1, 1000);
                long threshold1 = ParseSigned(AtomicThreshold1TextBox.Text,
                    "PPS 1 lock threshold", 1, 1000);
                await RunAtomicOperationAsync("SA53 discipline configuration updated", clock =>
                {
                    clock.Set("PpsSource", source);
                    clock.Set("TauPps0", tau0);
                    clock.Set("TauPps1", tau1);
                    clock.Set("PpsQErr", qerr);
                    clock.Set("PhaseLimit", phaseLimit);
                    clock.Set("DisciplineThresholdPps0", threshold0);
                    clock.Set("DisciplineThresholdPps1", threshold1);
                    if (disciplining)
                    {
                        clock.Set("PhaseMetering", 0);
                        clock.Set("Disciplining", 1);
                    }
                    else if (metering)
                    {
                        clock.Set("Disciplining", 0);
                        clock.Set("PhaseMetering", 1);
                    }
                    else
                    {
                        clock.Set("Disciplining", 0);
                        clock.Set("PhaseMetering", 0);
                    }
                });
            }
            catch (Exception ex)
            {
                ShowAtomicError("Discipline settings were not fully applied", ex);
            }
        }

        private void UseCurrentAtomicTod_Click(object sender, RoutedEventArgs e)
        {
            long seconds = (long)(DateTime.UtcNow -
                new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc)).TotalSeconds;
            AtomicTodTextBox.Text = seconds.ToString(CultureInfo.InvariantCulture);
        }

        private async void ApplyAtomicTod_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                long value = ParseSigned(AtomicTodTextBox.Text,
                    "time-of-day counter", 0, 2147483647);
                await RunAtomicOperationAsync("SA53 time-of-day counter scheduled for the next PPS",
                    clock => clock.Set("TimeOfDay", value));
            }
            catch (Exception ex)
            {
                ShowAtomicError("Time-of-day was not changed", ex);
            }
        }

        private async void AcknowledgeAtomicAlarms_Click(object sender, RoutedEventArgs e)
        {
            long alarms;
            if (lastSa53Snapshot == null ||
                !lastSa53Snapshot.TryInt64("Alarms", out alarms) || alarms == 0)
            {
                MessageBox.Show(this, "There are no active SA53 alarm bits to acknowledge.",
                    "No active alarms", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }
            if (!ConfirmAtomicOperation("Acknowledge the SA53 alarm output? Active alarm conditions remain visible until their causes clear.",
                "Acknowledge alarms"))
                return;
            await RunAtomicOperationWithErrorAsync("SA53 alarm output acknowledged",
                clock => clock.AcknowledgeAlarms(alarms));
        }

        private async void JamSyncAtomic_Click(object sender, RoutedEventArgs e)
        {
            if (!ConfirmAtomicOperation("Force a JamSync now? This intentionally produces a phase jump on the 1PPS output.",
                "Force JamSync"))
                return;
            await RunAtomicOperationWithErrorAsync("SA53 JamSync requested", clock => clock.JamSync());
        }

        private async void LoadAtomicConfig_Click(object sender, RoutedEventArgs e)
        {
            if (!ConfirmAtomicOperation("Load the most recently stored SA53 configuration? Unsaved runtime edits will be discarded.",
                "Load stored configuration"))
                return;
            await RunAtomicOperationWithErrorAsync("SA53 stored configuration loaded", clock => clock.Load());
        }

        private async void StoreAtomicConfig_Click(object sender, RoutedEventArgs e)
        {
            if (!ConfirmAtomicOperation("Write the current persistable SA53 settings to flash? This includes current steering configuration.",
                "Store configuration to flash"))
                return;
            await RunAtomicOperationWithErrorAsync("SA53 configuration stored to flash", clock => clock.Store());
        }

        private async Task RunAtomicOperationWithErrorAsync(string description, Action<Sa53Client> operation)
        {
            try
            {
                await RunAtomicOperationAsync(description, operation);
            }
            catch (Exception ex)
            {
                ShowAtomicError(description + " failed", ex);
            }
        }

        private async Task RunAtomicOperationAsync(string description, Action<Sa53Client> operation)
        {
            if (!EnsureConnected())
                return;
            if (lastSnapshot != null &&
                lastSnapshot.Layout == "Orolia ART")
            {
                throw new InvalidOperationException(
                    "SA53 commands are disabled on the Orolia ART profile. Use UART 2 at 9,600 baud for its mRO-50 oscillator.");
            }
            if (atomicRefreshing)
                throw new InvalidOperationException("Wait for the current SA53 operation to finish.");
            atomicRefreshing = true;
            StopUartMonitor();
            AtomicRefreshButton.IsEnabled = false;
            AtomicConnectionText.Text = "APPLYING";
            AtomicConnectionText.Foreground = (Brush)FindResource("GoldBrush");
            try
            {
                Sa53Snapshot snapshot = await Task.Run(() =>
                {
                    Sa53Client clock = new Sa53Client(client);
                    operation(clock);
                    return clock.Refresh();
                });
                lastSa53Snapshot = snapshot;
                ApplyAtomicSnapshot(snapshot);
                AtomicConnectionText.Text = snapshot.Warnings.Count == 0 ? "LIVE · C3" : "LIVE · PARTIAL";
                AtomicConnectionText.Foreground = (Brush)FindResource(
                    snapshot.Warnings.Count == 0 ? "AccentBrush" : "GoldBrush");
                Log(description + ".");
            }
            finally
            {
                AtomicRefreshButton.IsEnabled = true;
                atomicRefreshing = false;
            }
        }

        private void ShowAtomicError(string title, Exception ex)
        {
            AtomicConnectionText.Text = "OPERATION FAILED";
            AtomicConnectionText.Foreground = (Brush)FindResource("DangerBrush");
            Log(title + ": " + ex.Message);
            MessageBox.Show(this, ex.Message, title, MessageBoxButton.OK, MessageBoxImage.Error);
        }

        private bool ConfirmAtomicOperation(string message, string title)
        {
            return MessageBox.Show(this, message, title, MessageBoxButton.YesNo,
                MessageBoxImage.Warning) == MessageBoxResult.Yes;
        }

        private long SelectedAtomicPpsSource()
        {
            ComboBoxItem item = AtomicPpsSourceCombo.SelectedItem as ComboBoxItem;
            if (item == null)
                throw new InvalidOperationException("Select an SA53 PPS input.");
            return long.Parse(Convert.ToString(item.Tag, CultureInfo.InvariantCulture),
                CultureInfo.InvariantCulture);
        }

        private static void RequireTenNanosecondStep(long value, string name)
        {
            if (value % 10 != 0)
                throw new InvalidOperationException(name + " must be a multiple of 10 ns.");
        }

        private static long ParseSigned(string text, string name, long minimum, long maximum)
        {
            long value;
            if (!long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value) ||
                value < minimum || value > maximum)
                throw new InvalidOperationException(string.Format(CultureInfo.InvariantCulture,
                    "Enter a valid {0} between {1} and {2}.", name, minimum, maximum));
            return value;
        }

        private static double ParseInvariantDouble(string text, string name,
            double minimum, double maximum)
        {
            double value;
            if (!double.TryParse(text, NumberStyles.Float,
                CultureInfo.InvariantCulture, out value) || double.IsNaN(value) ||
                double.IsInfinity(value) || value < minimum || value > maximum)
                throw new InvalidOperationException(string.Format(
                    CultureInfo.InvariantCulture,
                    "Enter a valid {0} between {1} and {2}.", name, minimum,
                    maximum));
            return value;
        }

        private static string AtomicValue(Sa53Snapshot snapshot, string name)
        {
            string value = snapshot.Value(name);
            return string.IsNullOrWhiteSpace(value) ? "—" : value;
        }

        private static void SetAtomicText(TextBox box, string value)
        {
            if (!string.IsNullOrWhiteSpace(value))
                box.Text = value;
        }

        private static string FormatAtomicUnit(Sa53Snapshot snapshot, string name, string unit)
        {
            double value;
            return snapshot.TryDouble(name, out value) ?
                value.ToString("N0", CultureInfo.InvariantCulture) + unit : "—";
        }

        private static string FormatDuration(long seconds)
        {
            if (seconds < 0)
                return "—";
            TimeSpan duration = TimeSpan.FromSeconds(seconds);
            if (duration.TotalDays >= 1)
                return string.Format(CultureInfo.InvariantCulture, "{0:N0} d {1:00}:{2:00}:{3:00}",
                    Math.Floor(duration.TotalDays), duration.Hours, duration.Minutes, duration.Seconds);
            return string.Format(CultureInfo.InvariantCulture, "{0:00}:{1:00}:{2:00}",
                duration.Hours, duration.Minutes, duration.Seconds);
        }

        private static string DescribeAtomicAlarms(long alarms)
        {
            if (alarms == 0)
                return "No active alarm bits";
            List<string> active = new List<string>();
            AddAtomicAlarm(active, alarms, 1, "FPGA fault");
            AddAtomicAlarm(active, alarms, 2, "PLL fault");
            AddAtomicAlarm(active, alarms, 4, "flash fault");
            AddAtomicAlarm(active, alarms, 8, "acquisition failed");
            AddAtomicAlarm(active, alarms, 16, "no external oscillator");
            AddAtomicAlarm(active, alarms, 32, "cell heater fault");
            AddAtomicAlarm(active, alarms, 64, "incompatible firmware");
            AddAtomicAlarm(active, alarms, 65536, "temperature warning");
            AddAtomicAlarm(active, alarms, 131072, "no PPS input");
            AddAtomicAlarm(active, alarms, 262144, "disciplining range warning");
            return active.Count == 0 ? "Unrecognized alarm bits" : string.Join(", ", active);
        }

        private static void AddAtomicAlarm(ICollection<string> active, long alarms,
            long mask, string name)
        {
            if ((alarms & mask) != 0)
                active.Add(name);
        }

        private void OpenSma_Click(object sender, RoutedEventArgs e)
        {
            SmaNav.IsChecked = true;
        }

        private async void RefreshSma_Click(object sender, RoutedEventArgs e)
        {
            await RefreshSmaAsync();
        }

        private async Task RefreshSmaAsync()
        {
            if (client == null)
            {
                SetSmaUnavailable("NOT CONNECTED");
                return;
            }
            if (lastSnapshot == null || lastSnapshot.AbiVersion < 2)
            {
                SetSmaUnavailable("DRIVER UPDATE REQUIRED");
                return;
            }

            for (uint connector = 1; connector <= 4; connector++)
            {
                try
                {
                    uint currentConnector = connector;
                    SmaConnectorState state = await Task.Run(
                        () => client.GetSmaConnector(currentConnector));
                    ApplySmaState(state);
                }
                catch (Exception ex)
                {
                    SetSmaConnectorUnavailable(connector, "QUERY FAILED");
                    Log(string.Format(CultureInfo.InvariantCulture,
                        "SMA {0} query failed: {1}", connector, ex.Message));
                }
            }
        }

        private async void ApplySma_Click(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint connector;
            if (button == null || !uint.TryParse(
                Convert.ToString(button.Tag, CultureInfo.InvariantCulture),
                NumberStyles.Integer, CultureInfo.InvariantCulture, out connector) ||
                !EnsureConnected())
                return;

            ComboBox directionCombo = GetSmaDirectionCombo(connector);
            ComboBox functionCombo = GetSmaFunctionCombo(connector);
            SmaDirection direction = SelectedSmaDirection(directionCombo);
            SmaFunctionChoice function = functionCombo.SelectedItem as SmaFunctionChoice;
            uint functionValue = direction == SmaDirection.Disabled || function == null ?
                0u : function.Value;

            if (direction == SmaDirection.Output)
            {
                string signalName = function == null ? "the selected signal" : function.Name;
                MessageBoxResult confirmation = MessageBox.Show(this,
                    string.Format(CultureInfo.InvariantCulture,
                        "Route {0} to SMA {1} as an output? Disconnect any external source from this connector first.",
                        signalName, connector),
                    "Confirm SMA output", MessageBoxButton.YesNo,
                    MessageBoxImage.Warning);
                if (confirmation != MessageBoxResult.Yes)
                    return;
            }

            button.IsEnabled = false;
            try
            {
                SmaConnectorState state = await Task.Run(() =>
                    client.SetSmaConnector(connector, direction, functionValue));
                ApplySmaState(state);
                Log(string.Format(CultureInfo.InvariantCulture,
                    "SMA {0} set to {1}{2}.", connector, direction,
                    direction == SmaDirection.Disabled ? string.Empty :
                    " / " + (function == null ?
                        string.Format(CultureInfo.InvariantCulture, "0x{0:X4}", functionValue) :
                        function.Name)));
            }
            catch (Exception ex)
            {
                Log(string.Format(CultureInfo.InvariantCulture,
                    "SMA {0} configuration failed: {1}", connector, ex.Message));
                MessageBox.Show(this, ex.Message, "Unable to configure SMA connector",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                button.IsEnabled = true;
            }
        }

        private void SmaDirection_SelectionChanged(object sender,
                                                    SelectionChangedEventArgs e)
        {
            ComboBox combo = sender as ComboBox;
            uint connector;
            if (smaUpdatingUi || combo == null || !IsInitialized ||
                !uint.TryParse(Convert.ToString(combo.Tag, CultureInfo.InvariantCulture),
                    NumberStyles.Integer, CultureInfo.InvariantCulture, out connector) ||
                combo.SelectedItem == null)
                return;
            PopulateSmaFunctions(connector, SelectedSmaDirection(combo), null);
        }

        private void ApplySmaState(SmaConnectorState state)
        {
            ComboBox directionCombo = GetSmaDirectionCombo(state.Connector);
            TextBlock statusText = GetSmaStatusText(state.Connector);
            TextBlock rawText = GetSmaRawText(state.Connector);
            Button applyButton = GetSmaApplyButton(state.Connector);
            Brush healthyBrush = (Brush)FindResource("AccentBrush");

            if (state.Connector >= 1 && state.Connector <= 4)
                lastSmaLedStates[(int)state.Connector - 1] = state;

            smaUpdatingUi = true;
            try
            {
                directionCombo.SelectedIndex = (int)state.Direction;
                directionCombo.IsEnabled = state.IsPresent && !state.IsDirectionFixed;
                PopulateSmaFunctions(state.Connector, state.Direction,
                    state.Direction == SmaDirection.Disabled ? (uint?)null : state.Function);
                applyButton.IsEnabled = state.IsPresent;
                statusText.Text = state.IsDirectionFixed ?
                    "FIXED " + state.Direction.ToString().ToUpperInvariant() :
                    state.Direction.ToString().ToUpperInvariant();
                statusText.Foreground = healthyBrush;
                rawText.Text = string.Format(CultureInfo.InvariantCulture,
                    "Input 0x{0:X4} · Output 0x{1:X4}",
                    state.InputMap, state.OutputMap);
            }
            finally
            {
                smaUpdatingUi = false;
            }
            UpdateBoardLedAutomationAsync(false);
        }

        private void PopulateSmaFunctions(uint connector, SmaDirection direction,
                                          uint? selectedValue)
        {
            ComboBox combo = GetSmaFunctionCombo(connector);
            combo.Items.Clear();
            if (direction == SmaDirection.Disabled)
            {
                combo.Items.Add(new SmaFunctionChoice("No signal routed", 0));
                combo.SelectedIndex = 0;
                combo.IsEnabled = false;
                return;
            }

            bool art = lastSnapshot != null &&
                lastSnapshot.Layout == "Orolia ART";
            SmaFunctionChoice[] choices = art ?
                (direction == SmaDirection.Input ?
                    ArtSmaInputFunctions : ArtSmaOutputFunctions) :
                (direction == SmaDirection.Input ?
                    SmaInputFunctions : SmaOutputFunctions);
            foreach (SmaFunctionChoice choice in choices)
                combo.Items.Add(choice);
            if (selectedValue.HasValue &&
                !choices.Any(choice => choice.Value == selectedValue.Value))
                combo.Items.Add(new SmaFunctionChoice(
                    string.Format(CultureInfo.InvariantCulture,
                        "Firmware value 0x{0:X4}", selectedValue.Value),
                    selectedValue.Value));

            int selectedIndex = 0;
            if (selectedValue.HasValue)
            {
                for (int index = 0; index < combo.Items.Count; index++)
                {
                    SmaFunctionChoice choice = combo.Items[index] as SmaFunctionChoice;
                    if (choice != null && choice.Value == selectedValue.Value)
                    {
                        selectedIndex = index;
                        break;
                    }
                }
            }
            combo.SelectedIndex = selectedIndex;
            combo.IsEnabled = true;
        }

        private void SetSmaUnavailable(string status)
        {
            for (uint connector = 1; connector <= 4; connector++)
                SetSmaConnectorUnavailable(connector, status);
        }

        private void SetSmaConnectorUnavailable(uint connector, string status)
        {
            GetSmaStatusText(connector).Text = status;
            GetSmaStatusText(connector).Foreground = (Brush)FindResource("GoldBrush");
            GetSmaDirectionCombo(connector).IsEnabled = false;
            GetSmaFunctionCombo(connector).IsEnabled = false;
            GetSmaApplyButton(connector).IsEnabled = false;
        }

        private static SmaDirection SelectedSmaDirection(ComboBox combo)
        {
            ComboBoxItem item = combo.SelectedItem as ComboBoxItem;
            if (item == null)
                throw new InvalidOperationException("Select an SMA connector direction.");
            return (SmaDirection)uint.Parse(
                Convert.ToString(item.Tag, CultureInfo.InvariantCulture),
                CultureInfo.InvariantCulture);
        }

        private ComboBox GetSmaDirectionCombo(uint connector)
        {
            switch (connector)
            {
            case 1: return Sma1DirectionCombo;
            case 2: return Sma2DirectionCombo;
            case 3: return Sma3DirectionCombo;
            case 4: return Sma4DirectionCombo;
            default: throw new ArgumentOutOfRangeException("connector");
            }
        }

        private ComboBox GetSmaFunctionCombo(uint connector)
        {
            switch (connector)
            {
            case 1: return Sma1FunctionCombo;
            case 2: return Sma2FunctionCombo;
            case 3: return Sma3FunctionCombo;
            case 4: return Sma4FunctionCombo;
            default: throw new ArgumentOutOfRangeException("connector");
            }
        }

        private TextBlock GetSmaStatusText(uint connector)
        {
            switch (connector)
            {
            case 1: return Sma1StatusText;
            case 2: return Sma2StatusText;
            case 3: return Sma3StatusText;
            case 4: return Sma4StatusText;
            default: throw new ArgumentOutOfRangeException("connector");
            }
        }

        private TextBlock GetSmaRawText(uint connector)
        {
            switch (connector)
            {
            case 1: return Sma1RawText;
            case 2: return Sma2RawText;
            case 3: return Sma3RawText;
            case 4: return Sma4RawText;
            default: throw new ArgumentOutOfRangeException("connector");
            }
        }

        private Button GetSmaApplyButton(uint connector)
        {
            switch (connector)
            {
            case 1: return Sma1ApplyButton;
            case 2: return Sma2ApplyButton;
            case 3: return Sma3ApplyButton;
            case 4: return Sma4ApplyButton;
            default: throw new ArgumentOutOfRangeException("connector");
            }
        }

        private void OpenTiming_Click(object sender, RoutedEventArgs e)
        {
            TimingNav.IsChecked = true;
        }

        private async void RefreshTiming_Click(object sender, RoutedEventArgs e)
        {
            await RefreshTimingAsync();
        }

        private async Task RefreshTimingAsync()
        {
            if (timingRefreshing)
                return;
            if (client == null)
            {
                SetTimingUnavailable("NOT CONNECTED");
                return;
            }
            if (lastSnapshot == null || lastSnapshot.AbiVersion < 5)
            {
                SetTimingUnavailable("DRIVER 1.10 / ABI 5 REQUIRED");
                return;
            }

            timingRefreshing = true;
            SmaConnectorState[] routes = new SmaConnectorState[4];
            try
            {
                for (uint channel = 1; channel <= 4; channel++)
                {
                    uint currentChannel = channel;
                    try
                    {
                        SignalGeneratorState state = await Task.Run(() =>
                            client.GetSignalGenerator(currentChannel));
                        ApplySignalGeneratorState(state);
                    }
                    catch (Exception ex)
                    {
                        SetSignalGeneratorUnavailable(channel, "QUERY FAILED");
                        Log(string.Format(CultureInfo.InvariantCulture,
                            "Generator {0} query failed: {1}", channel, ex.Message));
                    }

                    try
                    {
                        FrequencyCounterState state = await Task.Run(() =>
                            client.GetFrequencyCounter(currentChannel));
                        ApplyFrequencyCounterState(state);
                    }
                    catch (Exception ex)
                    {
                        SetFrequencyCounterUnavailable(channel, "QUERY FAILED");
                        Log(string.Format(CultureInfo.InvariantCulture,
                            "Frequency {0} query failed: {1}", channel, ex.Message));
                    }
                }

                for (uint connector = 1; connector <= 4; connector++)
                {
                    uint currentConnector = connector;
                    try
                    {
                        routes[connector - 1] = await Task.Run(() =>
                            client.GetSmaConnector(currentConnector));
                    }
                    catch (Exception ex)
                    {
                        Log(string.Format(CultureInfo.InvariantCulture,
                            "Timing route query for SMA {0} failed: {1}",
                            connector, ex.Message));
                    }
                }
                ApplyTimingRoutes(routes);
            }
            finally
            {
                timingRefreshing = false;
            }
        }

        private void SignalScheduleMode_SelectionChanged(object sender,
                                                          RoutedEventArgs e)
        {
            ComboBox combo = sender as ComboBox;
            uint generator;
            if (combo == null || signalGeneratorControls == null ||
                !uint.TryParse(Convert.ToString(combo.Tag,
                    CultureInfo.InvariantCulture), NumberStyles.Integer,
                    CultureInfo.InvariantCulture, out generator) ||
                generator == 0 ||
                generator > (uint)signalGeneratorControls.Length)
                return;
            UpdateSignalScheduleControls(
                signalGeneratorControls[generator - 1]);
        }

        private static void UpdateSignalScheduleControls(
            SignalGeneratorControls controls)
        {
            bool available = controls.StartMode.IsEnabled;
            bool absolute = string.Equals(GetComboTag(controls.StartMode),
                "Absolute", StringComparison.OrdinalIgnoreCase);
            controls.Phase.IsEnabled = available && !absolute;
            controls.Start.IsEnabled = available && absolute;
        }

        private static void ParsePhcStart(string text, out ulong seconds,
                                          out uint nanoseconds)
        {
            string value = (text ?? string.Empty).Trim();
            int separator = value.IndexOf('.');
            string secondsText = separator < 0 ? value :
                value.Substring(0, separator);
            string fraction = separator < 0 ? string.Empty :
                value.Substring(separator + 1);
            if ((separator >= 0 && value.IndexOf('.', separator + 1) >= 0) ||
                !ulong.TryParse(secondsText, NumberStyles.None,
                    CultureInfo.InvariantCulture, out seconds) ||
                seconds > uint.MaxValue || fraction.Length > 9 ||
                (separator >= 0 && fraction.Length == 0) ||
                fraction.Any(character => character < '0' || character > '9'))
                throw new InvalidOperationException(
                    "Enter an exact future PHC/TAI time as seconds.nanoseconds; seconds must fit the hardware's 32-bit register and the fraction may contain up to nine digits.");

            uint parsedFraction = 0;
            if (fraction.Length != 0 && !uint.TryParse(
                fraction.PadRight(9, '0'), NumberStyles.None,
                CultureInfo.InvariantCulture, out parsedFraction))
                throw new InvalidOperationException(
                    "Enter the PHC nanosecond fraction using decimal digits.");
            nanoseconds = parsedFraction;
        }

        private async void ApplySignalGenerator_Click(object sender,
                                                        RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint generator;
            if (button == null || !uint.TryParse(
                Convert.ToString(button.Tag, CultureInfo.InvariantCulture),
                NumberStyles.Integer, CultureInfo.InvariantCulture,
                out generator) || !EnsureConnected())
                return;
            if (lastSnapshot == null || lastSnapshot.AbiVersion < 5)
            {
                MessageBox.Show(this,
                    "Signal-generator control requires Time Card driver 1.10 / ABI 5.",
                    "Driver update required", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }

            SignalGeneratorControls controls = signalGeneratorControls[generator - 1];
            bool enabled = controls.Enabled.IsChecked == true;
            bool activeHigh = controls.ActiveHigh.IsChecked == true;
            bool absoluteStart = enabled && string.Equals(
                GetComboTag(controls.StartMode), "Absolute",
                StringComparison.OrdinalIgnoreCase);
            ulong period = 0;
            ulong pulse = 0;
            ulong phase = 0;
            ulong startSeconds = 0;
            uint startNanoseconds = 0;
            uint repeat = 0;
            uint cableDelay = 0;
            try
            {
                if (enabled)
                {
                    decimal frequency;
                    if (!decimal.TryParse(controls.Frequency.Text,
                        NumberStyles.Float, CultureInfo.InvariantCulture,
                        out frequency) || frequency < 0.000001m ||
                        frequency > 500000000m)
                        throw new InvalidOperationException(
                            "Enter a generator frequency from 0.000001 through 500000000 Hz.");
                    period = decimal.ToUInt64(decimal.Round(
                        1000000000m / frequency, 0,
                        MidpointRounding.AwayFromZero));
                    uint duty = ParseUnsigned(controls.Duty.Text,
                        "duty cycle", 1, 99);
                    pulse = decimal.ToUInt64(decimal.Round(
                        period * (decimal)duty / 100m, 0,
                        MidpointRounding.AwayFromZero));
                    if (pulse == 0 || pulse >= period)
                        throw new InvalidOperationException(
                            "The selected frequency and duty cycle cannot be represented at one-nanosecond resolution.");
                    if (absoluteStart)
                        ParsePhcStart(controls.Start.Text, out startSeconds,
                            out startNanoseconds);
                    else
                        phase = ParseUnsignedLong(controls.Phase.Text,
                            "phase", 0, period - 1);
                }

                if (lastSnapshot.AbiVersion >= 12)
                {
                    repeat = ParseUnsigned(controls.Repeat.Text,
                        "repeat count", 0, uint.MaxValue);
                    cableDelay = ParseUnsigned(controls.CableDelay.Text,
                        "generator cable delay", 0, 0xffff);
                }

                uint route = SelectedTimingRoute(controls.Route);
                if (route != 0)
                {
                    MessageBoxResult confirmation = MessageBox.Show(this,
                        string.Format(CultureInfo.InvariantCulture,
                            "Route Generator {0} to SMA {1} as an output? Disconnect any external source from this connector first.",
                            generator, route),
                        "Confirm generator output", MessageBoxButton.YesNo,
                        MessageBoxImage.Warning);
                    if (confirmation != MessageBoxResult.Yes)
                        return;
                }

                button.IsEnabled = false;
                SignalGeneratorState state = await Task.Run(() =>
                    absoluteStart ? client.SetSignalGeneratorAt(generator,
                        enabled, period, pulse, activeHigh, repeat, cableDelay,
                        startSeconds, startNanoseconds) :
                    client.SetSignalGenerator(generator, enabled, period,
                        pulse, phase, activeHigh, repeat, cableDelay));
                ApplySignalGeneratorState(state);
                if (route != 0)
                {
                    uint function = 0x0040u << ((int)generator - 1);
                    SmaConnectorState routed = await Task.Run(() =>
                        client.SetSmaConnector(route, SmaDirection.Output,
                                               function));
                    ApplySmaState(routed);
                    controls.Route.SelectedIndex = (int)route;
                }
                Log(string.Format(CultureInfo.InvariantCulture,
                    "Generator {0} {1}{2}{3}.", generator,
                    enabled ? "configured and enabled" : "disabled",
                    absoluteStart ? string.Format(CultureInfo.InvariantCulture,
                        " for exact PHC start {0}.{1:D9}", startSeconds,
                        startNanoseconds) : string.Empty,
                    route == 0 ? string.Empty : " on SMA " + route));
            }
            catch (Exception ex)
            {
                Log(string.Format(CultureInfo.InvariantCulture,
                    "Generator {0} configuration failed: {1}",
                    generator, ex.Message));
                MessageBox.Show(this, ex.Message,
                    "Unable to configure signal generator",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                button.IsEnabled = true;
            }
        }

        private async void ApplyFrequencyCounter_Click(object sender,
                                                        RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint counter;
            if (button == null || !uint.TryParse(
                Convert.ToString(button.Tag, CultureInfo.InvariantCulture),
                NumberStyles.Integer, CultureInfo.InvariantCulture,
                out counter) || !EnsureConnected())
                return;
            if (lastSnapshot == null || lastSnapshot.AbiVersion < 5)
                return;

            FrequencyCounterControls controls = frequencyCounterControls[counter - 1];
            button.IsEnabled = false;
            try
            {
                uint seconds = ParseUnsigned(controls.Seconds.Text,
                    "integration time", 0, 255);
                uint route = SelectedTimingRoute(controls.Route);
                FrequencyCounterState state = await Task.Run(() =>
                    client.SetFrequencyCounter(counter, seconds));
                ApplyFrequencyCounterState(state);
                if (route != 0)
                {
                    uint function = 0x0100u << ((int)counter - 1);
                    SmaConnectorState routed = await Task.Run(() =>
                        client.SetSmaConnector(route, SmaDirection.Input,
                                               function));
                    ApplySmaState(routed);
                    controls.Route.SelectedIndex = (int)route;
                }
                Log(string.Format(CultureInfo.InvariantCulture,
                    "Frequency counter {0} integration set to {1} second(s){2}.",
                    counter, seconds, route == 0 ? string.Empty :
                    " from SMA " + route));
            }
            catch (Exception ex)
            {
                Log(string.Format(CultureInfo.InvariantCulture,
                    "Frequency counter {0} configuration failed: {1}",
                    counter, ex.Message));
                MessageBox.Show(this, ex.Message,
                    "Unable to configure frequency counter",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                button.IsEnabled = true;
            }
        }

        private async void ClearSignalGeneratorStatus_Click(object sender,
                                                              RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint generator;
            if (button == null || !uint.TryParse(
                Convert.ToString(button.Tag, CultureInfo.InvariantCulture),
                NumberStyles.Integer, CultureInfo.InvariantCulture,
                out generator) || !EnsureConnected())
                return;
            if (lastSnapshot == null || lastSnapshot.AbiVersion < 13)
            {
                MessageBox.Show(this,
                    "Clearing signal-generator status requires Time Card driver 1.40 / ABI 13.",
                    "Driver update required", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }

            bool completed = false;
            button.IsEnabled = false;
            try
            {
                SignalGeneratorState state = await Task.Run(() =>
                    client.ClearSignalGeneratorStatus(generator));
                ApplySignalGeneratorState(state);
                completed = true;
                Log(string.Format(CultureInfo.InvariantCulture,
                    "Generator {0} sticky status clear requested.", generator));
            }
            catch (Exception ex)
            {
                Log(string.Format(CultureInfo.InvariantCulture,
                    "Generator {0} status clear failed: {1}", generator,
                    ex.Message));
                MessageBox.Show(this, ex.Message,
                    "Unable to clear signal-generator status",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                if (!completed)
                    button.IsEnabled = client != null &&
                        lastSnapshot != null &&
                        lastSnapshot.AbiVersion >= 13;
            }
        }

        private void ApplySignalGeneratorState(SignalGeneratorState state)
        {
            SignalGeneratorControls controls = signalGeneratorControls[state.Generator - 1];
            bool available = state.IsPresent;
            controls.Enabled.IsChecked = state.IsEnabled;
            controls.ActiveHigh.IsChecked = state.IsActiveHigh;
            controls.Repeat.Text = state.RepeatCount.ToString(
                CultureInfo.InvariantCulture);
            controls.CableDelay.Text = state.CableDelayNanoseconds.ToString(
                CultureInfo.InvariantCulture);
            controls.Start.Text = string.Format(CultureInfo.InvariantCulture,
                "{0}.{1:D9}", state.StartSeconds, state.StartNanoseconds);
            if (state.PeriodNanoseconds != 0)
            {
                controls.Frequency.Text = state.FrequencyHz.ToString(
                    "0.#########", CultureInfo.InvariantCulture);
                controls.Duty.Text = Math.Round(state.DutyPercent).ToString(
                    "0", CultureInfo.InvariantCulture);
                controls.Phase.Text = state.PhaseNanoseconds.ToString(
                    CultureInfo.InvariantCulture);
            }
            controls.Status.Text = state.HasError ?
                string.Format(CultureInfo.InvariantCulture,
                    "FAULT 0x{0:X8}", state.Status) :
                state.HasTimeJump ? "TIME JUMP" :
                state.IsEnabled ? "RUNNING" : "DISABLED";
            controls.Status.Foreground = state.HasError ?
                (Brush)FindResource("DangerBrush") :
                state.HasTimeJump ? (Brush)FindResource("GoldBrush") :
                state.IsEnabled ? (Brush)FindResource("AccentBrush") :
                (Brush)FindResource("MutedBrush");
            controls.Detail.Text = string.Format(CultureInfo.InvariantCulture,
                "Period {0:N0} ns · Start {1}.{2:D9} TAI · v{3:X8}",
                state.PeriodNanoseconds, state.StartSeconds,
                state.StartNanoseconds, state.Version) + string.Format(
                    CultureInfo.InvariantCulture,
                    " | Repeat {0} | Delay {1} ns", state.RepeatCount,
                    state.CableDelayNanoseconds);
            controls.Enabled.IsEnabled = available;
            controls.Frequency.IsEnabled = available;
            controls.Duty.IsEnabled = available;
            controls.ActiveHigh.IsEnabled = available;
            controls.Repeat.IsEnabled = available && lastSnapshot != null &&
                lastSnapshot.AbiVersion >= 12;
            controls.CableDelay.IsEnabled = available && lastSnapshot != null &&
                lastSnapshot.AbiVersion >= 12;
            controls.StartMode.IsEnabled = available;
            UpdateSignalScheduleControls(controls);
            controls.Route.IsEnabled = available;
            controls.Clear.IsEnabled = available && lastSnapshot != null &&
                lastSnapshot.AbiVersion >= 13 &&
                (state.HasError || state.HasTimeJump);
            controls.Apply.IsEnabled = available;
        }

        private void ApplyFrequencyCounterState(FrequencyCounterState state)
        {
            FrequencyCounterControls controls = frequencyCounterControls[state.Counter - 1];
            bool available = state.IsPresent;
            if (state.IsEnabled)
                controls.Seconds.Text = state.IntegrationSeconds.ToString(
                    CultureInfo.InvariantCulture);
            controls.Value.Text = state.IsValid ?
                state.FrequencyHz.ToString("N0", CultureInfo.InvariantCulture) + " Hz" :
                "— Hz";
            controls.Status.Text = state.HasError ? "ERROR" :
                state.HasOverrun ? "OVERRUN" :
                state.IsValid ? "VALID" :
                state.IsEnabled ? "MEASURING" : "DISABLED";
            controls.Status.Foreground = state.HasError || state.HasOverrun ?
                (Brush)FindResource("DangerBrush") :
                state.IsValid ? (Brush)FindResource("AccentBrush") :
                (Brush)FindResource("MutedBrush");
            controls.Detail.Text = string.Format(CultureInfo.InvariantCulture,
                "Control 0x{0:X8} · Status 0x{1:X8}",
                state.Control, state.Status);
            controls.Seconds.IsEnabled = available;
            controls.Route.IsEnabled = available;
            controls.Apply.IsEnabled = available;
        }

        private void ApplyTimingRoutes(IEnumerable<SmaConnectorState> routes)
        {
            foreach (SignalGeneratorControls controls in signalGeneratorControls)
                controls.Route.SelectedIndex = 0;
            foreach (FrequencyCounterControls controls in frequencyCounterControls)
                controls.Route.SelectedIndex = 0;
            foreach (SmaConnectorState route in routes.Where(value => value != null))
            {
                if (route.Direction == SmaDirection.Output &&
                    route.Function >= 0x0040u && route.Function <= 0x0200u)
                {
                    for (int index = 0; index < 4; index++)
                    {
                        if (route.Function == (0x0040u << index))
                            signalGeneratorControls[index].Route.SelectedIndex =
                                (int)route.Connector;
                    }
                }
                if (route.Direction == SmaDirection.Input &&
                    route.Function >= 0x0100u && route.Function <= 0x0800u)
                {
                    for (int index = 0; index < 4; index++)
                    {
                        if (route.Function == (0x0100u << index))
                            frequencyCounterControls[index].Route.SelectedIndex =
                                (int)route.Connector;
                    }
                }
            }
        }

        private void SetTimingUnavailable(string status)
        {
            for (uint channel = 1; channel <= 4; channel++)
            {
                SetSignalGeneratorUnavailable(channel, status);
                SetFrequencyCounterUnavailable(channel, status);
            }
        }

        private void SetSignalGeneratorUnavailable(uint generator, string status)
        {
            SignalGeneratorControls controls = signalGeneratorControls[generator - 1];
            controls.Status.Text = status;
            controls.Status.Foreground = (Brush)FindResource("GoldBrush");
            controls.Enabled.IsEnabled = false;
            controls.Frequency.IsEnabled = false;
            controls.Duty.IsEnabled = false;
            controls.Phase.IsEnabled = false;
            controls.ActiveHigh.IsEnabled = false;
            controls.Repeat.IsEnabled = false;
            controls.CableDelay.IsEnabled = false;
            controls.StartMode.IsEnabled = false;
            controls.Start.IsEnabled = false;
            controls.Route.IsEnabled = false;
            controls.Clear.IsEnabled = false;
            controls.Apply.IsEnabled = false;
        }

        private void SetFrequencyCounterUnavailable(uint counter, string status)
        {
            FrequencyCounterControls controls = frequencyCounterControls[counter - 1];
            controls.Status.Text = status;
            controls.Status.Foreground = (Brush)FindResource("GoldBrush");
            controls.Value.Text = "— Hz";
            controls.Seconds.IsEnabled = false;
            controls.Route.IsEnabled = false;
            controls.Apply.IsEnabled = false;
        }

        private static uint SelectedTimingRoute(ComboBox combo)
        {
            ComboBoxItem item = combo.SelectedItem as ComboBoxItem;
            uint route;
            return item != null && uint.TryParse(
                Convert.ToString(item.Tag, CultureInfo.InvariantCulture),
                NumberStyles.Integer, CultureInfo.InvariantCulture,
                out route) ? route : 0;
        }

        private static ulong ParseUnsignedLong(string text, string name,
                                               ulong minimum, ulong maximum)
        {
            ulong value;
            if (!ulong.TryParse(text, NumberStyles.Integer,
                CultureInfo.InvariantCulture, out value) ||
                value < minimum || value > maximum)
                throw new InvalidOperationException(string.Format(
                    CultureInfo.InvariantCulture,
                    "Enter a valid {0} between {1} and {2}.",
                    name, minimum, maximum));
            return value;
        }

        private async void RefreshI2c_Click(object sender, RoutedEventArgs e)
        {
            await RefreshI2cAsync(false);
        }

        private async void ScanI2c_Click(object sender, RoutedEventArgs e)
        {
            await RefreshI2cAsync(true);
        }

        private async Task RefreshI2cAsync(bool fullScan)
        {
            if (i2cRefreshing)
                return;
            if (client == null)
            {
                SetI2cUnavailable("NOT CONNECTED", "Connect to the Time Card driver first.");
                return;
            }
            if (lastSnapshot != null && lastSnapshot.AbiVersion < 3)
            {
                SetI2cUnavailable("DRIVER UPDATE REQUIRED",
                    "Install Time Card driver 1.8 or later to enable I2C access.");
                return;
            }

            i2cRefreshing = true;
            I2cRefreshButton.IsEnabled = false;
            I2cScanButton.IsEnabled = false;
            I2cControllerStateText.Text = fullScan ? "SCANNING" : "REFRESHING";
            I2cScanResultText.Text = fullScan ?
                "Probing 7-bit addresses 0x08 through 0x77..." :
                lastSnapshot != null && lastSnapshot.Layout == "Orolia ART" ?
                    "Probing the ART 24c08 EEPROM banks at 0x50 through 0x57..." :
                    "Probing the two declared onboard devices...";
            TimeCardClient activeClient = client;
            try
            {
                I2cRefreshResult result = await Task.Run(() =>
                {
                    I2cRefreshResult value = new I2cRefreshResult
                    {
                        Addresses = new List<uint>(),
                        FullScan = fullScan,
                        IsArt = lastSnapshot != null &&
                            lastSnapshot.Layout == "Orolia ART"
                    };
                    if (!value.IsArt && lastSnapshot != null &&
                        lastSnapshot.AbiVersion >= 7)
                        value.Mux = activeClient.GetI2cMux();
                    uint first = fullScan ? 0x08u : 0x50u;
                    uint last = fullScan ? 0x77u :
                        value.IsArt ? 0x57u : 0x58u;
                    for (uint address = first; address <= last; address++)
                    {
                        if (!fullScan && !value.IsArt &&
                            address != 0x50u && address != 0x58u)
                            continue;
                        I2cProbeResult probe = activeClient.ProbeI2c(address);
                        if (address == 0x50u)
                            value.BoardEeprom = probe;
                        else if (!value.IsArt && address == 0x58u)
                            value.MacEeprom = probe;
                        if (probe.IsPresent)
                            value.Addresses.Add(address);
                    }
                    value.Status = activeClient.GetI2cStatus();
                    return value;
                });
                if (client != activeClient)
                    return;
                ApplyI2cRefresh(result);
                await RefreshIdentityAsync();
                Log(fullScan ? "I2C address scan completed." :
                    "I2C controller and onboard devices refreshed.");
            }
            catch (Exception ex)
            {
                SetI2cUnavailable("I2C UNAVAILABLE", ex.Message);
                Log("I2C refresh failed: " + ex.Message);
            }
            finally
            {
                i2cRefreshing = false;
                bool supported = client != null &&
                    (lastSnapshot == null || lastSnapshot.AbiVersion >= 3);
                I2cRefreshButton.IsEnabled = supported;
                I2cScanButton.IsEnabled = supported;
            }
        }

        private void ApplyI2cRefresh(I2cRefreshResult result)
        {
            Brush healthyBrush = (Brush)FindResource("AccentBrush");
            Brush warningBrush = (Brush)FindResource("GoldBrush");
            I2cControllerStatus status = result.Status;
            bool readsSupported = SupportsReliableI2cReads();

            if (result.IsArt)
            {
                I2cMuxStatusText.Text = "NOT FITTED ON ART";
                I2cMuxStatusText.Foreground = warningBrush;
            }
            else
            {
                ApplyI2cMuxState(result.Mux);
            }

            I2cControllerStateText.Text = status.IsPresent ?
                (status.IsEnabled ? "ENABLED" : "PRESENT") : "NOT PRESENT";
            I2cControllerStateText.Foreground = status.IsPresent ? healthyBrush : warningBrush;
            I2cControllerDetailText.Text = string.Format(CultureInfo.InvariantCulture,
                "AXI IIC · ISR 0x{0:X2} · IER 0x{1:X2}",
                status.InterruptStatus, status.InterruptEnable);
            I2cBusStateText.Text = status.IsBusBusy ? "BUSY" : "IDLE";
            I2cBusStateText.Foreground = status.IsBusBusy ? warningBrush : healthyBrush;
            I2cBusDetailText.Text = string.Format(CultureInfo.InvariantCulture,
                "TX FIFO {0} · RX FIFO {1}", status.TxFifoOccupancy,
                status.RxFifoOccupancy);
            I2cOffsetText.Text = string.Format(CultureInfo.InvariantCulture,
                "0x{0:X8}", status.Offset);
            I2cRegisterText.Text = string.Format(CultureInfo.InvariantCulture,
                "CR 0x{0:X2} · SR 0x{1:X2}", status.Control, status.Status);

            I2cBoardStatusText.Text = result.BoardEeprom != null &&
                result.BoardEeprom.IsPresent ? "PRESENT" : "NO ACK";
            I2cBoardStatusText.Foreground = result.BoardEeprom != null &&
                result.BoardEeprom.IsPresent ? healthyBrush : warningBrush;
            I2cMacStatusText.Text = result.IsArt ? "NOT FITTED" :
                result.MacEeprom != null &&
                result.MacEeprom.IsPresent ? "PRESENT" : "NO ACK";
            I2cMacStatusText.Foreground = result.IsArt ? warningBrush :
                result.MacEeprom != null &&
                result.MacEeprom.IsPresent ? healthyBrush : warningBrush;

            I2cDeviceCountText.Text = result.Addresses.Count.ToString(
                CultureInfo.InvariantCulture);
            I2cScanResultText.Text = result.Addresses.Count == 0 ?
                "No addresses acknowledged" :
                string.Join(", ", result.Addresses.Select(address =>
                    string.Format(CultureInfo.InvariantCulture, "0x{0:X2}", address)));
            I2cAddressMapText.Text = FormatI2cAddressMap(result);
            I2cDiagnosticsText.Text = FormatI2cControllerDiagnostics(
                status, result.FullScan ? "Full 7-bit scan completed." :
                result.IsArt ? "ART 24c08 bank probe completed." :
                    "Known-device probe completed.");
            I2cReadButton.IsEnabled = readsSupported;
            I2cPreviousButton.IsEnabled = readsSupported;
            I2cNextButton.IsEnabled = readsSupported;
            I2cBoardReadButton.IsEnabled = readsSupported &&
                result.BoardEeprom != null &&
                result.BoardEeprom.IsPresent;
            I2cMacReadButton.IsEnabled = readsSupported &&
                result.MacEeprom != null &&
                result.MacEeprom.IsPresent;
            if (!readsSupported)
                I2cReadStatusText.Text = "Install Time Card driver 1.14 to enable reliable I\u00B2C reads.";
        }

        private void UpdateI2cDriverCompatibility(TimeCardSnapshot snapshot)
        {
            bool abiSupported = snapshot.AbiVersion >= 3;
            bool reliableReads = abiSupported &&
                DriverVersionAtLeast(snapshot.DriverVersion, 1, 14);
            bool isArt = snapshot.Layout == "Orolia ART";
            bool boardControls = !isArt &&
                snapshot.AbiVersion >= 7 && reliableReads;
            Brush stateBrush = (Brush)FindResource(
                boardControls ? "AccentBrush" : "GoldBrush");

            I2cModeIcon.Foreground = stateBrush;
            I2cDriverBadgeText.Foreground = stateBrush;
            I2cLedAutoCheckBox.IsEnabled = boardControls;
            I2cLedManualPanel.IsEnabled = boardControls &&
                I2cLedAutoCheckBox.IsChecked != true;
            I2cMuxPanel.IsEnabled = !isArt && boardControls;
            I2cMuxPanel.Opacity = isArt ? 0.55 : 1.0;
            I2cLedPanel.IsEnabled = boardControls;
            I2cLedPanel.Opacity = isArt ? 0.55 : 1.0;
            foreach (ComboBoxItem preset in
                I2cPresetCombo.Items.OfType<ComboBoxItem>())
            {
                string tag = Convert.ToString(preset.Tag,
                    CultureInfo.InvariantCulture);
                preset.IsEnabled = !isArt ||
                    tag == "custom" || tag == "board" || tag == "direct";
            }
            if (isArt && reliableReads)
            {
                I2cLedAutoCheckBox.IsChecked = false;
                I2cDriverBadgeText.Text = "OROLIA ART \u00B7 READS";
                I2cSafetyBannerText.Text =
                    "Direct OpenCores I\u00B2C at BAR + 0x00350000 exposes the ART 24c08 EEPROM banks at 0x50-0x57. This board profile has no PCA9546A sensor mux or IS32FL3207 LEDs.";
                I2cLedOperationText.Text =
                    "Status LEDs are not fitted on the Orolia ART profile.";
                return;
            }
            if (boardControls)
            {
                I2cDriverBadgeText.Text = "DRIVER " + snapshot.DriverVersion;
                I2cSafetyBannerText.Text =
                    "Guarded mode allows reads plus dedicated PCA9546A routing and IS32FL3207 LED updates. Arbitrary I\u00B2C writes remain blocked.";
                return;
            }

            if (reliableReads)
            {
                I2cDriverBadgeText.Text = "READS ONLY · UPDATE";
                I2cSafetyBannerText.Text = string.Format(
                    CultureInfo.InvariantCulture,
                    "Driver {0} supports legacy reads. Install driver 1.14 / ABI 7 for corrected PCA9546A, identity, and subsystem LED control.",
                    snapshot.DriverVersion);
                return;
            }

            I2cDriverBadgeText.Text = "UPDATE REQUIRED";
            I2cSafetyBannerText.Text = abiSupported ? string.Format(
                CultureInfo.InvariantCulture,
                "Driver {0} uses the legacy AXI IIC sequence that Windows can report as a CRC data error. Install driver 1.14 before reading.",
                snapshot.DriverVersion) :
                "This driver predates corrected I\u00B2C control support. Install Time Card driver 1.14 before using the bus workspace.";
            I2cReadButton.IsEnabled = false;
            I2cPreviousButton.IsEnabled = false;
            I2cNextButton.IsEnabled = false;
            I2cBoardReadButton.IsEnabled = false;
            I2cMacReadButton.IsEnabled = false;
            I2cReadStatusText.Text = "Driver update required for I\u00B2C reads.";
        }

        private bool SupportsReliableI2cReads()
        {
            return client != null && lastSnapshot != null &&
                lastSnapshot.AbiVersion >= 3 &&
                DriverVersionAtLeast(lastSnapshot.DriverVersion, 1, 14);
        }

        private bool SupportsI2cBoardControls()
        {
            return client != null && lastSnapshot != null &&
                lastSnapshot.Layout != "Orolia ART" &&
                lastSnapshot.AbiVersion >= 7 &&
                DriverVersionAtLeast(lastSnapshot.DriverVersion, 1, 14);
        }

        private static bool DriverVersionAtLeast(string value, int major,
                                                 int minor)
        {
            Version installed;
            return Version.TryParse(value, out installed) &&
                installed.CompareTo(new Version(major, minor)) >= 0;
        }

        private void ApplyI2cMuxState(I2cMuxState state)
        {
            Brush healthyBrush = (Brush)FindResource("AccentBrush");
            Brush warningBrush = (Brush)FindResource("GoldBrush");
            if (state == null)
            {
                I2cMuxStatusText.Text = "DRIVER 1.14 / ABI 7 REQUIRED";
                I2cMuxStatusText.Foreground = warningBrush;
                return;
            }
            if (!state.IsPresent)
            {
                I2cMuxStatusText.Text = "NO ACK AT 0x70";
                I2cMuxStatusText.Foreground = warningBrush;
                return;
            }

            I2cMuxStatusText.Text = state.ChannelMask == 0 ?
                "ALL CHANNELS DISCONNECTED" :
                "ACTIVE · " + FormatI2cMuxMask(state.ChannelMask);
            I2cMuxStatusText.Foreground = healthyBrush;
        }

        private static string FormatI2cMuxMask(uint mask)
        {
            List<string> channels = new List<string>();
            if ((mask & 1u) != 0)
                channels.Add("CH0 MAC");
            if ((mask & 2u) != 0)
                channels.Add("CH1 SENSORS");
            if ((mask & 4u) != 0)
                channels.Add("CH2 AN/ADC");
            if ((mask & 8u) != 0)
                channels.Add("CH3 DC");
            return channels.Count == 0 ? "NONE" : string.Join(" + ", channels);
        }

        private async void SelectI2cMux_Click(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint channelMask;
            if (button == null || !uint.TryParse(
                Convert.ToString(button.Tag, CultureInfo.InvariantCulture),
                NumberStyles.Integer, CultureInfo.InvariantCulture,
                out channelMask))
                return;
            if (!SupportsI2cBoardControls())
            {
                MessageBox.Show(this,
                    "Install Time Card driver 1.14 / ABI 7 to control the PCA9546A.",
                    "Driver update required", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }

            TimeCardClient activeClient = client;
            button.IsEnabled = false;
            I2cMuxStatusText.Text = "APPLYING ROUTE…";
            try
            {
                I2cMuxState state = await Task.Run(
                    () => activeClient.SetI2cMux(channelMask));
                if (client != activeClient)
                    return;
                ApplyI2cMuxState(state);
                Log("I2C mux route set to " + FormatI2cMuxMask(channelMask) + ".");
            }
            catch (Exception ex)
            {
                I2cMuxStatusText.Text = "ROUTE FAILED";
                I2cMuxStatusText.Foreground = (Brush)FindResource("GoldBrush");
                I2cLedOperationText.Text = ex.Message;
                Log("I2C mux route failed: " + ex.Message);
            }
            finally
            {
                button.IsEnabled = true;
            }
        }

        private async void UpdateBoardLedAutomationAsync(bool force)
        {
            if (I2cLedAutoCheckBox == null ||
                I2cLedAutoCheckBox.IsChecked != true || ledAutomationUpdating)
                return;
            if (!SupportsI2cBoardControls())
            {
                I2cLedOperationText.Text =
                    "Automatic LED mapping requires Time Card driver 1.14 / ABI 7.";
                return;
            }

            ledAutomationUpdating = true;
            TimeCardClient activeClient = client;
            try
            {
                DateTime now = DateTime.UtcNow;
                if (lastSnapshot != null && lastSnapshot.AbiVersion >= 2 &&
                    now - lastSmaLedRefreshUtc >= TimeSpan.FromSeconds(10))
                {
                    lastSmaLedRefreshUtc = now;
                    SmaConnectorState[] routes = await Task.Run(() =>
                    {
                        SmaConnectorState[] values = new SmaConnectorState[4];
                        for (uint connector = 1; connector <= 4; connector++)
                        {
                            try
                            {
                                values[(int)connector - 1] =
                                    activeClient.GetSmaConnector(connector);
                            }
                            catch
                            {
                                values[(int)connector - 1] = null;
                            }
                        }
                        return values;
                    });
                    if (client != activeClient)
                        return;
                    for (int index = 0; index < routes.Length; index++)
                        lastSmaLedStates[index] = routes[index];
                }

                if (lastSnapshot != null && lastSnapshot.AbiVersion >= 6 &&
                    now - lastSecondaryLedObservationUtc >=
                    TimeSpan.FromSeconds(10))
                {
                    lastSecondaryLedObservationUtc = now;
                    try
                    {
                        UartObservation observation = await Task.Run(
                            () => activeClient.ObserveUart(1, 1500));
                        if (client != activeClient)
                            return;
                        secondaryGnssPresent = observation.IsPresent &&
                            observation.HasActivity;
                    }
                    catch
                    {
                        secondaryGnssPresent = false;
                    }
                }

                BoardLedColor[] desired = BuildAutomaticLedColors();
                for (int index = 0; index < desired.Length; index++)
                    ApplyBoardLedVisual(index, desired[index]);
                if (force)
                    Array.Clear(lastAppliedLedColors, 0,
                        lastAppliedLedColors.Length);

                List<int> changed = new List<int>();
                for (int index = 0; index < desired.Length; index++)
                {
                    if (!desired[index].SameColor(lastAppliedLedColors[index]))
                        changed.Add(index);
                }
                if (changed.Count == 0)
                {
                    I2cLedOperationText.Text = boardLedHardwareWarning ??
                        "Automatic mapping is active · hardware colors are current.";
                    return;
                }

                BoardLedState[] applied = await Task.Run(() =>
                {
                    BoardLedState[] states = new BoardLedState[changed.Count];
                    for (int resultIndex = 0;
                         resultIndex < changed.Count; resultIndex++)
                    {
                        int index = changed[resultIndex];
                        BoardLedColor color = desired[index];
                        states[resultIndex] = activeClient.SetBoardLed(
                            (uint)index, color.Red,
                            color.Green, color.Blue, 96);
                    }
                    return states;
                });
                if (client != activeClient)
                    return;
                foreach (int index in changed)
                    lastAppliedLedColors[index] = desired[index];
                List<uint> faulted = new List<uint>();
                foreach (BoardLedState state in applied)
                {
                    bool hasFault = HasBoardLedFault(state);
                    boardLedHardwareFaults[state.Led] = hasFault;
                    if (!hasFault)
                        continue;
                    faulted.Add(state.Led + 1);
                    TextBlock status = GetBoardLedStatusText((int)state.Led);
                    status.Text = "HARDWARE FAULT";
                    status.Foreground = (Brush)FindResource("GoldBrush");
                }
                if (faulted.Count != 0)
                {
                    boardLedHardwareWarning = string.Format(
                        CultureInfo.InvariantCulture,
                        "IS32FL3207 accepted the colors, but LED {0} report electrical open/short faults. Check the +3.3 V common-anode rail and D13-D18 assembly.",
                        string.Join(", ", faulted));
                    I2cLedOperationText.Text = boardLedHardwareWarning;
                }
                else
                {
                    boardLedHardwareWarning = null;
                    I2cLedOperationText.Text = string.Format(
                        CultureInfo.InvariantCulture,
                        "Automatic mapping updated {0} indicator{1}; mux route restored after each update.",
                        changed.Count,
                        changed.Count == 1 ? string.Empty : "s");
                }
            }
            catch (Exception ex)
            {
                I2cLedOperationText.Text = "Automatic LED update failed: " + ex.Message;
                Log("Automatic subsystem LED update failed: " + ex.Message);
            }
            finally
            {
                ledAutomationUpdating = false;
            }
        }

        private BoardLedColor[] BuildAutomaticLedColors()
        {
            BoardLedColor[] colors = new BoardLedColor[6];
            TimeCardSnapshot snapshot = lastSnapshot;
            colors[0] = snapshot != null && snapshot.GnssFixOk ?
                new BoardLedColor(0, 180, 30, "FIX LOCKED") :
                snapshot != null && snapshot.SatelliteDataValid &&
                snapshot.SeenSatellites != 0 ?
                    new BoardLedColor(170, 78, 0, "SEARCHING") :
                    new BoardLedColor(180, 0, 0, "NO FIX");
            colors[1] = secondaryGnssPresent == true ?
                new BoardLedColor(0, 165, 30, "UART ACTIVE") :
                secondaryGnssPresent == false ?
                    new BoardLedColor(180, 0, 0, "NOT PRESENT") :
                    new BoardLedColor(145, 64, 0, "UART UNKNOWN");

            for (int index = 0; index < 4; index++)
            {
                SmaConnectorState state = lastSmaLedStates[index];
                if (state == null)
                    colors[index + 2] = new BoardLedColor(130, 55, 0, "UNKNOWN");
                else if (!state.IsPresent)
                    colors[index + 2] = new BoardLedColor(180, 0, 0, "NOT PRESENT");
                else if (state.IsDisabled || state.Direction == SmaDirection.Disabled)
                    colors[index + 2] = new BoardLedColor(50, 35, 0, "DISABLED");
                else if (state.Direction == SmaDirection.Output)
                    colors[index + 2] = new BoardLedColor(0, 165, 30, "OUTPUT");
                else
                    colors[index + 2] = new BoardLedColor(0, 65, 180, "INPUT");
            }
            return colors;
        }

        private void ApplyBoardLedVisual(int led, BoardLedColor color)
        {
            Border swatch = GetBoardLedSwatch(led);
            TextBlock status = GetBoardLedStatusText(led);
            swatch.Background = new SolidColorBrush(Color.FromRgb(
                color.Red, color.Green, color.Blue));
            status.Text = color.Status;
            status.Foreground = (Brush)FindResource("MutedBrush");
            if (boardLedHardwareFaults[led])
            {
                status.Text = "HARDWARE FAULT";
                status.Foreground = (Brush)FindResource("GoldBrush");
            }
        }

        private Border GetBoardLedSwatch(int led)
        {
            return led == 0 ? I2cLedGnss1Swatch :
                led == 1 ? I2cLedGnss2Swatch :
                led == 2 ? I2cLedIo1Swatch :
                led == 3 ? I2cLedIo2Swatch :
                led == 4 ? I2cLedIo3Swatch : I2cLedIo4Swatch;
        }

        private TextBlock GetBoardLedStatusText(int led)
        {
            return led == 0 ? I2cLedGnss1StatusText :
                led == 1 ? I2cLedGnss2StatusText :
                led == 2 ? I2cLedIo1StatusText :
                led == 3 ? I2cLedIo2StatusText :
                led == 4 ? I2cLedIo3StatusText : I2cLedIo4StatusText;
        }

        private void I2cLedAuto_Click(object sender, RoutedEventArgs e)
        {
            bool automatic = I2cLedAutoCheckBox.IsChecked == true;
            I2cLedManualPanel.IsEnabled = SupportsI2cBoardControls() && !automatic;
            if (automatic)
            {
                Array.Clear(lastAppliedLedColors, 0,
                    lastAppliedLedColors.Length);
                UpdateBoardLedAutomationAsync(true);
            }
            else
            {
                I2cLedOperationText.Text =
                    "Automatic mapping paused. Select an indicator and apply a manual color.";
            }
        }

        private void I2cLedSelection_SelectionChanged(
            object sender, SelectionChangedEventArgs e)
        {
            if (I2cLedSelectionCombo != null && IsInitialized &&
                I2cLedAutoCheckBox.IsChecked != true)
                I2cLedOperationText.Text = "Read the selected indicator or apply a new color.";
        }

        private void I2cLedSlider_ValueChanged(object sender,
                                                RoutedPropertyChangedEventArgs<double> e)
        {
            if (ledControlsUpdating || I2cLedRedSlider == null ||
                I2cLedGreenSlider == null || I2cLedBlueSlider == null ||
                I2cLedCurrentSlider == null || I2cLedPreview == null ||
                I2cLedRedValueText == null || I2cLedGreenValueText == null ||
                I2cLedBlueValueText == null || I2cLedCurrentValueText == null)
                return;
            byte red = (byte)Math.Round(I2cLedRedSlider.Value);
            byte green = (byte)Math.Round(I2cLedGreenSlider.Value);
            byte blue = (byte)Math.Round(I2cLedBlueSlider.Value);
            I2cLedRedValueText.Text = red.ToString(CultureInfo.InvariantCulture);
            I2cLedGreenValueText.Text = green.ToString(CultureInfo.InvariantCulture);
            I2cLedBlueValueText.Text = blue.ToString(CultureInfo.InvariantCulture);
            I2cLedCurrentValueText.Text = Math.Round(
                I2cLedCurrentSlider.Value).ToString(CultureInfo.InvariantCulture);
            I2cLedPreview.Background = new SolidColorBrush(
                Color.FromRgb(red, green, blue));
        }

        private async void ReadI2cLed_Click(object sender, RoutedEventArgs e)
        {
            if (!SupportsI2cBoardControls())
                return;
            uint led = SelectedBoardLed();
            TimeCardClient activeClient = client;
            I2cLedOperationText.Text = "Reading selected indicator…";
            try
            {
                BoardLedState state = await Task.Run(
                    () => activeClient.GetBoardLed(led));
                if (client != activeClient)
                    return;
                ApplyManualBoardLedState(state);
                I2cLedOperationText.Text = string.Format(
                    CultureInfo.InvariantCulture,
                    "LED {0} read · RGB {1}/{2}/{3} · global current {4} · {5}.",
                    led + 1, state.Red, state.Green, state.Blue,
                    state.GlobalCurrent, DescribeBoardLedFaults(state));
            }
            catch (Exception ex)
            {
                I2cLedOperationText.Text = "LED read failed: " + ex.Message;
            }
        }

        private async void ApplyI2cLed_Click(object sender, RoutedEventArgs e)
        {
            await SetManualBoardLedAsync(false);
        }

        private async void TurnOffI2cLed_Click(object sender, RoutedEventArgs e)
        {
            await SetManualBoardLedAsync(true);
        }

        private async void TestI2cLeds_Click(object sender, RoutedEventArgs e)
        {
            if (!SupportsI2cBoardControls() || ledAutomationUpdating)
                return;

            TimeCardClient activeClient = client;
            BoardLedElectricalTestResult test = null;
            string resultText = null;
            ledAutomationUpdating = true;
            SetI2cLedButtonsEnabled(false);
            I2cLedTestButton.IsEnabled = false;
            I2cLedOperationText.Text =
                "Preparing the guarded IS32FL3207 electrical test…";
            try
            {
                test = await Task.Run(() =>
                {
                    BoardLedElectricalTestResult value =
                        new BoardLedElectricalTestResult
                        {
                            Saved = new BoardLedState[6]
                        };
                    for (uint led = 0; led < 6; led++)
                        value.Saved[led] = activeClient.GetBoardLed(led);

                    value.Reset = activeClient.SetBoardLed(
                        0, 255, 255, 255, 128, false, true);
                    for (uint led = 1; led < 6; led++)
                        activeClient.SetBoardLed(
                            led, 255, 255, 255, 128);
                    value.Output = activeClient.SetBoardLed(
                        0, 255, 255, 255, 128, true);
                    return value;
                });
                if (client == activeClient)
                    I2cLedOperationText.Text =
                        "Electrical test active · all 18 outputs forced on for five seconds.";
                await Task.Delay(TimeSpan.FromSeconds(5));

                if (!test.Reset.IsSdbHigh)
                    resultText =
                        "Electrical test failed: the IS32FL3207 SDB pin is low. Check R109 and the U6 shutdown net.";
                else if (HasAnyBoardLedFault(test.Output))
                    resultText = string.Format(
                        CultureInfo.InvariantCulture,
                        "Electrical test: SDB is high and DC mode was accepted, but U6 reports open 0x{0:X5} / short 0x{1:X5}. Inspect the +3.3 V common-anode rail and D13-D18 orientation/soldering.",
                        test.Output.OpenOutputMask,
                        test.Output.ShortOutputMask);
                else
                    resultText =
                        "Electrical test passed: SDB is high and all 18 LED outputs are electrically connected.";
            }
            catch (Exception ex)
            {
                resultText = "LED electrical test failed: " + ex.Message;
            }
            finally
            {
                if (test != null && test.Saved != null)
                {
                    try
                    {
                        await Task.Run(() =>
                        {
                            for (uint led = 0; led < test.Saved.Length; led++)
                            {
                                BoardLedState saved = test.Saved[led];
                                byte current = (byte)Math.Max(
                                    1, (int)saved.GlobalCurrent);
                                activeClient.SetBoardLed(
                                    led, saved.Red, saved.Green,
                                    saved.Blue, current);
                            }
                        });
                    }
                    catch (Exception ex)
                    {
                        resultText = (resultText ?? "LED test completed.") +
                            " Restore failed: " + ex.Message;
                    }
                }
                Array.Clear(lastAppliedLedColors, 0,
                    lastAppliedLedColors.Length);
                boardLedHardwareWarning = resultText;
                ledAutomationUpdating = false;
                if (client == activeClient)
                {
                    SetI2cLedButtonsEnabled(true);
                    I2cLedTestButton.IsEnabled = true;
                    I2cLedOperationText.Text = resultText ??
                        "LED electrical test completed.";
                }
            }
        }

        private async Task SetManualBoardLedAsync(bool turnOff)
        {
            if (!SupportsI2cBoardControls())
                return;
            uint led = SelectedBoardLed();
            byte red = turnOff ? (byte)0 :
                (byte)Math.Round(I2cLedRedSlider.Value);
            byte green = turnOff ? (byte)0 :
                (byte)Math.Round(I2cLedGreenSlider.Value);
            byte blue = turnOff ? (byte)0 :
                (byte)Math.Round(I2cLedBlueSlider.Value);
            byte current = (byte)Math.Round(I2cLedCurrentSlider.Value);
            TimeCardClient activeClient = client;
            SetI2cLedButtonsEnabled(false);
            I2cLedOperationText.Text = turnOff ?
                "Turning off selected indicator…" : "Applying selected color…";
            try
            {
                BoardLedState state = await Task.Run(() =>
                    activeClient.SetBoardLed(led, red, green, blue, current));
                if (client != activeClient)
                    return;
                ApplyManualBoardLedState(state);
                I2cLedOperationText.Text = string.Format(
                    CultureInfo.InvariantCulture,
                    "LED {0} {5} · RGB {1}/{2}/{3} · {4}; previous I2C route restored.",
                    led + 1, state.Red, state.Green, state.Blue,
                    DescribeBoardLedFaults(state),
                    state.IsPresent ? "updated" : "not fitted");
                if (state.IsPresent)
                    Log(string.Format(CultureInfo.InvariantCulture,
                        "Subsystem LED {0} set to RGB {1}/{2}/{3}.",
                        led + 1, state.Red, state.Green, state.Blue));
            }
            catch (Exception ex)
            {
                I2cLedOperationText.Text = "LED update failed: " + ex.Message;
                Log("Subsystem LED update failed: " + ex.Message);
            }
            finally
            {
                SetI2cLedButtonsEnabled(true);
            }
        }

        private uint SelectedBoardLed()
        {
            uint led;
            return uint.TryParse(GetComboTag(I2cLedSelectionCombo),
                NumberStyles.Integer, CultureInfo.InvariantCulture,
                out led) ? led : 0u;
        }

        private uint BoardLedPhysicalIndex(uint logicalLed)
        {
            if (lastSensorSnapshot != null && lastSensorSnapshot.IsCelestica)
            {
                uint[] celesticaMap = { 4u, 5u, 0u, 1u, 2u, 3u };
                return logicalLed < (uint)celesticaMap.Length ?
                    celesticaMap[(int)logicalLed] : logicalLed;
            }
            if (lastSnapshot == null ||
                !string.Equals(lastSnapshot.Layout, "MSI",
                    StringComparison.OrdinalIgnoreCase))
                return logicalLed;
            return logicalLed == 0 ? 4u :
                logicalLed == 1 ? 5u :
                logicalLed == 4 ? 0u :
                logicalLed == 5 ? 1u : logicalLed;
        }

        private string DescribeBoardLedFaults(BoardLedState state)
        {
            if (!state.IsPresent)
                return "indicator not fitted on this board";
            if (!state.HasFaultDiagnostics)
                return "hardware fault diagnostics unavailable";

            uint physicalLed = BoardLedPhysicalIndex(state.Led);
            uint channelMask = 7u << ((int)physicalLed * 3);
            uint open = state.OpenOutputMask & channelMask;
            uint shorted = state.ShortOutputMask & channelMask;
            if (open == 0 && shorted == 0)
                return "LED outputs electrically connected";
            if (open != 0 && shorted != 0)
                return string.Format(CultureInfo.InvariantCulture,
                    "open 0x{0:X5}, short 0x{1:X5}",
                    state.OpenOutputMask, state.ShortOutputMask);
            return open != 0 ? string.Format(CultureInfo.InvariantCulture,
                "open-output mask 0x{0:X5}", state.OpenOutputMask) :
                string.Format(CultureInfo.InvariantCulture,
                    "short-output mask 0x{0:X5}", state.ShortOutputMask);
        }

        private bool HasBoardLedFault(BoardLedState state)
        {
            if (!state.HasFaultDiagnostics)
                return false;
            uint physicalLed = BoardLedPhysicalIndex(state.Led);
            uint channelMask = 7u << ((int)physicalLed * 3);
            return ((state.OpenOutputMask | state.ShortOutputMask) &
                    channelMask) != 0;
        }

        private static bool HasAnyBoardLedFault(BoardLedState state)
        {
            return state.HasFaultDiagnostics &&
                (state.OpenOutputMask | state.ShortOutputMask) != 0;
        }

        private void ApplyManualBoardLedState(BoardLedState state)
        {
            ledControlsUpdating = true;
            try
            {
                I2cLedRedSlider.Value = state.Red;
                I2cLedGreenSlider.Value = state.Green;
                I2cLedBlueSlider.Value = state.Blue;
                I2cLedCurrentSlider.Value = state.GlobalCurrent == 0 ?
                    96 : state.GlobalCurrent;
            }
            finally
            {
                ledControlsUpdating = false;
            }
            I2cLedSlider_ValueChanged(null, null);
            ApplyBoardLedVisual((int)state.Led,
                new BoardLedColor(state.Red, state.Green, state.Blue,
                    state.Red == 0 && state.Green == 0 && state.Blue == 0 ?
                    "OFF" : "MANUAL"));
            boardLedHardwareFaults[state.Led] = HasBoardLedFault(state);
            if (HasBoardLedFault(state))
            {
                TextBlock status = GetBoardLedStatusText((int)state.Led);
                status.Text = "HARDWARE FAULT";
                status.Foreground = (Brush)FindResource("GoldBrush");
            }
        }

        private void SetI2cLedButtonsEnabled(bool enabled)
        {
            I2cLedReadButton.IsEnabled = enabled;
            I2cLedOffButton.IsEnabled = enabled;
            I2cLedApplyButton.IsEnabled = enabled;
            I2cLedTestButton.IsEnabled = enabled;
        }

        private void SetI2cUnavailable(string state, string detail)
        {
            Brush warningBrush = (Brush)FindResource("GoldBrush");
            I2cControllerStateText.Text = state;
            I2cControllerStateText.Foreground = warningBrush;
            I2cControllerDetailText.Text = detail;
            I2cBusStateText.Text = "—";
            I2cBusDetailText.Text = "No controller sample";
            I2cOffsetText.Text = "—";
            I2cRegisterText.Text = "CR — · SR —";
            I2cDeviceCountText.Text = "—";
            I2cScanResultText.Text = detail;
            I2cAddressMapText.Text = "Bus map unavailable.";
            I2cDiagnosticsText.Text = detail;
            I2cBoardStatusText.Text = "NOT AVAILABLE";
            I2cMacStatusText.Text = "NOT AVAILABLE";
            I2cSerialNumberText.Text = "Card serial unavailable";
            I2cMuxStatusText.Text = "NOT AVAILABLE";
            I2cMuxStatusText.Foreground = warningBrush;
            I2cLedOperationText.Text = detail;
            I2cReadButton.IsEnabled = false;
            I2cPreviousButton.IsEnabled = false;
            I2cNextButton.IsEnabled = false;
            I2cBoardReadButton.IsEnabled = false;
            I2cMacReadButton.IsEnabled = false;
        }

        private string FormatI2cAddressMap(I2cRefreshResult result)
        {
            if (result.Addresses.Count == 0)
                return result.FullScan ?
                    "Scan complete · no addresses ACKed." :
                    "Known-device probe · no addresses ACKed.";

            StringBuilder output = new StringBuilder();
            output.Append(result.FullScan ? "Full scan" : "Known probes");
            output.AppendFormat(CultureInfo.InvariantCulture,
                " · {0} ACK{1}\r\n", result.Addresses.Count,
                result.Addresses.Count == 1 ? "" : "s");
            foreach (uint address in result.Addresses)
            {
                bool celestica = lastSensorSnapshot != null &&
                    lastSensorSnapshot.IsCelestica;
                string name = celestica && address == 0x34 ?
                    "IS32FL3207 status LEDs (upstream)" :
                    celestica && address == 0x44 ?
                    "SHT3x humidity/temperature (CH1)" :
                    celestica && address == 0x48 ? "LM75B sensor 1 (CH0)" :
                    celestica && address == 0x49 ? "LM75B sensor 2 (CH0)" :
                    celestica && address == 0x4a ?
                    "LM75B sensor 3 (CH0) or BNO085 (CH3)" :
                    celestica && address == 0x63 ?
                    "ICP-10100 pressure/temperature (CH2)" :
                    address == 0x50 ? "board EEPROM" :
                    address == 0x58 ? "MAC identity" :
                    address == 0x70 ?
                    (celestica ? "TCA9546A mux" : "PCA9546A mux") :
                    address == 0x37 ? "IS32FL3207 status LEDs (CH1)" :
                    address == 0x29 ? "BNO055 IMU (CH1)" :
                    address == 0x40 ? "INA219 12 V monitor (CH1)" :
                    address == 0x41 ? "INA219 5 V monitor (CH1)" :
                    address == 0x44 ? "INA219 3.3 V monitor (CH1)" :
                    address == 0x76 ? "BME280 environment (CH1)" :
                    "unclassified / expansion";
                output.AppendFormat(CultureInfo.InvariantCulture,
                    "0x{0:X2}  {1}\r\n", address, name);
            }
            return output.ToString().TrimEnd();
        }

        private static string FormatI2cControllerDiagnostics(
            I2cControllerStatus status, string operation)
        {
            return string.Format(CultureInfo.InvariantCulture,
                "CR  0x{0:X2}  {1}\r\nSR  0x{2:X2}  {3}\r\n" +
                "ISR 0x{4:X8}  {5}\r\nIER 0x{6:X8}\r\n" +
                "TX FIFO {7}  RX FIFO {8}\r\n" +
                "Last START CR 0x{9:X2}->0x{10:X2}  SR 0x{11:X2}->0x{12:X2}\r\n" +
                "Last START events 0x{13:X4}  TX FIFO {14}->{15}\r\n{16}",
                status.Control, DecodeI2cControl(status.Control),
                status.Status, DecodeI2cStatus(status.Status),
                status.InterruptStatus,
                DecodeI2cInterrupts(status.InterruptStatus),
                status.InterruptEnable, status.TxFifoOccupancy,
                status.RxFifoOccupancy,
                status.LastStartInitialControl, status.LastStartFinalControl,
                status.LastStartInitialStatus, status.LastStartFinalStatus,
                status.LastStartInterruptStatus,
                status.LastStartInitialTxFifoOccupancy,
                status.LastStartFinalTxFifoOccupancy, operation);
        }

        private static string FormatI2cTransactionDiagnostics(
            I2cReadResult result, uint subaddress, uint subaddressLength,
            uint requestedLength, uint timeoutMilliseconds)
        {
            return string.Format(CultureInfo.InvariantCulture,
                "READ 0x{0:X2} / sub 0x{1:X4} ({2} byte{3})\r\n" +
                "RX {4}/{5} bytes · timeout {6} ms\r\n" +
                "SR  0x{7:X2}  {8}\r\nISR 0x{9:X8}  {10}",
                result.Address, subaddress, subaddressLength,
                subaddressLength == 1 ? "" : "s", result.Data.Length,
                requestedLength, timeoutMilliseconds,
                result.ControllerStatus, DecodeI2cStatus(result.ControllerStatus),
                result.InterruptStatus,
                DecodeI2cInterrupts(result.InterruptStatus));
        }

        private static string DecodeI2cControl(uint value)
        {
            return DecodeFlags(value, new[]
            {
                new KeyValuePair<uint, string>(0x01, "EN"),
                new KeyValuePair<uint, string>(0x02, "TX_RESET"),
                new KeyValuePair<uint, string>(0x04, "MASTER"),
                new KeyValuePair<uint, string>(0x08, "TX"),
                new KeyValuePair<uint, string>(0x10, "NACK"),
                new KeyValuePair<uint, string>(0x20, "RESTART")
            });
        }

        private static string DecodeI2cStatus(uint value)
        {
            return DecodeFlags(value, new[]
            {
                new KeyValuePair<uint, string>(0x80, "TX_EMPTY"),
                new KeyValuePair<uint, string>(0x40, "RX_EMPTY"),
                new KeyValuePair<uint, string>(0x20, "RX_FULL"),
                new KeyValuePair<uint, string>(0x10, "TX_FULL"),
                new KeyValuePair<uint, string>(0x08, "SLAVE_READ"),
                new KeyValuePair<uint, string>(0x04, "BUS_BUSY"),
                new KeyValuePair<uint, string>(0x02, "ADDRESSED")
            });
        }

        private static string DecodeI2cInterrupts(uint value)
        {
            return DecodeFlags(value, new[]
            {
                new KeyValuePair<uint, string>(0x01, "ARB_LOST"),
                new KeyValuePair<uint, string>(0x02, "TX_ERROR/FINAL_NACK"),
                new KeyValuePair<uint, string>(0x04, "TX_EMPTY"),
                new KeyValuePair<uint, string>(0x08, "RX_FULL"),
                new KeyValuePair<uint, string>(0x10, "BUS_NOT_BUSY")
            });
        }

        private static string DecodeFlags(uint value,
            IEnumerable<KeyValuePair<uint, string>> flags)
        {
            string decoded = string.Join("|", flags
                .Where(flag => (value & flag.Key) != 0)
                .Select(flag => flag.Value));
            return decoded.Length == 0 ? "none" : decoded;
        }

        private async void ReadI2c_Click(object sender, RoutedEventArgs e)
        {
            await ExecuteI2cReadAsync();
        }

        private void I2cPreset_SelectionChanged(object sender,
                                                SelectionChangedEventArgs e)
        {
            if (I2cAddressTextBox == null || I2cPresetCombo == null)
                return;
            string preset = GetComboTag(I2cPresetCombo);
            if (preset == "board")
            {
                I2cAddressTextBox.Text = "50";
                I2cSubaddressTextBox.Text = "00";
                I2cSubaddressLengthCombo.SelectedIndex = 1;
                I2cLengthTextBox.Text = "32";
            }
            else if (preset == "identity")
            {
                I2cAddressTextBox.Text = "58";
                I2cSubaddressTextBox.Text = "9A";
                I2cSubaddressLengthCombo.SelectedIndex = 1;
                I2cLengthTextBox.Text = "6";
            }
            else if (preset == "imu")
            {
                I2cAddressTextBox.Text = "29";
                I2cSubaddressTextBox.Text = "00";
                I2cSubaddressLengthCombo.SelectedIndex = 1;
                I2cLengthTextBox.Text = "4";
            }
            else if (preset == "power12")
            {
                I2cAddressTextBox.Text = "40";
                I2cSubaddressTextBox.Text = "00";
                I2cSubaddressLengthCombo.SelectedIndex = 1;
                I2cLengthTextBox.Text = "2";
            }
            else if (preset == "environment")
            {
                I2cAddressTextBox.Text = "76";
                I2cSubaddressTextBox.Text = "D0";
                I2cSubaddressLengthCombo.SelectedIndex = 1;
                I2cLengthTextBox.Text = "1";
            }
            else if (preset == "leddriver")
            {
                I2cAddressTextBox.Text = "37";
                I2cSubaddressTextBox.Text = "00";
                I2cSubaddressLengthCombo.SelectedIndex = 1;
                I2cLengthTextBox.Text = "8";
            }
            else if (preset == "direct")
            {
                I2cSubaddressTextBox.Text = "00";
                I2cSubaddressLengthCombo.SelectedIndex = 0;
                I2cLengthTextBox.Text = "16";
            }
        }

        private void I2cDisplayMode_SelectionChanged(
            object sender, SelectionChangedEventArgs e)
        {
            if (I2cOutputTextBox != null && lastI2cData != null)
                RenderI2cOutput();
        }

        private async void I2cPrevious_Click(object sender, RoutedEventArgs e)
        {
            await MoveI2cPageAsync(-1);
        }

        private async void I2cNext_Click(object sender, RoutedEventArgs e)
        {
            await MoveI2cPageAsync(1);
        }

        private async Task MoveI2cPageAsync(int direction)
        {
            try
            {
                uint subaddressLength = GetI2cSubaddressLength();
                if (subaddressLength == 0)
                    throw new InvalidOperationException(
                        "Page navigation requires a 1-byte or 2-byte subaddress.");
                uint maximum = subaddressLength == 1 ? 0xffu : 0xffffu;
                uint current = ParseHex(I2cSubaddressTextBox.Text,
                    "subaddress", 0, maximum);
                uint length = ParseUnsigned(I2cLengthTextBox.Text,
                    "read length", 1, 255);
                uint next = direction < 0 ?
                    (current > length ? current - length : 0u) :
                    Math.Min(maximum, current + length);
                I2cSubaddressTextBox.Text = next.ToString(
                    subaddressLength == 1 ? "X2" : "X4",
                    CultureInfo.InvariantCulture);
                I2cPresetCombo.SelectedIndex = 0;
                await ExecuteI2cReadAsync();
            }
            catch (Exception ex)
            {
                I2cReadStatusText.Text = ex.Message;
            }
        }

        private void CopyI2c_Click(object sender, RoutedEventArgs e)
        {
            if (!string.IsNullOrEmpty(I2cOutputTextBox.Text))
            {
                Clipboard.SetText(I2cOutputTextBox.Text);
                I2cReadStatusText.Text = "The formatted I²C output was copied.";
            }
        }

        private static string GetComboTag(ComboBox combo)
        {
            ComboBoxItem item = combo == null ? null :
                combo.SelectedItem as ComboBoxItem;
            return item == null ? string.Empty :
                Convert.ToString(item.Tag, CultureInfo.InvariantCulture);
        }

        private uint GetI2cSubaddressLength()
        {
            string value = GetComboTag(I2cSubaddressLengthCombo);
            uint result;
            if (!uint.TryParse(value, NumberStyles.Integer,
                CultureInfo.InvariantCulture, out result) || result > 2)
                throw new InvalidOperationException(
                    "Select a valid subaddress length.");
            return result;
        }

        private async void ReadBoardEeprom_Click(object sender, RoutedEventArgs e)
        {
            I2cPresetCombo.SelectedIndex = 1;
            I2cAddressTextBox.Text = "50";
            I2cSubaddressTextBox.Text = "00";
            I2cSubaddressLengthCombo.SelectedIndex = 1;
            I2cLengthTextBox.Text = "32";
            await ExecuteI2cReadAsync();
        }

        private async void ReadMacEeprom_Click(object sender, RoutedEventArgs e)
        {
            I2cPresetCombo.SelectedIndex = 2;
            I2cAddressTextBox.Text = "58";
            I2cSubaddressTextBox.Text = "9A";
            I2cSubaddressLengthCombo.SelectedIndex = 1;
            I2cLengthTextBox.Text = "6";
            await ExecuteI2cReadAsync();
            await RefreshIdentityAsync();
        }

        private async Task RefreshIdentityAsync()
        {
            TimeCardClient activeClient;
            int generation;
            if (!TryCaptureDeviceSession(out activeClient, out generation) ||
                lastSnapshot == null ||
                !SupportsReliableI2cReads())
            {
                SidebarSerialText.Text = "Driver 1.14 required";
                I2cSerialNumberText.Text =
                    "Update to driver 1.14 to read the card identity";
                return;
            }

            try
            {
                string serial;
                bool valid;
                if (lastSnapshot.AbiVersion >= 4)
                {
                    TimeCardIdentity identity = await Task.Run(() =>
                        activeClient.GetIdentity());
                    serial = identity.SerialNumber;
                    valid = identity.IsPresent && identity.IsValid;
                }
                else
                {
                    I2cReadResult result = await Task.Run(() =>
                        activeClient.ReadI2c(0x58, 0x9a, 1, 6, 100));
                    serial = BitConverter.ToString(result.Data).Replace('-', ':');
                    valid = result.Data.Length == 6 &&
                        result.Data.Any(value => value != 0) &&
                        result.Data.Any(value => value != 0xff);
                }

                if (!IsDeviceSessionCurrent(activeClient, generation))
                    return;
                SidebarSerialText.Text = serial;
                I2cSerialNumberText.Text = valid ?
                    "Unique card serial " + serial : "Identity bytes invalid: " + serial;
                I2cSerialNumberText.Foreground = (Brush)FindResource(
                    valid ? "CyanBrush" : "GoldBrush");
                if (valid)
                    UpdateActiveDeviceIdentity(serial);
                Log("Card identity read from 24MAC402: " + serial + ".");
            }
            catch (Exception ex)
            {
                if (!IsDeviceSessionCurrent(activeClient, generation))
                    return;
                SidebarSerialText.Text = "Identity unavailable";
                I2cSerialNumberText.Text = "Card serial read failed";
                I2cSerialNumberText.Foreground = (Brush)FindResource("GoldBrush");
                Log("Card identity read failed: " + ex.Message);
            }
        }

        private async Task ExecuteI2cReadAsync()
        {
            if (!EnsureConnected())
                return;
            if (!SupportsReliableI2cReads())
            {
                MessageBox.Show(this,
                    "Corrected I2C reads require Time Card driver 1.14 or later. " +
                    "This computer is running driver " +
                    (lastSnapshot == null ? "an unknown version" :
                     lastSnapshot.DriverVersion) + ".\n\n" +
                     "Install the current driver 1.14 package, then restart the Control Center.",
                    "Driver update required", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }

            TimeCardClient activeClient = client;
            try
            {
                uint address = ParseHex(I2cAddressTextBox.Text,
                    "7-bit I2C address", 0x08, 0x77);
                uint subaddressLength = GetI2cSubaddressLength();
                uint subaddressMaximum = subaddressLength == 2 ? 0xffffu :
                    subaddressLength == 1 ? 0xffu : 0u;
                uint subaddress = subaddressLength == 0 ? 0u :
                    ParseHex(I2cSubaddressTextBox.Text, "subaddress", 0,
                        subaddressMaximum);
                uint length = ParseUnsigned(I2cLengthTextBox.Text,
                    "read length", 1, 255);
                uint timeoutMilliseconds = uint.Parse(
                    GetComboTag(I2cTimeoutCombo), CultureInfo.InvariantCulture);

                I2cReadButton.IsEnabled = false;
                I2cPreviousButton.IsEnabled = false;
                I2cNextButton.IsEnabled = false;
                I2cCopyButton.IsEnabled = false;
                I2cBoardReadButton.IsEnabled = false;
                I2cMacReadButton.IsEnabled = false;
                I2cReadStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                    "Reading {0} byte(s) from 0x{1:X2}...", length, address);
                I2cReadResult result = await Task.Run(() =>
                    activeClient.ReadI2c(address, subaddress,
                        subaddressLength, length, timeoutMilliseconds));
                if (client != activeClient)
                    return;

                lastI2cData = result.Data;
                lastI2cAddress = result.Address;
                lastI2cSubaddress = subaddress;
                lastI2cSubaddressLength = subaddressLength;
                RenderI2cOutput();
                I2cCopyButton.IsEnabled = true;
                I2cDiagnosticsText.Text = FormatI2cTransactionDiagnostics(
                    result, subaddress, subaddressLength, length,
                    timeoutMilliseconds);
                I2cReadStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                    "Read {0} byte(s) · SR 0x{1:X2} · events 0x{2:X8}",
                    result.Data.Length, result.ControllerStatus,
                    result.InterruptStatus);
                Log(string.Format(CultureInfo.InvariantCulture,
                    "I2C read: 0x{0:X2}, subaddress 0x{1:X4}, {2} byte(s).",
                    address, subaddress, result.Data.Length));
            }
            catch (Exception ex)
            {
                string failureMessage = DescribeI2cReadFailure(ex);
                I2cReadStatusText.Text = "Read failed: " + failureMessage;
                if (client == activeClient && activeClient != null)
                {
                    try
                    {
                        I2cControllerStatus status = await Task.Run(() =>
                            activeClient.GetI2cStatus());
                        I2cDiagnosticsText.Text = FormatI2cControllerDiagnostics(
                            status, "Last read failed: " + failureMessage);
                    }
                    catch (Exception statusError)
                    {
                        I2cDiagnosticsText.Text = "Read failed: " + failureMessage +
                            "\r\nStatus sample failed: " + statusError.Message;
                    }
                }
                Log("I2C read failed: " + failureMessage);
                MessageBox.Show(this, failureMessage, "I2C read failed",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                bool supported = SupportsReliableI2cReads();
                I2cReadButton.IsEnabled = supported;
                I2cPreviousButton.IsEnabled = supported;
                I2cNextButton.IsEnabled = supported;
                I2cCopyButton.IsEnabled = lastI2cData != null;
                I2cBoardReadButton.IsEnabled = supported;
                I2cMacReadButton.IsEnabled = supported;
            }
        }

        private static string DescribeI2cReadFailure(Exception error)
        {
            Win32Exception win32 = error as Win32Exception;
            if (win32 != null && win32.NativeErrorCode == 23)
            {
                return "The installed driver returned its legacy I2C short-transfer status. " +
                    "Windows labels that status as a CRC data error, but this transaction does not use a CRC. " +
                    "Install Time Card driver 1.14 and retry the read.";
            }
            return error.Message;
        }

        private static uint ParseHex(string text, string name,
                                     uint minimum, uint maximum)
        {
            string valueText = (text ?? string.Empty).Trim();
            if (valueText.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                valueText = valueText.Substring(2);
            uint value;
            if (!uint.TryParse(valueText, NumberStyles.AllowHexSpecifier,
                CultureInfo.InvariantCulture, out value) ||
                value < minimum || value > maximum)
                throw new InvalidOperationException(string.Format(
                    CultureInfo.InvariantCulture,
                    "Enter a valid {0} from 0x{1:X} through 0x{2:X}.",
                    name, minimum, maximum));
            return value;
        }

        private void RenderI2cOutput()
        {
            if (lastI2cData == null)
                return;
            string mode = GetComboTag(I2cDisplayModeCombo);
            string subaddressText = lastI2cSubaddressLength == 0 ?
                "direct receive" : string.Format(CultureInfo.InvariantCulture,
                    "subaddress 0x{0:X4} ({1} byte{2})",
                    lastI2cSubaddress, lastI2cSubaddressLength,
                    lastI2cSubaddressLength == 1 ? "" : "s");
            I2cOutputTextBox.Text = string.Format(CultureInfo.InvariantCulture,
                "Device 0x{0:X2} · {1} · {2} bytes · {3}\r\n\r\n{4}",
                lastI2cAddress, subaddressText, lastI2cData.Length,
                I2cDisplayModeCombo.Text,
                FormatI2cData(lastI2cData, lastI2cSubaddress, mode));
        }

        private static string FormatI2cData(byte[] data, uint baseAddress,
                                            string mode)
        {
            StringBuilder output = new StringBuilder();
            int columns = mode == "binary" ? 4 :
                mode == "decimal" ? 8 : mode == "ascii" ? 32 : 16;
            for (int offset = 0; offset < data.Length; offset += columns)
            {
                output.AppendFormat(CultureInfo.InvariantCulture,
                    "{0:X4}  ", baseAddress + (uint)offset);
                int count = Math.Min(columns, data.Length - offset);
                if (mode == "binary")
                {
                    for (int index = 0; index < count; index++)
                        output.Append(Convert.ToString(data[offset + index], 2)
                            .PadLeft(8, '0')).Append(' ');
                }
                else if (mode == "decimal")
                {
                    for (int index = 0; index < count; index++)
                        output.AppendFormat(CultureInfo.InvariantCulture,
                            "{0,3} ", data[offset + index]);
                }
                else if (mode == "ascii")
                {
                    for (int index = 0; index < count; index++)
                    {
                        byte value = data[offset + index];
                        output.Append(value >= 0x20 && value <= 0x7e ?
                            (char)value : '.');
                    }
                }
                else
                {
                    for (int index = 0; index < columns; index++)
                    {
                        if (index < count)
                            output.AppendFormat(CultureInfo.InvariantCulture,
                                "{0:X2} ", data[offset + index]);
                        else if (mode == "hexascii")
                            output.Append("   ");
                    }
                    if (mode == "hexascii")
                    {
                        output.Append(" ");
                        for (int index = 0; index < count; index++)
                        {
                            byte value = data[offset + index];
                            output.Append(value >= 0x20 && value <= 0x7e ?
                                (char)value : '.');
                        }
                    }
                }
                output.AppendLine();
            }
            return output.ToString();
        }

        private void OpenI2c_Click(object sender, RoutedEventArgs e)
        {
            I2cNav.IsChecked = true;
            ShowPage("I2c");
        }

        private byte[] ParseUartSendData()
        {
            ComboBoxItem modeItem = UartSendModeCombo.SelectedItem as ComboBoxItem;
            string mode = modeItem == null ? "Text" : Convert.ToString(modeItem.Content, CultureInfo.InvariantCulture);
            if (mode == "Text")
            {
                string text = UartSendTextBox.Text ?? string.Empty;
                ComboBoxItem endingItem = UartLineEndingCombo.SelectedItem as ComboBoxItem;
                string ending = endingItem == null ? "None" :
                    Convert.ToString(endingItem.Tag, CultureInfo.InvariantCulture);
                if (ending == "CR")
                    text += "\r";
                else if (ending == "LF")
                    text += "\n";
                else if (ending == "CRLF")
                    text += "\r\n";
                return Encoding.ASCII.GetBytes(text);
            }

            string[] tokens = (UartSendTextBox.Text ?? string.Empty)
                .Split(new[] { ' ', '\t', '\r', '\n', ',', '-', ':' }, StringSplitOptions.RemoveEmptyEntries);
            if (tokens.Length > 256)
                throw new InvalidOperationException("UART writes are limited to 256 bytes.");
            byte[] data = new byte[tokens.Length];
            for (int index = 0; index < tokens.Length; index++)
                data[index] = byte.Parse(tokens[index], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture);
            return data;
        }

        private void AppendUart(uint port, string direction, byte[] data,
                                uint lineStatus)
        {
            if (data == null)
                return;
            byte[] capturedData = (byte[])data.Clone();
            UbloxDecodeBatch decoded = null;
            if (port < (uint)uartReceiveDecoders.Length &&
                uartReceiveDecoders[(int)port] != null)
            {
                UbloxStreamDecoder decoder = string.Equals(direction, "TX",
                    StringComparison.OrdinalIgnoreCase) ?
                    uartTransmitDecoders[(int)port] :
                    uartReceiveDecoders[(int)port];
                decoded = decoder.Feed(capturedData);
            }
            UartConsoleEntry entry = new UartConsoleEntry
            {
                Timestamp = DateTime.Now,
                Port = port,
                PortLabel = "UART " + port.ToString(CultureInfo.InvariantCulture),
                HasLineStatus = true,
                Direction = direction,
                Data = capturedData,
                LineStatus = lineStatus,
                Decoded = decoded
            };
            AppendUartEntry(entry);
            UpdateUartDecoderStatus(port);
        }

        private void AppendGenericUart(string portName, string direction,
                                       byte[] data)
        {
            if (data == null)
                return;
            byte[] capturedData = (byte[])data.Clone();
            UbloxDecodeBatch decoded = string.Equals(direction, "TX",
                StringComparison.OrdinalIgnoreCase) ?
                genericTransmitDecoder.Feed(capturedData) :
                genericReceiveDecoder.Feed(capturedData);
            UartConsoleEntry entry = new UartConsoleEntry
            {
                Timestamp = DateTime.Now,
                PortLabel = portName,
                IsGenericPort = true,
                HasLineStatus = false,
                Direction = direction,
                Data = capturedData,
                Decoded = decoded
            };
            AppendUartEntry(entry);
            UpdateGenericUartDecoderStatus(portName);
        }

        private void AppendUartEntry(UartConsoleEntry entry)
        {
            uartConsoleEntries.Add(entry);
            uartConsoleHistoryBytes += entry.Data.Length;
            while (uartConsoleHistoryBytes > 65536 && uartConsoleEntries.Count > 1)
            {
                uartConsoleHistoryBytes -= uartConsoleEntries[0].Data.Length;
                uartConsoleEntries.RemoveAt(0);
            }
            OnUartEntryAppended(entry);
            if (!uartDisplayPaused && ShouldRenderUartEntry(entry))
            {
                UartOutputTextBox.AppendText(RenderUartConsoleEntry(entry));
                if (UartOutputTextBox.Text.Length > 131072)
                    UartOutputTextBox.Text = UartOutputTextBox.Text.Substring(
                        UartOutputTextBox.Text.Length - 65536);
                UartOutputTextBox.ScrollToEnd();
            }
        }

        private void RenderUartConsole()
        {
            const int maximumCharacters = 131072;
            List<string> renderedEntries = new List<string>(uartConsoleEntries.Count);
            int renderedCharacters = 0;
            for (int index = uartConsoleEntries.Count - 1; index >= 0; index--)
            {
                UartConsoleEntry entry = uartConsoleEntries[index];
                if (!ShouldRenderUartEntry(entry))
                    continue;
                string rendered = RenderUartConsoleEntry(entry);
                if (renderedCharacters != 0 &&
                    renderedCharacters + rendered.Length > maximumCharacters)
                    break;
                renderedEntries.Add(rendered);
                renderedCharacters += rendered.Length;
            }
            renderedEntries.Reverse();
            UartOutputTextBox.Text = string.Concat(renderedEntries);
            UartOutputTextBox.ScrollToEnd();
        }

        private string RenderUartConsoleEntry(UartConsoleEntry entry)
        {
            string mode = SelectedUartDisplayMode();
            if (entry.IsGenericPort && entry.Decoded != null &&
                (mode == "Ublox" || mode == "Nmea"))
                return RenderDecodedGenericUartEntry(entry, mode == "Nmea");
            if (!entry.IsGenericPort &&
                ((mode == "Ublox" && entry.Port <= 1) ||
                 (mode == "Nmea" && entry.Port == 3)))
                return RenderDecodedUartEntry(entry);

            string formatted = FormatUartData(entry.Data);
            if (!entry.HasLineStatus)
            {
                return string.Format(CultureInfo.InvariantCulture,
                    "[{0:HH:mm:ss.fff}] {1} {2} {3} byte(s)\r\n{4}{5}",
                    entry.Timestamp, entry.PortLabel, entry.Direction,
                    entry.Data.Length, formatted,
                    formatted.EndsWith("\n", StringComparison.Ordinal) ?
                        string.Empty : "\r\n");
            }
            return string.Format(CultureInfo.InvariantCulture,
                "[{0:HH:mm:ss.fff}] {1} {2} {3} byte(s) · LSR 0x{4:X2}\r\n{5}{6}",
                entry.Timestamp, entry.PortLabel, entry.Direction, entry.Data.Length,
                entry.LineStatus & 0xff, formatted,
                formatted.EndsWith("\n", StringComparison.Ordinal) ? string.Empty : "\r\n");
        }

        private static string RenderDecodedUartEntry(UartConsoleEntry entry)
        {
            UbloxDecodeBatch batch = entry.Decoded;
            bool nmeaOnly = entry.Port == 3;
            StringBuilder output = new StringBuilder();
            output.AppendFormat(CultureInfo.InvariantCulture,
                "[{0:HH:mm:ss.fff}] UART {1} {2} {3} byte(s) · {4} · LSR 0x{5:X2}\r\n",
                entry.Timestamp, entry.Port, entry.Direction, entry.Data.Length,
                nmeaOnly ? "NMEA stream" : "u-blox stream",
                entry.LineStatus & 0xff);
            if (batch == null)
            {
                output.Append("  Decoder unavailable for this sample.\r\n");
                return output.ToString();
            }

            foreach (UbloxDecodedMessage message in batch.Messages)
            {
                output.Append("  ").Append(message.ToConsoleText()
                    .Replace("\r\n", "\r\n  ")).Append("\r\n");
            }
            if (batch.Messages.Count == 0)
            {
                if (batch.BufferedBytes != 0)
                {
                    output.AppendFormat(CultureInfo.InvariantCulture,
                        "  Partial protocol frame buffered ({0} byte(s)); waiting for the next read.\r\n",
                        batch.BufferedBytes);
                }
                else
                {
                    output.Append(nmeaOnly ?
                        "  No complete NMEA sentence in this read.\r\n" :
                        "  No complete UBX, NMEA, or RTCM3 message in this read.\r\n");
                }
            }
            if (batch.DiscardedBytes != 0)
            {
                output.AppendFormat(CultureInfo.InvariantCulture,
                    "  Stream resynchronized after {0} unframed byte(s).\r\n",
                    batch.DiscardedBytes);
            }
            return output.ToString();
        }

        private static string RenderDecodedGenericUartEntry(UartConsoleEntry entry,
                                                             bool nmeaOnly)
        {
            StringBuilder output = new StringBuilder();
            output.AppendFormat(CultureInfo.InvariantCulture,
                "[{0:HH:mm:ss.fff}] {1} {2} {3} byte(s) · {4}\r\n",
                entry.Timestamp, entry.PortLabel, entry.Direction,
                entry.Data.Length, nmeaOnly ? "NMEA stream" : "u-blox mixed stream");
            IEnumerable<UbloxDecodedMessage> messages = entry.Decoded.Messages;
            if (nmeaOnly)
                messages = messages.Where(message => string.Equals(message.Protocol,
                    "NMEA", StringComparison.OrdinalIgnoreCase));
            int count = 0;
            foreach (UbloxDecodedMessage message in messages)
            {
                output.Append("  ").Append(message.ToConsoleText()
                    .Replace("\r\n", "\r\n  ")).Append("\r\n");
                count++;
            }
            if (count == 0)
                output.Append(entry.Decoded.BufferedBytes != 0 ?
                    "  Partial protocol frame buffered; waiting for more data.\r\n" :
                    "  No complete matching message in this read.\r\n");
            return output.ToString();
        }

        private void ResetUartDecoder(uint port)
        {
            if (port >= (uint)uartReceiveDecoders.Length)
                return;
            if (uartReceiveDecoders[(int)port] != null)
                uartReceiveDecoders[(int)port].Reset();
            if (uartTransmitDecoders[(int)port] != null)
                uartTransmitDecoders[(int)port].Reset();
            UpdateUartDecoderStatus(port);
        }

        private void ResetAllUartDecoders()
        {
            for (int port = 0; port < uartReceiveDecoders.Length; port++)
            {
                if (uartReceiveDecoders[port] != null)
                    uartReceiveDecoders[port].Reset();
                if (uartTransmitDecoders[port] != null)
                    uartTransmitDecoders[port].Reset();
            }
            genericReceiveDecoder.Reset();
            genericTransmitDecoder.Reset();
            if (UartPortCombo != null && UartPortCombo.SelectedIndex >= 0 &&
                SelectedGenericComPort() == null)
                UpdateUartDecoderStatus(SelectedUartPort());
        }

        private void UpdateGenericUartDecoderStatus(string portName)
        {
            if (UartDecoderStatusText == null)
                return;
            UartDecoderStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                "{0} · RX {1} message(s) · TX {2} · checksum failures {3} · buffered {4} byte(s)",
                portName, genericReceiveDecoder.TotalMessages,
                genericTransmitDecoder.TotalMessages,
                genericReceiveDecoder.ChecksumFailures +
                    genericTransmitDecoder.ChecksumFailures,
                genericReceiveDecoder.BufferedBytes +
                    genericTransmitDecoder.BufferedBytes);
        }

        private void UpdateUartDecoderStatus(uint port)
        {
            if (port >= (uint)uartReceiveDecoders.Length ||
                uartReceiveDecoders[(int)port] == null)
                return;
            UbloxStreamDecoder receive = uartReceiveDecoders[(int)port];
            UbloxStreamDecoder transmit = uartTransmitDecoders[(int)port];
            TextBlock status = port == 3 ? NmeaDecoderStatusText :
                UartDecoderStatusText;
            if (status == null)
                return;
            if (port == 3)
            {
                status.Text = string.Format(CultureInfo.InvariantCulture,
                    "NMEA decode · {0} sentence(s) · {1} bad · {2} B pending",
                    receive.TotalMessages,
                    receive.ChecksumFailures + transmit.ChecksumFailures,
                    receive.BufferedBytes + transmit.BufferedBytes);
            }
            else
            {
                status.Text = string.Format(CultureInfo.InvariantCulture,
                    "UART {0} · RX {1} message(s) · TX {2} · checksum failures {3} · buffered {4} byte(s)",
                    port, receive.TotalMessages, transmit.TotalMessages,
                    receive.ChecksumFailures + transmit.ChecksumFailures,
                    receive.BufferedBytes + transmit.BufferedBytes);
            }
        }

        private void UseUbloxDecode_Click(object sender, RoutedEventArgs e)
        {
            SelectComboTag(UartFormatCombo, "Ublox");
        }

        private void UseNmeaDecode_Click(object sender, RoutedEventArgs e)
        {
            SelectComboTag(UartFormatCombo, "Nmea");
        }

        private string FormatUartData(byte[] data)
        {
            string mode = SelectedUartDisplayMode();
            bool printable = data.Length == 0 || data.Count(value => value == 9 || value == 10 || value == 13 ||
                (value >= 32 && value < 127)) >= data.Length * 0.82;
            if (mode == "Ascii" || (mode == "Auto" && printable))
            {
                StringBuilder text = new StringBuilder(data.Length);
                foreach (byte value in data)
                {
                    if (value == 9 || value == 10 || value == 13 || (value >= 32 && value < 127))
                        text.Append((char)value);
                    else
                        text.Append('·');
                }
                return text.ToString();
            }
            if (mode == "Binary")
                return string.Join(" ", data.Select(value => Convert.ToString(value, 2).PadLeft(8, '0')));
            if (mode == "Decimal")
                return string.Join(" ", data.Select(value => value.ToString("D3", CultureInfo.InvariantCulture)));
            return string.Join(" ", data.Select(value => value.ToString("X2", CultureInfo.InvariantCulture)));
        }

        private string SelectedUartDisplayMode()
        {
            ComboBoxItem item = UartFormatCombo.SelectedItem as ComboBoxItem;
            return item == null ? "Auto" :
                Convert.ToString(item.Tag, CultureInfo.InvariantCulture);
        }

        private string SelectedUartDisplayName()
        {
            ComboBoxItem item = UartFormatCombo.SelectedItem as ComboBoxItem;
            return item == null ? "Auto" :
                Convert.ToString(item.Content, CultureInfo.InvariantCulture);
        }

        private uint SelectedUartPort()
        {
            ComboBoxItem item = UartPortCombo.SelectedItem as ComboBoxItem;
            if (item == null)
                throw new InvalidOperationException("Select a UART port.");
            uint port;
            if (!uint.TryParse(Convert.ToString(item.Tag,
                    CultureInfo.InvariantCulture), NumberStyles.Integer,
                    CultureInfo.InvariantCulture, out port))
                throw new InvalidOperationException(
                    "The selected serial port is not a Time Card hardware UART.");
            return port;
        }

        private string SelectedGenericComPort()
        {
            ComboBoxItem item = UartPortCombo == null ? null :
                UartPortCombo.SelectedItem as ComboBoxItem;
            string tag = item == null ? null : Convert.ToString(item.Tag,
                CultureInfo.InvariantCulture);
            return tag != null && tag.StartsWith(GenericComTagPrefix,
                StringComparison.OrdinalIgnoreCase) ?
                tag.Substring(GenericComTagPrefix.Length) : null;
        }

        private static uint ParseUnsigned(string text, string name, uint minimum, uint maximum)
        {
            uint value;
            if (!uint.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value) ||
                value < minimum || value > maximum)
                throw new InvalidOperationException(string.Format(CultureInfo.InvariantCulture,
                    "Enter a valid {0} between {1} and {2}.", name, minimum, maximum));
            return value;
        }

        private void ShowUartError(string title, Exception ex)
        {
            UartStatusText.Text = title;
            Log(title + ": " + ex.Message);
            MessageBox.Show(this, ex.Message, title, MessageBoxButton.OK, MessageBoxImage.Error);
        }

        private bool EnsureConnected()
        {
            if (client != null)
                return true;
            MessageBox.Show(this, "The Time Card driver is not connected. Restart the application as administrator and verify the card is present.",
                "Time Card unavailable", MessageBoxButton.OK, MessageBoxImage.Information);
            return false;
        }

        private void CopyDiagnostics_Click(object sender, RoutedEventArgs e)
        {
            string summary = lastSnapshot == null ? "No Time Card sample is available." : BuildDiagnostics(lastSnapshot);
            Clipboard.SetText(summary + Environment.NewLine + Environment.NewLine + "Session activity:" +
                Environment.NewLine + LogTextBox.Text);
            Log("Diagnostic summary copied to the clipboard.");
        }

        private string BuildDiagnostics(TimeCardSnapshot snapshot)
        {
            string diagnostics = string.Join(Environment.NewLine, new[]
            {
                "Device:       " + ActiveDeviceDisplayName(),
                "Serial:       " + (string.IsNullOrWhiteSpace(
                    ActiveDeviceSerial()) ? "unavailable" :
                    ActiveDeviceSerial()),
                "Interface:    " + (string.IsNullOrWhiteSpace(
                    ActiveDevicePath()) ? "unavailable" :
                    ActiveDevicePath()),
                "Driver:       " + snapshot.DriverVersion + " (ABI " + snapshot.AbiVersion + ")",
                "Transport:    " + snapshot.Layout + " · " + snapshot.InterruptMessages + " interrupts",
                string.Format(CultureInfo.InvariantCulture, "BAR length:    0x{0:X8}", snapshot.BarLength),
                string.Format(CultureInfo.InvariantCulture, "Clock core:    {0} @ 0x{1:X8}", snapshot.ClockVersion, snapshot.ClockOffset),
                snapshot.ClockStatus == uint.MaxValue ?
                    "Clock status:  unavailable before Clock core v1.2" :
                    string.Format(CultureInfo.InvariantCulture, "Clock status:  0x{0:X8} · {1}", snapshot.ClockStatus,
                        snapshot.IsClockSynchronized ? "in sync" : "not in sync"),
                string.Format(CultureInfo.InvariantCulture, "Clock source:  0x{0:X4}", snapshot.ClockSource),
                snapshot.TodStatus == uint.MaxValue ?
                    "ToD status:    unavailable" :
                    string.Format(CultureInfo.InvariantCulture, "ToD status:    0x{0:X8}", snapshot.TodStatus),
                !snapshot.GnssTelemetryAvailable ?
                    "GNSS status:   unavailable" :
                    string.Format(CultureInfo.InvariantCulture, "GNSS status:   0x{0:X8} · {1}", snapshot.GnssStatus, snapshot.GnssFix),
                !snapshot.GnssTelemetryAvailable ?
                    "Satellites:    unavailable" :
                    string.Format(CultureInfo.InvariantCulture, "Satellites:    {0} seen · {1} locked · valid {2}",
                        snapshot.SeenSatellites, snapshot.LockedSatellites, snapshot.SatelliteDataValid ? "yes" : "no"),
                "Hierarchy:    " + (snapshot.HierarchyRuntimeEnabled ? "enabled" : "disabled") +
                    " · persisted " + (snapshot.HierarchyPersisted ? "yes" : "no"),
                "Card UTC:     " + FormatUtc(snapshot.CardTimeUtc),
                "System UTC:   " + FormatUtc(snapshot.SystemTimeUtc),
                "PHC offset:   " + FormatNanoseconds(snapshot.OffsetNanoseconds),
                "Sample window:" + " " + FormatNanoseconds(snapshot.SamplingWindowNanoseconds)
            });
            if (lastFpgaImageInfo != null && lastFpgaImageInfo.IsPresent)
            {
                diagnostics += Environment.NewLine + string.Format(
                    CultureInfo.InvariantCulture,
                    "FPGA image:   {0} {1} · tag {2} · raw 0x{3:X8} · register 0x{4:X8} · board/layout {5}/{6}",
                    lastFpgaImageInfo.ImageKind,
                    lastFpgaImageInfo.VersionText,
                    lastFpgaImageInfo.ImageTag,
                    lastFpgaImageInfo.RawVersion,
                    lastFpgaImageInfo.RegisterOffset,
                    lastFpgaImageInfo.BoardProfile,
                    lastFpgaImageInfo.Layout);
            }
            else if (snapshot.AbiVersion >= FpgaImageRequiredAbi &&
                     !string.Equals(snapshot.Layout, "Orolia ART",
                         StringComparison.OrdinalIgnoreCase))
            {
                diagnostics += Environment.NewLine +
                    "FPGA image:   not sampled or no valid static image word";
            }
            else if (string.Equals(snapshot.Layout, "Orolia ART",
                         StringComparison.OrdinalIgnoreCase))
            {
                diagnostics += Environment.NewLine +
                    "FPGA image:   unsupported on ART; no standard static image register assumed";
            }
            return diagnostics;
        }

        private void ClearLog_Click(object sender, RoutedEventArgs e)
        {
            sessionLogStore.Clear();
            Log("Session log cleared.");
        }

        private void Log(string message)
        {
            Log(message, SessionLogStore.InferSeverity(message),
                SessionLogStore.InferCategory(message));
        }

        private void Log(string message, SessionLogSeverity severity,
                         string category)
        {
            if (string.IsNullOrWhiteSpace(message))
                return;
            if (!Dispatcher.CheckAccess())
            {
                if (!Dispatcher.HasShutdownStarted)
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                        Log(message, severity, category)));
                }
                return;
            }

            sessionLogStore.Append(DateTime.UtcNow, severity, category,
                CurrentSessionLogCardContext(), message);
            RefreshSessionLogView(true);
        }

        private string CurrentSessionLogCardContext()
        {
            if (client == null)
                return productSettings != null && productSettings.DemoMode ?
                    "demo" : "offline";

            string serial = SidebarSerialText == null ? null :
                SidebarSerialText.Text;
            if (IsUsefulSessionLogCardIdentity(serial))
                return serial.Trim();

            if (lastSnapshot != null &&
                !string.IsNullOrWhiteSpace(lastSnapshot.Layout))
                return lastSnapshot.Layout + " / " +
                    (string.IsNullOrWhiteSpace(ActiveDevicePath()) ?
                        "Time Card" : ActiveDevicePath());
            return string.IsNullOrWhiteSpace(ActiveDevicePath()) ?
                "Time Card" : ActiveDevicePath();
        }

        private static bool IsUsefulSessionLogCardIdentity(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return false;
            string[] placeholders =
            {
                "not read", "unavailable", "required", "reading",
                "not available", "unknown"
            };
            foreach (string placeholder in placeholders)
            {
                if (value.IndexOf(placeholder,
                    StringComparison.OrdinalIgnoreCase) >= 0)
                    return false;
            }
            return true;
        }

        private SessionLogFilter CurrentSessionLogFilter()
        {
            SessionLogFilter filter = new SessionLogFilter();
            string severityTag = GetComboTag(SessionLogSeverityCombo);
            SessionLogSeverity severity;
            if (!string.Equals(severityTag, "All",
                    StringComparison.OrdinalIgnoreCase) &&
                Enum.TryParse(severityTag, true, out severity))
                filter.Severity = severity;

            string categoryTag = GetComboTag(SessionLogCategoryCombo);
            if (!string.IsNullOrWhiteSpace(categoryTag) &&
                !string.Equals(categoryTag, "All",
                    StringComparison.OrdinalIgnoreCase))
                filter.Category = categoryTag;
            filter.SearchText = SessionLogSearchTextBox == null ? null :
                SessionLogSearchTextBox.Text;
            return filter;
        }

        private void RefreshSessionLogView(bool scrollToEnd)
        {
            if (SessionLogListBox == null || LogTextBox == null ||
                SessionLogCountText == null)
                return;

            List<SessionLogRecord> visibleRecords =
                sessionLogStore.Query(CurrentSessionLogFilter());
            SessionLogListBox.ItemsSource = visibleRecords;
            LogTextBox.Text = sessionLogStore.ToText();

            string dropped = sessionLogStore.DroppedRecordCount == 0 ?
                string.Empty : string.Format(CultureInfo.InvariantCulture,
                    " / {0:N0} older discarded",
                    sessionLogStore.DroppedRecordCount);
            SessionLogCountText.Text = string.Format(CultureInfo.InvariantCulture,
                "{0:N0} shown / {1:N0} retained{2}", visibleRecords.Count,
                sessionLogStore.Count, dropped);

            if (scrollToEnd && visibleRecords.Count != 0)
                SessionLogListBox.ScrollIntoView(
                    visibleRecords[visibleRecords.Count - 1]);
        }

        private void SessionLogFilter_Changed(object sender, RoutedEventArgs e)
        {
            RefreshSessionLogView(false);
        }

        private void SessionLogClearFilter_Click(object sender,
                                                   RoutedEventArgs e)
        {
            if (SessionLogSeverityCombo != null)
                SessionLogSeverityCombo.SelectedIndex = 0;
            if (SessionLogCategoryCombo != null)
                SessionLogCategoryCombo.SelectedIndex = 0;
            if (SessionLogSearchTextBox != null)
                SessionLogSearchTextBox.Clear();
            RefreshSessionLogView(false);
        }

        private void SessionLogExport_Click(object sender, RoutedEventArgs e)
        {
            Microsoft.Win32.SaveFileDialog dialog =
                new Microsoft.Win32.SaveFileDialog
                {
                    Title = "Export filtered Time Card session log",
                    Filter = "Text log (*.txt)|*.txt|JSON log (*.json)|*.json",
                    FilterIndex = 1,
                    AddExtension = true,
                    DefaultExt = ".txt",
                    FileName = "timecard-session-" +
                        DateTime.UtcNow.ToString("yyyyMMdd-HHmmss",
                            CultureInfo.InvariantCulture) + ".txt"
                };
            if (productSettings != null)
                dialog.InitialDirectory = ExistingDirectory(
                    productSettings.LastExportDirectory);
            if (dialog.ShowDialog(this) != true)
                return;

            try
            {
                SessionLogFilter filter = CurrentSessionLogFilter();
                bool json = dialog.FilterIndex == 2 ||
                    string.Equals(Path.GetExtension(dialog.FileName), ".json",
                        StringComparison.OrdinalIgnoreCase);
                string content = json ? sessionLogStore.ToJson(filter) :
                    sessionLogStore.ToText(filter);
                File.WriteAllText(dialog.FileName, content,
                    new UTF8Encoding(false));
                if (productSettings != null)
                    RememberExportDirectory(dialog.FileName);
                Log("Session log view exported to " +
                    Path.GetFileName(dialog.FileName) + ".",
                    SessionLogSeverity.Information, "Diagnostics");
            }
            catch (Exception ex)
            {
                Log("Session log export failed: " + ex.Message,
                    SessionLogSeverity.Error, "Diagnostics");
                MessageBox.Show(this, ex.Message, "Session log export failed",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void OpenNmea_Click(object sender, RoutedEventArgs e)
        {
            UartPortCombo.SelectedIndex = 3;
            UartNav.IsChecked = true;
            ShowPage("Uart");
            await RefreshNmeaAsync(false);
        }

        private async void RefreshSubsystems_Click(object sender,
                                                     RoutedEventArgs e)
        {
            await RefreshSubsystemsAsync();
        }

        private async Task RefreshSubsystemsAsync()
        {
            if (subsystemsRefreshing)
                return;
            if (client == null)
            {
                SubsystemGnss2StatusText.Text = "NOT PRESENT";
                SubsystemGnss2StatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                SubsystemGnss2DetailText.Text = "Driver access is unavailable.";
                return;
            }
            if (lastSnapshot == null || lastSnapshot.AbiVersion < 6)
            {
                SubsystemGnss2StatusText.Text = "DRIVER 1.12 / ABI 6 REQUIRED";
                SubsystemGnss2StatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                SubsystemGnss2DetailText.Text =
                    "Install the current driver for non-destructive UART activity detection.";
                return;
            }

            subsystemsRefreshing = true;
            StopUartMonitor();
            SubsystemGnss2StatusText.Text = "LISTENING ON UART 1";
            SubsystemGnss2StatusText.Foreground =
                (Brush)FindResource("CyanBrush");
            SubsystemGnss2DetailText.Text =
                "Waiting up to 1.5 seconds for receiver data…";
            try
            {
                UartObservation observation = await Task.Run(
                    () => client.ObserveUart(1, 1500));
                if (observation.IsPresent && observation.HasActivity)
                {
                    secondaryGnssPresent = true;
                    SubsystemGnss2StatusText.Text = "PRESENT · UART DATA";
                    SubsystemGnss2StatusText.Foreground =
                        (Brush)FindResource("AccentBrush");
                    SubsystemGnss2DetailText.Text = string.Format(
                        CultureInfo.InvariantCulture,
                        "Received activity detected; line status 0x{0:X2}.",
                        observation.LineStatus);
                }
                else
                {
                    secondaryGnssPresent = false;
                    SubsystemGnss2StatusText.Text = "NOT PRESENT";
                    SubsystemGnss2StatusText.Foreground =
                        (Brush)FindResource("GoldBrush");
                    SubsystemGnss2DetailText.Text =
                        "No data was received on UART 1 during the observation window.";
                }
            }
            catch (Exception ex)
            {
                secondaryGnssPresent = false;
                SubsystemGnss2StatusText.Text = "NOT PRESENT";
                SubsystemGnss2StatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                SubsystemGnss2DetailText.Text = "UART observation failed: " + ex.Message;
                Log("Secondary GNSS UART observation failed: " + ex.Message);
            }
            finally
            {
                subsystemsRefreshing = false;
                UpdateBoardLedAutomationAsync(false);
            }
        }

        private async void OpenFlash_Click(object sender, RoutedEventArgs e)
        {
            ShowPage("Flash");
            await RefreshFlashAsync(true);
        }

        private async void RefreshFlash_Click(object sender, RoutedEventArgs e)
        {
            await RefreshFlashAsync(true);
        }

        private void BackToSubsystems_Click(object sender, RoutedEventArgs e)
        {
            SubsystemsNav.IsChecked = true;
            ShowPage("Subsystems");
        }

        private async Task RefreshFlashAsync(bool showError)
        {
            if (flashUpdating)
                return;
            lastFlashStatus = null;
            FlashControllerText.Text = "QUERYING";
            FlashControllerText.Foreground = (Brush)FindResource("CyanBrush");
            FlashJedecText.Text = "—";
            FlashCapacityText.Text = "—";
            FlashRegionText.Text = "—";
            if (client == null || lastSnapshot == null)
            {
                FlashControllerText.Text = "NOT CONNECTED";
                FlashControllerText.Foreground = (Brush)FindResource("GoldBrush");
                FlashLogTextBox.Text = "Connect to the Time Card driver before querying SPI flash.";
                UpdateFlashStartState();
                return;
            }
            if (lastSnapshot.AbiVersion < 6)
            {
                FlashControllerText.Text = "ABI 6 REQUIRED";
                FlashControllerText.Foreground = (Brush)FindResource("GoldBrush");
                FlashLogTextBox.Text = "Install Time Card driver 1.12 or newer to enable guarded FPGA firmware updates.";
                UpdateFlashStartState();
                return;
            }

            try
            {
                FlashDeviceStatus status = await Task.Run(() => client.GetFlashStatus());
                lastFlashStatus = status;
                FlashControllerText.Text = status.IsSupported ? "READY" : "UNSUPPORTED";
                FlashControllerText.Foreground = (Brush)FindResource(
                    status.IsSupported ? "AccentBrush" : "GoldBrush");
                FlashJedecText.Text = status.IsIdentified ? string.Format(
                    CultureInfo.InvariantCulture, "0x{0:X6}", status.JedecId) : "NOT IDENTIFIED";
                FlashCapacityText.Text = status.CapacityBytes == 0 ? "—" :
                    string.Format(CultureInfo.InvariantCulture, "{0:N0} MiB",
                        status.CapacityBytes / 1048576.0);
                FlashRegionText.Text = status.CapacityBytes <= status.FirmwareOffset ? "—" :
                    string.Format(CultureInfo.InvariantCulture, "0x{0:X8} · {1:N0} MiB",
                        status.FirmwareOffset,
                        (status.CapacityBytes - status.FirmwareOffset) / 1048576.0);
                FlashLogTextBox.Text = string.Format(CultureInfo.InvariantCulture,
                    "SPI controller 0x{0:X8}\r\nFIFO depth {1} byte(s)\r\nErase {2:N0} bytes · page {3:N0} bytes\r\nController status 0x{4:X8} · flash status 0x{5:X2}",
                    status.ControllerOffset, status.FifoDepth, status.EraseSize,
                    status.PageSize, status.ControllerStatus, status.FlashStatus);
            }
            catch (Exception ex)
            {
                FlashControllerText.Text = "QUERY FAILED";
                FlashControllerText.Foreground = (Brush)FindResource("GoldBrush");
                FlashLogTextBox.Text = ex.Message;
                Log("SPI flash query failed: " + ex.Message);
                if (showError)
                    MessageBox.Show(this, ex.Message, "Unable to query SPI flash",
                        MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                UpdateFlashStartState();
            }
        }

        private void ChooseFirmware_Click(object sender, RoutedEventArgs e)
        {
            Microsoft.Win32.OpenFileDialog dialog = new Microsoft.Win32.OpenFileDialog
            {
                Title = "Select FPGA firmware image",
                Filter = "FPGA firmware (*.bin;*.fw;*.bit)|*.bin;*.fw;*.bit|All files (*.*)|*.*",
                CheckFileExists = true,
                Multiselect = false
            };
            if (dialog.ShowDialog(this) != true)
                return;

            try
            {
                byte[] file = File.ReadAllBytes(dialog.FileName);
                if (file.Length == 0)
                    throw new InvalidDataException("The selected firmware image is empty.");

                bool wrapped = file.Length >= 4 && file[0] == (byte)'O' &&
                    file[1] == (byte)'C' && file[2] == (byte)'P' &&
                    file[3] == (byte)'C';
                byte[] image;
                string detail;
                if (wrapped)
                {
                    if (file.Length < 16)
                        throw new InvalidDataException("The OCPC firmware header is truncated.");
                    ushort vendor = ReadBigEndian16(file, 4);
                    ushort device = ReadBigEndian16(file, 6);
                    uint declaredSize = ReadBigEndian32(file, 8);
                    ushort hardwareRevision = ReadBigEndian16(file, 12);
                    ushort expectedCrc = ReadBigEndian16(file, 14);
                    if (vendor != 0x1d9b || device != 0x0400)
                        throw new InvalidDataException(string.Format(
                            CultureInfo.InvariantCulture,
                            "This image targets PCI {0:X4}:{1:X4}, not the Time Card 1D9B:0400.",
                            vendor, device));
                    if (declaredSize != file.Length - 16)
                        throw new InvalidDataException("The OCPC image length does not match its header.");
                    ushort actualCrc = Crc16(file, 16, (int)declaredSize);
                    if (expectedCrc != actualCrc)
                        throw new InvalidDataException(string.Format(
                            CultureInfo.InvariantCulture,
                            "The OCPC CRC is invalid (expected 0x{0:X4}, calculated 0x{1:X4}).",
                            expectedCrc, actualCrc));
                    image = new byte[declaredSize];
                    Buffer.BlockCopy(file, 16, image, 0, image.Length);
                    detail = string.Format(CultureInfo.InvariantCulture,
                        "Validated OCPC image · PCI 1D9B:0400 · hardware revision 0x{0:X4} · CRC 0x{1:X4} · {2:N0} bytes",
                        hardwareRevision, actualCrc, image.Length);
                }
                else
                {
                    image = file;
                    detail = string.Format(CultureInfo.InvariantCulture,
                        "Raw image · no hardware identity or CRC header · {0:N0} bytes",
                        image.Length);
                }

                if (lastFlashStatus != null && lastFlashStatus.CapacityBytes >
                    lastFlashStatus.FirmwareOffset && image.LongLength >
                    lastFlashStatus.CapacityBytes - lastFlashStatus.FirmwareOffset)
                    throw new InvalidDataException("The firmware image does not fit in the protected FPGA image region.");

                selectedFlashImage = image;
                selectedFlashPath = dialog.FileName;
                selectedFlashIsRaw = !wrapped;
                FlashImagePathText.Text = Path.GetFileName(dialog.FileName);
                FlashImageDetailText.Text = detail;
                using (SHA256 sha = SHA256.Create())
                    FlashHashText.Text = "SHA-256 " +
                        BitConverter.ToString(sha.ComputeHash(image)).Replace("-", string.Empty);
                FlashAcknowledgeCheckBox.IsChecked = false;
                FlashProgressBar.Value = 0;
                FlashProgressText.Text = "Image validated; acknowledgement required.";
                FlashLogTextBox.Text = wrapped ?
                    "OCPC wrapper validated. The 16-byte wrapper will not be written to flash." :
                    "RAW IMAGE WARNING: compatibility cannot be established from the file. Confirm its source before continuing.";
                UpdateFlashStartState();
            }
            catch (Exception ex)
            {
                selectedFlashImage = null;
                selectedFlashPath = null;
                selectedFlashIsRaw = false;
                FlashImagePathText.Text = "Image rejected";
                FlashImageDetailText.Text = ex.Message;
                FlashHashText.Text = "SHA-256 —";
                UpdateFlashStartState();
                MessageBox.Show(this, ex.Message, "Invalid firmware image",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void FlashAcknowledge_Changed(object sender, RoutedEventArgs e)
        {
            if (IsInitialized)
                UpdateFlashStartState();
        }

        private void UpdateFlashStartState()
        {
            if (FlashStartButton == null)
                return;
            FlashStartButton.IsEnabled = !flashUpdating && client != null &&
                lastSnapshot != null && lastSnapshot.AbiVersion >= 6 &&
                lastFlashStatus != null && lastFlashStatus.IsSupported &&
                selectedFlashImage != null && selectedFlashImage.Length != 0 &&
                FlashAcknowledgeCheckBox.IsChecked == true;
            FlashChooseButton.IsEnabled = !flashUpdating;
        }

        private async void FlashFirmware_Click(object sender, RoutedEventArgs e)
        {
            if (!FlashStartButton.IsEnabled || selectedFlashImage == null ||
                lastFlashStatus == null)
                return;
            if (selectedFlashImage.LongLength > lastFlashStatus.CapacityBytes -
                lastFlashStatus.FirmwareOffset)
            {
                MessageBox.Show(this, "The firmware image does not fit in the FPGA image region.",
                    "Image too large", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            string rawWarning = selectedFlashIsRaw ?
                "\n\nThis is a raw image with no OCPC hardware identity or CRC wrapper." : string.Empty;
            MessageBoxResult confirmation = MessageBox.Show(this,
                "Erase the current FPGA firmware, program " +
                Path.GetFileName(selectedFlashPath) +
                ", and verify every byte? Do not remove power during this operation." + rawWarning,
                "Confirm FPGA firmware update", MessageBoxButton.YesNo,
                MessageBoxImage.Warning, MessageBoxResult.No);
            if (confirmation != MessageBoxResult.Yes)
                return;

            flashUpdating = true;
            UpdateFlashStartState();
            UpdateDeviceSelectionControls();
            FlashAcknowledgeCheckBox.IsEnabled = false;
            FlashProgressBar.Value = 0;
            FlashLogTextBox.Clear();
            FlashAppendLog("Firmware region starts at physical flash offset " +
                string.Format(CultureInfo.InvariantCulture, "0x{0:X8}.",
                    lastFlashStatus.FirmwareOffset));
            try
            {
                byte[] image = (byte[])selectedFlashImage.Clone();
                FlashDeviceStatus status = lastFlashStatus;
                TimeCardClient operationClient = client;
                await Task.Run(() => ProgramAndVerifyFlash(operationClient,
                    status, image));
                FlashReportProgress("Verified · power-cycle the card to load the new FPGA image.", 100);
                FlashAppendLog("UPDATE COMPLETE. Read-back matches the selected firmware image.");
                Log("FPGA firmware update completed and verified: " +
                    Path.GetFileName(selectedFlashPath));
                MessageBox.Show(this,
                    "The FPGA firmware was programmed and verified. Power-cycle or reboot the card to load the new image.",
                    "Firmware update complete", MessageBoxButton.OK,
                    MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                FlashProgressText.Text = "FAILED · keep the card powered and retry with a known-good image";
                FlashAppendLog("FAILED: " + ex.Message);
                Log("FPGA firmware update failed: " + ex.Message);
                MessageBox.Show(this,
                    ex.Message + "\n\nThe previous FPGA image may be incomplete. Keep the card powered and retry with a known-good image.",
                    "Firmware update failed", MessageBoxButton.OK,
                    MessageBoxImage.Error);
            }
            finally
            {
                flashUpdating = false;
                FlashAcknowledgeCheckBox.IsEnabled = true;
                UpdateFlashStartState();
                UpdateDeviceSelectionControls();
            }
        }

        private void ProgramAndVerifyFlash(TimeCardClient operationClient,
            FlashDeviceStatus status, byte[] image)
        {
            int eraseSize = checked((int)status.EraseSize);
            int pageSize = checked((int)status.PageSize);
            if (eraseSize <= 0 || pageSize <= 0 || pageSize > 256)
                throw new InvalidOperationException("The flash geometry reported by the driver is invalid.");
            int eraseCount = (image.Length + eraseSize - 1) / eraseSize;
            int programCount = (image.Length + pageSize - 1) / pageSize;
            int verifyCount = (image.Length + 255) / 256;
            int totalOperations = eraseCount + programCount + verifyCount;
            int completed = 0;

            for (int sector = 0; sector < eraseCount; sector++)
            {
                uint offset = checked((uint)(sector * eraseSize));
                FlashReportProgress(string.Format(CultureInfo.InvariantCulture,
                    "Erasing sector {0:N0} of {1:N0}…", sector + 1, eraseCount),
                    completed * 100.0 / totalOperations);
                operationClient.EraseFlashSector(offset, status.EraseSize);
                completed++;
                FlashAppendLog(string.Format(CultureInfo.InvariantCulture,
                    "Erased relative offset 0x{0:X8}", offset));
            }

            for (int offset = 0; offset < image.Length; offset += pageSize)
            {
                int length = Math.Min(pageSize, image.Length - offset);
                byte[] page = new byte[length];
                Buffer.BlockCopy(image, offset, page, 0, length);
                FlashReportProgress(string.Format(CultureInfo.InvariantCulture,
                    "Programming {0:N0} of {1:N0} bytes…", offset + length,
                    image.Length), completed * 100.0 / totalOperations);
                operationClient.ProgramFlashPage((uint)offset, page);
                completed++;
            }
            FlashAppendLog(string.Format(CultureInfo.InvariantCulture,
                "Programmed {0:N0} bytes.", image.Length));

            for (int offset = 0; offset < image.Length; offset += 256)
            {
                int length = Math.Min(256, image.Length - offset);
                FlashReportProgress(string.Format(CultureInfo.InvariantCulture,
                    "Verifying {0:N0} of {1:N0} bytes…", offset + length,
                    image.Length), completed * 100.0 / totalOperations);
                byte[] actual = operationClient.ReadFlash((uint)offset,
                    (uint)length);
                for (int index = 0; index < length; index++)
                {
                    if (actual[index] != image[offset + index])
                        throw new InvalidDataException(string.Format(
                            CultureInfo.InvariantCulture,
                            "Read-back mismatch at relative flash offset 0x{0:X8} (expected 0x{1:X2}, read 0x{2:X2}).",
                            offset + index, image[offset + index], actual[index]));
                }
                completed++;
            }
        }

        private void FlashReportProgress(string message, double percent)
        {
            Dispatcher.Invoke(new Action(() =>
            {
                FlashProgressText.Text = message;
                FlashProgressBar.Value = Math.Max(0, Math.Min(100, percent));
            }));
        }

        private void FlashAppendLog(string message)
        {
            Dispatcher.Invoke(new Action(() =>
            {
                FlashLogTextBox.AppendText(string.Format(CultureInfo.InvariantCulture,
                    "[{0:HH:mm:ss}] {1}\r\n", DateTime.Now, message));
                FlashLogTextBox.ScrollToEnd();
            }));
        }

        private static ushort ReadBigEndian16(byte[] value, int offset)
        {
            return (ushort)((value[offset] << 8) | value[offset + 1]);
        }

        private static uint ReadBigEndian32(byte[] value, int offset)
        {
            return ((uint)value[offset] << 24) | ((uint)value[offset + 1] << 16) |
                ((uint)value[offset + 2] << 8) | value[offset + 3];
        }

        private static ushort Crc16(byte[] value, int offset, int length)
        {
            ushort crc = 0xffff;
            for (int index = 0; index < length; index++)
            {
                crc ^= value[offset + index];
                for (int bit = 0; bit < 8; bit++)
                    crc = (ushort)(((crc & 1) != 0) ?
                        ((crc >> 1) ^ 0xa001) : (crc >> 1));
            }
            return crc;
        }

        private void UpdateSensorsCompatibility(TimeCardSnapshot snapshot)
        {
            if (snapshot != null && snapshot.Layout == "Orolia ART")
            {
                lastSensorSnapshot = null;
                SensorsDriverText.Text =
                    "The Orolia ART profile has no PCA9546A sensor branch. The mRO-50 raw temperature word is available in the Atomic workspace.";
                SensorsStatusText.Text = "NOT FITTED ON ART";
                SensorsStatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                SensorsStatusDot.Fill = (Brush)FindResource("GoldBrush");
                SensorsLastSampleText.Text = "BOARD PROFILE";
                SetEnvironmentUnavailable(
                    "ENVIRONMENT SENSOR \u00B7 NOT FITTED ON OROLIA ART");
                Rail12StatusText.Text = "NOT FITTED";
                Rail5StatusText.Text = "NOT FITTED";
                Rail3V3StatusText.Text = "NOT FITTED";
                SensorsSamplingDurationChart.Clear();
                SensorsSamplingDurationText.Text = "N/A";
                SensorsSamplingDurationRangeText.Text = "NO SENSOR FABRIC";
                ApplyImu(new ImuSensorReading(new TimeCardBno055ReadingRaw
                {
                    Size = (uint)System.Runtime.InteropServices.Marshal.SizeOf(
                        typeof(TimeCardBno055ReadingRaw))
                }));
                ImuStatusText.Text = "NOT FITTED";
                ImuDetailText.Text =
                    "IMU sensor branch is not implemented by the Orolia ART profile";
                ImuDetailText.Foreground =
                    (Brush)FindResource("GoldBrush");
                return;
            }

            bool supported = snapshot != null && snapshot.AbiVersion >= 10;
            if (supported)
            {
                if (lastSensorSnapshot == null)
                    SensorsDriverText.Text =
                        "Sensor branch ready · board-specific I2C routes are selected only for each guarded sample, then restored.";
                return;
            }

            SensorsDriverText.Text = snapshot == null
                ? "Connect a Time Card with driver 1.37 / ABI 10 to begin live sampling."
                : string.Format(CultureInfo.InvariantCulture,
                    "Driver {0} / ABI {1} does not expose the board-specific sensor ABI. Install Time Card driver 1.37 / ABI 10.",
                    snapshot.DriverVersion, snapshot.AbiVersion);
            SensorsStatusText.Text = "DRIVER UPDATE REQUIRED";
            SensorsStatusText.Foreground = (Brush)FindResource("GoldBrush");
            SensorsStatusDot.Fill = (Brush)FindResource("GoldBrush");
            SensorsSamplingDurationChart.Clear();
            SensorsSamplingDurationText.Text = "N/A";
            SensorsSamplingDurationRangeText.Text = "DRIVER UPDATE REQUIRED";
        }

        private async Task RefreshSensorsAsync()
        {
            TimeCardClient activeClient;
            int generation;
            if (sensorsRefreshing || !TryCaptureDeviceSession(
                out activeClient, out generation))
                return;
            if (lastSnapshot == null || lastSnapshot.AbiVersion < 10)
            {
                UpdateSensorsCompatibility(lastSnapshot);
                return;
            }
            if (lastSnapshot.Layout == "Orolia ART")
            {
                UpdateSensorsCompatibility(lastSnapshot);
                return;
            }

            sensorsRefreshing = true;
            SensorsStatusText.Text = "SAMPLING";
            SensorsStatusText.Foreground = (Brush)FindResource("CyanBrush");
            SensorsStatusDot.Fill = (Brush)FindResource("CyanBrush");
            Stopwatch sampleTimer = Stopwatch.StartNew();
            try
            {
                SensorTelemetrySnapshot telemetry = await Task.Run(
                    () => activeClient.GetSensorTelemetry());
                if (!IsDeviceSessionCurrent(activeClient, generation))
                    return;
                sampleTimer.Stop();
                RecordSensorSamplingDuration(sampleTimer.Elapsed.TotalMilliseconds,
                    true);
                ApplySensorTelemetry(telemetry);
            }
            catch (Exception ex)
            {
                if (!IsDeviceSessionCurrent(activeClient, generation))
                    return;
                sampleTimer.Stop();
                RecordSensorSamplingDuration(sampleTimer.Elapsed.TotalMilliseconds,
                    false);
                SensorsStatusText.Text = "SAMPLE FAILED";
                SensorsStatusText.Foreground = (Brush)FindResource("DangerBrush");
                SensorsStatusDot.Fill = (Brush)FindResource("DangerBrush");
                SensorsLastSampleText.Text = "ERROR";
                SensorsDriverText.Text = "Sensor read failed: " + ex.Message;
                Log("Sensor telemetry failed: " + ex.Message);
            }
            finally
            {
                sensorsRefreshing = false;
            }
        }

        private void RecordSensorSamplingDuration(double milliseconds,
                                                  bool succeeded)
        {
            if (double.IsNaN(milliseconds) || double.IsInfinity(milliseconds))
                return;
            milliseconds = Math.Max(0, milliseconds);
            SensorsSamplingDurationChart.AddSample(DateTime.UtcNow, milliseconds);
            string value = milliseconds >= 100 ?
                milliseconds.ToString("F0", CultureInfo.InvariantCulture) :
                milliseconds >= 10 ?
                    milliseconds.ToString("F1", CultureInfo.InvariantCulture) :
                    milliseconds.ToString("F2", CultureInfo.InvariantCulture);
            SensorsSamplingDurationText.Text = succeeded ? value + " ms" :
                "FAIL " + value + " ms";
            SensorsSamplingDurationText.Foreground = (Brush)FindResource(
                succeeded ? "CyanBrush" : "DangerBrush");
            if (SensorsSamplingDurationChart.SampleCount < 2)
            {
                SensorsSamplingDurationRangeText.Text = "COLLECTING · 1/60";
                return;
            }
            SensorsSamplingDurationRangeText.Text = string.Format(
                CultureInfo.InvariantCulture, "MIN {0:F2} · MAX {1:F2} MS · 60 S",
                SensorsSamplingDurationChart.Minimum,
                SensorsSamplingDurationChart.Maximum);
        }

        private void ApplySensorTelemetry(SensorTelemetrySnapshot telemetry)
        {
            lastSensorSnapshot = telemetry;
            UpdateHealthExperience();
            Brush healthy = (Brush)FindResource("AccentBrush");
            Brush warning = (Brush)FindResource("GoldBrush");
            bool anyValid = telemetry.Environment.IsValid ||
                telemetry.Rail12V.IsValid || telemetry.Rail5V.IsValid ||
                telemetry.Rail3V3.IsValid || telemetry.Imu.IsValid ||
                telemetry.HumiditySensor.IsValid ||
                telemetry.PressureSensor.IsValid ||
                telemetry.BoardTemperatures.Any(item => item.IsValid);

            SensorsStatusText.Text = anyValid ? "LIVE · 1 HZ" : "NO SENSOR DATA";
            SensorsStatusText.Foreground = anyValid ? healthy : warning;
            SensorsStatusDot.Fill = anyValid ? healthy : warning;
            SensorsLastSampleText.Text = DateTime.Now.ToString(
                "HH:mm:ss", CultureInfo.InvariantCulture);
            if (telemetry.IsCelestica)
            {
                ApplyCelesticaSensorTelemetry(telemetry, healthy, warning);
                ApplyImu(telemetry.Imu);
                return;
            }

            ConfigureLegacySensorWorkspace();
            SensorsDriverText.Text = string.Format(CultureInfo.InvariantCulture,
                "Guarded all-route inventory · BME {3} · INA219 {4}/{5}/{6} · IMU {7} · prior mux 0x{0:X2} · controller 0x{1:X2} · events 0x{2:X8}",
                telemetry.MuxChannelMask, telemetry.ControllerStatus,
                telemetry.InterruptStatus,
                telemetry.Environment.IsPresent ? "ACK" : "NO ACK",
                telemetry.Rail12V.IsPresent ? "ACK" : "NO ACK",
                telemetry.Rail5V.IsPresent ? "ACK" : "NO ACK",
                telemetry.Rail3V3.IsPresent ? "ACK" : "NO ACK",
                telemetry.Imu.IsPresent ? "ACK" : "NO ACK");

            EnvironmentSensorReading environment = telemetry.Environment;
            if (environment.IsValid)
            {
                string sensorName = environment.ChipId == 0x58u ?
                    "BMP280" : "BME280";
                BmeTemperatureText.Text = environment.TemperatureCelsius.ToString(
                    "F1", CultureInfo.InvariantCulture) + " °C";
                BmeHumidityText.Text = environment.HasHumidity ?
                    environment.HumidityPercent.ToString(
                        "F1", CultureInfo.InvariantCulture) + " %" :
                    "N/A";
                BmePressureText.Text = environment.PressureHectopascals.ToString(
                    "F1", CultureInfo.InvariantCulture) + " hPa";
                BmeDewPointText.Text = environment.HasHumidity ?
                    environment.DewPointCelsius.ToString(
                        "F1", CultureInfo.InvariantCulture) + " °C" :
                    "N/A";
                BmeDetailText.Text = string.Format(CultureInfo.InvariantCulture,
                    "{0} · AUTO ADDRESS · ID 0x{1:X2} · {2}", sensorName,
                    environment.ChipId,
                    environment.IsConversionReady ? "READY" : "CONVERTING");
                BmeDetailText.Foreground = healthy;
            }
            else
            {
                SetEnvironmentUnavailable(environment.IsPresent ?
                    "BME280/BMP280 · 0x76/0x77 · INVALID SAMPLE" :
                    "BME280/BMP280 · 0x76/0x77 · NO I2C ACK · NOT FITTED OR UNPOWERED");
            }

            ApplyPowerRail(telemetry.Rail12V, Rail12VoltageText,
                Rail12CurrentText, Rail12PowerText, Rail12DetailText,
                Rail12StatusText);
            ApplyPowerRail(telemetry.Rail5V, Rail5VoltageText,
                Rail5CurrentText, Rail5PowerText, Rail5DetailText,
                Rail5StatusText);
            ApplyPowerRail(telemetry.Rail3V3, Rail3V3VoltageText,
                Rail3V3CurrentText, Rail3V3PowerText, Rail3V3DetailText,
                Rail3V3StatusText);
            ApplyImu(telemetry.Imu);
        }

        private void ConfigureLegacySensorWorkspace()
        {
            SensorsIntroText.Text =
                "Live BME280/BMP280 environment, INA219 power-rail, and BNO055/BNO08x nine-axis readings from the auto-detected sensor branch.";
            EnvironmentDescriptionText.Text =
                "Auto-detected BME280/BMP280 measurements";
            PowerSectionTitleText.Text = "Power rails";
            PowerSectionDescriptionText.Text =
                "Live bus voltage, shunt current, and calculated load power from the three INA219 monitors.";
            Rail12TitleText.Text = "+12 V input";
            Rail5TitleText.Text = "+5 V rail";
            Rail3V3TitleText.Text = "+3.3 V rail";
            Rail12SecondaryGrid.Visibility = Visibility.Visible;
            Rail5SecondaryGrid.Visibility = Visibility.Visible;
            Rail3V3SecondaryGrid.Visibility = Visibility.Visible;
        }

        private void ApplyCelesticaSensorTelemetry(
            SensorTelemetrySnapshot telemetry, Brush healthy, Brush warning)
        {
            Sht3xSensorReading humidity = telemetry.HumiditySensor;
            Icp10100SensorReading pressure = telemetry.PressureSensor;
            int lm75Valid = telemetry.BoardTemperatures.Count(item => item.IsValid);

            SensorsIntroText.Text =
                "Celestica R4006 telemetry from three LM75B thermal zones, SHT3x humidity, ICP-10100 pressure, and BNO085 inertial sensing.";
            EnvironmentDescriptionText.Text =
                "SHT3x humidity/temperature and ICP-10100 compensated pressure";
            PowerSectionTitleText.Text = "Board temperature sensors";
            PowerSectionDescriptionText.Text =
                "Three independently addressed LM75B sensors on TCA9546A channel 0.";
            SensorsDriverText.Text = string.Format(CultureInfo.InvariantCulture,
                "Celestica fixed-channel inventory · LM75B {3}/3 · SHT3x {4} · ICP-10100 {5} · BNO085 {6} · prior mux 0x{0:X2} · controller 0x{1:X2} · events 0x{2:X8}",
                telemetry.MuxChannelMask, telemetry.ControllerStatus,
                telemetry.InterruptStatus, lm75Valid,
                humidity.IsValid ? "CRC OK" : humidity.IsPresent ? "INVALID" : "NO ACK",
                pressure.IsValid ? "CRC OK" : pressure.IsPresent ? "INVALID" : "NO ACK",
                telemetry.Imu.IsValid ? "LIVE" : telemetry.Imu.IsPresent ? "INITIALIZING" : "NO ACK");

            bool hasTemperature = humidity.IsValid || pressure.IsValid;
            double temperature = humidity.IsValid ? humidity.TemperatureCelsius :
                pressure.TemperatureCelsius;
            BmeTemperatureText.Text = hasTemperature ?
                temperature.ToString("F1", CultureInfo.InvariantCulture) + " °C" :
                "— °C";
            BmeHumidityText.Text = humidity.IsValid ?
                humidity.HumidityPercent.ToString("F1",
                    CultureInfo.InvariantCulture) + " %" : "— %";
            BmePressureText.Text = pressure.IsValid &&
                pressure.HasCompensatedPressure ?
                pressure.PressureHectopascals.ToString("F1",
                    CultureInfo.InvariantCulture) + " hPa" : "— hPa";
            BmeDewPointText.Text = humidity.IsValid ?
                humidity.DewPointCelsius.ToString("F1",
                    CultureInfo.InvariantCulture) + " °C" : "— °C";
            BmeDetailText.Text = string.Format(CultureInfo.InvariantCulture,
                "SHT3x 0x44 {0} · ICP-10100 0x63 {1}",
                humidity.IsValid ? "CRC OK" : humidity.IsPresent ? "INVALID" : "NO ACK",
                pressure.IsValid && pressure.HasCompensatedPressure ?
                    "CRC + OTP OK" : pressure.IsPresent ? "INVALID" : "NO ACK");
            BmeDetailText.Foreground = humidity.IsValid || pressure.IsValid ?
                healthy : warning;

            Rail12TitleText.Text = "LM75B sensor 1";
            Rail5TitleText.Text = "LM75B sensor 2";
            Rail3V3TitleText.Text = "LM75B sensor 3";
            Rail12SecondaryGrid.Visibility = Visibility.Collapsed;
            Rail5SecondaryGrid.Visibility = Visibility.Collapsed;
            Rail3V3SecondaryGrid.Visibility = Visibility.Collapsed;
            ApplyBoardTemperature(telemetry.BoardTemperatures[0],
                Rail12VoltageText, Rail12DetailText, Rail12StatusText,
                healthy, warning);
            ApplyBoardTemperature(telemetry.BoardTemperatures[1],
                Rail5VoltageText, Rail5DetailText, Rail5StatusText,
                healthy, warning);
            ApplyBoardTemperature(telemetry.BoardTemperatures[2],
                Rail3V3VoltageText, Rail3V3DetailText, Rail3V3StatusText,
                healthy, warning);
        }

        private static void ApplyBoardTemperature(
            BoardTemperatureReading sensor, TextBlock value, TextBlock detail,
            TextBlock status, Brush healthy, Brush warning)
        {
            value.Text = sensor.IsValid ?
                sensor.TemperatureCelsius.ToString("F1",
                    CultureInfo.InvariantCulture) + " °C" : "— °C";
            detail.Text = sensor.IsValid ?
                string.Format(CultureInfo.InvariantCulture,
                    "Raw 0x{0:X4} · TCA9546A CH0",
                    (ushort)sensor.RawTemperature) :
                sensor.IsPresent ? "I²C ACK; sample invalid" :
                    "No I²C ACK; device not fitted or not powered";
            status.Text = string.Format(CultureInfo.InvariantCulture,
                "0x{0:X2} · {1}", sensor.Address,
                sensor.IsValid ? "LIVE" : sensor.IsPresent ? "INVALID" : "NO ACK");
            status.Foreground = sensor.IsValid ? healthy : warning;
        }

        private void SetEnvironmentUnavailable(string detail)
        {
            BmeTemperatureText.Text = "— °C";
            BmeHumidityText.Text = "— %";
            BmePressureText.Text = "— hPa";
            BmeDewPointText.Text = "— °C";
            BmeDetailText.Text = detail;
            BmeDetailText.Foreground = (Brush)FindResource("GoldBrush");
        }

        private void ApplyPowerRail(PowerRailReading rail, TextBlock voltage,
                                    TextBlock current, TextBlock power,
                                    TextBlock detail, TextBlock status)
        {
            Brush healthy = (Brush)FindResource("AccentBrush");
            Brush warning = (Brush)FindResource("GoldBrush");
            if (!rail.IsValid)
            {
                voltage.Text = "— V";
                current.Text = "— A";
                power.Text = "— W";
                detail.Text = rail.IsPresent ? "I2C ACK; sample unavailable" :
                    "No I2C ACK; device not fitted or not powered";
                status.Text = string.Format(CultureInfo.InvariantCulture,
                    "0x{0:X2} · {1}", rail.Address,
                    rail.IsPresent ? "INVALID" : "NO ACK");
                status.Foreground = warning;
                return;
            }

            voltage.Text = rail.VoltageVolts.ToString("F3",
                CultureInfo.InvariantCulture) + " V";
            current.Text = rail.CurrentAmps.ToString("F3",
                CultureInfo.InvariantCulture) + " A";
            power.Text = rail.PowerWatts.ToString("F3",
                CultureInfo.InvariantCulture) + " W";
            detail.Text = string.Format(CultureInfo.InvariantCulture,
                "Shunt {0:F3} mV · config 0x{1:X4}",
                rail.ShuntMillivolts, rail.Configuration);
            status.Text = string.Format(CultureInfo.InvariantCulture,
                "0x{0:X2} · {1}", rail.Address,
                rail.HasOverflow ? "OVERFLOW" :
                rail.IsConversionReady ? "READY" : "LIVE");
            status.Foreground = rail.HasOverflow ? warning : healthy;
        }

        private void ApplyImu(ImuSensorReading imu)
        {
            Brush healthy = (Brush)FindResource("AccentBrush");
            Brush warning = (Brush)FindResource("GoldBrush");
            bool isBno08x = imu.ChipId == 0x80u;
            string imuName = isBno08x ? "BNO08x" : "BNO055";
            string imuAddress = isBno08x ? "0x4A" : "0x29";
            if (!imu.IsValid)
            {
                bool showcase = !imu.IsPresent;
                if (showcase)
                    StartImuCubeShowcase();
                else
                    StopImuCubeShowcase();
                imuOrientationFilter.ShouldAccept(false, false);
                if (imuOrientationFilter.HasAcceptedSample)
                {
                    ImuVisualizationStatusText.Text = "HOLD · NO DATA";
                    ImuVisualizationStatusText.Foreground =
                        (Brush)FindResource("GoldBrush");
                }
                else if (!showcase)
                {
                    UpdateImuCubeOrientation(Quaternion.Identity, false);
                }
                ImuHeadingText.Text = "—°";
                ImuRollText.Text = "—°";
                ImuPitchText.Text = "—°";
                ImuQuaternionText.Text = "—, —, —, —";
                ImuTemperatureText.Text = "— °C";
                ImuSystemCalibrationText.Text = "—/3";
                ImuGyroCalibrationText.Text = "—/3";
                ImuAccelCalibrationText.Text = "—/3";
                ImuMagCalibrationText.Text = "—/3";
                ImuAccelerationText.Text = "X —   Y —   Z —";
                ImuGyroscopeText.Text = "X —   Y —   Z —";
                ImuMagneticText.Text = "X —   Y —   Z —";
                ImuLinearAccelerationText.Text = "X —   Y —   Z —";
                ImuGravityText.Text = "X —   Y —   Z —";
                ImuStatusText.Text = imu.IsPresent ? "INITIALIZING" :
                    "NOT PRESENT";
                ImuStatusText.Foreground = warning;
                ImuDetailText.Text = imu.IsPresent ?
                    imuName + " · " + imuAddress + " · INITIALIZING" :
                    "BNO055/BNO08x · AUTO ROUTE · NOT PRESENT";
                ImuDetailText.Foreground = warning;
                ImuRawStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                    "Mode 0x{0:X2} · Status 0x{1:X2}",
                    imu.OperationMode, imu.SystemStatus);
                ImuClockText.Text = "Clock source not available";
                if (showcase)
                {
                    ImuVisualizationStatusText.Text = "SHOWCASE · NO IMU";
                    ImuVisualizationStatusText.Foreground =
                        (Brush)FindResource("MutedBrush");
                }
                return;
            }

            StopImuCubeShowcase();
            ImuHeadingText.Text = imu.HeadingDegrees.ToString("F1",
                CultureInfo.InvariantCulture) + "°";
            ImuRollText.Text = imu.RollDegrees.ToString("F1",
                CultureInfo.InvariantCulture) + "°";
            ImuPitchText.Text = imu.PitchDegrees.ToString("F1",
                CultureInfo.InvariantCulture) + "°";
            ImuQuaternionText.Text = string.Format(CultureInfo.InvariantCulture,
                "{0:F4}, {1:F4}, {2:F4}, {3:F4}", imu.QuaternionW,
                imu.QuaternionX, imu.QuaternionY, imu.QuaternionZ);
            UpdateImuCubeFromReading(imu);
            ImuTemperatureText.Text = imu.HasTemperature ?
                imu.TemperatureCelsius.ToString("F1",
                    CultureInfo.InvariantCulture) + " °C" : "— °C";
            ImuSystemCalibrationText.Text = imu.SystemCalibration + "/3";
            ImuGyroCalibrationText.Text = imu.GyroscopeCalibration + "/3";
            ImuAccelCalibrationText.Text = imu.AccelerometerCalibration + "/3";
            ImuMagCalibrationText.Text = imu.MagnetometerCalibration + "/3";
            ImuAccelerationText.Text = FormatSensorVector(imu.Acceleration);
            ImuGyroscopeText.Text = FormatSensorVector(imu.Gyroscope);
            ImuMagneticText.Text = FormatSensorVector(imu.MagneticField);
            ImuLinearAccelerationText.Text =
                FormatSensorVector(imu.LinearAcceleration);
            ImuGravityText.Text = FormatSensorVector(imu.Gravity);
            ImuStatusText.Text = imu.SystemError == 0 ?
                (isBno08x ? "SHTP LIVE" : "FUSION LIVE") :
                "SYSTEM ERROR";
            ImuStatusText.Foreground = imu.SystemError == 0 ? healthy : warning;
            ImuDetailText.Text = string.Format(CultureInfo.InvariantCulture,
                "{0} · {1} · ID 0x{2:X2} · {3}", imuName, imuAddress,
                imu.ChipId, isBno08x ? "SH-2 FUSION" : "NDOF");
            ImuDetailText.Foreground = healthy;
            ImuRawStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                "Mode 0x{0:X2} · Status 0x{1:X2} · Error 0x{2:X2}",
                imu.OperationMode, imu.SystemStatus, imu.SystemError);
            ImuClockText.Text = isBno08x ?
                "SHTP sensor hub · mux route auto-detected" :
                imu.UsesExternalClock ?
                    "External 32.768 kHz crystal active" :
                    "Internal clock active";
        }

        private void InitializeImuCube()
        {
            Model3DGroup scene = new Model3DGroup();
            scene.Children.Add(new AmbientLight(Color.FromRgb(62, 83, 105)));
            scene.Children.Add(new DirectionalLight(Color.FromRgb(238, 247, 255),
                new Vector3D(-1.2, -1.6, -2.8)));
            scene.Children.Add(new DirectionalLight(Color.FromRgb(73, 171, 255),
                new Vector3D(2.1, 0.7, 1.4)));

            Model3DGroup cube = new Model3DGroup();
            const double half = 0.48;
            cube.Children.Add(CreateImuCubeFace(
                new Point3D(-half, -half, half), new Point3D(half, -half, half),
                new Point3D(half, half, half), new Point3D(-half, half, half),
                "logo_blue.png", Color.FromRgb(42, 65, 121)));
            cube.Children.Add(CreateImuCubeFace(
                new Point3D(half, -half, -half), new Point3D(-half, -half, -half),
                new Point3D(-half, half, -half), new Point3D(half, half, -half),
                "logo_black.png", Color.FromRgb(13, 17, 23)));
            cube.Children.Add(CreateImuCubeFace(
                new Point3D(-half, half, half), new Point3D(half, half, half),
                new Point3D(half, half, -half), new Point3D(-half, half, -half),
                "logo_yellow.png", Color.FromRgb(112, 111, 34)));
            cube.Children.Add(CreateImuCubeFace(
                new Point3D(-half, -half, -half), new Point3D(half, -half, -half),
                new Point3D(half, -half, half), new Point3D(-half, -half, half),
                "logo_purple.png", Color.FromRgb(74, 43, 117)));
            cube.Children.Add(CreateImuCubeFace(
                new Point3D(half, -half, half), new Point3D(half, -half, -half),
                new Point3D(half, half, -half), new Point3D(half, half, half),
                "logo_green.png", Color.FromRgb(20, 100, 76)));
            cube.Children.Add(CreateImuCubeFace(
                new Point3D(-half, -half, -half), new Point3D(-half, -half, half),
                new Point3D(-half, half, half), new Point3D(-half, half, -half),
                "logo_red.png", Color.FromRgb(116, 28, 26)));

            imuCubeRotation = new QuaternionRotation3D(Quaternion.Identity);
            cube.Transform = new RotateTransform3D(imuCubeRotation);
            scene.Children.Add(cube);
            ImuCubeVisual.Content = scene;
        }

        private static GeometryModel3D CreateImuCubeFace(Point3D p0, Point3D p1,
                                                          Point3D p2, Point3D p3,
                                                          string imageName,
                                                          Color background)
        {
            MeshGeometry3D mesh = new MeshGeometry3D
            {
                Positions = new Point3DCollection { p0, p1, p2, p3 },
                TriangleIndices = new Int32Collection { 0, 1, 2, 0, 2, 3 },
                TextureCoordinates = new PointCollection
                {
                    new Point(0, 1), new Point(1, 1),
                    new Point(1, 0), new Point(0, 0)
                }
            };
            ImageSource image = LoadImuCubeFaceImage(imageName);
            const double imageWidth = 0.94;
            double imageHeight = imageWidth * image.Height / image.Width;
            Rect imageBounds = new Rect((1.0 - imageWidth) / 2.0,
                (1.0 - imageHeight) / 2.0, imageWidth, imageHeight);
            DrawingGroup drawing = new DrawingGroup();
            drawing.Children.Add(new GeometryDrawing(
                new SolidColorBrush(background), null,
                new RectangleGeometry(new Rect(0, 0, 1, 1))));
            drawing.Children.Add(new ImageDrawing(image, imageBounds));
            DrawingBrush faceBrush = new DrawingBrush(drawing)
            {
                Viewbox = new Rect(0, 0, 1, 1),
                ViewboxUnits = BrushMappingMode.Absolute,
                Stretch = Stretch.Fill
            };
            MaterialGroup materials = new MaterialGroup();
            materials.Children.Add(new DiffuseMaterial(faceBrush));
            materials.Children.Add(new SpecularMaterial(
                new SolidColorBrush(Color.FromArgb(80, 225, 243, 255)), 34.0));
            return new GeometryModel3D(mesh, materials) { BackMaterial = materials };
        }

        private static ImageSource LoadImuCubeFaceImage(string imageName)
        {
            BitmapImage image = new BitmapImage();
            image.BeginInit();
            image.CacheOption = BitmapCacheOption.OnLoad;
            image.UriSource = new Uri(
                "pack://application:,,,/Assets/imu-cube/" + imageName,
                UriKind.Absolute);
            image.EndInit();
            image.Freeze();
            return image;
        }

        private void StartImuCubeShowcase()
        {
            if (imuCubeShowcaseActive)
                return;
            imuCubeShowcaseActive = true;
            imuCubeShowcaseClock.Restart();
            imuCubeShowcaseTimer.Start();
        }

        private void StopImuCubeShowcase()
        {
            if (!imuCubeShowcaseActive)
                return;
            imuCubeShowcaseActive = false;
            imuCubeShowcaseTimer.Stop();
            imuCubeShowcaseClock.Stop();
        }

        private void ImuCubeShowcaseTimer_Tick(object sender, EventArgs e)
        {
            if (!imuCubeShowcaseActive || imuCubeRotation == null)
                return;
            double seconds = imuCubeShowcaseClock.Elapsed.TotalSeconds;
            Quaternion yaw = new Quaternion(new Vector3D(0, 1, 0),
                (seconds * 29.0) % 360.0);
            Quaternion pitch = new Quaternion(new Vector3D(1, 0, 0),
                (seconds * 17.0) % 360.0);
            Quaternion roll = new Quaternion(new Vector3D(0, 0, 1),
                (seconds * 11.0) % 360.0);
            Quaternion target = yaw * pitch * roll;
            target.Normalize();
            imuCubeRotation.BeginAnimation(
                QuaternionRotation3D.QuaternionProperty, null);
            imuCubeRotation.Quaternion = target;
            imuCubeCurrent = target;
        }

        private void UpdateImuCubeFromReading(ImuSensorReading imu)
        {
            Quaternion sample = new Quaternion(imu.QuaternionX,
                imu.QuaternionY, imu.QuaternionZ, imu.QuaternionW);
            double lengthSquared = sample.X * sample.X + sample.Y * sample.Y +
                sample.Z * sample.Z + sample.W * sample.W;
            bool quaternionValid = !double.IsNaN(lengthSquared) &&
                !double.IsInfinity(lengthSquared) && lengthSquared >= 0.000001;
            bool identityQuaternion = quaternionValid &&
                Math.Abs(sample.X) < 0.0005 && Math.Abs(sample.Y) < 0.0005 &&
                Math.Abs(sample.Z) < 0.0005 &&
                Math.Abs(Math.Abs(sample.W) - 1.0) < 0.0005;
            bool zeroEuler = Math.Abs(imu.HeadingDegrees) < 0.05 &&
                Math.Abs(imu.RollDegrees) < 0.05 &&
                Math.Abs(imu.PitchDegrees) < 0.05;
            bool zeroOrientation = identityQuaternion && zeroEuler;

            if (!imuOrientationFilter.ShouldAccept(quaternionValid,
                    zeroOrientation))
            {
                if (imuOrientationFilter.HasAcceptedSample)
                {
                    ImuVisualizationStatusText.Text = zeroOrientation ?
                        "HOLD · ZERO SAMPLE" : "HOLD · INVALID SAMPLE";
                    ImuVisualizationStatusText.Foreground =
                        (Brush)FindResource("GoldBrush");
                }
                else
                {
                    UpdateImuCubeOrientation(Quaternion.Identity, false);
                }
                return;
            }

            UpdateImuCubeOrientation(sample, true);
        }

        private void UpdateImuCubeOrientation(Quaternion orientation, bool valid)
        {
            if (imuCubeRotation == null)
                return;

            Quaternion target = orientation;
            double lengthSquared = target.X * target.X + target.Y * target.Y +
                target.Z * target.Z + target.W * target.W;
            if (!valid || double.IsNaN(lengthSquared) ||
                double.IsInfinity(lengthSquared) || lengthSquared < 0.000001)
            {
                target = Quaternion.Identity;
                valid = false;
            }
            else
            {
                target.Normalize();
                double dot = imuCubeCurrent.X * target.X +
                    imuCubeCurrent.Y * target.Y + imuCubeCurrent.Z * target.Z +
                    imuCubeCurrent.W * target.W;
                if (dot < 0.0)
                    target = new Quaternion(-target.X, -target.Y, -target.Z, -target.W);
            }

            QuaternionAnimation animation = new QuaternionAnimation(
                imuCubeCurrent, target, TimeSpan.FromMilliseconds(260))
            {
                EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut },
                FillBehavior = FillBehavior.HoldEnd,
                UseShortestPath = true
            };
            imuCubeRotation.BeginAnimation(QuaternionRotation3D.QuaternionProperty,
                animation, HandoffBehavior.SnapshotAndReplace);
            imuCubeCurrent = target;
            ImuVisualizationStatusText.Text = valid ? "LIVE" : "WAITING";
            ImuVisualizationStatusText.Foreground = (Brush)FindResource(
                valid ? "AccentBrush" : "MutedBrush");
        }

        private static string FormatSensorVector(SensorVector3 vector)
        {
            return vector == null ? "X —   Y —   Z —" :
                string.Format(CultureInfo.InvariantCulture,
                    "X {0,7:F3}   Y {1,7:F3}   Z {2,7:F3}",
                    vector.X, vector.Y, vector.Z);
        }

        private async void Navigate_Checked(object sender, RoutedEventArgs e)
        {
            RadioButton button = sender as RadioButton;
            if (button == null || !IsInitialized)
                return;
            string page = Convert.ToString(button.Tag, CultureInfo.InvariantCulture);
            ShowPage(page);
            if (page == "Gnss")
                await RefreshUbloxAsync(false);
            else if (page == "Atomic")
                await RefreshAtomicAsync(false);
            else if (page == "Oscillatord")
            {
                await RefreshNativeDisciplineAsync(false);
                await RefreshOscillatordAsync(false);
            }
            else if (page == "Uart" && UartPortCombo.SelectedIndex == 3)
                await RefreshNmeaAsync(false);
            else if (page == "Sma")
                await RefreshSmaAsync();
            else if (page == "Timing")
                await RefreshTimingAsync();
            else if (page == "Fpga")
                await RefreshFpgaCoresAsync();
            else if (page == "Sensors")
                await RefreshSensorsAsync();
            else if (page == "I2c")
                await RefreshI2cAsync(false);
            else if (page == "Subsystems")
                await RefreshSubsystemsAsync();
        }

        private void ShowPage(string name)
        {
            if (OverviewPage == null)
                return;
            if (name == "Uart")
                RefreshGenericComPorts(false);
            OverviewPage.Visibility = name == "Overview" ? Visibility.Visible : Visibility.Collapsed;
            ClockPage.Visibility = name == "Clock" ? Visibility.Visible : Visibility.Collapsed;
            GnssPage.Visibility = name == "Gnss" ? Visibility.Visible : Visibility.Collapsed;
            AtomicPage.Visibility = name == "Atomic" ? Visibility.Visible : Visibility.Collapsed;
            OscillatordPage.Visibility = name == "Oscillatord" ? Visibility.Visible : Visibility.Collapsed;
            UartPage.Visibility = name == "Uart" ? Visibility.Visible : Visibility.Collapsed;
            SmaPage.Visibility = name == "Sma" ? Visibility.Visible : Visibility.Collapsed;
            TimingPage.Visibility = name == "Timing" ? Visibility.Visible : Visibility.Collapsed;
            FpgaPage.Visibility = name == "Fpga" ? Visibility.Visible : Visibility.Collapsed;
            SensorsPage.Visibility = name == "Sensors" ? Visibility.Visible : Visibility.Collapsed;
            I2cPage.Visibility = name == "I2c" ? Visibility.Visible : Visibility.Collapsed;
            TelemetryPage.Visibility = name == "Telemetry" ? Visibility.Visible : Visibility.Collapsed;
            OperationsPage.Visibility = name == "Operations" ? Visibility.Visible : Visibility.Collapsed;
            FlashPage.Visibility = name == "Flash" ? Visibility.Visible : Visibility.Collapsed;
            SubsystemsPage.Visibility = name == "Subsystems" ? Visibility.Visible : Visibility.Collapsed;
            DiagnosticsPage.Visibility = name == "Diagnostics" ? Visibility.Visible : Visibility.Collapsed;
            string title = name == "Gnss" ? "GNSS & Time-of-Day" :
                name == "Atomic" ? "Atomic Clock" :
                name == "Oscillatord" ? "Oscillator Discipline" :
                name == "Uart" ? "UART Console" :
                name == "Sma" ? "SMA Connectors" :
                name == "Timing" ? "Generators & Frequency" :
                name == "Fpga" ? "FPGA Engines" :
                name == "Sensors" ? "Sensors & IMU" :
                name == "Telemetry" ? "Telemetry Studio" :
                name == "Operations" ? "Profiles & Self-Test" :
                name == "Flash" ? "FPGA SPI Flash" :
                name == "I2c" ? "I²C Bus" : name;
            TopPageTitle.Text = title;
        }

        private void Elevate_Click(object sender, RoutedEventArgs e)
        {
            RestartAsAdministrator();
        }

        private bool RestartAsAdministrator()
        {
            try
            {
                ProcessStartInfo startInfo = new ProcessStartInfo
                {
                    FileName = Assembly.GetEntryAssembly().Location,
                    UseShellExecute = true,
                    Verb = "runas",
                    Arguments = string.Join(" ", startupArguments.Skip(1).Select(QuoteProcessArgument))
                };
                Process.Start(startInfo);
                Application.Current.Shutdown();
                return true;
            }
            catch (Win32Exception ex)
            {
                Log("Elevation was not completed: " + ex.Message);
                return false;
            }
        }

        private static bool HasAdministratorAccess()
        {
            try
            {
                using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
                {
                    WindowsPrincipal principal = new WindowsPrincipal(identity);
                    return principal.IsInRole(WindowsBuiltInRole.Administrator);
                }
            }
            catch
            {
                return false;
            }
        }

        private static string QuoteProcessArgument(string argument)
        {
            if (string.IsNullOrEmpty(argument))
                return "\"\"";
            if (!argument.Any(character => char.IsWhiteSpace(character) || character == '\"'))
                return argument;

            StringBuilder quoted = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char character in argument)
            {
                if (character == '\\')
                {
                    backslashes++;
                    continue;
                }

                if (character == '\"')
                {
                    quoted.Append('\\', (backslashes * 2) + 1);
                    quoted.Append('\"');
                    backslashes = 0;
                    continue;
                }

                quoted.Append('\\', backslashes);
                backslashes = 0;
                quoted.Append(character);
            }
            quoted.Append('\\', backslashes * 2);
            quoted.Append('\"');
            return quoted.ToString();
        }

        private void ConnectionChip_Click(object sender, RoutedEventArgs e)
        {
            if (ElevateButton.Visibility == Visibility.Visible)
                Elevate_Click(sender, e);
        }

        private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ClickCount == 2)
                ToggleMaximize();
            else if (e.LeftButton == MouseButtonState.Pressed)
                DragMove();
        }

        private void Minimize_Click(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        private void Maximize_Click(object sender, RoutedEventArgs e)
        {
            ToggleMaximize();
        }

        private void ToggleMaximize()
        {
            WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
        }

        private void Close_Click(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void Window_StateChanged(object sender, EventArgs e)
        {
            RootBorder.CornerRadius = WindowState == WindowState.Maximized ? new CornerRadius(0) : new CornerRadius(16);
        }

        private static string FormatUtc(DateTime value)
        {
            return value.ToUniversalTime().ToString("yyyy-MM-dd  HH:mm:ss.fffffff 'UTC'", CultureInfo.InvariantCulture);
        }

        private static string FormatNanoseconds(long nanoseconds)
        {
            double absolute = Math.Abs((double)nanoseconds);
            if (absolute < 1000)
                return nanoseconds.ToString(CultureInfo.InvariantCulture) + " ns";
            if (absolute < 1000000)
                return (nanoseconds / 1000.0).ToString("N3", CultureInfo.InvariantCulture) + " µs";
            if (absolute < 1000000000)
                return (nanoseconds / 1000000.0).ToString("N3", CultureInfo.InvariantCulture) + " ms";
            if (absolute < 60000000000.0)
                return (nanoseconds / 1000000000.0).ToString("N6", CultureInfo.InvariantCulture) + " s";
            if (absolute < 3600000000000.0)
                return (nanoseconds / 60000000000.0).ToString("N3", CultureInfo.InvariantCulture) + " min";
            if (absolute < 86400000000000.0)
                return (nanoseconds / 3600000000000.0).ToString("N3", CultureInfo.InvariantCulture) + " h";
            return (nanoseconds / 86400000000000.0).ToString("N3", CultureInfo.InvariantCulture) + " d";
        }

    }
}
