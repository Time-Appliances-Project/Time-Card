using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;

namespace TimeCardControlCenter
{
    public sealed class UbloxSatelliteInfo
    {
        internal UbloxSatelliteInfo(byte gnssIdentifier, byte satelliteIdentifier,
            byte carrierToNoise, int elevationDegrees, int azimuthDegrees,
            uint flags, string constellation, string displayIdentifier)
        {
            GnssIdentifier = gnssIdentifier;
            SatelliteIdentifier = satelliteIdentifier;
            CarrierToNoise = carrierToNoise;
            ElevationDegrees = elevationDegrees;
            AzimuthDegrees = azimuthDegrees;
            Flags = flags;
            Constellation = constellation;
            DisplayIdentifier = displayIdentifier;
            IsUsed = (flags & 0x08u) != 0;
            QualityIndicator = (byte)(flags & 0x07u);
        }

        public byte GnssIdentifier { get; private set; }
        public byte SatelliteIdentifier { get; private set; }
        public byte CarrierToNoise { get; private set; }
        public int ElevationDegrees { get; private set; }
        public int AzimuthDegrees { get; private set; }
        public uint Flags { get; private set; }
        public string Constellation { get; private set; }
        public string DisplayIdentifier { get; private set; }
        public bool IsUsed { get; private set; }
        public byte QualityIndicator { get; private set; }

        public string QualityDescription
        {
            get
            {
                switch (QualityIndicator)
                {
                    case 0: return "No signal";
                    case 1: return "Searching";
                    case 2: return "Acquired";
                    case 3: return "Unusable";
                    case 4: return "Code locked";
                    case 5: return "Carrier locked";
                    default: return "Tracked";
                }
            }
        }
    }

    public sealed class UbloxRfBlockInfo
    {
        internal UbloxRfBlockInfo(byte blockIdentifier, byte jammingState,
            byte antennaStatus, byte antennaPower, ushort noisePerMillisecond,
            ushort automaticGainControl, byte jammingIndicator)
        {
            BlockIdentifier = blockIdentifier;
            JammingState = jammingState;
            AntennaStatus = antennaStatus;
            AntennaPower = antennaPower;
            NoisePerMillisecond = noisePerMillisecond;
            AutomaticGainControl = automaticGainControl;
            JammingIndicator = jammingIndicator;
        }

        public byte BlockIdentifier { get; private set; }
        public byte JammingState { get; private set; }
        public byte AntennaStatus { get; private set; }
        public byte AntennaPower { get; private set; }
        public ushort NoisePerMillisecond { get; private set; }
        public ushort AutomaticGainControl { get; private set; }
        public byte JammingIndicator { get; private set; }
    }

    public sealed class UbloxReceiverSnapshot
    {
        private readonly Dictionary<uint, ulong> configuration;

        internal UbloxReceiverSnapshot(Dictionary<uint, ulong> configuration,
            IList<string> warnings)
        {
            this.configuration = configuration;
            List<string> warningList = warnings as List<string> ?? new List<string>(warnings);
            Warnings = warningList.AsReadOnly();
            Satellites = new List<UbloxSatelliteInfo>().AsReadOnly();
            RfBlocks = new List<UbloxRfBlockInfo>().AsReadOnly();
            CapturedAtUtc = DateTime.UtcNow;
        }

        public string SoftwareVersion { get; internal set; }
        public string HardwareVersion { get; internal set; }
        public string Module { get; internal set; }
        public string Firmware { get; internal set; }
        public string ProtocolVersion { get; internal set; }
        public string SupportedGnss { get; internal set; }
        public string ReceiverFamily { get; internal set; }
        public string UniqueChipId { get; internal set; }
        public bool IsF9TimingReceiver { get; internal set; }
        public bool IsPassiveStream { get; internal set; }
        public byte FixType { get; internal set; }
        public bool FixValid { get; internal set; }
        public bool DifferentialSolution { get; internal set; }
        public byte SatellitesUsed { get; internal set; }
        public int SatellitesVisible { get; internal set; }
        public double AverageCno { get; internal set; }
        public string ConstellationSummary { get; internal set; }
        public IList<UbloxSatelliteInfo> Satellites { get; internal set; }
        public DateTime CapturedAtUtc { get; internal set; }
        public DateTime? Utc { get; internal set; }
        public uint TimeAccuracyNanoseconds { get; internal set; }
        public double Latitude { get; internal set; }
        public double Longitude { get; internal set; }
        public uint HorizontalAccuracyMillimeters { get; internal set; }
        public uint VerticalAccuracyMillimeters { get; internal set; }
        public double PositionDop { get; internal set; }
        public bool ConfigurationSupported { get; internal set; }
        public bool RateConfigurationSupported { get; internal set; }
        public bool SignalConfigurationSupported { get; internal set; }
        public bool TimePulseConfigurationSupported { get; internal set; }
        public bool MessageConfigurationSupported { get; internal set; }
        public bool TimingConfigurationSupported { get; internal set; }
        public bool SurveyInStatusSupported { get; internal set; }
        public bool SurveyInActive { get; internal set; }
        public bool SurveyInValid { get; internal set; }
        public uint SurveyInDurationSeconds { get; internal set; }
        public uint SurveyInObservations { get; internal set; }
        public double SurveyInMeanAccuracyMillimeters { get; internal set; }
        public bool RfStatusSupported { get; internal set; }
        public IList<UbloxRfBlockInfo> RfBlocks { get; internal set; }
        public bool GpsTimeSupported { get; internal set; }
        public bool GpsTimeValid { get; internal set; }
        public int GpsWeek { get; internal set; }
        public double GpsTimeOfWeekSeconds { get; internal set; }
        public int GpsUtcLeapSeconds { get; internal set; }
        public uint GpsTimeAccuracyNanoseconds { get; internal set; }
        public bool LeapSecondStatusSupported { get; internal set; }
        public bool CurrentLeapSecondsValid { get; internal set; }
        public bool LeapSecondEventValid { get; internal set; }
        public int CurrentLeapSeconds { get; internal set; }
        public int LeapSecondChange { get; internal set; }
        public int SecondsToLeapSecondEvent { get; internal set; }
        public ushort LeapSecondEventGpsWeek { get; internal set; }
        public ushort LeapSecondEventGpsDay { get; internal set; }
        public byte CurrentLeapSecondSource { get; internal set; }
        public byte LeapSecondEventSource { get; internal set; }
        public IList<string> Warnings { get; private set; }

