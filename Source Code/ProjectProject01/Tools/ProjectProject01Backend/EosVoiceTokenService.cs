using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

internal sealed class EosVoiceTokenService : IHostedService, IDisposable
{
    private readonly ILogger<EosVoiceTokenService> _logger;
    private readonly IConfiguration _configuration;
    private readonly SemaphoreSlim _queryGate = new(1, 1);
    private readonly ConcurrentQueue<Action> _eosCommands = new();
    private readonly AutoResetEvent _eosCommandAvailable = new(false);
    private CancellationTokenSource? _tickCancellation;
    private Thread? _eosThread;
    private TaskCompletionSource<bool>? _eosThreadStarted;
    private TaskCompletionSource<bool>? _eosThreadStopped;
    private IntPtr _platform;
    private IntPtr _rtcAdmin;
    private bool _initializedSdk;

    public EosVoiceTokenService(ILogger<EosVoiceTokenService> logger, IConfiguration configuration)
    {
        _logger = logger;
        _configuration = configuration;
    }

    public bool IsReady => _platform != IntPtr.Zero && _rtcAdmin != IntPtr.Zero;

    public static bool IsProductUserIdText(string? value) =>
        !string.IsNullOrWhiteSpace(value) && Regex.IsMatch(value, "^[0-9a-fA-F]{32}$", RegexOptions.CultureInvariant);

    private static bool IsPortalIdentifier(string? value) =>
        !string.IsNullOrWhiteSpace(value) && Regex.IsMatch(value, "^[0-9a-fA-F]{32}$", RegexOptions.CultureInvariant);

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        var productId = ReadSecret("PROJECTPROJECT01_EOS_PRODUCT_ID", "EosVoice:ProductId");
        var sandboxId = ReadSecret("PROJECTPROJECT01_EOS_SANDBOX_ID", "EosVoice:SandboxId");
        var deploymentId = ReadSecret("PROJECTPROJECT01_EOS_DEPLOYMENT_ID", "EosVoice:DeploymentId");
        var clientId = ReadSecret("PROJECTPROJECT01_EOS_VOICE_SERVER_CLIENT_ID", "EosVoice:VoiceServerClientId");
        var clientSecret = ReadSecret("PROJECTPROJECT01_EOS_VOICE_SERVER_CLIENT_SECRET", "EosVoice:VoiceServerClientSecret");
        if (new[] { productId, sandboxId, deploymentId, clientId, clientSecret }.Any(string.IsNullOrWhiteSpace))
        {
            _logger.LogWarning(
                "EOS voice token service is disabled. Set Product/Sandbox/Deployment IDs and the voice-server client credentials through environment variables or user-secrets.");
            return;
        }
        if (!IsPortalIdentifier(productId) || !IsPortalIdentifier(sandboxId) ||
            !IsPortalIdentifier(deploymentId) || clientId!.Length is < 16 or > 64 ||
            clientSecret!.Length is < 32 or > 64)
        {
            _logger.LogError(
                "EOS voice token service credentials have an invalid format. Copy the complete IDs and Client Secret from Epic Developer Portal.");
            return;
        }

