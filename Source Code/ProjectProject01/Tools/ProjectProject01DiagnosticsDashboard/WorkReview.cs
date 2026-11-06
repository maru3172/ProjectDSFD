using System.Text;
using System.Text.RegularExpressions;

namespace ProjectProject01DiagnosticsDashboard;

internal enum WorkArea { Plan, Classes, Ui, Functions, Assets, Tests, Files }
internal enum WorkStatus { Observed, Review, Missing }

internal sealed record WorkReviewItem(WorkArea Area, WorkStatus Status, string Name, string Path, int? Line, string Detail);

internal sealed record WorkReviewReport(string Summary, IReadOnlyList<WorkReviewItem> Items)
{
    internal static string AreaName(WorkArea area) => area switch
    {
        WorkArea.Plan => "작업 계획",
        WorkArea.Classes => "클래스",
        WorkArea.Ui => "UI·위젯",
        WorkArea.Functions => "기능·함수",
        WorkArea.Assets => "에셋·맵",
        WorkArea.Tests => "테스트",
        _ => "Git·파일"
    };

    internal static string StatusName(WorkStatus status) => status switch
    {
        WorkStatus.Observed => "발견",
        WorkStatus.Missing => "근거 미발견",
        _ => "확인 필요"
    };
}

internal static partial class WorkReview
{
    private const long MaxSourceBytes = 2 * 1024 * 1024;
    internal sealed record Change(string Status, string Path)
    {
        internal bool IsNew => Status == "??";
        internal bool IsDeleted => Status.Contains('D');
        internal bool HasConflict => Status.Contains('U') || Status is "AA" or "DD";
    }
    internal sealed record Symbol(WorkArea Area, string Name, int Line, int End);

