using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Xml.Serialization;

namespace TimeCardControlCenter
{
    public partial class MainWindow
    {
        private const uint ProfileEngineAbi = 12;
        private const uint ProfileNmeaAdvancedAbi = 13;
        private const uint ProfileCorePpsMaster = 1u << 0;
        private const uint ProfileCorePpsSlave = 1u << 1;
        private const uint ProfileCoreIrigMaster = 1u << 2;
        private const uint ProfileCoreIrigSlave = 1u << 3;
        private const uint ProfileCoreDcfMaster = 1u << 4;
        private const uint ProfileCoreDcfSlave = 1u << 5;
        private const uint ProfileCoreTodSlave = 1u << 6;
        private const uint ProfileCoreTodMaster = 1u << 7;
        private const uint ProfileCoreSignalGenerators = 1u << 8;

        private ControlCenterSettings productSettings;
        private TelemetrySession telemetrySession;
        private SensorTelemetrySnapshot lastSensorSnapshot;
        private HealthReport lastHealthReport;
        private readonly List<ConfigurationProfile> configurationProfiles =
            new List<ConfigurationProfile>();
        private readonly List<SelfTestResult> lastSelfTestResults =
            new List<SelfTestResult>();
        private bool productInitialized;
        private bool telemetryPaused;
        private double demoPhase;
        private bool uartDisplayPaused;
        private bool uartCaptureActive;
        private int uartCaptureStartIndex;
        private int uartCaptureEndIndex = -1;
        private readonly Queue<double> vibrationWindow = new Queue<double>();

        private void InitializeProductFeatures()
        {
            productSettings = SettingsStore.Load(SettingsStore.SettingsPath,
                new ControlCenterSettings());
            if (startupArguments.Any(argument => string.Equals(argument,
                "--demo", StringComparison.OrdinalIgnoreCase)))
                productSettings.DemoMode = true;
            string themeArgument = startupArguments.FirstOrDefault(argument =>
                argument.StartsWith("--theme=", StringComparison.OrdinalIgnoreCase));
            if (!string.IsNullOrWhiteSpace(themeArgument))
                productSettings.Theme = themeArgument.Substring(8).Replace('-', ' ');
            if (startupArguments.Any(argument => string.Equals(argument,
                "--compact", StringComparison.OrdinalIgnoreCase)))
                productSettings.CompactNavigation = true;
            telemetrySession = new TelemetrySession(productSettings.TelemetryCapacity);
            refreshTimer.Interval = TimeSpan.FromSeconds(Math.Max(.25,
                Math.Min(10, productSettings.RefreshSeconds)));
            LoadConfigurationProfiles();
            PopulateSettingsControls();
            TimeFlowTopology.WorkspaceRequested += Topology_WorkspaceRequested;
            UpdateCompatibilityMatrix(null);
            UpdateProfileSelection();
            ApplyTheme(productSettings.Theme);
            ApplyResponsiveLayout();
            productInitialized = true;
        }

        private void PopulateSettingsControls()
        {
            SelectProductComboText(ProductThemeCombo, productSettings.Theme);
            SelectProductComboText(ProductRefreshCombo,
                productSettings.RefreshSeconds.ToString("0.##", CultureInfo.InvariantCulture) + " s");
            ProductDemoCheckBox.IsChecked = productSettings.DemoMode;
            ProductCompactNavCheckBox.IsChecked = productSettings.CompactNavigation;
            RefreshProfileHistory();
        }

        private void LoadConfigurationProfiles()
        {
            configurationProfiles.Clear();
            configurationProfiles.AddRange(BuiltInProfiles.Create());
            ConfigurationProfileList saved = SettingsStore.Load(
                SettingsStore.ProfilesPath, new ConfigurationProfileList());
            if (saved.Profiles != null)
                configurationProfiles.AddRange(saved.Profiles.Where(item =>
                    item != null && !string.IsNullOrWhiteSpace(item.Name)));
            ProfileCombo.ItemsSource = null;
            ProfileCombo.ItemsSource = configurationProfiles;
            if (ProfileCombo.Items.Count > 0)
                ProfileCombo.SelectedIndex = 0;
        }

        private void SaveCustomProfiles()
        {
            ConfigurationProfileList list = new ConfigurationProfileList();
            list.Profiles.AddRange(configurationProfiles.Where(item => !item.IsBuiltIn));
            SettingsStore.Save(SettingsStore.ProfilesPath, list);
        }

        private static void SelectProductComboText(ComboBox combo, string text)
        {
            for (int index = 0; index < combo.Items.Count; index++)
            {
                ComboBoxItem item = combo.Items[index] as ComboBoxItem;
                if (item != null && string.Equals(Convert.ToString(item.Content,
                    CultureInfo.InvariantCulture), text, StringComparison.OrdinalIgnoreCase))
                {
                    combo.SelectedIndex = index;
                    return;
                }
            }
        }

        private void UpdateProductSnapshot(TimeCardSnapshot snapshot)
        {
            if (!productInitialized || snapshot == null)
                return;
            UpdateHealthExperience();
            UpdateCompatibilityMatrix(snapshot);
            if (telemetryPaused)
                return;

            double temperature = lastSensorSnapshot != null &&
                lastSensorSnapshot.Environment.IsValid ?
                lastSensorSnapshot.Environment.TemperatureCelsius : 0;
            double atomicPhase = 0;
            if (lastSa53Snapshot != null)
                lastSa53Snapshot.TryDouble("Phase", out atomicPhase);
            DateTime now = DateTime.UtcNow;
            double vibration = OverallVibration(lastSensorSnapshot);
            bool vibrationValid = vibration >= 0;
            if (vibrationValid)
                AddVibrationWindow(vibration);
            ProductOffsetChart.AddSample(now, snapshot.OffsetNanoseconds);
            ProductSamplingChart.AddSample(now, snapshot.SamplingWindowNanoseconds);
            CrossTimestampHistogram.AddSample(snapshot.SamplingWindowNanoseconds);
            ProductSatelliteChart.AddSample(now, snapshot.LockedSatellites);
            if (temperature != 0)
                ProductTemperatureChart.AddSample(now, temperature);
            if (vibrationValid)
                ProductVibrationChart.AddSample(now, vibration);
            ProductOffsetValueText.Text = FormatNanoseconds(snapshot.OffsetNanoseconds);
            ProductSamplingValueText.Text = FormatNanoseconds(snapshot.SamplingWindowNanoseconds);
            ProductHistogramSummaryText.Text = CrossTimestampHistogram.Summary("ns");
            ProductSatelliteValueText.Text = snapshot.LockedSatellites.ToString(
                CultureInfo.InvariantCulture) + " locked";
            ProductTemperatureValueText.Text = temperature == 0 ? "Not sampled" :
                temperature.ToString("F1", CultureInfo.InvariantCulture) + " °C";
            ProductVibrationValueText.Text = vibrationValid ? string.Format(
                CultureInfo.InvariantCulture, "{0:F3} m/s² · RMS {1:F3}",
                vibration, VibrationRms()) : "Not sampled";
            if (telemetrySession.IsRecording)
            {
                telemetrySession.Add(new TelemetryPoint
                {
                    TimestampUtc = now,
                    OffsetNanoseconds = snapshot.OffsetNanoseconds,
                    SamplingWindowNanoseconds = snapshot.SamplingWindowNanoseconds,
                    SatellitesSeen = snapshot.SeenSatellites,
                    SatellitesLocked = snapshot.LockedSatellites,
                    BoardTemperatureCelsius = temperature,
                    AtomicPhase = atomicPhase,
                    VibrationMetersPerSecondSquared = vibrationValid ? vibration : 0
                });
                TelemetrySampleCountText.Text = telemetrySession.Points.Count.ToString(
                    CultureInfo.InvariantCulture) + " samples recorded";
            }
        }

        private static double OverallVibration(SensorTelemetrySnapshot telemetry)
        {
            if (telemetry == null || telemetry.Imu == null ||
                !telemetry.Imu.IsValid || telemetry.Imu.LinearAcceleration == null)
                return -1;
            SensorVector3 value = telemetry.Imu.LinearAcceleration;
            return TelemetryMath.VectorMagnitude(value.X, value.Y, value.Z);
        }

        private void AddVibrationWindow(double vibration)
        {
            vibrationWindow.Enqueue(vibration);
            while (vibrationWindow.Count > 60)
                vibrationWindow.Dequeue();
        }

        private double VibrationRms()
        {
            return TelemetryMath.RootMeanSquare(vibrationWindow);
        }

        private void UpdateHealthExperience()
        {
            lastHealthReport = ControlCenterHealth.Evaluate(lastSnapshot,
                lastUbloxSnapshot, lastSa53Snapshot, lastMro50Status,
                lastSensorSnapshot,
                client != null, productSettings != null && productSettings.DemoMode);
            TimeFlowTopology.Update(lastHealthReport, lastSmaLedStates);
            int attention = lastHealthReport.AttentionCount;
            OverallHealthText.Text = attention == 0 ? "ALL SYSTEMS NOMINAL" :
                attention == 1 ? "1 ITEM NEEDS ATTENTION" :
                attention.ToString(CultureInfo.InvariantCulture) + " ITEMS NEED ATTENTION";
            OverallHealthText.Foreground = SeverityBrush(lastHealthReport.Overall);
            OverallHealthDetailText.Text = attention == 0 ?
                "The live timing path is healthy from source through Windows UTC." :
                "Select a highlighted component below for details and corrective controls.";
            HealthNeedsAttentionItems.Items.Clear();
            foreach (HealthNode node in lastHealthReport.Nodes.Where(item =>
                item.Severity == HealthSeverity.Attention ||
                item.Severity == HealthSeverity.Unavailable).Take(4))
                HealthNeedsAttentionItems.Items.Add(node.Name + " — " + node.Status +
                    "\n" + node.Detail);
            if (HealthNeedsAttentionItems.Items.Count == 0)
                HealthNeedsAttentionItems.Items.Add("No active warnings. Live health checks are passing.");
        }

        private Brush SeverityBrush(HealthSeverity severity)
        {
            return (Brush)FindResource(severity == HealthSeverity.Healthy ? "AccentBrush" :
                severity == HealthSeverity.Attention ? "GoldBrush" :
                severity == HealthSeverity.Unavailable ? "DangerBrush" : "CyanBrush");
        }

        private void Topology_WorkspaceRequested(object sender, WorkspaceRequestEventArgs e)
        {
            NavigateToWorkspace(e.Workspace);
        }

        private void NavigateToWorkspace(string workspace)
        {
            RadioButton navigation = workspace == "Clock" ? ClockNav :
                workspace == "Gnss" ? GnssNav :
                workspace == "Atomic" ? AtomicNav :
                workspace == "Oscillatord" ? OscillatordNav :
                workspace == "Uart" ? UartNav :
                workspace == "Sma" ? SmaNav :
                workspace == "Timing" ? TimingNav :
                workspace == "Fpga" ? FpgaNav :
                workspace == "Sensors" ? SensorsNav :
                workspace == "I2c" ? I2cNav :
                workspace == "Telemetry" ? TelemetryNav :
                workspace == "Operations" ? OperationsNav :
                workspace == "Diagnostics" ? DiagnosticsNav : SubsystemsNav;
            navigation.IsChecked = true;
        }

        private void ProfileCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            UpdateProfileSelection();
        }