        _tickCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        _eosThreadStarted = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        _eosThreadStopped = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        _eosThread = new Thread(() => RunEosThread(
            productId!, sandboxId!, deploymentId!, clientId!, clientSecret!, _tickCancellation.Token))
        {
            IsBackground = true,
            Name = "ProjectProject01 EOS Voice"
        };
        _eosThread.Start();
        await _eosThreadStarted.Task.WaitAsync(cancellationToken);
    }

    private void RunEosThread(
        string productId,
        string sandboxId,
        string deploymentId,
        string clientId,
        string clientSecret,
        CancellationToken cancellationToken)
    {
        try
        {
            var libraryPath = ResolveLibraryPath();
            EosNative.Load(libraryPath);
            using var productName = new Utf8String("ProjectProject01Backend");
            using var productVersion = new Utf8String("1.0");
            var initializeOptions = new EosNative.InitializeOptions
            {
                ApiVersion = 5,
                ProductName = productName.Pointer,
                ProductVersion = productVersion.Pointer
            };
            var initializeResult = EosNative.EOS_Initialize(ref initializeOptions);
            if (initializeResult is not (0 or 15))
            {
                throw new InvalidOperationException($"EOS_Initialize failed: {EosNative.ResultText(initializeResult)}");
            }
            _initializedSdk = initializeResult == 0;
            EosNative.ConfigureLogging((level, category, message) =>
            {
                // EOS SDK diagnostics are useful for credential/platform setup, but credentials must never enter logs.
                var safeMessage = message
                    .Replace(clientSecret!, "***", StringComparison.Ordinal)
                    .Replace(clientId!, "[client-id]", StringComparison.Ordinal);
                if (level <= 200) _logger.LogError("EOS SDK [{Category}] {Message}", category, safeMessage);
                else if (level <= 300) _logger.LogWarning("EOS SDK [{Category}] {Message}", category, safeMessage);
                else _logger.LogInformation("EOS SDK [{Category}] {Message}", category, safeMessage);
            });

            using var product = new Utf8String(productId!);
            using var sandbox = new Utf8String(sandboxId!);
            using var deployment = new Utf8String(deploymentId!);
            using var id = new Utf8String(clientId!);
            using var secret = new Utf8String(clientSecret!);
            using var xAudioPath = new Utf8String(ResolveXAudioPath(libraryPath));
            var windowsRtcOptions = new EosNative.WindowsRtcOptions
            {
                ApiVersion = 1,
                XAudio29DllPath = xAudioPath.Pointer
            };
            var windowsRtcPointer = Marshal.AllocHGlobal(Marshal.SizeOf<EosNative.WindowsRtcOptions>());
            var rtcOptions = new EosNative.PlatformRtcOptions
            {
                ApiVersion = 3,
                PlatformSpecificOptions = windowsRtcPointer,
                BackgroundMode = 1
            };
            var rtcPointer = Marshal.AllocHGlobal(Marshal.SizeOf<EosNative.PlatformRtcOptions>());
            try
            {
                Marshal.StructureToPtr(windowsRtcOptions, windowsRtcPointer, false);
                Marshal.StructureToPtr(rtcOptions, rtcPointer, false);
                var platformOptions = new EosNative.PlatformOptions
                {
                    ApiVersion = 15,
                    ProductId = product.Pointer,
                    SandboxId = sandbox.Pointer,
                    ClientCredentials = new EosNative.PlatformClientCredentials
                    {
                        ClientId = id.Pointer,
                        ClientSecret = secret.Pointer
                    },
                    IsServer = 1,
                    DeploymentId = deployment.Pointer,
                    Flags = 0x2 | 0x4,
                    TickBudgetInMilliseconds = 1,
                    RtcOptions = rtcPointer
                };
                _platform = EosNative.EOS_Platform_Create(ref platformOptions);
            }
            finally
            {
                Marshal.FreeHGlobal(rtcPointer);
                Marshal.FreeHGlobal(windowsRtcPointer);
            }
            if (_platform == IntPtr.Zero)
            {
                throw new InvalidOperationException("EOS_Platform_Create returned a null handle.");
            }
            _rtcAdmin = EosNative.EOS_Platform_GetRTCAdminInterface(_platform);
            if (_rtcAdmin == IntPtr.Zero)
            {
                throw new InvalidOperationException("EOS RTC Admin interface is unavailable.");
            }

            _logger.LogInformation("EOS trusted voice token service is ready.");
            _eosThreadStarted?.TrySetResult(true);
            while (!cancellationToken.IsCancellationRequested)
            {
                while (_eosCommands.TryDequeue(out var command))
                {
                    try { command(); }
                    catch (Exception exception) { _logger.LogError(exception, "EOS voice command failed."); }
                }
                EosNative.EOS_Platform_Tick(_platform);
                _eosCommandAvailable.WaitOne(10);
            }
        }
        catch (Exception exception)
        {
            _logger.LogError(exception, "EOS trusted voice token service failed to initialize.");
            _eosThreadStarted?.TrySetResult(false);
        }
        finally
        {
            ReleaseNativeResources();
            _eosThreadStopped?.TrySetResult(true);
        }
    }

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        _tickCancellation?.Cancel();
        _eosCommandAvailable.Set();
        if (_eosThreadStopped is not null)
        {
            try { await _eosThreadStopped.Task.WaitAsync(cancellationToken); }
            catch (OperationCanceledException) { }
        }
    }

    public async Task<EosRoomToken> CreateRoomTokenAsync(
        string roomName,
        string productUserId,
        string? remoteAddress,
        CancellationToken cancellationToken)
    {
        if (!IsReady) throw new InvalidOperationException("EOS voice token service is not ready.");
        if (!IsProductUserIdText(productUserId)) throw new ArgumentException("Invalid Product User ID.", nameof(productUserId));
        await _queryGate.WaitAsync(cancellationToken);
        try
        {
            var completion = new TaskCompletionSource<EosRoomToken>(TaskCreationOptions.RunContinuationsAsynchronously);
            _eosCommands.Enqueue(() => BeginRoomTokenQuery(roomName, productUserId, completion));
            _eosCommandAvailable.Set();
            return await completion.Task.WaitAsync(cancellationToken);
        }
        finally
        {
            _queryGate.Release();
        }
    }

    private void BeginRoomTokenQuery(
        string roomName,
        string productUserId,
        TaskCompletionSource<EosRoomToken> completion)
    {
        GCHandle operationHandle = default;
        try
        {
            if (!IsReady) throw new InvalidOperationException("EOS voice token service is not ready.");
            var targetUser = EosNative.EOS_ProductUserId_FromString(productUserId);
            if (targetUser == IntPtr.Zero) throw new InvalidOperationException("EOS rejected the Product User ID.");
            var operation = new QueryOperation(this, targetUser, completion);
            operationHandle = GCHandle.Alloc(operation);
            using var room = new Utf8String(roomName);
            var targetUsers = Marshal.AllocHGlobal(IntPtr.Size);
            Marshal.WriteIntPtr(targetUsers, targetUser);
            try
            {
                var options = new EosNative.QueryJoinRoomTokenOptions
                {
                    ApiVersion = 2,
                    LocalUserId = targetUser,
                    RoomName = room.Pointer,
                    TargetUserIds = targetUsers,
                    TargetUserIdsCount = 1,
                    TargetUserIpAddresses = IntPtr.Zero
                };
                EosNative.EOS_RTCAdmin_QueryJoinRoomToken(
                    _rtcAdmin,
                    ref options,
                    GCHandle.ToIntPtr(operationHandle),
                    EosNative.QueryCallback);
            }
            finally
            {
                Marshal.FreeHGlobal(targetUsers);
            }
        }
        catch (Exception exception)
        {
            if (operationHandle.IsAllocated) operationHandle.Free();
            completion.TrySetException(exception);
        }
    }

    private string? ReadSecret(string environmentName, string configurationName) =>
        Environment.GetEnvironmentVariable(environmentName) ?? _configuration[configurationName];

    private string ResolveLibraryPath()
    {
        var configured = ReadSecret("PROJECTPROJECT01_EOS_SDK_PATH", "EosVoice:SdkPath");
        var candidates = new[]
        {
            configured,
            Path.Combine(AppContext.BaseDirectory, "EOSSDK-Win64-Shipping.dll"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "Epic Games", "UE_5.8", "Engine", "Binaries", "Win64", "EOSSDK-Win64-Shipping.dll")
        };
        var found = candidates.FirstOrDefault(path => !string.IsNullOrWhiteSpace(path) && File.Exists(path));
        return found ?? throw new FileNotFoundException(
            "EOSSDK-Win64-Shipping.dll was not found. Set PROJECTPROJECT01_EOS_SDK_PATH.");
    }

    private string ResolveXAudioPath(string eosLibraryPath)
    {
        var configured = ReadSecret("PROJECTPROJECT01_EOS_XAUDIO_PATH", "EosVoice:XAudioPath");
        var eosBinaryDirectory = Path.GetDirectoryName(eosLibraryPath);
        var engineBinariesDirectory = eosBinaryDirectory is null
            ? null
            : Directory.GetParent(eosBinaryDirectory)?.FullName;
        var candidates = new[]
        {
            configured,
            engineBinariesDirectory is null
                ? null
                : Path.Combine(engineBinariesDirectory, "ThirdParty", "Windows", "XAudio2_9", "x64", "xaudio2_9redist.dll"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "Epic Games", "UE_5.8", "Engine", "Binaries", "ThirdParty", "Windows", "XAudio2_9", "x64", "xaudio2_9redist.dll")
        };
        var found = candidates.FirstOrDefault(path => !string.IsNullOrWhiteSpace(path) && File.Exists(path));
        return found ?? throw new FileNotFoundException(
            "xaudio2_9redist.dll was not found. Set PROJECTPROJECT01_EOS_XAUDIO_PATH.");
    }

    private void ReleaseNativeResources()
    {
        _rtcAdmin = IntPtr.Zero;
        if (_platform != IntPtr.Zero)
        {
            EosNative.EOS_Platform_Release(_platform);
            _platform = IntPtr.Zero;
        }
        if (_initializedSdk)
        {
            EosNative.EOS_Shutdown();
            _initializedSdk = false;
        }
    }

    public void Dispose()
    {
        _tickCancellation?.Cancel();
        _eosCommandAvailable.Set();
        _tickCancellation?.Dispose();
        _eosCommandAvailable.Dispose();
        _queryGate.Dispose();
    }

    private sealed record QueryOperation(
        EosVoiceTokenService Service,
        IntPtr TargetUserId,
        TaskCompletionSource<EosRoomToken> Completion);

    internal sealed record EosRoomToken(string ClientBaseUrl, string ParticipantToken);

    private sealed class Utf8String : IDisposable
    {
        public Utf8String(string value) => Pointer = Marshal.StringToCoTaskMemUTF8(value);
        public IntPtr Pointer { get; }
        public void Dispose() => Marshal.FreeCoTaskMem(Pointer);
    }

    private static class EosNative
    {
        private const string LibraryName = "EOSSDK-Win64-Shipping.dll";
        private static IntPtr _library;
        private static Action<int, string, string>? _logSink;
        private static readonly LogMessageCallback LogCallback = HandleLogMessage;
        internal static readonly QueryJoinRoomTokenCallback QueryCallback = HandleQueryCompleted;

        internal static void Load(string path)
        {
            if (_library != IntPtr.Zero) return;
            _library = NativeLibrary.Load(path);
            try
            {
                NativeLibrary.SetDllImportResolver(typeof(EosNative).Assembly, ResolveImport);
            }
            catch (InvalidOperationException)
            {
                // A resolver may already be registered by the host. The explicit load still makes the DLL available.
            }
        }

        private static IntPtr ResolveImport(string libraryName, Assembly assembly, DllImportSearchPath? searchPath) =>
            string.Equals(libraryName, LibraryName, StringComparison.OrdinalIgnoreCase) ? _library : IntPtr.Zero;

        internal static string ResultText(int result)
        {
            var pointer = EOS_EResult_ToString(result);
            return pointer == IntPtr.Zero ? $"EOS result {result}" : Marshal.PtrToStringUTF8(pointer) ?? $"EOS result {result}";
        }

        internal static void ConfigureLogging(Action<int, string, string> sink)
        {
            _logSink = sink;
            var callbackResult = EOS_Logging_SetCallback(LogCallback);
            if (callbackResult == 0)
            {
                EOS_Logging_SetLogLevel(0x7fffffff, 300);
            }
        }

        private static void HandleLogMessage(IntPtr messagePointer)
        {
            if (messagePointer == IntPtr.Zero || _logSink is null) return;
            var message = Marshal.PtrToStructure<LogMessage>(messagePointer);
            _logSink(message.Level,
                Marshal.PtrToStringUTF8(message.Category) ?? "Unknown",
                Marshal.PtrToStringUTF8(message.Message) ?? string.Empty);
        }

        private static void HandleQueryCompleted(IntPtr callbackInfoPointer)
        {
            var callbackInfo = Marshal.PtrToStructure<QueryJoinRoomTokenCompleteCallbackInfo>(callbackInfoPointer);
            var operationHandle = GCHandle.FromIntPtr(callbackInfo.ClientData);
            var operation = (QueryOperation)operationHandle.Target!;
            try
            {
                if (callbackInfo.ResultCode != 0)
                {
                    operation.Completion.TrySetException(new InvalidOperationException(
                        $"EOS_RTCAdmin_QueryJoinRoomToken failed: {ResultText(callbackInfo.ResultCode)}"));
                    return;
                }
                var copyOptions = new CopyUserTokenByUserIdOptions
                {
                    ApiVersion = 2,
                    TargetUserId = operation.TargetUserId,
                    QueryId = callbackInfo.QueryId
                };
                var copyResult = EOS_RTCAdmin_CopyUserTokenByUserId(
                    operation.Service._rtcAdmin, ref copyOptions, out var tokenPointer);
                if (copyResult != 0 || tokenPointer == IntPtr.Zero)
                {
                    operation.Completion.TrySetException(new InvalidOperationException(
                        $"EOS_RTCAdmin_CopyUserTokenByUserId failed: {ResultText(copyResult)}"));
                    return;
                }
                try
                {
                    var userToken = Marshal.PtrToStructure<UserToken>(tokenPointer);
                    var clientBaseUrl = Marshal.PtrToStringUTF8(callbackInfo.ClientBaseUrl) ?? string.Empty;
                    var participantToken = Marshal.PtrToStringUTF8(userToken.Token) ?? string.Empty;
                    if (clientBaseUrl.Length == 0 || participantToken.Length == 0)
                    {
                        throw new InvalidOperationException("EOS returned an empty RTC URL or participant token.");
                    }
                    operation.Completion.TrySetResult(new EosRoomToken(clientBaseUrl, participantToken));
                }
                finally
                {
                    EOS_RTCAdmin_UserToken_Release(tokenPointer);
                }
            }
            catch (Exception exception)
            {
                operation.Completion.TrySetException(exception);
            }
            finally
            {
                operationHandle.Free();
            }
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void QueryJoinRoomTokenCallback(IntPtr data);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void LogMessageCallback(IntPtr message);

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct LogMessage
        {
            internal IntPtr Category;
            internal IntPtr Message;
            internal int Level;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct InitializeOptions
        {
            internal int ApiVersion;
            internal IntPtr AllocateMemoryFunction;
            internal IntPtr ReallocateMemoryFunction;
            internal IntPtr ReleaseMemoryFunction;
            internal IntPtr ProductName;
            internal IntPtr ProductVersion;
            internal IntPtr Reserved;
            internal IntPtr SystemInitializeOptions;
            internal IntPtr OverrideThreadAffinity;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct PlatformClientCredentials
        {
            internal IntPtr ClientId;
            internal IntPtr ClientSecret;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct PlatformRtcOptions
        {
            internal int ApiVersion;
            internal IntPtr PlatformSpecificOptions;
            internal int BackgroundMode;
            internal IntPtr Reserved;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct WindowsRtcOptions
        {
            internal int ApiVersion;
            internal IntPtr XAudio29DllPath;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct PlatformOptions
        {
            internal int ApiVersion;
            internal IntPtr Reserved;
            internal IntPtr ProductId;
            internal IntPtr SandboxId;
            internal PlatformClientCredentials ClientCredentials;
            internal int IsServer;
            internal IntPtr EncryptionKey;
            internal IntPtr OverrideCountryCode;
            internal IntPtr OverrideLocaleCode;
            internal IntPtr DeploymentId;
            internal ulong Flags;
            internal IntPtr CacheDirectory;
            internal uint TickBudgetInMilliseconds;
            internal IntPtr RtcOptions;
            internal IntPtr IntegratedPlatformOptionsContainerHandle;
            internal IntPtr SystemSpecificOptions;
            internal IntPtr TaskNetworkTimeoutSeconds;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct QueryJoinRoomTokenOptions
        {
            internal int ApiVersion;
            internal IntPtr LocalUserId;
            internal IntPtr RoomName;
            internal IntPtr TargetUserIds;
            internal uint TargetUserIdsCount;
            internal IntPtr TargetUserIpAddresses;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct QueryJoinRoomTokenCompleteCallbackInfo
        {
            internal int ResultCode;
            internal IntPtr ClientData;
            internal IntPtr RoomName;
            internal IntPtr ClientBaseUrl;
            internal uint QueryId;
            internal uint TokenCount;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct CopyUserTokenByUserIdOptions
        {
            internal int ApiVersion;
            internal IntPtr TargetUserId;
            internal uint QueryId;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        internal struct UserToken
        {
            internal int ApiVersion;
            internal IntPtr ProductUserId;
            internal IntPtr Token;
        }

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int EOS_Initialize(ref InitializeOptions options);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int EOS_Shutdown();

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int EOS_Logging_SetCallback(LogMessageCallback callback);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int EOS_Logging_SetLogLevel(int category, int level);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr EOS_EResult_ToString(int result);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr EOS_Platform_Create(ref PlatformOptions options);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void EOS_Platform_Release(IntPtr platform);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void EOS_Platform_Tick(IntPtr platform);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr EOS_Platform_GetRTCAdminInterface(IntPtr platform);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr EOS_ProductUserId_FromString([MarshalAs(UnmanagedType.LPUTF8Str)] string value);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void EOS_RTCAdmin_QueryJoinRoomToken(
            IntPtr rtcAdmin,
            ref QueryJoinRoomTokenOptions options,
            IntPtr clientData,
            QueryJoinRoomTokenCallback callback);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int EOS_RTCAdmin_CopyUserTokenByUserId(
            IntPtr rtcAdmin,
            ref CopyUserTokenByUserIdOptions options,
            out IntPtr userToken);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void EOS_RTCAdmin_UserToken_Release(IntPtr userToken);
    }
}
