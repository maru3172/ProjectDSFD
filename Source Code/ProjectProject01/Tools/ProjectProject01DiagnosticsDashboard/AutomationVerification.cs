using System.Globalization;
using System.Text.Json;
using System.Xml.Linq;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed record VerificationAttempt(string Id, string RunDirectory, string Directory, DateTimeOffset StartedUtc)
{
    internal string ReportDirectory => Path.Combine(Directory, "AutomationReport");
    internal string ReportFile => Path.Combine(ReportDirectory, "index.json");
    internal string EngineLog => Path.Combine(Directory, "Unreal.log");
    internal string Relative(string name) => Path.GetRelativePath(RunDirectory, Path.Combine(Directory, name));
}

internal sealed record AutomationVerdict(string Status, string Reason, int? ExitCode = null,
    int? Succeeded = null, int? Warnings = null, int? Failed = null, int? NotRun = null, int? InProcess = null)
{
    internal bool Passed => Status == "Succeeded";
    internal string Description => AutomationVerification.StatusText(Status) + "：" + Reason;
}

internal static class AutomationVerification
{
    internal static VerificationAttempt CreateAttempt(string runDirectory)
    {
        string id = DateTimeOffset.UtcNow.ToString("yyyyMMdd_HHmmss", CultureInfo.InvariantCulture) + "_" + Guid.NewGuid().ToString("N");
        string directory = Path.Combine(Path.GetFullPath(runDirectory), "Verification", "Attempts", id);
        System.IO.Directory.CreateDirectory(directory);
        return new(id, Path.GetFullPath(runDirectory), directory, DateTimeOffset.UtcNow);
    }
    internal static string[] Arguments(string projectFile, VerificationAttempt attempt, string? filter = null) =>
        ArgumentsFor(projectFile, attempt, filter ?? UnrealProject.Load(projectFile).GeneralTestGroup);
    private static string[] ArgumentsFor(string projectFile, VerificationAttempt attempt, string filter) =>
        [projectFile, "-unattended", "-nop4", "-nosplash", "-RenderOffscreen", "-TestExit=Automation Test Queue Empty",
         "-ExecCmds=Automation RunTests " + filter,
         "-ReportExportPath=" + attempt.ReportDirectory, "-abslog=" + attempt.EngineLog, "-stdout", "-FullStdOutLogOutput"];

    internal static string[] SplitFilters(string filter)
    {
        // UE 5.8 keeps a 16,384-character internal command line even when it reads a response file.
        // Keep room for paths and flags, and only split between complete test filters.
        const int limit = 8_000;
        if (filter.Length <= limit) return [filter];
        var batches = new List<string>();
        var batch = new List<string>();
        int length = 0;
        foreach (string name in filter.Split('+'))
        {
            if (batch.Count > 0 && length + name.Length + 1 > limit)
            {
                batches.Add(string.Join('+', batch));
                batch.Clear();
                length = 0;
            }
            batch.Add(name);
            length += name.Length + 1;
        }
        batches.Add(string.Join('+', batch));
        return batches.ToArray();
    }

    internal static AutomationVerdict CombineBatches(IReadOnlyList<AutomationVerdict> results)
    {
        int success = results.Count(result => result.Passed);
        int Count(Func<AutomationVerdict, int?> select) => results.Sum(result => select(result) ?? 0);
        int passed = Count(result => result.Succeeded), warnings = Count(result => result.Warnings);
        bool complete = success == results.Count;
        return new(complete ? "Succeeded" : "BatchFailed",
            $"묶음 {success}/{results.Count}개 통과 · 성공 {passed}개 · 경고 포함 성공 {warnings}개. 자세한 결과는 묶음별 로그를 확인하세요.",
            complete ? 0 : null, passed, warnings, Count(result => result.Failed), Count(result => result.NotRun), Count(result => result.InProcess));
    }

