using System.Text;
using System.Text.RegularExpressions;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed record ProjectCheckResult(bool Passed, string Summary, WorkReviewReport? WorkReview = null);

internal static partial class ProjectVerification
{
    internal static bool IsValidAssetClass(string value) => value.Length == 0 || (value.Length <= 150 && AssetClass().IsMatch(value));

    internal static async Task<ProjectCheckResult> ValidateAssetsAsync(UnrealProject project, string assetClass, string output,
        CancellationToken token, IProgress<string> progress)
    {
        progress.Report("Unreal Data Validation에서 디스크의 에셋을 확인하고 있습니다.");
        string log = Path.Combine(output, "Unreal.log");
        var arguments = new List<string> { project.ProjectFile, "-run=DataValidation", "-IncludeOnlyOnDiskAssets",
            "-unattended", "-nop4", "-nosplash", "-NoCrashDialog", "-culture=en", "-stdout", "-FullStdOutLogOutput", "-abslog=" + log };
        if (assetClass.Length > 0) arguments.Add("-AssetType=" + assetClass);
        int assets = 0;
        var result = await DashboardForm.RunProcessAsync(project.EngineBinary("UnrealEditor-Cmd.exe"), arguments,
            token, timeout: TimeSpan.FromMinutes(30), outputFile: Path.Combine(output, "Details.txt"), onLine: line =>
            {
                const string marker = "Loading asset ";
                int start = line.IndexOf(marker, StringComparison.Ordinal);
                if (start >= 0 && ++assets % 25 == 0)
                    progress.Report($"에셋 로딩 {assets}개\n{line[(start + marker.Length)..]}");
            });
        return SummarizeAssetValidation(File.Exists(log) ? File.ReadLines(log) : [], result.ExitCode);
    }

    internal static ProjectCheckResult SummarizeAssetValidation(IEnumerable<string> lines, int exitCode)
    {
        // The UE commandlet can return zero after invalid assets. Its per-asset verdicts are logged
        // under AssetCheck even when the editor-only aggregate message is absent.
        int selected = 0, valid = 0, warnings = 0, invalid = 0, unvalidated = 0;
        foreach (string line in lines)
        {
            if (line.Contains("LogContentValidation:", StringComparison.Ordinal))
            {
                var start = ValidationStart().Match(line);
                if (start.Success) int.TryParse(start.Groups[1].Value, out selected);
            }
            if (!line.Contains("AssetCheck:", StringComparison.Ordinal)) continue;
            if (line.EndsWith("contains valid data.", StringComparison.Ordinal)) valid++;
            else if (line.EndsWith("contains valid data, but has warnings.", StringComparison.Ordinal)) warnings++;
            else if (line.EndsWith("contains invalid data.", StringComparison.Ordinal)) invalid++;
            else if (line.EndsWith("has no data validation.", StringComparison.Ordinal)) unvalidated++;
        }
        int checkedCount = valid + warnings + invalid + unvalidated;
        bool complete = selected > 0 && checkedCount == selected;
        return new(exitCode == 0 && complete && invalid == 0 && unvalidated == 0,
            $"Unreal Data Validation · 종료 코드 {exitCode}\n대상 {selected}개 · 결과 {checkedCount}개\n"
            + $"정상 {valid}개 · 경고 포함 정상 {warnings}개 · 오류 {invalid}개 · 검증 불가 {unvalidated}개\n"
            + (complete ? "" : "검사 결과가 전부 확인되지 않았습니다.\n") + "세부 사항은 Unreal 로그에서 확인하세요.");
    }

    internal static async Task<ProjectCheckResult> VerifyPackageAsync(UnrealProject project, string packageRoot,
        string output, CancellationToken token, IProgress<string> progress)
    {
        string content = Path.Combine(packageRoot, project.Name, "Content");
        string paks = Path.Combine(content, "Paks");
        bool client = File.Exists(Path.Combine(packageRoot, project.Name + ".exe"));
        bool server = File.Exists(Path.Combine(packageRoot, project.Name + "Server.exe"));
        if (client == server || !Directory.Exists(content))
            return new(false, "게임 또는 데디 서버 실행 파일이 있는 Windows 패키지 폴더 하나를 선택하세요.");

        string config = await File.ReadAllTextAsync(Path.Combine(project.ProjectRoot, "Config", "DefaultGame.ini"), token);
        var maps = CookMap().Matches(config).Cast<Match>().Select(m => m.Groups[1].Value + ".umap")
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (maps.Length == 0) return new(false, "DefaultGame.ini의 MapsToCook 목록을 확인할 수 없습니다.");
        string[] clientAssets = ["MyProject/Title/M_TitleSurface.uasset", "MyProject/Title/M_TitleMist.uasset",
            "MyProject/Title/M_TitleDust.uasset", "MyProject/VFX/M_HeartbeatScreenVFX.uasset"];
        string[] required = client && project.Name == "ProjectDFSD" ? [.. maps, .. clientAssets] : maps;
        var present = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string path in required)
            if (File.Exists(Path.Combine(content, path))) present.Add(path);
        var containers = Directory.Exists(paks)
            ? Directory.EnumerateFiles(paks).Where(file => IsContainer(file, ".pak") || IsContainer(file, ".utoc"))
                .Order(StringComparer.OrdinalIgnoreCase).ToArray()
            : [];

