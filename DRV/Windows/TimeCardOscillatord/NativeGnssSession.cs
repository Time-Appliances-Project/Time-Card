using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;

namespace TimeCardControlCenter
{
    /*
     * Native, transport-neutral u-blox session support for the Windows
     * oscillatord service.  Receiving is deliberately passive.  Every method
     * which changes receiver or UART state requires allowHardwareWrite=true.
     */
    internal enum UbxResetKind
    {
        GnssStart,
        GnssStop,
        Soft,
        Hard,
        Cold
    }

    internal sealed class UbxConfigurationValue
    {
        public UbxConfigurationValue(uint key, ulong value)
        {
            Key = key;
            Value = value;
        }

        public uint Key { get; private set; }
        public ulong Value { get; private set; }
    }

    internal sealed class UbxFrame
    {
        public UbxFrame(byte messageClass, byte messageId, byte[] payload)
        {
            MessageClass = messageClass;
            MessageId = messageId;
            Payload = payload == null ? new byte[0] : (byte[])payload.Clone();
        }

        public byte MessageClass { get; private set; }
        public byte MessageId { get; private set; }
        public byte[] Payload { get; private set; }
    }

    internal static class UbxProtocol
    {
        private const int MaximumPayloadLength = 4096;

        public static byte[] BuildFrame(byte messageClass, byte messageId,
            byte[] payload)
        {
            payload = payload ?? new byte[0];
            if (payload.Length > MaximumPayloadLength)
                throw new ArgumentOutOfRangeException("payload");

            byte[] frame = new byte[payload.Length + 8];
            frame[0] = 0xb5;
            frame[1] = 0x62;
            frame[2] = messageClass;
            frame[3] = messageId;
            frame[4] = (byte)(payload.Length & 0xff);
            frame[5] = (byte)((payload.Length >> 8) & 0xff);
            Buffer.BlockCopy(payload, 0, frame, 6, payload.Length);
            byte a = 0;
            byte b = 0;
            for (int index = 2; index < frame.Length - 2; ++index)
            {
                unchecked
                {
                    a += frame[index];
                    b += a;
                }
            }
            frame[frame.Length - 2] = a;
            frame[frame.Length - 1] = b;
            return frame;
        }

        public static byte[] BuildResetFrame(UbxResetKind kind)
        {
            ushort backupMask;
            byte resetMode;
            switch (kind)
            {
                case UbxResetKind.GnssStart:
                    backupMask = 0x0001;
                    resetMode = 0x09;
                    break;
                case UbxResetKind.GnssStop:
                    backupMask = 0x0001;
                    resetMode = 0x08;
                    break;
                case UbxResetKind.Soft:
                    backupMask = 0x0001;
                    resetMode = 0x01;
                    break;
                case UbxResetKind.Hard:
                    backupMask = 0x0001;
                    resetMode = 0x04;
                    break;
                case UbxResetKind.Cold:
                    backupMask = 0xffff;
                    resetMode = 0x02;
                    break;
                default:
                    throw new ArgumentOutOfRangeException("kind");
            }

            byte[] payload =
            {
                (byte)(backupMask & 0xff),
                (byte)(backupMask >> 8),
                resetMode,
                0
            };
            return BuildFrame(0x06, 0x04, payload);
        }

        public static byte[] BuildConfigurationFrame(
            IEnumerable<UbxConfigurationValue> values, bool persist)
        {
            if (values == null)
                throw new ArgumentNullException("values");
            List<UbxConfigurationValue> materialized =
                new List<UbxConfigurationValue>(values);
            if (materialized.Count == 0)
                throw new ArgumentException(
                    "At least one configuration value is required.", "values");

            int payloadLength = 4;
            foreach (UbxConfigurationValue item in materialized)
            {
                if (item == null)
                    throw new ArgumentException(
                        "Configuration values cannot contain null.", "values");
                payloadLength = checked(payloadLength + 4 + KeySize(item.Key));
            }
            // TimeCardClient limits a UART transaction to 256 bytes, including
            // the eight-byte UBX envelope.
            if (payloadLength > 248)
                throw new ArgumentException(
                    "The CFG-VALSET payload exceeds one driver UART transaction.",
                    "values");

            byte[] payload = new byte[payloadLength];
            payload[0] = 0;                 // CFG-VALSET version 0
            payload[1] = persist ? (byte)0x07 : (byte)0x01;
            payload[2] = 0;                 // no transaction
            payload[3] = 0;
            int offset = 4;
            foreach (UbxConfigurationValue item in materialized)
            {
                WriteUInt32(payload, offset, item.Key);
                offset += 4;
                int size = KeySize(item.Key);
                for (int index = 0; index < size; ++index)
                    payload[offset + index] = (byte)(item.Value >> (index * 8));
                offset += size;
            }
            return BuildFrame(0x06, 0x8a, payload);
        }

        private static int KeySize(uint key)
        {
            switch ((key >> 28) & 0x0f)
            {
                case 1: return 1; // L / bit
                case 2: return 1; // U1, I1, E1
                case 3: return 2; // U2, I2, E2
                case 4: return 4; // U4, I4, R4
                case 5: return 8; // U8, I8, R8
                default:
                    throw new ArgumentException(string.Format(
                        CultureInfo.InvariantCulture,
                        "UBX configuration key 0x{0:X8} has an invalid size tag.",
                        key), "key");
            }
        }

        private static void WriteUInt32(byte[] value, int offset, uint item)
        {
            value[offset] = (byte)item;
            value[offset + 1] = (byte)(item >> 8);
            value[offset + 2] = (byte)(item >> 16);
            value[offset + 3] = (byte)(item >> 24);
        }

        public static IList<UbxConfigurationValue>
            BuildOscillatordConfiguration(uint baud, string preferredTimeScale,
                int cableDelayNanoseconds, bool rtcmEnabled)
        {
            if (baud < 1200u || baud > 2000000u)
                throw new ArgumentOutOfRangeException("baud");
            byte timeGrid;
            switch ((preferredTimeScale ?? string.Empty).Trim()
                .ToUpperInvariant())
            {
                case "UTC": timeGrid = 0; break;
                case "GPS": timeGrid = 1; break;
                case "GLO": timeGrid = 2; break;
                case "BDS": timeGrid = 3; break;
                case "GAL": timeGrid = 4; break;
                default:
                    throw new ArgumentException(
                        "GNSS preferred time scale must be UTC, GPS, GLO, BDS, or GAL.",
                        "preferredTimeScale");
            }
            if (cableDelayNanoseconds < 0 ||
                cableDelayNanoseconds > short.MaxValue)
                throw new ArgumentOutOfRangeException(
                    "cableDelayNanoseconds");

            List<UbxConfigurationValue> result =
                new List<UbxConfigurationValue>
            {
                // UART1 transport and a deterministic one-hertz navigation
                // epoch, matching upstream's timing receiver profile.
                new UbxConfigurationValue(0x40520001u, baud),
                new UbxConfigurationValue(0x10740001u, 1u),
                new UbxConfigurationValue(0x10740004u,
                    rtcmEnabled ? 1u : 0u),
                new UbxConfigurationValue(0x30210001u, 1000u),
                new UbxConfigurationValue(0x30210002u, 1u),
                new UbxConfigurationValue(0x20210003u, 1u),

                // Messages consumed by native discipline and monitoring.
                new UbxConfigurationValue(0x20910007u, 1u), // NAV-PVT
                new UbxConfigurationValue(0x20910061u, 1u), // NAV-TIMELS
                new UbxConfigurationValue(0x2091017eu, 1u), // TIM-TP
                new UbxConfigurationValue(0x20910098u, 1u), // TIM-SVIN
                new UbxConfigurationValue(0x2091035au, 1u), // MON-RF

                // One-hertz TP1, GNSS synchronized and TOW aligned.
                new UbxConfigurationValue(0x10050007u, 1u),
                new UbxConfigurationValue(0x10050008u, 1u),
                new UbxConfigurationValue(0x10050009u, 1u),
                new UbxConfigurationValue(0x1005000au, 1u),
                new UbxConfigurationValue(0x1005000bu, 1u),
                new UbxConfigurationValue(0x2005000cu, timeGrid),
                new UbxConfigurationValue(0x40050002u, 1000000u),
                new UbxConfigurationValue(0x40050003u, 1000000u)
            };
            if (cableDelayNanoseconds != 0)
            {
                result.Add(new UbxConfigurationValue(0x30050001u,
                    unchecked((ushort)cableDelayNanoseconds)));
            }
            if (rtcmEnabled)
            {
                // Upstream's optional base-station stream: native RTCM MSM,
                // station/bias records, plus raw observations and ephemeris
                // for an external NTRIP client.
                result.Add(new UbxConfigurationValue(0x209102beu, 1u));
                result.Add(new UbxConfigurationValue(0x209102cdu, 1u));
                result.Add(new UbxConfigurationValue(0x209102d2u, 1u));
                result.Add(new UbxConfigurationValue(0x20910319u, 1u));
                result.Add(new UbxConfigurationValue(0x209102d7u, 1u));
                result.Add(new UbxConfigurationValue(0x20910304u, 1u));
                result.Add(new UbxConfigurationValue(0x209102a5u, 1u));
                result.Add(new UbxConfigurationValue(0x20910232u, 1u));
            }
            return result;
        }
    }

