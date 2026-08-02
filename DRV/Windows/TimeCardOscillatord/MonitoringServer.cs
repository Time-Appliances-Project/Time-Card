using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.IO.Pipes;
using Microsoft.Win32.SafeHandles;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace TimeCardControlCenter
{
    internal sealed class MonitoringServer
    {
        private readonly OscillatordConfiguration configuration;
        private readonly OscillatordRuntime runtime;
        private readonly ServiceLog log;
        private readonly SemaphoreSlim handlers = new SemaphoreSlim(16, 16);
        private readonly SemaphoreSlim lifecycle = new SemaphoreSlim(1, 1);
        private readonly object handlerGate = new object();
        private readonly HashSet<TcpClient> activeClients =
            new HashSet<TcpClient>();
        private readonly HashSet<Task> activeHandlers = new HashSet<Task>();
        private TcpListener listener;
        private CancellationTokenSource cancellation;
        private Task worker;

        public MonitoringServer(OscillatordConfiguration activeConfiguration,
            OscillatordRuntime activeRuntime, ServiceLog serviceLog)
        {
            configuration = activeConfiguration;
            runtime = activeRuntime;
            log = serviceLog;
        }

        public void Start(CancellationToken parentToken)
        {
            lifecycle.Wait();
            try
            {
                if (cancellation != null)
                    return;
                CancellationTokenSource source = null;
                TcpListener startedListener = null;
                Task startedWorker = null;
                try
                {
                    source = CancellationTokenSource.CreateLinkedTokenSource(
                        parentToken);
                    startedListener = new TcpListener(
                        configuration.SocketAddress,
                        configuration.SocketPort);
                    startedListener.Start(16);
                    startedWorker = Task.Run(() => AcceptLoopAsync(
                        startedListener, source.Token));
                    cancellation = source;
                    listener = startedListener;
                    worker = startedWorker;
                    log.Info("Monitoring TCP endpoint listening on " +
                        configuration.SocketAddress + ":" +
                        configuration.SocketPort + ".");
                }
                catch
                {
                    if (source != null)
                    {
                        try { source.Cancel(); }
                        catch (Exception ex)
                        {
                            log.Error("Monitoring TCP rollback cancellation failed.", ex);
                        }
                    }
                    if (startedListener != null)
                    {
                        try { startedListener.Stop(); }
                        catch (Exception ex)
                        {
                            log.Error("Monitoring TCP rollback listener cleanup failed.", ex);
                        }
                    }
                    if (startedWorker != null)
                    {
                        try { startedWorker.GetAwaiter().GetResult(); }
                        catch (OperationCanceledException) { }
                        catch (ObjectDisposedException) { }
                        catch (SocketException) { }
                        catch (Exception ex)
                        {
                            log.Error("Monitoring TCP rollback worker cleanup failed.", ex);
                        }
                    }
                    if (source != null)
                        source.Dispose();
                    cancellation = null;
                    listener = null;
                    worker = null;
                    throw;
                }
            }
            finally
            {
                lifecycle.Release();
            }
        }

        public async Task StopAsync()
        {
            await lifecycle.WaitAsync().ConfigureAwait(false);
            try
            {
                CancellationTokenSource source = cancellation;
                TcpListener activeListener = listener;
                Task activeWorker = worker;
                if (source == null && activeListener == null &&
                    activeWorker == null)
                    return;

                if (source != null)
                {
                    try { source.Cancel(); }
                    catch (Exception ex)
                    {
                        log.Error("Monitoring TCP cancellation failed.", ex);
                    }
                }
                if (activeListener != null)
                {
                    try { activeListener.Stop(); }
                    catch (Exception ex)
                    {
                        log.Error("Monitoring TCP listener cleanup failed.", ex);
                    }
                }
                try
                {
                    CloseActiveClients();
                    if (activeWorker != null)
                    {
                        try { await activeWorker.ConfigureAwait(false); }
                        catch (OperationCanceledException) { }
                        catch (ObjectDisposedException) { }
                        catch (SocketException ex)
                        {
                            log.Warning("Monitoring TCP listener stopped with: " +
                                ex.Message);
                        }
                        catch (Exception ex)
                        {
                            /* A faulted accept loop is already quiescent. */
                            log.Error("Monitoring TCP listener faulted during cleanup.",
                                ex);
                        }
                    }

                    /* The accept loop is quiescent, so this set cannot grow. */
                    CloseActiveClients();
                    await AwaitHandlersAsync().ConfigureAwait(false);
                }
                finally
                {
                    if (ReferenceEquals(cancellation, source))
                    {
                        cancellation = null;
                        listener = null;
                        worker = null;
                    }
                    if (source != null)
                        source.Dispose();
                }
            }
            finally
            {
                lifecycle.Release();
            }
        }

        public void Stop()
        {
            StopAsync().GetAwaiter().GetResult();
        }

        private async Task AcceptLoopAsync(TcpListener activeListener,
            CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                TcpClient accepted = null;
                bool handlerSlot = false;
                try
                {
                    accepted = await activeListener.AcceptTcpClientAsync()
                        .ConfigureAwait(false);
                    if (!await handlers.WaitAsync(0, token)
                        .ConfigureAwait(false))
                    {
                        accepted.Dispose();
                        accepted = null;
                        continue;
                    }
                    handlerSlot = true;
                    token.ThrowIfCancellationRequested();
                    TaskCompletionSource<bool> dispatch =
                        new TaskCompletionSource<bool>(
                            TaskCreationOptions.RunContinuationsAsynchronously);
                    Task handler = HandleAfterDispatchAsync(dispatch.Task,
                        accepted, token);
                    TrackHandler(accepted, handler);
                    dispatch.SetResult(true);
                    accepted = null;
                    handlerSlot = false;
                }
                catch (ObjectDisposedException) when (token.IsCancellationRequested)
                {
                    break;
                }
                catch (SocketException) when (token.IsCancellationRequested)
                {
                    break;
                }
                catch (OperationCanceledException) when (
                    token.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception ex)
                {
                    log.Error("Monitoring TCP accept failed.", ex);
                    await Task.Delay(1000, token).ConfigureAwait(false);
                }
                finally
                {
                    if (accepted != null)
                        accepted.Dispose();
                    if (handlerSlot)
                        handlers.Release();
                }
            }
        }

        private void TrackHandler(TcpClient accepted, Task handler)
        {
            lock (handlerGate)
            {
                activeClients.Add(accepted);
                activeHandlers.Add(handler);
            }
            handler.ContinueWith(completed =>
            {
                lock (handlerGate)
                {
                    activeClients.Remove(accepted);
                    activeHandlers.Remove(completed);
                }
                handlers.Release();
                Exception ignored = completed.Exception;
                GC.KeepAlive(ignored);
            }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        }

        private async Task HandleAfterDispatchAsync(Task dispatch,
            TcpClient accepted, CancellationToken token)
        {
            await dispatch.ConfigureAwait(false);
            await HandleAsync(accepted, token).ConfigureAwait(false);
        }

        private void CloseActiveClients()
        {
            TcpClient[] clients;
            lock (handlerGate)
            {
                clients = new TcpClient[activeClients.Count];
                activeClients.CopyTo(clients);
            }
            foreach (TcpClient active in clients)
            {
                try { active.Dispose(); }
                catch (ObjectDisposedException) { }
            }
        }

        private async Task AwaitHandlersAsync()
        {
            while (true)
            {
                Task[] pending;
                lock (handlerGate)
                {
                    if (activeHandlers.Count == 0)
                        return;
                    pending = new Task[activeHandlers.Count];
                    activeHandlers.CopyTo(pending);
                }
                CloseActiveClients();
                try { await Task.WhenAll(pending).ConfigureAwait(false); }
                catch (OperationCanceledException) { }
                catch (ObjectDisposedException) { }
                catch (IOException ex)
                {
                    log.Warning("Monitoring TCP client stopped with: " +
                        ex.Message);
                }
                catch (Exception ex)
                {
                    log.Error("Monitoring TCP client cleanup failed.", ex);
                }
            }
        }

        private async Task HandleAsync(TcpClient accepted,
            CancellationToken token)
        {
            using (accepted)
            {
                accepted.NoDelay = true;
                accepted.ReceiveTimeout = 5000;
                accepted.SendTimeout = 5000;
                using (NetworkStream stream = accepted.GetStream())
                {
                    try
                    {
                        OscillatordWireRequest request =
                            await MonitoringWire.ReadRequestAsync(stream, token)
                                .ConfigureAwait(false);
                        token.ThrowIfCancellationRequested();
                        OscillatordSnapshot response = runtime.HandleRequest(
                            (OscillatordRequest)request.Request, request.Token,
                            MonitoringOrigin.Tcp);
                        await MonitoringWire.WriteResponseAsync(stream, response,
                            token).ConfigureAwait(false);
                    }
                    catch (Exception ex)
                    {
                        await MonitoringWire.TryWriteErrorAsync(stream,
                            ex.Message, token).ConfigureAwait(false);
                    }
                }
            }
        }

    }

    internal sealed class NamedPipeMonitoringServer
    {
        private readonly string pipeName;
        private readonly OscillatordRuntime runtime;
        private readonly ServiceLog log;
        private readonly SemaphoreSlim lifecycle = new SemaphoreSlim(1, 1);
        private readonly object currentGate = new object();
        private CancellationTokenSource cancellation;
        private Task worker;
        private NamedPipeServerStream current;

        public NamedPipeMonitoringServer(string name,
            OscillatordRuntime activeRuntime, ServiceLog serviceLog)
        {
            pipeName = name;
            runtime = activeRuntime;
            log = serviceLog;
        }

        public void Start(CancellationToken parentToken)
        {
            lifecycle.Wait();
            try
            {
                if (cancellation != null)
                    return;
                CancellationTokenSource source = null;
                Task startedWorker = null;
                try
                {
                    source = CancellationTokenSource.CreateLinkedTokenSource(
                        parentToken);
                    startedWorker = Task.Run(() =>
                        AcceptLoopAsync(source.Token));
                    cancellation = source;
                    worker = startedWorker;
                    log.Info("Monitoring named pipe listening at \\.\\pipe\\" +
                        pipeName + ".");
                }
                catch
                {
                    if (source != null)
                    {
                        try { source.Cancel(); }
                        catch (Exception ex)
                        {
                            log.Error("Monitoring pipe rollback cancellation failed.", ex);
                        }
                    }
                    NamedPipeServerStream startedPipe = CurrentPipe();
                    if (startedPipe != null)
                    {
                        try { startedPipe.Dispose(); }
                        catch (Exception ex)
                        {
                            log.Error("Monitoring pipe rollback stream cleanup failed.", ex);
                        }
                    }
                    if (startedWorker != null)
                    {
                        try { startedWorker.GetAwaiter().GetResult(); }
                        catch (OperationCanceledException) { }
                        catch (ObjectDisposedException) { }
                        catch (IOException) { }
                        catch (Exception ex)
                        {
                            log.Error("Monitoring pipe rollback worker cleanup failed.", ex);
                        }
                    }
                    if (source != null)
                        source.Dispose();
                    cancellation = null;
                    worker = null;
                    throw;
                }
            }
            finally
            {
                lifecycle.Release();
            }
        }

        public async Task StopAsync()
        {
            await lifecycle.WaitAsync().ConfigureAwait(false);
            try
            {
                CancellationTokenSource source = cancellation;
                Task activeWorker = worker;
                NamedPipeServerStream active = CurrentPipe();
                if (source == null && activeWorker == null && active == null)
                    return;
                if (source != null)
                {
                    try { source.Cancel(); }
                    catch (Exception ex)
                    {
                        log.Error("Monitoring pipe cancellation failed.", ex);
                    }
                }
                if (active != null)
                {
                    try { active.Dispose(); }
                    catch (Exception ex)
                    {
                        log.Error("Monitoring pipe stream cleanup failed.", ex);
                    }
                }
                try
                {
                    if (activeWorker != null)
                    {
                        try { await activeWorker.ConfigureAwait(false); }
                        catch (OperationCanceledException) { }
                        catch (ObjectDisposedException) { }
                        catch (IOException ex)
                        {
                            log.Warning("Monitoring pipe stopped with: " +
                                ex.Message);
                        }
                        catch (Exception ex)
                        {
                            /* A faulted pipe worker is already quiescent. */
                            log.Error("Monitoring pipe worker faulted during cleanup.",
                                ex);
                        }
                    }
                }
                finally
                {
                    if (ReferenceEquals(cancellation, source))
                    {
                        cancellation = null;
                        worker = null;
                        lock (currentGate)
                            current = null;
                    }
                    if (source != null)
                        source.Dispose();
                }
            }
            finally
            {
                lifecycle.Release();
            }
        }

        public void Stop()
        {
            StopAsync().GetAwaiter().GetResult();
        }

        private async Task AcceptLoopAsync(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                NamedPipeServerStream pipe = null;
                try
                {
                    pipe = CreatePipe();
                    SetCurrentPipe(pipe);
                    await pipe.WaitForConnectionAsync(token)
                        .ConfigureAwait(false);
                    if (!IsLocalClient(pipe))
                        throw new UnauthorizedAccessException(
                            "Remote named-pipe clients are not accepted.");
                    bool administrator = IsClientAdministrator(pipe);
                    OscillatordWireRequest request =
                        await MonitoringWire.ReadRequestAsync(pipe, token)
                            .ConfigureAwait(false);
                    token.ThrowIfCancellationRequested();
                    OscillatordSnapshot response = runtime.HandleRequest(
                        (OscillatordRequest)request.Request, request.Token,
                        administrator ?
                            MonitoringOrigin.NamedPipeAdministrator :
                            MonitoringOrigin.NamedPipeReadOnly);
                    await MonitoringWire.WriteResponseAsync(pipe, response,
                        token).ConfigureAwait(false);
                }
                catch (ObjectDisposedException) when (token.IsCancellationRequested)
                {
                    break;
                }
                catch (OperationCanceledException) when (token.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception ex)
                {
                    if (pipe != null && pipe.IsConnected)
                        await MonitoringWire.TryWriteErrorAsync(pipe,
                            ex.Message, token).ConfigureAwait(false);
                    else
                        log.Warning("Monitoring pipe request failed: " +
                            ex.Message);
                }
                finally
                {
                    ClearCurrentPipe(pipe);
                    if (pipe != null)
                        pipe.Dispose();
                }
            }
        }

        private NamedPipeServerStream CurrentPipe()
        {
            lock (currentGate)
                return current;
        }

        private void SetCurrentPipe(NamedPipeServerStream pipe)
        {
            lock (currentGate)
                current = pipe;
        }

        private void ClearCurrentPipe(NamedPipeServerStream pipe)
        {
            lock (currentGate)
            {
                if (ReferenceEquals(current, pipe))
                    current = null;
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
                PipeAccessRights.ReadWrite, AccessControlType.Allow));
            return new NamedPipeServerStream(pipeName, PipeDirection.InOut,
                NamedPipeServerStream.MaxAllowedServerInstances,
                PipeTransmissionMode.Byte, PipeOptions.Asynchronous,
                4096, MonitoringWire.MaximumMessageBytes, security);
        }

        private static bool IsClientAdministrator(NamedPipeServerStream pipe)
        {
            bool administrator = false;
            pipe.RunAsClient(() =>
            {
                using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
                {
                    WindowsPrincipal principal = new WindowsPrincipal(identity);
                    administrator = principal.IsInRole(
                        WindowsBuiltInRole.Administrator);
                }
            });
            return administrator;
        }

        private static bool IsLocalClient(NamedPipeServerStream pipe)
        {
            StringBuilder name = new StringBuilder(256);
            if (!GetNamedPipeClientComputerName(pipe.SafePipeHandle, name,
                checked((uint)name.Capacity)))
            {
                int error = Marshal.GetLastWin32Error();
                // ERROR_PIPE_LOCAL is the documented local-client result.
                if (error == 229)
                    return true;
                throw new Win32Exception(error,
                    "The named-pipe client computer could not be verified.");
            }
            string computer = name.ToString().Trim().TrimStart('\\');
            return computer.Length == 0 || computer == "." ||
                computer.Equals("localhost",
                    StringComparison.OrdinalIgnoreCase) ||
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

    internal static class MonitoringWire
    {
        public const int MaximumMessageBytes = 1024 * 1024;

        public static async Task<OscillatordWireRequest> ReadRequestAsync(
            Stream stream, CancellationToken token)
        {
            using (CancellationTokenSource timeout =
                CancellationTokenSource.CreateLinkedTokenSource(token))
            using (MemoryStream bytes = new MemoryStream())
            {
                timeout.CancelAfter(TimeSpan.FromSeconds(5));
                byte[] buffer = new byte[4096];
                while (bytes.Length < MaximumMessageBytes)
                {
                    int count = await stream.ReadAsync(buffer, 0, buffer.Length,
                        timeout.Token).ConfigureAwait(false);
                    if (count == 0)
                        break;
                    bytes.Write(buffer, 0, count);
                    OscillatordWireRequest parsed;
                    if (TryDeserialize(bytes.ToArray(), out parsed))
                    {
                        if (parsed.Request < 0 || parsed.Request > 13)
                            throw new InvalidDataException(
                                "The monitoring request number is invalid.");
                        return parsed;
                    }
                }
            }
            throw new InvalidDataException(
                "The monitoring request is incomplete, malformed, or oversized.");
        }

        public static async Task WriteResponseAsync(Stream stream,
            OscillatordSnapshot response, CancellationToken token)
        {
            byte[] bytes;
            DataContractJsonSerializer serializer =
                new DataContractJsonSerializer(typeof(OscillatordSnapshot));
            using (MemoryStream output = new MemoryStream())
            {
                serializer.WriteObject(output, response);
                bytes = output.ToArray();
            }
            if (bytes.Length > MaximumMessageBytes)
                throw new InvalidDataException(
                    "The monitoring response exceeds the protocol limit.");
            await stream.WriteAsync(bytes, 0, bytes.Length, token)
                .ConfigureAwait(false);
            await stream.FlushAsync(token).ConfigureAwait(false);
        }

        public static async Task TryWriteErrorAsync(Stream stream,
            string message, CancellationToken token)
        {
            try
            {
                await WriteResponseAsync(stream, new OscillatordSnapshot
                {
                    Service = "oscillatord-windows",
                    Version = OscillatordRuntime.ServiceVersion,
                    ProtocolVersion = 1,
                    Error = message
                }, token).ConfigureAwait(false);
            }
            catch
            {
            }
        }

        private static bool TryDeserialize(byte[] bytes,
            out OscillatordWireRequest request)
        {
            request = null;
            try
            {
                DataContractJsonSerializer serializer =
                    new DataContractJsonSerializer(
                        typeof(OscillatordWireRequest));
                using (MemoryStream input = new MemoryStream(bytes, false))
                    request = serializer.ReadObject(input) as
                        OscillatordWireRequest;
                return request != null;
            }
            catch (SerializationException)
            {
                return false;
            }
            catch (System.Xml.XmlException)
            {
                return false;
            }
        }
    }
}
