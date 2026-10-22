using System.Globalization;
using System.Text.Json;

namespace ProjectProject01DiagnosticsDashboard;

// Only offline summaries live here: no new gameplay sampling, polling, forced GC or provider hooks.
internal sealed record HistoryStatistic(double? Average, double? Maximum, double? Minimum, double? Last,
    double? Total, double? Recommended, int ValidSamples)
{
    internal double? Select(string aggregation) => aggregation switch
    {
        "평균" => Average, "최대" => Maximum, "최소" => Minimum, "마지막" => Last,
        "합계·관측 증가량" => Total, _ => Recommended
    };
}

internal sealed record DiagnosticHistoryCapture(string Id, string Source, string Scope, DateTimeOffset Start,
    DateTimeOffset End, int Samples, string Context, Dictionary<string, HistoryStatistic> Metrics, string[] Notes);

internal static class LongTermDiagnostics
{
    internal static readonly string[] AllocatorKeys = ["Allocator used MiB", "Allocator OS-held MiB",
        "Allocator retained/slack MiB", "Available system RAM MiB", "Streaming texture pool %", "Total system RAM MiB"];
    internal static readonly string[] SummaryKeys = ["GC 관측 횟수", "GC 빈도", "GC 평균 경과 시간", "GC 최대 경과 시간",
        "GC당 평균 프로세스 RAM 순감소", "GC당 평균 사용 중 할당 순감소", "인증·로비 HTTP 처리량",
        "인증·로비 서버·전송 오류율", "인증·로비 4xx 비율", "인증·로비 타임아웃", "인증·로비 재시도 예정",
        "Windows 파일 디스크립터", "C++ 전체 메모리 할당률"];
    internal static readonly string[] FailureKinds = ["client-abort", "unhandled-exception", "timeout-status", "http-status",
        "timeout", "cancellation", "connection-failure", "retry", "retry-pass", "retry-scheduled", "circuit-open", "timeout-or-cancellation", "other-aggregated"];
    internal static readonly string[] GcFields = ["PhysicalBeforeBytes", "PhysicalAfterBytes", "PhysicalDeltaBytes", "VirtualBeforeBytes", "VirtualAfterBytes", "VirtualDeltaBytes",
        "UObjectsBefore", "UObjectsAfter", "UObjectDelta", "ElapsedMilliseconds", "AllocatorBeforeAvailable", "AllocatorAfterAvailable",
        "AllocatorUsedBeforeBytes", "AllocatorUsedAfterBytes", "AllocatorCommittedBeforeBytes", "AllocatorCommittedAfterBytes", "AllocatorRetainedBeforeBytes", "AllocatorRetainedAfterBytes"];
    internal static readonly string[] GcQuantities = ["PhysicalBytes", "VirtualBytes", "UObjectCount", "AllocatorUsedBytes", "AllocatorCommittedBytes", "AllocatorRetainedBytes"];
    private static readonly HashSet<string> NativeIntervals = ["SampleWallSeconds", "HttpCompletedCount", "HttpServerFailureCount",
        "HttpClientErrorCount", "HttpTimeoutCount", "HttpRetryCount"];
    private static readonly HashSet<string> BackendIntervals = ["HTTP.Completed", "HTTP.Canceled", "GC.Gen0Collections",
        "GC.Gen1Collections", "GC.Gen2Collections", "GC.PauseTotalMs"];
    private static readonly HashSet<string> BackendCumulative = ["Cache.Hits", "Cache.Misses", "Cache.Evictions", "Cache.Expirations",
        "Cache.CapacityRejected", "db.client.connections.timeouts", "Circuit.ResultRecovery.Rejected"];
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;
    internal static string GameKey(string key) => "Game | " + key;
    internal static string BackendKey(string key) => "Backend | " + key;
    internal static string Category(string key)
    {
        string raw = key[(key.IndexOf(" | ", StringComparison.Ordinal) + 3)..];
        if (raw.StartsWith("GC", StringComparison.Ordinal)) return "GC·회수·기준선";
        if (raw.StartsWith("Cache.", StringComparison.Ordinal)) return "캐시";
        if (raw.Contains("Circuit", StringComparison.Ordinal) || raw.Contains("서킷", StringComparison.Ordinal)) return "서킷 브레이커";
        if (raw.StartsWith("db.", StringComparison.Ordinal) || raw.StartsWith("DB.", StringComparison.Ordinal)) return "DB 연결 풀";
        if (raw.StartsWith("http.client", StringComparison.Ordinal) || raw.StartsWith("HTTP.Pool", StringComparison.Ordinal)) return "HTTP 연결 풀";
        if (raw.StartsWith("ThreadPool", StringComparison.Ordinal)) return "스레드 풀";
        if (raw.StartsWith("Errors.", StringComparison.Ordinal) || raw.StartsWith("Error detail.", StringComparison.Ordinal)) return "오류·타임아웃·재시도 상세";
        if (raw.StartsWith("HTTP.", StringComparison.Ordinal) || raw.StartsWith("Http", StringComparison.Ordinal) || raw.StartsWith("인증·로비", StringComparison.Ordinal) || raw.StartsWith("kestrel", StringComparison.Ordinal)) return "HTTP 요청·처리량·큐";
        if (raw.StartsWith("Process", StringComparison.Ordinal) || raw.StartsWith("Tcp", StringComparison.Ordinal) || raw.StartsWith("Udp", StringComparison.Ordinal) || raw.StartsWith("Windows", StringComparison.Ordinal)) return "프로세스·스레드·소켓";
        if (raw.StartsWith("UObject", StringComparison.Ordinal) || raw.StartsWith("Managed", StringComparison.Ordinal) || raw.StartsWith("C++", StringComparison.Ordinal)) return "객체·메모리 할당률";
        if (raw.StartsWith("Streaming", StringComparison.Ordinal)) return "텍스처 풀";
        if (raw.StartsWith("Allocator", StringComparison.Ordinal) || raw.StartsWith("Available", StringComparison.Ordinal) || raw.StartsWith("Total system", StringComparison.Ordinal)) return "메모리";
        return "진단 수집 비용·구간";
    }
    internal static IEnumerable<string> KnownKeys => AllocatorKeys.Concat(RuntimeMetricAnalysis.Keys).Concat(SummaryKeys).Select(GameKey)
        .Concat(GcFields.Select(f => GameKey("GC event." + f)))
        .Concat(GcQuantities.Select(f => GameKey("GC net reduction." + f)))
        .Concat(GcQuantities.Select(f => GameKey("GC post-baseline growth." + f)))
        .Concat(MetricLabels.Keys.Select(BackendKey)).Concat(FailureKinds.Select(k => BackendKey("Errors." + k + ".Count")));
    internal static string Label(string key)
    {
        string raw = key[(key.IndexOf(" | ", StringComparison.Ordinal) + 3)..];
        string label = key.StartsWith("Backend | ", StringComparison.Ordinal) ? MetricLabels.Label(raw) : raw switch
        {
            "Allocator used MiB" => "할당자 사용 메모리 (MiB)", "Allocator OS-held MiB" => "할당자 OS 확보 메모리 (MiB)",
            "Allocator retained/slack MiB" => "할당자 여유·보유 차이 (MiB)", "Available system RAM MiB" => "시스템 가용 RAM (MiB)", "Total system RAM MiB" => "시스템 전체 RAM (MiB)",
            "Streaming texture pool %" => "스트리밍 텍스처 풀 사용률 (%)", "ProcessHandleCount" => "프로세스 전체 핸들 수",
            "ProcessThreadCount" => "프로세스 스레드 수", "TcpEndpointCount" => "TCP 엔드포인트 수", "UdpEndpointCount" => "UDP 엔드포인트 수",
            "ProcessCountersAgeSeconds" => "프로세스 측정값 경과 시간 (초)", "UObjectCreationsPerSecond" => "UObject 생성률 (개/초)",
            "SampleWallSeconds" => "실제 표본 구간 (초)", "HttpCompletedCount" => "인증·로비 HTTP 완료 수",
            "HttpServerFailureCount" => "인증·로비 서버·전송 오류 수", "HttpClientErrorCount" => "인증·로비 4xx 수",
            "HttpTimeoutCount" => "인증·로비 타임아웃 수", "HttpRetryCount" => "인증·로비 재시도 예정 수",
            "BackendCircuitState" => "인증·로비 서킷 상태 (0 정상 / 1 차단 / 2 복구 시험)", "BackendCircuitFailures" => "인증·로비 연속 장애 수",
            "BackendCircuitRetryAfterSeconds" => "인증·로비 서킷 대기 (초)", "BackendCircuitRejected" => "인증·로비 차단 누적 수", _ => raw
        };
        if (raw.StartsWith("GC event.", StringComparison.Ordinal)) label = "GC 전후 원본 · " + GcLabel(raw[9..]);
        if (raw.StartsWith("GC net reduction.", StringComparison.Ordinal)) label = "GC당 순감소 · " + GcLabel(raw[17..]);
        if (raw.StartsWith("GC post-baseline growth.", StringComparison.Ordinal)) label = "GC 후 기준선 변화 (3회·30초 이상) · " + GcLabel(raw[24..]);
        if (raw.StartsWith("Errors.", StringComparison.Ordinal)) label = "오류 발생 횟수 · " + raw[7..^6];
        if (raw.StartsWith("Error detail.", StringComparison.Ordinal)) label = "원인·HTTP별 오류 상세 · " + raw[13..];
        return (key.StartsWith("Game | ", StringComparison.Ordinal) ? "게임 · " : "백엔드 · ") + label + " [" + raw + "]";
    }
    private static string GcLabel(string raw) => raw.Replace("Physical", "프로세스 RAM ").Replace("Virtual", "가상 메모리 ")
        .Replace("UObjects", "UObject ").Replace("UObject", "UObject ").Replace("AllocatorUsed", "할당자 사용 ")
        .Replace("AllocatorCommitted", "할당자 OS 확보 ").Replace("AllocatorRetained", "할당자 보유 차이 ")
        .Replace("Before", "이전 ").Replace("After", "이후 ").Replace("Delta", "변화 ").Replace("Bytes", "(바이트)")
        .Replace("Count", "(개)").Replace("ElapsedMilliseconds", "경과 시간 (ms)").Replace("Available", "측정 지원 여부 (0/1)");
    internal static HistoryStatistic Aggregate(IEnumerable<(double? Value, double Weight)> source, string kind = "gauge", bool preferMaximum = false)
    {
        var valid = source.Where(x => x.Value.HasValue && double.IsFinite(x.Value.Value) && double.IsFinite(x.Weight) && x.Weight > 0).ToArray();
        if (valid.Length == 0) return new(null, null, null, null, null, null, 0);
        double last = valid[^1].Value!.Value, max = valid.Max(x => x.Value!.Value), min = valid.Min(x => x.Value!.Value);
        double? mean = kind == "state" ? null : valid.Sum(x => x.Value!.Value * x.Weight) / valid.Sum(x => x.Weight);
        double? total = kind == "interval" ? valid.Sum(x => x.Value!.Value) : null;
        // A cumulative counter's first value predates the capture. Never count it as new work.
        if (kind == "cumulative" && valid.Length > 1)
            total = valid.Zip(valid.Skip(1), (a, b) => b.Value >= a.Value ? b.Value!.Value - a.Value!.Value : b.Value!.Value).Sum();
        double? recommended = kind switch { "state" => last, "interval" => total, "cumulative" => total, _ => preferMaximum ? max : mean };
        return new(mean, max, min, last, total, recommended, valid.Length);
    }
    private static HistoryStatistic Scalar(double? value) => new(null, null, null, null, null, value, value.HasValue ? 1 : 0);
    private static DateTimeOffset Time(string value) => DateTimeOffset.TryParse(value, Invariant, DateTimeStyles.AssumeUniversal, out var t) ? t : DateTimeOffset.MinValue;

