using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;

namespace TimeCardControlCenter
{
    public partial class MainWindow
    {
        private readonly SemaphoreSlim deviceLifecycleGate =
            new SemaphoreSlim(1, 1);
        private readonly List<TimeCardDeviceDescriptor> deviceInventory =
            new List<TimeCardDeviceDescriptor>();
        private TimeCardDeviceDescriptor selectedDevice;
        private TimeCardDeviceDescriptor activeDevice;
        private DateTime lastDeviceInventoryUtc = DateTime.MinValue;
        private int deviceSessionGeneration;
        private bool cardInventoryRefreshing;
        private bool updatingCardSelector;
        private bool deviceSwitching;
        private bool criticalConfigurationWrite;
        private bool deviceSelectionClosed;
        private Task uartMonitorTask;

        private async Task ConnectSelectedDeviceAsync()
        {
            if (connecting || deviceSwitching || client != null ||
                deviceSelectionClosed || (productSettings != null &&
                productSettings.DemoMode))
                return;

            connecting = true;
            lastConnectionAttemptUtc = DateTime.UtcNow;
            SetConnectionState(false, "Connecting...", false);
            try
            {
                if (DateTime.UtcNow - lastDeviceInventoryUtc >
                    TimeSpan.FromSeconds(3))
                    await RefreshCardInventoryAsync(false);

                TimeCardDeviceDescriptor target = selectedDevice;
                if (target == null)
                {
                    bool hasPreference = HasPersistedDevicePreference();
                    SetConnectionState(false, hasPreference ?
                        "Selected card unavailable" : "No Time Card found", false);
                    SidebarDriverText.Text = hasPreference ?
                        "Waiting for the selected Time Card" :
                        "No supported Time Card is present";
                    return;
                }

                await deviceLifecycleGate.WaitAsync();
                try
                {
                    if (client != null || deviceSelectionClosed)
                        return;
                    TimeCardClient candidate = await Task.Run(() =>
                        new TimeCardClient(target.DevicePath));
                    client = candidate;
                    activeDevice = target;
                    Interlocked.Increment(ref deviceSessionGeneration);
                    PersistDeviceSelection(target);
                    SetConnectionState(true, "Time Card Connected", false);
                    Log("Connected to " + target.DisplayName + " at " +
                        target.DevicePath + ".");
                }
                finally
                {
                    deviceLifecycleGate.Release();
                }

                await RefreshSnapshotAsync(true);
                if (client != null)
                {
                    await RefreshIdentityAsync();
                    if (lastSnapshot != null && lastSnapshot.AbiVersion >= 10 &&
                        lastSnapshot.Layout != "Orolia ART")
                        await RefreshSensorsAsync();
                }
            }
            catch (Win32Exception ex)
            {
                bool accessDenied = ex.NativeErrorCode == 5;
                SetConnectionState(false, accessDenied ?
                    "Administrator required" : "Driver unavailable",
                    accessDenied);
                SidebarDriverText.Text = accessDenied ?
                    "Elevate to access hardware" :
                    "Selected Time Card is not available";
                Log(string.Format("Connection to {0} failed: {1} (Windows error {2}).",
                    SelectedDeviceLogName(), ex.Message, ex.NativeErrorCode));
            }
            catch (Exception ex)
            {
                SetConnectionState(false, "Connection failed", false);
                SidebarDriverText.Text = "Selected Time Card connection failed";
                Log("Connection to " + SelectedDeviceLogName() +
                    " failed: " + ex.Message);
            }
            finally
            {
                connecting = false;
                UpdateDeviceSelectionControls();
            }
        }

        private async Task RefreshCardInventoryAsync(bool userInitiated)
        {
            if (cardInventoryRefreshing || deviceSelectionClosed)
                return;
            cardInventoryRefreshing = true;
            UpdateDeviceSelectionControls();
            if (CardSelector != null)
                CardSelector.Text = "Scanning Time Cards...";
            try
            {
                IList<TimeCardDeviceDescriptor> discovered = await Task.Run(
                    () => TimeCardClient.EnumerateDevices());
                if (deviceSelectionClosed)
                    return;
                lastDeviceInventoryUtc = DateTime.UtcNow;
                deviceInventory.Clear();
                deviceInventory.AddRange(discovered);
                ApplyDeviceInventory();
                if (userInitiated)
                    Log(string.Format("Time Card rescan found {0} controller interface(s).",
                        deviceInventory.Count));
            }
            catch (Exception ex)
            {
                if (CardSelector != null)
                {
                    CardSelector.SelectedIndex = -1;
                    CardSelector.Text = "Time Card scan failed";
                    CardSelector.ToolTip = ex.Message;
                }
                Log("Time Card inventory refresh failed: " + ex.Message);
                if (userInitiated)
                    MessageBox.Show(this, ex.Message,
                        "Unable to scan Time Cards", MessageBoxButton.OK,
                        MessageBoxImage.Warning);
            }
            finally
            {
                cardInventoryRefreshing = false;
                UpdateDeviceSelectionControls();
            }
        }

