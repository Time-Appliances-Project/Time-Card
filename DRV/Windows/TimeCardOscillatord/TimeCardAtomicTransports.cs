using System;
using System.Diagnostics;
using System.Text;

namespace TimeCardControlCenter
{
    internal sealed class TimeCardSa3xTransport : ISa3xTelemetryTransport
    {
        private readonly TimeCardClient client;

        public TimeCardSa3xTransport(TimeCardClient activeClient)
        {
            client = activeClient ?? throw new ArgumentNullException(
                "activeClient");
        }

        public string QueryTelemetry(uint timeoutMilliseconds)
        {
            timeoutMilliseconds = Math.Max(10u,
                Math.Min(timeoutMilliseconds, 5000u));
            client.ConfigureUart(2u, 57600u);
            for (int pass = 0; pass < 8; ++pass)
            {
                UartReadResult stale = client.ReadUart(2u, 256u, 5u);
                if (stale.Data.Length == 0)
                    break;
            }
            UartWriteResult sent = client.WriteUart(2u,
                new[] { (byte)'^' }, timeoutMilliseconds);
            if (sent.BytesTransferred != 1u)
                throw new InvalidOperationException(
                    "The SA3x telemetry request was not transmitted.");

            Stopwatch timer = Stopwatch.StartNew();
            StringBuilder response = new StringBuilder(160);
            while (timer.ElapsedMilliseconds < timeoutMilliseconds &&
                   response.Length < 256)
            {
                uint remaining = (uint)Math.Max(1L,
                    (long)timeoutMilliseconds - timer.ElapsedMilliseconds);
                UartReadResult result = client.ReadUart(2u, 256u,
                    Math.Min(remaining, 100u));
                if (result.Data.Length == 0)
                    continue;
                response.Append(Encoding.ASCII.GetString(result.Data));
                if (response.ToString().IndexOf('\n') >= 0)
                    break;
            }
            if (response.Length == 0)
                throw new TimeoutException(
                    "The SA3x did not return telemetry.");
            return response.ToString();
        }
    }
}