        private void UpdateProfileSelection()
        {
            if (ProfileDescriptionText == null)
                return;
            ConfigurationProfile profile = ProfileCombo.SelectedItem as ConfigurationProfile;
            if (profile == null)
            {
                ProfileDescriptionText.Text =
                    "Choose a profile to preview its effect.";
            }
            else if (profile.HasFpgaImageIdentity)
            {
                ProfileDescriptionText.Text = string.Format(
                    CultureInfo.InvariantCulture,
                    "{0}\nCompatibility: board profile {1}, layout {2}, {3} image {4}.{5} (tag {6}, raw 0x{7:X8}).",
                    profile.Description,
                    profile.FpgaImageBoardProfile,
                    profile.FpgaImageLayout,
                    profile.FpgaImageLoaderEncoding ? "loader-encoded" : "application",
                    profile.FpgaImageVersion >> 8,
                    profile.FpgaImageVersion & 0xffu,
                    profile.FpgaImageTag,
                    profile.FpgaImageRawVersion);
            }
            else
            {
                ProfileDescriptionText.Text = profile.Description;
            }
            if (profile != null && profile.SchemaVersion != 0)
                ProfileDescriptionText.Text += string.Format(
                    CultureInfo.InvariantCulture,
                    "\nProfile schema {0}; captured with ABI {1}; requires ABI {2} " +
                    "and FPGA core mask 0x{3:X8}.",
                    profile.SchemaVersion, profile.CapturedAbiVersion,
                    GetProfileRequiredAbi(profile),
                    GetProfileRequiredCoreMask(profile));
            ProfileDiffText.Text = profile == null ? "No profile selected." :
                "Select Preview changes to compare this profile with the live card.";
        }

        private async void ProfilePreview_Click(object sender, RoutedEventArgs e)
        {
            ConfigurationProfile profile = ProfileCombo.SelectedItem as ConfigurationProfile;
            if (!EnsureProfileReady(profile))
                return;
            SetProfileBusy(true, "Reading current configuration…");
            try
            {
                ConfigurationProfile current = await Task.Run(() => CaptureConfigurationCore("Current card"));
                ProfileDiffText.Text = BuildProfileDiff(profile, current);
                SetProfileBusy(false, "Preview ready — no hardware changes made.");
            }
            catch (Exception ex)
            {
                SetProfileBusy(false, "Preview failed: " + ex.Message);
            }
        }

        private async void ProfileApply_Click(object sender, RoutedEventArgs e)
        {
            ConfigurationProfile profile = ProfileCombo.SelectedItem as ConfigurationProfile;
            if (!EnsureProfileReady(profile))
                return;
            ConfigurationProfile rollback = null;
            criticalConfigurationWrite = true;
            UpdateDeviceSelectionControls();
            try
            {
                SetProfileBusy(true, "Capturing rollback state…");
                rollback = await Task.Run(() => CaptureConfigurationCore("Rollback"));
                productSettings.LastKnownGoodProfile = rollback;
                SaveProductSettings();
                string preview = BuildProfileDiff(profile, rollback);
                ProfileDiffText.Text = preview;
                if (MessageBox.Show(this,
                    "Apply profile ‘" + profile.Name + "’?\n\n" + preview +
                    "\nThe current configuration will be restored automatically if verification fails.",
                    "Apply Time Card profile", MessageBoxButton.YesNo,
                    MessageBoxImage.Warning, MessageBoxResult.No) != MessageBoxResult.Yes)
                {
                    SetProfileBusy(false, "Apply cancelled. No hardware changes made.");
                    return;
                }
                SetProfileBusy(true, "Applying and verifying profile…");
                await Task.Run(() => ApplyConfigurationCore(profile, true));
                AddConfigurationHistory("APPLIED", profile.Name, "Read-back verification passed");
                SetProfileBusy(false, "Applied and verified: " + profile.Name);
                await RefreshSnapshotAsync(false);
                await RefreshSmaAsync();
            }
            catch (Exception ex)
            {
                try
                {
                    if (client != null && rollback != null)
                        await Task.Run(() => ApplyConfigurationCore(rollback, false));
                    AddConfigurationHistory("ROLLED BACK", profile.Name, ex.Message);
                    SetProfileBusy(false, "Verification failed; rollback completed: " + ex.Message);
                }
                catch (Exception rollbackError)
                {
                    AddConfigurationHistory("ROLLBACK FAILED", profile.Name,
                        ex.Message + " / " + rollbackError.Message);
                    SetProfileBusy(false, "Apply and rollback failed. Review Diagnostics immediately.");
                    MessageBox.Show(this, "Profile apply failed:\n" + ex.Message +
                        "\n\nRollback also failed:\n" + rollbackError.Message,
                        "Configuration requires attention", MessageBoxButton.OK,
                        MessageBoxImage.Error);
                }
            }
            finally
            {
                criticalConfigurationWrite = false;
                UpdateDeviceSelectionControls();
            }
        }

        private async void RestoreLastKnownGood_Click(object sender, RoutedEventArgs e)
        {
            ConfigurationProfile target = productSettings.LastKnownGoodProfile;
            if (target == null)
            {
                SetProfileBusy(false, "No last-known-good configuration has been captured yet.");
                return;
            }
            if (!EnsureProfileReady(target))
                return;
            ConfigurationProfile current = null;
            criticalConfigurationWrite = true;
            UpdateDeviceSelectionControls();
            try
            {
                current = await Task.Run(() => CaptureConfigurationCore("Pre-restore"));
                string preview = BuildProfileDiff(target, current);
                ProfileDiffText.Text = preview;
                if (MessageBox.Show(this, "Restore the last-known-good card configuration?\n\n" +
                    preview, "Restore configuration", MessageBoxButton.YesNo,
                    MessageBoxImage.Warning, MessageBoxResult.No) != MessageBoxResult.Yes)
                    return;
                SetProfileBusy(true, "Restoring and verifying last-known-good state…");
                await Task.Run(() => ApplyConfigurationCore(target, true));
                productSettings.LastKnownGoodProfile = current;
                SaveProductSettings();
                AddConfigurationHistory("RESTORED", target.Name, "Read-back verification passed");
                SetProfileBusy(false, "Last-known-good configuration restored and verified.");
                await RefreshSnapshotAsync(false);
                await RefreshSmaAsync();
            }
            catch (Exception ex)
            {
                if (current != null)
                {
                    try { await Task.Run(() => ApplyConfigurationCore(current, false)); }
                    catch { }
                }
                AddConfigurationHistory("RESTORE FAILED", target.Name, ex.Message);
                SetProfileBusy(false, "Restore failed; the pre-restore state was retained when possible: " + ex.Message);
            }
            finally
            {
                criticalConfigurationWrite = false;
                UpdateDeviceSelectionControls();
            }
        }

        private bool EnsureProfileReady(ConfigurationProfile profile)
        {
            if (profile == null)
            {
                SetProfileBusy(false, "Select a profile first.");
                return false;
            }
            if (productSettings.DemoMode)
            {
                SetProfileBusy(false, "Profile apply is disabled in demo mode.");
                return false;
            }
            if (client == null || lastSnapshot == null)
            {
                SetProfileBusy(false, "Connect to the Time Card before using profiles.");
                return false;
            }
            if (profile.SchemaVersion > ConfigurationProfile.CurrentSchemaVersion)
            {
                SetProfileBusy(false, string.Format(CultureInfo.InvariantCulture,
                    "Profile schema {0} is newer than the supported schema {1}.",
                    profile.SchemaVersion,
                    ConfigurationProfile.CurrentSchemaVersion));
                return false;
            }
            uint requiredAbi = GetProfileRequiredAbi(profile);
            if (lastSnapshot.AbiVersion < requiredAbi)
            {
                SetProfileBusy(false, string.Format(CultureInfo.InvariantCulture,
                    "This profile requires driver ABI {0}; the connected card exposes ABI {1}.",
                    requiredAbi, lastSnapshot.AbiVersion));
                return false;
            }
            uint requiredCores = GetProfileRequiredCoreMask(profile);
            if (requiredCores != 0)
            {
                try
                {
                    FpgaCapabilities capabilities = client.GetFpgaCapabilities();
                    uint missing = requiredCores & ~capabilities.CoreMask;
                    if (missing != 0)
                    {
                        SetProfileBusy(false, string.Format(
                            CultureInfo.InvariantCulture,
                            "The live FPGA image is missing required core mask 0x{0:X8} " +
                            "(required 0x{1:X8}, present 0x{2:X8}).",
                            missing, requiredCores, capabilities.CoreMask));
                        return false;
                    }
                }
                catch (Exception ex)
                {
                    Log("Profile FPGA capability validation failed: " + ex.Message);
                    SetProfileBusy(false,
                        "Unable to validate the profile's required FPGA cores: " +
                        ex.Message);
                    return false;
                }
            }
            if (profile.HasFpgaImageIdentity)
            {
                if (lastSnapshot.AbiVersion < FpgaImageRequiredAbi)
                {
                    SetProfileBusy(false,
                        "This profile contains image compatibility metadata. Install ABI 13 or newer before applying it.");
                    return false;
                }
                try
                {
                    FpgaImageInfo image = client.GetFpgaImageInfo();
                    if (!image.IsPresent ||
                        image.BoardProfile != profile.FpgaImageBoardProfile ||
                        image.Layout != profile.FpgaImageLayout ||
                        image.RawVersion != profile.FpgaImageRawVersion ||
                        image.ImageTag != profile.FpgaImageTag ||
                        image.ImageVersion != profile.FpgaImageVersion ||
                        image.IsLoader != profile.FpgaImageLoaderEncoding)
                    {
                        SetProfileBusy(false, string.Format(
                            CultureInfo.InvariantCulture,
                            "Profile FPGA image mismatch: expected board/layout {0}/{1}, " +
                            "{2} image {3}.{4}, tag {5}, raw 0x{6:X8}; live card is " +
                            "board/layout {7}/{8}, {9} image {10}.{11}, tag {12}, " +
                            "raw 0x{13:X8}. No configuration was written.",
                            profile.FpgaImageBoardProfile,
                            profile.FpgaImageLayout,
                            profile.FpgaImageLoaderEncoding ? "loader-encoded" :
                                "application",
                            profile.FpgaImageVersion >> 8,
                            profile.FpgaImageVersion & 0xffu,
                            profile.FpgaImageTag,
                            profile.FpgaImageRawVersion,
                            image.BoardProfile,
                            image.Layout,
                            image.IsLoader ? "loader-encoded" : "application",
                            image.ImageVersion >> 8,
                            image.ImageVersion & 0xffu,
                            image.ImageTag,
                            image.RawVersion));
                        return false;
                    }
                }
                catch (Exception ex)
                {
                    SetProfileBusy(false,
                        "Unable to validate the profile's FPGA image compatibility: " +
                        ex.Message);
                    return false;
                }
            }
            return true;
        }

        private static uint GetProfileRequiredAbi(ConfigurationProfile profile)
        {
            uint required = profile.RequiredAbiVersion;
            if ((profile.PpsEngines != null && profile.PpsEngines.Count != 0) ||
                (profile.TimecodeEngines != null &&
                 profile.TimecodeEngines.Count != 0) ||
                profile.TodParser != null ||
                (profile.SignalGenerators != null &&
                 profile.SignalGenerators.Count != 0))
                required = Math.Max(required, ProfileEngineAbi);
            if (profile.HasNmeaAdvanced)
                required = Math.Max(required, ProfileNmeaAdvancedAbi);
            return required;
        }