        public ulong Config(uint key, ulong fallback)
        {
            ulong value;
            return configuration.TryGetValue(key, out value) ? value : fallback;
        }

        public bool HasConfig(uint key)
        {
            return configuration.ContainsKey(key);
        }

        public int ConfigSigned32(uint key, int fallback)
        {
            ulong value;
            return configuration.TryGetValue(key, out value) ?
                unchecked((int)(uint)value) : fallback;
        }

        public int ConfigSigned8(uint key, int fallback)
        {
            ulong value;
            return configuration.TryGetValue(key, out value) ?
                unchecked((sbyte)(byte)value) : fallback;
        }
    }

    public sealed class UbloxClient
    {
        internal const uint CfgRateMeas = 0x30210001;
        internal const uint CfgRateNav = 0x30210002;
        internal const uint CfgRateTimeRef = 0x20210003;
        internal const uint CfgDynamicModel = 0x20110021;

        internal const uint CfgSignalGps = 0x1031001f;
        internal const uint CfgSignalSbas = 0x10310020;
        internal const uint CfgSignalGalileo = 0x10310021;
        internal const uint CfgSignalBeiDou = 0x10310022;
        internal const uint CfgSignalQzss = 0x10310024;
        internal const uint CfgSignalGlonass = 0x10310025;

        internal const uint CfgTpPulseDefinition = 0x20050023;
        internal const uint CfgTpLengthDefinition = 0x20050030;
        internal const uint CfgTpPeriod = 0x40050002;
        internal const uint CfgTpPeriodLocked = 0x40050003;
        internal const uint CfgTpLength = 0x40050004;
        internal const uint CfgTpLengthLocked = 0x40050005;
        internal const uint CfgTpEnabled = 0x10050007;
        internal const uint CfgTpSyncGnss = 0x10050008;
        internal const uint CfgTpUseLocked = 0x10050009;
        internal const uint CfgTpAlignTow = 0x1005000a;
        internal const uint CfgTpPolarity = 0x1005000b;
        internal const uint CfgTpTimeGrid = 0x2005000c;

        internal const uint CfgMsgUbxNavPvtUart1 = 0x20910007;
        internal const uint CfgMsgNmeaGgaUart1 = 0x209100bb;
        internal const uint CfgMsgNmeaGsaUart1 = 0x209100c0;
        internal const uint CfgMsgNmeaGsvUart1 = 0x209100c5;
        internal const uint CfgMsgNmeaRmcUart1 = 0x209100ac;
        internal const uint CfgMsgNmeaZdaUart1 = 0x209100d9;
        internal const uint CfgMsgUbxNavTimeGpsUart1 = 0x20910048;
        internal const uint CfgMsgUbxNavTimeLsUart1 = 0x20910061;
        internal const uint CfgMsgUbxNavTimeUtcUart1 = 0x2091005c;
        internal const uint CfgMsgUbxTimSvinUart1 = 0x20910098;

        internal const uint CfgTmodeMode = 0x20030001;
        internal const uint CfgTmodePositionType = 0x20030002;
        internal const uint CfgTmodeEcefX = 0x40030003;
        internal const uint CfgTmodeEcefY = 0x40030004;
        internal const uint CfgTmodeEcefZ = 0x40030005;
        internal const uint CfgTmodeEcefXHighPrecision = 0x20030006;
        internal const uint CfgTmodeEcefYHighPrecision = 0x20030007;
        internal const uint CfgTmodeEcefZHighPrecision = 0x20030008;
        internal const uint CfgTmodeLatitude = 0x40030009;
        internal const uint CfgTmodeLongitude = 0x4003000a;
        internal const uint CfgTmodeHeight = 0x4003000b;
        internal const uint CfgTmodeLatitudeHighPrecision = 0x2003000c;
        internal const uint CfgTmodeLongitudeHighPrecision = 0x2003000d;
        internal const uint CfgTmodeHeightHighPrecision = 0x2003000e;
        internal const uint CfgTmodeFixedPositionAccuracy = 0x4003000f;
        internal const uint CfgTmodeSurveyInMinimumDuration = 0x40030010;
        internal const uint CfgTmodeSurveyInAccuracyLimit = 0x40030011;

