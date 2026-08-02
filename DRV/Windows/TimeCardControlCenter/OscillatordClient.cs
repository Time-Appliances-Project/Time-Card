using System;
using System.IO;
using System.IO.Pipes;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text;
using System.Threading.Tasks;
using System.Xml;

namespace TimeCardControlCenter
{
    public enum OscillatordRequest
    {
        Status = 0,
        Calibration = 1,
        GnssStart = 2,
        GnssStop = 3,
        GnssSoftReset = 4,
        GnssHardReset = 5,
        GnssColdReset = 6,
        ReadEeprom = 7,
        SaveEeprom = 8,
        FakeHoldoverStart = 9,
        FakeHoldoverStop = 10,
        MroCoarseIncrement = 11,
        MroCoarseDecrement = 12,
        ResetUbloxSerial = 13
    }

    [DataContract]
    internal sealed class OscillatordWireRequest
    {
        [DataMember(Name = "request", Order = 1)]
        public int Request { get; set; }

        [DataMember(Name = "token", Order = 2, EmitDefaultValue = false)]
        public string Token { get; set; }
    }

    [DataContract]
    public sealed class OscillatordSnapshot
    {
        [DataMember(Name = "service")]
        public string Service { get; set; }

        [DataMember(Name = "version")]
        public string Version { get; set; }

        [DataMember(Name = "protocol_version")]
        public int ProtocolVersion { get; set; }

        [DataMember(Name = "control_enabled")]
        public bool ControlEnabled { get; set; }

        [DataMember(Name = "error")]
        public string Error { get; set; }

        [DataMember(Name = "Action requested")]
        public string ActionRequested { get; set; }

        [DataMember(Name = "service_state", EmitDefaultValue = false)]
        public string ServiceState { get; set; }

        [DataMember(Name = "board", EmitDefaultValue = false)]
        public string Board { get; set; }

        [DataMember(Name = "card_serial", EmitDefaultValue = false)]
        public string CardSerial { get; set; }

        [DataMember(Name = "updated_utc", EmitDefaultValue = false)]
        public string UpdatedUtc { get; set; }

        [DataMember(Name = "eeprom", EmitDefaultValue = false)]
        public OscillatordEeprom Eeprom { get; set; }

        [DataMember(Name = "clock")]
        public OscillatordClock Clock { get; set; }

        [DataMember(Name = "disciplining")]
        public OscillatordDisciplining Disciplining { get; set; }

        [DataMember(Name = "oscillator")]
        public OscillatordOscillator Oscillator { get; set; }

        [DataMember(Name = "gnss")]
        public OscillatordGnss Gnss { get; set; }
    }

    [DataContract]
    public sealed class OscillatordEeprom
    {
        [DataMember(Name = "present")]
        public bool Present { get; set; }

        [DataMember(Name = "valid")]
        public bool Valid { get; set; }

        [DataMember(Name = "length")]
        public int Length { get; set; }

        [DataMember(Name = "sha256", EmitDefaultValue = false)]
        public string Sha256 { get; set; }

        [DataMember(Name = "data_base64", EmitDefaultValue = false)]
        public string DataBase64 { get; set; }
    }

    [DataContract]
    public sealed class OscillatordClock
    {
        [DataMember(Name = "class")]
        public string Class { get; set; }

        [DataMember(Name = "offset")]
        public long OffsetNanoseconds { get; set; }
    }

    [DataContract]
    public sealed class OscillatordDisciplining
    {
        [DataMember(Name = "status")]
        public string Status { get; set; }

        [DataMember(Name = "current_phase_convergence_count")]
        public int CurrentConvergenceCount { get; set; }

        [DataMember(Name = "valid_phase_convergence_threshold")]
        public int ConvergenceThreshold { get; set; }

        [DataMember(Name = "convergence_progress")]
        public double ConvergenceProgress { get; set; }

        [DataMember(Name = "ready_for_holdover")]
        public bool ReadyForHoldover { get; set; }
    }

    [DataContract]
    public sealed class OscillatordOscillator
    {
        [DataMember(Name = "model")]
        public string Model { get; set; }

        [DataMember(Name = "fine_ctrl")]
        public long FineControl { get; set; }

        [DataMember(Name = "coarse_ctrl")]
        public long CoarseControl { get; set; }

        [DataMember(Name = "lock")]
        public bool Locked { get; set; }

        [DataMember(Name = "temperature")]
        public double TemperatureCelsius { get; set; }
    }

    [DataContract]
    public sealed class OscillatordGnss
    {
        [DataMember(Name = "fix")]
        public int Fix { get; set; }

        [DataMember(Name = "fixOk")]
        public bool FixOk { get; set; }

        [DataMember(Name = "antenna_power")]
        public int AntennaPower { get; set; }

        [DataMember(Name = "antenna_status")]
        public int AntennaStatus { get; set; }

        [DataMember(Name = "lsChange")]
        public int LeapSecondChange { get; set; }

        [DataMember(Name = "leap_seconds")]
        public int LeapSeconds { get; set; }

        [DataMember(Name = "satellites_count")]
        public int Satellites { get; set; }

        [DataMember(Name = "survey_in_position_error")]
        public double SurveyPositionErrorMeters { get; set; }