        var details = new List<string>();
        for (int index = 0; index < containers.Length; index++)
        {
            string container = containers[index];
            string name = Path.GetFileName(container);
            progress.Report($"패키지 컨테이너 {index + 1}/{containers.Length}\n{name}");
            if (IsContainer(container, ".utoc") && !File.Exists(Path.ChangeExtension(container, ".ucas")))
            {
                details.Add($"[읽기 실패] {name}에 대응하는 .ucas가 없습니다.");
                continue;
            }
            if (name.Equals("global.utoc", StringComparison.OrdinalIgnoreCase))
            {
                details.Add("[컨테이너 짝 확인] global.utoc / global.ucas");
                continue;
            }
            string csv = Path.Combine(output, $"Container-{index + 1}.csv");
            var result = await DashboardForm.RunProcessAsync(project.EngineBinary("UnrealPak.exe"),
                [container, "-List", "-ExtractToMountPoint", "-CSV=" + csv, "-unattended", "-NoCrashDialog", "-stdout"],
                token, timeout: TimeSpan.FromMinutes(4), outputFile: Path.Combine(output, $"Container-{index + 1}.log"));
            if (result.ExitCode != 0 || !File.Exists(csv))
            {
                details.Add($"[읽기 실패] {name} (종료 코드 {result.ExitCode})");
                continue;
            }
            details.Add($"[목록 확인] {name}");
            foreach (string path in ReadContainerEntries(File.ReadLines(csv), project.Name)) present.Add(path);
        }
        var missing = required.Where(path => !present.Contains(path)).ToList();
        if (client && project.Name == "ProjectDFSD" && !File.Exists(Path.Combine(content, "Branding", "UniversityLogo.png")))
            missing.Add("Branding/UniversityLogo.png");
        bool passed = missing.Count == 0 && details.All(line => !line.StartsWith("[읽기 실패]", StringComparison.Ordinal));
        details.AddRange(required.Select(path => (present.Contains(path) ? "[포함] " : "[누락] ") + path));
        details.AddRange(missing.Where(path => !required.Contains(path)).Select(path => "[누락] " + path));
        await File.WriteAllLinesAsync(Path.Combine(output, "Details.txt"), details, Encoding.UTF8, token);
        return new(passed, $"{(client ? "클라이언트" : "데디 서버")} 패키지 · 컨테이너 {containers.Length}개 · 필수 파일 누락 {missing.Count}개\n"
            + "실제 패키지의 파일 포함 여부를 검사했습니다. 게임 실행과 네트워크 경기는 별도로 검증합니다.");
    }

    private static bool IsContainer(string path, string extension) =>
        Path.GetExtension(path).Equals(extension, StringComparison.OrdinalIgnoreCase);

    internal static IEnumerable<string> ReadContainerEntries(IEnumerable<string> lines, string projectName)
    {
        using var rows = lines.GetEnumerator();
        if (!rows.MoveNext()) throw new InvalidDataException("UnrealPak 목록이 비어 있습니다.");
        var columns = rows.Current.Split(',').Select(column => column.Trim()).ToArray();
        int file = Array.IndexOf(columns, "Filename");
        int deleted = Array.IndexOf(columns, "Deleted");
        if (file != 0) throw new InvalidDataException("UnrealPak 목록의 Filename 열을 읽을 수 없습니다.");
        string prefix = projectName + "/Content/";
        while (rows.MoveNext())
        {
            var fields = rows.Current.Split(',');
            if (fields.Length != columns.Length || (deleted >= 0 && fields[deleted].Trim().Equals("true", StringComparison.OrdinalIgnoreCase))) continue;
            string path = fields[file].Trim().Trim('"').Replace('\\', '/');
            while (path.StartsWith("../", StringComparison.Ordinal)) path = path[3..];
            if (path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) yield return path[prefix.Length..];
        }
    }

    [GeneratedRegex(@"Starting to validate (\d+) assets?\b", RegexOptions.CultureInvariant)]
    private static partial Regex ValidationStart();

    [GeneratedRegex(@"^\s*\+MapsToCook\s*=\s*\(FilePath=""/Game/([^""]+)""\)", RegexOptions.Multiline)]
    private static partial Regex CookMap();

    [GeneratedRegex(@"\A(?:/Script/[A-Za-z0-9_]+\.)?[A-Za-z0-9_]+\z")]
    private static partial Regex AssetClass();
}
