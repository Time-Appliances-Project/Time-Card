using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
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
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace TimeCardControlCenter
{
    public partial class MainWindow : Window
    {
        private readonly DispatcherTimer refreshTimer;
        private readonly string[] startupArguments;
        private TimeCardClient client;
        private TimeCardSnapshot lastSnapshot;
        private CancellationTokenSource uartMonitorCancellation;
        private readonly List<UartConsoleEntry> uartConsoleEntries = new List<UartConsoleEntry>();
        private int uartConsoleHistoryBytes;
        private bool refreshing;
        private bool connecting;
        private bool smaUpdatingUi;
        private bool timingRefreshing;
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
        private bool? secondaryGnssPresent;
        private DateTime lastSmaLedRefreshUtc = DateTime.MinValue;
        private DateTime lastSecondaryLedObservationUtc = DateTime.MinValue;
        private bool atomicRefreshing;
        private Sa53Snapshot lastSa53Snapshot;
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
            public CheckBox Invert { get; set; }
            public ComboBox Route { get; set; }
            public TextBlock Detail { get; set; }
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
            public string Direction { get; set; }
            public byte[] Data { get; set; }
            public uint LineStatus { get; set; }
        }

        private sealed class I2cRefreshResult
        {
            public I2cControllerStatus Status { get; set; }
            public I2cMuxState Mux { get; set; }
            public I2cProbeResult BoardEeprom { get; set; }
            public I2cProbeResult MacEeprom { get; set; }
            public List<uint> Addresses { get; set; }
            public bool FullScan { get; set; }
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

        public MainWindow()
        {
            InitializeComponent();
            signalGeneratorControls = new[]
            {
                new SignalGeneratorControls { Status = Generator1StatusText, Enabled = Generator1EnabledCheckBox, Frequency = Generator1FrequencyTextBox, Duty = Generator1DutyTextBox, Phase = Generator1PhaseTextBox, Invert = Generator1InvertCheckBox, Route = Generator1RouteCombo, Detail = Generator1DetailText, Apply = Generator1ApplyButton },
                new SignalGeneratorControls { Status = Generator2StatusText, Enabled = Generator2EnabledCheckBox, Frequency = Generator2FrequencyTextBox, Duty = Generator2DutyTextBox, Phase = Generator2PhaseTextBox, Invert = Generator2InvertCheckBox, Route = Generator2RouteCombo, Detail = Generator2DetailText, Apply = Generator2ApplyButton },
                new SignalGeneratorControls { Status = Generator3StatusText, Enabled = Generator3EnabledCheckBox, Frequency = Generator3FrequencyTextBox, Duty = Generator3DutyTextBox, Phase = Generator3PhaseTextBox, Invert = Generator3InvertCheckBox, Route = Generator3RouteCombo, Detail = Generator3DetailText, Apply = Generator3ApplyButton },
                new SignalGeneratorControls { Status = Generator4StatusText, Enabled = Generator4EnabledCheckBox, Frequency = Generator4FrequencyTextBox, Duty = Generator4DutyTextBox, Phase = Generator4PhaseTextBox, Invert = Generator4InvertCheckBox, Route = Generator4RouteCombo, Detail = Generator4DetailText, Apply = Generator4ApplyButton }
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
        }

        private void ApplyStartupView(string[] arguments)
        {
            string page = null;
            int? uartPort = null;
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
            }

            if (uartPort.HasValue)
                UartPortCombo.SelectedIndex = uartPort.Value;
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
                page.Equals("Uart", StringComparison.OrdinalIgnoreCase) ? UartNav :
                page.Equals("Sma", StringComparison.OrdinalIgnoreCase) ? SmaNav :
                page.Equals("Timing", StringComparison.OrdinalIgnoreCase) ? TimingNav :
                page.Equals("I2c", StringComparison.OrdinalIgnoreCase) ? I2cNav :
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
            if (string.IsNullOrWhiteSpace(capturePath) && !HasAdministratorAccess())
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
            await ConnectAsync();
            ApplyStartupView(startupArguments);
            if (!string.IsNullOrWhiteSpace(capturePath))
            {
                await Task.Delay(300);
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
            refreshTimer.Stop();
            StopUartMonitor();
            if (client != null)
            {
                client.Dispose();
                client = null;
            }
        }

        private async void RefreshTimer_Tick(object sender, EventArgs e)
        {
            if (client == null)
            {
                if (!connecting && DateTime.UtcNow - lastConnectionAttemptUtc > TimeSpan.FromSeconds(5))
                    await ConnectAsync();
                return;
            }
            await RefreshSnapshotAsync(false);
        }

        private async Task ConnectAsync()
        {
            if (connecting || client != null)
                return;
            connecting = true;
            lastConnectionAttemptUtc = DateTime.UtcNow;
            SetConnectionState(false, "Connecting…", false);
            try
            {
                client = await Task.Run(() => new TimeCardClient());
                SetConnectionState(true, "Time Card Connected", false);
                Log("Connected to \\.\\TimeCard0.");
                await RefreshSnapshotAsync(true);
                await RefreshIdentityAsync();
            }
            catch (Win32Exception ex)
            {
                bool accessDenied = ex.NativeErrorCode == 5;
                SetConnectionState(false, accessDenied ? "Administrator required" : "Driver unavailable", accessDenied);
                SidebarDriverText.Text = accessDenied ? "Elevate to access hardware" : "Time Card not available";
                Log(string.Format("Connection failed: {0} (Windows error {1}).", ex.Message, ex.NativeErrorCode));
            }
            catch (Exception ex)
            {
                SetConnectionState(false, "Connection failed", false);
                Log("Connection failed: " + ex.Message);
            }
            finally
            {
                connecting = false;
            }
        }

        private async Task RefreshSnapshotAsync(bool logSuccess)
        {
            if (refreshing || client == null)
                return;
            refreshing = true;
            try
            {
                TimeCardSnapshot snapshot = await Task.Run(() => client.GetSnapshot());
                lastSnapshot = snapshot;
                ApplySnapshot(snapshot);
                if (logSuccess)
                    Log(string.Format("Driver {0}, ABI {1}, {2} with {3} interrupt messages.",
                        snapshot.DriverVersion, snapshot.AbiVersion, snapshot.Layout, snapshot.InterruptMessages));
            }
            catch (Exception ex)
            {
                Log("Telemetry refresh failed: " + ex.Message);
                if (client != null)
                {
                    client.Dispose();
                    client = null;
                }
                SetConnectionState(false, "Connection lost", false);
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

            SidebarDriverText.Text = "Driver " + snapshot.DriverVersion + " · ABI " + snapshot.AbiVersion;
            LastRefreshText.Text = "Sampled " + DateTime.Now.ToString("HH:mm:ss", CultureInfo.InvariantCulture);
            UpdateI2cDriverCompatibility(snapshot);

            SyncStatusText.Text = snapshot.IsClockSynchronized ? "IN SYNC" : "NOT LOCKED";
            SyncStatusText.Foreground = snapshot.IsClockSynchronized ? healthyBrush : warningBrush;
            SyncDetailText.Text = string.Format("Status 0x{0:X8}", snapshot.ClockStatus);
            GnssFixMetricText.Text = snapshot.GnssFix;
            GnssFixMetricText.Foreground = snapshot.GnssFixOk ? healthyBrush : warningBrush;
            SatelliteText.Text = snapshot.SatelliteDataValid
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
            ClockChipText.Text = snapshot.IsClockSynchronized ? "SYNCHRONIZED" : "LIVE · UNLOCKED";
            ClockChipText.Foreground = snapshot.IsClockSynchronized ? healthyBrush : warningBrush;
            HierarchyOverviewText.Text = snapshot.HierarchyRuntimeEnabled ? "ENABLED" : "DISABLED";

            ClockCardTimeText.Text = cardTime;
            ClockSystemTimeText.Text = systemTime;
            ClockOffsetText.Text = offset;
            ClockSyncText.Text = snapshot.IsClockSynchronized ? "IN SYNC" : "NOT IN SYNC";
            ClockSyncText.Foreground = snapshot.IsClockSynchronized ? healthyBrush : warningBrush;
            ClockStatusRawText.Text = string.Format("Status register 0x{0:X8}", snapshot.ClockStatus);
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
            GnssFixValidityText.Text = snapshot.GnssFixOk ? "Receiver reports a valid fix" : "Fix not currently asserted";
            SatellitesSeenText.Text = snapshot.SeenSatellites.ToString(CultureInfo.InvariantCulture);
            SatellitesLockedText.Text = snapshot.LockedSatellites.ToString(CultureInfo.InvariantCulture);
            SatelliteValidityText.Text = snapshot.SatelliteDataValid ? "Satellite count is valid" : "Satellite count not valid";
            bool utcValid = (snapshot.UtcStatus & (1u << 8)) != 0;
            int utcOffset = (int)(snapshot.UtcStatus & 0xff);
            UtcMetricText.Text = utcValid ? "UTC +" + utcOffset : "NOT VALID";
            LeapMetricText.Text = (snapshot.UtcStatus & (1u << 16)) != 0
                ? string.Format("Leap info valid · next {0} s", unchecked((int)snapshot.Leap))
                : "Leap information not valid";
            GnssRawText.Text = string.Format("0x{0:X8}", snapshot.GnssStatus);
            SatelliteRawText.Text = string.Format("0x{0:X8}", snapshot.Satellites);
            TodRawText.Text = string.Format("0x{0:X8}", snapshot.TodStatus);
            UtcRawText.Text = string.Format("0x{0:X8}", snapshot.UtcStatus);
            LeapRawText.Text = string.Format("0x{0:X8}", snapshot.Leap);

            HierarchyRuntimeText.Text = snapshot.HierarchyRuntimeEnabled ? "enabled" : "disabled";
            HierarchyRuntimeText.Foreground = snapshot.HierarchyRuntimeEnabled ? healthyBrush : warningBrush;
            HierarchyPersistedText.Text = snapshot.HierarchyPersisted ? "enabled" : "disabled";
            HierarchyPersistedText.Foreground = snapshot.HierarchyPersisted ? healthyBrush : warningBrush;
            DiagnosticsSummaryText.Text = BuildDiagnostics(snapshot);
            UpdateBoardLedAutomationAsync(false);
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

        private async void ConfigureUart_Click(object sender, RoutedEventArgs e)
        {
            if (!EnsureConnected())
                return;
            try
            {
                uint port = SelectedUartPort();
                uint baud = ParseUnsigned(UartBaudCombo.Text, "baud rate", 1, uint.MaxValue);
                await Task.Run(() => client.ConfigureUart(port, baud));
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
            if (!EnsureConnected())
                return;
            try
            {
                uint port = SelectedUartPort();
                uint bytes = ParseUnsigned(UartBytesTextBox.Text, "byte count", 1, 256);
                uint timeout = ParseUnsigned(UartTimeoutTextBox.Text, "timeout", 0, 5000);
                UartReadResult result = await Task.Run(() => client.ReadUart(port, bytes, timeout));
                AppendUart("RX", result.Data, result.LineStatus);
                UartStatusText.Text = string.Format("Read {0} byte(s) · LSR 0x{1:X2}", result.Data.Length, result.LineStatus & 0xff);
            }
            catch (Exception ex)
            {
                ShowUartError("UART read failed", ex);
            }
        }

        private async void SendUart_Click(object sender, RoutedEventArgs e)
        {
            if (!EnsureConnected())
                return;
            try
            {
                byte[] data = ParseUartSendData();
                if (data.Length == 0)
                    throw new InvalidOperationException("Enter text or hexadecimal bytes to send.");
                uint port = SelectedUartPort();
                UartWriteResult result = await Task.Run(() => client.WriteUart(port, data, 1000));
                AppendUart("TX", data, result.LineStatus);
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
            if (!EnsureConnected())
                return;
            uartMonitorCancellation = new CancellationTokenSource();
            MonitorButton.Content = "Stop monitor";
            UartStatusText.Text = "Monitoring UART " + SelectedUartPort();
            MonitorUartAsync(uartMonitorCancellation.Token);
        }

        private async void MonitorUartAsync(CancellationToken cancellationToken)
        {
            try
            {
                uint port = SelectedUartPort();
                uint bytes = ParseUnsigned(UartBytesTextBox.Text, "byte count", 1, 256);
                while (!cancellationToken.IsCancellationRequested)
                {
                    UartReadResult result = await Task.Run(() => client.ReadUart(port, bytes, 250));
                    if (result.Data.Length != 0)
                    {
                        AppendUart("RX", result.Data, result.LineStatus);
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
                ShowUartError("UART monitor stopped", ex);
            }
            finally
            {
                if (uartMonitorCancellation != null)
                {
                    uartMonitorCancellation.Dispose();
                    uartMonitorCancellation = null;
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

        private void ClearUart_Click(object sender, RoutedEventArgs e)
        {
            uartConsoleEntries.Clear();
            uartConsoleHistoryBytes = 0;
            UartOutputTextBox.Clear();
            UartStatusText.Text = "UART terminal cleared";
        }

        private void UartFormat_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (UartOutputTextBox == null)
                return;
            RenderUartConsole();
            if (UartStatusText != null && uartConsoleEntries.Count != 0)
                UartStatusText.Text = "UART display changed to " + SelectedUartDisplayName();
        }

        private async void UartPort_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            StopUartMonitor();
            if (UartStatusText == null || UartPortCombo.SelectedIndex < 0)
                return;
            uint port = SelectedUartPort();
            UartStatusText.Text = "UART " + port + " selected";
            if (NmeaConfigPanel != null)
                NmeaConfigPanel.Visibility = port == 3 ? Visibility.Visible : Visibility.Collapsed;
            if (port == 3)
                await RefreshNmeaAsync(false);
        }

        private async void RefreshNmea_Click(object sender, RoutedEventArgs e)
        {
            await RefreshNmeaAsync(true);
        }

        private async Task RefreshNmeaAsync(bool showError)
        {
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
            try
            {
                uint baud = ParseUnsigned(SelectedComboText(NmeaBaudCombo),
                    "NMEA baud rate", 1200, 2000000);
                bool enabled = NmeaEnabledCheckBox.IsChecked == true;
                bool inverted = NmeaPolarityCheckBox.IsChecked == true;
                NmeaApplyButton.IsEnabled = false;
                bool monitorWasRunning = uartMonitorCancellation != null;
                StopUartMonitor();
                for (int attempt = 0; monitorWasRunning &&
                     uartMonitorCancellation != null && attempt < 20; attempt++)
                    await Task.Delay(50);
                NmeaOutputState state = await Task.Run(() =>
                    client.SetNmeaOutput(enabled, baud, inverted));
                ApplyNmeaState(state);
                UartPortCombo.SelectedIndex = 3;
                UartBaudCombo.Text = state.Baud.ToString(CultureInfo.InvariantCulture);
                Log(string.Format(CultureInfo.InvariantCulture,
                    "NMEA generator {0} at {1} baud{2}.",
                    state.IsEnabled ? "enabled" : "disabled", state.Baud,
                    state.IsInverted ? " with inverted polarity" : string.Empty));
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
                    lastSnapshot != null && lastSnapshot.AbiVersion >= 4;
            }
        }

        private void ApplyNmeaState(NmeaOutputState state)
        {
            Brush stateBrush = (Brush)FindResource(
                state.IsEnabled ? "AccentBrush" : "GoldBrush");
            NmeaStatusText.Text = state.IsPresent ?
                (state.IsEnabled ? "ENABLED · READY TO MONITOR" : "DISABLED") :
                "NOT PRESENT";
            NmeaStatusText.Foreground = stateBrush;
            NmeaEnabledCheckBox.IsChecked = state.IsEnabled;
            NmeaPolarityCheckBox.IsChecked = state.IsInverted;
            SelectComboText(NmeaBaudCombo,
                state.Baud.ToString(CultureInfo.InvariantCulture));
            UartBaudCombo.Text = state.Baud.ToString(CultureInfo.InvariantCulture);
            NmeaRegisterText.Text = string.Format(CultureInfo.InvariantCulture,
                "Control 0x{0:X8} · Status 0x{1:X8} · Version 0x{2:X8}",
                state.Control, state.Status, state.Version);
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
                lastUbloxPort = port;
                lastUbloxBaud = baud;
                ApplyUbloxSnapshot(snapshot);
                UbloxConnectionText.Text = snapshot.ConfigurationSupported ?
                    (snapshot.Warnings.Count == 0 ? "LIVE · UBX CFG" : "LIVE · PARTIAL") :
                    "LIVE · STATUS ONLY";
                UbloxConnectionText.Foreground = (Brush)FindResource(
                    snapshot.ConfigurationSupported ? "AccentBrush" : "GoldBrush");
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
            RenderUbloxSkyMap(snapshot);

            UbloxRatePanel.IsEnabled = snapshot.RateConfigurationSupported;
            UbloxSignalPanel.IsEnabled = snapshot.SignalConfigurationSupported;
            UbloxTimePulsePanel.IsEnabled = snapshot.TimePulseConfigurationSupported;
            UbloxMessagePanel.IsEnabled = snapshot.MessageConfigurationSupported;
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
                bool? persist = ConfirmUbloxPersistence();
                if (!persist.HasValue)
                    return;
                await RunUbloxOperationAsync("u-blox UART 1 message rates updated",
                    receiver => receiver.ApplyMessageRates(navPvt, gga, gsa,
                        gsv, rmc, zda, persist.Value));
            }
            catch (Exception ex)
            {
                ShowUbloxError("Message-rate configuration was not applied", ex);
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

        private async void RefreshAtomic_Click(object sender, RoutedEventArgs e)
        {
            await RefreshAtomicAsync(true);
        }

        private async Task RefreshAtomicAsync(bool showError)
        {
            if (atomicRefreshing)
                return;
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

            SmaFunctionChoice[] choices = direction == SmaDirection.Input ?
                SmaInputFunctions : SmaOutputFunctions;
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
            bool inverted = controls.Invert.IsChecked == true;
            ulong period = 0;
            ulong pulse = 0;
            ulong phase = 0;
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
                    phase = ParseUnsignedLong(controls.Phase.Text,
                        "phase", 0, period - 1);
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
                    client.SetSignalGenerator(generator, enabled, period,
                        pulse, phase, inverted));
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
                    "Generator {0} {1}{2}.", generator,
                    enabled ? "configured and enabled" : "disabled",
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

        private void ApplySignalGeneratorState(SignalGeneratorState state)
        {
            SignalGeneratorControls controls = signalGeneratorControls[state.Generator - 1];
            bool available = state.IsPresent;
            controls.Enabled.IsChecked = state.IsEnabled;
            controls.Invert.IsChecked = state.IsInverted;
            if (state.PeriodNanoseconds != 0)
            {
                controls.Frequency.Text = state.FrequencyHz.ToString(
                    "0.#########", CultureInfo.InvariantCulture);
                controls.Duty.Text = Math.Round(state.DutyPercent).ToString(
                    "0", CultureInfo.InvariantCulture);
                controls.Phase.Text = state.PhaseNanoseconds.ToString(
                    CultureInfo.InvariantCulture);
            }
            controls.Status.Text = state.Status != 0 ?
                string.Format(CultureInfo.InvariantCulture,
                    "FAULT 0x{0:X8}", state.Status) :
                state.IsEnabled ? "RUNNING" : "DISABLED";
            controls.Status.Foreground = state.Status != 0 ?
                (Brush)FindResource("DangerBrush") :
                state.IsEnabled ? (Brush)FindResource("AccentBrush") :
                (Brush)FindResource("MutedBrush");
            controls.Detail.Text = string.Format(CultureInfo.InvariantCulture,
                "Period {0:N0} ns · Start {1}.{2:D9} TAI · v{3:X8}",
                state.PeriodNanoseconds, state.StartSeconds,
                state.StartNanoseconds, state.Version);
            controls.Enabled.IsEnabled = available;
            controls.Frequency.IsEnabled = available;
            controls.Duty.IsEnabled = available;
            controls.Phase.IsEnabled = available;
            controls.Invert.IsEnabled = available;
            controls.Route.IsEnabled = available;
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
            controls.Invert.IsEnabled = false;
            controls.Route.IsEnabled = false;
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
                "Probing the two declared onboard devices...";
            TimeCardClient activeClient = client;
            try
            {
                I2cRefreshResult result = await Task.Run(() =>
                {
                    I2cRefreshResult value = new I2cRefreshResult
                    {
                        Addresses = new List<uint>(),
                        FullScan = fullScan
                    };
                    if (lastSnapshot != null && lastSnapshot.AbiVersion >= 7)
                        value.Mux = activeClient.GetI2cMux();
                    uint first = fullScan ? 0x08u : 0x50u;
                    uint last = fullScan ? 0x77u : 0x58u;
                    for (uint address = first; address <= last; address++)
                    {
                        if (!fullScan && address != 0x50u && address != 0x58u)
                            continue;
                        I2cProbeResult probe = activeClient.ProbeI2c(address);
                        if (address == 0x50u)
                            value.BoardEeprom = probe;
                        else if (address == 0x58u)
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

            ApplyI2cMuxState(result.Mux);

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
            I2cMacStatusText.Text = result.MacEeprom != null &&
                result.MacEeprom.IsPresent ? "PRESENT" : "NO ACK";
            I2cMacStatusText.Foreground = result.MacEeprom != null &&
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
                I2cReadStatusText.Text = "Install Time Card driver 1.13 to enable reliable I\u00B2C reads.";
        }

        private void UpdateI2cDriverCompatibility(TimeCardSnapshot snapshot)
        {
            bool abiSupported = snapshot.AbiVersion >= 3;
            bool reliableReads = abiSupported &&
                DriverVersionAtLeast(snapshot.DriverVersion, 1, 11);
            bool boardControls = snapshot.AbiVersion >= 7;
            Brush stateBrush = (Brush)FindResource(
                boardControls ? "AccentBrush" : "GoldBrush");

            I2cModeIcon.Foreground = stateBrush;
            I2cDriverBadgeText.Foreground = stateBrush;
            I2cLedAutoCheckBox.IsEnabled = boardControls;
            I2cLedManualPanel.IsEnabled = boardControls &&
                I2cLedAutoCheckBox.IsChecked != true;
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
                    "Driver {0} supports reliable reads. Install driver 1.13 / ABI 7 to control the PCA9546A mux and subsystem LEDs.",
                    snapshot.DriverVersion);
                return;
            }

            I2cDriverBadgeText.Text = "UPDATE REQUIRED";
            I2cSafetyBannerText.Text = abiSupported ? string.Format(
                CultureInfo.InvariantCulture,
                "Driver {0} uses the legacy AXI IIC receive sequence that Windows can report as a CRC data error. Install driver 1.13 before reading.",
                snapshot.DriverVersion) :
                "This driver predates I\u00B2C control support. Install Time Card driver 1.13 before using the bus workspace.";
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
                DriverVersionAtLeast(lastSnapshot.DriverVersion, 1, 11);
        }

        private bool SupportsI2cBoardControls()
        {
            return client != null && lastSnapshot != null &&
                lastSnapshot.AbiVersion >= 7;
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
                I2cMuxStatusText.Text = "DRIVER 1.13 / ABI 7 REQUIRED";
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
                    "Install Time Card driver 1.13 / ABI 7 to control the PCA9546A.",
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
                    "Automatic LED mapping requires Time Card driver 1.13 / ABI 7.";
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
                    I2cLedOperationText.Text =
                        "Automatic mapping is active · hardware colors are current.";
                    return;
                }

                await Task.Run(() =>
                {
                    foreach (int index in changed)
                    {
                        BoardLedColor color = desired[index];
                        activeClient.SetBoardLed((uint)index, color.Red,
                            color.Green, color.Blue, 96);
                    }
                });
                if (client != activeClient)
                    return;
                foreach (int index in changed)
                    lastAppliedLedColors[index] = desired[index];
                I2cLedOperationText.Text = string.Format(
                    CultureInfo.InvariantCulture,
                    "Automatic mapping updated {0} indicator{1}; mux route restored after each update.",
                    changed.Count, changed.Count == 1 ? string.Empty : "s");
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
                    "LED {0} read · RGB {1}/{2}/{3} · global current {4}.",
                    led + 1, state.Red, state.Green, state.Blue,
                    state.GlobalCurrent);
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
                    "LED {0} updated · RGB {1}/{2}/{3}; previous PCA9546A route restored.",
                    led + 1, state.Red, state.Green, state.Blue);
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
        }

        private void SetI2cLedButtonsEnabled(bool enabled)
        {
            I2cLedReadButton.IsEnabled = enabled;
            I2cLedOffButton.IsEnabled = enabled;
            I2cLedApplyButton.IsEnabled = enabled;
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

        private static string FormatI2cAddressMap(I2cRefreshResult result)
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
                string name = address == 0x50 ? "board EEPROM" :
                    address == 0x58 ? "MAC identity" :
                    address == 0x70 ? "PCA9546A mux" :
                    address == 0x6e ? "IS32FL3207 status LEDs (CH1)" :
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
                "TX FIFO {7}  RX FIFO {8}\r\n{9}",
                status.Control, DecodeI2cControl(status.Control),
                status.Status, DecodeI2cStatus(status.Status),
                status.InterruptStatus,
                DecodeI2cInterrupts(status.InterruptStatus),
                status.InterruptEnable, status.TxFifoOccupancy,
                status.RxFifoOccupancy, operation);
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
                I2cSubaddressTextBox.Text = "00";
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
                I2cAddressTextBox.Text = "6E";
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
            I2cSubaddressTextBox.Text = "00";
            I2cSubaddressLengthCombo.SelectedIndex = 1;
            I2cLengthTextBox.Text = "6";
            await ExecuteI2cReadAsync();
            await RefreshIdentityAsync();
        }

        private async Task RefreshIdentityAsync()
        {
            if (client == null || lastSnapshot == null ||
                !SupportsReliableI2cReads())
            {
                SidebarSerialText.Text = "Driver 1.12 required";
                I2cSerialNumberText.Text =
                    "Update to driver 1.12 to read the card identity";
                return;
            }

            try
            {
                string serial;
                bool valid;
                if (lastSnapshot.AbiVersion >= 4)
                {
                    TimeCardIdentity identity = await Task.Run(() => client.GetIdentity());
                    serial = identity.SerialNumber;
                    valid = identity.IsPresent && identity.IsValid;
                }
                else
                {
                    I2cReadResult result = await Task.Run(() =>
                        client.ReadI2c(0x58, 0, 1, 6, 100));
                    serial = BitConverter.ToString(result.Data).Replace('-', ':');
                    valid = result.Data.Length == 6 &&
                        result.Data.Any(value => value != 0) &&
                        result.Data.Any(value => value != 0xff);
                }

                SidebarSerialText.Text = serial;
                I2cSerialNumberText.Text = valid ?
                    "Unique card serial " + serial : "Identity bytes invalid: " + serial;
                I2cSerialNumberText.Foreground = (Brush)FindResource(
                    valid ? "CyanBrush" : "GoldBrush");
                Log("Card identity read from 24MAC402: " + serial + ".");
            }
            catch (Exception ex)
            {
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
                    "Reliable I2C reads require Time Card driver 1.11 or later. " +
                    "This computer is running driver " +
                    (lastSnapshot == null ? "an unknown version" :
                     lastSnapshot.DriverVersion) + ".\n\n" +
                     "Install the current driver 1.13 package, then restart the Control Center.",
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
                    "Install Time Card driver 1.13 and retry the read.";
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
                return Encoding.ASCII.GetBytes(UartSendTextBox.Text ?? string.Empty);

            string[] tokens = (UartSendTextBox.Text ?? string.Empty)
                .Split(new[] { ' ', '\t', '\r', '\n', ',', '-', ':' }, StringSplitOptions.RemoveEmptyEntries);
            if (tokens.Length > 256)
                throw new InvalidOperationException("UART writes are limited to 256 bytes.");
            byte[] data = new byte[tokens.Length];
            for (int index = 0; index < tokens.Length; index++)
                data[index] = byte.Parse(tokens[index], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture);
            return data;
        }

        private void AppendUart(string direction, byte[] data, uint lineStatus)
        {
            if (data == null)
                return;
            byte[] capturedData = (byte[])data.Clone();
            UartConsoleEntry entry = new UartConsoleEntry
            {
                Timestamp = DateTime.Now,
                Direction = direction,
                Data = capturedData,
                LineStatus = lineStatus
            };
            uartConsoleEntries.Add(entry);
            uartConsoleHistoryBytes += capturedData.Length;
            while (uartConsoleHistoryBytes > 65536 && uartConsoleEntries.Count > 1)
            {
                uartConsoleHistoryBytes -= uartConsoleEntries[0].Data.Length;
                uartConsoleEntries.RemoveAt(0);
            }
            UartOutputTextBox.AppendText(RenderUartConsoleEntry(entry));
            if (UartOutputTextBox.Text.Length > 131072)
                UartOutputTextBox.Text = UartOutputTextBox.Text.Substring(
                    UartOutputTextBox.Text.Length - 65536);
            UartOutputTextBox.ScrollToEnd();
        }

        private void RenderUartConsole()
        {
            const int maximumCharacters = 131072;
            List<string> renderedEntries = new List<string>(uartConsoleEntries.Count);
            int renderedCharacters = 0;
            for (int index = uartConsoleEntries.Count - 1; index >= 0; index--)
            {
                UartConsoleEntry entry = uartConsoleEntries[index];
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
            string formatted = FormatUartData(entry.Data);
            return string.Format(CultureInfo.InvariantCulture,
                "[{0:HH:mm:ss.fff}] {1} {2} byte(s) · LSR 0x{3:X2}\r\n{4}{5}",
                entry.Timestamp, entry.Direction, entry.Data.Length,
                entry.LineStatus & 0xff, formatted,
                formatted.EndsWith("\n", StringComparison.Ordinal) ? string.Empty : "\r\n");
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
            return uint.Parse(Convert.ToString(item.Tag, CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
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

        private static string BuildDiagnostics(TimeCardSnapshot snapshot)
        {
            return string.Join(Environment.NewLine, new[]
            {
                "Device:       OCP Time Card Controller",
                "Driver:       " + snapshot.DriverVersion + " (ABI " + snapshot.AbiVersion + ")",
                "Transport:    " + snapshot.Layout + " · " + snapshot.InterruptMessages + " interrupts",
                string.Format(CultureInfo.InvariantCulture, "BAR length:    0x{0:X8}", snapshot.BarLength),
                string.Format(CultureInfo.InvariantCulture, "Clock core:    {0} @ 0x{1:X8}", snapshot.ClockVersion, snapshot.ClockOffset),
                string.Format(CultureInfo.InvariantCulture, "Clock status:  0x{0:X8} · {1}", snapshot.ClockStatus,
                    snapshot.IsClockSynchronized ? "in sync" : "not in sync"),
                string.Format(CultureInfo.InvariantCulture, "Clock source:  0x{0:X4}", snapshot.ClockSource),
                string.Format(CultureInfo.InvariantCulture, "ToD status:    0x{0:X8}", snapshot.TodStatus),
                string.Format(CultureInfo.InvariantCulture, "GNSS status:   0x{0:X8} · {1}", snapshot.GnssStatus, snapshot.GnssFix),
                string.Format(CultureInfo.InvariantCulture, "Satellites:    {0} seen · {1} locked · valid {2}",
                    snapshot.SeenSatellites, snapshot.LockedSatellites, snapshot.SatelliteDataValid ? "yes" : "no"),
                "Hierarchy:    " + (snapshot.HierarchyRuntimeEnabled ? "enabled" : "disabled") +
                    " · persisted " + (snapshot.HierarchyPersisted ? "yes" : "no"),
                "Card UTC:     " + FormatUtc(snapshot.CardTimeUtc),
                "System UTC:   " + FormatUtc(snapshot.SystemTimeUtc),
                "PHC offset:   " + FormatNanoseconds(snapshot.OffsetNanoseconds),
                "Sample window:" + " " + FormatNanoseconds(snapshot.SamplingWindowNanoseconds)
            });
        }

        private void ClearLog_Click(object sender, RoutedEventArgs e)
        {
            LogTextBox.Clear();
            Log("Session log cleared.");
        }

        private void Log(string message)
        {
            if (LogTextBox == null)
                return;
            LogTextBox.AppendText(string.Format(CultureInfo.InvariantCulture, "[{0:HH:mm:ss.fff}] {1}\r\n", DateTime.Now, message));
            LogTextBox.ScrollToEnd();
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
            else if (page == "Uart" && UartPortCombo.SelectedIndex == 3)
                await RefreshNmeaAsync(false);
            else if (page == "Sma")
                await RefreshSmaAsync();
            else if (page == "Timing")
                await RefreshTimingAsync();
            else if (page == "I2c")
                await RefreshI2cAsync(false);
            else if (page == "Subsystems")
                await RefreshSubsystemsAsync();
        }

        private void ShowPage(string name)
        {
            if (OverviewPage == null)
                return;
            OverviewPage.Visibility = name == "Overview" ? Visibility.Visible : Visibility.Collapsed;
            ClockPage.Visibility = name == "Clock" ? Visibility.Visible : Visibility.Collapsed;
            GnssPage.Visibility = name == "Gnss" ? Visibility.Visible : Visibility.Collapsed;
            AtomicPage.Visibility = name == "Atomic" ? Visibility.Visible : Visibility.Collapsed;
            UartPage.Visibility = name == "Uart" ? Visibility.Visible : Visibility.Collapsed;
            SmaPage.Visibility = name == "Sma" ? Visibility.Visible : Visibility.Collapsed;
            TimingPage.Visibility = name == "Timing" ? Visibility.Visible : Visibility.Collapsed;
            I2cPage.Visibility = name == "I2c" ? Visibility.Visible : Visibility.Collapsed;
            FlashPage.Visibility = name == "Flash" ? Visibility.Visible : Visibility.Collapsed;
            SubsystemsPage.Visibility = name == "Subsystems" ? Visibility.Visible : Visibility.Collapsed;
            DiagnosticsPage.Visibility = name == "Diagnostics" ? Visibility.Visible : Visibility.Collapsed;
            string title = name == "Gnss" ? "GNSS & Time-of-Day" :
                name == "Atomic" ? "Atomic Clock" :
                name == "Uart" ? "UART Console" :
                name == "Sma" ? "SMA Connectors" :
                name == "Timing" ? "Generators & Frequency" :
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
            return (nanoseconds / 1000000000.0).ToString("N6", CultureInfo.InvariantCulture) + " s";
        }

    }
}
