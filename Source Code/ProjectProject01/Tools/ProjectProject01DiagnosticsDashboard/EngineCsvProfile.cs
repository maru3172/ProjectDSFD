// File: Tools/ProjectProject01DiagnosticsDashboard/EngineCsvProfile.cs
// Target: Win64 .NET 8 Windows desktop companion for Unreal Engine 5.8.2 diagnostic artifacts
// Reads Unreal Engine CSV Profiler captures (CsvProfile Start/Stop -> Saved/Profiling/CSV/*.csv) offline.
// The file is streamed once and reduced to fixed-size statistics; only five key frame-time series are retained.

using System.Globalization;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed record EngineCsvStat(string Name, int Samples, double Mean, double Max);
internal sealed record EngineCsvKeySeries(string Name, int Samples, double Mean, double Median, double P95, double Max);
internal sealed record EngineCsvEvent(int Frame, string Name, double? Seconds);
internal sealed record EngineCsvFileRow(string 파일, string 크기MiB, string 수정시각, string FullPath);
internal sealed record EngineCsvRow(string 구분, string 항목, string 표본, string 평균, string 최대);

internal sealed class EngineCsvProfileResult
{
    public required string FilePath { get; init; }
    public required long Bytes { get; init; }
    public required DateTime WriteUtc { get; init; }
    public required int Frames { get; init; }
    public required bool Truncated { get; init; }
    public required IReadOnlyDictionary<string, string> Metadata { get; init; }
    public required IReadOnlyList<EngineCsvKeySeries> Key { get; init; }
    public required IReadOnlyList<EngineCsvStat> Stats { get; init; }
    public required IReadOnlyList<EngineCsvEvent> Events { get; init; }
    public required IReadOnlyDictionary<string, float[]> KeyFrames { get; init; }
    public double? CaptureSeconds => Number("captureduration");
    public DateTimeOffset? StartUtc => Unix("starttimestamp");
    public DateTimeOffset? EndUtc => Unix("endtimestamp");
    public bool HasGpuBreakdown => Stats.Any(s => EngineCsvProfile.IsGpuPass(s.Name));
    public EngineCsvKeySeries? KeyOf(string name) => Key.FirstOrDefault(k => k.Name == name);
    public double MeanOf(string name) => Stats.FirstOrDefault(s => s.Name == name)?.Mean ?? 0;

    private double? Number(string key) =>
        Metadata.TryGetValue(key, out var text) && double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value) && double.IsFinite(value) ? value : null;
    private DateTimeOffset? Unix(string key) =>
        Number(key) is { } seconds && seconds > 0 && seconds < 32503680000 ? DateTimeOffset.FromUnixTimeMilliseconds((long)(seconds * 1000)) : null;
}

internal static class EngineCsvProfile
{
    internal static readonly string[] KeyColumns = ["FrameTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime", "GPUTime"];
    internal const long MaxFileBytes = 1024L * 1024 * 1024;
    internal const int MaxColumns = 8192;
    internal const int MaxEvents = 2000;
    // Never shown: the engine records the full command line (may include tokens/paths) and an account-derived login id.
    private static readonly HashSet<string> HiddenMetadata = new(StringComparer.OrdinalIgnoreCase) { "commandline", "loginid" };

    internal static EngineCsvProfileResult Load(string path, CancellationToken cancellation, int maxFrames = 500_000)
    {
        var info = new FileInfo(path);
        if (!info.Exists) throw new FileNotFoundException("CSV 프로파일 파일이 없습니다.", path);
        if (info.Length > MaxFileBytes) throw new InvalidDataException("CSV 프로파일은 1 GiB 이하만 읽습니다.");
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 16, FileOptions.SequentialScan);
        using var reader = new StreamReader(stream, detectEncodingFromByteOrderMarks: true);
        var firstLine = reader.ReadLine() ?? throw new InvalidDataException("빈 파일입니다.");
        var header = DashboardForm.ParseCsvLine(firstLine);
        if (header.Count < 2 || header[0] != "EVENTS" || header.Count > MaxColumns)
            throw new InvalidDataException("언리얼 CSV 프로파일러 형식(첫 열 EVENTS)이 아닙니다.");