        private static uint GetProfileRequiredCoreMask(
            ConfigurationProfile profile)
        {
            uint mask = profile.RequiredFpgaCoreMask;
            if (profile.SchemaVersion == 0)
                return mask;
            if (profile.HasNmea)
                mask |= ProfileCoreTodMaster;
            if (profile.PpsEngines != null)
                foreach (PpsProfileSetting setting in profile.PpsEngines)
                    mask |= setting.Core == 1 ? ProfileCorePpsMaster :
                        setting.Core == 2 ? ProfileCorePpsSlave : 0u;
            if (profile.TimecodeEngines != null)
                foreach (TimecodeProfileSetting setting in
                         profile.TimecodeEngines)
                    mask |= TimecodeCoreMask(setting.Format, setting.Role);
            if (profile.TodParser != null)
                mask |= ProfileCoreTodSlave;
            if (profile.SignalGenerators != null &&
                profile.SignalGenerators.Count != 0)
                mask |= ProfileCoreSignalGenerators;
            return mask;
        }

        private static uint TimecodeCoreMask(uint format, uint role)
        {
            if (format == 1 && role == 1) return ProfileCoreIrigMaster;
            if (format == 1 && role == 2) return ProfileCoreIrigSlave;
            if (format == 2 && role == 1) return ProfileCoreDcfMaster;
            if (format == 2 && role == 2) return ProfileCoreDcfSlave;
            return 0;
        }

        private ConfigurationProfile CaptureConfigurationCore(string name)
        {
            TimeCardSnapshot snapshot = client.GetSnapshot();
            FpgaCapabilities capabilities = null;
            ConfigurationProfile profile = new ConfigurationProfile
            {
                SchemaVersion = ConfigurationProfile.CurrentSchemaVersion,
                CapturedAbiVersion = snapshot.AbiVersion,
                Name = name,
                Description = "Configuration captured at " + DateTime.Now.ToString("G", CultureInfo.CurrentCulture),
                HasClockSource = snapshot.AbiVersion >= 4,
                ClockSource = snapshot.ClockSource
            };
            if (snapshot.AbiVersion >= ProfileEngineAbi)
                capabilities = client.GetFpgaCapabilities();
            if (snapshot.AbiVersion >= FpgaImageRequiredAbi &&
                !string.Equals(snapshot.Layout, "Orolia ART",
                    StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    FpgaImageInfo image = client.GetFpgaImageInfo();
                    profile.HasFpgaImageIdentity = image.IsPresent;
                    profile.FpgaImageRawVersion = image.RawVersion;
                    profile.FpgaImageTag = image.ImageTag;
                    profile.FpgaImageVersion = image.ImageVersion;
                    profile.FpgaImageLayout = image.Layout;
                    profile.FpgaImageBoardProfile = image.BoardProfile;
                    profile.FpgaImageLoaderEncoding = image.IsLoader;
                }
                catch
                {
                    /* Image metadata is additive; configuration capture remains usable. */
                }
            }
            /*
             * Do not query the optional ToD Master while capturing a profile.
             * The generic Meta/Celestica resource map reserves its address but
             * does not prove that every deployed bitstream decodes the slave.
             * A future, signed image-specific synthesis manifest may opt this
             * block into capture. Explicit NMEA query/set operations remain
             * available in the NMEA workspace and for declared profiles.
             */
            if (snapshot.AbiVersion >= 4)
            {
                for (uint connector = 1; connector <= 4; connector++)
                {
                    SmaConnectorState state = client.GetSmaConnector(connector);
                    if (state.IsPresent)
                        profile.Sma.Add(new SmaProfileSetting
                        {
                            Connector = connector,
                            Direction = state.Direction,
                            Function = state.Function
                        });
                }
            }
            if (capabilities != null)
            {
                for (uint core = 1; core <= 2; core++)
                {
                    uint coreMask = core == 1 ? ProfileCorePpsMaster :
                        ProfileCorePpsSlave;
                    if ((capabilities.CoreMask & coreMask) == 0)
                        continue;
                    PpsEngineState state = client.GetPpsEngine(core);
                    if (state.IsPresent)
                        profile.PpsEngines.Add(new PpsProfileSetting
                        {
                            Core = core,
                            CoreVersion = state.Version,
                            Enabled = state.IsEnabled,
                            ActiveHigh = state.IsActiveHigh,
                            HasPulseWidth = state.IsPulseWidthWritable,
                            PulseWidthMilliseconds =
                                state.PulseWidthMilliseconds,
                            CableDelayNanoseconds =
                                state.CableDelayNanoseconds
                        });
                }
                for (uint format = 1; format <= 2; format++)
                    for (uint role = 1; role <= 2; role++)
                    {
                        uint coreMask = TimecodeCoreMask(format, role);
                        if ((capabilities.CoreMask & coreMask) == 0)
                            continue;
                        TimecodeEngineState state =
                            client.GetTimecodeEngine(format, role);
                        if (state.IsPresent)
                            profile.TimecodeEngines.Add(
                                new TimecodeProfileSetting
                                {
                                    Format = format,
                                    Role = role,
                                    CoreVersion = state.Version,
                                    Enabled = state.IsEnabled,
                                    Mode = state.Mode,
                                    Code = state.Code,
                                    CorrectionSeconds =
                                        state.CorrectionSeconds,
                                    HasDelay = state.IsDelayWritable,
                                    DelayNanoseconds = state.DelayNanoseconds,
                                    HasControlBits =
                                        state.IsControlBitsWritable,
                                    ControlBits = state.ControlBits
                                });
                    }
                if ((capabilities.CoreMask & ProfileCoreTodSlave) != 0)
                {
                    TodParserState state = client.GetTodParser();
                    if (state.IsPresent)
                        profile.TodParser = new TodParserProfileSetting
                        {
                            CoreVersion = state.Version,
                            Enabled = state.IsEnabled,
                            Protocol = state.Protocol,
                            Gnss = state.Gnss,
                            Baud = state.Baud,
                            Inverted = state.IsInverted,
                            CorrectionSeconds = state.CorrectionSeconds,
                            MessageDisableMask = state.MessageDisableMask
                        };
                }
                if ((capabilities.CoreMask &
                     ProfileCoreSignalGenerators) != 0)
                    for (uint generator = 1; generator <= 4; generator++)
                    {
                        SignalGeneratorState state =
                            client.GetSignalGenerator(generator);
                        if (state.IsPresent)
                            profile.SignalGenerators.Add(
                                new SignalGeneratorProfileSetting
                                {
                                    Generator = generator,
                                    CoreVersion = state.Version,
                                    Enabled = state.IsEnabled,
                                    ActiveHigh = state.IsActiveHigh,
                                    PeriodNanoseconds =
                                        state.PeriodNanoseconds,
                                    PulseNanoseconds = state.PulseNanoseconds,
                                    PhaseNanoseconds = state.PhaseNanoseconds,
                                    RepeatCount = state.RepeatCount,
                                    CableDelayNanoseconds =
                                        state.CableDelayNanoseconds
                                });
                    }
            }
            if (profile.HasClockSource || profile.HasNmea ||
                profile.Sma.Count != 0)
                profile.RequiredAbiVersion = 4;
            if (profile.HasFpgaImageIdentity)
                profile.RequiredAbiVersion = Math.Max(
                    profile.RequiredAbiVersion, FpgaImageRequiredAbi);
            profile.RequiredAbiVersion = GetProfileRequiredAbi(profile);
            profile.RequiredFpgaCoreMask =
                GetProfileRequiredCoreMask(profile);
            return profile;
        }

        private void ApplyConfigurationCore(ConfigurationProfile profile, bool verify)
        {
            if (profile.HasClockSource)
            {
                uint active = client.SetClockSource(profile.ClockSource);
                if (verify && active != profile.ClockSource)
                    throw new InvalidOperationException("Clock source read-back did not match the requested selector.");
            }
            if (profile.HasNmea)
            {
                NmeaOutputState current = client.GetNmeaOutput();
                RequireCoreRevision("NMEA master", current.Version,
                    profile.NmeaCoreVersion);
                NmeaOutputState state = profile.HasNmeaAdvanced ?
                    client.SetNmeaOutput(profile.NmeaEnabled,
                        profile.NmeaBaud, profile.NmeaInverted,
                        profile.NmeaCorrectionSeconds,
                        profile.NmeaLocalOffsetMinutes, profile.NmeaGnss,
                        profile.NmeaMessageDisableMask) :
                    client.SetNmeaOutput(profile.NmeaEnabled,
                        profile.NmeaBaud, profile.NmeaInverted);
                if (verify && (state.IsEnabled != profile.NmeaEnabled ||
                    state.IsInverted != profile.NmeaInverted ||
                    (profile.NmeaEnabled && state.Baud != profile.NmeaBaud) ||
                    (profile.HasNmeaAdvanced &&
                     (state.CorrectionSeconds != profile.NmeaCorrectionSeconds ||
                      state.LocalOffsetMinutes != profile.NmeaLocalOffsetMinutes ||
                      state.Gnss != profile.NmeaGnss ||
                      state.MessageDisableMask !=
                          profile.NmeaMessageDisableMask))))
                    throw new InvalidOperationException("NMEA generator read-back verification failed.");
            }
            if (profile.PpsEngines != null)
                foreach (PpsProfileSetting setting in profile.PpsEngines)
                {
                    PpsEngineState current = client.GetPpsEngine(setting.Core);
                    if (!current.IsPresent)
                        throw new InvalidOperationException(
                            "PPS " + PpsRoleName(setting.Core) +
                            " is not present.");
                    RequireCoreRevision("PPS " + PpsRoleName(setting.Core),
                        current.Version, setting.CoreVersion);
                    uint pulseWidth = setting.HasPulseWidth ?
                        setting.PulseWidthMilliseconds :
                        current.PulseWidthMilliseconds;
                    PpsEngineState state = client.SetPpsEngine(setting.Core,
                        setting.Enabled, setting.ActiveHigh, pulseWidth,
                        setting.CableDelayNanoseconds, false);
                    if (verify &&
                        (state.IsEnabled != setting.Enabled ||
                         state.IsActiveHigh != setting.ActiveHigh ||
                         state.CableDelayNanoseconds !=
                            setting.CableDelayNanoseconds ||
                         (setting.HasPulseWidth &&
                          state.PulseWidthMilliseconds !=
                            setting.PulseWidthMilliseconds)))
                        throw new InvalidOperationException(
                            "PPS " + PpsRoleName(setting.Core) +
                            " read-back verification failed.");
                }
            if (profile.TimecodeEngines != null)
                foreach (TimecodeProfileSetting setting in
                         profile.TimecodeEngines)
                {
                    TimecodeEngineState current = client.GetTimecodeEngine(
                        setting.Format, setting.Role);
                    if (!current.IsPresent)
                        throw new InvalidOperationException(
                            TimecodeName(setting.Format, setting.Role) +
                            " is not present.");
                    RequireCoreRevision(
                        TimecodeName(setting.Format, setting.Role),
                        current.Version, setting.CoreVersion);
                    int delay = setting.HasDelay ? setting.DelayNanoseconds :
                        current.DelayNanoseconds;
                    uint controlBits = setting.HasControlBits ?
                        setting.ControlBits : current.ControlBits;
                    TimecodeEngineState state = client.SetTimecodeEngine(
                        setting.Format, setting.Role, setting.Enabled,
                        setting.Mode, setting.Code,
                        setting.CorrectionSeconds, delay, controlBits, false);
                    if (verify &&
                        (state.IsEnabled != setting.Enabled ||
                         state.Mode != setting.Mode ||
                         state.Code != setting.Code ||
                         state.CorrectionSeconds !=
                            setting.CorrectionSeconds ||
                         (setting.HasDelay &&
                          state.DelayNanoseconds != setting.DelayNanoseconds) ||
                         (setting.HasControlBits &&
                          state.ControlBits != setting.ControlBits)))
                        throw new InvalidOperationException(
                            TimecodeName(setting.Format, setting.Role) +
                            " read-back verification failed.");
                }
            if (profile.TodParser != null)
            {
                TodParserProfileSetting setting = profile.TodParser;
                TodParserState current = client.GetTodParser();
                if (!current.IsPresent)
                    throw new InvalidOperationException(
                        "ToD parser is not present.");
                RequireCoreRevision("ToD parser", current.Version,
                    setting.CoreVersion);
                TodParserState state = client.SetTodParser(setting.Enabled,
                    setting.Protocol, setting.Gnss, setting.Baud,
                    setting.Inverted, setting.CorrectionSeconds,
                    setting.MessageDisableMask, false);
                if (verify &&
                    (state.IsEnabled != setting.Enabled ||
                     state.Protocol != setting.Protocol ||
                     state.Gnss != setting.Gnss ||
                     state.Baud != setting.Baud ||
                     state.IsInverted != setting.Inverted ||
                     state.CorrectionSeconds != setting.CorrectionSeconds ||
                     state.MessageDisableMask !=
                        setting.MessageDisableMask))
                    throw new InvalidOperationException(
                        "ToD parser read-back verification failed.");
            }
            if (profile.SignalGenerators != null)
                foreach (SignalGeneratorProfileSetting setting in
                         profile.SignalGenerators)
                {
                    SignalGeneratorState current =
                        client.GetSignalGenerator(setting.Generator);
                    if (!current.IsPresent)
                        throw new InvalidOperationException(string.Format(
                            CultureInfo.InvariantCulture,
                            "Signal generator {0} is not present.",
                            setting.Generator));
                    RequireCoreRevision("Signal generator " +
                        setting.Generator.ToString(CultureInfo.InvariantCulture),
                        current.Version, setting.CoreVersion);
                    SignalGeneratorState state = client.SetSignalGenerator(
                        setting.Generator, setting.Enabled,
                        setting.PeriodNanoseconds, setting.PulseNanoseconds,
                        setting.PhaseNanoseconds, setting.ActiveHigh,
                        setting.RepeatCount, setting.CableDelayNanoseconds);
                    if (verify &&
                        (state.IsEnabled != setting.Enabled ||
                         state.IsActiveHigh != setting.ActiveHigh ||
                         state.PeriodNanoseconds != setting.PeriodNanoseconds ||
                         state.PulseNanoseconds != setting.PulseNanoseconds ||
                         state.PhaseNanoseconds != setting.PhaseNanoseconds ||
                         state.RepeatCount != setting.RepeatCount ||
                         state.CableDelayNanoseconds !=
                            setting.CableDelayNanoseconds))
                        throw new InvalidOperationException(string.Format(
                            CultureInfo.InvariantCulture,
                            "Signal generator {0} read-back verification failed.",
                            setting.Generator));
                }
            foreach (SmaProfileSetting setting in profile.Sma)
            {
                SmaConnectorState state = client.SetSmaConnector(setting.Connector,
                    setting.Direction, setting.Function);
                if (verify && (state.Direction != setting.Direction ||
                    state.Function != setting.Function))
                    throw new InvalidOperationException("SMA " + setting.Connector +
                        " read-back verification failed.");
            }
        }