    internal static AutomationVerdict Evaluate(VerificationAttempt attempt, int exitCode, string? requestedFilter = null)
    {
        // Each invocation owns a fresh directory. Never fall back to Verification/AutomationReport.
        if (!File.Exists(attempt.ReportFile)) return new(exitCode != 0 ? "ProcessFailed" : "ReportMissing",
            exitCode != 0 ? $"검사 프로그램이 종료 코드 {exitCode}로 중단됐고 이번 실행 보고서가 없습니다. 이번 Unreal.log를 확인하세요." : "프로그램은 종료됐지만 이번 실행의 보고서가 생성되지 않았습니다.", exitCode);
        try
        {
            if (new FileInfo(attempt.ReportFile).Length > 32 * 1024 * 1024) return new("InvalidReport", "보고서가 32 MiB 상한을 초과했습니다.", exitCode);
            if (File.GetLastWriteTimeUtc(attempt.ReportFile) < attempt.StartedUtc.UtcDateTime.AddSeconds(-2))
                return new("StaleReport", "실행 시작 전 보고서입니다. 과거 성공·실패 횟수는 판정에 사용하지 않았습니다.", exitCode);
            using var document = JsonDocument.Parse(File.ReadAllText(attempt.ReportFile));
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return new("InvalidReport", "보고서 루트가 올바르지 않습니다.", exitCode);
            if (root.TryGetProperty("reportCreatedOn", out var date) && date.ValueKind == JsonValueKind.String &&
                DateTimeOffset.TryParseExact(date.GetString(), "yyyy.MM.dd-HH.mm.ss", CultureInfo.InvariantCulture,
                    DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out var created) && created < attempt.StartedUtc.AddSeconds(-2))
                return new("StaleReport", "보고서 내부 생성 시각이 이번 실행보다 오래됐습니다. 예전 횟수는 표시하지 않습니다.", exitCode);
            int? Count(string name) => root.TryGetProperty(name, out var item) && item.TryGetInt32(out int n) && n >= 0 ? n : null;
            int? passed = Count("succeeded"), warnings = Count("succeededWithWarnings"), failed = Count("failed"), notRun = Count("notRun"), inProcess = Count("inProcess");
            if (new[] { passed, warnings, failed, notRun, inProcess }.Any(n => !n.HasValue) ||
                !root.TryGetProperty("tests", out var tests) || tests.ValueKind != JsonValueKind.Array)
                return new("InvalidReport", "필수 검사 횟수나 검사 목록이 누락됐습니다.", exitCode);
            AutomationVerdict Verdict(string status, string reason) => new(status, reason, exitCode, passed, warnings, failed, notRun, inProcess);
            if (exitCode != 0) return Verdict("ProcessFailed", $"검사 프로그램이 비정상 종료했습니다 (코드 {exitCode}). 일부 결과가 있어도 전체 성공으로 처리하지 않습니다.");
            if (failed > 0) return Verdict("TestFailed", $"이번 검사 {failed}개가 실패했습니다. 이번 보고서의 실패 항목과 로그를 확인하세요.");
            if (notRun > 0 || inProcess > 0) return Verdict("Incomplete", $"미실행 {notRun}개, 미완료 {inProcess}개가 남았습니다.");
            if (passed + warnings == 0 || tests.GetArrayLength() == 0) return Verdict("NoTests", "실제로 완료된 검사가 없습니다. 검사 필터·실행 환경을 확인하세요.");
            // A truncated/inconsistent summary must not be mistaken for a complete queue.
            if ((long)passed!.Value + warnings!.Value + failed!.Value + notRun!.Value + inProcess!.Value != tests.GetArrayLength() ||
                tests.EnumerateArray().Any(t => !t.TryGetProperty("state", out var state) || state.ValueKind != JsonValueKind.String ||
                    state.GetString() is not ("Success" or "SuccessWithWarnings")))
                return Verdict("Incomplete", "집계와 검사별 완료 상태가 일치하지 않습니다.");
            if (!string.IsNullOrEmpty(requestedFilter) && !requestedFilter.StartsWith("Group:", StringComparison.OrdinalIgnoreCase))
            {
                var names = tests.EnumerateArray()
                    .Select(row => row.TryGetProperty("fullTestPath", out var name) ? name.GetString() ?? "" : "").ToArray();
                var missing = requestedFilter.Split('+').Where(filter => !names.Any(name => MatchesFilter(name, filter))).ToArray();
                if (missing.Length > 0) return Verdict("Incomplete", "실행 결과가 없는 검사: " + string.Join(", ", missing));
            }
            return Verdict("Succeeded", $"성공 {passed}개, 경고 포함 성공 {warnings}개. 이번 실행의 검사 목록이 모두 완료됐습니다.");
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException or InvalidOperationException)
        { return new("InvalidReport", "이번 보고서를 읽지 못했습니다: " + ex.GetType().Name, exitCode); }
    }