        var sum = new double[header.Count]; var max = new double[header.Count]; var count = new int[header.Count];
        Array.Fill(max, double.NegativeInfinity);
        var keyValues = KeyColumns.ToDictionary(k => k, _ => new List<float>());
        var keyByColumn = KeyColumns.Where(k => header.IndexOf(k) > 0).ToDictionary(k => header.IndexOf(k), k => keyValues[k]);
        List<string>? finalHeader = null;
        var metadata = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var events = new List<EngineCsvEvent>();
        int frames = 0; bool truncated = false;

        void Accumulate(int column, ReadOnlySpan<char> cell)
        {
            if (column == 0) { if (!cell.IsEmpty) ReadEvents(cell, frames, events); return; }
            if (column >= MaxColumns || !double.TryParse(cell, NumberStyles.Float, CultureInfo.InvariantCulture, out var value) || !double.IsFinite(value)) return;
            if (column >= sum.Length) Grow(ref sum, ref max, ref count, column + 1);
            sum[column] += value; ++count[column]; if (value > max[column]) max[column] = value;
            if (keyByColumn.TryGetValue(column, out var series)) series.Add((float)value);
        }

        while (reader.ReadLine() is { } line)
        {
            if (((frames + 1) & 1023) == 0) cancellation.ThrowIfCancellationRequested();
            if (line.Length == 0) continue;
            // Metadata is written after the final header row; a data row's first cell (event text) may also start with '['.
            if (line[0] == '[' && (finalHeader is not null || line.StartsWith("[HasHeaderRowAtEnd]", StringComparison.Ordinal))) { ReadMetadata(line, metadata); continue; }
            if (line.StartsWith("EVENTS,", StringComparison.Ordinal))
            {
                var candidate = DashboardForm.ParseCsvLine(line);
                if (candidate.Count <= MaxColumns) finalHeader = candidate;
                continue;
            }
            if (frames >= maxFrames) { truncated = true; continue; }

            if (line.Contains('"'))
            {
                var cells = DashboardForm.ParseCsvLine(line);
                for (int c = 0; c < cells.Count; ++c) Accumulate(c, cells[c].AsSpan());
            }
            else
            {
                for (int position = 0, column = 0, start = 0; position <= line.Length; ++position)
                {
                    if (position < line.Length && line[position] != ',') continue;
                    Accumulate(column++, line.AsSpan(start, position - start));
                    start = position + 1;
                }
            }
            ++frames;
        }