        private static string BuildProfileDiff(ConfigurationProfile target,
                                                ConfigurationProfile current)
        {
            List<string> changes = new List<string>();
            if (target.HasClockSource && (!current.HasClockSource ||
                target.ClockSource != current.ClockSource))
                changes.Add(string.Format(CultureInfo.InvariantCulture,
                    "• Clock source: 0x{0:X2} → 0x{1:X2}",
                    current.ClockSource, target.ClockSource));
            if (target.HasNmea && (!current.HasNmea ||
                target.NmeaEnabled != current.NmeaEnabled ||
                target.NmeaBaud != current.NmeaBaud ||
                target.NmeaInverted != current.NmeaInverted ||
                (target.HasNmeaAdvanced &&
                 (!current.HasNmeaAdvanced ||
                  target.NmeaCorrectionSeconds != current.NmeaCorrectionSeconds ||
                  target.NmeaLocalOffsetMinutes != current.NmeaLocalOffsetMinutes ||
                  target.NmeaGnss != current.NmeaGnss ||
                  target.NmeaMessageDisableMask !=
                      current.NmeaMessageDisableMask))))
                changes.Add(string.Format(CultureInfo.InvariantCulture,
                    "• NMEA: {0} {1} baud {2}{3}",
                    target.NmeaEnabled ? "enabled" : "disabled",
                    target.NmeaBaud,
                    target.NmeaInverted ? "inverted" : "normal polarity",
                    target.HasNmeaAdvanced ? string.Format(
                        CultureInfo.InvariantCulture,
                        ", correction {0} s, local {1} min, GNSS {2}, mask 0x{3:X2}",
                        target.NmeaCorrectionSeconds,
                        target.NmeaLocalOffsetMinutes, target.NmeaGnss,
                        target.NmeaMessageDisableMask) : string.Empty));
            if (target.PpsEngines != null)
                foreach (PpsProfileSetting setting in target.PpsEngines)
                {
                    PpsProfileSetting old = current.PpsEngines == null ? null :
                        current.PpsEngines.FirstOrDefault(item =>
                            item.Core == setting.Core);
                    if (old == null || old.Enabled != setting.Enabled ||
                        old.ActiveHigh != setting.ActiveHigh ||
                        old.CableDelayNanoseconds !=
                            setting.CableDelayNanoseconds ||
                        (setting.HasPulseWidth &&
                         (!old.HasPulseWidth || old.PulseWidthMilliseconds !=
                            setting.PulseWidthMilliseconds)))
                        changes.Add(string.Format(CultureInfo.InvariantCulture,
                            "• PPS {0}: {1}, {2}, cable {3} ns{4}",
                            PpsRoleName(setting.Core),
                            setting.Enabled ? "enabled" : "disabled",
                            setting.ActiveHigh ? "active-high" : "active-low",
                            setting.CableDelayNanoseconds,
                            setting.HasPulseWidth ? ", pulse " +
                                setting.PulseWidthMilliseconds.ToString(
                                    CultureInfo.InvariantCulture) + " ms" :
                                string.Empty));
                }
            if (target.TimecodeEngines != null)
                foreach (TimecodeProfileSetting setting in
                         target.TimecodeEngines)
                {
                    TimecodeProfileSetting old =
                        current.TimecodeEngines == null ? null :
                        current.TimecodeEngines.FirstOrDefault(item =>
                            item.Format == setting.Format &&
                            item.Role == setting.Role);
                    if (old == null || old.Enabled != setting.Enabled ||
                        old.Mode != setting.Mode || old.Code != setting.Code ||
                        old.CorrectionSeconds != setting.CorrectionSeconds ||
                        (setting.HasDelay && (!old.HasDelay ||
                         old.DelayNanoseconds != setting.DelayNanoseconds)) ||
                        (setting.HasControlBits && (!old.HasControlBits ||
                         old.ControlBits != setting.ControlBits)))
                        changes.Add(string.Format(CultureInfo.InvariantCulture,
                            "• {0}: {1}, mode {2}, code {3}, correction {4} s{5}{6}",
                            TimecodeName(setting.Format, setting.Role),
                            setting.Enabled ? "enabled" : "disabled",
                            setting.Mode, setting.Code,
                            setting.CorrectionSeconds,
                            setting.HasDelay ? ", delay " +
                                setting.DelayNanoseconds.ToString(
                                    CultureInfo.InvariantCulture) + " ns" :
                                string.Empty,
                            setting.HasControlBits ? ", control 0x" +
                                setting.ControlBits.ToString("X8",
                                    CultureInfo.InvariantCulture) :
                                string.Empty));
                }
            if (target.TodParser != null)
            {
                TodParserProfileSetting setting = target.TodParser;
                TodParserProfileSetting old = current.TodParser;
                if (old == null || old.Enabled != setting.Enabled ||
                    old.Protocol != setting.Protocol ||
                    old.Gnss != setting.Gnss || old.Baud != setting.Baud ||
                    old.Inverted != setting.Inverted ||
                    old.CorrectionSeconds != setting.CorrectionSeconds ||
                    old.MessageDisableMask != setting.MessageDisableMask)
                    changes.Add(string.Format(CultureInfo.InvariantCulture,
                        "• ToD parser: {0}, {1}, GNSS {2}, {3} baud, {4}, " +
                        "correction {5} s, mask 0x{6:X2}",
                        setting.Enabled ? "enabled" : "disabled",
                        TodProtocolName(setting.Protocol), setting.Gnss,
                        setting.Baud,
                        setting.Inverted ? "inverted" : "normal",
                        setting.CorrectionSeconds,
                        setting.MessageDisableMask));
            }
            if (target.SignalGenerators != null)
                foreach (SignalGeneratorProfileSetting setting in
                         target.SignalGenerators)
                {
                    SignalGeneratorProfileSetting old =
                        current.SignalGenerators == null ? null :
                        current.SignalGenerators.FirstOrDefault(item =>
                            item.Generator == setting.Generator);
                    if (old == null || old.Enabled != setting.Enabled ||
                        old.ActiveHigh != setting.ActiveHigh ||
                        old.PeriodNanoseconds != setting.PeriodNanoseconds ||
                        old.PulseNanoseconds != setting.PulseNanoseconds ||
                        old.PhaseNanoseconds != setting.PhaseNanoseconds ||
                        old.RepeatCount != setting.RepeatCount ||
                        old.CableDelayNanoseconds !=
                            setting.CableDelayNanoseconds)
                        changes.Add(string.Format(CultureInfo.InvariantCulture,
                            "• Signal generator {0}: {1}, period {2} ns, " +
                            "pulse {3} ns, phase {4} ns, {5}, repeat {6}, cable {7} ns",
                            setting.Generator,
                            setting.Enabled ? "enabled" : "disabled",
                            setting.PeriodNanoseconds,
                            setting.PulseNanoseconds,
                            setting.PhaseNanoseconds,
                            setting.ActiveHigh ? "active high" : "active low",
                            setting.RepeatCount,
                            setting.CableDelayNanoseconds));
                }
            foreach (SmaProfileSetting setting in target.Sma)
            {
                SmaProfileSetting old = current.Sma.FirstOrDefault(item =>
                    item.Connector == setting.Connector);
                if (old == null || old.Direction != setting.Direction ||
                    old.Function != setting.Function)
                    changes.Add(string.Format(CultureInfo.InvariantCulture,
                        "• SMA {0}: {1}, function 0x{2:X4}", setting.Connector,
                        setting.Direction, setting.Function));
            }
            return changes.Count == 0 ? "No changes. The live card already matches this profile." :
                string.Join(Environment.NewLine, changes);
        }

        private static string PpsRoleName(uint core)
        {
            return core == 1 ? "master" : core == 2 ? "slave" :
                "core " + core.ToString(CultureInfo.InvariantCulture);
        }

        private static string TimecodeName(uint format, uint role)
        {
            string formatName = format == 1 ? "IRIG" :
                format == 2 ? "DCF77" : "timecode " +
                format.ToString(CultureInfo.InvariantCulture);
            return formatName + " " + (role == 1 ? "master" :
                role == 2 ? "slave" : "role " +
                role.ToString(CultureInfo.InvariantCulture));
        }

        private static string TodProtocolName(uint protocol)
        {
            return protocol == 0 ? "NMEA" : protocol == 1 ? "UBX" :
                protocol == 2 ? "TSIP" : protocol == 3 ? "ESIP" :
                protocol == 4 ? "PFEC" :
                "protocol " + protocol.ToString(CultureInfo.InvariantCulture);
        }