    internal static async Task<ProjectCheckResult> RunAsync(UnrealProject project, IEnumerable<string> plan,
        string output, CancellationToken token, IProgress<string> progress)
    {
        Task<DashboardForm.ProcessResult> Git(string[] args, Action<string>? onLine = null) =>
            DashboardForm.RunProcessAsync("git", ["--no-optional-locks", "-c", "core.quotepath=false", .. args],
                token, timeout: TimeSpan.FromMinutes(1), workingDirectory: project.ProjectRoot, outputEncoding: Encoding.UTF8, onLine: onLine);
        async Task<string> ReadGit(string[] args, string message)
        {
            var result = await Git(args).ConfigureAwait(false);
            if (result.ExitCode != 0) throw new InvalidOperationException(message + "\n" + result.Output.Trim());
            return result.Output;
        }

        progress.Report("Git에서 변경 파일과 변경 줄을 찾고 있습니다.");
        string root = Path.GetFullPath((await ReadGit(["rev-parse", "--show-toplevel"], "Git 저장소를 찾지 못했습니다.").ConfigureAwait(false)).Trim());
        string baseCommit = (await ReadGit(["rev-parse", "HEAD"], "비교할 HEAD 커밋을 찾지 못했습니다.").ConfigureAwait(false)).Trim();
        string status = await ReadGit(["status", "--porcelain=v1", "-z", "--untracked-files=all", "--no-renames"], "Git 변경 목록을 읽지 못했습니다.").ConfigureAwait(false);
        var changes = status.Split('\0').Where(entry => entry.Length >= 4 && entry[2] == ' ')
            .Select(entry => new Change(entry[..2], entry[3..])).ToArray();
        string[] requirements = plan.Select(line => line.Trim()).Where(line => line.Length > 0)
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        var diff = new ChangedLines(requirements.Length > 0);
        var diffResult = await Git(["diff", "--no-color", "--no-ext-diff", "--no-textconv", "--no-renames",
            "--src-prefix=a/", "--dst-prefix=b/", "--unified=0", baseCommit, "--"], diff.Read).ConfigureAwait(false);
        if (diffResult.ExitCode != 0) throw new InvalidOperationException("Git 변경 줄을 읽지 못했습니다.");
        var whitespace = await Git(["diff", "--check", "--no-ext-diff", "--no-textconv", baseCommit, "--"]).ConfigureAwait(false);
        if (whitespace.ExitCode is not (0 or 2)) throw new InvalidOperationException("Git 공백·충돌 검사를 실행하지 못했습니다.");

        progress.Report("변경된 클래스·기능·위젯과 기존 검사 참조를 대조하고 있습니다.");
        var items = new List<WorkReviewItem>();
        var widgets = new List<WorkReviewItem>();
        foreach (var change in changes)
        {
            token.ThrowIfCancellationRequested();
            AddChange(root, change, diff, items, widgets);
        }
        if (widgets.Count > 0)
        {
            string files = await ReadGit(["ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                "Git에서 테스트 파일 목록을 읽지 못했습니다.").ConfigureAwait(false);
            AddWidgetTestEvidence(root, files.Split('\0'), widgets, items, token);
        }
        if (whitespace.ExitCode != 0)
            items.Add(new(WorkArea.Files, WorkStatus.Review, "공백·충돌 표시", "", null, whitespace.Output.Trim()));
        foreach (var requirement in requirements)
            items.Add(CheckRequirement(requirement, changes, items, diff.ChangedText));

        string currentHead = (await ReadGit(["rev-parse", "HEAD"], "검사 종료 시 HEAD를 읽지 못했습니다.").ConfigureAwait(false)).Trim();
        if (!string.Equals(baseCommit, currentHead, StringComparison.Ordinal))
            items.Add(new(WorkArea.Files, WorkStatus.Review, "검사 중 기준 커밋 변경", "", null, "검사 중 HEAD가 변경됐습니다. 변경을 마친 뒤 다시 검사하세요."));
        int candidates = items.Count(item => item.Status != WorkStatus.Observed);
        string summary = $"HEAD {baseCommit[..Math.Min(baseCommit.Length, 8)]} 기준 · 변경 파일 {changes.Length}개 · 확인할 항목 {candidates}개";
        var ordered = items.OrderBy(item => item.Area).ThenByDescending(item => item.Status)
            .ThenBy(item => item.Path, StringComparer.OrdinalIgnoreCase).ThenBy(item => item.Line).ToArray();
        string[] Format(WorkReviewItem item) =>
            [$"[{WorkReviewReport.AreaName(item.Area)}] [{WorkReviewReport.StatusName(item.Status)}] {item.Name}",
                string.IsNullOrEmpty(item.Path) ? "" : item.Path + (item.Line is int line ? $":{line}" : ""), item.Detail, ""];
        await File.WriteAllLinesAsync(Path.Combine(output, "Details.txt"),
            new[] { summary, "코드 이름·변경 줄·테스트 참조에 기반한 검토 결과입니다. 테스트는 실행하지 않았습니다.", "" }
                .Concat(ordered.SelectMany(Format)), Encoding.UTF8, token).ConfigureAwait(false);
        return new(candidates == 0, summary, new(summary, ordered));
    }

    private static void AddChange(string root, Change change, ChangedLines diff, List<WorkReviewItem> items, List<WorkReviewItem> widgets)
    {
        string path = change.Path;
        string name = Path.GetFileNameWithoutExtension(path);
        string extension = Path.GetExtension(path).ToLowerInvariant();
        string disposition = change.HasConflict ? "병합 충돌" : change.IsNew ? "Git 미추적" : change.IsDeleted ? "삭제" : "변경";
        items.Add(new(WorkArea.Files, change.HasConflict || change.IsNew ? WorkStatus.Review : WorkStatus.Observed,
            $"{name} · {disposition}", path, null, $"Git 상태 [{change.Status}]."));
        if (extension is ".uasset" or ".umap")
        {
            bool ui = extension == ".uasset" && (name.StartsWith("WBP_", StringComparison.OrdinalIgnoreCase) ||
                path.Split('/').Any(segment => segment.Equals("Widget", StringComparison.OrdinalIgnoreCase) ||
                    segment.Equals("Widgets", StringComparison.OrdinalIgnoreCase) || segment.Equals("UI", StringComparison.OrdinalIgnoreCase)));
            var item = new WorkReviewItem(ui ? WorkArea.Ui : WorkArea.Assets, WorkStatus.Observed,
                name, path, null, $"{disposition}된 Unreal 바이너리입니다. 파일 경로로 분류했습니다.");
            items.Add(item);
            if (ui && !change.IsDeleted) widgets.Add(item);
            return;
        }
        if (!IsSource(extension)) return;
        if (IsTestFile(path))
            items.Add(new(WorkArea.Tests, WorkStatus.Observed, name, path, null, $"{disposition}된 테스트 파일입니다."));

        diff.Removed.TryGetValue(path, out var removed);
        string[]? source = change.IsDeleted ? null : ReadSource(root, path);
        if (source is null && !change.IsDeleted)
            items.Add(new(WorkArea.Files, WorkStatus.Review, "코드 읽기 제한", path, null,
                "2 MiB 이하의 일반 소스 파일만 기호를 분석합니다. 접근이 제한된 파일과 링크는 건너뜁니다."));
        var liveSymbols = source is null ? [] : ScanSymbols(source, extension).ToArray();
        if (source is not null && change.IsNew) diff.AddUntrackedText(path, source);
        if (removed is not null)
        {
            foreach (var (number, line) in removed)
            {
                var symbol = ReadSymbol(line, extension);
                if (symbol is null || liveSymbols.Any(current => current.Name == symbol.Value.Name && current.Area == symbol.Value.Area)) continue;
                items.Add(new(symbol.Value.Area, WorkStatus.Observed, symbol.Value.Name, path, number, "HEAD의 기존 줄에서 삭제된 선언 또는 경로를 찾았습니다."));
            }
        }
        diff.Added.TryGetValue(path, out var added);
        var owners = new HashSet<string>(StringComparer.Ordinal);
        foreach (var symbol in liveSymbols.Where(symbol => change.IsNew || (added?.GetViewBetween(symbol.Line, symbol.End).Count ?? 0) > 0))
        {
            var item = new WorkReviewItem(symbol.Area, WorkStatus.Observed, symbol.Name, path, symbol.Line,
                change.IsNew ? "미추적 파일의 선언 또는 경로에서 찾았습니다." : "이 선언 또는 본문 범위에 Git 변경 줄이 있습니다.");
            items.Add(item);
            if (item.Area == WorkArea.Ui) widgets.Add(item);
            int separator = symbol.Name.LastIndexOf("::", StringComparison.Ordinal);
            if (symbol.Area != WorkArea.Functions || separator < 0) continue;
            string owner = symbol.Name[..separator].Split("::", StringSplitOptions.None)[^1];
            if (!owners.Add(owner) || !Regex.IsMatch(owner, @"^[AUFI][A-Z]")) continue;
            var ownerItem = new WorkReviewItem(LooksLikeWidget(owner) ? WorkArea.Ui : WorkArea.Classes,
                WorkStatus.Observed, owner, path, symbol.Line, $"변경된 메서드 {symbol.Name}에서 찾은 타입입니다.");
            items.Add(ownerItem);
            if (ownerItem.Area == WorkArea.Ui) widgets.Add(ownerItem);
        }
    }

    private static void AddWidgetTestEvidence(string root, IEnumerable<string> paths, List<WorkReviewItem> widgets,
        List<WorkReviewItem> items, CancellationToken token)
    {
        var wanted = widgets.SelectMany(widget => WidgetNames(widget.Name)).ToHashSet(StringComparer.Ordinal);
        var references = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        int unreadable = 0;
        foreach (string path in paths.Where(IsTestFile).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            token.ThrowIfCancellationRequested();
            var lines = ReadSource(root, path);
            if (lines is null) { unreadable++; continue; }
            string testSource = NonCode().Replace(string.Join("\n", lines), match => match.Groups["comment"].Success ? "" : match.Value);
            foreach (string word in Identifiers().Matches(testSource).Select(match => match.Value).Where(wanted.Contains).Distinct(StringComparer.Ordinal))
            {
                if (!references.TryGetValue(word, out var matches)) references[word] = matches = [];
                matches.Add(path);
            }
        }
        foreach (var widget in widgets.DistinctBy(item => (item.Name, item.Path)))
        {
            var matches = WidgetNames(widget.Name).Where(references.ContainsKey)
                .SelectMany(key => references[key]).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
            items.Add(widget with
            {
                Area = WorkArea.Tests,
                Status = matches.Length == 0 ? WorkStatus.Review : WorkStatus.Observed,
                Name = widget.Name + " · UI 검사 참조",
                Detail = matches.Length == 0
                    ? "찾은 테스트 소스에서 이 위젯 이름을 직접 참조하는 곳이 없습니다. 간접 검사나 에셋 기반 검사는 따로 확인하세요."
                    : "이름을 참조한 테스트 파일입니다. 실제 검사 범위는 테스트 본문에서 확인하세요.\n" + string.Join("\n", matches)
            });
        }
        if (unreadable > 0)
            items.Add(new(WorkArea.Tests, WorkStatus.Review, "읽지 못한 테스트 파일", "", null,
                $"테스트 파일 {unreadable}개를 이름 참조 검색에서 건너뛰었습니다."));
    }

    private static IEnumerable<string> WidgetNames(string name)
    {
        yield return name;
        if (name.Length > 2 && name[0] == 'U' && char.IsUpper(name[1])) yield return name[1..];
    }

    internal static WorkReviewItem CheckRequirement(string line, IReadOnlyList<Change> changes, IReadOnlyList<WorkReviewItem> items,
        IEnumerable<(string Path, int Line, string Text)>? changedText = null)
    {
        string[] parts = line.Split('|', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        string title = parts.FirstOrDefault() ?? line;
        string path = title.Replace('\\', '/');
        if (parts.Length <= 1 && changes.FirstOrDefault(change => change.Path.Equals(path, StringComparison.OrdinalIgnoreCase)) is { } file)
            return new(WorkArea.Plan, WorkStatus.Observed, title, file.Path, null, $"계획의 파일 경로와 Git 상태 [{file.Status}]가 일치합니다.");

        var terms = (parts.Length > 1 ? parts[1..] : [title]).Where(term => term.Length >= 3).ToArray();
        var evidence = items.Where(item => item.Area is not (WorkArea.Plan or WorkArea.Tests) && item.Status == WorkStatus.Observed &&
                terms.Any(term => ContainsTerm(item.Name, term) || ContainsTerm(item.Path, term)))
            .DistinctBy(item => (item.Name, item.Path)).Take(6).ToArray();
        if (evidence.Length > 0)
            return new(WorkArea.Plan, WorkStatus.Observed, title, evidence[0].Path, evidence[0].Line,
                "변경 항목에서 이름이나 경로가 일치했습니다.\n" + string.Join("\n", evidence.Select(item => $"{item.Name}  ·  {item.Path}")));
        if (changedText is not null && terms.Length > 0)
        {
            foreach (var changed in changedText)
                if (!IsTestFile(changed.Path) && terms.Any(term => ContainsTerm(changed.Text, term)))
                    return new(WorkArea.Plan, WorkStatus.Observed, title, changed.Path, changed.Line,
                        "추가되거나 수정된 코드 줄에서 검색어를 찾았습니다. 구현 내용은 해당 줄을 확인하세요.");
        }
        return new(WorkArea.Plan, WorkStatus.Missing, title, "", null,
            "현재 Git 변경에서 일치하는 이름이나 경로를 찾지 못했습니다. 이미 커밋한 변경이나 다른 이름으로 구현한 기능은 별도로 확인하세요."
            + (terms.Length == 0 ? "" : "\n검색어  " + string.Join(" / ", terms)));
    }

    private static bool ContainsTerm(string value, string term)
    {
        // Ignore case but keep the identifier intact. A whole filename/class or a path may be used directly.
        if (value.Equals(term, StringComparison.OrdinalIgnoreCase)) return true;
        return Identifiers().Matches(value).Any(match => match.Value.Equals(term, StringComparison.OrdinalIgnoreCase))
            || (term.Any(c => !char.IsLetterOrDigit(c) && c != '_') && value.Contains(term, StringComparison.OrdinalIgnoreCase));
    }

    private static bool IsSource(string ext) => ext is ".h" or ".hpp" or ".cpp" or ".inl" or ".cs" or ".ts" or ".tsx" or ".js" or ".jsx";
    private static bool IsTestFile(string path)
    {
        string name = Path.GetFileNameWithoutExtension(path);
        return IsSource(Path.GetExtension(path).ToLowerInvariant()) &&
            (name.EndsWith("Test", StringComparison.OrdinalIgnoreCase) || name.EndsWith("Tests", StringComparison.OrdinalIgnoreCase)
                || name.EndsWith("Checks", StringComparison.OrdinalIgnoreCase) || name.EndsWith(".Spec", StringComparison.OrdinalIgnoreCase)
                || path.Contains("/ProjectProject01FeatureChecks/", StringComparison.OrdinalIgnoreCase));
    }

    private static string[]? ReadSource(string root, string relative)
    {
        try
        {
            if (Path.IsPathRooted(relative) || relative.Split('/').Any(part => part is "" or "." or "..")) return null;
            string path = root;
            foreach (string part in relative.Split('/'))
            {
                path = Path.Combine(path, part);
                if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) return null;
            }
            var file = new FileInfo(path);
            return file.Exists && file.Length <= MaxSourceBytes ? File.ReadAllLines(path) : null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException) { return null; }
    }

    internal static IEnumerable<Symbol> ScanSymbols(string[] lines, string extension)
    {
        // Mask comments and string literals for brace pairing while preserving line and character offsets.
        string[] code = NonCode().Replace(string.Join("\n", lines), match =>
            string.Concat(match.Value.Select(c => c == '\n' ? '\n' : ' '))).Split('\n');
        var closing = new Dictionary<(int Line, int Column), int>();
        var braces = new Stack<(int Line, int Column)>();
        for (int line = 0; line < code.Length; line++)
        for (int column = 0; column < code[line].Length; column++)
        {
            if (code[line][column] == '{') braces.Push((line, column));
            if (code[line][column] == '}' && braces.TryPop(out var open)) closing[open] = line;
        }
        for (int line = 0; line < code.Length; line++)
        {
            var symbol = ReadSymbol(code[line], extension);
            if (Route().Match(lines[line]) is { Success: true } route && code[line].Contains(route.Groups["method"].Value + "(", StringComparison.Ordinal))
                symbol = (WorkArea.Functions, route.Groups["method"].Value + " " + route.Groups["path"].Value);
            if (symbol is null) continue;
            int end = line;
            if (!symbol.Value.Name.Contains('/'))
            {
                for (int next = line; next < Math.Min(line + 12, code.Length); next++)
                {
                    int delimiter = code[next].IndexOfAny(['{', ';', '}']);
                    if (delimiter < 0) continue;
                    if (closing.TryGetValue((next, delimiter), out int close)) end = close;
                    else end = next;
                    break;
                }
            }
            yield return new(symbol.Value.Area, symbol.Value.Name, line + 1, end + 1);
        }
    }

    private static (WorkArea Area, string Name)? ReadSymbol(string line, string extension)
    {
        var type = TypeDeclaration().Match(line);
        if (type.Success)
        {
            string name = type.Groups["name"].Value;
            bool ui = LooksLikeWidget(name) || line.Contains("UserWidget", StringComparison.Ordinal);
            return (ui ? WorkArea.Ui : WorkArea.Classes, name);
        }
        var function = (extension == ".cs" ? ManagedMethod() : NativeMethod()).Match(line);
        if (!function.Success && extension is ".h" or ".hpp" or ".inl") function = HeaderMethod().Match(line);
        if (!function.Success) return null;
        string symbol = function.Groups["name"].Value;
        string prefix = function.Groups["prefix"].Value.Trim();
        if (prefix is "return" or "co_return" or "throw" or "else")
            return null;
        if (symbol.Contains("::") && prefix.Length == 0)
        {
            string[] names = symbol.Split("::");
            if (names[^2] != names[^1].TrimStart('~')) return null;
        }
        return (WorkArea.Functions, symbol);
    }

    private static bool LooksLikeWidget(string name) =>
        name.Contains("Widget", StringComparison.Ordinal) || name.EndsWith("Form", StringComparison.Ordinal);

    internal sealed class ChangedLines(bool retainText = false)
    {
        internal Dictionary<string, SortedSet<int>> Added { get; } = new(StringComparer.OrdinalIgnoreCase);
        internal Dictionary<string, List<(int Line, string Text)>> Removed { get; } = new(StringComparer.OrdinalIgnoreCase);
        internal List<(string Path, int Line, string Text)> ChangedText { get; } = [];
        private string? path;
        private int oldLine, newLine, oldRemaining, newRemaining;

        internal void AddUntrackedText(string file, string[] lines)
        {
            if (!retainText) return;
            for (int index = 0; index < lines.Length; index++) ChangedText.Add((file, index + 1, lines[index]));
        }

        internal void Read(string line)
        {
            if (path is not null && (oldRemaining > 0 || newRemaining > 0))
            {
                if (line.StartsWith('+') && newRemaining > 0)
                {
                    if (!Added.TryGetValue(path, out var added)) Added[path] = added = [];
                    if (retainText) ChangedText.Add((path, newLine, line[1..]));
                    added.Add(newLine++);
                    newRemaining--;
                    return;
                }
                if (line.StartsWith('-') && oldRemaining > 0)
                {
                    if (!Removed.TryGetValue(path, out var removed)) Removed[path] = removed = [];
                    removed.Add((oldLine++, line[1..]));
                    oldRemaining--;
                    return;
                }
            }
            if (line.StartsWith("diff --git ", StringComparison.Ordinal))
            {
                path = null;
                oldRemaining = newRemaining = 0;
            }
            if (line.StartsWith("--- a/", StringComparison.Ordinal) || line.StartsWith("+++ b/", StringComparison.Ordinal))
                path = line[6..].TrimEnd('\t');
            var hunk = Hunk().Match(line);
            if (!hunk.Success || path is null || !IsSource(Path.GetExtension(path).ToLowerInvariant())) return;
            oldLine = int.Parse(hunk.Groups[1].Value);
            newLine = int.Parse(hunk.Groups[3].Value);
            oldRemaining = hunk.Groups[2].Success ? int.Parse(hunk.Groups[2].Value) : 1;
            newRemaining = hunk.Groups[4].Success ? int.Parse(hunk.Groups[4].Value) : 1;
            if (newRemaining == 0 && oldRemaining > 0)
            {
                if (!Added.TryGetValue(path, out var added)) Added[path] = added = [];
                added.Add(Math.Max(1, newLine));
            }
        }
    }

    [GeneratedRegex(@"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")]
    private static partial Regex Hunk();
    [GeneratedRegex(@"[\p{L}_][\p{L}\p{N}_]*")]
    private static partial Regex Identifiers();
    [GeneratedRegex("""(?<comment>//[^\n]*|/\*[\s\S]*?\*/)|"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*'""")]
    private static partial Regex NonCode();
    [GeneratedRegex(@"^\s*(?:(?:public|private|protected|internal|sealed|partial|abstract|static|export|readonly)\s+)*(?:class|struct|interface|record(?:\s+struct)?)\s+(?:(?:[A-Z0-9_]+_API)\s+)?(?<name>[A-Za-z_]\w*)\b(?!\s*;)")]
    private static partial Regex TypeDeclaration();
    [GeneratedRegex(@"^\s*(?<prefix>(?:[\w:<>,*&~]+[ \t]+)*)(?<name>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*::~?[A-Za-z_]\w*|function[ \t]+[A-Za-z_]\w*)\s*\(")]
    private static partial Regex NativeMethod();
    [GeneratedRegex(@"^[ \t]*(?<prefix>(?:[\w:<>,*&~]+[ \t]+)+)(?<name>[A-Za-z_]\w*)[ \t]*\(")]
    private static partial Regex HeaderMethod();
    [GeneratedRegex(@"^\s*(?:public|private|protected|internal)\s+(?:(?:[\w.<>,?\[\]]+)[ \t]+)+(?<name>[A-Za-z_]\w*)\s*\(")]
    private static partial Regex ManagedMethod();
    [GeneratedRegex("""\b(?<method>Map(?:Get|Post|Put|Patch|Delete|Group))\(\s*"(?<path>/[^"]*)["]""")]
    private static partial Regex Route();
}
