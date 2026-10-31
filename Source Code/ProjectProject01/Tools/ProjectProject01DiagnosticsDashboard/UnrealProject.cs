using System.Text.Json;
using Microsoft.Win32;

namespace ProjectProject01DiagnosticsDashboard;

/// <summary>
/// What the dashboard needs to know about the game, read from the Unreal project itself so the tool works in any
/// project it is copied into: the .uproject (found by walking up from a folder), its engine install (EngineAssociation
/// → Epic launcher folder or registry), the editor target (Source/*Editor.Target.cs) and the primary module name.
/// <c>dashboard.json</c> next to the dashboard may override any value.
/// </summary>
internal sealed class UnrealProject
{
    public string ProjectFile { get; init; } = "";
    public string Name { get; init; } = "Game";
    public string PrimaryModule { get; init; } = "Game";
    public string EngineAssociation { get; init; } = "";
    public string EngineRoot { get; init; } = "";
    public string EditorTarget { get; init; } = "GameEditor";
    /// <summary>Automation filter of the general test run.</summary>
    public string GeneralTestGroup { get; init; } = "Group:GameGeneral";
    /// <summary>Test path prefix of the bot match simulation.</summary>
    public string BotSimulationPrefix { get; init; } = "Game.Simulation.BotMatch";
    public string AdminKeyHeader { get; init; } = "X-Game-Admin-Key";
    public string BackendAddress { get; init; } = "http://127.0.0.1:5080";

    public string ProjectRoot => Path.GetDirectoryName(ProjectFile) ?? "";
    public string EngineBinary(string name) => Path.Combine(EngineRoot, "Engine", "Binaries", "Win64", name);
    public string BuildTool => Path.Combine(EngineRoot, "Engine", "Build", "BatchFiles", "Build.bat");
    public string EngineLabel => EngineAssociation.Length > 0 ? $"언리얼 {EngineAssociation}" : "언리얼";

    private sealed record Overrides(string? EngineRoot, string? EditorTarget, string? GeneralTestGroup, string? BotSimulationPrefix,
        string? AdminKeyHeader, string? BackendAddress);

    private static UnrealProject? current;
    /// <summary>The project this dashboard sits in (searched upward from the dashboard and the working folder).</summary>
    public static UnrealProject Current => current ??= FindFrom(AppContext.BaseDirectory) ?? FindFrom(Environment.CurrentDirectory) ?? Generic();

    public static string? FindProjectFile(string folder)
    {
        for (var dir = string.IsNullOrWhiteSpace(folder) ? null : new DirectoryInfo(folder); dir != null; dir = dir.Parent)
        {
            if (!dir.Exists) continue;
            var found = dir.GetFiles("*.uproject").OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase).FirstOrDefault();
            if (found != null) return found.FullName;
        }
        return null;
    }

    public static UnrealProject? FindFrom(string folder) => FindProjectFile(folder) is { } file ? Load(file) : null;

    private static UnrealProject Generic() => Apply(new UnrealProject(), ReadOverrides());

    public static UnrealProject Load(string projectFile)
    {
        string association = "", module = Path.GetFileNameWithoutExtension(projectFile);
        try
        {
            using var json = JsonDocument.Parse(File.ReadAllText(projectFile));
            if (json.RootElement.TryGetProperty("EngineAssociation", out var engine)) association = engine.GetString() ?? "";
            if (json.RootElement.TryGetProperty("Modules", out var modules) && modules.ValueKind == JsonValueKind.Array && modules.GetArrayLength() > 0 &&
                modules[0].TryGetProperty("Name", out var first)) module = first.GetString() ?? module;
        }
        catch (Exception ex) when (ex is IOException or JsonException) { }
        var name = Path.GetFileNameWithoutExtension(projectFile);
        var root = Path.GetDirectoryName(projectFile)!;
        var editorTarget = Directory.Exists(Path.Combine(root, "Source"))
            ? Directory.GetFiles(Path.Combine(root, "Source"), "*Editor.Target.cs").Select(f => Path.GetFileName(f)[..^".Target.cs".Length])
                .OrderBy(t => t.Equals(name + "Editor", StringComparison.OrdinalIgnoreCase) ? 0 : 1).FirstOrDefault() ?? name + "Editor"
            : name + "Editor";
        return Apply(new UnrealProject
        {
            ProjectFile = projectFile, Name = name, PrimaryModule = module, EngineAssociation = association, EngineRoot = FindEngine(association),
            EditorTarget = editorTarget, GeneralTestGroup = $"Group:{name}General", BotSimulationPrefix = $"{module}.Simulation.BotMatch",
            AdminKeyHeader = $"X-{module}-Admin-Key",
        }, ReadOverrides());
    }

    // Launcher installs: "<Program Files>\Epic Games\UE_<version>", else the launcher's registry entry, else a
    // source build registered under HKCU\Software\Epic Games\Unreal Engine\Builds.
    private static string FindEngine(string association)
    {
        if (association.Length == 0) return "";
        var launcher = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Epic Games", "UE_" + association);
        if (Directory.Exists(launcher)) return launcher;
        try
        {
            if (Registry.LocalMachine.OpenSubKey(@"SOFTWARE\EpicGames\Unreal Engine\" + association)?.GetValue("InstalledDirectory") is string installed && Directory.Exists(installed)) return installed;
            if (Registry.CurrentUser.OpenSubKey(@"Software\Epic Games\Unreal Engine\Builds")?.GetValue(association) is string build && Directory.Exists(build)) return build;
        }
        catch (Exception) { }
        return launcher;
    }

    private static Overrides? ReadOverrides()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "dashboard.json");
        try { return File.Exists(path) ? JsonSerializer.Deserialize<Overrides>(File.ReadAllText(path), new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) : null; }
        catch (Exception ex) when (ex is IOException or JsonException) { return null; }
    }

    private static UnrealProject Apply(UnrealProject p, Overrides? o) => o is null ? p : new UnrealProject
    {
        ProjectFile = p.ProjectFile, Name = p.Name, PrimaryModule = p.PrimaryModule, EngineAssociation = p.EngineAssociation,
        EngineRoot = o.EngineRoot ?? p.EngineRoot, EditorTarget = o.EditorTarget ?? p.EditorTarget, GeneralTestGroup = o.GeneralTestGroup ?? p.GeneralTestGroup,
        BotSimulationPrefix = o.BotSimulationPrefix ?? p.BotSimulationPrefix, AdminKeyHeader = o.AdminKeyHeader ?? p.AdminKeyHeader, BackendAddress = o.BackendAddress ?? p.BackendAddress,
    };
}