        private static readonly uint[] RateKeys =
        {
            CfgRateMeas, CfgRateNav, CfgRateTimeRef, CfgDynamicModel
        };

        private static readonly uint[] SignalKeys =
        {
            CfgSignalGps, CfgSignalSbas, CfgSignalGalileo,
            CfgSignalBeiDou, CfgSignalQzss, CfgSignalGlonass
        };

        private static readonly uint[] TimePulseKeys =
        {
            CfgTpPulseDefinition, CfgTpLengthDefinition, CfgTpPeriod,
            CfgTpPeriodLocked, CfgTpLength, CfgTpLengthLocked, CfgTpEnabled,
            CfgTpSyncGnss, CfgTpUseLocked, CfgTpAlignTow, CfgTpPolarity,
            CfgTpTimeGrid
        };

        private static readonly uint[] MessageKeys =
        {
            CfgMsgUbxNavPvtUart1, CfgMsgNmeaGgaUart1,
            CfgMsgNmeaGsaUart1, CfgMsgNmeaGsvUart1,
            CfgMsgNmeaRmcUart1, CfgMsgNmeaZdaUart1,
            CfgMsgUbxNavTimeGpsUart1, CfgMsgUbxNavTimeLsUart1,
            CfgMsgUbxNavTimeUtcUart1, CfgMsgUbxTimSvinUart1
        };

        private static readonly uint[] TimingModeKeys =
        {
            CfgTmodeMode, CfgTmodePositionType,
            CfgTmodeLatitude, CfgTmodeLongitude, CfgTmodeHeight,
            CfgTmodeLatitudeHighPrecision, CfgTmodeLongitudeHighPrecision,
            CfgTmodeHeightHighPrecision, CfgTmodeFixedPositionAccuracy,
            CfgTmodeSurveyInMinimumDuration, CfgTmodeSurveyInAccuracyLimit
        };

        private readonly TimeCardClient transport;
        private readonly uint port;
        private readonly uint baud;

        public UbloxClient(TimeCardClient transport, uint port, uint baud)
        {
            if (transport == null)
                throw new ArgumentNullException("transport");
            if (port > 1)
                throw new ArgumentOutOfRangeException("port");
            if (baud == 0)
                throw new ArgumentOutOfRangeException("baud");
            this.transport = transport;
            this.port = port;
            this.baud = baud;
        }

