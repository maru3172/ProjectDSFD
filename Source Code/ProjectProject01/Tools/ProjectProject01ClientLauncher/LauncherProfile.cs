using System.Text.Json;

#if PROJECT_SERVER_HELPER
namespace ProjectProject01BuildServerHelper;
#else
namespace ProjectProject01ClientLauncher;
#endif

/// <summary>
/// Everything that ties the launcher to one game, read from <c>launcher.json</c> next to the launcher. Without the file
/// every value falls back to something that works for any packaged Unreal game: the executable comes from the update
/// manifest (or the only top-level .exe), its process names are derived from it, and caches are found under any
/// <c>*/Saved</c>. So the launcher folder can be copied into another project and used as it is.
/// </summary>
internal sealed class LauncherProfile
{
    /// <summary>Shown in messages ("{name}를 찾지 못했습니다").</summary>
    public string GameName { get; set; } = "";
    /// <summary>Top-level executables to look for, in order, when the local manifest does not name one.</summary>
    public List<string> GameExecutables { get; set; } = [];
    /// <summary>Extra process names that mean the game is running (the executable's own names are always checked).</summary>
    public List<string> ProcessNames { get; set; } = [];
    /// <summary>Command line given to the game; <c>{address}</c> becomes the server address.</summary>
    public string ServerArgument { get; set; } = "-Server={address}";
    public int BackendPort { get; set; } = 5080;
    /// <summary>Folder under %LocalAppData% for the launcher's own settings.</summary>
    public string SettingsFolder { get; set; } = "GameLauncher";
    /// <summary>Folders the "clear cache" button empties, relative to the install root; a leading <c>*/</c> matches any folder.</summary>
    public List<string> CacheFolders { get; set; } = [".update-cache", "*/Saved/DerivedDataCache", "*/Saved/Shaders"];

    public const string FileName = "launcher.json";
    private static LauncherProfile? current;
    public static LauncherProfile Current => current ??= Load(AppContext.BaseDirectory);

    public static LauncherProfile Load(string folder)
    {
        var path = Path.Combine(folder, FileName);
        try
        {
            if (File.Exists(path) && JsonSerializer.Deserialize<LauncherProfile>(File.ReadAllText(path), new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) is { } loaded)
                return loaded;
        }
        catch (JsonException) { /* A broken profile falls back to the generic defaults. */ }
        return new LauncherProfile();
    }

    public string DisplayName(string? executablePath = null) =>
        GameName.Length > 0 ? GameName : executablePath is { Length: > 0 } ? Path.GetFileNameWithoutExtension(executablePath)
        : GameExecutables.Count > 0 ? Path.GetFileNameWithoutExtension(GameExecutables[0]) : "게임";

    /// <summary>Process names for "is the game running": the given executable, its Unreal Shipping binary, and the profile's extras.</summary>
    public IEnumerable<string> RunningProcessNames(string executablePath)
    {
        var names = new List<string>();
        foreach (var exe in GameExecutables.Prepend(executablePath))
        {
            var name = Path.GetFileNameWithoutExtension(exe);
            if (name.Length == 0) continue;
            names.Add(name);names.Add(name + "-Win64-Shipping");
        }
        return names.Concat(ProcessNames).Distinct(StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>The game executable in an install folder when no manifest names one.</summary>
    public string? FindExecutable(string installRoot)
    {
        foreach (var exe in GameExecutables)
            if (File.Exists(Path.Combine(installRoot, exe))) return exe;
        if (GameExecutables.Count > 0) return null;
        var launcher = Path.GetFileName(Environment.ProcessPath ?? "");
        var found = Directory.EnumerateFiles(installRoot, "*.exe", SearchOption.TopDirectoryOnly)
            .Select(Path.GetFileName).Where(n => n is not null && !string.Equals(n, launcher, StringComparison.OrdinalIgnoreCase) &&
                !n.Contains("Launcher", StringComparison.OrdinalIgnoreCase) && !n.Contains("Prereq", StringComparison.OrdinalIgnoreCase)).ToList();
        return found.Count == 1 ? found[0] : null;
    }

    public bool LooksLikeInstall(string folder) => FindExecutable(folder) is not null;

    public string GameArguments(string address) => ServerArgument.Replace("{address}", address, StringComparison.Ordinal);

    /// <summary>Cache folders that exist under the install root, with <c>*/</c> expanded.</summary>
    public IEnumerable<string> ExistingCacheFolders(string installRoot)
    {
        foreach (var entry in CacheFolders)
        {
            var relative = entry.Replace('\\', '/');
            if (relative.StartsWith("*/", StringComparison.Ordinal))
            {
                if (!Directory.Exists(installRoot)) continue;
                foreach (var dir in Directory.EnumerateDirectories(installRoot))
                    yield return Path.GetRelativePath(installRoot, Path.Combine(dir, relative[2..])).Replace('\\', '/');
            }
            else yield return relative;
        }
    }
}