    internal sealed class UbxStreamParser
    {
        private const int MaximumPayloadLength = 4096;
        private readonly List<byte> buffer = new List<byte>();

        public IList<UbxFrame> Feed(byte[] bytes)
        {
            if (bytes == null)
                throw new ArgumentNullException("bytes");
            buffer.AddRange(bytes);
            if (buffer.Count > 65536)
                buffer.RemoveRange(0, buffer.Count - 32768);

            List<UbxFrame> result = new List<UbxFrame>();
            UbxFrame frame;
            while (TryTakeFrame(out frame))
                result.Add(frame);
            return result;
        }

        private bool TryTakeFrame(out UbxFrame result)
        {
            result = null;
            while (true)
            {
                int start = -1;
                for (int index = 0; index + 1 < buffer.Count; ++index)
                {
                    if (buffer[index] == 0xb5 && buffer[index + 1] == 0x62)
                    {
                        start = index;
                        break;
                    }
                }
                if (start < 0)
                {
                    if (buffer.Count > 0 && buffer[buffer.Count - 1] == 0xb5)
                        buffer.RemoveRange(0, buffer.Count - 1);
                    else
                        buffer.Clear();
                    return false;
                }
                if (start > 0)
                    buffer.RemoveRange(0, start);
                if (buffer.Count < 8)
                    return false;

                int payloadLength = buffer[4] | (buffer[5] << 8);
                if (payloadLength > MaximumPayloadLength)
                {
                    buffer.RemoveAt(0);
                    continue;
                }
                int frameLength = payloadLength + 8;
                if (buffer.Count < frameLength)
                    return false;

                byte a = 0;
                byte b = 0;
                for (int index = 2; index < payloadLength + 6; ++index)
                {
                    unchecked
                    {
                        a += buffer[index];
                        b += a;
                    }
                }
                if (a != buffer[payloadLength + 6] ||
                    b != buffer[payloadLength + 7])
                {
                    // Drop one byte, not the entire candidate, so a valid sync
                    // sequence embedded after a damaged frame is recovered.
                    buffer.RemoveAt(0);
                    continue;
                }

                byte[] payload = new byte[payloadLength];
                buffer.CopyTo(6, payload, 0, payloadLength);
                result = new UbxFrame(buffer[2], buffer[3], payload);
                buffer.RemoveRange(0, frameLength);
                return true;
            }
        }
    }

    internal sealed class GnssUtcSample
    {
        public bool IsValid { get; internal set; }
        public string ValidationError { get; internal set; }
        public DateTime Utc { get; internal set; }
        public long UnixSeconds { get; internal set; }
        public int Nanoseconds { get; internal set; }
        public bool RepresentsLeapSecond { get; internal set; }
        public uint AccuracyNanoseconds { get; internal set; }
    }

    internal sealed class GnssNavigationSample
    {
        public long UpdatedMonotonicTicks { get; internal set; }
        public uint ITowMilliseconds { get; internal set; }
        public byte FixType { get; internal set; }
        public bool FixOk { get; internal set; }
        public byte Satellites { get; internal set; }
        public GnssUtcSample Utc { get; internal set; }
    }

    internal sealed class GnssLeapSecondSample
    {
        public long UpdatedMonotonicTicks { get; internal set; }
        public uint ITowMilliseconds { get; internal set; }
        public bool IsValid { get; internal set; }
        public string ValidationError { get; internal set; }
        public bool CurrentOffsetValid { get; internal set; }
        public int CurrentOffsetSeconds { get; internal set; }
        public byte CurrentSource { get; internal set; }
        public bool EventValid { get; internal set; }
        public int ChangeSeconds { get; internal set; }
        public int SecondsToEvent { get; internal set; }
        public ushort EventGpsWeek { get; internal set; }
        public ushort EventGpsDay { get; internal set; }
        public byte EventSource { get; internal set; }
    }

    internal sealed class GnssTimePulseSample
    {
        public long UpdatedMonotonicTicks { get; internal set; }
        public uint ITowMilliseconds { get; internal set; }
        public uint TowSubMilliseconds { get; internal set; }
        public ushort Week { get; internal set; }
        public int QuantizationErrorPicoseconds { get; internal set; }
        public bool QuantizationErrorValid { get; internal set; }
        public int? PreviousQuantizationErrorPicoseconds { get; internal set; }
        public bool PreviousPulseWasConsecutive { get; internal set; }
        public bool TimeBaseIsUtc { get; internal set; }
        public bool UtcAvailable { get; internal set; }
        public bool FlagsValid { get; internal set; }
        public bool TimingReferenceValid { get; internal set; }
        public byte Flags { get; internal set; }
        public byte ReferenceInfo { get; internal set; }

        public bool IsOneHertzGpsPulse
        {
            get
            {
                // iTOW association with NAV-PVT is unambiguous only for a
                // one-hertz GPS-grid pulse. Other grids require an explicit
                // time-scale conversion and must never initialize the PHC by
                // pretending their towMS is GPS iTOW.
                return !TimeBaseIsUtc &&
                    (ReferenceInfo & 0x0f) == 0 &&
                    TowSubMilliseconds == 0 &&
                    ITowMilliseconds % 1000u == 0 &&
                    PreviousPulseWasConsecutive;
            }
        }
    }

    internal sealed class GnssSurveySample
    {
        public long UpdatedMonotonicTicks { get; internal set; }
        public uint DurationSeconds { get; internal set; }
        public uint Observations { get; internal set; }
        public double MeanAccuracyMillimeters { get; internal set; }
        public bool Valid { get; internal set; }
        public bool Active { get; internal set; }
        public bool Completed { get; internal set; }
        public bool IsWellFormed { get; internal set; }
        public string ValidationError { get; internal set; }
    }

    internal sealed class GnssRfSample
    {
        public long UpdatedMonotonicTicks { get; internal set; }
        public byte BlockCount { get; internal set; }
        public byte AntennaStatus { get; internal set; }
        public byte AntennaPower { get; internal set; }
        public bool IsWellFormed { get; internal set; }
        public string ValidationError { get; internal set; }
    }

