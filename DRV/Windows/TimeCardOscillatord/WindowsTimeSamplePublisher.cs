using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;

namespace TimeCardControlCenter
{
    /*
     * A small seqlock-protected bridge between the LocalSystem discipline
     * service and the W32Time input-provider DLL.  This is the Windows-native
     * equivalent of oscillatord's Linux NTP shared-memory publication; it
     * contains measurements only and never lets the provider steer the PHC.
     */
    internal sealed class WindowsTimeSamplePublisher : IDisposable
    {
        public const string MappingName =
            "Global\\OcpTimeCard.TimeSample.v1";
        public const int MappingSize = 128;
        private const uint Magic = 0x5450434fu; // "OCPT" little endian.
        private const uint Version = 1u;
        private const uint FlagPresent = 1u << 0;
        private const uint FlagSynchronized = 1u << 1;
        private const uint FlagGnssFix = 1u << 2;
        private const ulong MaximumSampleAgeMilliseconds = 5000ul;
        private const ulong FileTimeUnixEpoch = 116444736000000000ul;

        private readonly MemoryMappedFile mapping;
        private readonly MemoryMappedViewAccessor view;
        private uint sequence;

        public WindowsTimeSamplePublisher()
        {
            MemoryMappedFileSecurity security =
                new MemoryMappedFileSecurity();
            security.AddAccessRule(new AccessRule<MemoryMappedFileRights>(
                new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null),
                MemoryMappedFileRights.FullControl, AccessControlType.Allow));
            security.AddAccessRule(new AccessRule<MemoryMappedFileRights>(
                new SecurityIdentifier(WellKnownSidType.LocalServiceSid, null),
                MemoryMappedFileRights.Read, AccessControlType.Allow));
            security.AddAccessRule(new AccessRule<MemoryMappedFileRights>(
                new SecurityIdentifier(
                    WellKnownSidType.BuiltinAdministratorsSid, null),
                MemoryMappedFileRights.Read, AccessControlType.Allow));
            mapping = MemoryMappedFile.CreateOrOpen(MappingName, MappingSize,
                MemoryMappedFileAccess.ReadWrite,
                MemoryMappedFileOptions.None, security,
                HandleInheritability.None);
            view = mapping.CreateViewAccessor(0, MappingSize,
                MemoryMappedFileAccess.ReadWrite);
            WriteHeader(0u);
        }

        public void Publish(TimeCardSnapshot card, bool synchronized,
            bool gnssFix)
        {
            if (card == null)
            {
                Invalidate();
                return;
            }
            ulong publishTick = GetTickCount64();
            if (card.SampleTickMilliseconds == 0ul ||
                publishTick < card.SampleTickMilliseconds ||
                publishTick - card.SampleTickMilliseconds >
                    MaximumSampleAgeMilliseconds)
            {
                Invalidate();
                return;
            }
            ulong before = checked((ulong)card.SystemTimeBeforeUtc.ToFileTimeUtc());
            ulong after = checked((ulong)card.SystemTimeAfterUtc.ToFileTimeUtc());
            if (after < before)
            {
                Invalidate();
                return;
            }
            ulong midpoint = before + ((after - before) / 2ul);
            DateTime epoch = new DateTime(1970, 1, 1, 0, 0, 0,
                DateTimeKind.Utc);
            long unixTicks = checked((card.CardTimeUtc - epoch).Ticks);
            ulong cardFileTime = checked(FileTimeUnixEpoch +
                (ulong)unixTicks);
            long offset = checked((long)cardFileTime - (long)midpoint);
            long delay = checked((long)(after - before));
            ulong dispersion = Math.Max(1ul, (after - before + 1ul) / 2ul);
            uint flags = FlagPresent;
            if (synchronized)
                flags |= FlagSynchronized;
            if (gnssFix)
                flags |= FlagGnssFix;

            BeginWrite(flags);
            view.Write(24, offset);
            view.Write(32, delay);
            view.Write(40, dispersion);
            /* Freshness is anchored to the cross-timestamp IOCTL, not to any
             * later oscillator UART polling performed before publication. */
            view.Write(48, card.SampleTickMilliseconds);
            view.Write(56, cardFileTime);
            view.Write(64, midpoint);
            view.Write(72, after);
            EndWrite();
        }

        public void Invalidate()
        {
            BeginWrite(0u);
            for (long offset = 24; offset < MappingSize; offset += 8)
                view.Write(offset, 0ul);
            EndWrite();
        }

        private void WriteHeader(uint flags)
        {
            view.Write(0, Magic);
            view.Write(4, Version);
            view.Write(8, (uint)MappingSize);
            view.Write(12, sequence);
            view.Write(16, flags);
            view.Write(20, 0u);
            view.Flush();
        }

        private void BeginWrite(uint flags)
        {
            unchecked { sequence = (sequence + 1u) | 1u; }
            view.Write(12, sequence);
            Thread.MemoryBarrier();
            view.Write(16, flags);
        }

        private void EndWrite()
        {
            Thread.MemoryBarrier();
            unchecked { sequence = (sequence + 1u) & ~1u; }
            view.Write(12, sequence);
            view.Flush();
        }

        public void Dispose()
        {
            try { Invalidate(); } catch { }
            view.Dispose();
            mapping.Dispose();
        }

        [DllImport("kernel32.dll")]
        private static extern ulong GetTickCount64();
    }
}
