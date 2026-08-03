[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$helperPath = Join-Path $root "TimeCardOscillatord\NativeGnssSession.cs"
if (-not (Test-Path -LiteralPath $helperPath)) {
    throw "Native GNSS helper not found: $helperPath"
}

# Compile the production helper unchanged, adding only a transport stub and a
# test harness.  Reflection keeps this test independent of the service project
# file while the helper is integrated there.
$source = Get-Content -LiteralPath $helperPath -Raw
$source += @'

namespace TimeCardControlCenter
{
    public sealed class UartReadResult
    {
        public UartReadResult(byte[] data, uint lineStatus)
        {
            Data = data;
            LineStatus = lineStatus;
        }
        public byte[] Data { get; private set; }
        public uint LineStatus { get; private set; }
    }

    public sealed class UartWriteResult
    {
        public UartWriteResult(uint bytesTransferred, uint lineStatus)
        {
            BytesTransferred = bytesTransferred;
            LineStatus = lineStatus;
        }
        public uint BytesTransferred { get; private set; }
        public uint LineStatus { get; private set; }
    }

    public sealed class TimeCardClient
    {
        public int ConfigureCalls;
        public int WriteCalls;
        public byte[] LastWrite;
        public readonly Queue<byte[]> Reads = new Queue<byte[]>();

        public UartReadResult ReadUart(uint port, uint length, uint timeout)
        {
            return new UartReadResult(Reads.Count == 0 ? new byte[0] :
                Reads.Dequeue(), 0);
        }

        public void ConfigureUart(uint port, uint baud)
        {
            ConfigureCalls++;
        }

        public UartWriteResult WriteUart(uint port, byte[] data, uint timeout)
        {
            WriteCalls++;
            LastWrite = (byte[])data.Clone();
            return new UartWriteResult((uint)data.Length, 0);
        }
    }

    public static class NativeGnssSessionGoldenHarness
    {
        private static int assertions;

        public static string Run()
        {
            TestProtocolGoldenVectors();
            NativeGnssSessionSnapshot epoch = TestEpochAssociation();
            TestUtcAndLeapValidation();
            TestPulseSafetyAndRfTelemetry();
            TestHardwareWriteOptIn();
            TestPassiveBaudDetection();
            TestReceiverProfileAndAcknowledgement();
            TestStartupPlanner(epoch);
            return assertions.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }

        private static void TestProtocolGoldenVectors()
        {
            Hex(UbxProtocol.BuildFrame(0x0a, 0x04, new byte[0]),
                "B5620A0400000E34", "MON-VER poll");
            Hex(UbxProtocol.BuildResetFrame(UbxResetKind.GnssStart),
                "B5620604040001000900187A", "GNSS start");
            Hex(UbxProtocol.BuildResetFrame(UbxResetKind.GnssStop),
                "B56206040400010008001778", "GNSS stop");
            Hex(UbxProtocol.BuildResetFrame(UbxResetKind.Soft),
                "B5620604040001000100106A", "software reset");
            Hex(UbxProtocol.BuildResetFrame(UbxResetKind.Hard),
                "B56206040400010004001370", "hardware reset");
            Hex(UbxProtocol.BuildResetFrame(UbxResetKind.Cold),
                "B56206040400FFFF02000E61", "cold start");

            UbxConfigurationValue navPvt = new UbxConfigurationValue(
                0x20910007u, 1u);
            Hex(UbxProtocol.BuildConfigurationFrame(
                new[] { navPvt }, false),
                "B562068A09000001000007009120015348",
                "RAM CFG-VALSET");
            Hex(UbxProtocol.BuildConfigurationFrame(
                new[] { navPvt }, true),
                "B562068A09000007000007009120015978",
                "persistent CFG-VALSET");
            Throws<ArgumentException>(delegate
            {
                UbxProtocol.BuildConfigurationFrame(new[]
                {
                    new UbxConfigurationValue(0x00910007u, 1u)
                }, false);
            }, "invalid configuration key size");
        }

        private static NativeGnssSessionSnapshot TestEpochAssociation()
        {
            GnssEpochAssembler assembler = new GnssEpochAssembler();
            byte[] nav1000 = UbxProtocol.BuildFrame(0x01, 0x07,
                NavPvt(1000, 2026, 8, 1, 12, 34, 56, 123456700, 0x07));
            byte[] corrupt = (byte[])nav1000.Clone();
            corrupt[corrupt.Length - 1] ^= 0x55;
            int split = nav1000.Length / 2;
            assembler.Feed(Concat(new byte[] { 0x7f, 0xb5 }, corrupt,
                Slice(nav1000, 0, split)));
            assembler.Feed(Concat(Slice(nav1000, split,
                    nav1000.Length - split),
                UbxProtocol.BuildFrame(0x01, 0x26,
                    TimeLs(1000, 18, 2, 1, 2500, 7, 0x03)),
                UbxProtocol.BuildFrame(0x0d, 0x01,
                    TimePulse(1000, 11)),
                UbxProtocol.BuildFrame(0x0d, 0x04, Survey(1200, true, false))));

            NativeGnssSessionSnapshot first = assembler.Snapshot();
            True(first.LatestCoherentEpoch == null,
                "startup waits for a consecutive one-hertz pulse");
            Equal(first.LatestEpoch.ITowMilliseconds, 1000u,
                "first epoch iTOW");
            True(first.LatestSurvey.Completed, "survey completion");
            Equal(first.LatestEpoch.Navigation.Utc.Nanoseconds,
                123456700, "UTC nanoseconds");

            assembler.Feed(Concat(
                UbxProtocol.BuildFrame(0x0d, 0x01, TimePulse(2000, 22)),
                UbxProtocol.BuildFrame(0x01, 0x07,
                    NavPvt(2000, 2026, 8, 1, 12, 34, 57, 0, 0x07)),
                UbxProtocol.BuildFrame(0x01, 0x26,
                    TimeLs(2000, 18, 0, 0, 0, 0, 0x01))));
            NativeGnssSessionSnapshot second = assembler.Snapshot();
            Equal(second.LatestCoherentEpoch.ITowMilliseconds, 2000u,
                "second epoch iTOW");
            Equal(second.LatestCoherentEpoch.TimePulse.
                PreviousQuantizationErrorPicoseconds.Value, 11,
                "qErr(n-1) association");
            Equal(second.LatestCoherentEpoch.TimePulse.
                QuantizationErrorPicoseconds, 22, "qErr(n)");
            True(second.ValidFrameCount >= 7,
                "valid frames counted after fragmented recovery");

            GnssEpochAssembler rollover = new GnssEpochAssembler();
            rollover.Feed(Concat(
                UbxProtocol.BuildFrame(0x01, 0x07,
                    NavPvt(604798000, 2026, 8, 1, 12, 34, 57, 0, 0x07)),
                UbxProtocol.BuildFrame(0x01, 0x26,
                    TimeLs(604798000, 18, 0, 0, 0, 0, 0x01)),
                UbxProtocol.BuildFrame(0x0d, 0x01,
                    TimePulse(604798000, 22)),
                UbxProtocol.BuildFrame(0x01, 0x07,
                    NavPvt(604799000, 2026, 8, 1, 12, 34, 58, 0, 0x07)),
                UbxProtocol.BuildFrame(0x01, 0x26,
                    TimeLs(604799000, 18, 0, 0, 0, 0, 0x01)),
                UbxProtocol.BuildFrame(0x0d, 0x01,
                    TimePulseWithWeek(604799000, 33, 2401))));
            True(rollover.Snapshot().LatestCoherentEpoch != null,
                "next-pulse iTOW wraps across the GNSS week");
            Equal(rollover.Snapshot().LatestCoherentEpoch.ITowMilliseconds,
                604799000u, "week-rollover epoch association");
            return second;
        }

        private static void TestUtcAndLeapValidation()
        {
            GnssEpochAssembler leap = new GnssEpochAssembler();
            leap.Feed(UbxProtocol.BuildFrame(0x01, 0x07,
                NavPvt(3000, 2028, 12, 31, 23, 59, 60, 0, 0x07)));
            NativeGnssSessionSnapshot leapSnapshot = leap.Snapshot();
            True(leapSnapshot.LatestEpoch.Navigation.Utc.IsValid,
                "valid UTC leap second accepted");
            True(leapSnapshot.LatestEpoch.Navigation.Utc.RepresentsLeapSecond,
                "UTC leap second is marked");

            GnssEpochAssembler negativeNano = new GnssEpochAssembler();
            negativeNano.Feed(UbxProtocol.BuildFrame(0x01, 0x07,
                NavPvt(3500, 2026, 8, 1, 12, 0, 1, -100000000, 0x07)));
            Equal(negativeNano.Snapshot().LatestEpoch.Navigation.Utc.Utc,
                new DateTime(2026, 8, 1, 12, 0, 0, 900,
                    DateTimeKind.Utc), "negative NAV-PVT nano normalization");

            GnssEpochAssembler invalidUtc = new GnssEpochAssembler();
            invalidUtc.Feed(UbxProtocol.BuildFrame(0x01, 0x07,
                NavPvt(4000, 2026, 2, 31, 12, 0, 0, 0, 0x07)));
            True(!invalidUtc.Snapshot().LatestEpoch.Navigation.Utc.IsValid,
                "impossible UTC date rejected");

            GnssEpochAssembler unresolvedUtc = new GnssEpochAssembler();
            unresolvedUtc.Feed(UbxProtocol.BuildFrame(0x01, 0x07,
                NavPvt(5000, 2026, 8, 1, 12, 0, 0, 0, 0x03)));
            True(!unresolvedUtc.Snapshot().LatestEpoch.Navigation.Utc.IsValid,
                "UTC without fully-resolved flag rejected");

            GnssEpochAssembler invalidLeap = new GnssEpochAssembler();
            invalidLeap.Feed(UbxProtocol.BuildFrame(0x01, 0x26,
                TimeLs(6000, 127, 2, 1, 2500, 7, 0x03)));
            GnssLeapSecondSample bad =
                invalidLeap.Snapshot().LatestEpoch.LeapSeconds;
            True(!bad.IsValid && !bad.CurrentOffsetValid,
                "implausible leap offset rejected");

            GnssEpochAssembler invalidEvent = new GnssEpochAssembler();
            invalidEvent.Feed(UbxProtocol.BuildFrame(0x01, 0x26,
                TimeLs(7000, 18, 2, 1, 2500, 8, 0x03)));
            True(!invalidEvent.Snapshot().LatestEpoch.LeapSeconds.IsValid,
                "invalid leap-event day rejected");

            GnssEpochAssembler invalidEventSource = new GnssEpochAssembler();
            invalidEventSource.Feed(UbxProtocol.BuildFrame(0x01, 0x26,
                TimeLs(7250, 18, 1, 1, 2500, 7, 0x03)));
            True(!invalidEventSource.Snapshot().LatestEpoch.LeapSeconds.IsValid,
                "invalid leap-event source rejected");

            GnssEpochAssembler beiDouEvent = new GnssEpochAssembler();
            beiDouEvent.Feed(UbxProtocol.BuildFrame(0x01, 0x26,
                TimeLs(7300, 18, 4, 1, 2500, 0, 0x03)));
            True(beiDouEvent.Snapshot().LatestEpoch.LeapSeconds.IsValid,
                "BeiDou leap-event Sunday accepted as day zero");

            byte[] reservedPayload = TimeLs(7500, 18, 0, 0, 0, 0, 0x01);
            reservedPayload[20] = 1;
            GnssEpochAssembler reservedLeap = new GnssEpochAssembler();
            reservedLeap.Feed(UbxProtocol.BuildFrame(0x01, 0x26,
                reservedPayload));
            True(!reservedLeap.Snapshot().LatestEpoch.LeapSeconds.IsValid,
                "nonzero leap reserved field rejected");

            GnssEpochAssembler invalidQerr = new GnssEpochAssembler();
            invalidQerr.Feed(UbxProtocol.BuildFrame(0x0d, 0x01,
                TimePulse(8000, 5001)));
            True(!invalidQerr.Snapshot().LatestEpoch.TimePulse.
                QuantizationErrorValid, "aberrant qErr rejected");

            byte[] flaggedQerrPayload = TimePulse(8100, 10);
            flaggedQerrPayload[14] = 0x10;
            GnssEpochAssembler flaggedQerr = new GnssEpochAssembler();
            flaggedQerr.Feed(UbxProtocol.BuildFrame(0x0d, 0x01,
                flaggedQerrPayload));
            True(!flaggedQerr.Snapshot().LatestEpoch.TimePulse.
                QuantizationErrorValid, "TIM-TP qErrInvalid flag honored");

            byte[] reservedFlagPayload = TimePulse(8200, 10);
            reservedFlagPayload[14] = 0x20;
            GnssEpochAssembler reservedFlag = new GnssEpochAssembler();
            reservedFlag.Feed(UbxProtocol.BuildFrame(0x0d, 0x01,
                reservedFlagPayload));
            True(!reservedFlag.Snapshot().LatestEpoch.TimePulse.FlagsValid,
                "TIM-TP reserved flag rejected");
        }

        private static void TestHardwareWriteOptIn()
        {
            TimeCardClient transport = new TimeCardClient();
            NativeGnssSessionManager manager =
                new NativeGnssSessionManager(transport, 0, 115200);
            Throws<InvalidOperationException>(delegate
            {
                manager.ConfigureUart(false);
            }, "UART configuration requires opt-in");
            Throws<InvalidOperationException>(delegate
            {
                manager.ResetReceiver(UbxResetKind.Soft, false);
            }, "receiver reset requires opt-in");
            manager.ConfigureUart(true);
            manager.ResetReceiver(UbxResetKind.Hard, true);
            manager.ApplyConfiguration(new[]
            {
                new UbxConfigurationValue(0x20910007u, 1u)
            }, false, true);
            Equal(transport.ConfigureCalls, 1, "opt-in UART configure call");
            Equal(transport.WriteCalls, 2, "opt-in receiver write calls");
            Hex(transport.LastWrite,
                "B562068A09000001000007009120015348",
                "opt-in configuration bytes");
        }

        private static void TestPulseSafetyAndRfTelemetry()
        {
            GnssEpochAssembler qerr = new GnssEpochAssembler();
            qerr.Feed(Concat(
                UbxProtocol.BuildFrame(0x0d, 0x01, TimePulse(1000, 10)),
                UbxProtocol.BuildFrame(0x0d, 0x01, TimePulse(2000, 5001)),
                UbxProtocol.BuildFrame(0x0d, 0x01, TimePulse(3000, 20))));
            Equal(qerr.Snapshot().LatestEpoch.TimePulse.
                PreviousQuantizationErrorPicoseconds.Value, 0,
                "invalid qErr advances the n-1 chain as zero");

            GnssEpochAssembler nonGps = new GnssEpochAssembler();
            byte[] firstUtc = TimePulse(1000, 1);
            byte[] secondUtc = TimePulse(2000, 2);
            firstUtc[14] = 0x03;
            secondUtc[14] = 0x03;
            nonGps.Feed(Concat(
                UbxProtocol.BuildFrame(0x01, 0x07,
                    NavPvt(1000, 2026, 8, 1, 1, 2, 3, 0, 0x07)),
                UbxProtocol.BuildFrame(0x01, 0x26,
                    TimeLs(1000, 18, 0, 0, 0, 0, 0x01)),
                UbxProtocol.BuildFrame(0x0d, 0x01, firstUtc),
                UbxProtocol.BuildFrame(0x01, 0x07,
                    NavPvt(2000, 2026, 8, 1, 1, 2, 4, 0, 0x07)),
                UbxProtocol.BuildFrame(0x01, 0x26,
                    TimeLs(2000, 18, 0, 0, 0, 0, 0x01)),
                UbxProtocol.BuildFrame(0x0d, 0x01, secondUtc)));
            True(nonGps.Snapshot().LatestCoherentEpoch == null,
                "UTC-grid TIM-TP is not mis-associated as GPS iTOW");

            GnssEpochAssembler invalidRaim = new GnssEpochAssembler();
            byte[] raim = TimePulse(4000, 2);
            raim[14] = 0x0c;
            invalidRaim.Feed(UbxProtocol.BuildFrame(0x0d, 0x01, raim));
            True(!invalidRaim.Snapshot().LatestEpoch.TimePulse.FlagsValid,
                "undefined TIM-TP RAIM state rejected");

            GnssEpochAssembler rf = new GnssEpochAssembler();
            byte[] rfPayload = new byte[52];
            rfPayload[1] = 2;
            rfPayload[6] = 2;
            rfPayload[7] = 1;
            rfPayload[30] = 4;
            rfPayload[31] = 0;
            rf.Feed(UbxProtocol.BuildFrame(0x0a, 0x38, rfPayload));
            NativeGnssSessionSnapshot rfSnapshot = rf.Snapshot();
            True(rfSnapshot.LatestRf.IsWellFormed,
                "MON-RF multi-block payload accepted");
            Equal(rfSnapshot.LatestRf.AntennaStatus, (byte)4,
                "MON-RF retains worst antenna status");
            Equal(rfSnapshot.LatestRf.AntennaPower, (byte)1,
                "MON-RF retains known antenna power");
        }

        private static void TestReceiverProfileAndAcknowledgement()
        {
            IList<UbxConfigurationValue> profile =
                UbxProtocol.BuildOscillatordConfiguration(115200, "GPS",
                    85, true);
            Equal(profile.Count, 28,
                "native receiver profile includes timing and RTCM outputs");
            True(HasConfiguration(profile, 0x20910007u, 1u),
                "native profile enables NAV-PVT");
            True(HasConfiguration(profile, 0x2091035au, 1u),
                "native profile enables MON-RF");
            True(HasConfiguration(profile, 0x2005000cu, 1u),
                "native profile selects GPS time grid");
            True(HasConfiguration(profile, 0x30050001u, 85u),
                "native profile applies antenna cable delay");
            True(HasConfiguration(profile, 0x209102beu, 1u) &&
                HasConfiguration(profile, 0x209102a5u, 1u),
                "native profile enables RTCM and RAWX output");

            TimeCardClient ackTransport = new TimeCardClient();
            ackTransport.Reads.Enqueue(UbxProtocol.BuildFrame(0x05, 0x01,
                new byte[] { 0x06, 0x8a }));
            NativeGnssSessionManager acknowledged =
                new NativeGnssSessionManager(ackTransport, 0, 115200);
            acknowledged.ApplyConfigurationAndAwaitAcknowledgementAsync(
                new[] { new UbxConfigurationValue(0x20910007u, 1u) },
                false, true, CancellationToken.None).GetAwaiter().GetResult();
            Equal(ackTransport.WriteCalls, 1,
                "CFG-VALSET acknowledgement path writes once");

            TimeCardClient nakTransport = new TimeCardClient();
            nakTransport.Reads.Enqueue(UbxProtocol.BuildFrame(0x05, 0x00,
                new byte[] { 0x06, 0x8a }));
            NativeGnssSessionManager rejected =
                new NativeGnssSessionManager(nakTransport, 0, 115200);
            Throws<InvalidOperationException>(delegate
            {
                rejected.ApplyConfigurationAndAwaitAcknowledgementAsync(
                    new[] { new UbxConfigurationValue(0x20910007u, 1u) },
                    false, true, CancellationToken.None).GetAwaiter()
                    .GetResult();
            }, "CFG-VALSET NAK is surfaced");
        }

        private static void TestPassiveBaudDetection()
        {
            TimeCardClient transport = new TimeCardClient();
            transport.Reads.Enqueue(new byte[0]);
            transport.Reads.Enqueue(new byte[0]);
            transport.Reads.Enqueue(new byte[0]);
            transport.Reads.Enqueue(UbxProtocol.BuildFrame(0x01, 0x61,
                new byte[] { 0x01, 0x00, 0x00, 0x00 }));
            NativeGnssSessionManager manager =
                new NativeGnssSessionManager(transport, 0, 115200);
            uint detected = manager.DetectReceiverBaudAsync(
                true, CancellationToken.None).GetAwaiter().GetResult();
            Equal(detected, 115200u,
                "checksum-valid passive UBX confirms receiver baud");
            Equal(transport.WriteCalls, 1,
                "baud detection still requests MON-VER before passive proof");
        }

        private static bool HasConfiguration(
            IEnumerable<UbxConfigurationValue> values, uint key, ulong value)
        {
            foreach (UbxConfigurationValue item in values)
            {
                if (item.Key == key && item.Value == value)
                    return true;
            }
            return false;
        }

        private static void TestStartupPlanner(
            NativeGnssSessionSnapshot gnss)
        {
            PhcStartupAlignmentPlanner planner =
                new PhcStartupAlignmentPlanner(1, 2000);
            PhcStartupObservation observation = new PhcStartupObservation
            {
                Gnss = gnss,
                MonotonicMilliseconds = 0,
                ReferenceCounter = 10
            };
            PhcStartupRecommendation initial = planner.Observe(observation);
            Equal(initial.Action, PhcStartupActionKind.SetPhcUtcAtNextPulse,
                "initial PHC set recommendation");
            True(initial.AcknowledgementToken != 0,
                "initial action acknowledgement token");
            Equal(initial.TargetUnixSeconds,
                gnss.LatestCoherentEpoch.Navigation.Utc.UnixSeconds + 1,
                "initial target is next UTC pulse");
            Equal(initial.TargetNanoseconds, 0,
                "initial target is integral UTC second");
            planner.Acknowledge(initial.AcknowledgementToken, true, 0, 11, 0);

            PhcStartupState waitingPhaseState = planner.State;
            PhcStartupRecommendation staleGnss = planner.Observe(
                new PhcStartupObservation
                {
                    Gnss = gnss,
                    GnssAgeMilliseconds = 2001,
                    MonotonicMilliseconds = 0
                });
            Equal(staleGnss.Action, PhcStartupActionKind.Wait,
                "stale GNSS waits");
            Equal(planner.State, waitingPhaseState,
                "stale GNSS does not restart PHC initialization");

            observation.HasPairedPhase = true;
            observation.PhaseErrorNanoseconds = 125;
            PhcStartupRecommendation rough = planner.Observe(observation);
            Equal(rough.Action, PhcStartupActionKind.AdjustPhcPhase,
                "rough phase recommendation");
            Equal(rough.PhaseAdjustmentNanoseconds, -125L,
                "rough phase sign");
            planner.Acknowledge(rough.AcknowledgementToken, true, 100, 11, 0);

            observation.MonotonicMilliseconds = 2099;
            Equal(planner.Observe(observation).Action,
                PhcStartupActionKind.Wait, "monotonic settling interval");
            observation.MonotonicMilliseconds = 2100;
            observation.Gnss.LatestCoherentEpoch.Sequence++;
            observation.Gnss.LatestCoherentEpoch.Navigation.Utc.UnixSeconds += 2;
            observation.Gnss.LatestCoherentEpoch.Navigation.Utc.Utc =
                observation.Gnss.LatestCoherentEpoch.Navigation.Utc.Utc
                    .AddSeconds(2);
            observation.ReferenceCounter = 11;
            PhcStartupRecommendation final = planner.Observe(observation);
            Equal(final.Action, PhcStartupActionKind.SetPhcUtcAtNextPulse,
                "final PHC set recommendation");
            planner.Acknowledge(final.AcknowledgementToken, true, 2100, 12,
                12500000);

            observation.HasPhcUtc = true;
            observation.ReferenceCounter = 12;
            observation.MonotonicMilliseconds = 2200;
            observation.GnssAgeMilliseconds = 100;
            observation.Gnss.LatestCoherentEpoch.Sequence++;
            observation.Gnss.LatestCoherentEpoch.Navigation.Utc.UnixSeconds++;
            observation.Gnss.LatestCoherentEpoch.Navigation.Utc.Utc =
                observation.Gnss.LatestCoherentEpoch.Navigation.Utc.Utc
                    .AddSeconds(1);
            observation.PhcUtc = new DateTime(1970, 1, 1, 0, 0, 0,
                DateTimeKind.Utc).AddSeconds(final.TargetUnixSeconds)
                .AddTicks(12500000 / 100).AddMilliseconds(100);
            PhcStartupRecommendation verified = planner.Observe(observation);
            Equal(verified.Action, PhcStartupActionKind.Complete,
                "PHC verification");

            PhcStartupAlignmentPlanner unsafePlanner =
                new PhcStartupAlignmentPlanner(1, 0);
            PhcStartupRecommendation unsafeInitial =
                unsafePlanner.Observe(new PhcStartupObservation
                {
                    Gnss = gnss,
                    MonotonicMilliseconds = 0
                });
            unsafePlanner.Acknowledge(
                unsafeInitial.AcknowledgementToken, true, 0, 1, 0);
            PhcStartupRecommendation unsafePhase =
                unsafePlanner.Observe(new PhcStartupObservation
                {
                    Gnss = gnss,
                    HasPairedPhase = true,
                    PhaseErrorNanoseconds = 500000000,
                    MonotonicMilliseconds = 0
                });
            Equal(unsafePhase.Action, PhcStartupActionKind.Fault,
                "unsafe phase adjustment rejected");
        }

        private static byte[] NavPvt(uint iTow, ushort year, byte month,
            byte day, byte hour, byte minute, byte second, int nano,
            byte valid)
        {
            byte[] value = new byte[92];
            U32(value, 0, iTow);
            U16(value, 4, year);
            value[6] = month;
            value[7] = day;
            value[8] = hour;
            value[9] = minute;
            value[10] = second;
            value[11] = valid;
            U32(value, 12, 42);
            I32(value, 16, nano);
            value[20] = 3;
            value[21] = 1;
            value[23] = 12;
            return value;
        }

        private static byte[] TimePulse(uint iTow, int qErr)
        {
            byte[] value = new byte[16];
            // The helper argument names the NAV epoch; TIM-TP reports the
            // following one-hertz pulse and is normalized back by the parser.
            uint pulseITow = iTow + 1000u;
            if (pulseITow >= 604800000u)
                pulseITow -= 604800000u;
            U32(value, 0, pulseITow);
            I32(value, 8, qErr);
            U16(value, 12, 2400);
            return value;
        }

        private static byte[] TimePulseWithWeek(uint iTow, int qErr,
            ushort week)
        {
            byte[] value = TimePulse(iTow, qErr);
            U16(value, 12, week);
            return value;
        }

        private static byte[] TimeLs(uint iTow, int current, int source,
            int change, int week, int day, byte valid)
        {
            byte[] value = new byte[24];
            U32(value, 0, iTow);
            value[4] = 0;
            value[8] = (byte)source;
            value[9] = unchecked((byte)(sbyte)current);
            value[10] = (byte)source;
            value[11] = unchecked((byte)(sbyte)change);
            I32(value, 12, change == 0 ? 0 : 86400);
            U16(value, 16, (ushort)week);
            U16(value, 18, (ushort)day);
            value[23] = valid;
            return value;
        }

        private static byte[] Survey(uint duration, bool valid, bool active)
        {
            byte[] value = new byte[28];
            U32(value, 0, duration);
            U32(value, 16, 810000);
            U32(value, 20, 1500);
            value[24] = valid ? (byte)1 : (byte)0;
            value[25] = active ? (byte)1 : (byte)0;
            return value;
        }

        private static byte[] Slice(byte[] value, int offset, int count)
        {
            byte[] result = new byte[count];
            Buffer.BlockCopy(value, offset, result, 0, count);
            return result;
        }

        private static byte[] Concat(params byte[][] values)
        {
            int length = 0;
            foreach (byte[] value in values)
                length += value.Length;
            byte[] result = new byte[length];
            int offset = 0;
            foreach (byte[] value in values)
            {
                Buffer.BlockCopy(value, 0, result, offset, value.Length);
                offset += value.Length;
            }
            return result;
        }

        private static void U16(byte[] value, int offset, ushort item)
        {
            value[offset] = (byte)item;
            value[offset + 1] = (byte)(item >> 8);
        }

        private static void U32(byte[] value, int offset, uint item)
        {
            value[offset] = (byte)item;
            value[offset + 1] = (byte)(item >> 8);
            value[offset + 2] = (byte)(item >> 16);
            value[offset + 3] = (byte)(item >> 24);
        }

        private static void I32(byte[] value, int offset, int item)
        {
            U32(value, offset, unchecked((uint)item));
        }

        private static void Hex(byte[] actual, string expected, string name)
        {
            string text = BitConverter.ToString(actual).Replace("-", "");
            Equal(text, expected, name);
        }

        private static void True(bool value, string name)
        {
            assertions++;
            if (!value)
                throw new InvalidOperationException("Assertion failed: " + name);
        }

        private static void Equal<T>(T actual, T expected, string name)
        {
            assertions++;
            if (!EqualityComparer<T>.Default.Equals(actual, expected))
                throw new InvalidOperationException(string.Format(
                    "Assertion failed: {0}; expected {1}, got {2}.",
                    name, expected, actual));
        }

        private static void Throws<T>(Action action, string name)
            where T : Exception
        {
            assertions++;
            try
            {
                action();
            }
            catch (T)
            {
                return;
            }
            throw new InvalidOperationException(
                "Assertion failed: " + name + " did not throw " +
                typeof(T).Name + ".");
        }
    }
}
'@

$parameters = New-Object System.CodeDom.Compiler.CompilerParameters
$parameters.GenerateInMemory = $true
$parameters.GenerateExecutable = $false
$parameters.ReferencedAssemblies.Add("System.dll") | Out-Null
$parameters.ReferencedAssemblies.Add("System.Core.dll") | Out-Null
$provider = New-Object Microsoft.CSharp.CSharpCodeProvider
$result = $provider.CompileAssemblyFromSource($parameters, $source)
if ($result.Errors.HasErrors) {
    $messages = $result.Errors | ForEach-Object {
        "{0}({1},{2}): {3}" -f $_.FileName, $_.Line, $_.Column, $_.ErrorText
    }
    throw "Native GNSS helper did not compile:`n$($messages -join [Environment]::NewLine)"
}

$harness = $result.CompiledAssembly.GetType(
    "TimeCardControlCenter.NativeGnssSessionGoldenHarness", $true)
$run = $harness.GetMethod("Run", [Reflection.BindingFlags]::Public -bor
    [Reflection.BindingFlags]::Static)
try {
    $count = $run.Invoke($null, @())
}
catch [Reflection.TargetInvocationException] {
    throw $_.Exception.InnerException
}
Write-Host "Native GNSS session golden tests passed ($count assertions)."