    internal sealed class GnssAssociatedEpoch
    {
        public ulong Sequence { get; internal set; }
        public long UpdatedMonotonicTicks { get; internal set; }
        public uint ITowMilliseconds { get; internal set; }
        public GnssNavigationSample Navigation { get; internal set; }
        public GnssTimePulseSample TimePulse { get; internal set; }
        public GnssLeapSecondSample LeapSeconds { get; internal set; }
        public GnssSurveySample Survey { get; internal set; }

        public bool IsCoherentForPhcInitialization
        {
            get
            {
                return Navigation != null && Navigation.FixOk &&
                    Navigation.Utc != null && Navigation.Utc.IsValid &&
                    !Navigation.Utc.RepresentsLeapSecond &&
                    TimePulse != null &&
                    TimePulse.FlagsValid &&
                    TimePulse.TimingReferenceValid &&
                    TimePulse.IsOneHertzGpsPulse &&
                    LeapSeconds != null && LeapSeconds.IsValid &&
                    LeapSeconds.CurrentOffsetValid &&
                    !IsNearLeapBoundary &&
                    ComponentsAreContemporaneous;
            }
        }

        private bool IsNearLeapBoundary
        {
            get
            {
                return LeapSeconds.EventValid &&
                    LeapSeconds.ChangeSeconds != 0 &&
                    Math.Abs((long)LeapSeconds.SecondsToEvent) <= 5;
            }
        }

        private bool ComponentsAreContemporaneous
        {
            get
            {
                long minimum = Math.Min(Navigation.UpdatedMonotonicTicks,
                    Math.Min(TimePulse.UpdatedMonotonicTicks,
                        LeapSeconds.UpdatedMonotonicTicks));
                long maximum = Math.Max(Navigation.UpdatedMonotonicTicks,
                    Math.Max(TimePulse.UpdatedMonotonicTicks,
                        LeapSeconds.UpdatedMonotonicTicks));
                return minimum > 0 && maximum >= minimum &&
                    (maximum - minimum) / (double)Stopwatch.Frequency <= 2.0;
            }
        }
    }

    internal sealed class NativeGnssSessionSnapshot
    {
        public GnssAssociatedEpoch LatestEpoch { get; internal set; }
        public GnssAssociatedEpoch LatestAssociatedEpoch { get; internal set; }
        public GnssAssociatedEpoch LatestCoherentEpoch { get; internal set; }
        public GnssSurveySample LatestSurvey { get; internal set; }
        public GnssRfSample LatestRf { get; internal set; }
        public ulong ValidFrameCount { get; internal set; }
        public ulong RejectedMessageCount { get; internal set; }
        public long UpdatedMonotonicTicks { get; internal set; }
    }

    internal sealed class GnssEpochAssembler
    {
        private sealed class EpochRecord
        {
            public ulong Sequence;
            public uint ITow;
            public GnssNavigationSample Navigation;
            public GnssTimePulseSample TimePulse;
            public GnssLeapSecondSample LeapSeconds;
        }

        private readonly object gate = new object();
        private readonly UbxStreamParser parser = new UbxStreamParser();
        private readonly Dictionary<uint, EpochRecord> epochs =
            new Dictionary<uint, EpochRecord>();
        private ulong sequence;
        private ulong validFrames;
        private ulong rejectedMessages;
        private bool havePreviousTimePulse;
        private int previousQuantizationError;
        private bool previousQuantizationErrorValid;
        private uint previousPulseITow;
        private ushort previousPulseWeek;
        private bool previousPulseUtcTimeBase;
        private byte previousPulseReferenceInfo;
        private GnssSurveySample latestSurvey;
        private GnssRfSample latestRf;
        private long updatedMonotonicTicks;

        public void Feed(byte[] bytes)
        {
            IList<UbxFrame> frames = parser.Feed(bytes);
            lock (gate)
            {
                bool timingStateChanged = false;
                foreach (UbxFrame frame in frames)
                {
                    ulong before = sequence;
                    validFrames++;
                    if (!Update(frame))
                        rejectedMessages++;
                    if (sequence != before)
                        timingStateChanged = true;
                }
                if (timingStateChanged)
                    updatedMonotonicTicks = Stopwatch.GetTimestamp();
                Trim();
            }
        }

        public NativeGnssSessionSnapshot Snapshot()
        {
            lock (gate)
            {
                EpochRecord latest = null;
                EpochRecord latestAssociated = null;
                EpochRecord coherent = null;
                foreach (EpochRecord item in epochs.Values)
                {
                    if (latest == null || item.Sequence > latest.Sequence)
                        latest = item;
                    if (item.Navigation != null && item.TimePulse != null &&
                        (latestAssociated == null ||
                         item.Sequence > latestAssociated.Sequence))
                        latestAssociated = item;
                    GnssAssociatedEpoch candidate = Copy(item);
                    if (candidate.IsCoherentForPhcInitialization &&
                        (coherent == null || item.Sequence > coherent.Sequence))
                        coherent = item;
                }
                return new NativeGnssSessionSnapshot
                {
                    LatestEpoch = Copy(latest),
                    LatestAssociatedEpoch = Copy(latestAssociated),
                    LatestCoherentEpoch = Copy(coherent),
                    LatestSurvey = Copy(latestSurvey),
                    LatestRf = Copy(latestRf),
                    ValidFrameCount = validFrames,
                    RejectedMessageCount = rejectedMessages,
                    UpdatedMonotonicTicks = updatedMonotonicTicks
                };
            }
        }

        private bool Update(UbxFrame frame)
        {
            if (frame.MessageClass == 0x01 && frame.MessageId == 0x07)
            {
                GnssNavigationSample sample;
                if (!TryParseNavigation(frame.Payload, out sample))
                    return false;
                EpochRecord record = Get(sample.ITowMilliseconds);
                sample.UpdatedMonotonicTicks = Stopwatch.GetTimestamp();
                record.Navigation = sample;
                Touch(record);
                return true;
            }
            if (frame.MessageClass == 0x01 && frame.MessageId == 0x26)
            {
                GnssLeapSecondSample sample;
                if (!TryParseLeapSeconds(frame.Payload, out sample))
                    return false;
                EpochRecord record = Get(sample.ITowMilliseconds);
                sample.UpdatedMonotonicTicks = Stopwatch.GetTimestamp();
                record.LeapSeconds = sample;
                Touch(record);
                return true;
            }
            if (frame.MessageClass == 0x0d && frame.MessageId == 0x01)
            {
                GnssTimePulseSample sample;
                if (!TryParseTimePulse(frame.Payload, out sample))
                    return false;
                sample.UpdatedMonotonicTicks = Stopwatch.GetTimestamp();
                bool consecutive = havePreviousTimePulse &&
                    PulsesAreConsecutive(previousPulseWeek,
                        previousPulseITow, sample.Week,
                        sample.ITowMilliseconds) &&
                    previousPulseUtcTimeBase == sample.TimeBaseIsUtc &&
                    previousPulseReferenceInfo == sample.ReferenceInfo;
                sample.PreviousQuantizationErrorPicoseconds = consecutive ?
                    (previousQuantizationErrorValid ?
                        previousQuantizationError : 0) : (int?)null;
                sample.PreviousPulseWasConsecutive = consecutive;
                havePreviousTimePulse = true;
                previousQuantizationError =
                    sample.QuantizationErrorPicoseconds;
                previousQuantizationErrorValid =
                    sample.QuantizationErrorValid;
                previousPulseITow = sample.ITowMilliseconds;
                previousPulseWeek = sample.Week;
                previousPulseUtcTimeBase = sample.TimeBaseIsUtc;
                previousPulseReferenceInfo = sample.ReferenceInfo;
                // TIM-TP describes the following one-hertz pulse. Associate
                // it with the NAV epoch one second earlier, as upstream does
                // when it subtracts one from the reported pulse time.
                EpochRecord record = Get(PreviousPulseEpochITow(
                    sample.ITowMilliseconds));
                record.TimePulse = sample;
                Touch(record);
                return true;
            }
            if (frame.MessageClass == 0x0d && frame.MessageId == 0x04)
            {
                GnssSurveySample sample;
                if (!TryParseSurvey(frame.Payload, out sample))
                    return false;
                sample.UpdatedMonotonicTicks = Stopwatch.GetTimestamp();
                latestSurvey = sample;
                sequence++;
                return true;
            }
            if (frame.MessageClass == 0x0a && frame.MessageId == 0x38)
            {
                GnssRfSample sample;
                if (!TryParseRf(frame.Payload, out sample))
                    return false;
                sample.UpdatedMonotonicTicks = Stopwatch.GetTimestamp();
                latestRf = sample;
                sequence++;
                return true;
            }
            // A well-formed but unrelated UBX message is not a rejection.
            return true;
        }

