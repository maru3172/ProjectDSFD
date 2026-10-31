using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace ProjectDFSDGitManager;

internal sealed record CommandResult(int Code, string Output, string Error)
{
    public string Display => (Output.TrimEnd() + "\n" + Error.TrimEnd()).Trim();
}
internal sealed record BranchInfo(string Name, bool Remote, bool Current)
{
    public override string ToString() => Name + (Current ? "  (현재)" : Remote ? "  (원격 추적)" : "");
}
internal sealed record FileChange(string Index, string Work, string Path, string? Original);
internal sealed record CommitInfo(string Hash, string Date, string Author, string Subject);
internal sealed record WorktreeInfo(string Path, string Branch, string Head);
internal sealed class MergePlan
{
    public string Repository { get; set; } = "";
    public string SourceRef { get; set; } = "";
    public string TargetBranch { get; set; } = "";
    public string SourceCommit { get; set; } = "";
    public string TargetCommit { get; set; } = "";
    public List<AssetDecision> Assets { get; set; } = [];
    public string? PreviewPath { get; set; }
    public string? PreviewBranch { get; set; }
    public string[] UnresolvedPaths { get; set; } = [];
}
internal sealed class AssetDecision
{
    public string Path { get; set; } = "";
    public string Kind { get; set; } = "";
    public string Choice { get; set; } = "";
    public bool ReviewRequired { get; set; }
    public string Reason { get; set; } = "";
    public string? SourceBlob { get; set; }
    public string? TargetBlob { get; set; }
}
internal sealed class MergeResult
{
    public string PlanFile { get; set; } = "";
    public string? PreviewPath { get; set; }
    public string? PreviewBranch { get; set; }
    public string[] CodeConflicts { get; set; } = [];
}

internal sealed class GitService
{
    internal static readonly JsonSerializerOptions Json = new() { PropertyNameCaseInsensitive = true, WriteIndented = true };
    public string Repository { get; }
    public GitService(string repository) => Repository = System.IO.Path.GetFullPath(repository);