        public UbloxReceiverSnapshot Refresh()
        {
            Dictionary<uint, ulong> configuration = new Dictionary<uint, ulong>();
            List<string> warnings = new List<string>();
            UbloxReceiverSnapshot snapshot = new UbloxReceiverSnapshot(configuration, warnings);
            IDictionary<ushort, byte[]> passiveMessages = null;

            try
            {
                ParseVersion(Poll(0x0a, 0x04), snapshot);
            }
            catch (Exception pollError)
            {
                passiveMessages = transport.CaptureUbxMessages(port, baud, 5000);
                byte[] version;
                if (!passiveMessages.ContainsKey(MessageKey(0x0a, 0x04)) &&
                    !LooksLikeF9TimingStream(passiveMessages))
                {
                    /*
                     * A receive-only FPGA path can begin its first capture
                     * between navigation epochs, particularly just after the
                     * unanswered MON-VER poll flushed the 16550 FIFO. Merge a
                     * second full epoch window before classifying the module.
                     */
                    IDictionary<ushort, byte[]> retryMessages =
                        transport.CaptureUbxMessages(port, baud, 5000);
                    foreach (KeyValuePair<ushort, byte[]> message in retryMessages)
                        passiveMessages[message.Key] = message.Value;
                }
                if (passiveMessages.TryGetValue(MessageKey(0x0a, 0x04),
                    out version))
                {
                    ParseVersion(version, snapshot);
                }
                else if (LooksLikeF9TimingStream(passiveMessages))
                {
                    snapshot.Module = "ZED/RCB-F9T (passive stream)";
                    snapshot.Firmware = "reported only in UBX-MON-VER";
                    snapshot.ProtocolVersion = "F9 UBX";
                    snapshot.SupportedGnss = "decoded from live stream";
                    snapshot.ReceiverFamily = "u-blox F9-compatible timing";
                    snapshot.IsF9TimingReceiver = true;
                }
                else if (passiveMessages.Count != 0)
                {
                    snapshot.Module = "u-blox receiver (passive stream)";
                    snapshot.Firmware = "reported only in UBX-MON-VER";
                    snapshot.ProtocolVersion = "UBX";
                    snapshot.SupportedGnss = "decoded from live stream";
                    snapshot.ReceiverFamily = "u-blox GNSS";
                }
                else
                {
                    throw new InvalidOperationException(
                        "The receiver did not answer UBX-MON-VER and no F9 timing stream was observed.",
                        pollError);
                }
                snapshot.IsPassiveStream = true;
                warnings.Add("Receiver control path did not answer; decoded the live UART stream in read-only passive mode.");
            }
            if (snapshot.IsF9TimingReceiver)
            {
                TryParseMessage(passiveMessages, 0x0d, 0x04, "UBX-TIM-SVIN",
                    payload => ParseSurveyIn(payload, snapshot), warnings);
                TryParseMessage(passiveMessages, 0x0a, 0x38, "UBX-MON-RF",
                    payload => ParseRfStatus(payload, snapshot), warnings);
                TryParseMessage(passiveMessages, 0x27, 0x03, "UBX-SEC-UNIQID",
                    payload => ParseUniqueId(payload, snapshot), warnings);
                TryParseMessage(passiveMessages, 0x01, 0x20, "UBX-NAV-TIMEGPS",
                    payload => ParseGpsTime(payload, snapshot), warnings);
                TryParseMessage(passiveMessages, 0x01, 0x21, "UBX-NAV-TIMEUTC",
                    payload => ParseUtcTime(payload, snapshot), warnings);
                TryParseMessage(passiveMessages, 0x01, 0x26, "UBX-NAV-TIMELS",
                    payload => ParseLeapSeconds(payload, snapshot), warnings);
            }
            TryParseMessage(passiveMessages, 0x01, 0x07, "UBX-NAV-PVT",
                payload => ParseNavigation(payload, snapshot), warnings);
            TryParseMessage(passiveMessages, 0x01, 0x35, "UBX-NAV-SAT",
                payload => ParseSatellites(payload, snapshot), warnings);

            if (snapshot.IsPassiveStream)
                return snapshot;
            snapshot.RateConfigurationSupported =
                TryReadConfiguration("rate and platform", RateKeys,
                configuration, warnings);
            snapshot.SignalConfigurationSupported =
                TryReadConfiguration("GNSS signals", SignalKeys,
                configuration, warnings);
            snapshot.TimePulseConfigurationSupported =
                TryReadConfiguration("time pulse", TimePulseKeys,
                configuration, warnings);
            snapshot.MessageConfigurationSupported =
                TryReadConfiguration("message output", MessageKeys,
                configuration, warnings);
            snapshot.TimingConfigurationSupported = snapshot.IsF9TimingReceiver &&
                TryReadConfiguration("F9T timing mode", TimingModeKeys,
                configuration, warnings);
            snapshot.ConfigurationSupported = snapshot.RateConfigurationSupported ||
                snapshot.SignalConfigurationSupported ||
                snapshot.TimePulseConfigurationSupported ||
                snapshot.MessageConfigurationSupported ||
                snapshot.TimingConfigurationSupported;
            return snapshot;
        }

        private void TryParseMessage(IDictionary<ushort, byte[]> passiveMessages,
            byte messageClass, byte messageId, string name,
            Action<byte[]> parser, ICollection<string> warnings)
        {
            try
            {
                byte[] payload;
                if (passiveMessages != null)
                {
                    if (!passiveMessages.TryGetValue(MessageKey(messageClass,
                        messageId), out payload))
                        return;
                }
                else
                {
                    payload = Poll(messageClass, messageId);
                }
                parser(payload);
            }
            catch (Exception ex)
            {
                warnings.Add(name + ": " + ex.Message);
            }
        }

        private static ushort MessageKey(byte messageClass, byte messageId)
        {
            return (ushort)((messageClass << 8) | messageId);
        }

        private static bool LooksLikeF9TimingStream(
            IDictionary<ushort, byte[]> messages)
        {
            bool timingEvidence =
                messages.ContainsKey(MessageKey(0x0d, 0x04)) ||
                messages.ContainsKey(MessageKey(0x01, 0x20)) ||
                messages.ContainsKey(MessageKey(0x01, 0x26));
            bool receiverEvidence =
                messages.ContainsKey(MessageKey(0x0a, 0x38)) ||
                messages.ContainsKey(MessageKey(0x01, 0x07)) ||
                messages.ContainsKey(MessageKey(0x01, 0x21));
            return timingEvidence && receiverEvidence;
        }

        public void ApplyRateAndModel(ushort measurementMilliseconds,
            ushort navigationRatio, byte timeReference, byte dynamicModel, bool persist)
        {
            SetConfiguration(new[]
            {
                Pair(CfgRateMeas, measurementMilliseconds),
                Pair(CfgRateNav, navigationRatio),
                Pair(CfgRateTimeRef, timeReference),
                Pair(CfgDynamicModel, dynamicModel)
            }, persist);
        }

        public void ApplySignals(bool gps, bool sbas, bool galileo,
            bool beiDou, bool qzss, bool glonass, bool persist)
        {
            SetConfiguration(new[]
            {
                Pair(CfgSignalGps, gps), Pair(CfgSignalSbas, sbas),
                Pair(CfgSignalGalileo, galileo), Pair(CfgSignalBeiDou, beiDou),
                Pair(CfgSignalQzss, qzss), Pair(CfgSignalGlonass, glonass)
            }, persist);
        }

