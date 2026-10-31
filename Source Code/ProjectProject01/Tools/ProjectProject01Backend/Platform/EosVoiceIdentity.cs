using System.Runtime.InteropServices;

internal sealed partial class EosVoiceTokenService
{
    private IntPtr _connect;
    private readonly HashSet<IntPtr> _identityOperations = [];

    // This only rejects malformed input. Authenticity, expiry and signatures are checked by EOS, NOT by decoding JSON.
    internal static bool IsIdentityProofText(string? proof)
    {
        if (proof is null || proof.Length is < 32 or > 16384) return false;
        string[] parts = proof.Split('.');
        return parts.Length == 3 && parts.All(part => part.Length > 0 &&
            part.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_'));
    }

    public async Task<bool> VerifyIdentityAsync(string productUserId, string? proof, CancellationToken cancellationToken)
    {
        if (!IsReady || !IsProductUserIdText(productUserId) || !IsIdentityProofText(proof)) return false;
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(3));
        bool acquired=false;
        try
        {
            await _queryGate.WaitAsync(timeout.Token);acquired=true;
            var completion = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            _eosCommands.Enqueue(() =>
            {
                if (completion.Task.IsCompleted || timeout.IsCancellationRequested) { completion.TrySetCanceled(); return; }
                IdentityOperation? operation = null;
                GCHandle handle = default;
                try
                {
                    if (!IsReady || _identityOperations.Count>=64) { completion.TrySetResult(false); return; }
                    operation = new(this, EosNative.EOS_ProductUserId_FromString(productUserId), proof!, completion);
                    handle = GCHandle.Alloc(operation);
                    var pointer = GCHandle.ToIntPtr(handle);
                    _identityOperations.Add(pointer);
                    var options = new EosNative.VerifyIdTokenOptions { ApiVersion = 1, IdToken = operation.Token };
                    EosNative.EOS_Connect_VerifyIdToken(_connect, ref options, pointer, EosNative.IdentityCallback);
                }
                catch
                {
                    if (handle.IsAllocated) { _identityOperations.Remove(GCHandle.ToIntPtr(handle)); handle.Free(); }
                    operation?.Dispose();
                    completion.TrySetResult(false);
                }
            });
            _eosCommandAvailable.Set();
            return await completion.Task.WaitAsync(timeout.Token);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested) { return false; }
        finally { if(acquired)_queryGate.Release(); }
    }

    private sealed class IdentityOperation : IDisposable
    {
        internal readonly EosVoiceTokenService Service;
        internal readonly IntPtr User;
        internal readonly TaskCompletionSource<bool> Completion;
        private readonly Utf8String proof;
        internal readonly IntPtr Token;
        internal IdentityOperation(EosVoiceTokenService service, IntPtr user, string jwt, TaskCompletionSource<bool> completion)
        {
            Service=service; User=user; Completion=completion; proof=new(jwt);
            Token=Marshal.AllocHGlobal(Marshal.SizeOf<EosNative.ConnectIdToken>());
            Marshal.StructureToPtr(new EosNative.ConnectIdToken { ApiVersion=1, ProductUserId=user, JsonWebToken=proof.Pointer },Token,false);
        }
        public void Dispose() { Marshal.FreeHGlobal(Token); proof.Dispose(); }
    }

    private void ReleaseIdentityOperations()
    {
        // Called on the EOS thread only, AFTER releasing the platform (no more native callbacks).
        foreach (var pointer in _identityOperations)
        {
            var handle=GCHandle.FromIntPtr(pointer);
            if(handle.Target is IdentityOperation operation) { operation.Completion.TrySetResult(false); operation.Dispose(); }
            handle.Free();
        }
        _identityOperations.Clear();
    }

    private bool MatchesIdentityEnvironment(EosNative.VerifyIdTokenCallbackInfo info)
    {
        bool Same(IntPtr value,string environment,string config) => string.Equals(Marshal.PtrToStringUTF8(value),
            ReadSecret(environment,config),StringComparison.Ordinal);
        return Same(info.ProductId,"PROJECTPROJECT01_EOS_PRODUCT_ID","EosVoice:ProductId") &&
            Same(info.SandboxId,"PROJECTPROJECT01_EOS_SANDBOX_ID","EosVoice:SandboxId") &&
            Same(info.DeploymentId,"PROJECTPROJECT01_EOS_DEPLOYMENT_ID","EosVoice:DeploymentId") &&
            (string.IsNullOrEmpty(ReadSecret("PROJECTPROJECT01_EOS_GAME_CLIENT_ID","EosVoice:GameClientId")) ||
             Same(info.ClientId,"PROJECTPROJECT01_EOS_GAME_CLIENT_ID","EosVoice:GameClientId"));
    }

    private static partial class EosNative
    {
        internal static readonly VerifyIdTokenCallback IdentityCallback = infoPointer =>
        {
            var info=Marshal.PtrToStructure<VerifyIdTokenCallbackInfo>(infoPointer);
            var handle=GCHandle.FromIntPtr(info.ClientData);
            var operation=(IdentityOperation)handle.Target!;
            try
            {
                // Device-ID identities legitimately have no external account information.
                operation.Completion.TrySetResult(info.ResultCode==0 && info.ProductUserId==operation.User &&
                    operation.Service.MatchesIdentityEnvironment(info));
            }
            catch { operation.Completion.TrySetResult(false); }
            finally { operation.Service._identityOperations.Remove(info.ClientData); operation.Dispose(); handle.Free(); }
        };
        [StructLayout(LayoutKind.Sequential, Pack=8)]
        internal struct ConnectIdToken { internal int ApiVersion; internal IntPtr ProductUserId; internal IntPtr JsonWebToken; }
        [StructLayout(LayoutKind.Sequential, Pack=8)]
        internal struct VerifyIdTokenOptions { internal int ApiVersion; internal IntPtr IdToken; }
        [StructLayout(LayoutKind.Sequential, Pack=8)]
        internal struct VerifyIdTokenCallbackInfo
        {
            internal int ResultCode; internal IntPtr ClientData; internal IntPtr ProductUserId;
            internal int IsAccountInfoPresent; internal int AccountIdType;
            internal IntPtr AccountId; internal IntPtr Platform; internal IntPtr DeviceType;
            internal IntPtr ClientId; internal IntPtr ProductId; internal IntPtr SandboxId; internal IntPtr DeploymentId;
        }
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void VerifyIdTokenCallback(IntPtr data);
        [DllImport(LibraryName, CallingConvention=CallingConvention.Cdecl)]
        internal static extern IntPtr EOS_Platform_GetConnectInterface(IntPtr platform);
        [DllImport(LibraryName, CallingConvention=CallingConvention.Cdecl)]
        internal static extern void EOS_Connect_VerifyIdToken(IntPtr connect, ref VerifyIdTokenOptions options,
            IntPtr clientData, VerifyIdTokenCallback callback);
    }
}