        private EpochRecord Get(uint iTow)
        {
            EpochRecord result;
            if (!epochs.TryGetValue(iTow, out result))
            {
                result = new EpochRecord { ITow = iTow };
                epochs.Add(iTow, result);
            }
            return result;
        }

        private static uint PreviousPulseEpochITow(uint pulseITow)
        {
            const uint millisecondsPerWeek = 604800000u;
            return pulseITow >= 1000u ? pulseITow - 1000u :
                pulseITow + millisecondsPerWeek - 1000u;
        }

        private static bool PulsesAreConsecutive(ushort previousWeek,
            uint previousITow, ushort currentWeek, uint currentITow)
        {
            const uint millisecondsPerWeek = 604800000u;
            if (currentWeek == previousWeek)
                return previousITow <= millisecondsPerWeek - 1000u &&
                    currentITow == previousITow + 1000u;
            return currentWeek == unchecked((ushort)(previousWeek + 1)) &&
                previousITow >= millisecondsPerWeek - 1000u &&
                currentITow == previousITow + 1000u - millisecondsPerWeek;
        }

        private void Touch(EpochRecord record)
        {
            sequence++;
            record.Sequence = sequence;
        }

        private void Trim()
        {
            while (epochs.Count > 16)
            {
                uint oldestKey = 0;
                ulong oldestSequence = ulong.MaxValue;
                foreach (KeyValuePair<uint, EpochRecord> item in epochs)
                {
                    if (item.Value.Sequence < oldestSequence)
                    {
                        oldestKey = item.Key;
                        oldestSequence = item.Value.Sequence;
                    }
                }
                epochs.Remove(oldestKey);
            }
        }

        private GnssAssociatedEpoch Copy(EpochRecord value)
        {
            if (value == null)
                return null;
            return new GnssAssociatedEpoch
            {
                Sequence = value.Sequence,
                UpdatedMonotonicTicks = Math.Max(
                    value.Navigation == null ? 0 :
                        value.Navigation.UpdatedMonotonicTicks,
                    Math.Max(value.TimePulse == null ? 0 :
                        value.TimePulse.UpdatedMonotonicTicks,
                        value.LeapSeconds == null ? 0 :
                            value.LeapSeconds.UpdatedMonotonicTicks)),
                ITowMilliseconds = value.ITow,
                Navigation = Copy(value.Navigation),
                TimePulse = Copy(value.TimePulse),
                LeapSeconds = Copy(value.LeapSeconds),
                Survey = Copy(latestSurvey)
            };
        }

        private static GnssNavigationSample Copy(GnssNavigationSample value)
        {
            return value == null ? null : new GnssNavigationSample
            {
                UpdatedMonotonicTicks = value.UpdatedMonotonicTicks,
                ITowMilliseconds = value.ITowMilliseconds,
                FixType = value.FixType,
                FixOk = value.FixOk,
                Satellites = value.Satellites,
                Utc = value.Utc == null ? null : new GnssUtcSample
                {
                    IsValid = value.Utc.IsValid,
                    ValidationError = value.Utc.ValidationError,
                    Utc = value.Utc.Utc,
                    UnixSeconds = value.Utc.UnixSeconds,
                    Nanoseconds = value.Utc.Nanoseconds,
                    RepresentsLeapSecond = value.Utc.RepresentsLeapSecond,
                    AccuracyNanoseconds = value.Utc.AccuracyNanoseconds
                }
            };
        }

        private static GnssLeapSecondSample Copy(GnssLeapSecondSample value)
        {
            return value == null ? null : new GnssLeapSecondSample
            {
                UpdatedMonotonicTicks = value.UpdatedMonotonicTicks,
                ITowMilliseconds = value.ITowMilliseconds,
                IsValid = value.IsValid,
                ValidationError = value.ValidationError,
                CurrentOffsetValid = value.CurrentOffsetValid,
                CurrentOffsetSeconds = value.CurrentOffsetSeconds,
                CurrentSource = value.CurrentSource,
                EventValid = value.EventValid,
                ChangeSeconds = value.ChangeSeconds,
                SecondsToEvent = value.SecondsToEvent,
                EventGpsWeek = value.EventGpsWeek,
                EventGpsDay = value.EventGpsDay,
                EventSource = value.EventSource
            };
        }

        private static GnssTimePulseSample Copy(GnssTimePulseSample value)
        {
            return value == null ? null : new GnssTimePulseSample
            {
                UpdatedMonotonicTicks = value.UpdatedMonotonicTicks,
                ITowMilliseconds = value.ITowMilliseconds,
                TowSubMilliseconds = value.TowSubMilliseconds,
                Week = value.Week,
                QuantizationErrorPicoseconds =
                    value.QuantizationErrorPicoseconds,
                QuantizationErrorValid = value.QuantizationErrorValid,
                PreviousQuantizationErrorPicoseconds =
                    value.PreviousQuantizationErrorPicoseconds,
                PreviousPulseWasConsecutive =
                    value.PreviousPulseWasConsecutive,
                TimeBaseIsUtc = value.TimeBaseIsUtc,
                UtcAvailable = value.UtcAvailable,
                FlagsValid = value.FlagsValid,
                TimingReferenceValid = value.TimingReferenceValid,
                Flags = value.Flags,
                ReferenceInfo = value.ReferenceInfo
            };
        }

        private static GnssSurveySample Copy(GnssSurveySample value)
        {
            return value == null ? null : new GnssSurveySample
            {
                UpdatedMonotonicTicks = value.UpdatedMonotonicTicks,
                DurationSeconds = value.DurationSeconds,
                Observations = value.Observations,
                MeanAccuracyMillimeters = value.MeanAccuracyMillimeters,
                Valid = value.Valid,
                Active = value.Active,
                Completed = value.Completed,
                IsWellFormed = value.IsWellFormed,
                ValidationError = value.ValidationError
            };
        }

        private static GnssRfSample Copy(GnssRfSample value)
        {
            return value == null ? null : new GnssRfSample
            {
                UpdatedMonotonicTicks = value.UpdatedMonotonicTicks,
                BlockCount = value.BlockCount,
                AntennaStatus = value.AntennaStatus,
                AntennaPower = value.AntennaPower,
                IsWellFormed = value.IsWellFormed,
                ValidationError = value.ValidationError
            };
        }

