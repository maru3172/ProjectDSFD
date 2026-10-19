namespace ProjectProject01BuildServerHelper;

internal sealed partial class MainForm
{
    internal static string BuildCookRunArguments(string project, string archive, bool server) =>
        $"BuildCookRun -project=\"{project}\" -noP4 -platform=Win64 " +
        (server ? "-server -serverconfig=Development -noclient " : "-clientconfig=Shipping ") +
        $"-build -cook -stage -pak -prereqs -crashreporter -archive -archivedirectory=\"{Path.Combine(archive, server ? "Server" : "Client")}\" -utf8output";

    internal static string[] MissingPrerequisiteInstallers(string engine) =>
        new[] { "vc_redist.x64.exe", "vc_redist.arm64.exe", "GameInputRedist.msi" }
            .Select(name => Path.Combine(engine, "Extras", "Redist", "en-us", name))
            .Where(path => !File.Exists(path)).ToArray();

    private (string Sdk, string Audio) PrepareBackendVoiceRuntime(string backend) =>
        EosRuntimeBundle.Prepare(backend, engineRoot.Text.Trim(),
            Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_SDK_PATH"),
            Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_XAUDIO_PATH"));
}
