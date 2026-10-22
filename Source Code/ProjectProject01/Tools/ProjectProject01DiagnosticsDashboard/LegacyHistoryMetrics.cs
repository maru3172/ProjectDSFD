namespace ProjectProject01DiagnosticsDashboard;

internal static class LegacyHistoryMetrics
{
    internal static readonly string[] Keys = [
        "Total risk candidates", "Risk: Performance", "Risk: Memory", "Risk: Network", "Risk: AI/Navigation", "Risk: Tuning/Data", "Risk: Multiplayer", "Risk: Engine/Logs", "Risk: Other",
        "Average FPS", "Max frame ms", "Max game thread ms", "Max draw thread ms", "Max GPU ms",
        "Max physical memory MiB", "Max virtual memory MiB", "Max memory growth MiB/min", "Max UObject count", "Max Actor count",
        "Max streaming texture MiB", "Max non-streaming texture MiB", "Max texture pool MiB", "Max inbound KiB/s", "Max outbound KiB/s",
        "Max AI tick ms", "Max line traces", "Max path failures", "Max connections", "Average diagnostics overhead ms", "Max diagnostics overhead ms",
        "Max diagnostics Actor/AI overhead ms", "Max diagnostics navigation overhead ms", "Max diagnostics network overhead ms",
        "Max diagnostics frame timing overhead ms", "Max diagnostics memory overhead ms", "Max diagnostics texture/RHI overhead ms",
        "Max diagnostics bookkeeping overhead ms", "Max diagnostics buffered memory MiB", "Max server-client timestamp offset ms"];
    internal static bool Contains(string raw) => Keys.Contains(raw) || raw.StartsWith("Risk code: ", StringComparison.Ordinal);
    internal static DiagnosticHistoryCapture Capture(RunSummary summary)
    {
        var metrics = Keys.Concat(summary.RiskByCode.Keys.Select(k => "Risk code: " + k)).ToDictionary(LongTermDiagnostics.GameKey, key =>
        {
            double value = DashboardForm.HistoryValue(summary, key);
            bool available = double.IsFinite(value) && (summary.Samples > 0 || key.StartsWith("Risk", StringComparison.Ordinal) || key == "Total risk candidates");
            return new HistoryStatistic(null, null, null, null, null, available ? value : null, available ? 1 : 0);
        });
        var starts = summary.ExtendedCaptures.Select(c => c.Start).Where(t => t != DateTimeOffset.MinValue).ToArray();
        var ends = summary.ExtendedCaptures.Select(c => c.End).Where(t => t != DateTimeOffset.MinValue).ToArray();
        return new("Game/" + summary.TestRunId + "/Combined", "Game", "전체 경기", starts.Length > 0 ? starts.Min() : DateTimeOffset.MinValue,
            ends.Length > 0 ? ends.Max() : DateTimeOffset.MinValue, summary.Samples,
            $"{summary.TestRunId} · {summary.Map} · 플레이어 {summary.Players} · 마네킹 {summary.Mannequins} · 튜닝 {summary.Tuning}", metrics,
            ["기존 성능·위험 비교의 동일한 집계값입니다. 권장 집계에서 확인하세요. 역할별 공유 프로세스 수치를 합산하지 않습니다."]);
    }
}
