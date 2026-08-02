using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.IO.Pipes;
using Microsoft.Win32.SafeHandles;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace TimeCardControlCenter
{
    internal enum NativeGnssForwardedFrameKind
    {
        Rtcm3,
        UbxRxmRawx,
        UbxRxmSfrbx
    }

    // Extracts the binary products exported by Linux oscillatord's RTCM socket.
    // The input may contain arbitrary interleaved UBX, RTCM3, NMEA, and noise.
    internal sealed class NativeRtcmFrameExtractor
    {
        internal const int MaximumRtcmPayloadBytes = 1023;
        internal const int MaximumUbxPayloadBytes = 8192;
        private const int MaximumBufferedBytes = MaximumUbxPayloadBytes + 8;
        private const int InputChunkBytes = 4096;

        private readonly List<byte> pending = new List<byte>(InputChunkBytes);
        private readonly Action<NativeGnssForwardedFrameKind, byte[]> emit;

        public NativeRtcmFrameExtractor(
            Action<NativeGnssForwardedFrameKind, byte[]> frameHandler)
        {
            if (frameHandler == null)
                throw new ArgumentNullException("frameHandler");
            emit = frameHandler;
        }

        public int BufferedByteCount
        {
            get { return pending.Count; }
        }

        public void Reset()
        {
            pending.Clear();
        }

        public void Feed(byte[] bytes)
        {
            if (bytes == null)
                throw new ArgumentNullException("bytes");
            Feed(bytes, 0, bytes.Length);
        }

        public void Feed(byte[] bytes, int offset, int count)
        {
            if (bytes == null)
                throw new ArgumentNullException("bytes");
            if (offset < 0 || count < 0 || offset > bytes.Length - count)
                throw new ArgumentOutOfRangeException();

            int consumed = 0;
            while (consumed < count)
            {
                int block = Math.Min(InputChunkBytes, count - consumed);
                for (int index = 0; index < block; ++index)
                    pending.Add(bytes[offset + consumed + index]);
                consumed += block;
                ParseAvailable();

                // A damaged, but plausible, length field must not retain an
                // unbounded stream forever. One byte is discarded and the
                // normal resynchronizer gets another opportunity.
                while (pending.Count > MaximumBufferedBytes)
                {
                    pending.RemoveAt(0);
                    ParseAvailable();
                }
            }
        }

        private void ParseAvailable()
        {
            while (pending.Count != 0)
            {
                int preamble = FindPreamble(0);
                if (preamble < 0)
                {
                    // Retain a possible first half of the UBX sync word.
                    byte tail = pending[pending.Count - 1];
                    pending.Clear();
                    if (tail == 0xb5)
                        pending.Add(tail);
                    return;
                }
                if (preamble != 0)
                    pending.RemoveRange(0, preamble);

                ParseResult result = pending[0] == 0xd3 ?
                    TryParseRtcm() : TryParseUbx();
                if (result == ParseResult.Incomplete)
                {
                    // If corruption supplied a believable large length, a
                    // later, already-complete checksum-valid frame is a safer
                    // synchronization point than waiting for bogus payload.
                    int replacement = FindCompleteValidFrame(1);
                    if (replacement > 0)
                    {
                        pending.RemoveRange(0, replacement);
                        continue;
                    }
                    return;
                }
                if (result == ParseResult.Invalid)
                {
                    pending.RemoveAt(0);
                    continue;
                }
            }
        }

        private ParseResult TryParseRtcm()
        {
            if (pending.Count < 3)
                return ParseResult.Incomplete;
            if ((pending[1] & 0xfc) != 0)
                return ParseResult.Invalid;

            int payloadLength = ((pending[1] & 0x03) << 8) | pending[2];
            if (payloadLength > MaximumRtcmPayloadBytes)
                return ParseResult.Invalid;
            int frameLength = payloadLength + 6;
            if (pending.Count < frameLength)
                return ParseResult.Incomplete;

            uint expected = ((uint)pending[frameLength - 3] << 16) |
                ((uint)pending[frameLength - 2] << 8) |
                pending[frameLength - 1];
            uint actual = ComputeCrc24Q(pending, 0, frameLength - 3);
            if (actual != expected)
                return ParseResult.Invalid;

            EmitAndRemove(NativeGnssForwardedFrameKind.Rtcm3, frameLength);
            return ParseResult.Consumed;
        }

        private ParseResult TryParseUbx()
        {
            if (pending.Count < 2)
                return ParseResult.Incomplete;
            if (pending[1] != 0x62)
                return ParseResult.Invalid;
            if (pending.Count < 6)
                return ParseResult.Incomplete;

            int payloadLength = pending[4] | (pending[5] << 8);
            if (payloadLength > MaximumUbxPayloadBytes)
                return ParseResult.Invalid;
            int frameLength = payloadLength + 8;
            if (pending.Count < frameLength)
                return ParseResult.Incomplete;

            byte checksumA = 0;
            byte checksumB = 0;
            for (int index = 2; index < frameLength - 2; ++index)
            {
                checksumA = unchecked((byte)(checksumA + pending[index]));
                checksumB = unchecked((byte)(checksumB + checksumA));
            }
            if (checksumA != pending[frameLength - 2] ||
                checksumB != pending[frameLength - 1])
                return ParseResult.Invalid;

            NativeGnssForwardedFrameKind kind;
            bool forward = TryGetUbxKind(pending[2], pending[3], out kind);
            if (forward)
                EmitAndRemove(kind, frameLength);
            else
                pending.RemoveRange(0, frameLength);
            return ParseResult.Consumed;
        }

        private int FindPreamble(int start)
        {
            for (int index = start; index < pending.Count; ++index)
            {
                byte value = pending[index];
                if (value == 0xd3 || value == 0xb5)
                    return index;
            }
            return -1;
        }

        private int FindCompleteValidFrame(int start)
        {
            int candidate = FindPreamble(start);
            while (candidate >= 0)
            {
                if (IsCompleteValidFrameAt(candidate))
                    return candidate;
                candidate = FindPreamble(candidate + 1);
            }
            return -1;
        }

        private bool IsCompleteValidFrameAt(int offset)
        {
            if (pending[offset] == 0xd3)
            {
                if (pending.Count - offset < 3 ||
                    (pending[offset + 1] & 0xfc) != 0)
                    return false;
                int length = ((pending[offset + 1] & 3) << 8) |
                    pending[offset + 2];
                int total = length + 6;
                if (length > MaximumRtcmPayloadBytes ||
                    pending.Count - offset < total)
                    return false;
                uint expected = ((uint)pending[offset + total - 3] << 16) |
                    ((uint)pending[offset + total - 2] << 8) |
                    pending[offset + total - 1];
                return ComputeCrc24Q(pending, offset, total - 3) == expected;
            }

            if (pending.Count - offset < 6 || pending[offset + 1] != 0x62)
                return false;
            int payload = pending[offset + 4] | (pending[offset + 5] << 8);
            int frame = payload + 8;
            if (payload > MaximumUbxPayloadBytes ||
                pending.Count - offset < frame)
                return false;
            byte a = 0;
            byte b = 0;
            for (int index = offset + 2; index < offset + frame - 2; ++index)
            {
                a = unchecked((byte)(a + pending[index]));
                b = unchecked((byte)(b + a));
            }
            return a == pending[offset + frame - 2] &&
                b == pending[offset + frame - 1];
        }

        private void EmitAndRemove(NativeGnssForwardedFrameKind kind,
            int frameLength)
        {
            byte[] frame = pending.GetRange(0, frameLength).ToArray();
            pending.RemoveRange(0, frameLength);
            emit(kind, frame);
        }

        private static bool TryGetUbxKind(byte messageClass, byte messageId,
            out NativeGnssForwardedFrameKind kind)
        {
            if (messageClass == 0x02 && messageId == 0x15)
            {
                kind = NativeGnssForwardedFrameKind.UbxRxmRawx;
                return true;
            }
            if (messageClass == 0x02 && messageId == 0x13)
            {
                kind = NativeGnssForwardedFrameKind.UbxRxmSfrbx;
                return true;
            }
            kind = NativeGnssForwardedFrameKind.Rtcm3;
            return false;
        }

        private static uint ComputeCrc24Q(IList<byte> bytes, int offset,
            int count)
        {
            uint crc = 0;
            for (int index = 0; index < count; ++index)
            {
                crc ^= (uint)bytes[offset + index] << 16;
                for (int bit = 0; bit < 8; ++bit)
                {
                    crc <<= 1;
                    if ((crc & 0x1000000) != 0)
                        crc ^= 0x1864cfb;
                }
            }
            return crc & 0xffffff;
        }

        private enum ParseResult
        {
            Incomplete,
            Invalid,
            Consumed
        }
    }

    // A raw, local-only stream equivalent of /run/oscillatord/rtcm.sock.
    // Feed never performs I/O. Complete frames enter a bounded, drop-oldest
    // queue, so an absent or slow consumer cannot stall the GNSS reader.
    internal sealed class NativeRtcmPublisher : IDisposable
    {
        public const string DefaultPipeName = "OcpTimeCard.Rtcm.v1";
        internal const int DefaultMaximumQueuedFrames = 256;
        internal const int DefaultMaximumQueuedBytes = 1024 * 1024;

        private readonly object stateLock = new object();
        private readonly object parserLock = new object();
        private readonly object queueLock = new object();
        private readonly Queue<byte[]> queue = new Queue<byte[]>();
        private readonly SemaphoreSlim available = new SemaphoreSlim(0, 1);
        private readonly NativeRtcmFrameExtractor extractor;
        private readonly string pipeName;
        private readonly int maximumQueuedFrames;
        private readonly int maximumQueuedBytes;

        private CancellationTokenSource cancellation;
        private Task worker;
        private NamedPipeServerStream activePipe;
        private int queuedBytes;
        private long acceptedFrames;
        private long droppedFrames;
        private bool disposed;

        public NativeRtcmPublisher()
            : this(DefaultPipeName, DefaultMaximumQueuedFrames,
                  DefaultMaximumQueuedBytes)
        {
        }

        internal NativeRtcmPublisher(string name, int frameLimit,
            int byteLimit)
        {
            if (String.IsNullOrWhiteSpace(name))
                throw new ArgumentException("A pipe name is required.",
                    "name");
            if (frameLimit < 1)
                throw new ArgumentOutOfRangeException("frameLimit");
            if (byteLimit < NativeRtcmFrameExtractor.MaximumRtcmPayloadBytes + 6)
                throw new ArgumentOutOfRangeException("byteLimit");
            pipeName = name;
            maximumQueuedFrames = frameLimit;
            maximumQueuedBytes = byteLimit;
            extractor = new NativeRtcmFrameExtractor(QueueFrame);
        }

        public int QueuedFrameCount
        {
            get
            {
                lock (queueLock)
                    return queue.Count;
            }
        }

        public int QueuedByteCount
        {
            get
            {
                lock (queueLock)
                    return queuedBytes;
            }
        }

        public long AcceptedFrameCount
        {
            get { return Interlocked.Read(ref acceptedFrames); }
        }

        public long DroppedFrameCount
        {
            get { return Interlocked.Read(ref droppedFrames); }
        }

        public void Start(CancellationToken parentToken)
        {
            lock (stateLock)
            {
                ThrowIfDisposed();
                if (cancellation != null)
                    return;
                cancellation = CancellationTokenSource.CreateLinkedTokenSource(
                    parentToken);
                worker = Task.Run(() => AcceptLoopAsync(cancellation.Token));
            }
        }

        public void Feed(byte[] bytes)
        {
            if (bytes == null)
                throw new ArgumentNullException("bytes");
            Feed(bytes, 0, bytes.Length);
        }

        public void Feed(byte[] bytes, int offset, int count)
        {
            if (bytes == null)
                throw new ArgumentNullException("bytes");
            lock (stateLock)
            {
                ThrowIfDisposed();
                if (cancellation == null || cancellation.IsCancellationRequested)
                    return;
                // Keep the running-state check and parser update atomic with
                // Stop, which resets partial protocol state before a restart.
                lock (parserLock)
                    extractor.Feed(bytes, offset, count);
            }
        }

        public void Stop()
        {
            CancellationTokenSource stopCancellation;
            Task stopWorker;
            NamedPipeServerStream stopPipe;
            lock (stateLock)
            {
                stopCancellation = cancellation;
                stopWorker = worker;
                stopPipe = activePipe;
            }

            if (stopCancellation != null)
                stopCancellation.Cancel();
            if (stopPipe != null)
                stopPipe.Dispose();
            WakeWriter();
            if (stopWorker != null)
            {
                if (Task.CurrentId == stopWorker.Id)
                    throw new InvalidOperationException(
                        "The RTCM publisher cannot stop itself.");
                try
                {
                    if (!stopWorker.Wait(TimeSpan.FromSeconds(5)))
                        throw new TimeoutException(
                            "The RTCM publisher worker did not stop within five seconds.");
                }
                catch (AggregateException aggregate)
                {
                    aggregate.Handle(error =>
                        error is OperationCanceledException ||
                        error is ObjectDisposedException ||
                        error is IOException);
                }
            }

            // Do not clear or dispose worker-owned state until termination is
            // proven.  On timeout the canceled state remains attached so a
            // later Stop/Dispose can retry without creating a second worker.
            lock (stateLock)
            {
                if (ReferenceEquals(worker, stopWorker))
                {
                    worker = null;
                    cancellation = null;
                    activePipe = null;
                }
            }
            if (stopCancellation != null)
                stopCancellation.Dispose();
            lock (parserLock)
                extractor.Reset();
            ClearQueue();
            while (available.Wait(0))
            {
            }
        }

        public void Dispose()
        {
            lock (stateLock)
            {
                if (disposed)
                    return;
                disposed = true;
            }
            try
            {
                Stop();
                available.Dispose();
            }
            catch
            {
                // A timed-out worker still owns the cancellation source and
                // semaphore.  Leave the object retryable instead of freeing
                // either resource underneath it.
                lock (stateLock)
                    disposed = false;
                throw;
            }
        }

        private async Task AcceptLoopAsync(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                NamedPipeServerStream pipe = null;
                try
                {
                    pipe = CreatePipe();
                    lock (stateLock)
                        activePipe = pipe;
                    await pipe.WaitForConnectionAsync(token)
                        .ConfigureAwait(false);
                    if (!IsLocalClient(pipe))
                        throw new UnauthorizedAccessException(
                            "Remote RTCM named-pipe clients are not accepted.");
                    WakeWriter();
                    await WriteFramesAsync(pipe, token).ConfigureAwait(false);
                }
                catch (ObjectDisposedException)
                {
                    if (!token.IsCancellationRequested)
                        throw;
                    break;
                }
                catch (OperationCanceledException)
                {
                    if (!token.IsCancellationRequested)
                        throw;
                    break;
                }
                catch (IOException)
                {
                    // A client disconnect is expected. Its in-flight frame is
                    // deliberately not replayed to the replacement client.
                }
                catch (UnauthorizedAccessException)
                {
                    // A remote or unauthorized connection is discarded. The
                    // next local client gets a fresh pipe instance.
                }
                catch (Win32Exception)
                {
                    // Client identity disappeared during verification.
                }
                finally
                {
                    lock (stateLock)
                    {
                        if (ReferenceEquals(activePipe, pipe))
                            activePipe = null;
                    }
                    if (pipe != null)
                        pipe.Dispose();
                }
            }
        }

        private async Task WriteFramesAsync(NamedPipeServerStream pipe,
            CancellationToken token)
        {
            while (!token.IsCancellationRequested && pipe.IsConnected)
            {
                await available.WaitAsync(token).ConfigureAwait(false);
                byte[] frame;
                while (TryDequeue(out frame))
                {
                    await pipe.WriteAsync(frame, 0, frame.Length, token)
                        .ConfigureAwait(false);
                    await pipe.FlushAsync(token).ConfigureAwait(false);
                }
            }
        }

        private NamedPipeServerStream CreatePipe()
        {
            PipeSecurity security = new PipeSecurity();
            security.AddAccessRule(new PipeAccessRule(
                new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null),
                PipeAccessRights.FullControl, AccessControlType.Allow));
            security.AddAccessRule(new PipeAccessRule(
                new SecurityIdentifier(
                    WellKnownSidType.BuiltinAdministratorsSid, null),
                PipeAccessRights.FullControl, AccessControlType.Allow));
            security.AddAccessRule(new PipeAccessRule(
                new SecurityIdentifier(
                    WellKnownSidType.AuthenticatedUserSid, null),
                PipeAccessRights.Read, AccessControlType.Allow));
            return new NamedPipeServerStream(pipeName, PipeDirection.Out, 1,
                PipeTransmissionMode.Byte, PipeOptions.Asynchronous,
                4096, 4096, security);
        }

        private void QueueFrame(NativeGnssForwardedFrameKind kind,
            byte[] frame)
        {
            GC.KeepAlive(kind);
            lock (queueLock)
            {
                if (frame.Length > maximumQueuedBytes)
                {
                    Interlocked.Increment(ref droppedFrames);
                    return;
                }
                while (queue.Count >= maximumQueuedFrames ||
                    queuedBytes > maximumQueuedBytes - frame.Length)
                {
                    byte[] removed = queue.Dequeue();
                    queuedBytes -= removed.Length;
                    Interlocked.Increment(ref droppedFrames);
                }
                queue.Enqueue(frame);
                queuedBytes += frame.Length;
                Interlocked.Increment(ref acceptedFrames);
            }
            WakeWriter();
        }

        private bool TryDequeue(out byte[] frame)
        {
            lock (queueLock)
            {
                if (queue.Count == 0)
                {
                    frame = null;
                    return false;
                }
                frame = queue.Dequeue();
                queuedBytes -= frame.Length;
                return true;
            }
        }

        private void ClearQueue()
        {
            lock (queueLock)
            {
                queue.Clear();
                queuedBytes = 0;
            }
        }

        private void WakeWriter()
        {
            try
            {
                if (available.CurrentCount == 0)
                    available.Release();
            }
            catch (SemaphoreFullException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException("NativeRtcmPublisher");
        }

        private static bool IsLocalClient(NamedPipeServerStream pipe)
        {
            StringBuilder name = new StringBuilder(256);
            if (!GetNamedPipeClientComputerName(pipe.SafePipeHandle, name,
                checked((uint)name.Capacity)))
            {
                int error = Marshal.GetLastWin32Error();
                if (error == 229) // ERROR_PIPE_LOCAL
                    return true;
                throw new Win32Exception(error,
                    "The RTCM pipe client computer could not be verified.");
            }
            string computer = name.ToString().Trim().TrimStart('\\');
            return computer.Length == 0 || computer == "." ||
                computer.Equals("localhost", StringComparison.OrdinalIgnoreCase) ||
                computer.Equals(Environment.MachineName,
                    StringComparison.OrdinalIgnoreCase) ||
                computer.Equals(Dns.GetHostName(),
                    StringComparison.OrdinalIgnoreCase);
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode,
            SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetNamedPipeClientComputerName(
            SafePipeHandle Pipe, StringBuilder ClientComputerName,
            uint ClientComputerNameLength);
    }
}