        private static bool TryParseNavigation(byte[] payload,
            out GnssNavigationSample result)
        {
            result = null;
            if (payload == null || payload.Length < 92)
                return false;
            uint iTow = ReadUInt32(payload, 0);
            if (!ValidITow(iTow))
                return false;

            GnssUtcSample utc = ParseUtc(payload);
            byte fixType = payload[20];
            bool fixOk = (payload[21] & 0x01) != 0 && fixType >= 2 &&
                fixType <= 5 && payload[23] > 3 && utc.IsValid;
            result = new GnssNavigationSample
            {
                ITowMilliseconds = iTow,
                FixType = fixType,
                FixOk = fixOk,
                Satellites = payload[23],
                Utc = utc
            };
            return true;
        }

        private static GnssUtcSample ParseUtc(byte[] payload)
        {
            GnssUtcSample result = new GnssUtcSample
            {
                AccuracyNanoseconds = ReadUInt32(payload, 12)
            };
            byte valid = payload[11];
            if ((valid & 0x07) != 0x07)
                return InvalidUtc(result,
                    "NAV-PVT date, time, and fully-resolved flags are required.");
            if ((valid & 0xf0) != 0)
                return InvalidUtc(result, "NAV-PVT UTC validity contains reserved bits.");
            byte flags2 = payload[22];
            if ((flags2 & 0x20) != 0 && (flags2 & 0xc0) != 0xc0)
                return InvalidUtc(result,
                    "NAV-PVT confirmed-available lacks confirmed date or time.");

            int year = ReadUInt16(payload, 4);
            int month = payload[6];
            int day = payload[7];
            int hour = payload[8];
            int minute = payload[9];
            int second = payload[10];
            int nano = ReadInt32(payload, 16);
            if (year < 1980 || year > 2099 || month < 1 || month > 12 ||
                hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
                second < 0 || second > 60 || nano <= -1000000000 ||
                nano >= 1000000000)
                return InvalidUtc(result, "NAV-PVT UTC fields are outside their ranges.");
            bool leapSecond = second == 60;
            if (leapSecond && !((month == 6 && day == 30) ||
                (month == 12 && day == 31)) ||
                (leapSecond && (hour != 23 || minute != 59)))
                return InvalidUtc(result,
                    "NAV-PVT second 60 is not at a valid UTC leap boundary.");

            try
            {
                DateTime utc = new DateTime(year, month, day, hour, minute,
                    Math.Min(second, 59), DateTimeKind.Utc);
                if (leapSecond)
                    utc = utc.AddSeconds(1);
                DateTime unixEpoch = new DateTime(1970, 1, 1, 0, 0, 0,
                    DateTimeKind.Utc);
                long unixSeconds = checked((utc.Ticks - unixEpoch.Ticks) /
                    TimeSpan.TicksPerSecond);
                while (nano < 0)
                {
                    unixSeconds--;
                    nano += 1000000000;
                }
                result.IsValid = true;
                result.ValidationError = null;
                result.Utc = unixEpoch.AddSeconds(unixSeconds)
                    .AddTicks(nano / 100);
                result.UnixSeconds = unixSeconds;
                result.Nanoseconds = nano;
                result.RepresentsLeapSecond = leapSecond;
                return result;
            }
            catch (ArgumentOutOfRangeException)
            {
                return InvalidUtc(result, "NAV-PVT contains an invalid UTC date.");
            }
            catch (OverflowException)
            {
                return InvalidUtc(result, "NAV-PVT UTC conversion overflowed.");
            }
        }

        private static GnssUtcSample InvalidUtc(GnssUtcSample value,
            string error)
        {
            value.IsValid = false;
            value.ValidationError = error;
            return value;
        }

        private static bool TryParseLeapSeconds(byte[] payload,
            out GnssLeapSecondSample result)
        {
            result = null;
            if (payload == null || payload.Length != 24)
                return false;
            uint iTow = ReadUInt32(payload, 0);
            if (!ValidITow(iTow))
                return false;

            byte version = payload[4];
            byte currentSource = payload[8];
            int current = unchecked((sbyte)payload[9]);
            byte eventSource = payload[10];
            int change = unchecked((sbyte)payload[11]);
            int secondsToEvent = ReadInt32(payload, 12);
            ushort eventWeek = ReadUInt16(payload, 16);
            ushort eventDay = ReadUInt16(payload, 18);
            byte valid = payload[23];
            bool currentValid = (valid & 0x01) != 0;
            bool eventValid = (valid & 0x02) != 0;
            string error = null;
            if (version != 0)
                error = "NAV-TIMELS version is not zero.";
            else if (payload[5] != 0 || payload[6] != 0 ||
                payload[7] != 0 || payload[20] != 0 ||
                payload[21] != 0 || payload[22] != 0)
                error = "NAV-TIMELS reserved fields are not zero.";
            else if ((valid & 0xfc) != 0)
                error = "NAV-TIMELS validity contains reserved bits.";
            else if (currentValid && (current < 0 || current > 64))
                error = "NAV-TIMELS current GPS-UTC offset is implausible.";
            else if (currentValid && !ValidCurrentLeapSource(currentSource))
                error = "NAV-TIMELS current source is invalid.";
            else if (eventValid && !ValidEventLeapSource(eventSource))
                error = "NAV-TIMELS event source is invalid.";
            else if (eventValid && (change < -1 || change > 1))
                error = "NAV-TIMELS leap change must be -1, 0, or +1.";
            else if (eventValid && (secondsToEvent < -315576000 ||
                secondsToEvent > 315576000))
                error = "NAV-TIMELS event countdown is implausible.";
            else if (eventValid && !ValidEventDay(eventSource, eventDay))
                error = "NAV-TIMELS event GPS week/day is invalid.";

            result = new GnssLeapSecondSample
            {
                ITowMilliseconds = iTow,
                IsValid = error == null,
                ValidationError = error,
                CurrentOffsetValid = currentValid && error == null,
                CurrentOffsetSeconds = current,
                CurrentSource = currentSource,
                EventValid = eventValid && error == null,
                ChangeSeconds = change,
                SecondsToEvent = secondsToEvent,
                EventGpsWeek = eventWeek,
                EventGpsDay = eventDay,
                EventSource = eventSource
            };
            return true;
        }

        private static bool ValidCurrentLeapSource(byte source)
        {
            return source <= 8 || source == 255;
        }

        private static bool ValidEventLeapSource(byte source)
        {
            return source == 0 || (source >= 2 && source <= 7);
        }

        private static bool ValidEventDay(byte source, ushort day)
        {
            // BeiDou numbers Sunday as 0 through Saturday as 6.  GPS and
            // Galileo use 1 through 7.  Source 0 carries no scheduled event.
            if (source == 0)
                return day <= 7;
            return source == 4 ? day <= 6 : day >= 1 && day <= 7;
        }

        private static bool TryParseTimePulse(byte[] payload,
            out GnssTimePulseSample result)
        {
            result = null;
            if (payload == null || payload.Length != 16)
                return false;
            uint iTow = ReadUInt32(payload, 0);
            if (!ValidITow(iTow))
                return false;
            int qErr = ReadInt32(payload, 8);
            byte flags = payload[14];
            byte reference = payload[15];
            bool utcTimeBase = (flags & 0x01) != 0;
            bool utcAvailable = (flags & 0x02) != 0;
            int referenceKind = utcTimeBase ?
                (reference >> 4) & 0x0f : reference & 0x0f;
            bool referenceValid = utcTimeBase ?
                utcAvailable && (referenceKind <= 7 || referenceKind == 15) :
                referenceKind <= 3 || referenceKind == 15;
            result = new GnssTimePulseSample
            {
                ITowMilliseconds = iTow,
                TowSubMilliseconds = ReadUInt32(payload, 4),
                QuantizationErrorPicoseconds = qErr,
                // Match upstream oscillatord: aberrant qErr is not used.
                QuantizationErrorValid = qErr >= -5000 && qErr <= 5000 &&
                    (flags & 0x10) == 0,
                Week = ReadUInt16(payload, 12),
                TimeBaseIsUtc = utcTimeBase,
                UtcAvailable = utcAvailable,
                // UBX-TIM-TP defines bits 0..4 only; bits 5..7 are reserved.
                FlagsValid = (flags & 0xe0) == 0 &&
                    ((flags >> 2) & 0x03) != 0x03,
                TimingReferenceValid = referenceValid &&
                    (flags & 0xe0) == 0 &&
                    ((flags >> 2) & 0x03) != 0x03,
                Flags = flags,
                ReferenceInfo = reference
            };
            return true;
        }