        private static void RequireCoreRevision(string name, uint actual,
                                                uint required)
        {
            if (required == 0)
                return;
            uint actualRevision = actual & 0xffff0000u;
            uint requiredRevision = required & 0xffff0000u;
            if (actualRevision < requiredRevision)
                throw new InvalidOperationException(string.Format(
                    CultureInfo.InvariantCulture,
                    "{0} core {1}.{2} is older than profile requirement {3}.{4}.",
                    name, actual >> 24, (actual >> 16) & 0xffu,
                    required >> 24, (required >> 16) & 0xffu));
        }

        private async void ProfileCapture_Click(object sender, RoutedEventArgs e)
        {
            if (!EnsureProfileReady(new ConfigurationProfile { Name = "capture" }))
                return;
            string name = ProfileNameTextBox.Text.Trim();
            if (string.IsNullOrWhiteSpace(name))
            {
                SetProfileBusy(false, "Enter a name for the captured profile.");
                return;
            }
            try
            {
                SetProfileBusy(true, "Capturing live card configuration…");
                ConfigurationProfile captured = await Task.Run(() => CaptureConfigurationCore(name));
                captured.Description = "User profile captured from this Time Card on " +
                    DateTime.Now.ToString("G", CultureInfo.CurrentCulture) + ".";
                configurationProfiles.RemoveAll(item => !item.IsBuiltIn &&
                    string.Equals(item.Name, name, StringComparison.OrdinalIgnoreCase));
                configurationProfiles.Add(captured);
                SaveCustomProfiles();
                ProfileCombo.ItemsSource = null;
                ProfileCombo.ItemsSource = configurationProfiles;
                ProfileCombo.SelectedItem = captured;
                AddConfigurationHistory("CAPTURED", name, "Saved locally");
                SetProfileBusy(false, "Captured profile: " + name);
            }
            catch (Exception ex)
            {
                SetProfileBusy(false, "Capture failed: " + ex.Message);
            }
        }

        private void ProfileExport_Click(object sender, RoutedEventArgs e)
        {
            ConfigurationProfile profile = ProfileCombo.SelectedItem as ConfigurationProfile;
            if (profile == null)
                return;
            SaveFileDialog dialog = new SaveFileDialog
            {
                Title = "Export Time Card profile",
                Filter = "Time Card profile (*.xml)|*.xml",
                FileName = SafeFileName(profile.Name) + ".xml"
            };
            if (dialog.ShowDialog(this) != true)
                return;
            using (FileStream stream = File.Create(dialog.FileName))
                new XmlSerializer(typeof(ConfigurationProfile)).Serialize(stream, profile);
            RememberExportDirectory(dialog.FileName);
            SetProfileBusy(false, "Exported " + Path.GetFileName(dialog.FileName));
        }

        private void ProfileImport_Click(object sender, RoutedEventArgs e)
        {
            OpenFileDialog dialog = new OpenFileDialog
            {
                Title = "Import Time Card profile",
                Filter = "Time Card profile (*.xml)|*.xml"
            };
            if (dialog.ShowDialog(this) != true)
                return;
            try
            {
                ConfigurationProfile profile;
                using (FileStream stream = File.OpenRead(dialog.FileName))
                    profile = (ConfigurationProfile)new XmlSerializer(
                        typeof(ConfigurationProfile)).Deserialize(stream);
                profile.IsBuiltIn = false;
                if (string.IsNullOrWhiteSpace(profile.Name))
                    throw new InvalidDataException("The profile has no name.");
                configurationProfiles.RemoveAll(item => !item.IsBuiltIn &&
                    string.Equals(item.Name, profile.Name, StringComparison.OrdinalIgnoreCase));
                configurationProfiles.Add(profile);
                SaveCustomProfiles();
                ProfileCombo.ItemsSource = null;
                ProfileCombo.ItemsSource = configurationProfiles;
                ProfileCombo.SelectedItem = profile;
                SetProfileBusy(false, "Imported " + profile.Name);
            }
            catch (Exception ex)
            {
                SetProfileBusy(false, "Import failed: " + ex.Message);
            }
        }

        private void SetProfileBusy(bool busy, string status)
        {
            if (ProfileApplyButton != null)
                ProfileApplyButton.IsEnabled = !busy;
            if (ProfilePreviewButton != null)
                ProfilePreviewButton.IsEnabled = !busy;
            if (ProfileApplyStatusText != null)
                ProfileApplyStatusText.Text = status;
        }

        private void AddConfigurationHistory(string action, string profile, string detail)
        {
            string entry = string.Format(CultureInfo.InvariantCulture,
                "{0:yyyy-MM-dd HH:mm:ss}  {1}  {2} — {3}", DateTime.Now,
                action, profile, detail);
            productSettings.ConfigurationHistory.Insert(0, entry);
            while (productSettings.ConfigurationHistory.Count > 100)
                productSettings.ConfigurationHistory.RemoveAt(
                    productSettings.ConfigurationHistory.Count - 1);
            SaveProductSettings();
            RefreshProfileHistory();
            Log("Configuration profile " + action.ToLowerInvariant() + ": " + profile + ".");
        }

        private void RefreshProfileHistory()
        {
            if (ProfileHistoryList == null || productSettings == null)
                return;
            ProfileHistoryList.Items.Clear();
            foreach (string entry in productSettings.ConfigurationHistory.Take(30))
                ProfileHistoryList.Items.Add(entry);
            if (ProfileHistoryList.Items.Count == 0)
                ProfileHistoryList.Items.Add("No configuration changes recorded yet.");
        }

        private void TelemetryRecord_Click(object sender, RoutedEventArgs e)
        {
            telemetrySession.IsRecording = !telemetrySession.IsRecording;
            TelemetryRecordButton.Content = telemetrySession.IsRecording ?
                "Stop recording" : "Start recording";
            TelemetryRecordingDot.Fill = (Brush)FindResource(
                telemetrySession.IsRecording ? "DangerBrush" : "MutedBrush");
            TelemetryRecordingStatusText.Text = telemetrySession.IsRecording ?
                "RECORDING LIVE SESSION" : "LIVE VIEW · NOT RECORDING";
            if (telemetrySession.IsRecording)
                Log("Telemetry session recording started.");
            else
                Log("Telemetry session recording stopped.");
        }

        private void TelemetryPause_Click(object sender, RoutedEventArgs e)
        {
            telemetryPaused = !telemetryPaused;
            ProductOffsetChart.IsPaused = telemetryPaused;
            ProductSamplingChart.IsPaused = telemetryPaused;
            ProductSatelliteChart.IsPaused = telemetryPaused;
            ProductTemperatureChart.IsPaused = telemetryPaused;
            ProductVibrationChart.IsPaused = telemetryPaused;
            TelemetryPauseButton.Content = telemetryPaused ? "Resume display" : "Pause display";
            TelemetryViewStatusText.Text = telemetryPaused ?
                "Display paused; hardware sampling continues." :
                "Scroll a chart to zoom. Hover for exact timestamp and value.";
        }

        private void TelemetryClear_Click(object sender, RoutedEventArgs e)
        {
            telemetrySession.Clear();
            ProductOffsetChart.Clear();
            ProductSamplingChart.Clear();
            CrossTimestampHistogram.Clear();
            ProductSatelliteChart.Clear();
            ProductTemperatureChart.Clear();
            ProductVibrationChart.Clear();
            vibrationWindow.Clear();
            ProductHistogramSummaryText.Text = "Waiting for samples";
            ProductVibrationValueText.Text = "Not sampled";
            TelemetrySampleCountText.Text = "0 samples recorded";
        }

        private void TelemetryExport_Click(object sender, RoutedEventArgs e)
        {
            if (telemetrySession.Points.Count == 0)
            {
                TelemetryViewStatusText.Text = "Start recording and collect at least one sample before exporting.";
                return;
            }
            SaveFileDialog dialog = new SaveFileDialog
            {
                Title = "Export telemetry session",
                Filter = "CSV telemetry (*.csv)|*.csv|JSON telemetry (*.json)|*.json",
                FileName = "timecard-telemetry-" + DateTime.Now.ToString("yyyyMMdd-HHmmss",
                    CultureInfo.InvariantCulture) + ".csv",
                InitialDirectory = ExistingDirectory(productSettings.LastExportDirectory)
            };
            if (dialog.ShowDialog(this) != true)
                return;
            File.WriteAllText(dialog.FileName,
                string.Equals(Path.GetExtension(dialog.FileName), ".json",
                    StringComparison.OrdinalIgnoreCase) ? telemetrySession.ToJson() :
                    telemetrySession.ToCsv(), new UTF8Encoding(false));
            RememberExportDirectory(dialog.FileName);
            TelemetryViewStatusText.Text = "Exported " + Path.GetFileName(dialog.FileName);
        }

        private async void RunSelfTest_Click(object sender, RoutedEventArgs e)
        {
            if (productSettings.DemoMode)
            {
                PopulateDemoSelfTest();
                return;
            }
            if (client == null)
            {
                SelfTestSummaryText.Text = "Connect to the Time Card before running self-test.";
                return;
            }
            SelfTestRunButton.IsEnabled = false;
            SelfTestResultsList.Items.Clear();
            SelfTestSummaryText.Text = "Running read-only hardware checks…";
            refreshTimer.Stop();
            try
            {
                IList<SelfTestResult> results = await Task.Run(() => RunSelfTestsCore());
                lastSelfTestResults.Clear();
                lastSelfTestResults.AddRange(results);
                foreach (SelfTestResult result in results)
                    SelfTestResultsList.Items.Add(result);
                int failed = results.Count(item => item.Outcome == SelfTestOutcome.Fail);
                int warnings = results.Count(item => item.Outcome == SelfTestOutcome.Warning);
                SelfTestSummaryText.Text = failed == 0 && warnings == 0 ?
                    "All checks passed." : string.Format(CultureInfo.InvariantCulture,
                    "Completed with {0} failure(s) and {1} warning(s).", failed, warnings);
                Log("Guided self-test completed: " + SelfTestSummaryText.Text);
            }
            finally
            {
                SelfTestRunButton.IsEnabled = true;
                refreshTimer.Start();
            }
        }