    internal static List<DiagnosticHistoryCapture> Game(string runId, string context, IReadOnlyList<PerformanceSample> samples, IReadOnlyList<DiagnosticEvent> events)
    {
        List<DiagnosticHistoryCapture> captures = [];
        foreach (var group in samples.GroupBy(s => (s.Role, s.NetMode)))
        {
            var rows = group.OrderBy(s => s.GameSeconds).ToList();
            var roleEvents = events.Where(e => e.Role == group.Key.Role && e.NetMode == group.Key.NetMode).ToList();
            Dictionary<string, HistoryStatistic> metrics = [];
            void Add(string key, Func<PerformanceSample, double?> get, string kind = "gauge", bool max = false) =>
                metrics[GameKey(key)] = Aggregate(rows.Select(s => (get(s), s.RuntimeCounters.GetValueOrDefault("SampleWallSeconds", 1))), kind, max);
            Add(AllocatorKeys[0], s => s.AllocatorUsedMiB); Add(AllocatorKeys[1], s => s.AllocatorCommittedMiB);
            Add(AllocatorKeys[2], s => s.AllocatorRetainedMiB); Add(AllocatorKeys[3], s => s.AvailablePhysicalMiB);
            Add(AllocatorKeys[4], s => s.TexturePoolUsePercent); Add(AllocatorKeys[5], s => s.TotalPhysicalMiB);
            foreach (string key in RuntimeMetricAnalysis.Keys)
                Add(key, s => s.RuntimeCounters.TryGetValue(key, out double n) ? n : null,
                    NativeIntervals.Contains(key) ? "interval" : key == "BackendCircuitRejected" ? "cumulative" : key == "BackendCircuitState" ? "state" : "gauge",
                    key is "ProcessHandleCount" or "ProcessThreadCount" or "TcpEndpointCount" or "UdpEndpointCount");
            foreach (var row in RuntimeMetricAnalysis.Analyze(rows, roleEvents).Where(r => SummaryKeys.Contains(r.Metric)))
            {
                double? number = row.Number is { } v && double.IsFinite(v) ? v : null;
                // Legacy captures cannot prove that absence of a new GC event means zero GC.
                if (row.Metric.StartsWith("GC", StringComparison.Ordinal) && !rows.Any(s => s.RuntimeCounters.ContainsKey("SampleWallSeconds")) && roleEvents.All(e => e.Code != "GarbageCollectionMemoryDelta")) number = null;
                metrics[GameKey(row.Metric)] = Scalar(number);
            }
            var gc = roleEvents.Where(e => e.Code == "GarbageCollectionMemoryDelta").OrderBy(e => e.GameSeconds).Select(e => Fields(e.Message)).ToList();
            foreach (string field in GcFields.Concat(gc.SelectMany(g => g.Keys)).Distinct())
                metrics[GameKey("GC event." + field)] = Aggregate(gc.Select(g =>
                    ((double?)(g.TryGetValue(field, out double n) && (!field.StartsWith("Allocator", StringComparison.Ordinal) ||
                    !field.EndsWith("Bytes", StringComparison.Ordinal) || g.GetValueOrDefault(field.Contains("Before", StringComparison.Ordinal) ? "AllocatorBeforeAvailable" : "AllocatorAfterAvailable") == 1) ? n : null), 1d)));
            foreach (string prefix in new[] { "Physical", "Virtual", "UObject", "AllocatorUsed", "AllocatorCommitted", "AllocatorRetained" })
            {
                string suffix = prefix == "UObject" ? "Count" : "Bytes";
                string before = prefix == "UObject" ? "UObjectsBefore" : prefix + "Before" + suffix;
                string after = prefix == "UObject" ? "UObjectsAfter" : prefix + "After" + suffix;
                var deltas = gc.Where(g => g.ContainsKey(before) && g.ContainsKey(after))
                    .Where(g => !prefix.StartsWith("Allocator", StringComparison.Ordinal) || g.GetValueOrDefault("AllocatorBeforeAvailable") == 1 && g.GetValueOrDefault("AllocatorAfterAvailable") == 1)
                    .Select(g => ((double?)(g[before] - g[after]), 1d));
                metrics[GameKey("GC net reduction." + prefix + suffix)] = Aggregate(deltas);
                var baselines = roleEvents.Where(e => e.Code == "GarbageCollectionMemoryDelta").OrderBy(e => e.GameSeconds)
                    .Select(e => (e.GameSeconds, Values: Fields(e.Message))).Where(g => g.Values.ContainsKey(after))
                    .Where(g => !prefix.StartsWith("Allocator", StringComparison.Ordinal) || g.Values.GetValueOrDefault("AllocatorAfterAvailable") == 1).ToArray();
                metrics[GameKey("GC post-baseline growth." + prefix + suffix)] = Scalar(baselines.Length >= 3 && baselines[^1].GameSeconds - baselines[0].GameSeconds >= 30
                    ? baselines[^1].Values[after] - baselines[0].Values[after] : null);
            }
            var findings = CacheMemoryAnalysis.Analyze(rows, roleEvents);
            captures.Add(new("Game/" + runId + "/" + group.Key.Role + "/" + group.Key.NetMode, "Game", group.Key.Role + " / " + group.Key.NetMode,
                rows.Select(s => Time(s.UtcTime)).Min(), rows.Select(s => Time(s.UtcTime)).Max(), rows.Count, context, metrics,
                findings.Select(f => f.Category + ": " + f.Status + " / " + f.Code + " / " + f.Evidence + " / " + f.Interpretation).ToArray()));
        }
        return captures;
    }
    private static Dictionary<string, double> Fields(string text)
    {
        Dictionary<string, double> result = [];
        foreach (string part in text.Split(';'))
        {
            var pair = part.Split('=', 2);
            if (pair.Length == 2 && double.TryParse(pair[1], NumberStyles.Float, Invariant, out double value) && double.IsFinite(value)) result[pair[0].Trim()] = value;
        }
        return result;
    }

