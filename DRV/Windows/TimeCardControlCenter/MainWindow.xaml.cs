using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
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
        private bool refreshing;
        private bool connecting;
        private bool smaUpdatingUi;
        private bool i2cRefreshing;
        private bool atomicRefreshing;
        private Sa53Snapshot lastSa53Snapshot;
        private bool ubloxRefreshing;
        private UbloxReceiverSnapshot lastUbloxSnapshot;
        private uint? lastUbloxPort;
        private uint? lastUbloxBaud;
        private DateTime lastConnectionAttemptUtc = DateTime.MinValue;

        private sealed class I2cRefreshResult
        {
            public I2cControllerStatus Status { get; set; }
            public I2cProbeResult BoardEeprom { get; set; }
            public I2cProbeResult MacEeprom { get; set; }
            public List<uint> Addresses { get; set; }
            public bool FullScan { get; set; }
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
            RadioButton navigation = page.Equals("Clock", StringComparison.OrdinalIgnoreCase) ? ClockNav :
                page.Equals("Gnss", StringComparison.OrdinalIgnoreCase) ? GnssNav :
                page.Equals("Atomic", StringComparison.OrdinalIgnoreCase) ? AtomicNav :
                page.Equals("Uart", StringComparison.OrdinalIgnoreCase) ? UartNav :
                page.Equals("Sma", StringComparison.OrdinalIgnoreCase) ? SmaNav :
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
            refreshTimer.Start();
            await ConnectAsync();
            ApplyStartupView(startupArguments);
            string capturePath = StartupArgumentValue(startupArguments, "--capture");
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
                SetConnectionState(true, "Hardware connected", false);
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
        }

        private void SetConnectionState(bool connected, string text, bool showElevation)
        {
            ConnectionText.Text = text;
            ConnectionDot.Fill = (Brush)FindResource(connected ? "AccentBrush" : "DangerBrush");
            ElevateButton.Visibility = showElevation ? Visibility.Visible : Visibility.Collapsed;
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
                Log("PHC set from Windows UTC.");
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
                        ". Set the PHC from Windows or establish GNSS time before using it to set Windows.",
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
            UartOutputTextBox.Clear();
            UartStatusText.Text = "UART terminal cleared";
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
            I2cReadButton.IsEnabled = true;
            I2cBoardReadButton.IsEnabled = result.BoardEeprom != null &&
                result.BoardEeprom.IsPresent;
            I2cMacReadButton.IsEnabled = result.MacEeprom != null &&
                result.MacEeprom.IsPresent;
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
            I2cBoardStatusText.Text = "NOT AVAILABLE";
            I2cMacStatusText.Text = "NOT AVAILABLE";
            I2cSerialNumberText.Text = "Card serial unavailable";
            I2cReadButton.IsEnabled = false;
            I2cBoardReadButton.IsEnabled = false;
            I2cMacReadButton.IsEnabled = false;
        }

        private async void ReadI2c_Click(object sender, RoutedEventArgs e)
        {
            await ExecuteI2cReadAsync();
        }

        private async void ReadBoardEeprom_Click(object sender, RoutedEventArgs e)
        {
            I2cAddressTextBox.Text = "50";
            I2cSubaddressTextBox.Text = "00";
            I2cSubaddressLengthCombo.SelectedIndex = 1;
            I2cLengthTextBox.Text = "32";
            await ExecuteI2cReadAsync();
        }

        private async void ReadMacEeprom_Click(object sender, RoutedEventArgs e)
        {
            I2cAddressTextBox.Text = "58";
            I2cSubaddressTextBox.Text = "00";
            I2cSubaddressLengthCombo.SelectedIndex = 1;
            I2cLengthTextBox.Text = "6";
            await ExecuteI2cReadAsync();
            await RefreshIdentityAsync();
        }

        private async Task RefreshIdentityAsync()
        {
            if (client == null || lastSnapshot == null || lastSnapshot.AbiVersion < 3)
            {
                SidebarSerialText.Text = "Driver update required";
                I2cSerialNumberText.Text = "Card serial requires ABI 3+";
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
            if (lastSnapshot != null && lastSnapshot.AbiVersion < 3)
            {
                MessageBox.Show(this,
                    "I2C access requires Time Card driver 1.8 or later.",
                    "Driver update required", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }

            try
            {
                uint address = ParseHex(I2cAddressTextBox.Text,
                    "7-bit I2C address", 0x08, 0x77);
                ComboBoxItem lengthItem =
                    I2cSubaddressLengthCombo.SelectedItem as ComboBoxItem;
                if (lengthItem == null)
                    throw new InvalidOperationException("Select a subaddress length.");
                uint subaddressLength = uint.Parse(
                    Convert.ToString(lengthItem.Tag, CultureInfo.InvariantCulture),
                    CultureInfo.InvariantCulture);
                uint subaddressMaximum = subaddressLength == 2 ? 0xffffu :
                    subaddressLength == 1 ? 0xffu : 0u;
                uint subaddress = subaddressLength == 0 ? 0u :
                    ParseHex(I2cSubaddressTextBox.Text, "subaddress", 0,
                        subaddressMaximum);
                uint length = ParseUnsigned(I2cLengthTextBox.Text,
                    "read length", 1, 255);
                TimeCardClient activeClient = client;

                I2cReadButton.IsEnabled = false;
                I2cBoardReadButton.IsEnabled = false;
                I2cMacReadButton.IsEnabled = false;
                I2cReadStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                    "Reading {0} byte(s) from 0x{1:X2}...", length, address);
                I2cReadResult result = await Task.Run(() =>
                    activeClient.ReadI2c(address, subaddress,
                        subaddressLength, length, 100));
                if (client != activeClient)
                    return;

                I2cOutputTextBox.Text = string.Format(CultureInfo.InvariantCulture,
                    "Device 0x{0:X2} · subaddress 0x{1:X4} ({2} byte{3})\r\n\r\n{4}",
                    result.Address, subaddress, subaddressLength,
                    subaddressLength == 1 ? "" : "s",
                    FormatI2cData(result.Data));
                I2cReadStatusText.Text = string.Format(CultureInfo.InvariantCulture,
                    "Read {0} byte(s) · SR 0x{1:X2} · ISR 0x{2:X8}",
                    result.Data.Length, result.ControllerStatus,
                    result.InterruptStatus);
                Log(string.Format(CultureInfo.InvariantCulture,
                    "I2C read: 0x{0:X2}, subaddress 0x{1:X4}, {2} byte(s).",
                    address, subaddress, result.Data.Length));
            }
            catch (Exception ex)
            {
                I2cReadStatusText.Text = "Read failed: " + ex.Message;
                Log("I2C read failed: " + ex.Message);
                MessageBox.Show(this, ex.Message, "I2C read failed",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                bool supported = client != null &&
                    (lastSnapshot == null || lastSnapshot.AbiVersion >= 3);
                I2cReadButton.IsEnabled = supported;
                I2cBoardReadButton.IsEnabled = supported;
                I2cMacReadButton.IsEnabled = supported;
            }
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

        private static string FormatI2cData(byte[] data)
        {
            StringBuilder output = new StringBuilder();
            for (int offset = 0; offset < data.Length; offset += 16)
            {
                output.AppendFormat(CultureInfo.InvariantCulture,
                    "{0:X4}  ", offset);
                for (int index = 0; index < 16; index++)
                {
                    if (offset + index < data.Length)
                        output.AppendFormat(CultureInfo.InvariantCulture,
                            "{0:X2} ", data[offset + index]);
                    else
                        output.Append("   ");
                }
                output.Append(" ");
                for (int index = 0; index < 16 &&
                     offset + index < data.Length; index++)
                {
                    byte value = data[offset + index];
                    output.Append(value >= 0x20 && value <= 0x7e ?
                        (char)value : '.');
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
            string formatted = FormatUartData(data);
            string line = string.Format(CultureInfo.InvariantCulture, "[{0:HH:mm:ss.fff}] {1} {2} byte(s) · LSR 0x{3:X2}\r\n{4}{5}",
                DateTime.Now, direction, data.Length, lineStatus & 0xff, formatted,
                formatted.EndsWith("\n", StringComparison.Ordinal) ? string.Empty : "\r\n");
            UartOutputTextBox.AppendText(line);
            if (UartOutputTextBox.Text.Length > 131072)
                UartOutputTextBox.Text = UartOutputTextBox.Text.Substring(UartOutputTextBox.Text.Length - 65536);
            UartOutputTextBox.ScrollToEnd();
        }

        private string FormatUartData(byte[] data)
        {
            ComboBoxItem item = UartFormatCombo.SelectedItem as ComboBoxItem;
            string mode = item == null ? "Auto" : Convert.ToString(item.Content, CultureInfo.InvariantCulture);
            bool printable = data.Length == 0 || data.Count(value => value == 9 || value == 10 || value == 13 ||
                (value >= 32 && value < 127)) >= data.Length * 0.82;
            if (mode == "Text" || (mode == "Auto" && printable))
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
            return string.Join(" ", data.Select(value => value.ToString("X2", CultureInfo.InvariantCulture)));
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
            else if (page == "I2c")
                await RefreshI2cAsync(false);
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
            I2cPage.Visibility = name == "I2c" ? Visibility.Visible : Visibility.Collapsed;
            SubsystemsPage.Visibility = name == "Subsystems" ? Visibility.Visible : Visibility.Collapsed;
            DiagnosticsPage.Visibility = name == "Diagnostics" ? Visibility.Visible : Visibility.Collapsed;
            string title = name == "Gnss" ? "GNSS & Time-of-Day" :
                name == "Atomic" ? "Atomic Clock" :
                name == "Uart" ? "UART Console" :
                name == "Sma" ? "SMA Connectors" :
                name == "I2c" ? "I²C Bus" : name;
            TopPageTitle.Text = title;
        }

        private void Elevate_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                ProcessStartInfo startInfo = new ProcessStartInfo
                {
                    FileName = Assembly.GetEntryAssembly().Location,
                    UseShellExecute = true,
                    Verb = "runas"
                };
                Process.Start(startInfo);
                Application.Current.Shutdown();
            }
            catch (Win32Exception ex)
            {
                Log("Elevation was not completed: " + ex.Message);
            }
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