        public void ApplyTimePulse(bool enabled, bool syncGnss, bool useLocked,
            bool risingEdge, byte timeGrid, uint periodMicroseconds,
            uint lockedPeriodMicroseconds, uint lengthMicroseconds,
            uint lockedLengthMicroseconds, bool persist)
        {
            SetConfiguration(new[]
            {
                Pair(CfgTpPulseDefinition, 0),
                Pair(CfgTpLengthDefinition, 1),
                Pair(CfgTpEnabled, enabled),
                Pair(CfgTpSyncGnss, syncGnss),
                Pair(CfgTpUseLocked, useLocked),
                Pair(CfgTpAlignTow, true),
                Pair(CfgTpPolarity, risingEdge),
                Pair(CfgTpTimeGrid, timeGrid),
                Pair(CfgTpPeriod, periodMicroseconds),
                Pair(CfgTpPeriodLocked, lockedPeriodMicroseconds),
                Pair(CfgTpLength, lengthMicroseconds),
                Pair(CfgTpLengthLocked, lockedLengthMicroseconds)
            }, persist);
        }

        public void ApplyMessageRates(byte navPvt, byte gga, byte gsa,
            byte gsv, byte rmc, byte zda, byte timeGps, byte timeUtc,
            byte timeLs, byte surveyIn, bool persist)
        {
            SetConfiguration(new[]
            {
                Pair(CfgMsgUbxNavPvtUart1, navPvt),
                Pair(CfgMsgNmeaGgaUart1, gga),
                Pair(CfgMsgNmeaGsaUart1, gsa),
                Pair(CfgMsgNmeaGsvUart1, gsv),
                Pair(CfgMsgNmeaRmcUart1, rmc),
                Pair(CfgMsgNmeaZdaUart1, zda),
                Pair(CfgMsgUbxNavTimeGpsUart1, timeGps),
                Pair(CfgMsgUbxNavTimeUtcUart1, timeUtc),
                Pair(CfgMsgUbxNavTimeLsUart1, timeLs),
                Pair(CfgMsgUbxTimSvinUart1, surveyIn)
            }, persist);
        }

        public void ApplyTimingMode(byte mode, uint surveyMinimumSeconds,
            uint surveyAccuracyTenthsMillimeter, double latitudeDegrees,
            double longitudeDegrees, double heightMeters,
            uint fixedAccuracyTenthsMillimeter, bool persist)
        {
            if (mode > 2)
                throw new ArgumentOutOfRangeException("mode");

            List<KeyValuePair<uint, ulong>> values =
                new List<KeyValuePair<uint, ulong>> { Pair(CfgTmodeMode, mode) };
            if (mode == 1)
            {
                if (surveyMinimumSeconds == 0 || surveyAccuracyTenthsMillimeter == 0)
                    throw new ArgumentOutOfRangeException("surveyMinimumSeconds",
                        "Survey-in duration and accuracy must both be greater than zero.");
                values.Add(Pair(CfgTmodeSurveyInMinimumDuration,
                    surveyMinimumSeconds));
                values.Add(Pair(CfgTmodeSurveyInAccuracyLimit,
                    surveyAccuracyTenthsMillimeter));
            }
            else if (mode == 2)
            {
                if (double.IsNaN(latitudeDegrees) || latitudeDegrees < -90.0 ||
                    latitudeDegrees > 90.0)
                    throw new ArgumentOutOfRangeException("latitudeDegrees");
                if (double.IsNaN(longitudeDegrees) || longitudeDegrees < -180.0 ||
                    longitudeDegrees > 180.0)
                    throw new ArgumentOutOfRangeException("longitudeDegrees");
                if (double.IsNaN(heightMeters) || double.IsInfinity(heightMeters))
                    throw new ArgumentOutOfRangeException("heightMeters");
                if (fixedAccuracyTenthsMillimeter == 0)
                    throw new ArgumentOutOfRangeException("fixedAccuracyTenthsMillimeter");

                int latitudeBase, latitudeHighPrecision;
                int longitudeBase, longitudeHighPrecision;
                int heightBase, heightHighPrecision;
                SplitCoordinate(latitudeDegrees * 10000000.0,
                    out latitudeBase, out latitudeHighPrecision);
                SplitCoordinate(longitudeDegrees * 10000000.0,
                    out longitudeBase, out longitudeHighPrecision);
                SplitCoordinate(heightMeters * 100.0,
                    out heightBase, out heightHighPrecision);

                values.Add(Pair(CfgTmodePositionType, 1));
                values.Add(PairSigned32(CfgTmodeLatitude, latitudeBase));
                values.Add(PairSigned32(CfgTmodeLongitude, longitudeBase));
                values.Add(PairSigned32(CfgTmodeHeight, heightBase));
                values.Add(PairSigned8(CfgTmodeLatitudeHighPrecision,
                    latitudeHighPrecision));
                values.Add(PairSigned8(CfgTmodeLongitudeHighPrecision,
                    longitudeHighPrecision));
                values.Add(PairSigned8(CfgTmodeHeightHighPrecision,
                    heightHighPrecision));
                values.Add(Pair(CfgTmodeFixedPositionAccuracy,
                    fixedAccuracyTenthsMillimeter));
            }
            SetConfiguration(values, persist);
        }

        private byte[] Poll(byte messageClass, byte messageId)
        {
            return transport.ExecuteUbxPoll(port, baud, messageClass, messageId,
                new byte[0], 1500);
        }