        private static bool TryParseSurvey(byte[] payload,
            out GnssSurveySample result)
        {
            result = null;
            if (payload == null || payload.Length != 28)
                return false;
            byte valid = payload[24];
            byte active = payload[25];
            string error = valid > 1 || active > 1 ?
                "TIM-SVIN valid and active fields must be Boolean." : null;
            uint duration = ReadUInt32(payload, 0);
            result = new GnssSurveySample
            {
                DurationSeconds = duration,
                MeanAccuracyMillimeters = Math.Sqrt(ReadUInt32(payload, 16)),
                Observations = ReadUInt32(payload, 20),
                Valid = valid == 1,
                Active = active == 1,
                Completed = error == null && valid == 1 && active == 0 &&
                    duration >= 1200,
                IsWellFormed = error == null,
                ValidationError = error
            };
            return true;
        }

        private static bool TryParseRf(byte[] payload,
            out GnssRfSample result)
        {
            result = null;
            if (payload == null || payload.Length < 4)
                return false;
            byte version = payload[0];
            byte blockCount = payload[1];
            int required = 4 + blockCount * 24;
            string error = null;
            if (version != 0)
                error = "MON-RF version is not zero.";
            else if (payload[2] != 0 || payload[3] != 0)
                error = "MON-RF reserved fields are not zero.";
            else if (blockCount == 0 || payload.Length != required)
                error = "MON-RF block count does not match its payload.";

            byte antennaStatus = 5;
            byte antennaPower = 5;
            if (error == null)
            {
                for (int block = 0; block < blockCount; ++block)
                {
                    int offset = 4 + block * 24;
                    byte status = payload[offset + 2];
                    byte power = payload[offset + 3];
                    if (status > 4 || power > 2)
                    {
                        error = "MON-RF antenna state is outside the documented range.";
                        break;
                    }
                    // Match upstream: retain the worst RF block instead of
                    // hiding an open/short condition behind a healthy block.
                    if (antennaStatus == 5 ||
                        (antennaStatus == 2 && status != 2))
                        antennaStatus = status;
                    if (antennaPower == 5 ||
                        (antennaPower == 2 && power != 2))
                        antennaPower = power;
                }
            }
            result = new GnssRfSample
            {
                BlockCount = blockCount,
                AntennaStatus = antennaStatus,
                AntennaPower = antennaPower,
                IsWellFormed = error == null,
                ValidationError = error
            };
            return true;
        }

        private static bool ValidITow(uint value)
        {
            // u-blox permits a small overrun around week rollover.
            return value <= 604800999u;
        }

        private static ushort ReadUInt16(byte[] value, int offset)
        {
            return (ushort)(value[offset] | (value[offset + 1] << 8));
        }

        private static uint ReadUInt32(byte[] value, int offset)
        {
            return (uint)(value[offset] | (value[offset + 1] << 8) |
                (value[offset + 2] << 16) | (value[offset + 3] << 24));
        }

        private static int ReadInt32(byte[] value, int offset)
        {
            return unchecked((int)ReadUInt32(value, offset));
        }
    }

    internal sealed class NativeGnssSessionManager
    {
        private readonly TimeCardClient client;
        private readonly uint port;
        private readonly uint baud;
        private readonly GnssEpochAssembler assembler = new GnssEpochAssembler();
        private readonly Action<byte[]> rawObserver;
        private uint configuredHostBaud;

        public NativeGnssSessionManager(TimeCardClient activeClient,
            uint uartPort, uint uartBaud)
            : this(activeClient, uartPort, uartBaud, null)
        {
        }

        public NativeGnssSessionManager(TimeCardClient activeClient,
            uint uartPort, uint uartBaud, Action<byte[]> observer)
        {
            if (activeClient == null)
                throw new ArgumentNullException("activeClient");
            if (uartPort > 1)
                throw new ArgumentOutOfRangeException("uartPort");
            if (uartBaud == 0)
                throw new ArgumentOutOfRangeException("uartBaud");
            client = activeClient;
            port = uartPort;
            baud = uartBaud;
            rawObserver = observer;
        }

        public async Task RunPassiveAsync(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                UartReadResult read = client.ReadUart(port, 256u, 20u);
                if (read.Data.Length != 0)
                {
                    ObserveRaw(read.Data);
                    assembler.Feed(read.Data);
                }
                else
                    await Task.Delay(5, token).ConfigureAwait(false);
            }
        }

        public NativeGnssSessionSnapshot Snapshot()
        {
            return assembler.Snapshot();
        }

        internal void FeedForTest(byte[] bytes)
        {
            assembler.Feed(bytes);
        }

        public void ConfigureUart(bool allowHardwareWrite)
        {
            RequireHardwareWrite(allowHardwareWrite);
            client.ConfigureUart(port, baud);
            configuredHostBaud = baud;
        }

        public async Task<uint> DetectReceiverBaudAsync(
            bool allowHardwareWrite, CancellationToken token)
        {
            RequireHardwareWrite(allowHardwareWrite);
            uint[] candidates = { baud, 115200u, 38400u, 9600u, 230400u,
                460800u };
            HashSet<uint> attempted = new HashSet<uint>();
            foreach (uint candidate in candidates)
            {
                if (!attempted.Add(candidate))
                    continue;
                client.ConfigureUart(port, candidate);
                configuredHostBaud = candidate;
                // Drain stale bytes captured with a different divisor.
                for (int drain = 0; drain < 3; ++drain)
                    client.ReadUart(port, 256u, 5u);
                SendComplete(UbxProtocol.BuildFrame(0x0a, 0x04,
                    new byte[0]));
                UbxStreamParser probeParser = new UbxStreamParser();
                long started = Stopwatch.GetTimestamp();
                while (!token.IsCancellationRequested &&
                    (Stopwatch.GetTimestamp() - started) /
                        (double)Stopwatch.Frequency <= 0.4)
                {
                    UartReadResult read = client.ReadUart(port, 256u, 30u);
                    if (read.Data.Length != 0)
                    {
                        ObserveRaw(read.Data);
                        assembler.Feed(read.Data);
                        foreach (UbxFrame frame in probeParser.Feed(read.Data))
                        {
                            if (frame.MessageClass == 0x0a &&
                                frame.MessageId == 0x04)
                                return candidate;
                        }
                    }
                    else
                        await Task.Delay(5, token).ConfigureAwait(false);
                }
            }
            token.ThrowIfCancellationRequested();
            throw new InvalidOperationException(
                "The u-blox receiver did not answer UBX-MON-VER at the configured or standard UART rates.");
        }

        public void ApplyConfiguration(
            IEnumerable<UbxConfigurationValue> values, bool persist,
            bool allowHardwareWrite)
        {
            RequireHardwareWrite(allowHardwareWrite);
            SendComplete(UbxProtocol.BuildConfigurationFrame(values, persist));
        }