        private void ApplyDeviceInventory()
        {
            bool hasPreference = HasPersistedDevicePreference();
            TimeCardDeviceDescriptor target = null;
            if (activeDevice != null)
                target = TimeCardDeviceSelection.SelectPreferred(deviceInventory,
                    activeDevice.SerialNumber, activeDevice.DevicePath, false);
            if (target == null)
                target = TimeCardDeviceSelection.SelectPreferred(deviceInventory,
                    productSettings == null ? null :
                        productSettings.SelectedDeviceSerial,
                    productSettings == null ? null :
                        productSettings.SelectedDevicePath,
                    !hasPreference);

            updatingCardSelector = true;
            try
            {
                CardSelector.ItemsSource = null;
                CardSelector.ItemsSource = deviceInventory.ToList();
                CardSelector.SelectedItem = target;
                if (target == null)
                {
                    CardSelector.SelectedIndex = -1;
                    CardSelector.Text = hasPreference ?
                        "Selected card is not present" :
                        "No Time Cards found";
                    CardSelector.ToolTip = hasPreference ?
                        "Reconnect the previously selected card, or select another present card." :
                        "No controller published the Time Card device interface.";
                }
                else
                {
                    CardSelector.ToolTip = DeviceToolTip(target);
                }
            }
            finally
            {
                updatingCardSelector = false;
            }

            selectedDevice = target;
            if (activeDevice != null && target != null &&
                string.Equals(activeDevice.DevicePath, target.DevicePath,
                    StringComparison.OrdinalIgnoreCase))
                activeDevice = target;
            if (!hasPreference && target != null)
                PersistDeviceSelection(target);
        }

        private async void RescanCards_Click(object sender, RoutedEventArgs e)
        {
            if (!CanChangeDevice(true))
                return;
            await RefreshCardInventoryAsync(true);
            if (client == null && selectedDevice != null)
                await ConnectSelectedDeviceAsync();
        }

        private async void CardSelector_SelectionChanged(object sender,
            SelectionChangedEventArgs e)
        {
            if (updatingCardSelector || cardInventoryRefreshing ||
                deviceSelectionClosed)
                return;
            TimeCardDeviceDescriptor target =
                CardSelector.SelectedItem as TimeCardDeviceDescriptor;
            if (target == null)
                return;
            if (!CanChangeDevice(true))
            {
                RestoreSelectorToActiveDevice();
                return;
            }
            selectedDevice = target;
            PersistDeviceSelection(target);
            CardSelector.ToolTip = DeviceToolTip(target);
            if (productSettings != null && productSettings.DemoMode)
                return;
            if (client != null && activeDevice != null &&
                string.Equals(activeDevice.DevicePath, target.DevicePath,
                    StringComparison.OrdinalIgnoreCase))
                return;
            await SwitchToDeviceAsync(target);
        }

        private async Task SwitchToDeviceAsync(TimeCardDeviceDescriptor target)
        {
            if (target == null || deviceSwitching || deviceSelectionClosed)
                return;
            if (!CanChangeDevice(true))
                return;

            await deviceLifecycleGate.WaitAsync();
            TimeCardClient retiringClient = null;
            bool opened = false;
            try
            {
                if (!CanChangeDevice(true))
                    return;
                deviceSwitching = true;
                connecting = true;
                UpdateDeviceSelectionControls();
                SetConnectionState(false, "Switching Time Cards...", false);

                await StopUartMonitorAndWaitAsync();
                await StopNativeDisciplineAsync(false);

                Interlocked.Increment(ref deviceSessionGeneration);
                retiringClient = client;
                client = null;
                activeDevice = null;
                if (retiringClient != null)
                    await Task.Run(() => retiringClient.Dispose());
                ResetDeviceSessionState();

                selectedDevice = target;
                PersistDeviceSelection(target);
                TimeCardClient candidate = await Task.Run(() =>
                    new TimeCardClient(target.DevicePath));
                client = candidate;
                activeDevice = target;
                Interlocked.Increment(ref deviceSessionGeneration);
                opened = true;
                SetConnectionState(true, "Time Card Connected", false);
                Log("Switched to " + target.DisplayName + " at " +
                    target.DevicePath + ".");
            }
            catch (Exception ex)
            {
                SetConnectionState(false, "Selected card unavailable", false);
                SidebarDriverText.Text = "Waiting for the selected Time Card";
                Log("Unable to switch to " + target.DisplayName + ": " +
                    ex.Message);
                MessageBox.Show(this, ex.Message,
                    "Unable to switch Time Cards", MessageBoxButton.OK,
                    MessageBoxImage.Warning);
            }
            finally
            {
                connecting = false;
                deviceSwitching = false;
                deviceLifecycleGate.Release();
                UpdateDeviceSelectionControls();
            }

            if (opened)
            {
                await RefreshSnapshotAsync(true);
                if (client != null)
                {
                    await RefreshIdentityAsync();
                    if (lastSnapshot != null && lastSnapshot.AbiVersion >= 10 &&
                        lastSnapshot.Layout != "Orolia ART")
                        await RefreshSensorsAsync();
                }
            }
        }