        private bool TryReadConfiguration(string group, uint[] keys,
            IDictionary<uint, ulong> configuration, ICollection<string> warnings)
        {
            try
            {
                byte[] request = new byte[4 + keys.Length * 4];
                request[0] = 0;
                request[1] = 0;
                for (int index = 0; index < keys.Length; index++)
                    WriteUInt32(request, 4 + index * 4, keys[index]);
                byte[] response = transport.ExecuteUbxPoll(port, baud, 0x06, 0x8b,
                    request, 1500);
                ParseConfiguration(response, configuration);
                return true;
            }
            catch (Exception ex)
            {
                warnings.Add("Configuration " + group + ": " + ex.Message);
                return false;
            }
        }

        private void SetConfiguration(IEnumerable<KeyValuePair<uint, ulong>> values,
            bool persist)
        {
            List<KeyValuePair<uint, ulong>> items = values.ToList();
            int length = 4 + items.Sum(item => 4 + KeySize(item.Key));
            byte[] payload = new byte[length];
            payload[0] = 0;
            payload[1] = persist ? (byte)0x07 : (byte)0x01;
            int offset = 4;
            foreach (KeyValuePair<uint, ulong> item in items)
            {
                WriteUInt32(payload, offset, item.Key);
                offset += 4;
                int size = KeySize(item.Key);
                for (int index = 0; index < size; index++)
                    payload[offset + index] = (byte)((item.Value >> (index * 8)) & 0xff);
                offset += size;
            }
            transport.ExecuteUbxSet(port, baud, 0x06, 0x8a, payload, 2000);
        }

        private static void ParseVersion(byte[] payload, UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 40)
                throw new InvalidOperationException("UBX-MON-VER returned a truncated payload.");
            snapshot.SoftwareVersion = ReadAscii(payload, 0, 30);
            snapshot.HardwareVersion = ReadAscii(payload, 30, 10);
            List<string> extensions = new List<string>();
            for (int offset = 40; offset + 30 <= payload.Length; offset += 30)
            {
                string extension = ReadAscii(payload, offset, 30);
                if (!string.IsNullOrWhiteSpace(extension))
                    extensions.Add(extension);
            }
            snapshot.Module = ExtensionValue(extensions, "MOD=") ?? "u-blox receiver";
            snapshot.Firmware = ExtensionValue(extensions, "FWVER=") ?? snapshot.SoftwareVersion;
            snapshot.ProtocolVersion = ExtensionValue(extensions, "PROTVER=") ?? "unknown";
            snapshot.SupportedGnss = extensions.FirstOrDefault(value =>
                value.IndexOf("GPS", StringComparison.OrdinalIgnoreCase) >= 0 &&
                value.IndexOf(';') >= 0) ?? "not reported";
            string identity = string.Join(" ", new[] { snapshot.Module,
                snapshot.Firmware, snapshot.SoftwareVersion });
            snapshot.IsF9TimingReceiver =
                identity.IndexOf("F9T", StringComparison.OrdinalIgnoreCase) >= 0;
            snapshot.ReceiverFamily = snapshot.IsF9TimingReceiver ?
                "u-blox F9 high-accuracy timing" : "u-blox GNSS";
        }