    public static async Task<CommandResult> RunProcess(string executable, string directory, IEnumerable<string> arguments,
        bool check = true, int timeoutSeconds = 180)
    {
        var start = new ProcessStartInfo(executable) { WorkingDirectory = directory, UseShellExecute = false,
            CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8 };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        start.Environment["GIT_TERMINAL_PROMPT"] = "0";
        start.Environment["GIT_PAGER"] = "cat";
        start.Environment["GIT_EDITOR"] = "true";
        start.Environment["GIT_SEQUENCE_EDITOR"] = "true";
        using var process = Process.Start(start) ?? throw new InvalidOperationException("프로세스를 시작하지 못했습니다.");
        var output = process.StandardOutput.ReadToEndAsync(); var error = process.StandardError.ReadToEndAsync();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(timeoutSeconds));
        try { await process.WaitForExitAsync(timeout.Token); }
        catch (OperationCanceledException)
        {
            // Only this tool's child is stopped. Git recovery state/worktree is left intact.
            try { process.Kill(true); await process.WaitForExitAsync(); } catch (InvalidOperationException) { }
            throw new TimeoutException("명령 시간이 초과되었습니다. 로그와 저장소 상태를 새로고침해 확인하세요. 재시도 전에 진행 중인 merge/rebase 상태를 확인하세요.");
        }
        var result = new CommandResult(process.ExitCode, await output, await error);
        if (check && result.Code != 0) throw new InvalidOperationException(result.Display.Length > 0 ? result.Display : $"명령 실패: {result.Code}");
        return result;
    }
    public Task<CommandResult> Git(IEnumerable<string> args, bool check = true) =>
        RunProcess("git", Repository, new[] { "-c", "core.quotepath=false", "--no-pager" }.Concat(args), check);
    public Task<CommandResult> Git(params string[] args) => Git((IEnumerable<string>)args);
    public async Task<string> ResolveRoot() => (await Git("rev-parse", "--show-toplevel")).Output.Trim();
    public async Task<string> CurrentBranch()
    {
        var result = await Git(new[] { "symbolic-ref", "--quiet", "--short", "HEAD" }, false);
        return result.Code == 0 ? result.Output.Trim() : "분리된 HEAD";
    }
    public async Task<List<BranchInfo>> Branches()
    {
        var current = await CurrentBranch();
        var output = (await Git("for-each-ref", "--format=%(refname)", "refs/heads", "refs/remotes")).Output;
        return output.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(x => x.TrimEnd('\r'))
            .Where(x => !x.EndsWith("/HEAD", StringComparison.Ordinal))
            .Select(x => new BranchInfo(x.StartsWith("refs/heads/") ? x[11..] : x[13..], x.StartsWith("refs/remotes/"), x == "refs/heads/" + current)).ToList();
    }
    public async Task<List<FileChange>> Changes()
    {
        var records = (await Git("status", "--porcelain=v1", "-z", "--untracked-files=all")).Output.Split('\0');
        var changes = new List<FileChange>();
        for (int i = 0; i < records.Length; i++)
        {
            var value = records[i]; if (value.Length < 4) continue;
            string? original = null;
            if ((value[0] is 'R' or 'C' || value[1] is 'R' or 'C') && i + 1 < records.Length) original = records[++i];
            changes.Add(new(value[..1], value.Substring(1, 1), value[3..], original));
        }
        return changes;
    }
    public async Task<List<CommitInfo>> History(string reference)
    {
        var output = (await Git("log", "-100", "--date=iso-strict", "--format=%H%x1f%ad%x1f%an%x1f%s%x1e", reference, "--")).Output;
        return output.Split('\x1e', StringSplitOptions.RemoveEmptyEntries).Select(x => x.Trim('\r', '\n').Split('\x1f'))
            .Where(x => x.Length >= 4).Select(x => new CommitInfo(x[0], x[1], x[2], x[3])).ToList();
    }
    public async Task<List<WorktreeInfo>> Worktrees()
    {
        var output = (await Git("worktree", "list", "--porcelain", "-z")).Output;
        var list = new List<WorktreeInfo>(); string path = "", head = "", branch = "";
        foreach (var item in output.Split('\0'))
        {
            if (item.StartsWith("worktree ")) { if (path.Length > 0) list.Add(new(path, branch, head)); path = item[9..]; head = branch = ""; }
            else if (item.StartsWith("HEAD ")) head = item[5..];
            else if (item.StartsWith("branch ")) branch = item[7..].Replace("refs/heads/", "");
        }
        if (path.Length > 0) list.Add(new(path, branch, head)); return list;
    }
    public string ReportDirectory => System.IO.Path.Combine(Repository, "Source Code", "ProjectProject01", "Saved", "GitMergeReviews");
    public async Task<MergeResult> Review(string source, string target, string? planFile = null, string? selectionFile = null)
    {
        var args = new List<string> { "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
            System.IO.Path.Combine(AppContext.BaseDirectory, "Review-Merge.ps1"), "-RepositoryRoot", Repository,
            "-SourceRef", source, "-TargetBranch", target, "-ReportRoot", ReportDirectory, "-JsonOutput" };
        if (planFile != null) args.AddRange(["-Action", "Prepare", "-PlanFile", planFile]);
        if (selectionFile != null) args.AddRange(["-SelectionFile", selectionFile]);
        var result = await RunProcess("pwsh", Repository, args, timeoutSeconds: 900);
        return JsonSerializer.Deserialize<MergeResult>(result.Output, Json) ?? throw new InvalidDataException("병합 보고서를 읽지 못했습니다.");
    }
    public static MergePlan ReadPlan(string path) => JsonSerializer.Deserialize<MergePlan>(File.ReadAllText(path), Json) ?? throw new InvalidDataException("계획 파일이 비어 있습니다.");
    public static string Ref(BranchInfo branch) => (branch.Remote ? "refs/remotes/" : "refs/heads/") + branch.Name;
    public static string[] SelectedPathsCommand(string command, IEnumerable<FileChange> files) =>
        new[] { "--literal-pathspecs", command, "--" }.Concat(files.SelectMany(x => x.Original is null ? new[] { x.Path } : new[] { x.Path, x.Original })).Distinct().ToArray();

    [DllImport("shell32.dll", SetLastError = true)] private static extern IntPtr CommandLineToArgvW([MarshalAs(UnmanagedType.LPWStr)] string commandLine, out int count);
    [DllImport("kernel32.dll")] private static extern IntPtr LocalFree(IntPtr memory);
    public static string[] ParseArguments(string text)
    {
        var pointer = CommandLineToArgvW("git " + text, out var count);
        if (pointer == IntPtr.Zero) throw new ArgumentException("명령을 해석하지 못했습니다.");
        try { return Enumerable.Range(1, count - 1).Select(i => Marshal.PtrToStringUni(Marshal.ReadIntPtr(pointer, i * IntPtr.Size)) ?? "").ToArray(); }
        finally { LocalFree(pointer); }
    }
}