        [DataMember(Name = "time_accuracy")]
        public long TimeAccuracyNanoseconds { get; set; }
    }

    public sealed class OscillatordClient
    {
        private const int MaximumResponseBytes = 1024 * 1024;
        private const string LocalPipeName = "OcpTimeCard.Oscillatord.v1";
        private readonly TimeSpan timeout;

        public OscillatordClient(TimeSpan timeout)
        {
            if (timeout <= TimeSpan.Zero)
                throw new ArgumentOutOfRangeException("timeout");
            this.timeout = timeout;
        }

        public async Task<OscillatordSnapshot> RequestAsync(string host, int port,
            OscillatordRequest request, string token)
        {
            if (string.IsNullOrWhiteSpace(host))
                throw new ArgumentException("Enter the service host or IP address.", "host");
            if (port < 1 || port > 65535)
                throw new ArgumentOutOfRangeException("port");

            using (TcpClient client = new TcpClient())
            {
                client.NoDelay = true;
                Task connect = client.ConnectAsync(host.Trim(), port);
                await AwaitWithTimeout(connect, "Connecting to oscillatord timed out.");

                using (NetworkStream stream = client.GetStream())
                    return await ExchangeAsync(stream, request, token);
            }
            throw new InvalidDataException("oscillatord returned an incomplete or oversized JSON response.");
        }

        public async Task<OscillatordSnapshot> RequestPreferredAsync(
            string host, int port, OscillatordRequest request, string token)
        {
            string value = (host ?? string.Empty).Trim();
            if (string.Equals(value, "127.0.0.1", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "localhost", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "::1", StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    return await RequestLocalAsync(request, token);
                }
                catch (IOException)
                {
                }
                catch (TimeoutException)
                {
                }
            }
            return await RequestAsync(host, port, request, token);
        }

        public async Task<OscillatordSnapshot> RequestLocalAsync(
            OscillatordRequest request, string token)
        {
            using (NamedPipeClientStream pipe = new NamedPipeClientStream(
                ".", LocalPipeName, PipeDirection.InOut,
                PipeOptions.Asynchronous))
            {
                Task connect = pipe.ConnectAsync(checked((int)Math.Min(
                    timeout.TotalMilliseconds, int.MaxValue)));
                await AwaitWithTimeout(connect,
                    "Connecting to the local Windows oscillatord service timed out.");
                return await ExchangeAsync(pipe, request, token);
            }
        }

        private async Task<OscillatordSnapshot> ExchangeAsync(Stream stream,
            OscillatordRequest request, string token)
        {
            byte[] requestBytes = SerializeRequest(request, token);
            Task write = stream.WriteAsync(requestBytes, 0,
                requestBytes.Length);
            await AwaitWithTimeout(write,
                "Sending the oscillatord request timed out.");
            await AwaitWithTimeout(stream.FlushAsync(),
                "Flushing the oscillatord request timed out.");

            using (MemoryStream response = new MemoryStream())
            {
                byte[] buffer = new byte[4096];
                while (response.Length < MaximumResponseBytes)
                {
                    Task<int> read = stream.ReadAsync(buffer, 0, buffer.Length);
                    await AwaitWithTimeout(read,
                        "Waiting for oscillatord timed out.");
                    int count = read.Result;
                    if (count == 0)
                        break;
                    response.Write(buffer, 0, count);
                    OscillatordSnapshot snapshot;
                    if (TryParseResponse(response.ToArray(), out snapshot))
                        return snapshot;
                }
            }
            throw new InvalidDataException(
                "oscillatord returned an incomplete or oversized JSON response.");
        }

        public static OscillatordSnapshot ParseResponse(string json)
        {
            if (string.IsNullOrWhiteSpace(json))
                throw new InvalidDataException("The oscillatord response is empty.");
            OscillatordSnapshot snapshot;
            if (!TryParseResponse(Encoding.UTF8.GetBytes(json), out snapshot))
                throw new InvalidDataException("The oscillatord response is not valid JSON.");
            return snapshot;
        }

        private static byte[] SerializeRequest(OscillatordRequest request, string token)
        {
            OscillatordWireRequest value = new OscillatordWireRequest
            {
                Request = (int)request,
                Token = string.IsNullOrEmpty(token) ? null : token
            };
            DataContractJsonSerializer serializer =
                new DataContractJsonSerializer(typeof(OscillatordWireRequest));
            using (MemoryStream stream = new MemoryStream())
            {
                serializer.WriteObject(stream, value);
                return stream.ToArray();
            }
        }

        private static bool TryParseResponse(byte[] bytes,
            out OscillatordSnapshot snapshot)
        {
            snapshot = null;
            try
            {
                DataContractJsonSerializer serializer =
                    new DataContractJsonSerializer(typeof(OscillatordSnapshot));
                using (MemoryStream stream = new MemoryStream(bytes, false))
                    snapshot = serializer.ReadObject(stream) as OscillatordSnapshot;
                return snapshot != null;
            }
            catch (SerializationException)
            {
                return false;
            }
            catch (XmlException)
            {
                return false;
            }
        }

        private async Task AwaitWithTimeout(Task operation, string message)
        {
            Task completed = await Task.WhenAny(operation, Task.Delay(timeout));
            if (completed != operation)
                throw new TimeoutException(message);
            await operation;
        }
    }
}
