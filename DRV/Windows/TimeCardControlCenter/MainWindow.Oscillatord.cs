using System;
using System.Globalization;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace TimeCardControlCenter
{
    public partial class MainWindow
    {
        private readonly OscillatordClient oscillatordClient =
            new OscillatordClient(TimeSpan.FromSeconds(3));
        private bool oscillatordRefreshing;
        private bool nativeDisciplineRefreshing;
        private TimeCardCapabilities nativeDisciplineCapabilities;
        private NativeDisciplineEngine nativeDisciplineEngine;

        private async void RefreshNativeDiscipline_Click(object sender,
            RoutedEventArgs e)
        {
            await RefreshNativeDisciplineAsync(true);
        }

        private async Task RefreshNativeDisciplineAsync(bool showError)
        {
            if (nativeDisciplineRefreshing)
                return;
            if (productSettings != null && productSettings.DemoMode)
            {
                NativeDisciplineConnectionText.Text = "DEMO · ABI 11";
                NativeDisciplineConnectionText.Foreground =
                    (Brush)FindResource("AccentBrush");
                NativeDisciplineBoardText.Text = "Orolia/Safran ART";
                NativeDisciplineOscillatorText.Text = "Microchip mRO-50";
                NativeDisciplineCapabilityText.Text =
                    "Paired GNSS/internal PPS · direct fine/coarse steering · PHC phase correction · miniCOD 3.6.0";
                ApplyNativeDisciplineStatus(new NativeDisciplineStatus
                {
                    State = "TRACKING",
                    ClockClass = "LOCKED",
                    Detail = "Convergence 100.0% (50/50) · GNSS phase, fix and survey valid · qErr(n-1) -247 ps",
                    LastAction = "Fine control 2,413",
                    PhaseNanoseconds = 7,
                    Fine = 2413,
                    Coarse = 32768,
                    TemperatureCelsius = 42.18,
                    GnssValid = true,
                    OscillatorLocked = true,
                    ReadyForHoldover = true,
                    Running = true
                });
                NativeDisciplineStartButton.IsEnabled = false;
                NativeDisciplineStopButton.IsEnabled = false;
                NativeDisciplineCalibrateButton.IsEnabled = false;
                NativeDisciplineHoldoverButton.IsEnabled = false;
                NativeDisciplineBypassSurveyCheckBox.IsEnabled = false;
                return;
            }
            if (client == null)
            {
                NativeDisciplineConnectionText.Text = "DRIVER REQUIRED";
                NativeDisciplineConnectionText.Foreground =
                    (Brush)FindResource("GoldBrush");
                if (showError)
                    EnsureConnected();
                return;
            }
            nativeDisciplineRefreshing = true;
            NativeDisciplineRefreshButton.IsEnabled = false;
            NativeDisciplineConnectionText.Text = "DETECTING";
            try
            {
                TimeCardCapabilities capabilities = await Task.Run(
                    () => client.GetCapabilities());
                nativeDisciplineCapabilities = capabilities;
                ApplyNativeDisciplineCapabilities(capabilities);
            }
            catch (Exception ex)
            {
                NativeDisciplineConnectionText.Text = "ABI 11 REQUIRED";
                NativeDisciplineConnectionText.Foreground =
                    (Brush)FindResource("DangerBrush");
                NativeDisciplineCapabilityText.Text =
                    "Install Time Card driver 1.38 / ABI 11. " + ex.Message;
                NativeDisciplineStartButton.IsEnabled = false;
                if (showError)
                    MessageBox.Show(this, ex.Message,
                        "Native discipline unavailable", MessageBoxButton.OK,
                        MessageBoxImage.Warning);
            }
            finally
            {
                NativeDisciplineRefreshButton.IsEnabled = true;
                nativeDisciplineRefreshing = false;
            }
        }

        private void ApplyNativeDisciplineCapabilities(
            TimeCardCapabilities capabilities)
        {
            NativeDisciplineConnectionText.Text = "READY · ABI " +
                capabilities.AbiVersion.ToString(CultureInfo.InvariantCulture);
            NativeDisciplineConnectionText.Foreground =
                (Brush)FindResource("AccentBrush");
            NativeDisciplineBoardText.Text = capabilities.BoardName;
            NativeDisciplineOscillatorText.Text = capabilities.OscillatorName;
            if (capabilities.HasDirectMro50 &&
                capabilities.HasPairedPhaseMeter)
            {
                NativeDisciplineCapabilityText.Text = string.Format(
                    CultureInfo.InvariantCulture,
                    "miniCOD software discipline · paired PPS {0}/{1} · fine {2:N0}–{3:N0} · guarded PHC phase correction",
                    capabilities.ReferencePpsIndex,
                    capabilities.OscillatorPpsIndex,
                    capabilities.FineMinimum, capabilities.FineMaximum);
                NativeDisciplineStartButton.Content = "Start discipline";
                NativeDisciplineStartButton.IsEnabled =
                    nativeDisciplineEngine == null ||
                    !nativeDisciplineEngine.IsRunning;
                NativeDisciplineCalibrateButton.IsEnabled =
                    nativeDisciplineEngine != null &&
                    nativeDisciplineEngine.IsRunning;
                NativeDisciplineBypassSurveyCheckBox.IsEnabled =
                    nativeDisciplineEngine == null ||
                    !nativeDisciplineEngine.IsRunning;
            }
            else if (capabilities.HasAtomicUart &&
                capabilities.HasHardwareDiscipline)
            {
                NativeDisciplineCapabilityText.Text =
                    "Hardware-managed oscillator discipline is available through the atomic-clock UART. The app probes the protocol before enabling SA53 controls.";
                NativeDisciplineStartButton.Content = "Open atomic controls";
                NativeDisciplineStartButton.IsEnabled = true;
                NativeDisciplineCalibrateButton.IsEnabled = false;
                NativeDisciplineBypassSurveyCheckBox.IsEnabled = false;
            }
            else
            {
                NativeDisciplineCapabilityText.Text =
                    "PHC monitoring is available. This board does not expose a safely controllable oscillator and remains monitor-only.";
                NativeDisciplineStartButton.Content = "Monitor only";
                NativeDisciplineStartButton.IsEnabled = false;
                NativeDisciplineCalibrateButton.IsEnabled = false;
                NativeDisciplineBypassSurveyCheckBox.IsEnabled = false;
            }
            NativeDisciplineStopButton.IsEnabled =
                nativeDisciplineEngine != null && nativeDisciplineEngine.IsRunning;
            NativeDisciplineHoldoverButton.IsEnabled =
                nativeDisciplineEngine != null && nativeDisciplineEngine.IsRunning;
        }

        private void StartNativeDiscipline_Click(object sender,
            RoutedEventArgs e)
        {
            if (nativeDisciplineCapabilities == null)
                return;
            if (!nativeDisciplineCapabilities.HasDirectMro50)
            {
                AtomicNav.IsChecked = true;
                return;
            }
            if (MessageBox.Show(this,
                "Start Orolia miniCOD discipline on Windows? This continuously measures GNSS/internal PPS phase and may adjust the PHC and mRO-50 fine or coarse controls." +
                (NativeDisciplineBypassSurveyCheckBox.IsChecked == true ?
                    "\n\nGNSS survey validation is BYPASSED. Use this only when the antenna position is already qualified." : string.Empty) +
                "\n\nStop it before removing the card.",
                "Start native oscillator discipline", MessageBoxButton.YesNo,
                MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;
            try
            {
                if (nativeDisciplineEngine == null)
                    nativeDisciplineEngine = new NativeDisciplineEngine(client,
                        nativeDisciplineCapabilities,
                        ReportNativeDisciplineStatus,
                        NativeDisciplineBypassSurveyCheckBox.IsChecked == true);
                nativeDisciplineEngine.Start();
                NativeDisciplineBypassSurveyCheckBox.IsEnabled = false;
                NativeDisciplineStartButton.IsEnabled = false;
                NativeDisciplineStopButton.IsEnabled = true;
                NativeDisciplineCalibrateButton.IsEnabled = true;
                NativeDisciplineHoldoverButton.IsEnabled = true;
                NativeDisciplineConnectionText.Text = "RUNNING";
                Log("Native Windows miniCOD discipline started.");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message,
                    "Native discipline did not start", MessageBoxButton.OK,
                    MessageBoxImage.Warning);
            }
        }

        private async void StopNativeDiscipline_Click(object sender,
            RoutedEventArgs e)
        {
            await StopNativeDisciplineAsync(true);
        }

        private async Task StopNativeDisciplineAsync(bool log)
        {
            if (nativeDisciplineEngine == null)
                return;
            NativeDisciplineStopButton.IsEnabled = false;
            await nativeDisciplineEngine.StopAsync();
            nativeDisciplineEngine.Dispose();
            nativeDisciplineEngine = null;
            NativeDisciplineStartButton.IsEnabled =
                nativeDisciplineCapabilities != null &&
                nativeDisciplineCapabilities.HasDirectMro50;
            NativeDisciplineCalibrateButton.IsEnabled = false;
            NativeDisciplineHoldoverButton.IsEnabled = false;
            NativeDisciplineBypassSurveyCheckBox.IsEnabled =
                nativeDisciplineCapabilities != null &&
                nativeDisciplineCapabilities.HasDirectMro50;
            NativeDisciplineHoldoverButton.Content = "Simulate holdover";
            NativeDisciplineConnectionText.Text = "READY";
            if (log)
                Log("Native Windows miniCOD discipline stopped safely.");
        }

        private void CalibrateNativeDiscipline_Click(object sender,
            RoutedEventArgs e)
        {
            if (nativeDisciplineEngine == null)
                return;
            if (MessageBox.Show(this,
                "Run the full mRO-50 calibration? miniCOD will move through three fine-control points, wait for each to settle, and collect 50 paired PPS samples per point. This takes about three minutes and must not be interrupted.",
                "Calibrate mRO-50", MessageBoxButton.YesNo,
                MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;
            try
            {
                nativeDisciplineEngine.RequestCalibration();
                NativeDisciplineCalibrateButton.IsEnabled = false;
                Log("Native mRO-50 calibration requested.");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Calibration unavailable",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        private void ToggleNativeHoldover_Click(object sender,
            RoutedEventArgs e)
        {
            if (nativeDisciplineEngine == null)
                return;
            bool enabled = !nativeDisciplineEngine.FakeHoldover;
            nativeDisciplineEngine.SetFakeHoldover(enabled);
            NativeDisciplineHoldoverButton.Content = enabled ?
                "End holdover simulation" : "Simulate holdover";
            Log(enabled ? "Native holdover simulation started." :
                "Native holdover simulation stopped.");
        }

        private void ReportNativeDisciplineStatus(
            NativeDisciplineStatus status)
        {
            Dispatcher.BeginInvoke(new Action(() =>
                ApplyNativeDisciplineStatus(status)));
        }

        private void ApplyNativeDisciplineStatus(NativeDisciplineStatus status)
        {
            NativeDisciplineStateText.Text = status.State ?? "—";
            NativeDisciplineClassText.Text = "Clock class " +
                (status.ClockClass ?? "—");
            NativeDisciplinePhaseText.Text =
                FormatOscillatordNanoseconds(status.PhaseNanoseconds);
            NativeDisciplineFineCoarseText.Text = string.Format(
                CultureInfo.InvariantCulture, "{0:N0} / {1:N0}",
                status.Fine, status.Coarse);
            NativeDisciplineTemperatureText.Text =
                status.TemperatureCelsius < -273.15 ? "—" :
                status.TemperatureCelsius.ToString("F2",
                    CultureInfo.InvariantCulture) + " °C";
            NativeDisciplineGnssText.Text = status.GnssValid ?
                "GNSS PPS valid" : "GNSS PPS unavailable";
            NativeDisciplineGnssText.Foreground = (Brush)FindResource(
                status.GnssValid ? "AccentBrush" : "GoldBrush");
            NativeDisciplineActionText.Text = status.LastAction ?? "No action";
            NativeDisciplineDetailText.Text = status.Detail ?? string.Empty;
            NativeDisciplineConnectionText.Text = status.Running ?
                (status.Calibrating ? "CALIBRATING" : "RUNNING") : "READY";
            if (nativeDisciplineEngine != null &&
                nativeDisciplineEngine.IsRunning && !status.Calibrating)
                NativeDisciplineCalibrateButton.IsEnabled = true;
        }

        private async void RefreshOscillatord_Click(object sender, RoutedEventArgs e)
        {
            await RefreshOscillatordAsync(true);
        }

        private async Task RefreshOscillatordAsync(bool showError)
        {
            if (oscillatordRefreshing)
                return;
            if (productSettings != null && productSettings.DemoMode)
            {
                ApplyOscillatordSnapshot(CreateDemoOscillatordSnapshot());
                OscillatordConnectionText.Text = "DEMO";
                OscillatordResultText.Text = "Demo service telemetry · no network request was sent.";
                return;
            }
            oscillatordRefreshing = true;
            OscillatordRefreshButton.IsEnabled = false;
            OscillatordConnectionText.Text = "CONNECTING";
            OscillatordConnectionText.Foreground = (Brush)FindResource("GoldBrush");
            try
            {
                int port = ParseOscillatordPort();
                OscillatordSnapshot snapshot = await oscillatordClient.RequestAsync(
                    OscillatordHostTextBox.Text, port, OscillatordRequest.Status,
                    OscillatordTokenBox.Password);
                ApplyOscillatordSnapshot(snapshot);
                OscillatordResultText.Text = string.Format(CultureInfo.InvariantCulture,
                    "{0:u}  Connected to {1}:{2}. Protocol {3}; {4}.",
                    DateTime.UtcNow, OscillatordHostTextBox.Text.Trim(), port,
                    snapshot.ProtocolVersion,
                    snapshot.ControlEnabled ? "control enabled by service" : "read-only service");
                Log(string.Format(CultureInfo.InvariantCulture,
                    "oscillatord {0} connected at {1}:{2}.",
                    snapshot.Version ?? "unknown", OscillatordHostTextBox.Text.Trim(), port));
            }
            catch (Exception ex)
            {
                OscillatordConnectionText.Text = "UNAVAILABLE";
                OscillatordConnectionText.Foreground = (Brush)FindResource("DangerBrush");
                OscillatordResultText.Text = ex.Message;
                if (showError)
                    MessageBox.Show(this, ex.Message, "oscillatord connection failed",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            finally
            {
                OscillatordRefreshButton.IsEnabled = true;
                oscillatordRefreshing = false;
            }
        }

        private async void OscillatordAction_Click(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            int requestValue;
            if (button == null || !int.TryParse(Convert.ToString(button.Tag,
                CultureInfo.InvariantCulture), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out requestValue))
                return;
            OscillatordRequest request = (OscillatordRequest)requestValue;
            bool readOnly = request == OscillatordRequest.ReadEeprom;
            if (!readOnly)
            {
                string message = string.Format(CultureInfo.InvariantCulture,
                    "Send '{0}' to oscillatord? This can change oscillator, GNSS, holdover, calibration, or persistent timing state.",
                    button.Content);
                if (MessageBox.Show(this, message, "Confirm oscillatord operation",
                    MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
                    return;
            }

            oscillatordRefreshing = true;
            OscillatordRefreshButton.IsEnabled = false;
            try
            {
                OscillatordSnapshot snapshot = await oscillatordClient.RequestAsync(
                    OscillatordHostTextBox.Text, ParseOscillatordPort(), request,
                    OscillatordTokenBox.Password);
                ApplyOscillatordSnapshot(snapshot);
                if (!string.IsNullOrEmpty(snapshot.Error))
                    throw new InvalidOperationException(snapshot.Error);
                OscillatordResultText.Text = string.Format(CultureInfo.InvariantCulture,
                    "{0:u}  Service response: {1}.", DateTime.UtcNow,
                    string.IsNullOrEmpty(snapshot.ActionRequested) ? "accepted" : snapshot.ActionRequested);
                Log("oscillatord action: " + (snapshot.ActionRequested ?? request.ToString()));
            }
            catch (Exception ex)
            {
                OscillatordResultText.Text = ex.Message;
                MessageBox.Show(this, ex.Message, "oscillatord operation failed",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            finally
            {
                OscillatordRefreshButton.IsEnabled = true;
                oscillatordRefreshing = false;
            }
        }

        private int ParseOscillatordPort()
        {
            int port;
            if (!int.TryParse(OscillatordPortTextBox.Text, NumberStyles.None,
                CultureInfo.InvariantCulture, out port) || port < 1 || port > 65535)
                throw new InvalidOperationException("oscillatord port must be between 1 and 65535.");
            return port;
        }

        private void ApplyOscillatordSnapshot(OscillatordSnapshot snapshot)
        {
            OscillatordConnectionText.Text = "LIVE";
            OscillatordConnectionText.Foreground = (Brush)FindResource("AccentBrush");
            OscillatordVersionText.Text = string.Format(CultureInfo.InvariantCulture,
                "{0} {1} · protocol {2}", snapshot.Service ?? "oscillatord",
                snapshot.Version ?? "unknown", snapshot.ProtocolVersion);
            OscillatordControlPolicyText.Text = snapshot.ControlEnabled ?
                "The service permits token-authorized state changes. This client still confirms every operation." :
                "The service is read-only. Enable monitoring-allow-control in its Linux configuration to permit guarded operations.";
            OscillatordControlPolicyText.Foreground = (Brush)FindResource(
                snapshot.ControlEnabled ? "GoldBrush" : "MutedBrush");

            OscillatordDisciplining discipline = snapshot.Disciplining;
            OscillatordStateText.Text = discipline == null ? "—" :
                HumanizeOscillatordState(discipline.Status);
            OscillatordProgressText.Text = discipline == null ? "Discipline data unavailable" :
                string.Format(CultureInfo.InvariantCulture,
                    "{0:F1}% · {1}/{2}", discipline.ConvergenceProgress,
                    discipline.CurrentConvergenceCount, discipline.ConvergenceThreshold);
            OscillatordHoldoverText.Text = discipline == null ? "—" :
                (discipline.ReadyForHoldover ? "Ready" : "Not ready");

            OscillatordClock clock = snapshot.Clock;
            OscillatordOffsetText.Text = clock == null ? "—" :
                FormatOscillatordNanoseconds(clock.OffsetNanoseconds);
            OscillatordClockClassText.Text = clock == null ? "Clock class —" :
                "Clock class " + (clock.Class ?? "unknown");

            OscillatordOscillator oscillator = snapshot.Oscillator;
            OscillatordOscillatorText.Text = oscillator == null ? "—" :
                (oscillator.Model ?? "Unknown");
            OscillatordLockText.Text = oscillator == null ? "Lock unknown" :
                (oscillator.Locked ? "Locked" : "Not locked");
            OscillatordLockText.Foreground = (Brush)FindResource(
                oscillator != null && oscillator.Locked ? "AccentBrush" : "GoldBrush");
            OscillatordFineText.Text = oscillator == null ? "—" :
                oscillator.FineControl.ToString("N0", CultureInfo.InvariantCulture);
            OscillatordCoarseText.Text = oscillator == null ? "—" :
                oscillator.CoarseControl.ToString("N0", CultureInfo.InvariantCulture);
            OscillatordTemperatureText.Text = oscillator == null ||
                oscillator.TemperatureCelsius < -273.15 ? "—" :
                oscillator.TemperatureCelsius.ToString("F2", CultureInfo.InvariantCulture) + " °C";

            OscillatordGnss gnss = snapshot.Gnss;
            OscillatordGnssText.Text = gnss == null ? "—" :
                (gnss.FixOk ? "FIX VALID" : "NO FIX");
            OscillatordGnssText.Foreground = (Brush)FindResource(
                gnss != null && gnss.FixOk ? "AccentBrush" : "GoldBrush");
            OscillatordGnssDetailText.Text = gnss == null ? "GNSS data unavailable" :
                string.Format(CultureInfo.InvariantCulture, "Fix {0} · {1} satellites",
                    gnss.Fix, gnss.Satellites);
            OscillatordSatellitesText.Text = gnss == null ? "—" :
                gnss.Satellites.ToString(CultureInfo.InvariantCulture);
            OscillatordTimeAccuracyText.Text = gnss == null || gnss.TimeAccuracyNanoseconds < 0 ?
                "—" : FormatOscillatordNanoseconds(gnss.TimeAccuracyNanoseconds);
            OscillatordSurveyText.Text = gnss == null || gnss.SurveyPositionErrorMeters < 0 ?
                "—" : gnss.SurveyPositionErrorMeters.ToString("F2", CultureInfo.InvariantCulture) + " m";
            OscillatordLeapText.Text = gnss == null || gnss.LeapSeconds < 0 ? "—" :
                string.Format(CultureInfo.InvariantCulture, "{0} s · pending {1}",
                    gnss.LeapSeconds, gnss.LeapSecondChange);
            OscillatordAntennaText.Text = gnss == null ? "—" :
                string.Format(CultureInfo.InvariantCulture, "status {0} · power {1}",
                    gnss.AntennaStatus, gnss.AntennaPower);
        }

        private static string HumanizeOscillatordState(string value)
        {
            if (string.IsNullOrEmpty(value))
                return "—";
            if (string.Equals(value, "LOCK_HIGH_RESOLUTION", StringComparison.OrdinalIgnoreCase))
                return "HIGH RES LOCK";
            if (string.Equals(value, "LOCK_LOW_RESOLUTION", StringComparison.OrdinalIgnoreCase))
                return "LOW RES LOCK";
            return value.Replace('_', ' ');
        }

        private static OscillatordSnapshot CreateDemoOscillatordSnapshot()
        {
            return new OscillatordSnapshot
            {
                Service = "oscillatord",
                Version = "3.10.0",
                ProtocolVersion = 1,
                ControlEnabled = false,
                Clock = new OscillatordClock
                {
                    Class = "LOCKED",
                    OffsetNanoseconds = 7
                },
                Disciplining = new OscillatordDisciplining
                {
                    Status = "LOCK_HIGH_RESOLUTION",
                    CurrentConvergenceCount = 50,
                    ConvergenceThreshold = 50,
                    ConvergenceProgress = 100,
                    ReadyForHoldover = true
                },
                Oscillator = new OscillatordOscillator
                {
                    Model = "mRO50",
                    FineControl = 2413,
                    CoarseControl = 32768,
                    Locked = true,
                    TemperatureCelsius = 42.18
                },
                Gnss = new OscillatordGnss
                {
                    Fix = 3,
                    FixOk = true,
                    AntennaPower = 1,
                    AntennaStatus = 2,
                    LeapSecondChange = 0,
                    LeapSeconds = 18,
                    Satellites = 17,
                    SurveyPositionErrorMeters = 0.42,
                    TimeAccuracyNanoseconds = 8
                }
            };
        }

        private static string FormatOscillatordNanoseconds(long value)
        {
            long magnitude = value == long.MinValue ? long.MaxValue : Math.Abs(value);
            if (magnitude >= 1000000000L)
                return (value / 1000000000.0).ToString("F3", CultureInfo.InvariantCulture) + " s";
            if (magnitude >= 1000000L)
                return (value / 1000000.0).ToString("F3", CultureInfo.InvariantCulture) + " ms";
            if (magnitude >= 1000L)
                return (value / 1000.0).ToString("F3", CultureInfo.InvariantCulture) + " µs";
            return value.ToString("N0", CultureInfo.InvariantCulture) + " ns";
        }
    }
}
