using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace TimeCardControlCenter
{
    public partial class MainWindow
    {
        private const uint ContractClockServo = 1u << 0;
        private const uint ContractClockAdvanced = 1u << 1;
        private const uint ContractTodTelemetry = 1u << 2;
        private const uint ContractUtcRead = 1u << 3;
        private const uint ContractUtcWrite = 1u << 4;
        private const uint ContractIrigMasterAm = 1u << 5;
        private const uint ContractIrigSlaveAm = 1u << 6;
        private const uint ContractIrigSlaveYear = 1u << 7;
        private const uint ContractNtpSource = 1u << 8;
        private const uint ContractArtTimestamp = 1u << 9;
        private const uint ContractSynceSource = 1u << 10;
        private const uint ContractDynamicSource = 1u << 11;

        private void ApplyFpgaGapStates()
        {
            ApplyFpgaContractState();
            ApplyCoreInventoryState();
            ApplyNmeaUtcState();
            ApplySelectedTimestampState();
            ApplyClockAdjustmentState();
            ApplyClockAdvancedState();
            bool completionAvailable = lastFpgaCapabilities != null &&
                lastFpgaCapabilities.AbiVersion >= 15u &&
                (lastFpgaCapabilities.FeatureFlags & (1u << 9)) != 0u;
            SignalEventGeneratorCombo.IsEnabled = completionAvailable;
            SignalEventReadButton.IsEnabled = completionAvailable;
            if (!completionAvailable)
                SignalEventsTextBox.Text = "Completion IRQ queues are not " +
                    "available in this driver or board layout.";

            SetComboItemEnabled(ClockSourceCombo, 7u,
                lastFpgaContract != null &&
                lastFpgaContract.Allows(ContractNtpSource));
            SetComboItemEnabled(ClockSourceCombo, 8u,
                lastFpgaContract != null &&
                lastFpgaContract.Allows(ContractSynceSource));
            SetComboItemEnabled(ClockSourceCombo, 253u,
                lastFpgaContract != null &&
                lastFpgaContract.Allows(ContractDynamicSource));
        }

        private void ApplyFpgaContractState()
        {
            bool available = lastFpgaCapabilities != null &&
                lastFpgaCapabilities.AbiVersion >= 15u &&
                (lastFpgaCapabilities.FeatureFlags & (1u << 10)) != 0u &&
                lastFpgaContract != null && lastFpgaContract.HasImage;
            CheckBox[] boxes = ContractCapabilityBoxes();
            foreach (CheckBox box in boxes)
                box.IsEnabled = available;
            ContractAcknowledgeCheckBox.IsEnabled = available;
            ContractActivateButton.IsEnabled = available;
            ContractClearButton.IsEnabled = available &&
                lastFpgaContract.IsActive;
            if (!available)
            {
                FpgaContractStatusText.Text = "LOCKED";
                FpgaContractStatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                FpgaContractIdentityText.Text = lastFpgaCapabilities == null ?
                    "Refresh to query the exact-image contract." :
                    "No trusted static image identity is available for this " +
                    "layout. Optional register banks remain locked.";
                foreach (CheckBox box in boxes)
                    box.IsChecked = false;
                return;
            }

            uint flags = lastFpgaContract.CapabilityFlags;
            SetContractChecks(flags);
            ContractAcknowledgeCheckBox.IsChecked = false;
            FpgaContractStatusText.Text = lastFpgaContract.IsActive ?
                "ACTIVE" : lastFpgaContract.IsExactMatch ? "VERIFIED / LOCKED" :
                "LOCKED";
            FpgaContractStatusText.Foreground = (Brush)FindResource(
                lastFpgaContract.IsActive ? "AccentBrush" : "GoldBrush");
            FpgaContractIdentityText.Text = string.Format(
                CultureInfo.InvariantCulture,
                "Image 0x{0:X8} | profile {1} | layout {2} | effective 0x{3:X3}. " +
                "Only checked features will be eligible for optional MMIO.",
                lastFpgaContract.RawImageVersion,
                lastFpgaContract.BoardProfile, lastFpgaContract.Layout,
                lastFpgaContract.EffectiveFlags);
        }

        private CheckBox[] ContractCapabilityBoxes()
        {
            return new[] { ContractClockServoCheckBox,
                ContractClockAdvancedCheckBox, ContractTodTelemetryCheckBox,
                ContractUtcReadCheckBox, ContractUtcWriteCheckBox,
                ContractIrigMasterAmCheckBox, ContractIrigSlaveAmCheckBox,
                ContractIrigYearCheckBox, ContractNtpSourceCheckBox,
                ContractArtTimestampCheckBox, ContractSynceSourceCheckBox,
                ContractDynamicSourceCheckBox };
        }

        private void SetContractChecks(uint flags)
        {
            ContractClockServoCheckBox.IsChecked =
                (flags & ContractClockServo) != 0u;
            ContractClockAdvancedCheckBox.IsChecked =
                (flags & ContractClockAdvanced) != 0u;
            ContractTodTelemetryCheckBox.IsChecked =
                (flags & ContractTodTelemetry) != 0u;
            ContractUtcReadCheckBox.IsChecked =
                (flags & ContractUtcRead) != 0u;
            ContractUtcWriteCheckBox.IsChecked =
                (flags & ContractUtcWrite) != 0u;
            ContractIrigMasterAmCheckBox.IsChecked =
                (flags & ContractIrigMasterAm) != 0u;
            ContractIrigSlaveAmCheckBox.IsChecked =
                (flags & ContractIrigSlaveAm) != 0u;
            ContractIrigYearCheckBox.IsChecked =
                (flags & ContractIrigSlaveYear) != 0u;
            ContractNtpSourceCheckBox.IsChecked =
                (flags & ContractNtpSource) != 0u;
            ContractArtTimestampCheckBox.IsChecked =
                (flags & ContractArtTimestamp) != 0u;
            ContractSynceSourceCheckBox.IsChecked =
                (flags & ContractSynceSource) != 0u;
            ContractDynamicSourceCheckBox.IsChecked =
                (flags & ContractDynamicSource) != 0u;
        }

        private uint SelectedContractFlags()
        {
            uint flags = 0u;
            if (ContractClockServoCheckBox.IsChecked == true)
                flags |= ContractClockServo;
            if (ContractClockAdvancedCheckBox.IsChecked == true)
                flags |= ContractClockAdvanced;
            if (ContractTodTelemetryCheckBox.IsChecked == true)
                flags |= ContractTodTelemetry;
            if (ContractUtcReadCheckBox.IsChecked == true)
                flags |= ContractUtcRead;
            if (ContractUtcWriteCheckBox.IsChecked == true)
                flags |= ContractUtcWrite | ContractUtcRead;
            if (ContractIrigMasterAmCheckBox.IsChecked == true)
                flags |= ContractIrigMasterAm;
            if (ContractIrigSlaveAmCheckBox.IsChecked == true)
                flags |= ContractIrigSlaveAm;
            if (ContractIrigYearCheckBox.IsChecked == true)
                flags |= ContractIrigSlaveYear;
            if (ContractNtpSourceCheckBox.IsChecked == true)
                flags |= ContractNtpSource;
            if (ContractArtTimestampCheckBox.IsChecked == true)
                flags |= ContractArtTimestamp;
            if (ContractSynceSourceCheckBox.IsChecked == true)
                flags |= ContractSynceSource;
            if (ContractDynamicSourceCheckBox.IsChecked == true)
                flags |= ContractDynamicSource;
            return flags;
        }

        private async void ActivateFpgaContract_Click(object sender,
                                                       RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected() || lastFpgaContract == null)
                return;
            if (ContractAcknowledgeCheckBox.IsChecked != true)
            {
                MessageBox.Show(this,
                    "Confirm that the selected capabilities were verified " +
                    "against this exact FPGA image before activation.",
                    "Verification required", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }
            uint flags = SelectedContractFlags();
            if (flags == 0u)
            {
                MessageBox.Show(this, "Select at least one verified capability.",
                    "No capabilities selected", MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return;
            }
            if (MessageBox.Show(this,
                "Activate optional register access for exact image 0x" +
                lastFpgaContract.RawImageVersion.ToString("X8",
                    CultureInfo.InvariantCulture) + "?\n\nAn incorrect synthesis " +
                "declaration can cause FPGA decode errors. The driver will " +
                "revalidate the image on every optional access.",
                "Activate exact image contract", MessageBoxButton.YesNo,
                MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;
            try
            {
                ContractActivateButton.IsEnabled = false;
                FpgaImageContract contract = await Task.Run(() =>
                    client.SetFpgaImageContract(
                        lastFpgaContract.RawImageVersion, flags,
                        lastFpgaContract.BoardProfile,
                        lastFpgaContract.Layout));
                lastFpgaContract = contract;
                Log(string.Format(CultureInfo.InvariantCulture,
                    "Exact FPGA image contract activated: image 0x{0:X8}, " +
                    "capabilities 0x{1:X3}.", contract.RawImageVersion,
                    contract.EffectiveFlags));
                await RefreshFpgaCoresAsync();
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to activate image contract", ex);
                ApplyFpgaGapStates();
            }
        }

        private async void ClearFpgaContract_Click(object sender,
                                                    RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected() || lastFpgaContract == null)
                return;
            try
            {
                ContractClearButton.IsEnabled = false;
                lastFpgaContract = await Task.Run(() =>
                    client.SetFpgaImageContract(
                        lastFpgaContract.RawImageVersion, 0u,
                        lastFpgaContract.BoardProfile,
                        lastFpgaContract.Layout));
                Log("Optional FPGA register banks locked for this session.");
                await RefreshFpgaCoresAsync();
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to clear image contract", ex);
                ApplyFpgaGapStates();
            }
        }

        private void ApplyCoreInventoryState()
        {
            if (lastCoreInventory == null)
            {
                CoreInventoryStatusText.Text = "UNAVAILABLE";
                CoreInventoryStatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                CoreInventoryText.Text = "ABI 15 static inventory is not " +
                    "available. No fallback address scan will be attempted.";
                return;
            }
            CoreInventoryStatusText.Text = lastCoreInventory.Cores.Count.
                ToString(CultureInfo.InvariantCulture) + " CORES";
            CoreInventoryStatusText.Foreground =
                (Brush)FindResource("AccentBrush");
            List<string> lines = new List<string>();
            foreach (CoreDescriptor core in lastCoreInventory.Cores)
            {
                string interrupt = core.HasInterrupt ?
                    core.InterruptMessage.ToString(CultureInfo.InvariantCulture) :
                    "-";
                string version = core.Version == 0u ||
                    core.Version == uint.MaxValue ? "-" :
                    FormatFpgaVersion(core.Version);
                lines.Add(string.Format(CultureInfo.InvariantCulture,
                    "{0,-20} #{1}  BAR+0x{2:X8}  span 0x{3:X}  IRQ {4}  v{5}",
                    core.Name, core.Instance, core.RegisterOffset,
                    core.RegisterSpan, interrupt, version));
            }
            CoreInventoryText.Text = string.Join("\n", lines);
        }

        private void ApplyNmeaUtcState()
        {
            bool readContract = lastFpgaContract != null &&
                lastFpgaContract.Allows(ContractUtcRead);
            bool writeContract = lastFpgaContract != null &&
                lastFpgaContract.Allows(ContractUtcRead | ContractUtcWrite);
            if (lastNmeaUtc == null)
            {
                NmeaUtcStatusText.Text = readContract ? "QUERY FAILED" : "LOCKED";
                NmeaUtcStatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                NmeaUtcDetailText.Text = readContract ?
                    "The exact-image contract permits UTC reads, but the " +
                    "handshake did not return a value." :
                    "Optional UTC registers were not accessed because the " +
                    "exact-image read capability is not active.";
                NmeaUtcOffsetTextBox.IsEnabled = false;
                NmeaUtcValidCheckBox.IsEnabled = false;
                NmeaUtcLeap61CheckBox.IsEnabled = false;
                NmeaUtcLeap59CheckBox.IsEnabled = false;
                NmeaUtcApplyButton.IsEnabled = false;
                return;
            }
            NmeaUtcStatusText.Text = lastNmeaUtc.CanWrite ? "READ / WRITE" :
                "READ ONLY";
            NmeaUtcStatusText.Foreground = (Brush)FindResource(
                lastNmeaUtc.CanWrite ? "AccentBrush" : "CyanBrush");
            NmeaUtcOffsetTextBox.Text = lastNmeaUtc.UtcOffsetSeconds.ToString(
                CultureInfo.InvariantCulture);
            NmeaUtcValidCheckBox.IsChecked = lastNmeaUtc.IsOffsetValid;
            NmeaUtcLeap61CheckBox.IsChecked = lastNmeaUtc.Leap61;
            NmeaUtcLeap59CheckBox.IsChecked = lastNmeaUtc.Leap59;
            bool writable = writeContract && lastNmeaUtc.CanWrite;
            NmeaUtcOffsetTextBox.IsEnabled = writable;
            NmeaUtcValidCheckBox.IsEnabled = writable;
            NmeaUtcLeap61CheckBox.IsEnabled = writable;
            NmeaUtcLeap59CheckBox.IsEnabled = writable;
            NmeaUtcApplyButton.IsEnabled = writable;
            NmeaUtcDetailText.Text = string.Format(CultureInfo.InvariantCulture,
                "ToD Master {0} | raw UTC 0x{1:X8} | handshake 0x{2:X8}.",
                FormatFpgaVersion(lastNmeaUtc.Version),
                lastNmeaUtc.RawUtcInfo, lastNmeaUtc.HandshakeControl);
        }

        private async void ApplyNmeaUtc_Click(object sender, RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected() || lastNmeaUtc == null ||
                !lastNmeaUtc.CanWrite)
                return;
            try
            {
                uint offset = ParseUnsigned(NmeaUtcOffsetTextBox.Text,
                    "UTC offset", 0u, 0xffffu);
                if (NmeaUtcLeap61CheckBox.IsChecked == true &&
                    NmeaUtcLeap59CheckBox.IsChecked == true)
                    throw new InvalidOperationException(
                        "Positive and negative leap indicators cannot both be set.");
                NmeaUtcApplyButton.IsEnabled = false;
                lastNmeaUtc = await Task.Run(() => client.SetNmeaUtc(offset,
                    NmeaUtcLeap61CheckBox.IsChecked == true,
                    NmeaUtcLeap59CheckBox.IsChecked == true,
                    NmeaUtcValidCheckBox.IsChecked == true));
                ApplyNmeaUtcState();
                Log("ToD Master UTC metadata applied and verified.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to configure UTC metadata", ex);
                ApplyNmeaUtcState();
            }
        }

        private void TimestampChannelCombo_SelectionChanged(object sender,
                                                             SelectionChangedEventArgs e)
        {
            if (TimestampStatusText != null &&
                TimestampEnabledCheckBox != null &&
                TimestampApplyButton != null &&
                TimestampEventsTextBox != null)
                ApplySelectedTimestampState();
        }

        private uint SelectedTimestampChannel()
        {
            return (uint)SelectedComboTag(TimestampChannelCombo,
                "timestamp channel");
        }

        private void ApplySelectedTimestampState()
        {
            if (TimestampChannelCombo == null)
                return;
            bool advertised = lastFpgaCapabilities != null &&
                lastFpgaCapabilities.AbiVersion >= 15u &&
                (lastFpgaCapabilities.FeatureFlags & (1u << 7)) != 0u;
            TimestampChannelCombo.IsEnabled = advertised;
            uint channel = 0u;
            try { channel = SelectedTimestampChannel(); }
            catch { channel = 0u; }
            TimestampChannelState state = channel < 6u ?
                lastTimestampChannels[channel] : null;
            if (state == null || !state.IsPresent)
            {
                TimestampStatusText.Text = advertised ? "QUERY FAILED" :
                    "UNAVAILABLE";
                TimestampStatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                TimestampEnabledCheckBox.IsEnabled = false;
                TimestampPolarityCombo.IsEnabled = false;
                TimestampCableDelayTextBox.IsEnabled = false;
                TimestampApplyButton.IsEnabled = false;
                TimestampClearButton.IsEnabled = false;
                TimestampReadButton.IsEnabled = false;
                TimestampDetailText.Text = advertised ?
                    "This channel did not return a compatible v1.3+ surface." :
                    "Timestamp capture is not advertised by this driver/profile.";
                FpgaTimestampText.Text = TimestampDetailText.Text;
                return;
            }
            TimestampStatusText.Text = state.IsEnabled ? "CAPTURING" : "IDLE";
            TimestampStatusText.Foreground = (Brush)FindResource(
                state.HasDropError || state.HasQueueOverflow ? "DangerBrush" :
                state.IsEnabled ? "AccentBrush" : "MutedBrush");
            TimestampEnabledCheckBox.IsChecked = state.IsEnabled;
            SelectComboTag(TimestampPolarityCombo, state.Polarity);
            TimestampCableDelayTextBox.Text = state.CableDelayNanoseconds.
                ToString(CultureInfo.InvariantCulture);
            TimestampEnabledCheckBox.IsEnabled = state.HasInterrupt;
            TimestampPolarityCombo.IsEnabled = true;
            TimestampCableDelayTextBox.IsEnabled = state.IsCableDelayWritable;
            TimestampApplyButton.IsEnabled = true;
            TimestampClearButton.IsEnabled = true;
            TimestampReadButton.IsEnabled = state.HasInterrupt;
            TimestampDetailText.Text = string.Format(
                CultureInfo.InvariantCulture,
                "v{0} | IRQ {1} mask {2} | queue {3}, dropped {4} | " +
                "events {5}, timestamps {6} | data width {7} bit{8}",
                FormatFpgaVersion(state.Version), state.Interrupt,
                state.InterruptMask, state.QueueDepth, state.DroppedEvents,
                state.EventCount, state.TimestampCount, state.DataWidth,
                state.IsCableDelayWritable ? string.Empty :
                    " | basic surface: cable/counter/data registers not accessed");
            FpgaTimestampText.Text = "Windows interrupt-backed EXTTS is " +
                "available on six profiled channels. " +
                TimestampDetailText.Text;
        }

        private async void ApplyTimestampChannel_Click(object sender,
                                                        RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected())
                return;
            uint channel = SelectedTimestampChannel();
            TimestampChannelState current = lastTimestampChannels[channel];
            if (current == null)
                return;
            try
            {
                uint delay = current.IsCableDelayWritable ? ParseUnsigned(
                    TimestampCableDelayTextBox.Text, "timestamp cable delay",
                    0u, 0x7fffffffu) : 0u;
                TimestampApplyButton.IsEnabled = false;
                lastTimestampChannels[channel] = await Task.Run(() =>
                    client.SetTimestampChannel(channel,
                        TimestampEnabledCheckBox.IsChecked == true,
                        (uint)SelectedComboTag(TimestampPolarityCombo,
                            "timestamp edge"), delay, false, false));
                ApplySelectedTimestampState();
                Log("Timestamp channel " + channel.ToString(
                    CultureInfo.InvariantCulture) +
                    " configuration applied and verified.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to configure timestamp channel", ex);
                ApplySelectedTimestampState();
            }
        }

        private async void ClearTimestampChannel_Click(object sender,
                                                        RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected())
                return;
            uint channel = SelectedTimestampChannel();
            TimestampChannelState current = lastTimestampChannels[channel];
            if (current == null)
                return;
            try
            {
                TimestampClearButton.IsEnabled = false;
                lastTimestampChannels[channel] = await Task.Run(() =>
                    client.SetTimestampChannel(channel, current.IsEnabled,
                        current.Polarity, current.CableDelayNanoseconds,
                        true, true));
                TimestampEventsTextBox.Text = "Driver queue cleared.";
                ApplySelectedTimestampState();
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to clear timestamp channel", ex);
                ApplySelectedTimestampState();
            }
        }

        private async void ReadTimestampEvents_Click(object sender,
                                                      RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected())
                return;
            uint channel = SelectedTimestampChannel();
            try
            {
                TimestampReadButton.IsEnabled = false;
                TimestampEventBatch batch = await Task.Run(() =>
                    client.ReadTimestampEvents(channel, 16u));
                List<string> lines = new List<string>();
                foreach (TimestampEvent item in batch.Events)
                {
                    string data = item.IsDataValid ? " data=" +
                        FormatTimestampData(item.Data, item.DataWidth) :
                        string.Empty;
                    lines.Add(string.Format(CultureInfo.InvariantCulture,
                        "{0}.{1:D9} UTC  ts#{2} event#{3} err=0x{4:X8}{5}",
                        item.Seconds, item.Nanoseconds, item.TimestampCount,
                        item.EventCount, item.Error, data));
                }
                if (lines.Count == 0)
                    lines.Add("Queue empty.");
                if (batch.DroppedEvents != 0u)
                    lines.Add("Dropped events: " + batch.DroppedEvents.ToString(
                        CultureInfo.InvariantCulture));
                TimestampEventsTextBox.Text = string.Join("\n", lines);
                lastTimestampChannels[channel] = await Task.Run(() =>
                    client.GetTimestampChannel(channel));
                ApplySelectedTimestampState();
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to read timestamp events", ex);
                ApplySelectedTimestampState();
            }
        }

        private static string FormatTimestampData(uint[] data, uint width)
        {
            if (data == null || data.Length == 0 || width == 0u)
                return "-";
            int words = (int)Math.Min((width + 31u) / 32u,
                (uint)data.Length);
            StringBuilder text = new StringBuilder("0x");
            for (int index = words - 1; index >= 0; --index)
                text.Append(data[index].ToString("X8",
                    CultureInfo.InvariantCulture));
            return text.ToString();
        }

        private async void ReadSignalEvents_Click(object sender,
                                                   RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected())
                return;
            try
            {
                uint generator = (uint)SelectedComboTag(
                    SignalEventGeneratorCombo, "signal generator");
                SignalEventReadButton.IsEnabled = false;
                SignalCompletionBatch batch = await Task.Run(() =>
                    client.ReadSignalCompletionEvents(generator, 16u));
                List<string> lines = new List<string>();
                foreach (SignalCompletionEvent item in batch.Events)
                {
                    lines.Add(string.Format(CultureInfo.InvariantCulture,
                        "seq {0}  interrupt {1} x100 ns  status 0x{2:X8}{3}{4}",
                        item.Sequence, item.SystemInterruptTime100ns,
                        item.Status, item.HasError ? " ERROR" : string.Empty,
                        item.HasTimeJump ? " TIME-JUMP" : string.Empty));
                }
                if (lines.Count == 0)
                    lines.Add("Queue empty.");
                if (batch.DroppedEvents != 0u)
                    lines.Add("Dropped events: " + batch.DroppedEvents.ToString(
                        CultureInfo.InvariantCulture));
                SignalEventsTextBox.Text = string.Join("\n", lines);
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to read generator completion events", ex);
            }
            finally
            {
                SignalEventReadButton.IsEnabled = lastFpgaCapabilities != null &&
                    (lastFpgaCapabilities.FeatureFlags & (1u << 9)) != 0u;
            }
        }

        private void ApplyClockAdjustmentState()
        {
            bool available = lastClockAdjustment != null &&
                lastClockAdjustment.IsPresent;
            ClockApplyOffsetCheckBox.IsEnabled = available;
            ClockApplyDriftCheckBox.IsEnabled = available;
            ClockApplyThresholdCheckBox.IsEnabled = available;
            ClockOffsetTextBox.IsEnabled = available;
            ClockOffsetWindowTextBox.IsEnabled = available;
            ClockDriftTextBox.IsEnabled = available;
            ClockDriftWindowTextBox.IsEnabled = available;
            ClockThresholdTextBox.IsEnabled = available;
            ClockAdjustmentApplyButton.IsEnabled = available;
            if (!available)
            {
                ClockAdjustmentDetailText.Text = "Smooth clock correction " +
                    "requires ABI 15 and a standard Time Card clock map.";
                return;
            }
            ClockOffsetTextBox.Text = lastClockAdjustment.OffsetNanoseconds.
                ToString(CultureInfo.InvariantCulture);
            ClockOffsetWindowTextBox.Text =
                lastClockAdjustment.OffsetIntervalNanoseconds.ToString(
                    CultureInfo.InvariantCulture);
            ClockDriftTextBox.Text = lastClockAdjustment.DriftPpb.ToString(
                "0.####", CultureInfo.InvariantCulture);
            ClockDriftWindowTextBox.Text =
                lastClockAdjustment.DriftIntervalNanoseconds.ToString(
                    CultureInfo.InvariantCulture);
            ClockThresholdTextBox.Text =
                lastClockAdjustment.InSyncThresholdNanoseconds.ToString(
                    CultureInfo.InvariantCulture);
            ClockAdjustmentDetailText.Text = string.Format(
                CultureInfo.InvariantCulture,
                "Clock {0} | source request 0x{1:X2} | {2} drift | last " +
                "applied mask 0x{3:X8}.",
                FormatFpgaVersion(lastClockAdjustment.Version),
                lastClockAdjustment.Select & 0xffu,
                lastClockAdjustment.HasFractionalDrift ? "Q16.16" :
                    "integer-only", lastClockAdjustment.AppliedFlags);
        }

        private async void ApplyClockAdjustment_Click(object sender,
                                                       RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected() || lastClockAdjustment == null)
                return;
            try
            {
                uint apply = 0u;
                if (ClockApplyOffsetCheckBox.IsChecked == true) apply |= 0x10000u;
                if (ClockApplyDriftCheckBox.IsChecked == true) apply |= 0x20000u;
                if (ClockApplyThresholdCheckBox.IsChecked == true) apply |= 0x40000u;
                if (apply == 0u)
                    throw new InvalidOperationException(
                        "Select at least one correction field to apply.");
                int offset = checked((int)ParseSigned(ClockOffsetTextBox.Text,
                    "clock offset", -1000000000L, 1000000000L));
                uint offsetWindow = ParseUnsigned(
                    ClockOffsetWindowTextBox.Text, "offset window", 1u,
                    uint.MaxValue);
                double drift;
                if (!double.TryParse(ClockDriftTextBox.Text,
                    NumberStyles.Float, CultureInfo.InvariantCulture,
                    out drift) || double.IsNaN(drift) ||
                    double.IsInfinity(drift) || Math.Abs(drift) > 1000000.0)
                    throw new FormatException(
                        "Drift must be between -1,000,000 and 1,000,000 ppb.");
                long driftQ16 = checked((long)Math.Round(drift * 65536.0,
                    MidpointRounding.AwayFromZero));
                if (!lastClockAdjustment.HasFractionalDrift &&
                    (driftQ16 & 0xffffL) != 0L)
                    throw new InvalidOperationException(
                        "This clock revision accepts integer drift only.");
                uint driftWindow = ParseUnsigned(
                    ClockDriftWindowTextBox.Text, "drift window", 1u,
                    uint.MaxValue);
                uint threshold = ParseUnsigned(ClockThresholdTextBox.Text,
                    "in-sync threshold", 0u, 1000000000u);
                ClockAdjustmentApplyButton.IsEnabled = false;
                lastClockAdjustment = await Task.Run(() =>
                    client.SetClockAdjustment(offset, offsetWindow, driftQ16,
                        driftWindow, threshold, apply));
                ApplyClockAdjustmentState();
                Log("Smooth clock correction applied and verified.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to adjust clock", ex);
                ApplyClockAdjustmentState();
            }
        }

        private void ApplyClockAdvancedState()
        {
            if (lastClockAdvanced == null)
            {
                ClockAdvancedStatusText.Text = lastFpgaContract != null &&
                    lastFpgaContract.IsActive ? "NOT DECLARED" : "LOCKED";
                ClockAdvancedStatusText.Foreground =
                    (Brush)FindResource("GoldBrush");
                SetClockAdvancedInputs(false);
                ClockAdvancedDetailText.Text =
                    "No optional clock register was accessed. Activate only " +
                    "the exact-image capabilities verified for this bitstream.";
                return;
            }
            ClockAdvancedStatusText.Text = lastClockAdvanced.IsHoldoverReady ?
                "HOLDOVER READY" : lastClockAdvanced.IsAgingReady ?
                "AGING READY" : "AVAILABLE";
            ClockAdvancedStatusText.Foreground = (Brush)FindResource(
                lastClockAdvanced.IsHoldoverReady ? "CyanBrush" :
                    "AccentBrush");
            SetClockAdvancedInputs(true);
            ClockOffsetLimiterTextBox.Text =
                lastClockAdvanced.OffsetRateLimiter.ToString("X8",
                    CultureInfo.InvariantCulture);
            ClockDriftLimiterTextBox.Text =
                lastClockAdvanced.DriftRateLimiterQ16.ToString("X8",
                    CultureInfo.InvariantCulture);
            ClockAgingTextBox.Text =
                lastClockAdvanced.AgingConfiguration.ToString("X8",
                    CultureInfo.InvariantCulture);
            ClockHoldoverTextBox.Text =
                lastClockAdvanced.HoldoverConfiguration.ToString("X8",
                    CultureInfo.InvariantCulture);
            ClockOffsetOutlierTextBox.Text =
                lastClockAdvanced.OffsetOutlierFilter.ToString("X8",
                    CultureInfo.InvariantCulture);
            ClockDriftOutlierTextBox.Text =
                lastClockAdvanced.DriftOutlierFilter.ToString("X8",
                    CultureInfo.InvariantCulture);
            ClockServoOffsetPTextBox.Text = lastClockAdvanced.ServoOffsetP.
                ToString(CultureInfo.InvariantCulture);
            ClockServoOffsetITextBox.Text = lastClockAdvanced.ServoOffsetI.
                ToString(CultureInfo.InvariantCulture);
            ClockServoDriftPTextBox.Text = lastClockAdvanced.ServoDriftP.
                ToString(CultureInfo.InvariantCulture);
            ClockServoDriftITextBox.Text = lastClockAdvanced.ServoDriftI.
                ToString(CultureInfo.InvariantCulture);
            ClockHoldoverEnabledCheckBox.IsChecked =
                (lastClockAdvanced.Control & (1u << 16)) != 0u;
            ClockAgingEnabledCheckBox.IsChecked =
                (lastClockAdvanced.Control & (1u << 18)) != 0u;
            ClockRevertEnabledCheckBox.IsChecked =
                (lastClockAdvanced.Control & (1u << 19)) != 0u;
            ClockAdvancedDetailText.Text = string.Format(
                CultureInfo.InvariantCulture,
                "Clock {0} | control 0x{1:X8} | status 0x{2:X8} | " +
                "holdover 0x{3:X8}.{4:X8} ({5} samples) | outliers {6}/{7} | " +
                "aging 0x{8:X8}{9:X8} ({10} samples).",
                FormatFpgaVersion(lastClockAdvanced.Version),
                lastClockAdvanced.Control, lastClockAdvanced.Status,
                lastClockAdvanced.StatusHoldover,
                lastClockAdvanced.StatusHoldoverFraction,
                lastClockAdvanced.StatusHoldoverSamples,
                lastClockAdvanced.StatusOffsetOutliers,
                lastClockAdvanced.StatusDriftOutliers,
                lastClockAdvanced.StatusAgingHigh,
                lastClockAdvanced.StatusAgingLow,
                lastClockAdvanced.StatusAgingSamples);
        }

        private void SetClockAdvancedInputs(bool available)
        {
            bool rate = available && lastClockAdvanced.HasRateLimiters;
            bool outlier = available && lastClockAdvanced.HasOutlierFilters;
            bool servo = available && lastClockAdvanced.HasServoFactors;
            bool holdover = available && lastClockAdvanced.HasHoldover;
            bool aging = available && lastClockAdvanced.HasAging;
            ClockAdvancedRateCheckBox.IsEnabled = rate;
            ClockOffsetLimiterTextBox.IsEnabled = rate;
            ClockDriftLimiterTextBox.IsEnabled = rate;
            ClockAdvancedOutlierCheckBox.IsEnabled = outlier;
            ClockOffsetOutlierTextBox.IsEnabled = outlier;
            ClockDriftOutlierTextBox.IsEnabled = outlier;
            ClockAdvancedServoCheckBox.IsEnabled = servo;
            ClockServoOffsetPTextBox.IsEnabled = servo;
            ClockServoOffsetITextBox.IsEnabled = servo;
            ClockServoDriftPTextBox.IsEnabled = servo;
            ClockServoDriftITextBox.IsEnabled = servo;
            ClockAdvancedHoldoverCheckBox.IsEnabled = holdover;
            ClockHoldoverTextBox.IsEnabled = holdover;
            ClockAdvancedAgingCheckBox.IsEnabled = aging;
            ClockAgingTextBox.IsEnabled = aging;
            ClockAdvancedControlCheckBox.IsEnabled = holdover || aging;
            ClockHoldoverEnabledCheckBox.IsEnabled = holdover;
            ClockAgingEnabledCheckBox.IsEnabled = aging;
            ClockRevertEnabledCheckBox.IsEnabled = available &&
                lastClockAdvanced.HasRevert;
            ClockAdvancedApplyButton.IsEnabled = available;
        }

        private async void ApplyClockAdvanced_Click(object sender,
                                                     RoutedEventArgs e)
        {
            if (!EnsureFpgaConnected() || lastClockAdvanced == null)
                return;
            try
            {
                uint apply = 0u;
                if (ClockAdvancedRateCheckBox.IsChecked == true) apply |= 1u;
                if (ClockAdvancedHoldoverCheckBox.IsChecked == true) apply |= 2u;
                if (ClockAdvancedOutlierCheckBox.IsChecked == true) apply |= 4u;
                if (ClockAdvancedServoCheckBox.IsChecked == true) apply |= 8u;
                if (ClockAdvancedAgingCheckBox.IsChecked == true) apply |= 16u;
                if (ClockAdvancedControlCheckBox.IsChecked == true) apply |= 32u;
                if (apply == 0u)
                    throw new InvalidOperationException(
                        "Select at least one advanced group to apply.");
                uint offsetLimiter = ParseHex(ClockOffsetLimiterTextBox.Text,
                    "offset rate limiter", 0u, uint.MaxValue);
                uint driftLimiter = ParseHex(ClockDriftLimiterTextBox.Text,
                    "drift rate limiter", 0u, uint.MaxValue);
                uint aging = ParseHex(ClockAgingTextBox.Text,
                    "aging configuration", 0u, uint.MaxValue);
                uint holdover = ParseHex(ClockHoldoverTextBox.Text,
                    "holdover configuration", 0u, uint.MaxValue);
                uint offsetOutlier = ParseHex(ClockOffsetOutlierTextBox.Text,
                    "offset outlier filter", 0u, uint.MaxValue);
                uint driftOutlier = ParseHex(ClockDriftOutlierTextBox.Text,
                    "drift outlier filter", 0u, uint.MaxValue);
                ValidateLimiter(offsetLimiter, true, "offset rate limiter");
                ValidateLimiter(driftLimiter, false, "drift rate limiter");
                ValidateLimiter(offsetOutlier, true, "offset outlier filter");
                ValidateLimiter(driftOutlier, true, "drift outlier filter");
                if ((apply & 16u) != 0u && (aging >> 24) == 0u)
                    throw new InvalidOperationException(
                        "Aging sampling interval must be at least one second.");
                if ((aging & 0x00fe0000u) != 0u ||
                    (holdover & 0x00fe0000u) != 0u)
                    throw new InvalidOperationException(
                        "Reserved aging/holdover bits 23:17 must stay zero.");
                uint control = lastClockAdvanced.Control &
                    ~((1u << 16) | (1u << 17) | (1u << 18) | (1u << 19));
                if (ClockHoldoverEnabledCheckBox.IsChecked == true)
                    control |= 1u << 16;
                if (ClockAgingEnabledCheckBox.IsChecked == true)
                    control |= 1u << 18;
                if (ClockRevertEnabledCheckBox.IsChecked == true)
                    control |= 1u << 19;
                if ((control & (1u << 16)) != 0u &&
                    (holdover & 0x1ffffu) == 0u)
                    throw new InvalidOperationException(
                        "Holdover requires a nonzero sample count.");
                if ((control & (1u << 18)) != 0u &&
                    ((aging >> 24) == 0u || (aging & 0x1ffffu) == 0u))
                    throw new InvalidOperationException(
                        "Aging requires a sampling interval and sample count.");
                uint offsetP = ParseUnsigned(ClockServoOffsetPTextBox.Text,
                    "servo offset P", 0u, 0xffffu);
                uint offsetI = ParseUnsigned(ClockServoOffsetITextBox.Text,
                    "servo offset I", 0u, 0xffffu);
                uint driftP = ParseUnsigned(ClockServoDriftPTextBox.Text,
                    "servo drift P", 0u, 0xffffu);
                uint driftI = ParseUnsigned(ClockServoDriftITextBox.Text,
                    "servo drift I", 0u, 0xffffu);
                ClockAdvancedApplyButton.IsEnabled = false;
                lastClockAdvanced = await Task.Run(() =>
                    client.SetClockAdvanced(control, apply, offsetLimiter,
                        driftLimiter, aging, holdover, offsetOutlier,
                        driftOutlier, offsetP, offsetI, driftP, driftI));
                ApplyClockAdvancedState();
                Log("Selected advanced clock groups applied and verified.");
            }
            catch (Exception ex)
            {
                ShowFpgaError("Unable to configure advanced clock", ex);
                ApplyClockAdvancedState();
            }
        }

        private static void ValidateLimiter(uint value, bool oneSecondBound,
                                            string name)
        {
            uint magnitude = value & 0x7fffffffu;
            if ((value & 0x80000000u) != 0u && magnitude == 0u)
                throw new InvalidOperationException(
                    name + " cannot be enabled with a zero limit.");
            if (oneSecondBound && magnitude > 1000000000u)
                throw new InvalidOperationException(
                    name + " cannot exceed 1,000,000,000 ns.");
        }
    }
}