        public async Task ApplyConfigurationAndAwaitAcknowledgementAsync(
            IEnumerable<UbxConfigurationValue> values, bool persist,
            bool allowHardwareWrite, CancellationToken token)
        {
            RequireHardwareWrite(allowHardwareWrite);
            byte[] request = UbxProtocol.BuildConfigurationFrame(values,
                persist);
            SendComplete(request);
            UbxStreamParser acknowledgementParser = new UbxStreamParser();
            long started = Stopwatch.GetTimestamp();
            bool triedTargetBaud = configuredHostBaud == baud;
            while (!token.IsCancellationRequested)
            {
                UartReadResult read = client.ReadUart(port, 256u, 50u);
                if (read.Data.Length != 0)
                {
                    ObserveRaw(read.Data);
                    assembler.Feed(read.Data);
                    foreach (UbxFrame frame in acknowledgementParser.Feed(
                        read.Data))
                    {
                        if (frame.MessageClass != 0x05 ||
                            frame.Payload.Length != 2 ||
                            frame.Payload[0] != 0x06 ||
                            frame.Payload[1] != 0x8a)
                            continue;
                        if (frame.MessageId == 0x01)
                            return;
                        if (frame.MessageId == 0x00)
                            throw new InvalidOperationException(
                                "The GNSS receiver rejected CFG-VALSET with UBX-ACK-NAK.");
                    }
                }
                if ((Stopwatch.GetTimestamp() - started) /
                    (double)Stopwatch.Frequency > 2.0)
                    throw new TimeoutException(
                        "The GNSS receiver did not acknowledge CFG-VALSET within two seconds.");
                if (!triedTargetBaud &&
                    (Stopwatch.GetTimestamp() - started) /
                        (double)Stopwatch.Frequency > 0.35)
                {
                    // Receiver generations differ on whether the CFG-VALSET
                    // ACK is emitted before or after a UART baud transition.
                    client.ConfigureUart(port, baud);
                    configuredHostBaud = baud;
                    triedTargetBaud = true;
                }
                if (read.Data.Length == 0)
                    await Task.Delay(5, token).ConfigureAwait(false);
            }
            token.ThrowIfCancellationRequested();
        }

        public void ResetReceiver(UbxResetKind kind, bool allowHardwareWrite)
        {
            RequireHardwareWrite(allowHardwareWrite);
            SendComplete(UbxProtocol.BuildResetFrame(kind));
        }

        private void SendComplete(byte[] frame)
        {
            UartWriteResult result = client.WriteUart(port, frame, 2000u);
            if (result.BytesTransferred != (uint)frame.Length)
                throw new InvalidOperationException(
                    "The complete UBX frame was not transmitted.");
        }

        private void ObserveRaw(byte[] bytes)
        {
            if (rawObserver != null)
                rawObserver(bytes);
        }

