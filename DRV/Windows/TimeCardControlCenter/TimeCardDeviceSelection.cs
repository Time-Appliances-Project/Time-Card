using System;
using System.Collections.Generic;
using System.Globalization;

namespace TimeCardControlCenter
{
    public sealed class TimeCardDeviceDescriptor
    {
        public TimeCardDeviceDescriptor(string devicePath, string serialNumber,
            string boardName, uint abiVersion, bool isAccessible, string error)
        {
            DevicePath = devicePath ?? string.Empty;
            SerialNumber = serialNumber ?? string.Empty;
            BoardName = string.IsNullOrWhiteSpace(boardName) ?
                "Time Card" : boardName.Trim();
            AbiVersion = abiVersion;
            IsAccessible = isAccessible;
            Error = error ?? string.Empty;
        }

        public string DevicePath { get; private set; }
        public string SerialNumber { get; private set; }
        public string BoardName { get; private set; }
        public uint AbiVersion { get; private set; }
        public bool IsAccessible { get; private set; }
        public string Error { get; private set; }

        public string DisplayName
        {
            get
            {
                string serial = TimeCardDeviceSelection.NormalizeSerial(
                    SerialNumber);
                string detail = serial.Length == 12 ?
                    TimeCardDeviceSelection.FormatSerial(serial) :
                    ShortDevicePath(DevicePath);
                string status = IsAccessible ? string.Empty : " (unavailable)";
                return BoardName + (detail.Length == 0 ? string.Empty :
                    "  -  " + detail) + status;
            }
        }

        public override string ToString()
        {
            return DisplayName;
        }

        private static string ShortDevicePath(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return string.Empty;
            const int edge = 18;
            if (value.Length <= (edge * 2) + 3)
                return value;
            return value.Substring(0, edge) + "..." +
                value.Substring(value.Length - edge, edge);
        }
    }

    public static class TimeCardDeviceSelection
    {
        public static TimeCardDeviceDescriptor SelectPreferred(
            IList<TimeCardDeviceDescriptor> devices, string preferredSerial,
            string preferredPath, bool allowFallback)
        {
            if (devices == null)
                throw new ArgumentNullException("devices");

            string serial = NormalizeSerial(preferredSerial);
            string path = (preferredPath ?? string.Empty).Trim();
            if (serial.Length == 12)
            {
                List<TimeCardDeviceDescriptor> serialMatches =
                    new List<TimeCardDeviceDescriptor>();
                foreach (TimeCardDeviceDescriptor device in devices)
                {
                    if (device != null && string.Equals(NormalizeSerial(
                        device.SerialNumber), serial, StringComparison.Ordinal))
                        serialMatches.Add(device);
                }
                if (serialMatches.Count == 1)
                    return serialMatches[0];
                if (serialMatches.Count > 1 && path.Length != 0)
                {
                    foreach (TimeCardDeviceDescriptor device in serialMatches)
                    {
                        if (PathEquals(device.DevicePath, path))
                            return device;
                    }
                }
                if (!allowFallback)
                    return null;
            }

            if (path.Length != 0)
            {
                foreach (TimeCardDeviceDescriptor device in devices)
                {
                    if (device != null && PathEquals(device.DevicePath, path))
                        return device;
                }
                if (!allowFallback)
                    return null;
            }

            if (!allowFallback)
                return null;
            foreach (TimeCardDeviceDescriptor device in devices)
            {
                if (device != null && device.IsAccessible)
                    return device;
            }
            return devices.Count == 0 ? null : devices[0];
        }

        public static bool SameDevice(TimeCardDeviceDescriptor left,
            TimeCardDeviceDescriptor right)
        {
            if (ReferenceEquals(left, right))
                return true;
            if (left == null || right == null)
                return false;
            string leftSerial = NormalizeSerial(left.SerialNumber);
            string rightSerial = NormalizeSerial(right.SerialNumber);
            if (leftSerial.Length == 12 && rightSerial.Length == 12)
                return string.Equals(leftSerial, rightSerial,
                    StringComparison.Ordinal);
            return PathEquals(left.DevicePath, right.DevicePath);
        }

        public static string NormalizeSerial(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return string.Empty;
            char[] result = new char[12];
            int count = 0;
            foreach (char character in value)
            {
                if (character == ':' || character == '-' ||
                    char.IsWhiteSpace(character))
                    continue;
                if (!Uri.IsHexDigit(character) || count >= result.Length)
                    return string.Empty;
                result[count++] = char.ToUpperInvariant(character);
            }
            return count == result.Length ? new string(result) : string.Empty;
        }

        public static string FormatSerial(string normalizedSerial)
        {
            string serial = NormalizeSerial(normalizedSerial);
            if (serial.Length != 12)
                return string.Empty;
            return string.Format(CultureInfo.InvariantCulture,
                "{0}:{1}:{2}:{3}:{4}:{5}", serial.Substring(0, 2),
                serial.Substring(2, 2), serial.Substring(4, 2),
                serial.Substring(6, 2), serial.Substring(8, 2),
                serial.Substring(10, 2));
        }

        private static bool PathEquals(string left, string right)
        {
            return string.Equals((left ?? string.Empty).Trim(),
                (right ?? string.Empty).Trim(),
                StringComparison.OrdinalIgnoreCase);
        }
    }
}