    private static bool MatchesFilter(string name, string filter)
    {
        string value = filter.Trim('^', '$');
        const StringComparison comparison = StringComparison.OrdinalIgnoreCase;
        if (filter.StartsWith('^'))
            return filter.EndsWith('$') ? name.Equals(value, comparison) : name.StartsWith(value, comparison);
        return filter.EndsWith('$') ? name.EndsWith(value, comparison) : name.Contains(value, comparison);
    }
    internal static string StatusText(string status) => status switch
    {
        "Succeeded" => "성공", "TestFailed" => "검사 실패", "BatchFailed" => "검사 묶음 확인 필요",
        "ProcessFailed" => "실행 중단", "ReportMissing" => "보고서 없음",
        "InvalidReport" => "보고서 오류", "StaleReport" => "과거 보고서 제외", "Incomplete" => "검사 미완료", "NoTests" => "실행된 검사 없음",
        "NotRun" => "검사 미실행", "Running" => "검사 중", "Cancelled" => "검사 취소", "TimedOut" => "검사 시간 초과", _ => "확인 필요"
    };

    internal static void WriteFailedTests(VerificationAttempt attempt)
    {
        try
        {
            var file = new FileInfo(attempt.ReportFile);
            if (!file.Exists || file.Length > 32 * 1024 * 1024) return;
            using var report = JsonDocument.Parse(File.ReadAllText(file.FullName));
            if (!report.RootElement.TryGetProperty("tests", out var tests)) return;
            static string Field(JsonElement row, string key) =>
                row.TryGetProperty(key, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() ?? "" : "";
            var failures = new List<string>();
            foreach (var test in tests.EnumerateArray())
            {
                string state = Field(test, "state");
                if (state is "Success" or "SuccessWithWarnings") continue;
                failures.Add($"[{state}] {Field(test, "fullTestPath")}");
                if (test.TryGetProperty("entries", out var entries) && entries.ValueKind == JsonValueKind.Array)
                    foreach (var entry in entries.EnumerateArray())
                        if (entry.TryGetProperty("event", out var item) && Field(item, "type") is "Error" or "Warning")
                            failures.Add("  " + Field(item, "message"));
            }
            if (failures.Count > 0) File.WriteAllLines(Path.Combine(attempt.Directory, "FailedTests.txt"), failures);
        }
        catch (Exception e) when (e is JsonException or IOException or UnauthorizedAccessException or InvalidOperationException) { }
    }
    internal static void Save(VerificationAttempt attempt, string runId, string buildStatus, int? buildExitCode, AutomationVerdict verdict, bool roleOutcomesPass)
    {
        string final = buildStatus == "Succeeded" && verdict.Passed && roleOutcomesPass ? "Pass" : buildStatus == "Failed" ? "ReviewRequired" : verdict.Status is "Running" or "NotRun" or "Cancelled" ? "Inconclusive" : "ReviewRequired";
        var build = new XElement("Build", new XAttribute("Status", buildStatus), new XAttribute("Log", attempt.Relative("Build.log")));
        if (buildExitCode.HasValue) build.Add(new XAttribute("ExitCode", buildExitCode));
        var automation = new XElement("Automation", new XAttribute("Status", verdict.Status), new XAttribute("Log", attempt.Relative("Automation.log")));
        if (!Directory.Exists(Path.Combine(attempt.Directory, "Batch-01")))
            automation.Add(new XAttribute("EngineLog", attempt.Relative("Unreal.log")), new XAttribute("Report", attempt.Relative("AutomationReport/index.json")));
        automation.Add(new XElement("Reason", verdict.Reason));
        void Attribute(string name, int? count) { if (count.HasValue) automation.Add(new XAttribute(name, count)); }
        Attribute("ExitCode", verdict.ExitCode); Attribute("SucceededCount", verdict.Succeeded); Attribute("WarningCount", verdict.Warnings);
        Attribute("FailedCount", verdict.Failed); Attribute("NotRunCount", verdict.NotRun); Attribute("InProcessCount", verdict.InProcess);
        var document = new XDocument(new XElement("Verification", new XAttribute("TestRunId", runId), new XAttribute("AttemptId", attempt.Id),
            new XAttribute("StartedUtc", attempt.StartedUtc.ToString("O")), new XAttribute("RecordedUtc", DateTimeOffset.UtcNow.ToString("O")),
            new XAttribute("FinalOutcome", final), build, automation));
        document.Save(Path.Combine(attempt.Directory, "Verification.xml"));
        string destination = Path.Combine(attempt.RunDirectory, "Verification.xml"), temporary = Path.Combine(attempt.Directory, "CurrentVerification.tmp");
        document.Save(temporary); File.Move(temporary, destination, true);
    }
}
