using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace ProjectProject01BuildServerHelper;

/// <summary>
/// The game this helper builds and runs, worked out from the project itself so the helper folder can be copied into
/// another Unreal project and used as it is: names come from the .uproject (client <c>{Name}.exe</c>, dedicated server
/// <c>{Name}Server.exe</c>), the server map from <c>Config/DefaultEngine.ini</c> (ServerDefaultMap), the engine from
/// EngineAssociation, and companion tools from <c>Tools/*Backend</c>, <c>Tools/*ClientLauncher</c>,
/// <c>Tools/*DiagnosticsDashboard</c>. <c>helper.json</c> next to the helper may override ports or the server map.
/// </summary>
internal sealed partial class HelperProject
{
    public string ProjectFile { get; }
    public string Root { get; }
    public string Name { get; }
    public string EngineAssociation { get; }
    public int BackendPort { get; } = 5080;
    public int MySqlPort { get; } = 3307;
    public int GamePort { get; } = 7777;
    public string ServerMap { get; }

    public string ClientExecutable => Name + ".exe";
    public string ServerProcess => Name + "Server";
    public string ServerExecutable => ServerProcess + ".exe";
    public string DefaultServerArguments => (ServerMap.Length > 0 ? ServerMap + " " : "") + $"-log -port={GamePort}";
    public string? BackendProject => ToolProject("Backend");
    public string BackendFolder => BackendProject is { } p ? Path.GetDirectoryName(p)! : Path.Combine(Root, "Tools", Name + "Backend");
    public string LocalMySqlConfig => Path.Combine(BackendFolder, "LocalMySql", Path.GetFileName(BackendFolder) + ".local.json");
    public string? EosScript => Directory.Exists(BackendFolder) ? Directory.GetFiles(BackendFolder, "Configure*EosVoice.ps1").FirstOrDefault() : null;

    private sealed record Overrides(int? BackendPort, int? MySqlPort, int? GamePort, string? ServerMap);

    private static HelperProject? current;
    /// <summary>Set whenever the helper's .uproject field changes.</summary>
    public static HelperProject Current { get => current ?? For(""); set => current = value; }

    private HelperProject(string projectFile)
    {
        ProjectFile = projectFile;
        Root = projectFile.Length > 0 ? Path.GetDirectoryName(Path.GetFullPath(projectFile))! : AppContext.BaseDirectory;
        Name = projectFile.Length > 0 ? Path.GetFileNameWithoutExtension(projectFile) : "Game";
        EngineAssociation = ReadAssociation(projectFile);
        ServerMap = ReadServerMap(Root);
        var o = ReadOverrides();
        if (o != null)
        {
            BackendPort = o.BackendPort ?? BackendPort;MySqlPort = o.MySqlPort ?? MySqlPort;GamePort = o.GamePort ?? GamePort;ServerMap = o.ServerMap ?? ServerMap;
        }
    }

    public static HelperProject For(string projectFile) => new(File.Exists(projectFile) ? projectFile : "");

    /// <summary>A companion tool's project: <c>Tools/*{suffix}/*.csproj</c>.</summary>
    public string? ToolProject(string suffix)
    {
        var tools = Path.Combine(Root, "Tools");
        if (!Directory.Exists(tools)) return null;
        foreach (var dir in Directory.GetDirectories(tools).Where(d => Path.GetFileName(d).EndsWith(suffix, StringComparison.OrdinalIgnoreCase)).Order(StringComparer.OrdinalIgnoreCase))
            if (Directory.GetFiles(dir, "*.csproj").FirstOrDefault() is { } csproj) return csproj;
        return null;
    }

    /// <summary>The executable a companion tool builds (its AssemblyName, else the project name).</summary>
    public string ToolExecutable(string suffix)
    {
        if (ToolProject(suffix) is not { } csproj) return Name + suffix + ".exe";
        var match = AssemblyName().Match(File.ReadAllText(csproj));
        return (match.Success ? match.Groups[1].Value.Trim() : Path.GetFileNameWithoutExtension(csproj)) + ".exe";
    }

    [GeneratedRegex(@"<AssemblyName>([^<]+)</AssemblyName>")] private static partial Regex AssemblyName();
    [GeneratedRegex(@"^\s*ServerDefaultMap\s*=\s*(\S+)", RegexOptions.Multiline)] private static partial Regex ServerDefaultMap();

    private static string ReadAssociation(string projectFile)
    {
        try
        {
            if (projectFile.Length == 0) return "";
            using var json = JsonDocument.Parse(File.ReadAllText(projectFile));
            return json.RootElement.TryGetProperty("EngineAssociation", out var e) ? e.GetString() ?? "" : "";
        }
        catch (Exception ex) when (ex is IOException or JsonException) { return ""; }
    }

    // "/Game/Maps/Arena.Arena" -> "Arena"
    private static string ReadServerMap(string root)
    {
        var ini = Path.Combine(root, "Config", "DefaultEngine.ini");
        try
        {
            if (!File.Exists(ini)) return "";
            var match = ServerDefaultMap().Match(File.ReadAllText(ini));
            if (!match.Success) return "";
            var path = match.Groups[1].Value.Trim().Trim('"');
            var last = path[(path.LastIndexOf('/') + 1)..];
            var dot = last.IndexOf('.');
            return dot > 0 ? last[..dot] : last;
        }
        catch (IOException) { return ""; }
    }

    private static Overrides? ReadOverrides()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "helper.json");
        try { return File.Exists(path) ? JsonSerializer.Deserialize<Overrides>(File.ReadAllText(path), new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) : null; }
        catch (Exception ex) when (ex is IOException or JsonException) { return null; }
    }

    /// <summary>Engine folders to try, best first: a launcher install for EngineAssociation, its registry entry, a registered source build.</summary>
    public IEnumerable<string> EngineCandidates()
    {
        if (EngineAssociation.Length == 0) yield break;
        yield return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Epic Games", "UE_" + EngineAssociation, "Engine");
        string? installed = null, build = null;
        try
        {
            installed = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\EpicGames\Unreal Engine\" + EngineAssociation)?.GetValue("InstalledDirectory") as string;
            build = Registry.CurrentUser.OpenSubKey(@"Software\Epic Games\Unreal Engine\Builds")?.GetValue(EngineAssociation) as string;
        }
        catch (Exception) { }
        if (installed is { Length: > 0 }) yield return Path.Combine(installed, "Engine");
        if (build is { Length: > 0 }) yield return Path.Combine(build, "Engine");
    }

    /// <summary>The first *.uproject in a folder or any folder above it.</summary>
    public static string? FindUp(string folder)
    {
        for (var dir = new DirectoryInfo(folder); dir != null; dir = dir.Parent)
            if (dir.Exists && dir.GetFiles("*.uproject").OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase).FirstOrDefault() is { } file) return file.FullName;
        return null;
    }
}
