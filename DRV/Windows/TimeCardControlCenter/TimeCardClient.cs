using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace TimeCardControlCenter
{
    public sealed class TimeCardClient : IDisposable
    {
        private const string DevicePath = @"\\.\TimeCard0";
        private const uint GenericRead = 0x80000000;
        private const uint GenericWrite = 0x40000000;
        private const uint FileShareRead = 0x00000001;
        private const uint FileShareWrite = 0x00000002;
        private const uint OpenExisting = 3;
        private const uint FileDeviceUnknown = 0x22;
        private const uint FileReadAccess = 0x0001;
        private const uint FileWriteAccess = 0x0002;
        private const int MaximumUartTransfer = 256;
        private const int MaximumI2cTransfer = 255;
        private const int I2cTransferHeaderSize = 20;
        private const int I2cTransferBufferSize = 276;

        private static readonly uint IoctlSetTime = ControlCode(1, FileWriteAccess);
        private static readonly uint IoctlGetInfo = ControlCode(2, FileReadAccess);
        private static readonly uint IoctlGetCrossTimestamp = ControlCode(3, FileReadAccess);
        private static readonly uint IoctlUartConfigure = ControlCode(4, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlUartRead = ControlCode(5, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlUartWrite = ControlCode(6, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlHierarchy = ControlCode(7, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlSmaQuery = ControlCode(8, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlSmaSet = ControlCode(9, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlI2cGetStatus = ControlCode(10, FileReadAccess);
        private static readonly uint IoctlI2cProbe = ControlCode(11, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlI2cRead = ControlCode(12, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlClockSourceSet = ControlCode(13, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlNmeaQuery = ControlCode(14, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlNmeaSet = ControlCode(15, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlGetIdentity = ControlCode(16, FileReadAccess);

        private readonly object gate = new object();
        private SafeFileHandle handle;

        public TimeCardClient()
        {
            handle = CreateFile(DevicePath, GenericRead | GenericWrite,
                FileShareRead | FileShareWrite, IntPtr.Zero, OpenExisting, 0, IntPtr.Zero);
            if (handle.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                handle = null;
                throw new Win32Exception(error,
                    "Unable to open the OCP Time Card driver at " + DevicePath + ".");
            }
        }

        public TimeCardSnapshot GetSnapshot()
        {
            TimeCardInfoRaw info = GetOutput<TimeCardInfoRaw>(IoctlGetInfo);
            TimeCardCrossTimestampRaw timestamp =
                GetOutput<TimeCardCrossTimestampRaw>(IoctlGetCrossTimestamp);
            TimeCardHierarchyRaw hierarchy = SetHierarchyRaw(0, false);
            return new TimeCardSnapshot(info, timestamp, hierarchy);
        }

        public void SetClockFromSystem()
        {
            DateTime utc = DateTime.UtcNow;
            DateTime epoch = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
            long ticks = (utc - epoch).Ticks;
            TimeCardTimeRaw value = new TimeCardTimeRaw
            {
                Seconds = (ulong)(ticks / TimeSpan.TicksPerSecond),
                Nanoseconds = (uint)((ticks % TimeSpan.TicksPerSecond) * 100L),
                Reserved = 0
            };
            SendInput(IoctlSetTime, value);
        }

        public uint SetClockSource(uint source)
        {
            TimeCardClockSourceRaw request = new TimeCardClockSourceRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardClockSourceRaw)),
                Source = source
            };
            byte[] output = Call(IoctlClockSourceSet, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardClockSourceRaw)));
            return BytesToStruct<TimeCardClockSourceRaw>(output).ActiveSource;
        }

        public NmeaOutputState GetNmeaOutput()
        {
            return new NmeaOutputState(
                GetOutput<TimeCardNmeaControlRaw>(IoctlNmeaQuery));
        }

        public NmeaOutputState SetNmeaOutput(bool enabled, uint baud,
                                              bool inverted)
        {
            TimeCardNmeaControlRaw request = new TimeCardNmeaControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardNmeaControlRaw)),
                Flags = enabled ? 2u : 0u,
                Baud = baud,
                Polarity = inverted ? 1u : 0u
            };
            byte[] output = Call(IoctlNmeaSet, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardNmeaControlRaw)));
            return new NmeaOutputState(
                BytesToStruct<TimeCardNmeaControlRaw>(output));
        }

        public TimeCardIdentity GetIdentity()
        {
            return new TimeCardIdentity(
                GetOutput<TimeCardIdentityRaw>(IoctlGetIdentity));
        }

        public void ConfigureUart(uint port, uint baud)
        {
            if (port > 3)
                throw new ArgumentOutOfRangeException("port");
            if (baud == 0)
                throw new ArgumentOutOfRangeException("baud");
            SendInput(IoctlUartConfigure, new TimeCardUartConfigRaw { Port = port, Baud = baud });
        }

        public UartReadResult ReadUart(uint port, uint maximumBytes, uint timeoutMilliseconds)
        {
            if (port > 3)
                throw new ArgumentOutOfRangeException("port");
            maximumBytes = Math.Min(maximumBytes, MaximumUartTransfer);
            timeoutMilliseconds = Math.Min(timeoutMilliseconds, 5000);
            TimeCardUartReadRequestRaw request = new TimeCardUartReadRequestRaw
            {
                Port = port,
                MaximumBytes = maximumBytes,
                TimeoutMilliseconds = timeoutMilliseconds,
                Reserved = 0
            };
            byte[] output;
            try
            {
                output = Call(IoctlUartRead, StructToBytes(request), 16 + MaximumUartTransfer);
            }
            catch (Win32Exception ex)
            {
                // The KMDF driver maps STATUS_IO_TIMEOUT to ERROR_SEM_TIMEOUT
                // when no byte arrives. An idle UART is normal for a monitor.
                if (ex.NativeErrorCode == 121 || ex.NativeErrorCode == 1460)
                    return new UartReadResult(new byte[0], 0);
                throw;
            }
            uint length = BitConverter.ToUInt32(output, 4);
            uint lineStatus = BitConverter.ToUInt32(output, 12);
            if (length > MaximumUartTransfer)
                throw new InvalidOperationException("The driver returned an invalid UART length.");
            byte[] data = new byte[length];
            Buffer.BlockCopy(output, 16, data, 0, (int)length);
            return new UartReadResult(data, lineStatus);
        }

        public UartWriteResult WriteUart(uint port, byte[] data, uint timeoutMilliseconds)
        {
            if (port > 3)
                throw new ArgumentOutOfRangeException("port");
            if (data == null)
                throw new ArgumentNullException("data");
            if (data.Length > MaximumUartTransfer)
                throw new ArgumentOutOfRangeException("data", "UART writes are limited to 256 bytes.");

            byte[] input = new byte[16 + data.Length];
            Buffer.BlockCopy(BitConverter.GetBytes(port), 0, input, 0, 4);
            Buffer.BlockCopy(BitConverter.GetBytes((uint)data.Length), 0, input, 4, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(Math.Min(timeoutMilliseconds, 5000)), 0, input, 8, 4);
            Buffer.BlockCopy(data, 0, input, 16, data.Length);
            byte[] output = Call(IoctlUartWrite, input, 8);
            return new UartWriteResult(BitConverter.ToUInt32(output, 0),
                BitConverter.ToUInt32(output, 4));
        }

        internal string ExecuteSa53Command(string command, uint timeoutMilliseconds)
        {
            if (string.IsNullOrWhiteSpace(command))
                throw new ArgumentException("An SA53 command is required.", "command");
            if (command.Length > 220)
                throw new ArgumentOutOfRangeException("command", "SA53 commands are limited to 220 characters.");
            foreach (char value in command)
            {
                if (value < 0x20 || value > 0x7e || value == '{' || value == '}' ||
                    value == '[' || value == ']')
                    throw new ArgumentException("The SA53 command contains an unsupported character.", "command");
            }

            timeoutMilliseconds = Math.Max(100u, Math.Min(timeoutMilliseconds, 5000u));
            lock (gate)
            {
                // MAC-SA53 C3 defaults to 57,600 baud, 8N1, no flow control.
                ConfigureUart(2, 57600);

                // Discard stale announcements or bytes left by a previous console session.
                for (int pass = 0; pass < 8; pass++)
                {
                    UartReadResult stale = ReadUart(2, MaximumUartTransfer, 10);
                    if (stale.Data.Length == 0)
                        break;
                }

                byte[] request = Encoding.ASCII.GetBytes("{" + command + "}");
                UartWriteResult written = WriteUart(2, request, timeoutMilliseconds);
                if (written.BytesTransferred != request.Length)
                    throw new InvalidOperationException("The complete SA53 command was not transmitted.");

                Stopwatch timer = Stopwatch.StartNew();
                StringBuilder received = new StringBuilder();
                while (timer.ElapsedMilliseconds < timeoutMilliseconds && received.Length < 8192)
                {
                    uint remaining = (uint)Math.Max(1L,
                        (long)timeoutMilliseconds - timer.ElapsedMilliseconds);
                    UartReadResult result = ReadUart(2, MaximumUartTransfer,
                        Math.Min(remaining, 100u));
                    if (result.Data.Length == 0)
                        continue;
                    received.Append(Encoding.ASCII.GetString(result.Data));

                    int scan = 0;
                    while (scan < received.Length)
                    {
                        int start = received.ToString().IndexOf('[', scan);
                        if (start < 0)
                            break;
                        int end = received.ToString().IndexOf(']', start + 1);
                        if (end < 0)
                            break;
                        string frame = received.ToString(start + 1, end - start - 1);
                        scan = end + 1;
                        if (frame.StartsWith(">", StringComparison.Ordinal))
                            continue;
                        if (frame.StartsWith("!", StringComparison.Ordinal))
                            throw new InvalidOperationException("The SA53 rejected '" + command +
                                "': " + StripSa53Checksum(frame.Substring(1)) + ".");
                        if (frame.StartsWith("=", StringComparison.Ordinal))
                            return StripSa53Checksum(frame.Substring(1));
                    }
                }

                throw new TimeoutException("The SA53 did not answer '" + command +
                    "' within " + timeoutMilliseconds + " ms.");
            }
        }

        private static string StripSa53Checksum(string value)
        {
            int checksum = value.LastIndexOf('|');
            return checksum < 0 ? value.Trim() : value.Substring(0, checksum).Trim();
        }

        internal byte[] ExecuteUbxPoll(uint port, uint baud, byte messageClass,
            byte messageId, byte[] payload, uint timeoutMilliseconds)
        {
            return ExecuteUbxExchange(port, baud, messageClass, messageId,
                payload, messageClass, messageId, false, timeoutMilliseconds);
        }

        internal void ExecuteUbxSet(uint port, uint baud, byte messageClass,
            byte messageId, byte[] payload, uint timeoutMilliseconds)
        {
            ExecuteUbxExchange(port, baud, messageClass, messageId,
                payload, 0, 0, true, timeoutMilliseconds);
        }

        private byte[] ExecuteUbxExchange(uint port, uint baud,
            byte messageClass, byte messageId, byte[] payload,
            byte responseClass, byte responseId, bool expectAcknowledgement,
            uint timeoutMilliseconds)
        {
            if (port > 1)
                throw new ArgumentOutOfRangeException("port", "GNSS receivers use UART 0 or UART 1.");
            if (baud == 0)
                throw new ArgumentOutOfRangeException("baud");
            if (payload == null)
                payload = new byte[0];
            if (payload.Length > MaximumUartTransfer - 8)
                throw new ArgumentOutOfRangeException("payload", "UBX requests are limited to 248 payload bytes.");
            timeoutMilliseconds = Math.Max(100u, Math.Min(timeoutMilliseconds, 5000u));

            lock (gate)
            {
                ConfigureUart(port, baud);
                for (int pass = 0; pass < 12; pass++)
                {
                    UartReadResult stale = ReadUart(port, MaximumUartTransfer, 5);
                    if (stale.Data.Length == 0)
                        break;
                }

                byte[] request = BuildUbxFrame(messageClass, messageId, payload);
                UartWriteResult written = WriteUart(port, request, timeoutMilliseconds);
                if (written.BytesTransferred != request.Length)
                    throw new InvalidOperationException("The complete UBX request was not transmitted.");

                Stopwatch timer = Stopwatch.StartNew();
                List<byte> received = new List<byte>();
                while (timer.ElapsedMilliseconds < timeoutMilliseconds && received.Count < 16384)
                {
                    uint remaining = (uint)Math.Max(1L,
                        (long)timeoutMilliseconds - timer.ElapsedMilliseconds);
                    UartReadResult result = ReadUart(port, MaximumUartTransfer,
                        Math.Min(remaining, 100u));
                    if (result.Data.Length != 0)
                        received.AddRange(result.Data);

                    byte frameClass;
                    byte frameId;
                    byte[] framePayload;
                    while (TryTakeUbxFrame(received, out frameClass, out frameId,
                        out framePayload))
                    {
                        if (frameClass == 0x05 && framePayload.Length >= 2 &&
                            framePayload[0] == messageClass && framePayload[1] == messageId)
                        {
                            if (frameId == 0x00)
                                throw new InvalidOperationException(string.Format(
                                    "The u-blox receiver rejected UBX-{0:X2}-{1:X2}.",
                                    messageClass, messageId));
                            if (frameId == 0x01 && expectAcknowledgement)
                                return new byte[0];
                        }
                        if (!expectAcknowledgement && frameClass == responseClass &&
                            frameId == responseId)
                            return framePayload;
                    }
                }

                throw new TimeoutException(string.Format(
                    "The u-blox receiver did not answer UBX-{0:X2}-{1:X2} within {2} ms.",
                    messageClass, messageId, timeoutMilliseconds));
            }
        }

        private static byte[] BuildUbxFrame(byte messageClass, byte messageId,
            byte[] payload)
        {
            byte[] frame = new byte[payload.Length + 8];
            frame[0] = 0xB5;
            frame[1] = 0x62;
            frame[2] = messageClass;
            frame[3] = messageId;
            frame[4] = (byte)(payload.Length & 0xff);
            frame[5] = (byte)((payload.Length >> 8) & 0xff);
            Buffer.BlockCopy(payload, 0, frame, 6, payload.Length);
            byte checksumA = 0;
            byte checksumB = 0;
            for (int index = 2; index < payload.Length + 6; index++)
            {
                unchecked
                {
                    checksumA += frame[index];
                    checksumB += checksumA;
                }
            }
            frame[frame.Length - 2] = checksumA;
            frame[frame.Length - 1] = checksumB;
            return frame;
        }

        private static bool TryTakeUbxFrame(List<byte> data, out byte messageClass,
            out byte messageId, out byte[] payload)
        {
            messageClass = 0;
            messageId = 0;
            payload = null;
            while (true)
            {
                int start = -1;
                for (int index = 0; index + 1 < data.Count; index++)
                {
                    if (data[index] == 0xB5 && data[index + 1] == 0x62)
                    {
                        start = index;
                        break;
                    }
                }
                if (start < 0)
                {
                    if (data.Count > 0 && data[data.Count - 1] == 0xB5)
                        data.RemoveRange(0, data.Count - 1);
                    else
                        data.Clear();
                    return false;
                }
                if (start > 0)
                    data.RemoveRange(0, start);
                if (data.Count < 8)
                    return false;

                int payloadLength = data[4] | (data[5] << 8);
                if (payloadLength > 4096)
                {
                    data.RemoveAt(0);
                    continue;
                }
                int frameLength = payloadLength + 8;
                if (data.Count < frameLength)
                    return false;

                byte checksumA = 0;
                byte checksumB = 0;
                for (int index = 2; index < payloadLength + 6; index++)
                {
                    unchecked
                    {
                        checksumA += data[index];
                        checksumB += checksumA;
                    }
                }
                if (checksumA != data[payloadLength + 6] ||
                    checksumB != data[payloadLength + 7])
                {
                    data.RemoveAt(0);
                    continue;
                }

                messageClass = data[2];
                messageId = data[3];
                payload = new byte[payloadLength];
                data.CopyTo(6, payload, 0, payloadLength);
                data.RemoveRange(0, frameLength);
                return true;
            }
        }

        public void ControlHierarchy(uint action, bool persist)
        {
            SetHierarchyRaw(action, persist);
        }

        public SmaConnectorState GetSmaConnector(uint connector)
        {
            return CallSma(IoctlSmaQuery, connector, SmaDirection.Input, 0);
        }

        public SmaConnectorState SetSmaConnector(uint connector,
                                                 SmaDirection direction,
                                                 uint function)
        {
            return CallSma(IoctlSmaSet, connector, direction, function);
        }

        private SmaConnectorState CallSma(uint code, uint connector,
                                          SmaDirection direction,
                                          uint function)
        {
            if (connector == 0 || connector > 4)
                throw new ArgumentOutOfRangeException("connector");
            TimeCardSmaControlRaw request = new TimeCardSmaControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardSmaControlRaw)),
                Connector = connector,
                Direction = (uint)direction,
                Function = function
            };
            byte[] output = Call(code, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardSmaControlRaw)));
            return new SmaConnectorState(BytesToStruct<TimeCardSmaControlRaw>(output));
        }

        public I2cControllerStatus GetI2cStatus()
        {
            return new I2cControllerStatus(
                GetOutput<TimeCardI2cStatusRaw>(IoctlI2cGetStatus));
        }

        public I2cProbeResult ProbeI2c(uint address)
        {
            if (address < 0x08 || address > 0x77)
                throw new ArgumentOutOfRangeException("address");
            TimeCardI2cProbeRaw request = new TimeCardI2cProbeRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardI2cProbeRaw)),
                Address = address
            };
            byte[] output = Call(IoctlI2cProbe, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardI2cProbeRaw)));
            return new I2cProbeResult(BytesToStruct<TimeCardI2cProbeRaw>(output));
        }

        public I2cReadResult ReadI2c(uint address, uint subaddress,
                                     uint subaddressLength, uint length,
                                     uint timeoutMilliseconds)
        {
            if (address < 0x08 || address > 0x77)
                throw new ArgumentOutOfRangeException("address");
            if (subaddressLength > 2)
                throw new ArgumentOutOfRangeException("subaddressLength");
            if (length == 0 || length > MaximumI2cTransfer)
                throw new ArgumentOutOfRangeException("length");

            TimeCardI2cReadRequestRaw request = new TimeCardI2cReadRequestRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardI2cReadRequestRaw)),
                Address = address,
                SubaddressLength = subaddressLength,
                Subaddress = subaddress,
                Length = length,
                TimeoutMilliseconds = Math.Min(timeoutMilliseconds, 250u)
            };
            byte[] output = Call(IoctlI2cRead, StructToBytes(request),
                I2cTransferBufferSize);
            if (output.Length < I2cTransferHeaderSize)
                throw new InvalidOperationException("The driver returned a truncated I2C response.");
            uint returnedAddress = BitConverter.ToUInt32(output, 4);
            uint returnedLength = BitConverter.ToUInt32(output, 8);
            uint controllerStatus = BitConverter.ToUInt32(output, 12);
            uint interruptStatus = BitConverter.ToUInt32(output, 16);
            if (returnedLength > MaximumI2cTransfer ||
                output.Length < I2cTransferHeaderSize + returnedLength)
                throw new InvalidOperationException("The driver returned an invalid I2C length.");
            byte[] data = new byte[returnedLength];
            Buffer.BlockCopy(output, I2cTransferHeaderSize, data, 0,
                (int)returnedLength);
            return new I2cReadResult(returnedAddress, data,
                controllerStatus, interruptStatus);
        }

        private TimeCardHierarchyRaw SetHierarchyRaw(uint action, bool persist)
        {
            TimeCardHierarchyRaw request = new TimeCardHierarchyRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardHierarchyRaw)),
                Action = action,
                Persist = persist ? 1u : 0u
            };
            byte[] output = Call(IoctlHierarchy, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardHierarchyRaw)));
            return BytesToStruct<TimeCardHierarchyRaw>(output);
        }

        public void Dispose()
        {
            lock (gate)
            {
                if (handle != null)
                {
                    handle.Dispose();
                    handle = null;
                }
            }
        }

        private static uint ControlCode(uint index, uint access)
        {
            return (FileDeviceUnknown << 16) | (access << 14) | ((0x800 + index) << 2);
        }

        private T GetOutput<T>(uint code) where T : struct
        {
            byte[] output = Call(code, null, Marshal.SizeOf(typeof(T)));
            return BytesToStruct<T>(output);
        }

        private void SendInput<T>(uint code, T input) where T : struct
        {
            Call(code, StructToBytes(input), 0);
        }

        private byte[] Call(uint code, byte[] input, int outputSize)
        {
            lock (gate)
            {
                if (handle == null || handle.IsClosed || handle.IsInvalid)
                    throw new ObjectDisposedException("TimeCardClient");
                byte[] output = outputSize == 0 ? null : new byte[outputSize];
                int returned;
                bool success = DeviceIoControl(handle, code, input,
                    input == null ? 0 : input.Length, output, outputSize,
                    out returned, IntPtr.Zero);
                if (!success)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        string.Format("Time Card IOCTL 0x{0:X8} failed.", code));
                if (output != null && returned < outputSize)
                    Array.Resize(ref output, returned);
                return output ?? new byte[0];
            }
        }

        private static byte[] StructToBytes<T>(T value) where T : struct
        {
            int size = Marshal.SizeOf(typeof(T));
            byte[] bytes = new byte[size];
            IntPtr pointer = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(value, pointer, false);
                Marshal.Copy(pointer, bytes, 0, size);
                return bytes;
            }
            finally
            {
                Marshal.FreeHGlobal(pointer);
            }
        }

        private static T BytesToStruct<T>(byte[] bytes) where T : struct
        {
            int size = Marshal.SizeOf(typeof(T));
            if (bytes == null || bytes.Length < size)
                throw new InvalidOperationException("The driver returned a truncated response.");
            IntPtr pointer = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.Copy(bytes, 0, pointer, size);
                return (T)Marshal.PtrToStructure(pointer, typeof(T));
            }
            finally
            {
                Marshal.FreeHGlobal(pointer);
            }
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFile(string fileName, uint desiredAccess,
            uint shareMode, IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeviceIoControl(SafeFileHandle device, uint controlCode,
            byte[] inputBuffer, int inputBufferSize, byte[] outputBuffer,
            int outputBufferSize, out int bytesReturned, IntPtr overlapped);
    }
}
