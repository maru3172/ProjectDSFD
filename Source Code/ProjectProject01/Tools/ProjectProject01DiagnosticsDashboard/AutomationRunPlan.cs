namespace ProjectProject01DiagnosticsDashboard;

internal sealed record AutomationRunPlan(string Name, string Filter, TimeSpan Timeout, bool Simulation)
{
    internal static AutomationRunPlan General => new("일반 자동 검사", "Group:ProjectDFSDGeneral", TimeSpan.FromMinutes(30), false);
    internal static readonly string[] Scenarios = Enumerable.Range(1, 5).Select(i => $"Level{i}")
        .Concat(Enumerable.Range(1, 5).Select(i => $"Ladder.M{i}S3"))
        .Concat(Enumerable.Range(1, 5).Select(i => $"Ladder.H3S{i}")).ToArray();

    internal static AutomationRunPlan Bot(string scenario, int first, int count)
    {
        if (!Scenarios.Contains(scenario, StringComparer.Ordinal)) throw new ArgumentException("알 수 없는 경기 조합입니다.");
        int maximum = scenario.StartsWith("Level", StringComparison.Ordinal) ? 20 : 30;
        if (first < 1 || count < 1 || first + count - 1 > maximum) throw new ArgumentOutOfRangeException(nameof(count));
        // Exact test names keep Run01 from also selecting Run010 or another difficulty.
        string filter = string.Join('+', Enumerable.Range(first, count)
            .Select(i => $"^ProjectProject01.Simulation.BotMatch.{scenario}.Run{i:D2}$"));
        return new("봇 경기 시뮬레이션", filter, TimeSpan.FromMinutes(5 + count * 5), true);
    }
}