        private bool TryCaptureDeviceSession(out TimeCardClient activeClient,
            out int generation)
        {
            activeClient = client;
            generation = Volatile.Read(ref deviceSessionGeneration);
            return activeClient != null && !deviceSwitching &&
                !deviceSelectionClosed;
        }

        private bool IsDeviceSessionCurrent(TimeCardClient activeClient,
            int generation)
        {
            return !deviceSelectionClosed && !deviceSwitching &&
                ReferenceEquals(client, activeClient) &&
                Volatile.Read(ref deviceSessionGeneration) == generation;
        }

        private async Task RetireFailedDeviceSessionAsync(
            TimeCardClient failedClient, int generation)
        {
            await deviceLifecycleGate.WaitAsync();
            try
            {
                if (!IsDeviceSessionCurrent(failedClient, generation))
                    return;
                Interlocked.Increment(ref deviceSessionGeneration);
                client = null;
                activeDevice = null;
                await StopUartMonitorAndWaitAsync();
                await StopNativeDisciplineAsync(false);
                await Task.Run(() => failedClient.Dispose());
                ResetDeviceSessionState();
                SetConnectionState(false, "Connection lost", false);
                SidebarDriverText.Text = "Waiting for the selected Time Card";
            }
            finally
            {
                deviceLifecycleGate.Release();
                UpdateDeviceSelectionControls();
            }
        }

        private async Task StopUartMonitorAndWaitAsync()
        {
            StopUartMonitor();
            Task monitor = uartMonitorTask;
            if (monitor == null)
                return;
            Task completed = await Task.WhenAny(monitor,
                Task.Delay(TimeSpan.FromSeconds(3)));
            if (!ReferenceEquals(completed, monitor))
                throw new TimeoutException(
                    "The UART monitor did not stop; the card switch was cancelled.");
            await monitor;
        }

        private void ResetDeviceSessionState()
        {
            lastSnapshot = null;
            lastSensorSnapshot = null;
            lastSa53Snapshot = null;
            lastMro50Status = null;
            lastUbloxSnapshot = null;
            lastUbloxPort = null;
            lastUbloxBaud = null;
            lastFlashStatus = null;
            selectedFlashImage = null;
            selectedFlashPath = null;
            selectedFlashIsRaw = false;
            lastI2cData = null;
            lastFpgaCapabilities = null;
            lastFpgaImageInfo = null;
            lastPpsMaster = null;
            lastPpsSlave = null;
            lastIrigMaster = null;
            lastIrigSlave = null;
            lastDcfMaster = null;
            lastDcfSlave = null;
            lastTodParser = null;
            nativeDisciplineCapabilities = null;
            Array.Clear(lastSmaLedStates, 0, lastSmaLedStates.Length);
            Array.Clear(lastAppliedLedColors, 0, lastAppliedLedColors.Length);
            Array.Clear(boardLedHardwareFaults, 0,
                boardLedHardwareFaults.Length);
            boardLedHardwareWarning = null;
            secondaryGnssPresent = null;
            uartConsoleEntries.Clear();
            uartConsoleHistoryBytes = 0;
            if (UartOutputTextBox != null)
                UartOutputTextBox.Clear();
            ResetAllUartDecoders();
            ResetClockHistory();
            TelemetryClear_Click(null, null);
            if (SensorsSamplingDurationChart != null)
                SensorsSamplingDurationChart.Clear();
            if (SidebarSerialText != null)
                SidebarSerialText.Text = "Not read";
            if (I2cSerialNumberText != null)
                I2cSerialNumberText.Text = "Select a card to read its identity";
            UpdateFlashStartState();
            UpdateHealthExperience();
        }

