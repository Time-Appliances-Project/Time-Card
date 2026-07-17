using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace TimeCardControlCenter
{
    public sealed class UbloxDecodedMessage
    {
        internal UbloxDecodedMessage(string protocol, string name,
            string identifier, int frameLength, int payloadLength,
            bool? checksumValid, string summary, string details)
        {
            Protocol = protocol;
            Name = name;
            Identifier = identifier;
            FrameLength = frameLength;
            PayloadLength = payloadLength;
            ChecksumValid = checksumValid;
            Summary = summary ?? string.Empty;
            Details = details ?? string.Empty;
        }

        public string Protocol { get; private set; }
        public string Name { get; private set; }
        public string Identifier { get; private set; }
        public int FrameLength { get; private set; }
        public int PayloadLength { get; private set; }
        public bool? ChecksumValid { get; private set; }
        public string Summary { get; private set; }
        public string Details { get; private set; }

        public string ToConsoleText()
        {
            string lengthText = Protocol == "NMEA" ?
                FrameLength.ToString(CultureInfo.InvariantCulture) +
                    "-byte sentence" :
                PayloadLength.ToString(CultureInfo.InvariantCulture) +
                    "-byte payload";
            string checksumText = !ChecksumValid.HasValue ?
                "checksum not supplied" :
                (ChecksumValid.Value ? "checksum OK" : "CHECKSUM FAILED");
            StringBuilder output = new StringBuilder();
            output.Append(Name);
            if (!string.IsNullOrWhiteSpace(Identifier))
                output.Append(" [").Append(Identifier).Append(']');
            output.Append(" · ").Append(lengthText).Append(" · ")
                .Append(checksumText);
            if (!string.IsNullOrWhiteSpace(Summary))
                output.Append("\r\n  ").Append(Summary);
            if (!string.IsNullOrWhiteSpace(Details))
                output.Append("\r\n  ").Append(Details);
            return output.ToString();
        }
    }

    public sealed class UbloxDecodeBatch
    {
        internal UbloxDecodeBatch(List<UbloxDecodedMessage> messages,
            int bufferedBytes, int discardedBytes)
        {
            Messages = messages.AsReadOnly();
            BufferedBytes = bufferedBytes;
            DiscardedBytes = discardedBytes;
        }

        public IList<UbloxDecodedMessage> Messages { get; private set; }
        public int BufferedBytes { get; private set; }
        public int DiscardedBytes { get; private set; }
    }

    public sealed class UbloxStreamDecoder
    {
        private const int MaximumBufferedBytes = 65536;
        private const int MaximumUbxPayloadBytes = 16384;
        private const int MaximumNmeaSentenceBytes = 1024;
        private readonly List<byte> buffer = new List<byte>();

        public long TotalMessages { get; private set; }
        public long ChecksumFailures { get; private set; }
        public long TotalDiscardedBytes { get; private set; }
        public int BufferedBytes { get { return buffer.Count; } }

        public void Reset()
        {
            buffer.Clear();
            TotalMessages = 0;
            ChecksumFailures = 0;
            TotalDiscardedBytes = 0;
        }

        public UbloxDecodeBatch Feed(byte[] data)
        {
            if (data == null)
                throw new ArgumentNullException("data");

            int discarded = 0;
            if (data.Length != 0)
                buffer.AddRange(data);
            if (buffer.Count > MaximumBufferedBytes)
            {
                int overflow = buffer.Count - MaximumBufferedBytes;
                buffer.RemoveRange(0, overflow);
                discarded += overflow;
            }

            List<UbloxDecodedMessage> messages =
                new List<UbloxDecodedMessage>();
            while (buffer.Count != 0)
            {
                int start = FindFrameStart();
                if (start < 0)
                {
                    int retained = buffer[buffer.Count - 1] == 0xb5 ? 1 : 0;
                    int count = buffer.Count - retained;
                    if (count != 0)
                    {
                        buffer.RemoveRange(0, count);
                        discarded += count;
                    }
                    break;
                }
                if (start != 0)
                {
                    buffer.RemoveRange(0, start);
                    discarded += start;
                }

                byte marker = buffer[0];
                if (marker == 0xb5)
                {
                    if (!TryDecodeUbx(messages, ref discarded))
                        break;
                }
                else if (marker == (byte)'$' || marker == (byte)'!')
                {
                    if (!TryDecodeNmea(messages, ref discarded))
                        break;
                }
                else
                {
                    if (!TryDecodeRtcm(messages, ref discarded))
                        break;
                }
            }

            TotalDiscardedBytes += discarded;
            return new UbloxDecodeBatch(messages, buffer.Count, discarded);
        }

        private int FindFrameStart()
        {
            for (int index = 0; index < buffer.Count; index++)
            {
                byte value = buffer[index];
                if (value == 0xb5 || value == (byte)'$' ||
                    value == (byte)'!' || value == 0xd3)
                    return index;
            }
            return -1;
        }

        private bool TryDecodeUbx(ICollection<UbloxDecodedMessage> messages,
                                  ref int discarded)
        {
            if (buffer.Count < 2)
                return false;
            if (buffer[1] != 0x62)
            {
                buffer.RemoveAt(0);
                discarded++;
                return true;
            }
            if (buffer.Count < 6)
                return false;

            int payloadLength = buffer[4] | (buffer[5] << 8);
            if (payloadLength > MaximumUbxPayloadBytes)
            {
                buffer.RemoveAt(0);
                discarded++;
                return true;
            }
            int frameLength = payloadLength + 8;
            if (buffer.Count < frameLength)
                return false;

            byte checksumA = 0;
            byte checksumB = 0;
            for (int index = 2; index < 6 + payloadLength; index++)
            {
                checksumA = unchecked((byte)(checksumA + buffer[index]));
                checksumB = unchecked((byte)(checksumB + checksumA));
            }
            bool checksumValid = checksumA == buffer[6 + payloadLength] &&
                checksumB == buffer[7 + payloadLength];
            byte messageClass = buffer[2];
            byte messageId = buffer[3];
            byte[] payload = new byte[payloadLength];
            if (payloadLength != 0)
                buffer.CopyTo(6, payload, 0, payloadLength);

            string summary;
            string details;
            DecodeUbxPayload(messageClass, messageId, payload,
                out summary, out details);
            UbloxDecodedMessage message = new UbloxDecodedMessage(
                "UBX", UbxMessageName(messageClass, messageId),
                string.Format(CultureInfo.InvariantCulture, "{0:X2}/{1:X2}",
                    messageClass, messageId), frameLength, payloadLength,
                checksumValid, summary, details);
            AddMessage(messages, message);
            buffer.RemoveRange(0, frameLength);
            return true;
        }

        private bool TryDecodeNmea(ICollection<UbloxDecodedMessage> messages,
                                   ref int discarded)
        {
            int lineEnd = -1;
            int searchLimit = Math.Min(buffer.Count, MaximumNmeaSentenceBytes + 1);
            for (int index = 0; index < searchLimit; index++)
            {
                if (buffer[index] == (byte)'\n')
                {
                    lineEnd = index;
                    break;
                }
            }
            if (lineEnd < 0)
            {
                if (buffer.Count <= MaximumNmeaSentenceBytes)
                    return false;
                buffer.RemoveAt(0);
                discarded++;
                return true;
            }

            int characterCount = lineEnd + 1;
            byte[] sentenceBytes = buffer.GetRange(0, characterCount).ToArray();
            string sentence = Encoding.ASCII.GetString(sentenceBytes)
                .TrimEnd('\r', '\n');
            buffer.RemoveRange(0, characterCount);
            AddMessage(messages, DecodeNmeaSentence(sentence, characterCount));
            return true;
        }

        private bool TryDecodeRtcm(ICollection<UbloxDecodedMessage> messages,
                                   ref int discarded)
        {
            if (buffer.Count < 3)
                return false;
            if ((buffer[1] & 0xfc) != 0)
            {
                buffer.RemoveAt(0);
                discarded++;
                return true;
            }

            int payloadLength = ((buffer[1] & 0x03) << 8) | buffer[2];
            int frameLength = payloadLength + 6;
            if (buffer.Count < frameLength)
                return false;

            uint expected = (uint)((buffer[frameLength - 3] << 16) |
                (buffer[frameLength - 2] << 8) | buffer[frameLength - 1]);
            uint actual = Crc24Q(buffer, frameLength - 3);
            int messageType = payloadLength >= 2 ?
                (buffer[3] << 4) | (buffer[4] >> 4) : -1;
            string name = messageType < 0 ? "RTCM3" :
                "RTCM3-" + messageType.ToString(CultureInfo.InvariantCulture);
            string summary = messageType < 0 ? "Empty RTCM3 frame" :
                "RTCM3 correction message type " +
                messageType.ToString(CultureInfo.InvariantCulture);
            AddMessage(messages, new UbloxDecodedMessage(
                "RTCM3", name,
                messageType < 0 ? string.Empty :
                    messageType.ToString(CultureInfo.InvariantCulture),
                frameLength, payloadLength, actual == expected, summary,
                "CRC-24Q " + (actual == expected ? "validated" :
                    string.Format(CultureInfo.InvariantCulture,
                        "expected 0x{0:X6}, calculated 0x{1:X6}",
                        expected, actual))));
            buffer.RemoveRange(0, frameLength);
            return true;
        }

        private void AddMessage(ICollection<UbloxDecodedMessage> messages,
                                UbloxDecodedMessage message)
        {
            messages.Add(message);
            TotalMessages++;
            if (message.ChecksumValid.HasValue &&
                !message.ChecksumValid.Value)
                ChecksumFailures++;
        }

        private static UbloxDecodedMessage DecodeNmeaSentence(string sentence,
                                                               int frameLength)
        {
            int star = sentence.LastIndexOf('*');
            bool? checksumValid = null;
            string body = sentence.Length > 1 ? sentence.Substring(1) : string.Empty;
            if (star > 0 && star + 2 < sentence.Length)
            {
                int expectedHigh = HexValue(sentence[star + 1]);
                int expectedLow = HexValue(sentence[star + 2]);
                if (expectedHigh >= 0 && expectedLow >= 0)
                {
                    int checksum = 0;
                    for (int index = 1; index < star; index++)
                        checksum ^= sentence[index];
                    checksumValid = checksum == ((expectedHigh << 4) | expectedLow);
                }
                body = sentence.Substring(1, star - 1);
            }

            string[] fields = body.Split(',');
            string header = fields.Length == 0 ? "UNKNOWN" : fields[0];
            string type = header.Length >= 3 ?
                header.Substring(header.Length - 3).ToUpperInvariant() :
                header.ToUpperInvariant();
            string name = "NMEA-" + header.ToUpperInvariant();
            if (header.Equals("PUBX", StringComparison.OrdinalIgnoreCase) &&
                fields.Length > 1)
                name += "-" + fields[1];

            string summary;
            string details;
            DecodeNmeaFields(type, fields, out summary, out details);
            return new UbloxDecodedMessage("NMEA", name, type, frameLength,
                Math.Max(0, body.Length), checksumValid, summary, details);
        }

        private static void DecodeNmeaFields(string type, string[] fields,
                                             out string summary,
                                             out string details)
        {
            summary = NmeaDescription(type);
            details = NmeaFieldsText(fields);
            if (type == "RMC" && fields.Length >= 10)
            {
                string validity = fields[2] == "A" ? "valid" : "not valid";
                summary = "Recommended minimum navigation · " + validity +
                    (fields.Length > 12 && !string.IsNullOrWhiteSpace(fields[12]) ?
                        " · " + NmeaModeDescription(fields[12]) : string.Empty);
                details = JoinNonEmpty(
                    FormatNmeaDateTime(fields[9], fields[1]),
                    FormatNmeaPosition(fields[3], fields[4], fields[5], fields[6]),
                    FormatNmeaSpeed(fields[7], fields[8]));
            }
            else if (type == "GGA" && fields.Length >= 11)
            {
                summary = "Fix data · " + NmeaQualityName(fields[6]) +
                    " · " + EmptyAsDash(fields[7]) + " satellites";
                details = JoinNonEmpty(
                    FormatNmeaTime(fields[1]),
                    FormatNmeaPosition(fields[2], fields[3], fields[4], fields[5]),
                    "HDOP " + EmptyAsDash(fields[8]) + " · altitude " +
                        EmptyAsDash(fields[9]) + " " + EmptyAsDash(fields[10]),
                    fields.Length > 12 ? "Geoid separation " +
                        EmptyAsDash(fields[11]) + " " + EmptyAsDash(fields[12]) :
                        string.Empty);
            }
            else if (type == "GSA" && fields.Length >= 18)
            {
                List<string> satellites = new List<string>();
                for (int index = 3; index <= 14 && index < fields.Length; index++)
                {
                    if (!string.IsNullOrWhiteSpace(fields[index]))
                        satellites.Add(fields[index]);
                }
                summary = "DOP and active satellites · " +
                    NmeaFixName(fields[2]) + " · " + satellites.Count.ToString(
                        CultureInfo.InvariantCulture) + " SV";
                details = "PDOP " + EmptyAsDash(fields[15]) + " · HDOP " +
                    EmptyAsDash(fields[16]) + " · VDOP " + EmptyAsDash(fields[17]) +
                    " · active " + (satellites.Count == 0 ? "none" :
                        string.Join(", ", satellites.ToArray()));
            }
            else if (type == "GSV" && fields.Length >= 4)
            {
                summary = "Satellites in view · " + EmptyAsDash(fields[3]) +
                    " visible";
                details = "Sentence " + EmptyAsDash(fields[2]) + " of " +
                    EmptyAsDash(fields[1]) + FormatGsvSatellites(fields);
            }
            else if (type == "GLL" && fields.Length >= 7)
            {
                string validity = fields[6] == "A" ? "valid" : "not valid";
                summary = "Geographic position · " + validity +
                    (fields.Length > 7 && !string.IsNullOrWhiteSpace(fields[7]) ?
                        " · " + NmeaModeDescription(fields[7]) : string.Empty);
                details = JoinNonEmpty(
                    FormatNmeaTime(fields[5]),
                    FormatNmeaPosition(fields[1], fields[2], fields[3], fields[4]));
            }
            else if (type == "VTG" && fields.Length >= 9)
            {
                summary = "Course and speed over ground" +
                    (fields.Length > 9 && !string.IsNullOrWhiteSpace(fields[9]) ?
                        " · " + NmeaModeDescription(fields[9]) : string.Empty);
                details = "True course " + EmptyAsDash(fields[1]) + "° · magnetic " +
                    EmptyAsDash(fields[3]) + "° · " + EmptyAsDash(fields[5]) +
                    " kn · " + EmptyAsDash(fields[7]) + " km/h";
            }
            else if (type == "GNS" && fields.Length >= 11)
            {
                summary = "GNSS fix data · " + NmeaModeDescription(fields[6]) +
                    " · " + EmptyAsDash(fields[7]) + " satellites";
                details = JoinNonEmpty(
                    FormatNmeaTime(fields[1]),
                    FormatNmeaPosition(fields[2], fields[3], fields[4], fields[5]),
                    "HDOP " + EmptyAsDash(fields[8]) + " · altitude " +
                        EmptyAsDash(fields[9]) + " m · geoid separation " +
                        EmptyAsDash(fields[10]) + " m");
            }
            else if (type == "ZDA" && fields.Length >= 5)
            {
                summary = "UTC date and time";
                details = EmptyAsDash(fields[4]) + "-" +
                    EmptyAsDash(fields[3]).PadLeft(2, '0') + "-" +
                    EmptyAsDash(fields[2]).PadLeft(2, '0') + " " +
                    FormatNmeaTime(fields[1]);
            }
            else if (type == "GST" && fields.Length >= 9)
            {
                summary = "Position error statistics · RMS " +
                    EmptyAsDash(fields[2]) + " m";
                details = "Latitude σ " + EmptyAsDash(fields[6]) +
                    " m · longitude σ " + EmptyAsDash(fields[7]) +
                    " m · altitude σ " + EmptyAsDash(fields[8]) + " m";
            }
            else if (type == "TXT" && fields.Length >= 5)
            {
                summary = "Receiver text message";
                details = fields[4];
            }
            else if (type == "HDT" && fields.Length >= 2)
            {
                summary = "True heading";
                details = EmptyAsDash(fields[1]) + "° true";
            }
            else if (type == "THS" && fields.Length >= 3)
            {
                summary = "True heading and status · " +
                    (fields[2] == "A" ? "valid" : "not valid");
                details = EmptyAsDash(fields[1]) + "° true";
            }
        }

        private static void DecodeUbxPayload(byte messageClass, byte messageId,
                                             byte[] payload,
                                             out string summary,
                                             out string details)
        {
            summary = UbxDescription(messageClass, messageId);
            details = string.Empty;
            bool navigationClass = messageClass == 0x01 || messageClass == 0x29;
            if (navigationClass && messageId == 0x07 && payload.Length >= 92)
                DecodeNavPvt(payload, out summary, out details);
            else if (navigationClass && messageId == 0x35 && payload.Length >= 8)
                DecodeNavSat(payload, out summary, out details);
            else if (navigationClass && messageId == 0x03 && payload.Length >= 16)
                DecodeNavStatus(payload, out summary, out details);
            else if (navigationClass && messageId == 0x04 && payload.Length >= 18)
                DecodeNavDop(payload, out summary, out details);
            else if (navigationClass && messageId == 0x21 && payload.Length >= 20)
                DecodeNavTimeUtc(payload, out summary, out details);
            else if (navigationClass && messageId == 0x22 && payload.Length >= 20)
                DecodeNavClock(payload, out summary, out details);
            else if (messageClass == 0x0a && messageId == 0x04 &&
                     payload.Length >= 40)
                DecodeMonVer(payload, out summary, out details);
            else if (messageClass == 0x05 &&
                     (messageId == 0x00 || messageId == 0x01) &&
                     payload.Length >= 2)
            {
                summary = messageId == 0x01 ?
                    "Receiver acknowledged command" : "Receiver rejected command";
                details = "Command " + UbxMessageName(payload[0], payload[1]) +
                    string.Format(CultureInfo.InvariantCulture,
                        " [{0:X2}/{1:X2}]", payload[0], payload[1]);
            }
            else if (messageClass == 0x06 &&
                     (messageId == 0x8a || messageId == 0x8b || messageId == 0x8c) &&
                     payload.Length >= 4)
            {
                summary = UbxDescription(messageClass, messageId);
                details = string.Format(CultureInfo.InvariantCulture,
                    "Version {0} · layer 0x{1:X2} · position {2} · {3} data byte(s)",
                    payload[0], payload[1], ReadUInt16(payload, 2),
                    payload.Length - 4);
            }
            else if (messageClass == 0x02 && messageId == 0x15 &&
                     payload.Length >= 16)
                DecodeRawx(payload, out summary, out details);
            else if (messageClass == 0x0d && messageId == 0x01 &&
                     payload.Length >= 16)
                DecodeTimTp(payload, out summary, out details);
        }

        private static void DecodeNavPvt(byte[] payload, out string summary,
                                         out string details)
        {
            byte valid = payload[11];
            byte flags = payload[21];
            bool fixValid = (flags & 0x01) != 0;
            bool differential = (flags & 0x02) != 0;
            int carrier = (flags >> 6) & 0x03;
            string utc = "not valid";
            if ((valid & 0x03) == 0x03)
            {
                try
                {
                    DateTime value = new DateTime(ReadUInt16(payload, 4),
                        payload[6], payload[7], payload[8], payload[9],
                        Math.Min(payload[10], (byte)59), DateTimeKind.Utc);
                    utc = value.ToString("yyyy-MM-dd HH:mm:ss 'UTC'",
                        CultureInfo.InvariantCulture);
                }
                catch (ArgumentOutOfRangeException)
                {
                    utc = "invalid date fields";
                }
            }

            summary = FixName(payload[20]) + (fixValid ? " · valid" :
                " · not valid") + " · " + payload[23].ToString(
                    CultureInfo.InvariantCulture) + " SV · " + utc;
            List<string> flagsText = new List<string>();
            if (differential)
                flagsText.Add("differential");
            if (carrier == 1)
                flagsText.Add("RTK float");
            else if (carrier == 2)
                flagsText.Add("RTK fixed");
            string solution = flagsText.Count == 0 ? string.Empty :
                " · " + string.Join(" + ", flagsText.ToArray());
            details = string.Format(CultureInfo.InvariantCulture,
                "{0:F7}, {1:F7} · MSL {2:F3} m · hAcc {3:F3} m · " +
                "speed {4:F3} m/s · heading {5:F5}° · pDOP {6:F2}{7}",
                ReadInt32(payload, 28) * 1e-7,
                ReadInt32(payload, 24) * 1e-7,
                ReadInt32(payload, 36) / 1000.0,
                ReadUInt32(payload, 40) / 1000.0,
                ReadInt32(payload, 60) / 1000.0,
                ReadInt32(payload, 64) * 1e-5,
                ReadUInt16(payload, 76) * 0.01, solution);
        }

        private static void DecodeNavSat(byte[] payload, out string summary,
                                         out string details)
        {
            int reported = payload[5];
            int count = Math.Min(reported, Math.Max(0, (payload.Length - 8) / 12));
            int used = 0;
            int cnoTotal = 0;
            int cnoCount = 0;
            SortedDictionary<byte, int> constellations =
                new SortedDictionary<byte, int>();
            for (int index = 0; index < count; index++)
            {
                int offset = 8 + index * 12;
                byte gnss = payload[offset];
                byte cno = payload[offset + 2];
                uint flags = ReadUInt32(payload, offset + 8);
                if ((flags & 0x08) != 0)
                    used++;
                if (cno != 0)
                {
                    cnoTotal += cno;
                    cnoCount++;
                }
                int current;
                constellations.TryGetValue(gnss, out current);
                constellations[gnss] = current + 1;
            }
            summary = count.ToString(CultureInfo.InvariantCulture) +
                " visible · " + used.ToString(CultureInfo.InvariantCulture) +
                " used" + (cnoCount == 0 ? string.Empty :
                    string.Format(CultureInfo.InvariantCulture,
                        " · average C/N₀ {0:F1} dB-Hz",
                        cnoTotal / (double)cnoCount));
            List<string> counts = new List<string>();
            foreach (KeyValuePair<byte, int> item in constellations)
            {
                counts.Add(GnssName(item.Key) + " " +
                    item.Value.ToString(CultureInfo.InvariantCulture));
            }
            details = counts.Count == 0 ? "No satellite blocks" :
                string.Join(" · ", counts.ToArray());
        }

        private static void DecodeNavStatus(byte[] payload, out string summary,
                                            out string details)
        {
            bool fixValid = (payload[5] & 0x01) != 0;
            summary = FixName(payload[4]) + (fixValid ? " · valid" :
                " · not valid");
            details = string.Format(CultureInfo.InvariantCulture,
                "TTFF {0} ms · receiver uptime {1} ms · flags 0x{2:X2}/0x{3:X2}/0x{4:X2}",
                ReadUInt32(payload, 8), ReadUInt32(payload, 12),
                payload[5], payload[6], payload[7]);
        }

        private static void DecodeNavDop(byte[] payload, out string summary,
                                         out string details)
        {
            summary = string.Format(CultureInfo.InvariantCulture,
                "pDOP {0:F2} · hDOP {1:F2} · vDOP {2:F2}",
                ReadUInt16(payload, 6) * 0.01,
                ReadUInt16(payload, 12) * 0.01,
                ReadUInt16(payload, 10) * 0.01);
            details = string.Format(CultureInfo.InvariantCulture,
                "gDOP {0:F2} · tDOP {1:F2} · nDOP {2:F2} · eDOP {3:F2}",
                ReadUInt16(payload, 4) * 0.01,
                ReadUInt16(payload, 8) * 0.01,
                ReadUInt16(payload, 14) * 0.01,
                ReadUInt16(payload, 16) * 0.01);
        }

        private static void DecodeNavTimeUtc(byte[] payload, out string summary,
                                             out string details)
        {
            bool valid = (payload[19] & 0x04) != 0;
            string timestamp;
            try
            {
                DateTime value = new DateTime(ReadUInt16(payload, 12),
                    payload[14], payload[15], payload[16], payload[17],
                    Math.Min(payload[18], (byte)59), DateTimeKind.Utc);
                timestamp = value.ToString("yyyy-MM-dd HH:mm:ss 'UTC'",
                    CultureInfo.InvariantCulture);
            }
            catch (ArgumentOutOfRangeException)
            {
                timestamp = "invalid date fields";
                valid = false;
            }
            summary = timestamp + (valid ? " · valid" : " · not valid");
            details = string.Format(CultureInfo.InvariantCulture,
                "Accuracy {0} ns · fractional second {1} ns · iTOW {2} ms",
                ReadUInt32(payload, 4), ReadInt32(payload, 8),
                ReadUInt32(payload, 0));
        }

        private static void DecodeNavClock(byte[] payload, out string summary,
                                           out string details)
        {
            summary = string.Format(CultureInfo.InvariantCulture,
                "Clock bias {0} ns · drift {1} ns/s",
                ReadInt32(payload, 4), ReadInt32(payload, 8));
            details = string.Format(CultureInfo.InvariantCulture,
                "Time accuracy {0} ns · frequency accuracy {1} ps/s · iTOW {2} ms",
                ReadUInt32(payload, 12), ReadUInt32(payload, 16),
                ReadUInt32(payload, 0));
        }

        private static void DecodeMonVer(byte[] payload, out string summary,
                                         out string details)
        {
            string software = ReadAscii(payload, 0, 30);
            string hardware = ReadAscii(payload, 30, 10);
            List<string> extensions = new List<string>();
            for (int offset = 40; offset + 30 <= payload.Length; offset += 30)
            {
                string value = ReadAscii(payload, offset, 30);
                if (!string.IsNullOrWhiteSpace(value))
                    extensions.Add(value);
            }
            summary = "Software " + EmptyAsDash(software) +
                " · hardware " + EmptyAsDash(hardware);
            details = extensions.Count == 0 ? "No extension strings" :
                string.Join(" · ", extensions.ToArray());
        }

        private static void DecodeRawx(byte[] payload, out string summary,
                                       out string details)
        {
            int measurementCount = payload[11];
            int available = Math.Max(0, (payload.Length - 16) / 32);
            summary = measurementCount.ToString(CultureInfo.InvariantCulture) +
                " raw measurement(s) · GPS week " +
                ReadUInt16(payload, 8).ToString(CultureInfo.InvariantCulture);
            details = string.Format(CultureInfo.InvariantCulture,
                "Receiver TOW {0:F3} s · leap seconds {1} · version {2} · {3} complete block(s)",
                BitConverter.ToDouble(payload, 0), unchecked((sbyte)payload[10]),
                payload[13], Math.Min(measurementCount, available));
        }

        private static void DecodeTimTp(byte[] payload, out string summary,
                                        out string details)
        {
            summary = string.Format(CultureInfo.InvariantCulture,
                "Time pulse · GPS week {0} · TOW {1} ms",
                ReadUInt16(payload, 12), ReadUInt32(payload, 0));
            details = string.Format(CultureInfo.InvariantCulture,
                "Sub-ms 0x{0:X8} · quantization error {1} ps · flags 0x{2:X2} · ref 0x{3:X2}",
                ReadUInt32(payload, 4), ReadInt32(payload, 8),
                payload[14], payload[15]);
        }

        private static string UbxMessageName(byte messageClass, byte messageId)
        {
            return "UBX-" + UbxClassName(messageClass) + "-" +
                UbxMessageIdName(messageClass, messageId);
        }

        private static string UbxClassName(byte value)
        {
            switch (value)
            {
                case 0x01: return "NAV";
                case 0x02: return "RXM";
                case 0x04: return "INF";
                case 0x05: return "ACK";
                case 0x06: return "CFG";
                case 0x09: return "UPD";
                case 0x0a: return "MON";
                case 0x0b: return "AID";
                case 0x0d: return "TIM";
                case 0x10: return "ESF";
                case 0x13: return "MGA";
                case 0x21: return "LOG";
                case 0x27: return "SEC";
                case 0x28: return "HNR";
                case 0x29: return "NAV2";
                default: return "CLASS" + value.ToString("X2",
                    CultureInfo.InvariantCulture);
            }
        }

        private static string UbxMessageIdName(byte messageClass, byte messageId)
        {
            if (messageClass == 0x01 || messageClass == 0x29)
            {
                switch (messageId)
                {
                    case 0x01: return "POSECEF";
                    case 0x02: return "POSLLH";
                    case 0x03: return "STATUS";
                    case 0x04: return "DOP";
                    case 0x07: return "PVT";
                    case 0x09: return "ODO";
                    case 0x11: return "VELECEF";
                    case 0x12: return "VELNED";
                    case 0x13: return "HPPOSECEF";
                    case 0x14: return "HPPOSLLH";
                    case 0x20: return "TIMEGPS";
                    case 0x21: return "TIMEUTC";
                    case 0x22: return "CLOCK";
                    case 0x23: return "TIMEGLO";
                    case 0x24: return "TIMEBDS";
                    case 0x25: return "TIMEGAL";
                    case 0x26: return "TIMELS";
                    case 0x27: return "TIMEQZSS";
                    case 0x32: return "SBAS";
                    case 0x35: return "SAT";
                    case 0x36: return "COV";
                    case 0x3b: return "SVIN";
                    case 0x3c: return "RELPOSNED";
                    case 0x42: return "SLAS";
                    case 0x43: return "SIG";
                    case 0x61: return "EOE";
                }
            }
            else if (messageClass == 0x02)
            {
                switch (messageId)
                {
                    case 0x13: return "SFRBX";
                    case 0x14: return "MEASX";
                    case 0x15: return "RAWX";
                    case 0x34: return "COR";
                }
            }
            else if (messageClass == 0x05)
            {
                if (messageId == 0x00) return "NAK";
                if (messageId == 0x01) return "ACK";
            }
            else if (messageClass == 0x06)
            {
                switch (messageId)
                {
                    case 0x00: return "PRT";
                    case 0x01: return "MSG";
                    case 0x04: return "RST";
                    case 0x08: return "RATE";
                    case 0x09: return "CFG";
                    case 0x13: return "ANT";
                    case 0x24: return "NAV5";
                    case 0x31: return "TP5";
                    case 0x8a: return "VALSET";
                    case 0x8b: return "VALGET";
                    case 0x8c: return "VALDEL";
                }
            }
            else if (messageClass == 0x0a)
            {
                switch (messageId)
                {
                    case 0x04: return "VER";
                    case 0x09: return "HW";
                    case 0x0b: return "HW2";
                    case 0x21: return "RXR";
                    case 0x28: return "COMMS";
                    case 0x31: return "SPAN";
                    case 0x38: return "RF";
                    case 0x39: return "SYS";
                }
            }
            else if (messageClass == 0x0d)
            {
                switch (messageId)
                {
                    case 0x01: return "TP";
                    case 0x03: return "TM2";
                    case 0x04: return "SVIN";
                    case 0x11: return "VCAL";
                    case 0x12: return "FCHG";
                }
            }
            return "ID" + messageId.ToString("X2", CultureInfo.InvariantCulture);
        }

        private static string UbxDescription(byte messageClass, byte messageId)
        {
            string name = UbxMessageName(messageClass, messageId);
            switch (name)
            {
                case "UBX-NAV-PVT": return "Navigation position, velocity and time solution";
                case "UBX-NAV-SAT": return "Satellite information";
                case "UBX-NAV-STATUS": return "Receiver navigation status";
                case "UBX-NAV-DOP": return "Dilution of precision";
                case "UBX-NAV-TIMEUTC": return "UTC time solution";
                case "UBX-NAV-CLOCK": return "Receiver clock solution";
                case "UBX-MON-VER": return "Receiver and software version";
                case "UBX-RXM-RAWX": return "Multi-GNSS raw measurements";
                case "UBX-TIM-TP": return "Time pulse information";
                case "UBX-CFG-VALGET": return "Read configuration values";
                case "UBX-CFG-VALSET": return "Set configuration values";
                case "UBX-CFG-VALDEL": return "Delete configuration values";
                case "UBX-ACK-ACK": return "Receiver acknowledged command";
                case "UBX-ACK-NAK": return "Receiver rejected command";
                default: return name.Substring(4).Replace('-', ' ') + " message";
            }
        }

        private static string NmeaDescription(string type)
        {
            switch (type)
            {
                case "RMC": return "Recommended minimum navigation data";
                case "GGA": return "Fix data";
                case "GSA": return "DOP and active satellites";
                case "GSV": return "Satellites in view";
                case "GLL": return "Geographic position";
                case "VTG": return "Course and speed over ground";
                case "GNS": return "GNSS fix data";
                case "ZDA": return "UTC date and time";
                case "GST": return "Position error statistics";
                case "GBS": return "Satellite fault detection";
                case "HDT": return "True heading";
                case "THS": return "True heading and status";
                case "DTM": return "Datum reference";
                case "VLW": return "Distance traveled through water";
                case "ROT": return "Rate of turn";
                case "TXT": return "Receiver text message";
                default: return "NMEA " + type + " sentence";
            }
        }

        private static string NmeaFieldsText(string[] fields)
        {
            if (fields == null || fields.Length <= 1)
                return "No data fields";
            return "Fields: " + string.Join(" · ", fields, 1,
                fields.Length - 1);
        }

        private static string FormatGsvSatellites(string[] fields)
        {
            List<string> satellites = new List<string>();
            for (int offset = 4; offset + 3 < fields.Length; offset += 4)
            {
                if (string.IsNullOrWhiteSpace(fields[offset]))
                    continue;
                satellites.Add("SV " + fields[offset] + " elev " +
                    EmptyAsDash(fields[offset + 1]) + "° az " +
                    EmptyAsDash(fields[offset + 2]) + "° C/N₀ " +
                    EmptyAsDash(fields[offset + 3]) + " dB-Hz");
            }
            return satellites.Count == 0 ? string.Empty :
                " · " + string.Join("; ", satellites.ToArray());
        }

        private static string NmeaQualityName(string value)
        {
            switch (value)
            {
                case "0": return "no fix";
                case "1": return "autonomous fix";
                case "2": return "differential fix";
                case "3": return "PPS fix";
                case "4": return "RTK fixed";
                case "5": return "RTK float";
                case "6": return "estimated fix";
                case "7": return "manual input";
                case "8": return "simulation";
                default: return "quality " + EmptyAsDash(value);
            }
        }

        private static string NmeaModeDescription(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return "mode unavailable";
            List<string> modes = new List<string>();
            foreach (char mode in value.ToUpperInvariant())
            {
                string description;
                switch (mode)
                {
                    case 'A': description = "autonomous"; break;
                    case 'D': description = "differential"; break;
                    case 'E': description = "estimated"; break;
                    case 'F': description = "RTK float"; break;
                    case 'M': description = "manual"; break;
                    case 'N': description = "invalid"; break;
                    case 'P': description = "precise"; break;
                    case 'R': description = "RTK fixed"; break;
                    case 'S': description = "simulator"; break;
                    default: description = "mode " + mode; break;
                }
                if (!modes.Contains(description))
                    modes.Add(description);
            }
            return string.Join(" + ", modes.ToArray());
        }

        private static string FixName(byte fixType)
        {
            switch (fixType)
            {
                case 0: return "No fix";
                case 1: return "Dead reckoning only";
                case 2: return "2D fix";
                case 3: return "3D fix";
                case 4: return "GNSS + dead reckoning";
                case 5: return "Time-only fix";
                default: return "Fix type " + fixType.ToString(
                    CultureInfo.InvariantCulture);
            }
        }

        private static string NmeaFixName(string value)
        {
            if (value == "1") return "No fix";
            if (value == "2") return "2D fix";
            if (value == "3") return "3D fix";
            return "Fix " + EmptyAsDash(value);
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
                default: return "GNSS " + identifier.ToString(
                    CultureInfo.InvariantCulture);
            }
        }

        private static string FormatNmeaPosition(string latitude,
            string latitudeHemisphere, string longitude,
            string longitudeHemisphere)
        {
            double latitudeValue;
            double longitudeValue;
            if (!TryNmeaCoordinate(latitude, latitudeHemisphere,
                    out latitudeValue) ||
                !TryNmeaCoordinate(longitude, longitudeHemisphere,
                    out longitudeValue))
                return "Position unavailable";
            return string.Format(CultureInfo.InvariantCulture,
                "{0:F7}, {1:F7}", latitudeValue, longitudeValue);
        }

        private static bool TryNmeaCoordinate(string value, string hemisphere,
                                               out double coordinate)
        {
            coordinate = 0;
            double raw;
            if (!double.TryParse(value, NumberStyles.Float,
                    CultureInfo.InvariantCulture, out raw))
                return false;
            double degrees = Math.Floor(raw / 100.0);
            coordinate = degrees + (raw - degrees * 100.0) / 60.0;
            if (hemisphere == "S" || hemisphere == "W")
                coordinate = -coordinate;
            return true;
        }

        private static string FormatNmeaSpeed(string speedKnots,
                                              string courseDegrees)
        {
            double speed;
            string speedText = double.TryParse(speedKnots, NumberStyles.Float,
                CultureInfo.InvariantCulture, out speed) ?
                string.Format(CultureInfo.InvariantCulture,
                    "{0:F3} kn ({1:F3} m/s)", speed, speed * 0.514444) :
                "speed unavailable";
            return speedText + " · course " + EmptyAsDash(courseDegrees) + "°";
        }

        private static string FormatNmeaDateTime(string date, string time)
        {
            if (date != null && date.Length >= 6)
            {
                int day;
                int month;
                int year;
                if (int.TryParse(date.Substring(0, 2), out day) &&
                    int.TryParse(date.Substring(2, 2), out month) &&
                    int.TryParse(date.Substring(4, 2), out year))
                {
                    year += year >= 80 ? 1900 : 2000;
                    return string.Format(CultureInfo.InvariantCulture,
                        "{0:D4}-{1:D2}-{2:D2} {3}", year, month, day,
                        FormatNmeaTime(time));
                }
            }
            return FormatNmeaTime(time);
        }

        private static string FormatNmeaTime(string value)
        {
            if (string.IsNullOrWhiteSpace(value) || value.Length < 6)
                return "—";
            return value.Substring(0, 2) + ":" + value.Substring(2, 2) +
                ":" + value.Substring(4) + " UTC";
        }

        private static string JoinNonEmpty(params string[] values)
        {
            List<string> filtered = new List<string>();
            foreach (string value in values)
            {
                if (!string.IsNullOrWhiteSpace(value))
                    filtered.Add(value);
            }
            return string.Join(" · ", filtered.ToArray());
        }

        private static string EmptyAsDash(string value)
        {
            return string.IsNullOrWhiteSpace(value) ? "—" : value;
        }

        private static int HexValue(char value)
        {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            return -1;
        }

        private static uint Crc24Q(IList<byte> data, int length)
        {
            uint crc = 0;
            for (int index = 0; index < length; index++)
            {
                crc ^= (uint)data[index] << 16;
                for (int bit = 0; bit < 8; bit++)
                {
                    crc <<= 1;
                    if ((crc & 0x1000000) != 0)
                        crc ^= 0x1864cfb;
                }
            }
            return crc & 0xffffff;
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

        private static string ReadAscii(byte[] data, int offset, int length)
        {
            int available = Math.Min(length, data.Length - offset);
            int count = 0;
            while (count < available && data[offset + count] != 0)
                count++;
            return Encoding.ASCII.GetString(data, offset, count).Trim();
        }
    }
}