        private IList<SelfTestResult> RunSelfTestsCore()
        {
            List<SelfTestResult> results = new List<SelfTestResult>();
            TimeCardSnapshot first = null;
            RunCheck(results, "Driver connection", delegate
            {
                first = client.GetSnapshot();
                return Pass("Driver " + first.DriverVersion + " / ABI " + first.AbiVersion);
            });
            if (first == null)
                return results;
            RunCheck(results, "ABI compatibility", delegate
            {
                if (first.Layout == "Orolia ART")
                    return first.AbiVersion >= 9 ?
                        Pass("ART mRO-50 feature set available") :
                        Warning("ABI " + first.AbiVersion +
                            " connected; the direct mRO-50 bridge requires ABI 9");
                return first.AbiVersion >= 8 ? Pass("Full Control Center feature set available") :
                    Warning("ABI " + first.AbiVersion + " connected; sensor features require ABI 8");
            });
            RunCheck(results, "PHC advances", delegate
            {
                DateTime start = first.CardTimeUtc;
                Thread.Sleep(180);
                TimeCardSnapshot next = client.GetSnapshot();
                return next.CardTimeUtc > start ? Pass("Clock advanced " +
                    (next.CardTimeUtc - start).TotalMilliseconds.ToString("F1",
                    CultureInfo.InvariantCulture) + " ms") : Fail("Clock did not advance");
            });
            RunCheck(results, "Clock synchronization", delegate
            {
                return first.IsClockSynchronized ? Pass("PHC reports synchronized") :
                    Warning("PHC is live but not synchronized");
            });
            RunCheck(results, "GNSS / ToD", delegate
            {
                if (first.Layout == "Orolia ART" &&
                    !first.GnssTelemetryAvailable)
                    return Skipped("GNSS/ToD summary registers are not exposed by this ART FPGA image");
                return first.GnssFixOk ? Pass(first.GnssFix + ", " +
                    first.LockedSatellites + " satellites locked") :
                    Warning(first.GnssFix + "; antenna or sky view may need attention");
            });
            RunCheck(results, "Card identity", delegate
            {
                TimeCardIdentity identity = client.GetIdentity();
                if (first.Layout == "Orolia ART" && !identity.IsValid)
                    return Skipped("ART EEPROM stores oscillator configuration, not an EUI-48 serial");
                return identity.IsValid ? Pass(identity.SerialNumber) :
                    Warning("MAC EEPROM did not return a valid serial number");
            });
            RunCheck(results, "SMA routing", delegate
            {
                if (first.AbiVersion < 4)
                    return Skipped("Requires ABI 4");
                int present = 0;
                for (uint connector = 1; connector <= 4; connector++)
                    if (client.GetSmaConnector(connector).IsPresent)
                        present++;
                return present == 4 ? Pass("All four connectors responded") :
                    Warning(present + " of 4 connectors reported present");
            });
            RunCheck(results, "NMEA generator", delegate
            {
                if (first.Layout == "Orolia ART")
                    return Skipped("No FPGA ToD/NMEA generator in the ART profile");
                if (first.AbiVersion < 4)
                    return Skipped("Requires ABI 4");
                NmeaOutputState nmea = client.GetNmeaOutput();
                return nmea.IsPresent ? Pass((nmea.IsEnabled ? "Enabled" : "Disabled") +
                    " at " + nmea.Baud + " baud") : Warning("NMEA generator is not present");
            });
            RunCheck(results, "I2C management bus", delegate
            {
                if (first.AbiVersion < 3)
                    return Skipped("Requires ABI 3");
                I2cControllerStatus status = client.GetI2cStatus();
                return status.IsPresent && status.IsEnabled ? Pass("Controller ready") :
                    Warning("Controller present=" + status.IsPresent + ", enabled=" + status.IsEnabled);
            });
            RunCheck(results, "Sensor fabric", delegate
            {
                if (first.Layout == "Orolia ART")
                    return Skipped("PCA9546A sensors and status LEDs are not fitted on ART");
                if (first.AbiVersion < 8)
                    return Skipped("Requires ABI 8");
                SensorTelemetrySnapshot sensors = client.GetSensorTelemetry();
                return sensors.IsValid ? Pass("Environment, rails and IMU sampled") :
                    Warning("Sensor fabric returned no valid data");
            });
            RunCheck(results, "UART transport", delegate
            {
                UartObservation observation = client.ObserveUart(0, 15);
                if (first.Layout == "Orolia ART" && !observation.IsPresent)
                    return Skipped("The installed ART FPGA image does not implement the 16550 UART block");
                return observation.IsPresent ? Pass("GNSS UART present; LSR 0x" +
                    observation.LineStatus.ToString("X2", CultureInfo.InvariantCulture)) :
                    Warning("GNSS UART did not report present");
            });
            RunCheck(results, "Device hierarchy", delegate
            {
                return first.HierarchyRuntimeEnabled ? Pass("Subsystem hierarchy enabled") :
                    Warning("Subsystem hierarchy is disabled at runtime");
            });
            return results;
        }

        private static void RunCheck(List<SelfTestResult> results, string name,
                                     Func<SelfTestResult> check)
        {
            Stopwatch timer = Stopwatch.StartNew();
            try
            {
                SelfTestResult result = check();
                result.Name = name;
                result.Duration = timer.Elapsed;
                results.Add(result);
            }
            catch (Exception ex)
            {
                results.Add(new SelfTestResult
                {
                    Name = name, Outcome = SelfTestOutcome.Fail,
                    Detail = ex.Message, Duration = timer.Elapsed
                });
            }
        }

        private static SelfTestResult Pass(string detail) { return Result(SelfTestOutcome.Pass, detail); }
        private static SelfTestResult Warning(string detail) { return Result(SelfTestOutcome.Warning, detail); }
        private static SelfTestResult Fail(string detail) { return Result(SelfTestOutcome.Fail, detail); }
        private static SelfTestResult Skipped(string detail) { return Result(SelfTestOutcome.Skipped, detail); }
        private static SelfTestResult Result(SelfTestOutcome outcome, string detail)
        {
            return new SelfTestResult { Outcome = outcome, Detail = detail };
        }

        private void PopulateDemoSelfTest()
        {
            lastSelfTestResults.Clear();
            foreach (string name in new[] { "Driver connection", "ABI compatibility", "PHC advances",
                "Clock synchronization", "GNSS / ToD", "Card identity", "SMA routing",
                "NMEA generator", "I2C management bus", "Sensor fabric", "UART transport",
                "Device hierarchy" })
                lastSelfTestResults.Add(new SelfTestResult
                {
                    Name = name, Outcome = SelfTestOutcome.Pass,
                    Detail = "Simulated check passed", Duration = TimeSpan.FromMilliseconds(8)
                });
            SelfTestResultsList.Items.Clear();
            foreach (SelfTestResult result in lastSelfTestResults)
                SelfTestResultsList.Items.Add(result);
            SelfTestSummaryText.Text = "Demo report complete — all simulated checks passed.";
        }

        private void ExportSelfTest_Click(object sender, RoutedEventArgs e)
        {
            if (lastSelfTestResults.Count == 0)
            {
                SelfTestSummaryText.Text = "Run self-test before exporting a report.";
                return;
            }
            SaveFileDialog dialog = new SaveFileDialog
            {
                Title = "Export Time Card self-test report",
                Filter = "Text report (*.txt)|*.txt",
                FileName = "timecard-self-test-" + DateTime.Now.ToString("yyyyMMdd-HHmmss",
                    CultureInfo.InvariantCulture) + ".txt"
            };
            if (dialog.ShowDialog(this) != true)
                return;
            File.WriteAllText(dialog.FileName, BuildSelfTestReport(), new UTF8Encoding(false));
            RememberExportDirectory(dialog.FileName);
            SelfTestSummaryText.Text = "Exported " + Path.GetFileName(dialog.FileName);
        }

        private void CreateSupportBundle_Click(object sender, RoutedEventArgs e)
        {
            SaveFileDialog dialog = new SaveFileDialog
            {
                Title = "Create Time Card support bundle",
                Filter = "ZIP archive (*.zip)|*.zip",
                FileName = "timecard-support-" + DateTime.Now.ToString("yyyyMMdd-HHmmss",
                    CultureInfo.InvariantCulture) + ".zip"
            };
            if (dialog.ShowDialog(this) != true)
                return;
            try
            {
                using (FileStream output = File.Create(dialog.FileName))
                using (ZipArchive archive = new ZipArchive(output, ZipArchiveMode.Create))
                {
                    AddZipText(archive, "diagnostics.txt", lastSnapshot == null ?
                        "No hardware snapshot available." : BuildDiagnostics(lastSnapshot));
                    AddZipText(archive, "self-test.txt", BuildSelfTestReport());
                    AddZipText(archive, "session-log.txt", LogTextBox.Text);
                    AddZipText(archive, "telemetry.csv", telemetrySession.ToCsv());
                    AddZipText(archive, "compatibility.txt", CompatibilityMatrixText.Text);
                    AddZipText(archive, "environment.txt", BuildEnvironmentReport());
                }
                RememberExportDirectory(dialog.FileName);
                SelfTestSummaryText.Text = "Support bundle created: " + Path.GetFileName(dialog.FileName);
                Log("Support bundle created without credentials or private user data.");
            }
            catch (Exception ex)
            {
                SelfTestSummaryText.Text = "Support bundle failed: " + ex.Message;
            }
        }

        private static void AddZipText(ZipArchive archive, string name, string content)
        {
            ZipArchiveEntry entry = archive.CreateEntry(name, CompressionLevel.Optimal);
            using (StreamWriter writer = new StreamWriter(entry.Open(), new UTF8Encoding(false)))
                writer.Write(content ?? string.Empty);
        }