        private static void RequireHardwareWrite(bool allowed)
        {
            if (!allowed)
                throw new InvalidOperationException(
                    "GNSS hardware writes are disabled. Pass allowHardwareWrite=true only for an explicit operator action.");
        }
    }

    internal enum PhcStartupState
    {
        WaitingForCoherentGnss,
        WaitingForInitialSetAcknowledgement,
        WaitingForPairedPhase,
        WaitingForRoughPhaseAcknowledgement,
        Settling,
        WaitingForFinalSetAcknowledgement,
        Verifying,
        Complete,
        Faulted
    }

    internal enum PhcStartupActionKind
    {
        None,
        SetPhcUtcAtNextPulse,
        AdjustPhcPhase,
        Wait,
        Complete,
        Fault
    }

    internal sealed class PhcStartupObservation
    {
        public NativeGnssSessionSnapshot Gnss { get; set; }
        public bool HasPairedPhase { get; set; }
        public long PhaseErrorNanoseconds { get; set; }
        public bool HasPhcUtc { get; set; }
        public DateTime PhcUtc { get; set; }
        public long MonotonicMilliseconds { get; set; }
        public long GnssAgeMilliseconds { get; set; }
        public uint ReferenceCounter { get; set; }
    }

    internal sealed class PhcStartupRecommendation
    {
        public PhcStartupState State { get; internal set; }
        public PhcStartupActionKind Action { get; internal set; }
        public long AcknowledgementToken { get; internal set; }
        public string Reason { get; internal set; }
        public long TargetUnixSeconds { get; internal set; }
        public int TargetNanoseconds { get; internal set; }
        public long PhaseAdjustmentNanoseconds { get; internal set; }
        public uint SourceITowMilliseconds { get; internal set; }
        public ulong SourceEpochSequence { get; internal set; }
        public uint SourceReferenceCounter { get; internal set; }
        public bool PreservePhcNanoseconds { get; internal set; }
    }

    /*
     * Pure startup planner.  It mirrors upstream's safe order:
     * GNSS time -> PHC, paired-PPS rough phase jump, settling interval,
     * GNSS time -> PHC again, then verification.  It performs no I/O and
     * requires an acknowledgement token after each external mutation.
     */
    internal sealed class PhcStartupAlignmentPlanner
    {
        private const long MaximumSinglePhaseAdjustmentNanoseconds = 499999999;
        private readonly int phaseSign;
        private readonly long settlingMilliseconds;
        private PhcStartupState state =
            PhcStartupState.WaitingForCoherentGnss;
        private long tokenCounter;
        private long pendingToken;
        private long settlingStarted;
        private string fault;
        private ulong initialEpochSequence;
        private long pendingTargetUnixSeconds;
        private long finalTargetUnixSeconds;
        private int finalTargetNanoseconds;
        private long finalSetAcknowledgedMilliseconds;
        private uint finalSetReferenceCounter;
        private ulong pendingSourceEpochSequence;
        private ulong finalSourceEpochSequence;

        public PhcStartupAlignmentPlanner(int activePhaseSign,
            long activeSettlingMilliseconds)
        {
            if (activePhaseSign != 1 && activePhaseSign != -1)
                throw new ArgumentOutOfRangeException("activePhaseSign");
            if (activeSettlingMilliseconds < 0 ||
                activeSettlingMilliseconds > 60000)
                throw new ArgumentOutOfRangeException(
                    "activeSettlingMilliseconds");
            phaseSign = activePhaseSign;
            settlingMilliseconds = activeSettlingMilliseconds;
        }

        public PhcStartupState State { get { return state; } }

        public PhcStartupRecommendation Observe(PhcStartupObservation value)
        {
            if (value == null)
                throw new ArgumentNullException("value");
            if (value.MonotonicMilliseconds < 0)
                return Fail("The monotonic observation time is invalid.");
            if (state == PhcStartupState.Faulted)
                return Result(PhcStartupActionKind.Fault, fault);
            if (state == PhcStartupState.Complete)
                return Result(PhcStartupActionKind.Complete,
                    "PHC startup alignment is complete.");
            if (pendingToken != 0)
                return Result(PhcStartupActionKind.Wait,
                    "Waiting for acknowledgement of the recommended hardware action.");
            if (value.GnssAgeMilliseconds < 0 ||
                value.GnssAgeMilliseconds > 2000)
                return Result(PhcStartupActionKind.Wait,
                    "Waiting for a fresh GNSS epoch.");

            GnssAssociatedEpoch epoch = value.Gnss == null ? null :
                value.Gnss.LatestCoherentEpoch;
            if (epoch == null || !epoch.IsCoherentForPhcInitialization)
            {
                return Result(PhcStartupActionKind.Wait,
                    "Waiting for one iTOW-associated NAV-PVT, TIM-TP, and valid NAV-TIMELS epoch.");
            }

            if (state == PhcStartupState.WaitingForCoherentGnss)
            {
                state = PhcStartupState.WaitingForInitialSetAcknowledgement;
                initialEpochSequence = epoch.Sequence;
                return SetTimeRecommendation(epoch, value.ReferenceCounter,
                    false,
                    "Initialize the PHC from validated GPS-grid GNSS UTC at the next pulse.");
            }
            if (state == PhcStartupState.WaitingForPairedPhase)
            {
                if (!value.HasPairedPhase)
                    return Result(PhcStartupActionKind.Wait,
                        "Waiting for paired GNSS and oscillator PPS timestamps.");
                long adjustment;
                try
                {
                    adjustment = checked(-value.PhaseErrorNanoseconds *
                        (long)phaseSign);
                }
                catch (OverflowException)
                {
                    return Fail("The paired phase error overflowed.");
                }
                if (adjustment < -MaximumSinglePhaseAdjustmentNanoseconds ||
                    adjustment > MaximumSinglePhaseAdjustmentNanoseconds)
                    return Fail(
                        "The rough phase correction exceeds the driver's safe single-adjustment range.");
                state = PhcStartupState.WaitingForRoughPhaseAcknowledgement;
                pendingToken = ++tokenCounter;
                return new PhcStartupRecommendation
                {
                    State = state,
                    Action = PhcStartupActionKind.AdjustPhcPhase,
                    AcknowledgementToken = pendingToken,
                    Reason = "Apply the upstream-equivalent paired-PPS rough phase correction.",
                    PhaseAdjustmentNanoseconds = adjustment,
                    SourceITowMilliseconds = epoch.ITowMilliseconds
                };
            }
            if (state == PhcStartupState.Settling)
            {
                if (value.MonotonicMilliseconds < settlingStarted)
                    return Fail("The monotonic observation time moved backwards.");
                if (value.MonotonicMilliseconds - settlingStarted <
                    settlingMilliseconds)
                    return Result(PhcStartupActionKind.Wait,
                        "Waiting for the PHC phase adjustment to settle.");
                if (epoch.Sequence <= initialEpochSequence)
                    return Result(PhcStartupActionKind.Wait,
                        "Waiting for a newer coherent GNSS epoch before final PHC alignment.");
                state = PhcStartupState.WaitingForFinalSetAcknowledgement;
                return SetTimeRecommendation(epoch, value.ReferenceCounter,
                    true,
                    "Correct the PHC whole second at a newer reference edge while preserving its aligned phase.");
            }
            if (state == PhcStartupState.Verifying)
            {
                if (epoch.Sequence <= finalSourceEpochSequence)
                    return Result(PhcStartupActionKind.Wait,
                        "Waiting for a post-write coherent GNSS epoch to verify the PHC.");
                if (!value.HasPhcUtc || value.PhcUtc.Kind != DateTimeKind.Utc)
                    return Result(PhcStartupActionKind.Wait,
                        "Waiting for a UTC PHC verification sample.");
                if (CounterDelta(finalSetReferenceCounter,
                    value.ReferenceCounter) > 1u)
                    return Fail(
                        "PHC verification did not complete within the bound reference epoch.");
                long elapsedMilliseconds = value.MonotonicMilliseconds -
                    finalSetAcknowledgedMilliseconds;
                if (elapsedMilliseconds < 0 || elapsedMilliseconds > 1500)
                    return Fail("The PHC verification interval is invalid.");
                DateTime unix = new DateTime(1970, 1, 1, 0, 0, 0,
                    DateTimeKind.Utc);
                DateTime expected = unix.AddSeconds(finalTargetUnixSeconds)
                    .AddTicks(finalTargetNanoseconds / 100)
                    .AddMilliseconds(elapsedMilliseconds);
                double difference = Math.Abs((value.PhcUtc - expected)
                    .TotalSeconds);
                if (difference > 0.25)
                    return Fail(
                        "The PHC did not verify within 250 ms of the edge-bound GNSS target.");
                DateTime gnssAtObservation = epoch.Navigation.Utc.Utc
                    .AddMilliseconds(value.GnssAgeMilliseconds);
                double gnssDifference = Math.Abs((value.PhcUtc -
                    gnssAtObservation).TotalSeconds);
                if (gnssDifference > 0.25)
                    return Fail(
                        "The PHC did not agree within 250 ms of the post-write GNSS epoch.");
                state = PhcStartupState.Complete;
                return Result(PhcStartupActionKind.Complete,
                    "PHC time and rough phase alignment verified.");
            }
            return Result(PhcStartupActionKind.Wait,
                "Waiting for the current startup action.");
        }

        public void Acknowledge(long token, bool succeeded,
            long monotonicMilliseconds, uint referenceCounter,
            int appliedNanoseconds)
        {
            if (pendingToken == 0 || token != pendingToken)
                throw new InvalidOperationException(
                    "The acknowledgement token is stale or invalid.");
            pendingToken = 0;
            if (!succeeded)
            {
                state = PhcStartupState.Faulted;
                fault = "The external PHC action failed.";
                return;
            }
            if (state == PhcStartupState.WaitingForInitialSetAcknowledgement)
                state = PhcStartupState.WaitingForPairedPhase;
            else if (state ==
                PhcStartupState.WaitingForRoughPhaseAcknowledgement)
            {
                if (monotonicMilliseconds < 0)
                    throw new ArgumentOutOfRangeException(
                        "monotonicMilliseconds");
                settlingStarted = monotonicMilliseconds;
                state = PhcStartupState.Settling;
            }
            else if (state == PhcStartupState.WaitingForFinalSetAcknowledgement)
            {
                finalTargetUnixSeconds = pendingTargetUnixSeconds;
                if (appliedNanoseconds < 0 ||
                    appliedNanoseconds >= 1000000000)
                    throw new ArgumentOutOfRangeException(
                        "appliedNanoseconds");
                finalTargetNanoseconds = appliedNanoseconds;
                finalSetAcknowledgedMilliseconds = monotonicMilliseconds;
                finalSetReferenceCounter = referenceCounter;
                finalSourceEpochSequence = pendingSourceEpochSequence;
                state = PhcStartupState.Verifying;
            }
            else
                throw new InvalidOperationException(
                    "No hardware action is pending in the current state.");
        }

        private PhcStartupRecommendation SetTimeRecommendation(
            GnssAssociatedEpoch epoch, uint referenceCounter,
            bool preservePhcNanoseconds, string reason)
        {
            GnssUtcSample utc = epoch.Navigation.Utc;
            pendingToken = ++tokenCounter;
            pendingTargetUnixSeconds = checked(utc.UnixSeconds + 1);
            pendingSourceEpochSequence = epoch.Sequence;
            return new PhcStartupRecommendation
            {
                State = state,
                Action = PhcStartupActionKind.SetPhcUtcAtNextPulse,
                AcknowledgementToken = pendingToken,
                Reason = reason,
                // NAV-PVT and TIM-TP are associated by iTOW.  TIM-TP reports
                // the following pulse, so an action explicitly executed at
                // that pulse targets the next integral UTC second.
                TargetUnixSeconds = pendingTargetUnixSeconds,
                TargetNanoseconds = 0,
                SourceITowMilliseconds = epoch.ITowMilliseconds,
                SourceEpochSequence = epoch.Sequence,
                SourceReferenceCounter = referenceCounter,
                PreservePhcNanoseconds = preservePhcNanoseconds
            };
        }

        private static uint CounterDelta(uint previous, uint current)
        {
            return unchecked(current - previous);
        }

        private PhcStartupRecommendation Fail(string reason)
        {
            state = PhcStartupState.Faulted;
            pendingToken = 0;
            fault = reason;
            return Result(PhcStartupActionKind.Fault, reason);
        }

        private PhcStartupRecommendation Result(PhcStartupActionKind action,
            string reason)
        {
            return new PhcStartupRecommendation
            {
                State = state,
                Action = action,
                AcknowledgementToken = pendingToken,
                Reason = reason
            };
        }
    }
}