        var names = finalHeader ?? header;
        var stats = new List<EngineCsvStat>();
        for (int i = 1; i < sum.Length; ++i)
            if (count[i] > 0) stats.Add(new EngineCsvStat(i < names.Count ? names[i] : $"Column{i}", count[i], sum[i] / count[i], max[i]));
        var key = KeyColumns.Where(k => keyValues[k].Count > 0).Select(k => Describe(k, keyValues[k])).ToList();
        foreach (var hidden in HiddenMetadata) metadata.Remove(hidden);
        return new EngineCsvProfileResult
        {
            FilePath = info.FullName, Bytes = info.Length, WriteUtc = info.LastWriteTimeUtc, Frames = frames, Truncated = truncated,
            Metadata = metadata, Key = key, Stats = stats, Events = events,
            KeyFrames = keyValues.Where(p => p.Value.Count > 0).ToDictionary(p => p.Key, p => p.Value.ToArray())
        };
    }

    private static void Grow(ref double[] sum, ref double[] max, ref int[] count, int size)
    {
        int old = sum.Length; size = Math.Max(size, old * 2); size = Math.Min(size, MaxColumns);
        Array.Resize(ref sum, size); Array.Resize(ref max, size); Array.Resize(ref count, size);
        for (int i = old; i < size; ++i) max[i] = double.NegativeInfinity;
    }

    private static void ReadEvents(ReadOnlySpan<char> cell, int frame, List<EngineCsvEvent> events)
    {
        // Multiple events on a frame are ';' separated (the engine sanitizes ';' inside event text). Timestamp suffix: "##<seconds>".
        while (!cell.IsEmpty)
        {
            if (events.Count >= MaxEvents) return;
            int separator = cell.IndexOf(';');
            var part = separator < 0 ? cell : cell[..separator];
            cell = separator < 0 ? ReadOnlySpan<char>.Empty : cell[(separator + 1)..];
            if (part.IsEmpty) continue;
            int hash = part.LastIndexOf("##");
            double? seconds = null;
            if (hash >= 0 && double.TryParse(part[(hash + 2)..], NumberStyles.Float, CultureInfo.InvariantCulture, out var s) && double.IsFinite(s)) { seconds = s; part = part[..hash]; }
            var name = part.Length > 200 ? part[..200].ToString() : part.ToString();
            events.Add(new EngineCsvEvent(frame, name, seconds));
        }
    }

    private static void ReadMetadata(string line, Dictionary<string, string> metadata)
    {
        var cells = DashboardForm.ParseCsvLine(line);
        for (int i = 0; i + 1 < cells.Count && metadata.Count < 256; i += 2)
        {
            var key = cells[i].Trim();
            if (key.Length < 3 || key[0] != '[' || key[^1] != ']') continue;
            var value = cells[i + 1];
            metadata[key[1..^1]] = value.Length > 512 ? value[..512] : value;
        }
    }

    internal static EngineCsvKeySeries Describe(string name, List<float> values)
    {
        var sorted = values.ToArray(); Array.Sort(sorted);
        double Percentile(double p) => sorted[Math.Clamp((int)Math.Ceiling(p * sorted.Length) - 1, 0, sorted.Length - 1)];
        return new EngineCsvKeySeries(name, sorted.Length, sorted.Average(v => (double)v), Percentile(0.5), Percentile(0.95), sorted[^1]);
    }

    // Heuristic split of where frame time goes. Thread work is the sum of that thread's exclusive timers minus
    // EventWait timers, so a thread that is only waiting on the GPU is not blamed. (In editor captures the raw
    // GameThreadTime/RenderThreadTime columns do not line up with the exclusive timers, so they are shown separately.)
    internal static (string Verdict, List<EngineCsvRow> Rows) Bottleneck(EngineCsvProfileResult result)
    {
        double? Work(string prefix)
        {
            var timers = result.Stats.Where(s => s.Name.StartsWith(prefix, StringComparison.Ordinal) && !s.Name.Contains("/EventWait", StringComparison.Ordinal)
                && !s.Name.StartsWith(prefix + "STAT_", StringComparison.Ordinal)).ToList();
            return timers.Count == 0 ? null : timers.Sum(s => s.Mean);
        }
        double? Mean(string name) => result.KeyOf(name) is { Mean: > 0 } k ? k.Mean : null;
        var candidates = new List<(string Name, double? Ms, string Note)>
        {
            ("게임 스레드 실작업", Work("Exclusive/GameThread/"), "게임 스레드 Exclusive 합 − EventWait"),
            ("렌더 스레드 실작업", Work("Exclusive/RenderThread/"), "렌더 스레드 Exclusive 합 − EventWait"),
            ("RHI 스레드", Mean("RHIThreadTime"), "RHIThreadTime"),
            ("GPU", Mean("GPUTime"), "GPUTime"),
        };
        var rows = candidates.Select(c => new EngineCsvRow("병목 추정", c.Name, c.Ms is null ? "-" : result.Frames.ToString(CultureInfo.InvariantCulture),
            c.Ms is { } ms ? ms.ToString("F2", CultureInfo.InvariantCulture) : "측정값 없음", c.Note)).ToList();
        var best = candidates.Where(c => c.Ms is not null).OrderByDescending(c => c.Ms).FirstOrDefault();
        var frame = result.KeyOf("FrameTime");
        string verdict = best.Ms is not { } top ? "판정 불가 (핵심 시간 열 없음)"
            : frame is null ? $"{best.Name} 우세 추정 ({top:F2} ms)"
            : $"{best.Name} 우세 추정: 평균 {top:F2} ms / 프레임 평균 {frame.Mean:F2} ms ({1000 / Math.Max(frame.Mean, 0.001):F1} FPS)" +
              (frame.Mean > top * 1.5 ? " · 프레임 시간의 대부분이 측정된 작업 밖입니다(창 비활성 FPS 제한·vsync·고정 프레임 가능성). 창을 앞에 두고 다시 측정하세요." : "");
        return (verdict, rows);
    }

    internal static readonly (string Label, Func<EngineCsvStat, bool> Match, bool IsTime)[] Groups =
    [
        ("게임 스레드 (대기 제외)", s => s.Name.StartsWith("Exclusive/GameThread/", StringComparison.Ordinal) && !s.Name.StartsWith("Exclusive/GameThread/EventWait", StringComparison.Ordinal), true),
        ("렌더 스레드 (대기 제외)", s => s.Name.StartsWith("Exclusive/RenderThread/", StringComparison.Ordinal) && !s.Name.StartsWith("Exclusive/RenderThread/EventWait", StringComparison.Ordinal) && !s.Name.StartsWith("Exclusive/RenderThread/STAT_", StringComparison.Ordinal), true),
        ("워커 스레드", s => s.Name.StartsWith("Exclusive/AllWorkers/", StringComparison.Ordinal), true),
        ("GPU 패스", s => IsGpuPass(s.Name), true),
        ("스레드 대기", s => s.Name.Contains("/EventWait", StringComparison.Ordinal), true),
        ("틱 수", s => s.Name.StartsWith("Ticks/", StringComparison.Ordinal), false),
        ("드로우·메모리·기타", s => !s.Name.StartsWith("Exclusive/", StringComparison.Ordinal) && !IsGpuPass(s.Name) && !s.Name.StartsWith("Ticks/", StringComparison.Ordinal), false),
    ];

    // r.GPUCsvStatsEnabled 1 writes per-pass GPU time as "GPU/<stat>" (GPU2/, GPU3/... on multi-GPU), graphics queue 0,
    // exclusive busy+wait milliseconds (RHI GPUProfiler.cpp). "GPUMem/", "GPUSceneInstanceCount" etc. are not passes.
    internal static bool IsGpuPass(string name)
    {
        if (!name.StartsWith("GPU", StringComparison.Ordinal)) return false;
        int i = 3;
        while (i < name.Length && char.IsAsciiDigit(name[i])) ++i;
        return i < name.Length && name[i] == '/' && i + 1 < name.Length && (i == 3 || name[3] != '0');
    }

    // Largest GPU passes with their share of GPUTime. Pass timers are exclusive and come from one queue, so the
    // shares need not add up to 100% of GPUTime (async compute, idle gaps and unlabelled work are outside them).
    internal static List<(string Name, double Mean, double Max, double? ShareOfGpuTime)> TopGpuPasses(EngineCsvProfileResult result, int limit = 10)
    {
        double? gpu = result.KeyOf("GPUTime") is { Mean: > 0 } k ? k.Mean : null;
        return result.Stats.Where(s => IsGpuPass(s.Name)).OrderByDescending(s => s.Mean).Take(limit)
            .Select(s => (s.Name, s.Mean, s.Max, gpu is { } total ? s.Mean / total * 100 : (double?)null)).ToList();
    }

    internal static List<EngineCsvRow> Top(EngineCsvProfileResult result, int groupIndex, int limit = 40)
    {
        var group = Groups[Math.Clamp(groupIndex, 0, Groups.Length - 1)];
        return result.Stats.Where(group.Match).OrderByDescending(s => s.Mean).Take(limit)
            .Select(s => new EngineCsvRow(group.Label, s.Name, s.Samples.ToString(CultureInfo.InvariantCulture),
                s.Mean.ToString(group.IsTime ? "F3" : "F1", CultureInfo.InvariantCulture), s.Max.ToString(group.IsTime ? "F3" : "F1", CultureInfo.InvariantCulture))).ToList();
    }

    // Runs whose sample window overlaps the CSV capture window (inclusive, with slack for timer granularity).
    internal static List<string> OverlappingRuns(EngineCsvProfileResult result, IEnumerable<(string RunId, DateTimeOffset First, DateTimeOffset Last)> runs)
    {
        if (result.StartUtc is not { } start) return [];
        var end = result.EndUtc ?? (result.CaptureSeconds is { } seconds ? start.AddSeconds(seconds) : start);
        var slack = TimeSpan.FromSeconds(5);
        return runs.Where(r => r.First <= end + slack && r.Last >= start - slack).Select(r => r.RunId).Distinct().ToList();
    }

    internal static string Summary(EngineCsvProfileResult result, IReadOnlyList<string> linkedRuns)
    {
        var lines = new List<string>
        {
            $"파일: {Path.GetFileName(result.FilePath)}  ({result.Bytes / 1048576.0:F1} MiB)",
            $"프레임: {result.Frames}{(result.Truncated ? " (상한 도달, 이후 프레임은 생략)" : "")}  · 캡처 길이: {(result.CaptureSeconds is { } c ? c.ToString("F1", CultureInfo.InvariantCulture) + " 초" : "알 수 없음")}" +
                (result.CaptureSeconds is { } d && d > 0 ? $"  · 평균 {result.Frames / d:F1} FPS" : ""),
            result.Metadata.Count == 0 ? "메타데이터 없음: 캡처가 정상 종료(CsvProfile Stop)되지 않아 파일이 중간에 끊겼을 수 있습니다." : "메타데이터: 정상 종료됨",
            $"시작/종료(로컬): {result.StartUtc?.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss") ?? "-"} / {result.EndUtc?.ToLocalTime().ToString("HH:mm:ss") ?? "-"}",
            $"빌드: {Meta(result, "engineversion")} · {Meta(result, "config")} · {Meta(result, "platform")}",
            $"CPU: {Meta(result, "cpu")}  · 해상도: {Meta(result, "systemresolution.resx")}x{Meta(result, "systemresolution.resy")}",
            "",
            "핵심 시간 (밀리초)            평균      중앙값     p95       최대"
        };
        foreach (var k in result.Key)
            lines.Add($"  {k.Name,-24}{k.Mean,9:F2}{k.Median,10:F2}{k.P95,10:F2}{k.Max,10:F2}");
        lines.Add("");
        lines.Add("판정: " + Bottleneck(result).Verdict);
        if (result.HasGpuBreakdown)
        {
            var passes = TopGpuPasses(result);
            lines.Add($"GPU 패스별 시간 (상위 {passes.Count}개, 단독 시간·그래픽 큐 기준, 평균 ms / 최대 ms / GPUTime 대비):");
            foreach (var pass in passes)
                lines.Add($"  {pass.Name,-40}{pass.Mean,9:F2}{pass.Max,10:F2}{(pass.ShareOfGpuTime is { } share ? $"{share,9:F1}%" : "        -")}");
            lines.Add("  (비율 합이 100%가 아닐 수 있음: 비동기 컴퓨트·유휴·이름 없는 작업은 패스에 포함되지 않음)");
        }
        else lines.Add("GPU 패스별 시간: 없음 — 캡처 전 r.GPUCsvStatsEnabled 1 (또는 실행 인자 -csvGpuStats) 필요");
        lines.Add(linkedRuns.Count > 0 ? "같은 시간대 진단 실행: " + string.Join(", ", linkedRuns) + "  (위 실행 목록에서 선택하면 .utrace·대시보드 기록 확인)" : "같은 시간대 진단 실행: 없음 (프로젝트 진단 캡처를 함께 켜면 자동 연결)");
        var gc = result.Events.Count(e => e.Name.Equals("GC", StringComparison.Ordinal));
        lines.Add($"이벤트: {result.Events.Count}개{(result.Events.Count >= MaxEvents ? " (상한)" : "")}, 그중 GC {gc}회");
        lines.Add("");
        lines.Add("주의: 에디터 PIE 캡처는 에디터 UI·여러 PIE 뷰포트·서버가 한 프로세스에 함께 들어갑니다. 패키지 빌드와 수치가 다릅니다.");
        lines.Add("평균은 해당 열이 기록된 프레임 기준이며, 이 화면은 파일을 한 번 읽은 요약만 보관합니다(원본 미수정).");
        return string.Join(Environment.NewLine, lines);
    }

    private static string Meta(EngineCsvProfileResult result, string key) => result.Metadata.TryGetValue(key, out var v) ? v.Trim() : "-";

    internal const string RunCaptureFileName = "EngineCsvProfile.csv";

    // CSVs written by the project's diagnostics capture: <diagnosticsRoot>/<RunId>/EngineCsvProfile.csv (one level only).
    internal static List<EngineCsvFileRow> ListRunCsvFiles(string diagnosticsRoot, int limit = 200)
    {
        if (!Directory.Exists(diagnosticsRoot)) return [];
        var options = new EnumerationOptions { RecurseSubdirectories = false, AttributesToSkip = FileAttributes.ReparsePoint | FileAttributes.Hidden, IgnoreInaccessible = true };
        return new DirectoryInfo(diagnosticsRoot).EnumerateDirectories("*", options)
            .OrderByDescending(d => d.LastWriteTimeUtc).Take(500)
            .Select(d => new FileInfo(Path.Combine(d.FullName, RunCaptureFileName)))
            .Where(f => f.Exists && !f.Attributes.HasFlag(FileAttributes.ReparsePoint))
            .OrderByDescending(f => f.LastWriteTimeUtc).Take(limit)
            .Select(f => new EngineCsvFileRow($"{f.Directory!.Name}/{f.Name}", (f.Length / 1048576.0).ToString("F1", CultureInfo.InvariantCulture), f.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"), f.FullName)).ToList();
    }

    // Run id when the file is a diagnostics-run capture, so it links to that run even without sample timestamps.
    internal static string? RunIdFromPath(string filePath, string? diagnosticsRoot)
    {
        if (string.IsNullOrEmpty(diagnosticsRoot) || !string.Equals(Path.GetFileName(filePath), RunCaptureFileName, StringComparison.OrdinalIgnoreCase)) return null;
        var runDirectory = Path.GetDirectoryName(Path.GetFullPath(filePath));
        var parent = runDirectory is null ? null : Path.GetDirectoryName(runDirectory);
        return parent is not null && string.Equals(Path.GetFullPath(parent).TrimEnd(Path.DirectorySeparatorChar), Path.GetFullPath(diagnosticsRoot).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase)
            ? Path.GetFileName(runDirectory) : null;
    }

    internal static List<EngineCsvFileRow> ListCsvFiles(string directory, int limit = 200)
    {
        if (!Directory.Exists(directory)) return [];
        var options = new EnumerationOptions { RecurseSubdirectories = false, AttributesToSkip = FileAttributes.ReparsePoint | FileAttributes.Hidden, IgnoreInaccessible = true };
        return new DirectoryInfo(directory).EnumerateFiles("*.csv", options)
            .OrderByDescending(f => f.LastWriteTimeUtc).Take(limit)
            .Select(f => new EngineCsvFileRow(f.Name, (f.Length / 1048576.0).ToString("F1", CultureInfo.InvariantCulture), f.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"), f.FullName)).ToList();
    }

    // Long captures can hold hundreds of thousands of frames. Keep each bucket's peak so spikes stay visible
    // while the chart input stays bounded (TrendChart reduces further to 800 points per series).
    internal static IEnumerable<ChartPoint> BucketPeaks(string series, float[] values, int buckets = 2000)
    {
        if (values.Length == 0) yield break;
        int size = Math.Max(1, (int)Math.Ceiling(values.Length / (double)buckets));
        for (int start = 0; start < values.Length; start += size)
        {
            int peak = start, end = Math.Min(values.Length, start + size);
            for (int i = start + 1; i < end; ++i) if (values[i] > values[peak]) peak = i;
            yield return new ChartPoint(series, peak, values[peak]);
        }
    }
}
