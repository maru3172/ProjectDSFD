using System.Security.Cryptography;

#if PROJECT_SERVER_HELPER
namespace ProjectProject01BuildServerHelper;
#endif

// Linked into the helper as well as the backend; contains no credentials or native SDK calls.
internal static class EosRuntimeBundle
{
    internal const string SdkName = "EOSSDK-Win64-Shipping.dll";
    internal const string AudioName = "xaudio2_9redist.dll";
    internal static string DirectoryFor(string root) => Path.Combine(root, "Native", "EOS", "win-x64");

    internal static (string Sdk, string Audio) Prepare(string root, string? engineRoot = null,
        string? configuredSdk = null, string? configuredAudio = null, bool allowInstalledEngineFallback = true)
    {
        string destination = DirectoryFor(Path.GetFullPath(root));
        string sdkTarget = Path.Combine(destination, SdkName);
        string audioTarget = Path.Combine(destination, AudioName);
        // A complete portable copy wins over stale absolute paths after moving the server.
        if (File.Exists(sdkTarget) && File.Exists(audioTarget))
        {
            ValidateX64Dll(sdkTarget); ValidateX64Dll(audioTarget);
            return (sdkTarget, audioTarget);
        }
        string? fallbackEngine = allowInstalledEngineFallback
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Epic Games", "UE_5.8", "Engine")
            : null;
        string sdk = Find([sdkTarget, configuredSdk,
            EngineFile(engineRoot, "Binaries", "Win64", SdkName),
            EngineFile(fallbackEngine, "Binaries", "Win64", SdkName)], SdkName);
        string? sdkBinaries = Directory.GetParent(Path.GetDirectoryName(sdk)!)?.FullName;
        string audio = Find([audioTarget, configuredAudio,
            EngineFile(engineRoot, "Binaries", "ThirdParty", "Windows", "XAudio2_9", "x64", AudioName),
            EngineFile(sdkBinaries, "ThirdParty", "Windows", "XAudio2_9", "x64", AudioName),
            EngineFile(fallbackEngine, "Binaries", "ThirdParty", "Windows", "XAudio2_9", "x64", AudioName)], AudioName);
        ValidateX64Dll(sdk); ValidateX64Dll(audio);
        Directory.CreateDirectory(destination);
        CopyVerified(sdk, sdkTarget); CopyVerified(audio, audioTarget);
        return (sdkTarget, audioTarget);
    }

    private static string? EngineFile(string? root, params string[] parts) =>
        string.IsNullOrWhiteSpace(root) ? null : Path.Combine(new[] { root }.Concat(parts).ToArray());

    private static string Find(IEnumerable<string?> candidates, string name) =>
        candidates.FirstOrDefault(path => !string.IsNullOrWhiteSpace(path) && File.Exists(path)) ??
        throw new FileNotFoundException($"EOS runtime is missing {name}. Restore Native/EOS/win-x64 or configure the SDK/XAudio paths before removing Unreal Engine.");

    internal static void ValidateX64Dll(string path)
    {
        using var stream = File.OpenRead(path);
        using var reader = new BinaryReader(stream);
        if (stream.Length < 90 || reader.ReadUInt16() != 0x5a4d) throw new InvalidDataException("Not a Windows DLL: " + path);
        stream.Position = 0x3c;
        int pe = reader.ReadInt32();
        if (pe < 64 || pe > stream.Length - 26) throw new InvalidDataException("Invalid PE header: " + path);
        stream.Position = pe;
        if (reader.ReadUInt32() != 0x4550 || reader.ReadUInt16() != 0x8664)
            throw new InvalidDataException("EOS runtime requires a Windows x64 DLL: " + path);
        stream.Position = pe + 22;
        if ((reader.ReadUInt16() & 0x2000) == 0 || reader.ReadUInt16() != 0x20b)
            throw new InvalidDataException("EOS runtime requires a PE32+ DLL: " + path);
    }

    private static void CopyVerified(string source, string target)
    {
        if (Path.GetFullPath(source).Equals(Path.GetFullPath(target), StringComparison.OrdinalIgnoreCase)) return;
        string pending = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.Copy(source, pending, false);
            using var original = File.OpenRead(source);
            using var copied = File.OpenRead(pending);
            if (!CryptographicOperations.FixedTimeEquals(SHA256.HashData(original), SHA256.HashData(copied)))
                throw new IOException("EOS runtime copy verification failed: " + Path.GetFileName(target));
            copied.Dispose();
            File.Move(pending, target, true);
        }
        finally { if (File.Exists(pending)) File.Delete(pending); }
    }
}