        private string BuildSelfTestReport()
        {
            StringBuilder report = new StringBuilder();
            report.AppendLine("OCP Time Card Control Center — Self-Test Report");
            report.AppendLine("Generated: " + DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture));
            report.AppendLine();
            if (lastSelfTestResults.Count == 0)
                report.AppendLine("No self-test has been run in this session.");
            foreach (SelfTestResult result in lastSelfTestResults)
                report.AppendLine(result + " (" + result.Duration.TotalMilliseconds.ToString("F0",
                    CultureInfo.InvariantCulture) + " ms)");
            return report.ToString();
        }

        private string BuildEnvironmentReport()
        {
            return string.Format(CultureInfo.InvariantCulture,
                "Application: {0}\r\nOS: {1}\r\n64-bit OS: {2}\r\n64-bit process: {3}\r\nAdministrator: {4}\r\nTheme: {5}\r\nDemo mode: {6}\r\n",
                AssemblyVersionText(), Environment.OSVersion, Environment.Is64BitOperatingSystem,
                Environment.Is64BitProcess, HasAdministratorAccess(), productSettings.Theme,
                productSettings.DemoMode);
        }

        private static string AssemblyVersionText()
        {
            return typeof(MainWindow).Assembly.GetName().Version.ToString();
        }

        private void UpdateCompatibilityMatrix(TimeCardSnapshot snapshot)
        {
            if (CompatibilityMatrixText == null)
                return;
            uint abi = productSettings != null && productSettings.DemoMode ? 10 :
                snapshot == null ? 0 : snapshot.AbiVersion;
            CompatibilityMatrixText.Text =
                CompatibilityLine("Core PHC / cross timestamp", abi >= 1, "ABI 1") + "\n" +
                CompatibilityLine("I2C controller / identity", abi >= 3, "ABI 3") + "\n" +
                CompatibilityLine("Clock source / NMEA / SMA", abi >= 4, "ABI 4") + "\n" +
                CompatibilityLine("Signal generators / counters", abi >= 5, "ABI 5") + "\n" +
                CompatibilityLine("Guarded FPGA flash", abi >= 6, "ABI 6") + "\n" +
                CompatibilityLine("Extended management controls", abi >= 7, "ABI 7") + "\n" +
                CompatibilityLine("Environment / rails / IMU", abi >= 8, "ABI 8") + "\n" +
                CompatibilityLine("Orolia ART mRO-50 bridge", abi >= 9, "ABI 9") + "\n" +
                CompatibilityLine("Celestica R4006 sensors", abi >= 10, "ABI 10") + "\n" +
                CompatibilityLine("Trusted FPGA image identity", abi >= 13, "ABI 13");
        }

        private static string CompatibilityLine(string feature, bool available, string requirement)
        {
            return (available ? "READY     " : "UNAVAILABLE ") + feature + "  ·  " + requirement;
        }

        private void ProductSettings_Changed(object sender, RoutedEventArgs e)
        {
            if (!productInitialized)
                return;
            ComboBoxItem themeItem = ProductThemeCombo.SelectedItem as ComboBoxItem;
            if (themeItem != null)
                productSettings.Theme = Convert.ToString(themeItem.Content, CultureInfo.InvariantCulture);
            ComboBoxItem refreshItem = ProductRefreshCombo.SelectedItem as ComboBoxItem;
            double refresh;
            if (refreshItem != null && double.TryParse(Convert.ToString(refreshItem.Tag,
                CultureInfo.InvariantCulture), NumberStyles.Float, CultureInfo.InvariantCulture, out refresh))
                productSettings.RefreshSeconds = refresh;
            productSettings.CompactNavigation = ProductCompactNavCheckBox.IsChecked == true;
            bool oldDemo = productSettings.DemoMode;
            productSettings.DemoMode = ProductDemoCheckBox.IsChecked == true;
            refreshTimer.Interval = TimeSpan.FromSeconds(productSettings.RefreshSeconds);
            ApplyTheme(productSettings.Theme);
            ApplyResponsiveLayout();
            SaveProductSettings();
            if (oldDemo != productSettings.DemoMode)
                SwitchDemoMode(productSettings.DemoMode);
        }

        private async void SwitchDemoMode(bool enabled)
        {
            if (enabled)
            {
                CardSelector.Text = "Demo telemetry (no hardware)";
                UpdateDeviceSelectionControls();
                SetConnectionState(true, "Demo telemetry", false);
                UpdateDemoProduct();
                ProfileApplyButton.IsEnabled = false;
            }
            else
            {
                UpdateDeviceSelectionControls();
                ProfileApplyButton.IsEnabled = true;
                if (client == null)
                    await ConnectAsync();
                UpdateHealthExperience();
            }
        }

        private void UpdateDemoProduct()
        {
            if (!productInitialized)
                return;
            demoPhase += .12;
            DateTime now = DateTime.UtcNow;
            double offset = Math.Sin(demoPhase) * 8 + Math.Sin(demoPhase * .23) * 3;
            double window = 9200 + Math.Sin(demoPhase * .37) * 1100;
            double satellites = 15 + Math.Round(Math.Sin(demoPhase * .18) * 3);
            double temperature = 38.5 + Math.Sin(demoPhase * .07) * 1.8;
            double vibration = .018 + Math.Abs(Math.Sin(demoPhase * 1.7)) * .065 +
                Math.Abs(Math.Sin(demoPhase * .31)) * .012;
            AddVibrationWindow(vibration);
            Brush healthy = (Brush)FindResource("AccentBrush");
            SyncStatusText.Text = "IN SYNC";
            SyncStatusText.Foreground = healthy;
            SyncDetailText.Text = "Simulated disciplined clock";
            GnssFixMetricText.Text = "3D FIX";
            GnssFixMetricText.Foreground = healthy;
            SatelliteText.Text = ((int)satellites + 4).ToString(
                CultureInfo.InvariantCulture) + " seen · " +
                ((int)satellites).ToString(CultureInfo.InvariantCulture) + " locked";
            InterruptText.Text = "64";
            LayoutText.Text = "MSI-X register layout";
            ClockVersionText.Text = "v1.4.0";
            ClockSourceText.Text = "Time-of-Day / GNSS";
            CardTimeText.Text = now.ToString("yyyy-MM-dd  HH:mm:ss.fffffff 'UTC'",
                CultureInfo.InvariantCulture);
            SystemTimeText.Text = now.ToString("HH:mm:ss.fffffff 'UTC'",
                CultureInfo.InvariantCulture);
            OffsetText.Text = offset.ToString("+0.0;-0.0;0.0",
                CultureInfo.InvariantCulture) + " ns";
            SamplingWindowText.Text = window.ToString("F0",
                CultureInfo.InvariantCulture) + " ns";
            OffsetHistoryChart.AddSample(now, offset);
            SamplingHistoryChart.AddSample(now, window);
            OffsetHistoryValueText.Text = OffsetText.Text;
            SamplingHistoryValueText.Text = SamplingWindowText.Text;
            ClockChipText.Text = "SIMULATED · IN SYNC";
            ClockChipText.Foreground = healthy;
            HierarchyOverviewText.Text = "DEMO";
            SidebarDriverText.Text = "Demo data · ABI 12";
            SidebarSerialText.Text = "DEMO-TIMECARD-0001";
            if (!telemetryPaused)
            {
                ProductOffsetChart.AddSample(now, offset);
                ProductSamplingChart.AddSample(now, window);
                CrossTimestampHistogram.AddSample(window);
                ProductSatelliteChart.AddSample(now, satellites);
                ProductTemperatureChart.AddSample(now, temperature);
                ProductVibrationChart.AddSample(now, vibration);
                ProductOffsetValueText.Text = offset.ToString("+0.0;-0.0;0.0",
                    CultureInfo.InvariantCulture) + " ns";
                ProductSamplingValueText.Text = window.ToString("F0",
                    CultureInfo.InvariantCulture) + " ns";
                ProductHistogramSummaryText.Text = CrossTimestampHistogram.Summary("ns");
                ProductSatelliteValueText.Text = satellites.ToString("F0",
                    CultureInfo.InvariantCulture) + " locked";
                ProductTemperatureValueText.Text = temperature.ToString("F1",
                    CultureInfo.InvariantCulture) + " °C";
                ProductVibrationValueText.Text = string.Format(
                    CultureInfo.InvariantCulture, "{0:F3} m/s² · RMS {1:F3}",
                    vibration, VibrationRms());
            }
            if (telemetrySession.IsRecording)
                telemetrySession.Add(new TelemetryPoint
                {
                    TimestampUtc = now, OffsetNanoseconds = offset,
                    SamplingWindowNanoseconds = window,
                    SatellitesSeen = (int)satellites + 4,
                    SatellitesLocked = (int)satellites,
                    BoardTemperatureCelsius = temperature,
                    AtomicPhase = Math.Sin(demoPhase * .12) * .3,
                    VibrationMetersPerSecondSquared = vibration
                });
            if (telemetrySession.IsRecording)
                TelemetrySampleCountText.Text = telemetrySession.Points.Count.ToString(
                    CultureInfo.InvariantCulture) + " samples recorded";
            Stopwatch sensorSampleTimer = Stopwatch.StartNew();
            UpdateDemoCelesticaSensors();
            sensorSampleTimer.Stop();
            RecordSensorSamplingDuration(
                sensorSampleTimer.Elapsed.TotalMilliseconds, true);
            UpdateHealthExperience();
            LastRefreshText.Text = "Demo sample " + DateTime.Now.ToString("HH:mm:ss",
                CultureInfo.InvariantCulture);
        }

        private void UpdateDemoCelesticaSensors()
        {
            const uint presentValidReadyTemperature = 1u | 2u | 8u | 128u;
            double angle = 0.42 + Math.Sin(demoPhase * 0.31) * 0.08;
            int quaternionY = (int)Math.Round(Math.Sin(angle / 2.0) * 16384.0);
            int quaternionW = (int)Math.Round(Math.Cos(angle / 2.0) * 16384.0);
            TimeCardSensorTelemetryRaw raw = new TimeCardSensorTelemetryRaw
            {
                Size = (uint)System.Runtime.InteropServices.Marshal.SizeOf(
                    typeof(TimeCardSensorTelemetryRaw)),
                Flags = 3u,
                MuxChannelMask = 0u,
                ControllerStatus = 0xc0u,
                InterruptStatus = 0xd0u,
                BoardProfile = 3u,
                Capabilities = 16u | 32u | 64u | 8u,
                BoardTemperature = new[]
                {
                    new TimeCardLm75bReadingRaw
                    {
                        Size = 32u, Flags = presentValidReadyTemperature,
                        Address = 0x48u, RawTemperature = 0x2480,
                        TemperatureMilliCelsius = 36500
                    },
                    new TimeCardLm75bReadingRaw
                    {
                        Size = 32u, Flags = presentValidReadyTemperature,
                        Address = 0x49u, RawTemperature = 0x2300,
                        TemperatureMilliCelsius = 35000
                    },
                    new TimeCardLm75bReadingRaw
                    {
                        Size = 32u, Flags = presentValidReadyTemperature,
                        Address = 0x4au, RawTemperature = 0x2180,
                        TemperatureMilliCelsius = 33500
                    }
                },
                Humidity = new TimeCardSht3xReadingRaw
                {
                    Size = 32u, Flags = presentValidReadyTemperature |
                        4u | 64u | 512u,
                    Address = 0x44u, Status = 0u,
                    RawTemperature = 29657u, RawHumidity = 28049u,
                    TemperatureMilliCelsius = 34200,
                    HumidityMilliPercent = 42800u
                },
                Pressure = new TimeCardIcp10100ReadingRaw
                {
                    Size = 48u, Flags = presentValidReadyTemperature |
                        4u | 512u,
                    Address = 0x63u, ProductId = 0x08u,
                    RawPressure = 11477003u, RawTemperature = 28836u,
                    TemperatureMilliCelsius = 32000,
                    Otp = new[] { 10000, 20000, 30000, 4000 }
                },
                Imu = new TimeCardBno055ReadingRaw
                {
                    Size = 136u, Flags = 1u | 2u | 4u | 8u | 128u | 256u,
                    ChipId = 0x80u, OperationMode = 3u,
                    SystemStatus = 1u, Calibration = 0xffu,
                    Temperature = 4032,
                    AccelerationX = 18, AccelerationY = -10,
                    AccelerationZ = 980,
                    LinearAccelerationX = 18, LinearAccelerationY = -10,
                    GravityZ = 981, GyroscopeY = 2,
                    MagneticX = 320, MagneticY = 176,
                    MagneticZ = -96, QuaternionY = quaternionY,
                    QuaternionW = quaternionW
                }
            };
            if (startupArguments.Any(argument => string.Equals(argument,
                    "--demo-no-imu", StringComparison.OrdinalIgnoreCase)))
            {
                raw.Imu = new TimeCardBno055ReadingRaw
                {
                    Size = (uint)System.Runtime.InteropServices.Marshal.SizeOf(
                        typeof(TimeCardBno055ReadingRaw))
                };
            }
            ApplySensorTelemetry(new SensorTelemetrySnapshot(raw));
        }

        private void ApplyTheme(string theme)
        {
            if (string.IsNullOrWhiteSpace(theme))
                theme = "Dark";
            bool high = string.Equals(theme, "High contrast", StringComparison.OrdinalIgnoreCase) ||
                SystemParameters.HighContrast;
            bool midnight = string.Equals(theme, "Midnight blue", StringComparison.OrdinalIgnoreCase);
            SetBrushColor("WindowBrush", high ? "#000000" : midnight ? "#050A18" : "#07111F");
            SetBrushColor("SidebarBrush", high ? "#000000" : midnight ? "#071126" : "#091522");
            SetBrushColor("PanelBrush", high ? "#0A0A0A" : midnight ? "#0B1830" : "#0D1B2A");
            SetBrushColor("PanelRaisedBrush", high ? "#171717" : midnight ? "#12264A" : "#122437");
            SetBrushColor("BorderBrush", high ? "#FFFFFF" : midnight ? "#254A75" : "#1D344A");
            SetBrushColor("TextBrush", "#F4F8FC");
            SetBrushColor("MutedBrush", high ? "#E0E0E0" : "#8FA6BB");
            SetBrushColor("AccentBrush", high ? "#FFFF00" : "#69C441");
            RootBorder.Background = (Brush)FindResource("WindowBrush");
        }

        private void SetBrushColor(string key, string color)
        {
            Color value = (Color)ColorConverter.ConvertFromString(color);
            Application.Current.Resources[key] = new SolidColorBrush(value);
        }

        private void SaveProductSettings()
        {
            SettingsStore.Save(SettingsStore.SettingsPath, productSettings);
        }

        private void RememberExportDirectory(string path)
        {
            productSettings.LastExportDirectory = Path.GetDirectoryName(path);
            SaveProductSettings();
        }

        private static string ExistingDirectory(string path)
        {
            return !string.IsNullOrWhiteSpace(path) && Directory.Exists(path) ? path : null;
        }

        private static string SafeFileName(string name)
        {
            string result = name ?? "profile";
            foreach (char invalid in Path.GetInvalidFileNameChars())
                result = result.Replace(invalid, '-');
            return result;
        }

        private void UartFilter_Changed(object sender, RoutedEventArgs e)
        {
            if (UartOutputTextBox != null)
                RenderUartConsole();
        }

        private void UartPauseDisplay_Changed(object sender, RoutedEventArgs e)
        {
            if (UartPauseDisplayCheckBox == null)
                return;
            uartDisplayPaused = UartPauseDisplayCheckBox.IsChecked == true;
            if (!uartDisplayPaused)
                RenderUartConsole();
            UartStatusText.Text = uartDisplayPaused ?
                "Display paused · receive capture continues" : "Display resumed";
        }

        private bool ShouldRenderUartEntry(UartConsoleEntry entry)
        {
            if (entry == null)
                return false;
            ComboBoxItem directionItem = UartDirectionCombo == null ? null :
                UartDirectionCombo.SelectedItem as ComboBoxItem;
            string direction = directionItem == null ? "All" :
                Convert.ToString(directionItem.Tag, CultureInfo.InvariantCulture);
            if (direction != "All" && !string.Equals(direction, entry.Direction,
                StringComparison.OrdinalIgnoreCase))
                return false;
            string filter = UartFilterTextBox == null ? string.Empty :
                UartFilterTextBox.Text.Trim();
            if (filter.Length == 0)
                return true;
            string searchable = entry.PortLabel + " " + entry.Direction + " " +
                RenderUartConsoleEntry(entry);
            return searchable.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private void OnUartEntryAppended(UartConsoleEntry entry)
        {
            long rx = 0;
            long tx = 0;
            foreach (UartConsoleEntry item in uartConsoleEntries)
            {
                if (string.Equals(item.Direction, "TX", StringComparison.OrdinalIgnoreCase))
                    tx += item.Data.Length;
                else
                    rx += item.Data.Length;
            }
            if (UartTrafficCounterText != null)
                UartTrafficCounterText.Text = string.Format(CultureInfo.InvariantCulture,
                    "  ·  RX {0}  ·  TX {1}  ·  {2} frames", FormatByteCount(rx),
                    FormatByteCount(tx), uartConsoleEntries.Count);
            if (uartCaptureActive && UartCaptureButton != null)
                UartCaptureButton.Content = "Stop capture (" +
                    (uartConsoleEntries.Count - uartCaptureStartIndex).ToString(
                    CultureInfo.InvariantCulture) + ")";
        }

        private static string FormatByteCount(long bytes)
        {
            return bytes >= 1048576 ? (bytes / 1048576.0).ToString("F1",
                CultureInfo.InvariantCulture) + " MiB" : bytes >= 1024 ?
                (bytes / 1024.0).ToString("F1", CultureInfo.InvariantCulture) +
                " KiB" : bytes.ToString(CultureInfo.InvariantCulture) + " B";
        }

        private void OnUartConsoleCleared()
        {
            uartCaptureActive = false;
            uartCaptureStartIndex = 0;
            uartCaptureEndIndex = -1;
            if (UartCaptureButton != null)
                UartCaptureButton.Content = "Start capture";
            if (UartTrafficCounterText != null)
                UartTrafficCounterText.Text = "  ·  RX 0 B  ·  TX 0 B";
        }

        private void UartCapture_Click(object sender, RoutedEventArgs e)
        {
            if (!uartCaptureActive)
            {
                uartCaptureActive = true;
                uartCaptureStartIndex = uartConsoleEntries.Count;
                uartCaptureEndIndex = -1;
                UartCaptureButton.Content = "Stop capture (0)";
                UartStatusText.Text = "Serial capture started";
            }
            else
            {
                uartCaptureActive = false;
                uartCaptureEndIndex = uartConsoleEntries.Count;
                int count = Math.Max(0, uartCaptureEndIndex - uartCaptureStartIndex);
                UartCaptureButton.Content = "Start capture";
                UartStatusText.Text = count.ToString(CultureInfo.InvariantCulture) +
                    " serial frame(s) captured · ready to export";
            }
        }

        private IList<UartConsoleEntry> CapturedUartEntries()
        {
            int start = uartCaptureStartIndex;
            int end = uartCaptureActive ? uartConsoleEntries.Count :
                uartCaptureEndIndex >= 0 ? uartCaptureEndIndex : uartConsoleEntries.Count;
            if (uartCaptureEndIndex < 0 && !uartCaptureActive)
                start = 0;
            start = Math.Max(0, Math.Min(start, uartConsoleEntries.Count));
            end = Math.Max(start, Math.Min(end, uartConsoleEntries.Count));
            return uartConsoleEntries.Skip(start).Take(end - start).ToList().AsReadOnly();
        }

        private void UartExport_Click(object sender, RoutedEventArgs e)
        {
            IList<UartConsoleEntry> entries = CapturedUartEntries();
            if (entries.Count == 0)
            {
                UartStatusText.Text = "No serial data is available to export.";
                return;
            }
            SaveFileDialog dialog = new SaveFileDialog
            {
                Title = "Export serial capture",
                Filter = "Console log (*.log)|*.log|CSV frames (*.csv)|*.csv|JSON frames (*.json)|*.json|Binary payload (*.bin)|*.bin",
                FileName = "timecard-uart-" + DateTime.Now.ToString("yyyyMMdd-HHmmss",
                    CultureInfo.InvariantCulture) + ".log",
                InitialDirectory = ExistingDirectory(productSettings.LastExportDirectory)
            };
            if (dialog.ShowDialog(this) != true)
                return;
            string extension = Path.GetExtension(dialog.FileName).ToLowerInvariant();
            if (extension == ".bin")
            {
                using (FileStream stream = File.Create(dialog.FileName))
                    foreach (UartConsoleEntry entry in entries)
                        stream.Write(entry.Data, 0, entry.Data.Length);
            }
            else
            {
                string content = extension == ".csv" ? UartEntriesToCsv(entries) :
                    extension == ".json" ? UartEntriesToJson(entries) :
                    string.Concat(entries.Select(RenderUartConsoleEntry));
                File.WriteAllText(dialog.FileName, content, new UTF8Encoding(false));
            }
            RememberExportDirectory(dialog.FileName);
            UartStatusText.Text = "Exported " + entries.Count.ToString(
                CultureInfo.InvariantCulture) + " frame(s) to " + Path.GetFileName(dialog.FileName);
        }

        private static string UartEntriesToCsv(IEnumerable<UartConsoleEntry> entries)
        {
            StringBuilder output = new StringBuilder(
                "timestamp,port,direction,length,line_status,hex\r\n");
            foreach (UartConsoleEntry entry in entries)
                output.AppendFormat(CultureInfo.InvariantCulture,
                    "{0:O},\"{1}\",{2},{3},{4},\"{5}\"\r\n",
                    entry.Timestamp, entry.PortLabel.Replace("\"", "\"\""),
                    entry.Direction, entry.Data.Length,
                    entry.HasLineStatus ? "0x" + entry.LineStatus.ToString("X2",
                        CultureInfo.InvariantCulture) : string.Empty,
                    string.Join(" ", entry.Data.Select(value => value.ToString("X2",
                        CultureInfo.InvariantCulture))));
            return output.ToString();
        }

        private static string UartEntriesToJson(IEnumerable<UartConsoleEntry> entries)
        {
            IList<UartConsoleEntry> list = entries.ToList();
            StringBuilder output = new StringBuilder("[\r\n");
            for (int index = 0; index < list.Count; index++)
            {
                UartConsoleEntry entry = list[index];
                output.AppendFormat(CultureInfo.InvariantCulture,
                    "  {{\"timestamp\":\"{0:O}\",\"port\":\"{1}\",\"direction\":\"{2}\",\"length\":{3},\"lineStatus\":{4},\"base64\":\"{5}\"}}{6}\r\n",
                    entry.Timestamp, JsonEscape(entry.PortLabel),
                    JsonEscape(entry.Direction), entry.Data.Length,
                    entry.HasLineStatus ? entry.LineStatus.ToString(
                        CultureInfo.InvariantCulture) : "null",
                    Convert.ToBase64String(entry.Data), index + 1 == list.Count ?
                        string.Empty : ",");
            }
            return output.Append("]\r\n").ToString();
        }

        private static string JsonEscape(string value)
        {
            return (value ?? string.Empty).Replace("\\", "\\\\")
                .Replace("\"", "\\\"").Replace("\r", "\\r").Replace("\n", "\\n");
        }

        private void UartReplay_Click(object sender, RoutedEventArgs e)
        {
            OpenFileDialog dialog = new OpenFileDialog
            {
                Title = "Replay a serial byte stream",
                Filter = "Serial capture (*.bin;*.ubx;*.nmea;*.log)|*.bin;*.ubx;*.nmea;*.log|All files (*.*)|*.*"
            };
            if (dialog.ShowDialog(this) != true)
                return;
            FileInfo file = new FileInfo(dialog.FileName);
            if (file.Length > 16 * 1024 * 1024)
            {
                UartStatusText.Text = "Replay files are limited to 16 MiB.";
                return;
            }
            byte[] data = File.ReadAllBytes(dialog.FileName);
            string genericPort = SelectedGenericComPort();
            uint hardwarePort = genericPort == null ? SelectedUartPort() : 0;
            for (int offset = 0; offset < data.Length; offset += 256)
            {
                int length = Math.Min(256, data.Length - offset);
                byte[] chunk = new byte[length];
                Array.Copy(data, offset, chunk, 0, length);
                if (genericPort != null)
                    AppendGenericUart(genericPort + " replay", "RX", chunk);
                else
                    AppendUart(hardwarePort, "RX", chunk, 0);
            }
            UartStatusText.Text = "Replayed " + FormatByteCount(data.Length) +
                " from " + Path.GetFileName(dialog.FileName) + " without transmitting to hardware.";
        }

        private void MainWindow_SizeChanged(object sender, SizeChangedEventArgs e)
        {
            ApplyResponsiveLayout();
        }

        private void ApplyResponsiveLayout()
        {
            if (SidebarColumn == null || productSettings == null)
                return;
            bool compact = productSettings.CompactNavigation || ActualWidth < 1280;
            SidebarColumn.Width = new GridLength(compact ? 82 : 240);
            TitleBrandColumn.Width = new GridLength(compact ? 82 : 240);
            SidebarLogoPanel.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
            SidebarWorkspaceLabel.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
            SidebarSessionPanel.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
            HeaderBrandPanel.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
        }

        private async void MainWindow_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.F5)
            {
                if (productSettings.DemoMode)
                    UpdateDemoProduct();
                else
                    await RefreshSnapshotAsync(true);
                e.Handled = true;
            }
            else if ((Keyboard.Modifiers & ModifierKeys.Control) != 0 && e.Key == Key.R)
            {
                TelemetryRecord_Click(sender, new RoutedEventArgs());
                e.Handled = true;
            }
            else if ((Keyboard.Modifiers & ModifierKeys.Control) != 0 && e.Key == Key.E)
            {
                NavigateToWorkspace("Telemetry");
                TelemetryExport_Click(sender, new RoutedEventArgs());
                e.Handled = true;
            }
            else if ((Keyboard.Modifiers & ModifierKeys.Control) != 0 && e.Key == Key.D1)
            {
                OverviewNav.IsChecked = true;
                e.Handled = true;
            }
            else if ((Keyboard.Modifiers & ModifierKeys.Control) != 0 && e.Key == Key.D2)
            {
                ClockNav.IsChecked = true;
                e.Handled = true;
            }
            else if ((Keyboard.Modifiers & ModifierKeys.Control) != 0 && e.Key == Key.D3)
            {
                GnssNav.IsChecked = true;
                e.Handled = true;
            }
        }
    }
}
