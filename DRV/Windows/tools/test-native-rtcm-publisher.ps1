[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$windowsRoot = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $windowsRoot `
    "TimeCardOscillatord\NativeRtcmPublisher.cs"
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Native RTCM publisher source not found: $sourcePath"
}

# Compile the production source unchanged and put the white-box golden harness
# in the same assembly so the service's internal implementation stays private.
$source = Get-Content -LiteralPath $sourcePath -Raw
$source += @'

namespace TimeCardControlCenter
{
    public static class NativeRtcmPublisherGoldenHarness
    {
        private static int assertions;

        // Independently generated, checksum-complete protocol fixtures.
        private static readonly byte[] Rtcm1005 = Hex(
            "D300043ED000030923D9");
        private static readonly byte[] Rawx = Hex(
            "B562021504000102030425CF");
        private static readonly byte[] Sfrbx = Hex(
            "B56202130300AABBCC49CF");
        private static readonly byte[] NavPvt = Hex(
            "B562010703000908072372");

        public static string Run()
        {
            TestGoldenFramesAndMixedStream();
            TestFragmentationAndCorruptionRecovery();
            TestBoundsAndDropOldestQueue();
            TestLocalNamedPipeStream();
            return assertions.ToString(
                System.Globalization.CultureInfo.InvariantCulture);
        }

        private static void TestGoldenFramesAndMixedStream()
        {
            List<NativeGnssForwardedFrameKind> kinds =
                new List<NativeGnssForwardedFrameKind>();
            List<byte[]> frames = new List<byte[]>();
            NativeRtcmFrameExtractor extractor =
                new NativeRtcmFrameExtractor(delegate(
                    NativeGnssForwardedFrameKind kind, byte[] frame)
                {
                    kinds.Add(kind);
                    frames.Add(frame);
                });

            byte[] stream = Concat(
                System.Text.Encoding.ASCII.GetBytes("$GNTXT,noise*00\r\n"),
                NavPvt, Rtcm1005, Rawx, new byte[] { 0x00, 0xff }, Sfrbx);
            extractor.Feed(stream);

            Equal(frames.Count, 3, "only RTCM/RAWX/SFRBX forwarded");
            Equal(kinds[0], NativeGnssForwardedFrameKind.Rtcm3,
                "RTCM kind");
            Equal(kinds[1], NativeGnssForwardedFrameKind.UbxRxmRawx,
                "RAWX kind");
            Equal(kinds[2], NativeGnssForwardedFrameKind.UbxRxmSfrbx,
                "SFRBX kind");
            Bytes(frames[0], Rtcm1005, "RTCM golden bytes");
            Bytes(frames[1], Rawx, "RAWX golden bytes");
            Bytes(frames[2], Sfrbx, "SFRBX golden bytes");
            Equal(extractor.BufferedByteCount, 0,
                "mixed stream completely consumed");
        }

        private static void TestFragmentationAndCorruptionRecovery()
        {
            List<byte[]> frames = new List<byte[]>();
            NativeRtcmFrameExtractor extractor =
                new NativeRtcmFrameExtractor(delegate(
                    NativeGnssForwardedFrameKind kind, byte[] frame)
                {
                    frames.Add(frame);
                });

            byte[] badRtcm = (byte[])Rtcm1005.Clone();
            badRtcm[badRtcm.Length - 1] ^= 0x40;
            byte[] badRawx = (byte[])Rawx.Clone();
            badRawx[badRawx.Length - 2] ^= 0x20;
            byte[] plausibleBadRtcmLength =
                new byte[] { 0xd3, 0x03, 0xff, 0x01, 0x02 };
            byte[] plausibleBadUbxLength =
                new byte[] { 0xb5, 0x62, 0x02, 0x15, 0xff, 0x1f, 0x99 };
            byte[] oversizedUbxLength =
                new byte[] { 0xb5, 0x62, 0x02, 0x15, 0xff, 0xff };
            byte[] invalidRtcmHeader =
                new byte[] { 0xd3, 0x80, 0x00, 0, 0, 0 };

            byte[] input = Concat(new byte[] { 0xb5, 0x01 }, badRtcm,
                badRawx, invalidRtcmHeader, plausibleBadRtcmLength,
                Rtcm1005, plausibleBadUbxLength, Rawx,
                oversizedUbxLength, Sfrbx);
            for (int index = 0; index < input.Length; ++index)
                extractor.Feed(input, index, 1);

            Equal(frames.Count, 3,
                "fragmented parser recovers after corrupt frames and lengths");
            Bytes(frames[0], Rtcm1005, "RTCM after false long RTCM");
            Bytes(frames[1], Rawx, "RAWX after false long UBX");
            Bytes(frames[2], Sfrbx, "SFRBX after oversized UBX");

            // A never-completing maximum-sized candidate remains bounded even
            // when a hostile caller supplies data in very large chunks.
            byte[] hostile = new byte[40000];
            hostile[0] = 0xb5;
            hostile[1] = 0x62;
            hostile[2] = 0x02;
            hostile[3] = 0x15;
            hostile[4] = 0x00;
            hostile[5] = 0x20;
            extractor.Feed(hostile);
            True(extractor.BufferedByteCount <=
                NativeRtcmFrameExtractor.MaximumUbxPayloadBytes + 8,
                "extractor memory is bounded");
        }

        private static void TestBoundsAndDropOldestQueue()
        {
            string name = "OcpTimeCard.Rtcm.Test." + Guid.NewGuid().ToString("N");
            using (CancellationTokenSource cancellation =
                new CancellationTokenSource())
            using (NativeRtcmPublisher publisher =
                new NativeRtcmPublisher(name, 2, 2048))
            {
                publisher.Start(cancellation.Token);
                publisher.Feed(Concat(Rtcm1005, Rawx, Sfrbx));
                Equal(publisher.AcceptedFrameCount, 3L,
                    "all valid frames accepted without a reader");
                Equal(publisher.QueuedFrameCount, 2,
                    "queue obeys frame limit");
                Equal(publisher.DroppedFrameCount, 1L,
                    "oldest frame dropped at queue limit");
                True(publisher.QueuedByteCount <= 2048,
                    "queue obeys byte limit");
                publisher.Stop();
                Equal(publisher.QueuedFrameCount, 0,
                    "stop clears stale data");

                // Feed outside the running interval is intentionally inert.
                publisher.Feed(Rtcm1005);
                Equal(publisher.AcceptedFrameCount, 3L,
                    "stopped publisher ignores input");
            }
        }

        private static void TestLocalNamedPipeStream()
        {
            string name = "OcpTimeCard.Rtcm.Test." + Guid.NewGuid().ToString("N");
            byte[] expected = Concat(Rtcm1005, Rawx, Sfrbx);
            using (CancellationTokenSource cancellation =
                new CancellationTokenSource())
            using (NativeRtcmPublisher publisher =
                new NativeRtcmPublisher(name, 8, 4096))
            using (NamedPipeClientStream client = new NamedPipeClientStream(
                ".", name, PipeDirection.In, PipeOptions.Asynchronous))
            {
                publisher.Start(cancellation.Token);
                client.Connect(3000);
                publisher.Feed(Concat(new byte[] { 0x55 }, expected));

                byte[] actual = new byte[expected.Length];
                int offset = 0;
                using (CancellationTokenSource timeout =
                    new CancellationTokenSource(TimeSpan.FromSeconds(3)))
                {
                    while (offset < actual.Length)
                    {
                        int count = client.ReadAsync(actual, offset,
                            actual.Length - offset, timeout.Token)
                            .GetAwaiter().GetResult();
                        if (count == 0)
                            throw new EndOfStreamException(
                                "The RTCM pipe closed before the test frame.");
                        offset += count;
                    }
                }
                Bytes(actual, expected,
                    "local pipe preserves concatenated binary frames");
                publisher.Stop();
            }
        }

        private static byte[] Hex(string text)
        {
            byte[] value = new byte[text.Length / 2];
            for (int index = 0; index < value.Length; ++index)
                value[index] = Convert.ToByte(text.Substring(index * 2, 2), 16);
            return value;
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

        private static void Bytes(byte[] actual, byte[] expected, string name)
        {
            assertions++;
            if (actual.Length != expected.Length)
                throw new InvalidOperationException(string.Format(
                    "Assertion failed: {0}; expected {1} bytes, got {2}.",
                    name, expected.Length, actual.Length));
            for (int index = 0; index < actual.Length; ++index)
            {
                if (actual[index] != expected[index])
                    throw new InvalidOperationException(string.Format(
                        "Assertion failed: {0}; byte {1} expected {2:X2}, " +
                        "got {3:X2}.", name, index, expected[index],
                        actual[index]));
            }
        }

        private static void True(bool value, string name)
        {
            assertions++;
            if (!value)
                throw new InvalidOperationException(
                    "Assertion failed: " + name + ".");
        }

        private static void Equal<T>(T actual, T expected, string name)
        {
            assertions++;
            if (!EqualityComparer<T>.Default.Equals(actual, expected))
                throw new InvalidOperationException(string.Format(
                    "Assertion failed: {0}; expected {1}, got {2}.",
                    name, expected, actual));
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
    throw "Native RTCM publisher did not compile:`n$($messages -join [Environment]::NewLine)"
}

$harness = $result.CompiledAssembly.GetType(
    "TimeCardControlCenter.NativeRtcmPublisherGoldenHarness", $true)
$run = $harness.GetMethod("Run", [Reflection.BindingFlags]::Public -bor
    [Reflection.BindingFlags]::Static)
try {
    $count = $run.Invoke($null, @())
}
catch [Reflection.TargetInvocationException] {
    throw $_.Exception.InnerException
}
Write-Host "Native RTCM publisher tests passed ($count assertions)."
