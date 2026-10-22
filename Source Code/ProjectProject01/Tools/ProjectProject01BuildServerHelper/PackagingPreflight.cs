namespace ProjectProject01BuildServerHelper;

internal sealed partial class MainForm
{
    internal static string BuildCookRunArguments(string project, string archive, bool server) =>
        $"BuildCookRun -project=\"{project}\" -noP4 -platform=Win64 " +
        (server ? "-server -serverconfig=Development -noclient " : "-clientconfig=Shipping ") +
        $"-build -cook -stage -pak -prereqs -crashreporter -archive -archivedirectory=\"{Path.Combine(archive, server ? "Server" : "Client")}\" -utf8output";

    /// <summary>True when a tracked config line assigns a non-empty ClientSecret (e.g. a leftover injection).</summary>
    internal static bool TrackedEngineConfigContainsClientSecret(string configText) =>
        System.Text.RegularExpressions.Regex.IsMatch(configText, @"^[ \t]*ClientSecret[ \t]*=[ \t]*[^\s;]",
            System.Text.RegularExpressions.RegexOptions.Multiline | System.Text.RegularExpressions.RegexOptions.IgnoreCase);

    /// <summary>
    /// Build-only GeneratedEngine.ini content: keeps unrelated existing sections and replaces only the
    /// [EOSVoiceChat] ClientSecret entry. The file is git-ignored and removed/restored after packaging.
    /// </summary>
    internal static string BuildGeneratedEosClientConfig(string? existing, string clientSecret)
    {
        var kept = new List<string>();
        bool inEos = false;
        foreach (string raw in (existing ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
        {
            string line = raw.TrimEnd();
            string trimmed = line.Trim();
            if (trimmed.StartsWith('[') && trimmed.EndsWith(']'))
            {
                inEos = trimmed.Equals("[EOSVoiceChat]", StringComparison.OrdinalIgnoreCase);
                if (inEos) continue;
            }
            if (inEos) continue;
            kept.Add(line);
        }
        string prefix = string.Join("\r\n", kept).TrimEnd();
        return (prefix.Length > 0 ? prefix + "\r\n\r\n" : string.Empty) +
            "[EOSVoiceChat]\r\nClientSecret=" + clientSecret + "\r\n";
    }

    internal static string[] MissingPrerequisiteInstallers(string engine) =>
        new[] { "vc_redist.x64.exe", "vc_redist.arm64.exe", "GameInputRedist.msi" }
            .Select(name => Path.Combine(engine, "Extras", "Redist", "en-us", name))
            .Where(path => !File.Exists(path)).ToArray();

    private (string Sdk, string Audio) PrepareBackendVoiceRuntime(string backend) =>
        EosRuntimeBundle.Prepare(backend, engineRoot.Text.Trim(),
            Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_SDK_PATH"),
            Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_XAUDIO_PATH"));
}