        private static void ParseSurveyIn(byte[] payload,
            UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 28)
                throw new InvalidOperationException(
                    "The survey-in payload is shorter than 28 bytes.");
            snapshot.SurveyInDurationSeconds = ReadUInt32(payload, 0);
            snapshot.SurveyInMeanAccuracyMillimeters = Math.Sqrt(
                ReadUInt32(payload, 16));
            snapshot.SurveyInObservations = ReadUInt32(payload, 20);
            snapshot.SurveyInValid = payload[24] != 0;
            snapshot.SurveyInActive = payload[25] != 0;
            snapshot.SurveyInStatusSupported = true;
        }

        private static void ParseRfStatus(byte[] payload,
            UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 4)
                throw new InvalidOperationException("The RF payload is truncated.");
            int count = payload[1];
            if (payload.Length < 4 + count * 24)
                throw new InvalidOperationException("The RF block count is invalid.");
            List<UbloxRfBlockInfo> blocks = new List<UbloxRfBlockInfo>();
            for (int index = 0; index < count; index++)
            {
                int offset = 4 + index * 24;
                blocks.Add(new UbloxRfBlockInfo(payload[offset],
                    (byte)(payload[offset + 1] & 0x03), payload[offset + 2],
                    payload[offset + 3], ReadUInt16(payload, offset + 12),
                    ReadUInt16(payload, offset + 14), payload[offset + 16]));
            }
            snapshot.RfBlocks = blocks.AsReadOnly();
            snapshot.RfStatusSupported = true;
        }

        private static void ParseUniqueId(byte[] payload,
            UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 9)
                throw new InvalidOperationException(
                    "The unique-ID payload is shorter than 9 bytes.");
            snapshot.UniqueChipId = string.Join("", payload.Skip(4).Take(5)
                .Select(value => value.ToString("X2", CultureInfo.InvariantCulture)));
        }

        private static void ParseGpsTime(byte[] payload,
            UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 16)
                throw new InvalidOperationException(
                    "The GPS-time payload is shorter than 16 bytes.");
            byte valid = payload[11];
            snapshot.GpsTimeOfWeekSeconds = ReadUInt32(payload, 0) / 1000.0 +
                ReadInt32(payload, 4) / 1000000000.0;
            snapshot.GpsWeek = unchecked((short)ReadUInt16(payload, 8));
            snapshot.GpsUtcLeapSeconds = unchecked((sbyte)payload[10]);
            snapshot.GpsTimeValid = (valid & 0x03) == 0x03;
            snapshot.GpsTimeAccuracyNanoseconds = ReadUInt32(payload, 12);
            snapshot.GpsTimeSupported = true;
        }

        private static void ParseUtcTime(byte[] payload,
            UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 20)
                throw new InvalidOperationException(
                    "The UTC-time payload is shorter than 20 bytes.");
            snapshot.TimeAccuracyNanoseconds = ReadUInt32(payload, 4);
            if ((payload[19] & 0x04) == 0)
                return;
            try
            {
                DateTime value = new DateTime(ReadUInt16(payload, 12), payload[14],
                    payload[15], payload[16], payload[17],
                    Math.Min(payload[18], (byte)59), DateTimeKind.Utc);
                long fractionalTicks = ReadInt32(payload, 8) / 100;
                snapshot.Utc = value.AddTicks(fractionalTicks);
            }
            catch (ArgumentOutOfRangeException)
            {
                snapshot.Utc = null;
            }
        }

        private static void ParseLeapSeconds(byte[] payload,
            UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 24)
                throw new InvalidOperationException(
                    "The leap-second payload is shorter than 24 bytes.");
            snapshot.CurrentLeapSecondSource = payload[8];
            snapshot.CurrentLeapSeconds = unchecked((sbyte)payload[9]);
            snapshot.LeapSecondEventSource = payload[10];
            snapshot.LeapSecondChange = unchecked((sbyte)payload[11]);
            snapshot.SecondsToLeapSecondEvent = ReadInt32(payload, 12);
            snapshot.LeapSecondEventGpsWeek = ReadUInt16(payload, 16);
            snapshot.LeapSecondEventGpsDay = ReadUInt16(payload, 18);
            snapshot.CurrentLeapSecondsValid = (payload[23] & 0x01) != 0;
            snapshot.LeapSecondEventValid = (payload[23] & 0x02) != 0;
            snapshot.LeapSecondStatusSupported = true;
        }

        private static void ParseNavigation(byte[] payload, UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 92)
                throw new InvalidOperationException("The navigation payload is shorter than 92 bytes.");
            snapshot.FixType = payload[20];
            snapshot.FixValid = (payload[21] & 0x01) != 0;
            snapshot.DifferentialSolution = (payload[21] & 0x02) != 0;
            snapshot.SatellitesUsed = payload[23];
            snapshot.Longitude = ReadInt32(payload, 24) * 1e-7;
            snapshot.Latitude = ReadInt32(payload, 28) * 1e-7;
            snapshot.HorizontalAccuracyMillimeters = ReadUInt32(payload, 40);
            snapshot.VerticalAccuracyMillimeters = ReadUInt32(payload, 44);
            snapshot.PositionDop = ReadUInt16(payload, 76) * 0.01;
            snapshot.TimeAccuracyNanoseconds = ReadUInt32(payload, 12);

            byte valid = payload[11];
            if ((valid & 0x03) == 0x03)
            {
                try
                {
                    snapshot.Utc = new DateTime(ReadUInt16(payload, 4), payload[6],
                        payload[7], payload[8], payload[9], Math.Min(payload[10], (byte)59),
                        DateTimeKind.Utc);
                }
                catch (ArgumentOutOfRangeException)
                {
                    snapshot.Utc = null;
                }
            }
        }

        private static void ParseSatellites(byte[] payload, UbloxReceiverSnapshot snapshot)
        {
            if (payload.Length < 8)
                throw new InvalidOperationException("The satellite payload is truncated.");
            int count = payload[5];
            if (payload.Length < 8 + count * 12)
                throw new InvalidOperationException("The satellite block count is invalid.");
            int used = 0;
            int cnoTotal = 0;
            int cnoCount = 0;
            Dictionary<byte, int> constellations = new Dictionary<byte, int>();
            List<UbloxSatelliteInfo> satellites = new List<UbloxSatelliteInfo>();
            for (int index = 0; index < count; index++)
            {
                int offset = 8 + index * 12;
                byte gnss = payload[offset];
                byte satelliteIdentifier = payload[offset + 1];
                byte cno = payload[offset + 2];
                int elevation = unchecked((sbyte)payload[offset + 3]);
                int azimuth = unchecked((short)ReadUInt16(payload, offset + 4));
                uint flags = ReadUInt32(payload, offset + 8);
                satellites.Add(new UbloxSatelliteInfo(gnss,
                    satelliteIdentifier, cno, elevation, azimuth, flags,
                    GnssName(gnss), SatellitePrefix(gnss) +
                    satelliteIdentifier.ToString(CultureInfo.InvariantCulture)));
                if ((flags & 0x08) != 0)
                    used++;
                if (cno != 0)
                {
                    cnoTotal += cno;
                    cnoCount++;
                }
                int value;
                constellations.TryGetValue(gnss, out value);
                constellations[gnss] = value + 1;
            }
            snapshot.SatellitesVisible = count;
            snapshot.Satellites = satellites.AsReadOnly();
            snapshot.CapturedAtUtc = DateTime.UtcNow;
            snapshot.AverageCno = cnoCount == 0 ? 0 : cnoTotal / (double)cnoCount;
            snapshot.ConstellationSummary = string.Join(" · ", constellations
                .OrderBy(item => item.Key)
                .Select(item => GnssName(item.Key) + " " + item.Value));
            if (snapshot.SatellitesUsed == 0 && used > 0)
                snapshot.SatellitesUsed = (byte)Math.Min(used, byte.MaxValue);
        }

        private static void ParseConfiguration(byte[] payload,
            IDictionary<uint, ulong> configuration)
        {
            if (payload.Length < 4 || payload[0] != 1)
                throw new InvalidOperationException("UBX-CFG-VALGET returned an unsupported response.");
            int offset = 4;
            while (offset + 4 <= payload.Length)
            {
                uint key = ReadUInt32(payload, offset);
                offset += 4;
                int size = KeySize(key);
                if (offset + size > payload.Length)
                    throw new InvalidOperationException("A configuration value is truncated.");
                ulong value = 0;
                for (int index = 0; index < size; index++)
                    value |= (ulong)payload[offset + index] << (index * 8);
                offset += size;
                configuration[key] = value;
            }
        }

        private static KeyValuePair<uint, ulong> Pair(uint key, bool value)
        {
            return Pair(key, value ? 1UL : 0UL);
        }

        private static KeyValuePair<uint, ulong> Pair(uint key, ulong value)
        {
            return new KeyValuePair<uint, ulong>(key, value);
        }

        private static KeyValuePair<uint, ulong> PairSigned32(uint key, int value)
        {
            return Pair(key, unchecked((uint)value));
        }

        private static KeyValuePair<uint, ulong> PairSigned8(uint key, int value)
        {
            if (value < -99 || value > 99)
                throw new ArgumentOutOfRangeException("value");
            return Pair(key, unchecked((byte)(sbyte)value));
        }

        private static void SplitCoordinate(double scaledValue, out int whole,
            out int highPrecision)
        {
            whole = checked((int)Math.Truncate(scaledValue));
            highPrecision = (int)Math.Round((scaledValue - whole) * 100.0,
                MidpointRounding.AwayFromZero);
            if (highPrecision >= 100)
            {
                whole = checked(whole + 1);
                highPrecision -= 100;
            }
            else if (highPrecision <= -100)
            {
                whole = checked(whole - 1);
                highPrecision += 100;
            }
        }

        private static int KeySize(uint key)
        {
            switch ((key >> 28) & 0x07)
            {
                case 1:
                case 2:
                    return 1;
                case 3:
                    return 2;
                case 4:
                    return 4;
                case 5:
                    return 8;
                default:
                    throw new InvalidOperationException(string.Format(
                        CultureInfo.InvariantCulture,
                        "Configuration key 0x{0:X8} has an unsupported value size.", key));
            }
        }

        private static string ExtensionValue(IEnumerable<string> extensions, string prefix)
        {
            string value = extensions.FirstOrDefault(item =>
                item.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
            return value == null ? null : value.Substring(prefix.Length).Trim();
        }

        private static string ReadAscii(byte[] data, int offset, int length)
        {
            int count = 0;
            while (count < length && data[offset + count] != 0)
                count++;
            return Encoding.ASCII.GetString(data, offset, count).Trim();
        }

        private static string GnssName(byte identifier)
        {
            switch (identifier)
            {
                case 0: return "GPS";
                case 1: return "SBAS";
                case 2: return "Galileo";
                case 3: return "BeiDou";
                case 5: return "QZSS";
                case 6: return "GLONASS";
                case 7: return "NavIC";
                default: return "GNSS " + identifier.ToString(CultureInfo.InvariantCulture);
            }
        }

        private static string SatellitePrefix(byte identifier)
        {
            switch (identifier)
            {
                case 0: return "G";
                case 1: return "S";
                case 2: return "E";
                case 3: return "C";
                case 5: return "Q";
                case 6: return "R";
                case 7: return "I";
                default: return "?";
            }
        }

        private static ushort ReadUInt16(byte[] data, int offset)
        {
            return (ushort)(data[offset] | (data[offset + 1] << 8));
        }

        private static uint ReadUInt32(byte[] data, int offset)
        {
            return (uint)(data[offset] | (data[offset + 1] << 8) |
                (data[offset + 2] << 16) | (data[offset + 3] << 24));
        }

        private static int ReadInt32(byte[] data, int offset)
        {
            return unchecked((int)ReadUInt32(data, offset));
        }

        private static void WriteUInt32(byte[] data, int offset, uint value)
        {
            data[offset] = (byte)(value & 0xff);
            data[offset + 1] = (byte)((value >> 8) & 0xff);
            data[offset + 2] = (byte)((value >> 16) & 0xff);
            data[offset + 3] = (byte)((value >> 24) & 0xff);
        }
    }
}