        private bool CanChangeDevice(bool showMessage)
        {
            string reason = flashUpdating ?
                "The FPGA firmware update must finish before changing cards." :
                criticalConfigurationWrite ?
                "The configuration write and verification must finish before changing cards." :
                null;
            if (reason == null)
                return true;
            if (showMessage)
                MessageBox.Show(this, reason, "Time Card is busy",
                    MessageBoxButton.OK, MessageBoxImage.Information);
            return false;
        }

        private void UpdateDeviceSelectionControls()
        {
            if (CardSelector == null || CardRescanButton == null)
                return;
            bool busy = cardInventoryRefreshing || deviceSwitching ||
                flashUpdating || criticalConfigurationWrite ||
                (productSettings != null && productSettings.DemoMode);
            CardSelector.IsEnabled = !busy && deviceInventory.Count != 0;
            CardRescanButton.IsEnabled = !busy;
        }

        private void RestoreSelectorToActiveDevice()
        {
            updatingCardSelector = true;
            try
            {
                CardSelector.SelectedItem = activeDevice ?? selectedDevice;
            }
            finally
            {
                updatingCardSelector = false;
            }
        }

        private bool HasPersistedDevicePreference()
        {
            return productSettings != null &&
                (TimeCardDeviceSelection.NormalizeSerial(
                    productSettings.SelectedDeviceSerial).Length == 12 ||
                 !string.IsNullOrWhiteSpace(
                    productSettings.SelectedDevicePath));
        }

        private void PersistDeviceSelection(TimeCardDeviceDescriptor device)
        {
            if (device == null || productSettings == null)
                return;
            string serial = TimeCardDeviceSelection.NormalizeSerial(
                device.SerialNumber);
            productSettings.SelectedDeviceSerial = serial;
            productSettings.SelectedDevicePath = device.DevicePath;
            SaveProductSettings();
        }

        private void UpdateActiveDeviceIdentity(string serialNumber)
        {
            string serial = TimeCardDeviceSelection.NormalizeSerial(serialNumber);
            if (activeDevice == null || serial.Length != 12)
                return;
            TimeCardDeviceDescriptor updated = new TimeCardDeviceDescriptor(
                activeDevice.DevicePath,
                TimeCardDeviceSelection.FormatSerial(serial),
                activeDevice.BoardName, activeDevice.AbiVersion,
                activeDevice.IsAccessible, activeDevice.Error);
            activeDevice = updated;
            selectedDevice = updated;
            for (int index = 0; index < deviceInventory.Count; index++)
            {
                if (string.Equals(deviceInventory[index].DevicePath,
                    updated.DevicePath, StringComparison.OrdinalIgnoreCase))
                {
                    deviceInventory[index] = updated;
                    break;
                }
            }
            PersistDeviceSelection(updated);
            updatingCardSelector = true;
            try
            {
                CardSelector.ItemsSource = null;
                CardSelector.ItemsSource = deviceInventory.ToList();
                CardSelector.SelectedItem = updated;
                CardSelector.ToolTip = DeviceToolTip(updated);
            }
            finally
            {
                updatingCardSelector = false;
            }
        }

        private string SelectedDeviceLogName()
        {
            return selectedDevice == null ? "the selected Time Card" :
                selectedDevice.DisplayName + " at " +
                selectedDevice.DevicePath;
        }

        private string ActiveDevicePath()
        {
            return activeDevice == null ?
                (client == null ? string.Empty : client.DevicePath) :
                activeDevice.DevicePath;
        }

        private string ActiveDeviceSerial()
        {
            string serial = activeDevice == null ? string.Empty :
                TimeCardDeviceSelection.NormalizeSerial(
                    activeDevice.SerialNumber);
            return serial.Length == 12 ?
                TimeCardDeviceSelection.FormatSerial(serial) : string.Empty;
        }

        private string ActiveDeviceDisplayName()
        {
            return activeDevice == null ? "OCP Time Card Controller" :
                activeDevice.DisplayName;
        }

        private static string DeviceToolTip(TimeCardDeviceDescriptor device)
        {
            if (device == null)
                return string.Empty;
            string detail = device.DevicePath;
            if (!string.IsNullOrWhiteSpace(device.Error))
                detail += Environment.NewLine + device.Error;
            return detail;
        }

        private void CloseDeviceSelection()
        {
            deviceSelectionClosed = true;
            Interlocked.Increment(ref deviceSessionGeneration);
        }
    }
}