    internal static List<DiagnosticHistoryCapture> Backend(IEnumerable<BackendMetricSnapshot> source, string context)
    {
        List<DiagnosticHistoryCapture> captures = [];
        foreach (var group in source.GroupBy(s => (s.CaptureId, s.ProcessId)))
        {
            var rows = group.DistinctBy(s => s.UtcTime).OrderBy(s => s.UtcTime).ToArray();
            Dictionary<string, HistoryStatistic> metrics = [];
            foreach (string key in MetricLabels.Keys.Concat(rows.SelectMany(s => s.Values.Keys)).Distinct())
            {
                string kind = BackendIntervals.Contains(key) ? "interval" : BackendCumulative.Contains(key) ? "cumulative" : key == "Circuit.ResultRecovery.State" ? "state" : "gauge";
                var value = Aggregate(rows.Select(s => (s.Values.GetValueOrDefault(key), s.IntervalSeconds)), kind);
                // Error percentages are weighted by completed attempts, not interval duration.
                if (key is "HTTP.ServerErrorPercent" or "HTTP.ClientErrorPercent" or "HTTP.MeanDurationMs")
                    value = Aggregate(rows.Select(s => (s.Values.GetValueOrDefault(key), s.Values.GetValueOrDefault("HTTP.Completed") ?? 0)));
                // These source values are lifetime cumulative ratios/means. Their last observation
                // is meaningful; time-averaging them is not a new per-lookup average for this capture.
                if (key is "Cache.HitPercent" or "Cache.MissPercent" or "Cache.LookupMeanMs") value = value with { Recommended = value.Last };
                metrics[BackendKey(key)] = value;
            }
            foreach (string kind in FailureKinds.Concat(rows.SelectMany(r => r.Failures.Select(f => f.Kind))).Distinct())
                metrics[BackendKey("Errors." + kind + ".Count")] = Aggregate(rows.Select(s => ((double?)s.Failures.Where(f => f.Kind == kind).Sum(f => (double)f.Count), s.IntervalSeconds)), "interval");
            var errors = rows.SelectMany(s => s.Failures).GroupBy(f => (f.Source, f.Kind, f.Status));
            foreach (var error in errors)
            {
                if (metrics.Count > 1020) throw new InvalidDataException("오류 상세 조합이 너무 많습니다. 기록을 더 짧은 구간으로 나눠 가져오세요. 일부 항목을 숨겨 반영하지 않았습니다.");
                string prefix = "Error detail." + error.Key.Source + "." + error.Key.Kind + ".HTTP" + error.Key.Status;
                metrics[BackendKey(prefix + ".Count")] = Aggregate(error.Select(f => ((double?)f.Count, 1d)), "interval");
                metrics[BackendKey(prefix + ".Attempt")] = Aggregate(error.Select(f => ((double?)f.Attempt, 1d)), preferMaximum: true);
                metrics[BackendKey(prefix + ".Maximum")] = Aggregate(error.Select(f => ((double?)f.Maximum, 1d)), preferMaximum: true);
                metrics[BackendKey(prefix + ".NextDelaySeconds")] = Aggregate(error.Select(f => (f.NextDelaySeconds, 1d)), preferMaximum: true);
            }
            captures.Add(new("Backend/" + group.Key.CaptureId + "/" + group.Key.ProcessId, "Backend", "Backend", rows[0].UtcTime, rows[^1].UtcTime, rows.Length,
                context + " · PID " + group.Key.ProcessId + " · " + group.Key.CaptureId,
                metrics, rows[^1].States.Select(p => MetricLabels.State(p.Key, p.Value)).Append(
                    "가져온 구간만 집계합니다. 순환 파일에서 이미 덮어쓴 표본은 복원되지 않습니다. 누적 카운터의 첫 값은 새 발생 수에 포함하지 않습니다.").Append(
                    "권장: HTTP 오류율·평균 시간은 완료 요청 수 가중, 캐시 누적 비율·평균 조회 시간은 마지막 값, 기타 게이지·구간 평균은 관측 시간 가중입니다. 풀 대기 시간은 구간 평균의 시간 가중값이며 전체 요청별 정확한 평균은 아닙니다.").ToArray()));
        }
        return captures;
    }
}

