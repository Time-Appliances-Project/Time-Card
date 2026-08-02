using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace TimeCardControlCenter
{
    public partial class MainWindow
    {
        private const uint FpgaRequiredAbi = 12;
        private const uint FpgaImageRequiredAbi = 13;
        private bool fpgaRefreshing;
        private FpgaCapabilities lastFpgaCapabilities;
        private FpgaImageInfo lastFpgaImageInfo;
        private PpsEngineState lastPpsMaster;
        private PpsEngineState lastPpsSlave;
        private TimecodeEngineState lastIrigMaster;
        private TimecodeEngineState lastIrigSlave;
        private TimecodeEngineState lastDcfMaster;
        private TimecodeEngineState lastDcfSlave;
        private TodParserState lastTodParser;
        private FpgaImageContract lastFpgaContract;
        private CoreInventory lastCoreInventory;
        private ClockAdjustmentState lastClockAdjustment;
        private ClockAdvancedState lastClockAdvanced;
        private NmeaUtcState lastNmeaUtc;
        private readonly TimestampChannelState[] lastTimestampChannels =
            new TimestampChannelState[6];

        private sealed class FpgaRefreshResult
        {
            public FpgaRefreshResult()
            {
                Errors = new List<string>();
            }

            public FpgaCapabilities Capabilities { get; set; }
            public FpgaImageInfo ImageInfo { get; set; }
            public ClockTelemetryState Clock { get; set; }
            public PpsEngineState PpsMaster { get; set; }
            public PpsEngineState PpsSlave { get; set; }
            public TimecodeEngineState IrigMaster { get; set; }
            public TimecodeEngineState IrigSlave { get; set; }
            public TimecodeEngineState DcfMaster { get; set; }
            public TimecodeEngineState DcfSlave { get; set; }
            public TodParserState Tod { get; set; }
            public FpgaImageContract Contract { get; set; }
            public CoreInventory Inventory { get; set; }
            public ClockAdjustmentState ClockAdjustment { get; set; }
            public ClockAdvancedState ClockAdvanced { get; set; }
            public NmeaUtcState NmeaUtc { get; set; }
            public TimestampChannelState[] Timestamps { get; set; }
            public List<string> Errors { get; private set; }
        }

        private async void RefreshFpgaCores_Click(object sender,
                                                   RoutedEventArgs e)
        {
            await RefreshFpgaCoresAsync();
        }

        private async Task RefreshFpgaCoresAsync()
        {
            if (fpgaRefreshing)
                return;
            if (productSettings != null && productSettings.DemoMode)
            {
                SetFpgaUnavailable("DEMO MODE",
                    "Hardware writes are disabled while demo data is active.");
                FpgaCoreStatusDot.Fill = (Brush)FindResource("CyanBrush");
                FpgaCoreStatusText.Text = "DEMO / ABI 12";
                FpgaClockTelemetryText.Text =
                    "Clock core 1.4.0 | synchronized\n" +
                    "In-sync threshold 1000 ns\n" +
                    "Optional servo and correction-log registers are not advertised.";
                return;
            }
            if (client == null)
            {
                SetFpgaUnavailable("NOT CONNECTED",
                    "Connect to the Time Card driver to inspect its FPGA engines.");
                return;
            }
            if (lastSnapshot == null || lastSnapshot.AbiVersion < FpgaRequiredAbi)
            {
                SetFpgaUnavailable("ABI 12 REQUIRED",
                    "Install Time Card driver 1.39 or newer to use documented FPGA-core controls.");
                return;
            }

            fpgaRefreshing = true;
            FpgaCoreStatusText.Text = "READING";
            FpgaCoreStatusText.Foreground = (Brush)FindResource("CyanBrush");
            FpgaCoreStatusDot.Fill = (Brush)FindResource("CyanBrush");
            try
            {
                FpgaRefreshResult result = await Task.Run(() => QueryFpgaCores());
                ApplyFpgaRefreshResult(result);
            }
            catch (Exception ex)
            {
                SetFpgaUnavailable("QUERY FAILED", ex.Message);
                Log("FPGA engine refresh failed: " + ex.Message);
            }
            finally
            {
                fpgaRefreshing = false;
            }
        }

        private FpgaRefreshResult QueryFpgaCores()
        {
            FpgaRefreshResult result = new FpgaRefreshResult();
            result.Capabilities = TryFpgaQuery(
                () => client.GetFpgaCapabilities(), "capabilities", result.Errors);
            if (result.Capabilities == null)
                return result;
            uint cores = result.Capabilities.CoreMask;
            if (result.Capabilities.AbiVersion >= FpgaImageRequiredAbi &&
                (result.Capabilities.Layout == 1u ||
                 result.Capabilities.Layout == 2u))
                result.ImageInfo = TryFpgaQuery(
                    () => client.GetFpgaImageInfo(), "image identity", result.Errors);
            if ((result.Capabilities.FeatureFlags & (1u << 3)) != 0)
                result.Clock = TryFpgaQuery(
                    () => client.GetClockTelemetry(), "clock telemetry", result.Errors);
            if ((cores & (1u << 0)) != 0)
                result.PpsMaster = TryFpgaQuery(
                    () => client.GetPpsEngine(1), "PPS master", result.Errors);
            if ((cores & (1u << 1)) != 0)
                result.PpsSlave = TryFpgaQuery(
                    () => client.GetPpsEngine(2), "PPS slave", result.Errors);
            if ((cores & (1u << 2)) != 0)
                result.IrigMaster = TryFpgaQuery(
                    () => client.GetTimecodeEngine(1, 1), "IRIG master", result.Errors);
            if ((cores & (1u << 3)) != 0)
                result.IrigSlave = TryFpgaQuery(
                    () => client.GetTimecodeEngine(1, 2), "IRIG slave", result.Errors);
            if ((cores & (1u << 4)) != 0)
                result.DcfMaster = TryFpgaQuery(
                    () => client.GetTimecodeEngine(2, 1), "DCF master", result.Errors);
            if ((cores & (1u << 5)) != 0)
                result.DcfSlave = TryFpgaQuery(
                    () => client.GetTimecodeEngine(2, 2), "DCF slave", result.Errors);
            if ((cores & (1u << 6)) != 0)
                result.Tod = TryFpgaQuery(
                    () => client.GetTodParser(), "ToD parser", result.Errors);
            if (result.Capabilities.AbiVersion >= 15u)
            {
                if ((result.Capabilities.FeatureFlags & (1u << 8)) != 0u)
                    result.Inventory = TryFpgaQuery(
                        () => client.GetCoreInventory(), "static core inventory",
                        result.Errors);
                if ((result.Capabilities.FeatureFlags & (1u << 10)) != 0u)
                    result.Contract = TryFpgaQuery(
                        () => client.GetFpgaImageContract(),
                        "exact image contract", result.Errors);
                if (result.Capabilities.Layout == 1u ||
                    result.Capabilities.Layout == 2u)
                    result.ClockAdjustment = TryFpgaQuery(
                        () => client.GetClockAdjustment(),
                        "smooth clock adjustment", result.Errors);
                if (result.Contract != null && result.Contract.IsActive &&
                    (result.Contract.EffectiveFlags & 0x03u) != 0u)
                    result.ClockAdvanced = TryFpgaQuery(
                        () => client.GetClockAdvanced(),
                        "advanced clock controls", result.Errors);
                if (result.Contract != null && result.Contract.IsActive &&
                    (result.Contract.EffectiveFlags & 0x08u) != 0u)
                    result.NmeaUtc = TryFpgaQuery(
                        () => client.GetNmeaUtc(), "NMEA UTC metadata",
                        result.Errors);
                if ((cores & (1u << 10)) != 0u)
                {
                    result.Timestamps = new TimestampChannelState[6];
                    for (uint channel = 0u; channel < 6u; ++channel)
                    {
                        uint captured = channel;
                        result.Timestamps[channel] = TryFpgaQuery(
                            () => client.GetTimestampChannel(captured),
                            "timestamp channel " + captured.ToString(
                                CultureInfo.InvariantCulture), result.Errors);
                    }
                }
            }
            return result;
        }

        private static T TryFpgaQuery<T>(Func<T> query, string name,
            ICollection<string> errors) where T : class
        {
            try
            {
                return query();
            }
            catch (Exception ex)
            {
                errors.Add(name + ": " + ex.Message);
                return null;
            }
        }

        private void ApplyFpgaRefreshResult(FpgaRefreshResult result)
        {
            lastFpgaCapabilities = result.Capabilities;
            lastFpgaImageInfo = result.ImageInfo;
            lastPpsMaster = result.PpsMaster;
            lastPpsSlave = result.PpsSlave;
            lastIrigMaster = result.IrigMaster;
            lastIrigSlave = result.IrigSlave;
            lastDcfMaster = result.DcfMaster;
            lastDcfSlave = result.DcfSlave;
            lastTodParser = result.Tod;
            lastFpgaContract = result.Contract;
            lastCoreInventory = result.Inventory;
            lastClockAdjustment = result.ClockAdjustment;
            lastClockAdvanced = result.ClockAdvanced;
            lastNmeaUtc = result.NmeaUtc;
            for (int index = 0; index < lastTimestampChannels.Length; ++index)
                lastTimestampChannels[index] = result.Timestamps == null ?
                    null : result.Timestamps[index];

            ApplyPpsState(result.PpsMaster, 1);
            ApplyPpsState(result.PpsSlave, 2);
            ApplyTimecodeState(result.IrigMaster, 1);
            ApplyTimecodeState(result.IrigSlave, 2);
            ApplyTimecodeState(result.DcfMaster, 3);
            ApplyTimecodeState(result.DcfSlave, 4);
            ApplyTodParserState(result.Tod);
            ApplyClockTelemetry(result.Clock);
            ApplyFpgaGapStates();

            if (result.Capabilities != null)
            {
                string imageIdentity;
                if (result.ImageInfo != null)
                {
                    imageIdentity = string.Format(CultureInfo.InvariantCulture,
                        "Image identity: {0} {1}, tag {2}, raw 0x{3:X8}, trusted register 0x{4:X8}. ",
                        result.ImageInfo.ImageKind,
                        result.ImageInfo.VersionText,
                        result.ImageInfo.ImageTag,
                        result.ImageInfo.RawVersion,
                        result.ImageInfo.RegisterOffset);
                }
                else if (result.Capabilities.Layout == 3u)
                {
                    imageIdentity = "Image identity is intentionally unavailable on ART layouts; no standard static image register is assumed. ";
                }
                else if (result.Capabilities.AbiVersion < FpgaImageRequiredAbi)
                {
                    imageIdentity = "Install ABI 13 or newer to read the trusted static image identity. ";
                }
                else
                {
                    imageIdentity = "The trusted static image register did not return a valid identity. ";
                }
                FpgaDiscoveryText.Text = string.Format(
                    CultureInfo.InvariantCulture,
                    "Static profile {0} / layout {1} reports core mask 0x{2:X8} and feature mask 0x{3:X8}. " +
                    "Known gaps: 0x{4:X8}. {5}The production bitstream does not publish a Configuration Slave ROM, " +
                    "so the driver never probes undocumented BAR addresses. Version and register readback confirm " +
                    "the mapped interface, not which synthesis-optional protocol or feature logic is present; " +
                    "verify operation with a known input or loopback.",
                    result.Capabilities.BoardProfile,
                    result.Capabilities.Layout,
                    result.Capabilities.CoreMask,
                    result.Capabilities.FeatureFlags,
                    result.Capabilities.KnownGaps,
                    imageIdentity);
            }
            if (lastSnapshot != null && DiagnosticsSummaryText != null)
                DiagnosticsSummaryText.Text = BuildDiagnostics(lastSnapshot);

            int profiled = result.Capabilities == null ? 0 :
                CountBits(result.Capabilities.CoreMask);
            if (result.Errors.Count == 0)
            {
                FpgaCoreStatusText.Text = profiled == 0 ? "PROFILE UNSUPPORTED" :
                    profiled.ToString(CultureInfo.InvariantCulture) + " PROFILE CORES";
                FpgaCoreStatusText.Foreground = profiled == 0 ?
                    (Brush)FindResource("GoldBrush") :
                    (Brush)FindResource("AccentBrush");
                FpgaCoreStatusDot.Fill = FpgaCoreStatusText.Foreground;
            }
            else
            {
                FpgaCoreStatusText.Text = "PARTIAL / " + result.Errors.Count + " ERROR" +
                    (result.Errors.Count == 1 ? string.Empty : "S");
                FpgaCoreStatusText.Foreground = (Brush)FindResource("GoldBrush");
                FpgaCoreStatusDot.Fill = (Brush)FindResource("GoldBrush");
                foreach (string error in result.Errors)
                    Log("FPGA " + error);
            }
        }

        private static int CountBits(uint value)
        {
            int count = 0;
            while (value != 0)
            {
                count += (int)(value & 1u);
                value >>= 1;
            }
            return count;
        }

        private bool HasFpgaFeature(uint feature)
        {
            return lastFpgaCapabilities != null &&
                (lastFpgaCapabilities.FeatureFlags & feature) != 0;
        }

        private void ApplyPpsState(PpsEngineState state, uint core)
        {
            bool master = core == 1;
            TextBlock status = master ? PpsMasterStatusText : PpsSlaveStatusText;
            CheckBox enabled = master ? PpsMasterEnabledCheckBox : PpsSlaveEnabledCheckBox;
            ComboBox polarity = master ? PpsMasterPolarityCombo : PpsSlavePolarityCombo;
            TextBox delay = master ? PpsMasterDelayTextBox : PpsSlaveDelayTextBox;
            TextBlock detail = master ? PpsMasterDetailText : PpsSlaveDetailText;
            Button clear = master ? PpsMasterClearButton : PpsSlaveClearButton;
            Button apply = master ? PpsMasterApplyButton : PpsSlaveApplyButton;
            TextBox width = master ? PpsMasterWidthTextBox : null;

            bool available = state != null && state.IsPresent;
            if (!available)
            {
                bool advertised = lastFpgaCapabilities != null &&
                    (lastFpgaCapabilities.CoreMask & (1u << (int)(core - 1))) != 0;
                SetCoreStatus(status, state == null && advertised ?
                    "QUERY FAILED" : "NOT PRESENT",
                    "GoldBrush");
                enabled.IsEnabled = polarity.IsEnabled = delay.IsEnabled = false;
                if (width != null) width.IsEnabled = false;
                clear.IsEnabled = apply.IsEnabled = false;
                if (!master) PpsSlaveWidthText.Text = "- ms";
                detail.Text = "This core is not published by the active board profile.";
                return;
            }

            bool revisionWritable = master ?
                FpgaVersionAtLeast(state.Version, 1u, 4u) :
                FpgaVersionAtLeast(state.Version, 1u, 2u);
            bool writable = HasFpgaFeature(1u << 0) && revisionWritable;

            enabled.IsChecked = state.IsEnabled;
            SelectComboTag(polarity, state.IsActiveHigh ? 1ul : 0ul);
            delay.Text = state.CableDelayNanoseconds.ToString(CultureInfo.InvariantCulture);
            if (master)
                width.Text = state.PulseWidthMilliseconds.ToString(CultureInfo.InvariantCulture);
            else
                PpsSlaveWidthText.Text = state.PulseWidthMilliseconds == 0x3ffu ?
                    "unknown" : state.PulseWidthMilliseconds.ToString(
                        CultureInfo.InvariantCulture) + " ms";
            string stateText = state.HasSupervisionError ? "SUPERVISION ERROR" :
                state.HasFilterError ? "FILTER ERROR" : state.HasError ? "ERROR" :
                state.IsEnabled ? "RUNNING" : "DISABLED";
            SetCoreStatus(status, stateText, state.HasError ? "DangerBrush" :
                state.IsEnabled ? "AccentBrush" : "MutedBrush");
            bool statusAvailable = master ?
                FpgaVersionAtLeast(state.Version, 1u, 2u) :
                FpgaVersionAtLeast(state.Version, 1u, 3u);
            string statusDetail = statusAvailable ?
                string.Format(CultureInfo.InvariantCulture,
                    "Status 0x{0:X8}", state.Status) :
                (master ? "Status requires v1.2+" : "Status requires v1.3+");
            detail.Text = string.Format(CultureInfo.InvariantCulture,
                "Version {0} | Control 0x{1:X8} | {2}{3}",
                FormatFpgaVersion(state.Version), state.Control, statusDetail,
                writable ? string.Empty : !revisionWritable ?
                    (master ? " | configuration requires v1.4+" :
                        " | configuration requires v1.2+") :
                    " | read-only capability");
            enabled.IsEnabled = polarity.IsEnabled = delay.IsEnabled = writable;
            if (width != null)
                width.IsEnabled = writable && state.IsPulseWidthWritable;
            clear.IsEnabled = writable && state.HasError;
            apply.IsEnabled = writable;
        }

        private void ApplyTimecodeState(TimecodeEngineState state, uint target)
        {
            TextBlock status;
            CheckBox enabled;
            TextBlock detail;
            Button clear;
            Button apply;
            if (target == 1)
            {
                status = IrigMasterStatusText; enabled = IrigMasterEnabledCheckBox;
                detail = IrigMasterDetailText; clear = IrigMasterClearButton;
                apply = IrigMasterApplyButton;
            }
            else if (target == 2)
            {
                status = IrigSlaveStatusText; enabled = IrigSlaveEnabledCheckBox;
                detail = IrigSlaveDetailText; clear = IrigSlaveClearButton;
                apply = IrigSlaveApplyButton;
            }
            else if (target == 3)
            {
                status = DcfMasterStatusText; enabled = DcfMasterEnabledCheckBox;
                detail = DcfMasterDetailText; clear = DcfMasterClearButton;
                apply = DcfMasterApplyButton;
            }
            else
            {
                status = DcfSlaveStatusText; enabled = DcfSlaveEnabledCheckBox;
                detail = DcfSlaveDetailText; clear = DcfSlaveClearButton;
                apply = DcfSlaveApplyButton;
            }

            bool available = state != null && state.IsPresent;
            if (!available)
            {
                int coreBit = target == 1 ? 2 : target == 2 ? 3 :
                    target == 3 ? 4 : 5;
                bool advertised = lastFpgaCapabilities != null &&
                    (lastFpgaCapabilities.CoreMask & (1u << coreBit)) != 0;
                SetCoreStatus(status, state == null && advertised ?
                    "QUERY FAILED" : "NOT PRESENT",
                    "GoldBrush");
                enabled.IsEnabled = clear.IsEnabled = apply.IsEnabled = false;
                SetTimecodeInputsEnabled(target, false, null);
                detail.Text = "This core is not published by the active board profile.";
                return;
            }

            bool irig = target <= 2;
            bool revisionWritable = !irig ||
                FpgaVersionAtLeast(state.Version, 1u, 2u);
            bool writable = HasFpgaFeature(1u << 1) && revisionWritable;

            enabled.IsChecked = state.IsEnabled;
            SetCoreStatus(status, state.HasError ? "ERROR" :
                state.IsEnabled ? "RUNNING" : "DISABLED",
                state.HasError ? "DangerBrush" :
                state.IsEnabled ? "AccentBrush" : "MutedBrush");
            bool statusAvailable = !irig ||
                FpgaVersionAtLeast(state.Version, 1u, 1u);
            string statusDetail = statusAvailable ?
                string.Format(CultureInfo.InvariantCulture,
                    "Status 0x{0:X8}", state.Status) :
                "Status requires v1.1+";
            detail.Text = string.Format(CultureInfo.InvariantCulture,
                "Version {0} | Control 0x{1:X8} | {2}{3}",
                FormatFpgaVersion(state.Version), state.Control, statusDetail,
                writable ? string.Empty : !revisionWritable ?
                    " | IRIG configuration requires v1.2+" :
                    " | read-only capability");
            enabled.IsEnabled = writable;
            clear.IsEnabled = writable && state.HasError;
            apply.IsEnabled = writable;
            SetTimecodeInputsEnabled(target, writable, state);
        }

        private void SetTimecodeInputsEnabled(uint target, bool available,
            TimecodeEngineState state)
        {
            if (target == 1)
            {
                bool modeAndCodeWritable = available && state != null &&
                    FpgaVersionAtLeast(state.Version, 1u, 2u);
                bool irigGSupported = modeAndCodeWritable &&
                    FpgaVersionAtLeast(state.Version, 1u, 3u);
                IrigMasterModeCombo.IsEnabled = modeAndCodeWritable;
                SetComboItemEnabled(IrigMasterModeCombo, 1u,
                    modeAndCodeWritable);
                SetComboItemEnabled(IrigMasterModeCombo, 2u,
                    irigGSupported);
                IrigMasterCodeTextBox.IsEnabled = modeAndCodeWritable;
                IrigMasterCorrectionTextBox.IsEnabled = available;
                IrigMasterControlBitsTextBox.IsEnabled = available &&
                    state != null && state.IsControlBitsWritable;
                IrigMasterAmplitudeCheckBox.IsEnabled = available &&
                    state != null && state.IsAmplitudeModulationWritable;
                if (state != null)
                {
                    SelectComboTag(IrigMasterModeCombo, state.Mode);
                    IrigMasterCodeTextBox.Text = state.Code.ToString(CultureInfo.InvariantCulture);
                    IrigMasterCorrectionTextBox.Text = state.CorrectionSeconds.ToString(CultureInfo.InvariantCulture);
                    IrigMasterControlBitsTextBox.Text = state.ControlBits.ToString("X7", CultureInfo.InvariantCulture);
                    IrigMasterAmplitudeCheckBox.IsChecked =
                        state.AmplitudeModulation != 0u;
                }
            }
            else if (target == 2)
            {
                bool modeWritable = available && state != null &&
                    FpgaVersionAtLeast(state.Version, 1u, 3u);
                IrigSlaveModeCombo.IsEnabled = modeWritable;
                SetComboItemEnabled(IrigSlaveModeCombo, 1u, available);
                SetComboItemEnabled(IrigSlaveModeCombo, 2u, modeWritable);
                IrigSlaveDelayTextBox.IsEnabled = available && state != null && state.IsDelayWritable;
                IrigSlaveCorrectionTextBox.IsEnabled = available;
                IrigSlaveAmplitudeCheckBox.IsEnabled = available &&
                    state != null && state.IsAmplitudeModulationWritable;
                IrigSlaveYearTextBox.IsEnabled = available && state != null &&
                    state.IsManualYearWritable;
                if (state != null)
                {
                    SelectComboTag(IrigSlaveModeCombo, state.Mode);
                    IrigSlaveDelayTextBox.Text = state.DelayNanoseconds.ToString(CultureInfo.InvariantCulture);
                    IrigSlaveCorrectionTextBox.Text = state.CorrectionSeconds.ToString(CultureInfo.InvariantCulture);
                    IrigSlaveControlBitsText.Text = "0x" + state.ControlBits.ToString("X7", CultureInfo.InvariantCulture);
                    IrigSlaveAmplitudeCheckBox.IsChecked =
                        state.AmplitudeModulation != 0u;
                    IrigSlaveYearTextBox.Text = state.ManualYear.ToString(
                        CultureInfo.InvariantCulture);
                }
            }
            else if (target == 3)
            {
                DcfMasterCorrectionTextBox.IsEnabled = available;
                if (state != null)
                    DcfMasterCorrectionTextBox.Text = state.CorrectionSeconds.ToString(CultureInfo.InvariantCulture);
            }
            else
            {
                DcfSlaveCorrectionTextBox.IsEnabled = available;
                DcfSlaveDelayTextBox.IsEnabled = available && state != null && state.IsDelayWritable;
                if (state != null)
                {
                    DcfSlaveCorrectionTextBox.Text = state.CorrectionSeconds.ToString(CultureInfo.InvariantCulture);
                    DcfSlaveDelayTextBox.Text = state.DelayNanoseconds.ToString(CultureInfo.InvariantCulture);
                    uint position = Math.Min(59u, state.BitPosition);
                    DcfSlavePositionText.Text = string.Format(CultureInfo.InvariantCulture,
                        "Decoder position {0} / 59", state.BitPosition);
                    DcfSlavePositionProgress.Value = position;
                }
                else
                {
                    DcfSlavePositionText.Text = "Decoder position - / 59";
                    DcfSlavePositionProgress.Value = 0;
                }
            }
        }

        private void ApplyTodParserState(TodParserState state)
        {
            bool available = state != null && state.IsPresent;
            if (!available)
            {
                bool advertised = lastFpgaCapabilities != null &&
                    (lastFpgaCapabilities.CoreMask & (1u << 6)) != 0;
                TodParserDetailText.Text = state == null && advertised ?
                    "Parser query failed." :
                    "The ToD slave/parser core is not present in this board profile.";
                SetTodInputsEnabled(false, null);
                return;
            }

            TodParserEnabledCheckBox.IsChecked = state.IsEnabled;
            SelectComboTag(TodParserProtocolCombo, state.Protocol);
            SelectComboTag(TodParserGnssCombo, state.Gnss);
            SelectComboTag(TodParserBaudCombo, state.Baud);
            TodParserCorrectionTextBox.Text = state.CorrectionSeconds.ToString(
                CultureInfo.InvariantCulture);
            TodParserInvertCheckBox.IsChecked = state.IsInverted;
            TodParserTimeLsCheckBox.IsChecked = (state.MessageDisableMask & 0x01u) == 0;
            TodParserTimeUtcCheckBox.IsChecked = (state.MessageDisableMask & 0x02u) == 0;
            TodParserStatusCheckBox.IsChecked = (state.MessageDisableMask & 0x04u) == 0;
            TodParserMonitorCheckBox.IsChecked = (state.MessageDisableMask & 0x08u) == 0;
            TodParserSatelliteCheckBox.IsChecked = (state.MessageDisableMask & 0x10u) == 0;
            TodParserEsipCrwCheckBox.IsChecked = (state.MessageDisableMask & 0x20u) == 0;
            TodParserEsipCryCheckBox.IsChecked = (state.MessageDisableMask & 0x40u) == 0;
            TodParserEsipCrjCheckBox.IsChecked = (state.MessageDisableMask & 0x80u) == 0;
            string errors = state.HasParseError || state.HasChecksumError || state.HasUartError ?
                string.Format(CultureInfo.InvariantCulture,
                    "Errors:{0}{1}{2} | ",
                    state.HasParseError ? " parse" : string.Empty,
                    state.HasChecksumError ? " checksum" : string.Empty,
                    state.HasUartError ? " UART" : string.Empty) : string.Empty;
            string statusDetail = FpgaVersionAtLeast(state.Version, 1u, 2u) ?
                string.Format(CultureInfo.InvariantCulture,
                    "status 0x{0:X8}", state.Status) :
                "status requires v1.2+";
            string telemetry = state.HasUtcTelemetry || state.HasGnssTelemetry ?
                string.Format(CultureInfo.InvariantCulture,
                    "UTC 0x{0:X8}, leap {1}, GNSS 0x{2:X8}, satellites {3}",
                    state.UtcStatus, state.TimeToLeapSeconds,
                    state.GnssStatus, state.Satellites) :
                "optional UTC/leap/GNSS counters locked";
            TodParserDetailText.Text = string.Format(CultureInfo.InvariantCulture,
                "{0}Version {1} | {2:N0} baud | {3} | {4}{5}",
                errors, FormatFpgaVersion(state.Version), state.Baud,
                statusDetail, telemetry,
                HasFpgaFeature(1u << 2) ? string.Empty :
                    " | read-only capability");
            bool writable = HasFpgaFeature(1u << 2);
            SetTodInputsEnabled(writable, state);
            TodParserClearButton.IsEnabled = writable &&
                FpgaVersionAtLeast(state.Version, 1u, 2u) &&
                (state.HasParseError || state.HasChecksumError ||
                 state.HasUartError);
        }

        private void SetTodInputsEnabled(bool enabled, TodParserState state)
        {
            uint version = state == null ? 0u : state.Version;
            TodParserEnabledCheckBox.IsEnabled = enabled;
            SetComboItemEnabled(TodParserProtocolCombo, 0u, enabled);
            SetComboItemEnabled(TodParserProtocolCombo, 1u, enabled &&
                FpgaVersionAtLeast(version, 1u, 6u));
            SetComboItemEnabled(TodParserProtocolCombo, 2u, enabled &&
                FpgaVersionAtLeast(version, 1u, 9u));
            SetComboItemEnabled(TodParserProtocolCombo, 3u, enabled &&
                FpgaVersionAtLeast(version, 2u, 1u));
            SetComboItemEnabled(TodParserProtocolCombo, 4u, enabled &&
                FpgaVersionAtLeast(version, 2u, 3u));
            TodParserProtocolCombo.IsEnabled = enabled &&
                FpgaVersionAtLeast(version, 1u, 6u);
            TodParserGnssCombo.IsEnabled = enabled &&
                FpgaVersionAtLeast(version, 1u, 5u);
            TodParserBaudCombo.IsEnabled = enabled;
            TodParserCorrectionTextBox.IsEnabled = enabled;
            TodParserInvertCheckBox.IsEnabled = enabled &&
                FpgaVersionAtLeast(version, 1u, 3u);
            UpdateTodMessageControls(enabled, state);
            TodParserClearButton.IsEnabled = false;
            TodParserApplyButton.IsEnabled = enabled;
        }

        private void TodParserProtocolCombo_SelectionChanged(
            object sender, SelectionChangedEventArgs e)
        {
            UpdateTodMessageControls(HasFpgaFeature(1u << 2),
                lastTodParser);
        }

        private void UpdateTodMessageControls(bool writable,
                                              TodParserState state)
        {
            if (TodParserProtocolCombo == null ||
                TodParserTimeLsCheckBox == null ||
                TodParserTimeUtcCheckBox == null ||
                TodParserStatusCheckBox == null ||
                TodParserMonitorCheckBox == null ||
                TodParserSatelliteCheckBox == null ||
                TodParserEsipCrwCheckBox == null ||
                TodParserEsipCryCheckBox == null ||
                TodParserEsipCrjCheckBox == null)
                return;
            ComboBoxItem selected = TodParserProtocolCombo.SelectedItem as ComboBoxItem;
            uint protocol;
            if (state == null || selected == null || !uint.TryParse(
                    Convert.ToString(selected.Tag, CultureInfo.InvariantCulture),
                    NumberStyles.Integer, CultureInfo.InvariantCulture,
                    out protocol))
            {
                SetTodMessageCheckBox(TodParserTimeLsCheckBox, false);
                SetTodMessageCheckBox(TodParserTimeUtcCheckBox, false);
                SetTodMessageCheckBox(TodParserStatusCheckBox, false);
                SetTodMessageCheckBox(TodParserMonitorCheckBox, false);
                SetTodMessageCheckBox(TodParserSatelliteCheckBox, false);
                SetTodMessageCheckBox(TodParserEsipCrwCheckBox, false);
                SetTodMessageCheckBox(TodParserEsipCryCheckBox, false);
                SetTodMessageCheckBox(TodParserEsipCrjCheckBox, false);
                return;
            }

            uint supported = TodSupportedMessageMask(state.Version, protocol);
            SetTodMessageCheckBox(TodParserTimeLsCheckBox,
                (supported & 0x01u) != 0, writable);
            SetTodMessageCheckBox(TodParserTimeUtcCheckBox,
                (supported & 0x02u) != 0, writable);
            SetTodMessageCheckBox(TodParserStatusCheckBox,
                (supported & 0x04u) != 0, writable);
            SetTodMessageCheckBox(TodParserMonitorCheckBox,
                (supported & 0x08u) != 0, writable);
            SetTodMessageCheckBox(TodParserSatelliteCheckBox,
                (supported & 0x10u) != 0, writable);
            SetTodMessageCheckBox(TodParserEsipCrwCheckBox,
                (supported & 0x20u) != 0, writable);
            SetTodMessageCheckBox(TodParserEsipCryCheckBox,
                (supported & 0x40u) != 0, writable);
            SetTodMessageCheckBox(TodParserEsipCrjCheckBox,
                (supported & 0x80u) != 0, writable);
        }

        private static void SetTodMessageCheckBox(CheckBox checkBox,
                                                   bool supported,
                                                   bool writable = false)
        {
            checkBox.IsEnabled = supported && writable;
            if (!supported)
                checkBox.IsChecked = false;
        }

        private void ApplyClockTelemetry(ClockTelemetryState state)
        {
            if (state == null || (state.Flags & 1u) == 0)
            {
                FpgaClockTelemetryText.Text = state == null ?
                    "Clock telemetry query failed." :
                    "Advanced clock telemetry is not available for this board profile or clock-core version.";
                return;
            }
            List<string> lines = new List<string>();
            string statusDetail = FpgaVersionAtLeast(state.Version, 1u, 2u) ?
                string.Format(CultureInfo.InvariantCulture,
                    "status 0x{0:X8}", state.Status) :
                "status requires v1.2+";
            lines.Add(string.Format(CultureInfo.InvariantCulture,
                "Clock {0} | control 0x{1:X8} | {2} | select 0x{3:X8}",
                FormatFpgaVersion(state.Version), state.Control, statusDetail,
                state.Select));
            lines.Add(string.Format(CultureInfo.InvariantCulture,
                "In-sync threshold {0} ns", state.InSyncThreshold));
            if ((state.Flags & 8u) != 0)
            {
                lines.Add(string.Format(CultureInfo.InvariantCulture,
                    "Servo offset P/I {0}/{1} | drift P/I {2}/{3}",
                    state.ServoOffsetP, state.ServoOffsetI,
                    state.ServoDriftP, state.ServoDriftI));
            }
            if ((state.Flags & 16u) != 0)
            {
                string fractions = (state.Flags & 32u) != 0 ?
                    string.Format(CultureInfo.InvariantCulture,
                        " | fractions 0x{0:X4}/0x{1:X4}",
                        state.StatusOffsetFraction, state.StatusDriftFraction) :
                    string.Empty;
                lines.Add(string.Format(CultureInfo.InvariantCulture,
                    "Last offset {0:+#;-#;0} ns | drift {1:+#;-#;0} ppb{2}",
                    state.StatusOffsetNanoseconds, state.StatusDriftPpb,
                    fractions));
            }
            if ((state.Flags & (8u | 16u)) == 0)
            {
                lines.Add("Optional servo and correction-log registers are not " +
                    "advertised by this bitstream and were not read.");
            }
            FpgaClockTelemetryText.Text = string.Join("\n", lines);
        }

        private async void ApplyPpsEngine_Click(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint core;
            if (!TryGetButtonTag(button, out core) || !EnsureFpgaConnected())
                return;
            try
            {
                bool master = core == 1;
                bool enabled = (master ? PpsMasterEnabledCheckBox :
                    PpsSlaveEnabledCheckBox).IsChecked == true;
                ComboBox polarity = master ? PpsMasterPolarityCombo :
                    PpsSlavePolarityCombo;
                bool activeHigh = SelectedComboTag(polarity, "PPS polarity") == 1;
                uint width = master ? ParseUnsigned(PpsMasterWidthTextBox.Text,
                    "PPS pulse width", 1, 999) :
                    lastPpsSlave == null ? 0 : lastPpsSlave.PulseWidthMilliseconds;
                PpsEngineState current = master ? lastPpsMaster : lastPpsSlave;
                if (current == null)
                    throw new InvalidOperationException(
                        "Refresh the FPGA core state before applying PPS settings.");
                long maximumDelay = FpgaVersionAtLeast(current.Version, 1u, 6u) ?
                    0x3fffffffL : 0xffffL;
                int delay = checked((int)ParseSigned(master ?
                    PpsMasterDelayTextBox.Text : PpsSlaveDelayTextBox.Text,
                    "PPS cable compensation", -maximumDelay, maximumDelay));
                button.IsEnabled = false;
                PpsEngineState state = await Task.Run(() => client.SetPpsEngine(
                    core, enabled, activeHigh, width, delay, false));
                if (master) lastPpsMaster = state; else lastPpsSlave = state;
                ApplyPpsState(state, core);
                Log(string.Format(CultureInfo.InvariantCulture,
                    "PPS {0} configuration applied and verified.",
                    master ? "master" : "slave"));
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to configure PPS engine", ex);
            }
            finally
            {
                ApplyPpsState(core == 1 ? lastPpsMaster : lastPpsSlave,
                    core);
            }
        }

        private async void ClearPpsError_Click(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint core;
            if (!TryGetButtonTag(button, out core) || !EnsureFpgaConnected())
                return;
            PpsEngineState current = core == 1 ? lastPpsMaster : lastPpsSlave;
            if (current == null)
                return;
            try
            {
                button.IsEnabled = false;
                PpsEngineState state = await Task.Run(() => client.SetPpsEngine(
                    core, current.IsEnabled, current.IsActiveHigh,
                    current.PulseWidthMilliseconds, current.CableDelayNanoseconds,
                    true));
                if (core == 1) lastPpsMaster = state; else lastPpsSlave = state;
                ApplyPpsState(state, core);
                Log(state.HasError ?
                    "PPS clear requested; the hardware condition remains active." :
                    "PPS clear requested; status was refreshed.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to clear PPS errors", ex);
            }
            finally
            {
                ApplyPpsState(core == 1 ? lastPpsMaster : lastPpsSlave,
                    core);
            }
        }

        private async void ApplyTimecode_Click(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint target;
            if (!TryGetButtonTag(button, out target) || !EnsureFpgaConnected())
                return;
            try
            {
                uint format = target <= 2 ? 1u : 2u;
                uint role = target == 1 || target == 3 ? 1u : 2u;
                bool enabled;
                uint mode = 0;
                uint code = 0;
                int correction;
                int delay = 0;
                uint controlBits = 0;
                bool amplitudeModulation = false;
                uint manualYear = 0u;
                if (target == 1)
                {
                    enabled = IrigMasterEnabledCheckBox.IsChecked == true;
                    mode = (uint)SelectedComboTag(IrigMasterModeCombo, "IRIG format");
                    code = ParseUnsigned(IrigMasterCodeTextBox.Text, "IRIG code", 0, 7);
                    correction = checked((int)ParseSigned(IrigMasterCorrectionTextBox.Text,
                        "IRIG UTC correction", -0x7fffffff, 0x7fffffff));
                    controlBits = ParseHex(IrigMasterControlBitsTextBox.Text,
                        "IRIG control bits", 0, 0x07ffffff);
                    amplitudeModulation = lastIrigMaster != null &&
                        lastIrigMaster.IsAmplitudeModulationWritable &&
                        IrigMasterAmplitudeCheckBox.IsChecked == true;
                }
                else if (target == 2)
                {
                    enabled = IrigSlaveEnabledCheckBox.IsChecked == true;
                    mode = (uint)SelectedComboTag(IrigSlaveModeCombo, "IRIG format");
                    correction = checked((int)ParseSigned(IrigSlaveCorrectionTextBox.Text,
                        "IRIG UTC correction", -0x7fffffff, 0x7fffffff));
                    delay = checked((int)ParseSigned(IrigSlaveDelayTextBox.Text,
                        "IRIG cable delay", 0, 0xffff));
                    amplitudeModulation = lastIrigSlave != null &&
                        lastIrigSlave.IsAmplitudeModulationWritable &&
                        IrigSlaveAmplitudeCheckBox.IsChecked == true;
                    manualYear = lastIrigSlave != null &&
                        lastIrigSlave.IsManualYearWritable ? ParseUnsigned(
                            IrigSlaveYearTextBox.Text, "IRIG manual year",
                            0u, 2069u) : 0u;
                    if (manualYear != 0u && manualYear < 1970u)
                        throw new InvalidOperationException(
                            "IRIG manual year must be 0 (keep current) or " +
                            "between 1970 and 2069.");
                }
                else if (target == 3)
                {
                    enabled = DcfMasterEnabledCheckBox.IsChecked == true;
                    correction = checked((int)ParseSigned(DcfMasterCorrectionTextBox.Text,
                        "DCF UTC correction", -0x7fffffff, 0x7fffffff));
                }
                else
                {
                    enabled = DcfSlaveEnabledCheckBox.IsChecked == true;
                    correction = checked((int)ParseSigned(DcfSlaveCorrectionTextBox.Text,
                        "DCF UTC correction", -0x7fffffff, 0x7fffffff));
                    delay = checked((int)ParseSigned(DcfSlaveDelayTextBox.Text,
                        "DCF air delay", 0, 0x3fffffff));
                }

                button.IsEnabled = false;
                TimecodeEngineState state = await Task.Run(() =>
                    client.SetTimecodeEngine(format, role, enabled, mode, code,
                        correction, delay, controlBits, false,
                        amplitudeModulation, manualYear));
                StoreTimecodeState(target, state);
                ApplyTimecodeState(state, target);
                Log((format == 1 ? "IRIG" : "DCF") + " " +
                    (role == 1 ? "output" : "input") +
                    " configuration applied and verified.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to configure timecode engine", ex);
            }
            finally
            {
                ApplyTimecodeState(GetTimecodeState(target), target);
            }
        }

        private async void ClearTimecodeError_Click(object sender,
                                                     RoutedEventArgs e)
        {
            Button button = sender as Button;
            uint target;
            if (!TryGetButtonTag(button, out target) || !EnsureFpgaConnected())
                return;
            TimecodeEngineState current = GetTimecodeState(target);
            if (current == null)
                return;
            try
            {
                button.IsEnabled = false;
                TimecodeEngineState state = await Task.Run(() =>
                    client.SetTimecodeEngine(current.Format, current.Role,
                        current.IsEnabled, current.Mode, current.Code,
                        current.CorrectionSeconds, current.DelayNanoseconds,
                        current.ControlBits, true,
                        current.AmplitudeModulation != 0u,
                        current.ManualYear));
                StoreTimecodeState(target, state);
                ApplyTimecodeState(state, target);
                Log(state.HasError ?
                    "Timecode clear requested; the hardware condition remains active." :
                    "Timecode clear requested; status was refreshed.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to clear timecode error", ex);
            }
            finally
            {
                ApplyTimecodeState(GetTimecodeState(target), target);
            }
        }

        private async void ApplyTodParser_Click(object sender, RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected())
                return;
            try
            {
                uint protocol = (uint)SelectedComboTag(TodParserProtocolCombo,
                    "ToD protocol");
                if (lastTodParser == null ||
                    !TodProtocolSupported(lastTodParser.Version, protocol))
                    throw new InvalidOperationException(
                        "The selected ToD protocol is not available in this core revision.");
                uint supportedMessageMask = TodSupportedMessageMask(
                    lastTodParser.Version, protocol);
                uint disableMask = 0u;
                if ((supportedMessageMask & 0x01u) != 0 && TodParserTimeLsCheckBox.IsChecked != true) disableMask |= 0x01u;
                if ((supportedMessageMask & 0x02u) != 0 && TodParserTimeUtcCheckBox.IsChecked != true) disableMask |= 0x02u;
                if ((supportedMessageMask & 0x04u) != 0 && TodParserStatusCheckBox.IsChecked != true) disableMask |= 0x04u;
                if ((supportedMessageMask & 0x08u) != 0 && TodParserMonitorCheckBox.IsChecked != true) disableMask |= 0x08u;
                if ((supportedMessageMask & 0x10u) != 0 && TodParserSatelliteCheckBox.IsChecked != true) disableMask |= 0x10u;
                if ((supportedMessageMask & 0x20u) != 0 && TodParserEsipCrwCheckBox.IsChecked != true) disableMask |= 0x20u;
                if ((supportedMessageMask & 0x40u) != 0 && TodParserEsipCryCheckBox.IsChecked != true) disableMask |= 0x40u;
                if ((supportedMessageMask & 0x80u) != 0 && TodParserEsipCrjCheckBox.IsChecked != true) disableMask |= 0x80u;
                uint gnss = (uint)SelectedComboTag(TodParserGnssCombo,
                    "GNSS system");
                uint baud = (uint)SelectedComboTag(TodParserBaudCombo,
                    "ToD UART baud");
                int correction = checked((int)ParseSigned(
                    TodParserCorrectionTextBox.Text, "ToD correction",
                    -0x7fffffff, 0x7fffffff));
                TodParserApplyButton.IsEnabled = false;
                TodParserState state = await Task.Run(() => client.SetTodParser(
                    TodParserEnabledCheckBox.IsChecked == true, protocol, gnss,
                    baud, TodParserInvertCheckBox.IsChecked == true,
                    correction, disableMask, false));
                lastTodParser = state;
                ApplyTodParserState(state);
                Log("ToD input parser configuration applied and verified.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to configure ToD parser", ex);
            }
            finally
            {
                ApplyTodParserState(lastTodParser);
            }
        }

        private async void ClearTodParserError_Click(object sender,
                                                       RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected() || lastTodParser == null)
                return;
            try
            {
                TodParserClearButton.IsEnabled = false;
                TodParserState current = lastTodParser;
                TodParserState state = await Task.Run(() => client.SetTodParser(
                    current.IsEnabled, current.Protocol, current.Gnss,
                    current.Baud, current.IsInverted, current.CorrectionSeconds,
                    current.MessageDisableMask, true));
                lastTodParser = state;
                ApplyTodParserState(state);
                Log(state.HasParseError || state.HasChecksumError ||
                    state.HasUartError ?
                    "ToD clear requested; the hardware condition remains active." :
                    "ToD clear requested; status was refreshed.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to clear ToD parser errors", ex);
            }
            finally
            {
                ApplyTodParserState(lastTodParser);
            }
        }

        private void SetFpgaUnavailable(string status, string detail)
        {
            lastFpgaCapabilities = null;
            lastFpgaImageInfo = null;
            lastPpsMaster = null;
            lastPpsSlave = null;
            lastIrigMaster = null;
            lastIrigSlave = null;
            lastDcfMaster = null;
            lastDcfSlave = null;
            lastTodParser = null;
            lastFpgaContract = null;
            lastCoreInventory = null;
            lastClockAdjustment = null;
            lastClockAdvanced = null;
            lastNmeaUtc = null;
            for (int index = 0; index < lastTimestampChannels.Length; ++index)
                lastTimestampChannels[index] = null;
            FpgaCoreStatusText.Text = status;
            FpgaCoreStatusText.Foreground = (Brush)FindResource("GoldBrush");
            FpgaCoreStatusDot.Fill = (Brush)FindResource("GoldBrush");
            ApplyPpsState(null, 1);
            ApplyPpsState(null, 2);
            ApplyTimecodeState(null, 1);
            ApplyTimecodeState(null, 2);
            ApplyTimecodeState(null, 3);
            ApplyTimecodeState(null, 4);
            ApplyTodParserState(null);
            ApplyFpgaGapStates();
            FpgaClockTelemetryText.Text = detail;
            FpgaDiscoveryText.Text = "No current FPGA capability snapshot. " +
                detail;
            if (lastSnapshot != null && DiagnosticsSummaryText != null)
                DiagnosticsSummaryText.Text = BuildDiagnostics(lastSnapshot);
        }

        private bool EnsureFpgaConnected()
        {
            if (!EnsureConnected())
                return false;
            if (lastSnapshot == null || lastSnapshot.AbiVersion < FpgaRequiredAbi)
            {
                MessageBox.Show(this,
                    "FPGA engine control requires Time Card driver 1.39 / ABI 12.",
                    "Driver update required", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return false;
            }
            return true;
        }

        private static bool TryGetButtonTag(Button button, out uint value)
        {
            value = 0;
            return button != null && uint.TryParse(Convert.ToString(button.Tag,
                CultureInfo.InvariantCulture), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out value);
        }

        private void SetCoreStatus(TextBlock block, string text,
                                   string brushResource)
        {
            block.Text = text;
            block.Foreground = (Brush)FindResource(brushResource);
        }

        private TimecodeEngineState GetTimecodeState(uint target)
        {
            switch (target)
            {
            case 1: return lastIrigMaster;
            case 2: return lastIrigSlave;
            case 3: return lastDcfMaster;
            case 4: return lastDcfSlave;
            default: return null;
            }
        }

        private void StoreTimecodeState(uint target, TimecodeEngineState state)
        {
            switch (target)
            {
            case 1: lastIrigMaster = state; break;
            case 2: lastIrigSlave = state; break;
            case 3: lastDcfMaster = state; break;
            case 4: lastDcfSlave = state; break;
            }
        }

        private void ShowFpgaError(string title, Exception error)
        {
            Log(title + ": " + error.Message);
            MessageBox.Show(this, error.Message, title, MessageBoxButton.OK,
                MessageBoxImage.Error);
        }

        private static string FormatFpgaVersion(uint value)
        {
            return string.Format(CultureInfo.InvariantCulture, "{0}.{1}.{2}",
                value >> 24, (value >> 16) & 0xff, value & 0xffff);
        }

        private static bool FpgaVersionAtLeast(uint value, uint major,
                                               uint minor)
        {
            uint actualMajor = value >> 24;
            uint actualMinor = (value >> 16) & 0xffu;
            return value != 0u && value != uint.MaxValue &&
                (actualMajor > major ||
                 (actualMajor == major && actualMinor >= minor));
        }

        private static bool TodProtocolSupported(uint version, uint protocol)
        {
            switch (protocol)
            {
            case 0u: return true;
            case 1u: return FpgaVersionAtLeast(version, 1u, 6u);
            case 2u: return FpgaVersionAtLeast(version, 1u, 9u);
            case 3u: return FpgaVersionAtLeast(version, 2u, 1u);
            case 4u: return FpgaVersionAtLeast(version, 2u, 3u);
            default: return false;
            }
        }

        private static uint TodSupportedMessageMask(uint version,
                                                    uint protocol)
        {
            switch (protocol)
            {
            case 0u:
                return 0x1bu |
                    (FpgaVersionAtLeast(version, 2u, 0u) ? 0x04u : 0u);
            case 1u:
                return FpgaVersionAtLeast(version, 1u, 7u) ? 0x1fu : 0x07u;
            case 2u:
                return 0x1fu;
            case 3u:
                return 0xffu;
            case 4u:
                return 0x7fu;
            default:
                return 0u;
            }
        }

        private static void SetComboItemEnabled(ComboBox combo, uint tag,
                                                bool enabled)
        {
            if (combo == null)
                return;
            foreach (object item in combo.Items)
            {
                ComboBoxItem comboItem = item as ComboBoxItem;
                uint itemTag;
                if (comboItem != null && uint.TryParse(Convert.ToString(
                        comboItem.Tag, CultureInfo.InvariantCulture),
                        NumberStyles.Integer, CultureInfo.InvariantCulture,
                        out itemTag) && itemTag == tag)
                {
                    comboItem.IsEnabled = enabled;
                    return;
                }
            }
        }
    }
}
