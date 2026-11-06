using System.Text.Json;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed record AutomationCatalogSnapshot(string BuildStamp, DateTimeOffset ReadAt, string[] Tests);

internal static class AutomationTestCatalog
{
    private const string DisplayPrefix = "LogAutomationCommandLine: Display:";
    private const string CountSuffix = " Automation Tests";

    internal static string[] Arguments(string projectFile, string log) =>
        [projectFile, "-unattended", "-nop4", "-nosplash", "-NoCrashDialog", "-RenderOffscreen",
            "-TestExit=Automation Test Queue Empty", "-ExecCmds=Automation List", "-abslog=" + log, "-stdout", "-FullStdOutLogOutput"];

    internal static bool IsSelectableName(string name) => name.Length is > 0 and <= 512 && name == name.Trim() &&
        !name.Any(c => char.IsControl(c) || c is '+' or ';' or '"' or '\\' or '^' or '$');

    internal static string[] Parse(IEnumerable<string> lines, string module)
    {
        int declared = -1, listed = 0;
        bool complete = false;
        var projectTests = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string prefix = module + ".";
        foreach (string line in lines)
        {
            int marker = line.IndexOf(DisplayPrefix, StringComparison.Ordinal);
            if (marker < 0) continue;
            string text = line[(marker + DisplayPrefix.Length)..].Trim();
            if (text.StartsWith("Found ", StringComparison.Ordinal) && text.EndsWith(CountSuffix, StringComparison.Ordinal) &&
                int.TryParse(text.AsSpan(6, text.Length - 6 - CountSuffix.Length), out int count))
            {
                declared = count;
                listed = 0;
                complete = false;
                projectTests.Clear();
            }
            else if (declared >= 0 && text.Length >= 2 && text[0] == '\'' && text[^1] == '\'')
            {
                listed++;
                string name = text[1..^1];
                if (name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                {
                    if (!IsSelectableName(name)) throw new InvalidDataException($"Unreal 검사 이름에 선택할 수 없는 문자가 있습니다. {name}");
                    projectTests.Add(name);
                }
            }
            else if (declared >= 0 && text.StartsWith("...Automation Test Queue Empty ", StringComparison.Ordinal))
            {
                complete = true;
                break;
            }
        }
        if (!complete || listed != declared || projectTests.Count == 0)
            throw new InvalidDataException($"Unreal 검사 명단을 전부 읽지 못했습니다. 전체 {Math.Max(0, declared)}개 중 {listed}개를 읽었습니다.");
        return projectTests.Order(StringComparer.OrdinalIgnoreCase).ToArray();
    }

    internal static string BuildStamp(UnrealProject project)
    {
        string bin = Path.Combine(project.ProjectRoot, "Binaries", "Win64");
        var files = Directory.Exists(bin)
            ? Directory.EnumerateFiles(bin).Where(file => Path.GetExtension(file) is ".dll" or ".modules")
            : [];
        return string.Join('|', files.Append(project.ProjectFile).Append(project.EngineBinary("UnrealEditor-Cmd.exe"))
            .Order(StringComparer.OrdinalIgnoreCase).Select(file =>
            {
                var info = new FileInfo(file);
                return info.Exists ? $"{info.FullName}:{info.LastWriteTimeUtc.Ticks}:{info.Length}" : file;
            }));
    }

    internal static AutomationCatalogSnapshot? TryRead(UnrealProject project)
    {
        try
        {
            var file = new FileInfo(CacheFile(project));
            if (!file.Exists || file.Length > 2 * 1024 * 1024) return null;
            var snapshot = JsonSerializer.Deserialize<AutomationCatalogSnapshot>(File.ReadAllText(file.FullName));
            if (snapshot?.Tests is not { Length: > 0 } || snapshot.Tests.Any(name =>
                    name is null || !name.StartsWith(project.PrimaryModule + ".", StringComparison.OrdinalIgnoreCase) || !IsSelectableName(name)))
                return null;
            return snapshot;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException) { return null; }
    }

    internal static async Task<AutomationCatalogSnapshot> RefreshAsync(UnrealProject project, bool build,
        CancellationToken token, IProgress<string> progress)
    {
        string cache = CacheFile(project);
        var attempt = AutomationVerification.CreateAttempt(Path.GetDirectoryName(cache)!);
        if (build)
        {
            progress.Report("새 검사를 반영하기 위해 에디터를 빌드하고 있습니다.");
            var result = await DashboardForm.BuildAutomationEditorAsync(project, attempt.Directory, token).ConfigureAwait(false);
            if (result.ExitCode != 0)
                throw new InvalidOperationException($"빌드가 완료되지 않았습니다. 에디터·Live Coding을 닫았는지 확인하세요.\n{attempt.Directory}");
        }

        string stamp = BuildStamp(project);
        progress.Report("Unreal에서 등록된 검사 명단을 불러오고 있습니다.");
        var run = await DashboardForm.RunProcessAsync(project.EngineBinary("UnrealEditor-Cmd.exe"),
            Arguments(project.ProjectFile, attempt.EngineLog), token, timeout: TimeSpan.FromMinutes(5),
            outputFile: Path.Combine(attempt.Directory, "Catalog.log")).ConfigureAwait(false);
        if (run.ExitCode != 0 || !File.Exists(attempt.EngineLog))
            throw new InvalidOperationException($"검사 명단을 읽지 못했습니다. 종료 코드 {run.ExitCode}.\n{attempt.Directory}");
        string[] names;
        try { names = Parse(File.ReadLines(attempt.EngineLog), project.PrimaryModule); }
        catch (InvalidDataException e) { throw new InvalidDataException(e.Message + "\n" + attempt.EngineLog, e); }
        if (stamp != BuildStamp(project))
            throw new InvalidOperationException("명단을 읽는 중 빌드 파일이 바뀌었습니다. 빌드가 끝난 뒤 다시 갱신하세요.");
        var snapshot = new AutomationCatalogSnapshot(stamp, DateTimeOffset.UtcNow, names);
        string pending = Path.Combine(attempt.Directory, "Catalog.tmp");
        await File.WriteAllTextAsync(pending, JsonSerializer.Serialize(snapshot), token).ConfigureAwait(false);
        File.Move(pending, cache, true);
        return snapshot;
    }

    private static string CacheFile(UnrealProject project) =>
        Path.Combine(project.ProjectRoot, "Saved", "Verification", "AutomationCatalog", "Tests.json");
}