internal sealed record DiagnosticHistoryArchive(int Schema, DiagnosticHistoryCapture[] Captures);
internal static class DiagnosticHistoryFiles
{
    internal const int MaximumCaptures = 2048;
    internal static List<DiagnosticHistoryCapture> ReadBackend(IEnumerable<string> paths)
    {
        string[] files = paths.ToArray();
        if (files.Length > 64) throw new InvalidDataException("한 번에 최대 64개의 원본 파일을 선택하세요.");
        List<BackendMetricSnapshot> records = [];
        foreach (string file in files)
        {
            if (new FileInfo(file).Length > 4 * 1024 * 1024) throw new InvalidDataException("원본 파일은 각각 최대 4 MiB입니다.");
            using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var reader = new StreamReader(stream);
            while (ReadBoundedLine(reader) is { } line)
            {
                if (BackendMetricFiles.Parse(line) is not { } record) continue;
                if (records.Count >= 8192) throw new InvalidDataException("8,192표본을 초과했습니다. 파일을 나눠 가져오세요. 일부만 반영하지 않았습니다.");
                records.Add(record);
            }
        }
        return LongTermDiagnostics.Backend(records, "원본 기록 가져오기");
    }
    // Avoid allocating arbitrarily long lines from a malformed local file.
    private static string? ReadBoundedLine(StreamReader reader)
    {
        var line = new System.Text.StringBuilder();
        int c;
        while ((c = reader.Read()) >= 0 && c != '\n')
        {
            if (line.Length >= 65536) throw new InvalidDataException("진단 기록 한 행이 64 KiB를 초과합니다.");
            line.Append((char)c);
        }
        return c < 0 && line.Length == 0 ? null : line.ToString().TrimEnd('\r');
    }
    internal static void Save(string path, IReadOnlyCollection<DiagnosticHistoryCapture> captures)
    {
        Validate(captures);
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(new DiagnosticHistoryArchive(1, captures.ToArray()));
        if (bytes.Length > 32 * 1024 * 1024) throw new InvalidDataException("장기 비교 요약이 32 MiB를 초과합니다. 나눠 보관하세요.");
        // Stage beside the chosen file; a failed write never truncates the previous archive.
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try { File.WriteAllBytes(temporary, bytes); File.Move(temporary, path, true); }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    internal static List<DiagnosticHistoryCapture> Load(string path)
    {
        if (new FileInfo(path).Length > 32 * 1024 * 1024) throw new InvalidDataException("장기 비교 요약은 최대 32 MiB입니다.");
        using var stream = File.OpenRead(path);
        var archive = JsonSerializer.Deserialize<DiagnosticHistoryArchive>(stream);
        if (archive?.Schema != 1 || archive.Captures is null) throw new InvalidDataException("지원하지 않는 장기 비교 파일입니다.");
        Validate(archive.Captures); return archive.Captures.ToList();
    }
    private static void Validate(IReadOnlyCollection<DiagnosticHistoryCapture> captures)
    {
        if (captures.Count > MaximumCaptures) throw new InvalidDataException("장기 비교는 최대 2,048개의 역할별 캡처입니다.");
        foreach (var capture in captures)
        {
            if (capture is null || capture.Id is null || capture.Id.Length > 1024 || capture.Source is not ("Game" or "Backend") ||
                capture.Scope is null || capture.Scope.Length > 512 || capture.Context is null || capture.Context.Length > 4096 ||
                capture.Samples < 0 || capture.End < capture.Start || capture.Metrics is null || capture.Metrics.Count > 1024 ||
                capture.Notes is null || capture.Notes.Length > 1024 || capture.Notes.Any(n => n is null || n.Length > 8192))
                throw new InvalidDataException("장기 비교 캡처 구조가 올바르지 않습니다.");
            foreach (var pair in capture.Metrics)
                if (pair.Key.Length > 512 || pair.Value is null || pair.Value.ValidSamples < 0 ||
                    new[] { pair.Value.Average, pair.Value.Maximum, pair.Value.Minimum, pair.Value.Last, pair.Value.Total, pair.Value.Recommended }.Any(v => v.HasValue && !double.IsFinite(v.Value)))
                    throw new InvalidDataException("장기 비교 지표가 올바르지 않습니다.");
        }
    }
}
