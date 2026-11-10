namespace ProjectProject01DiagnosticsDashboard;

internal sealed record AutomationRunPlan(string Name, string Filter, TimeSpan Timeout, bool Simulation,
    IReadOnlyList<string>? EngineArguments = null)
{
    // Test names come from the project the dashboard sits in (UnrealProject), so the tool works in any project.
    internal static AutomationRunPlan General => new("일반 자동 검사", UnrealProject.Current.GeneralTestGroup, TimeSpan.FromMinutes(30), false);
    internal static AutomationRunPlan Frontend(string module) => new("UI 실험 검사",
        $"{module}.UITheme+{module}.CaseRecords.RenderPages+{module}.Frontend+{module}.Editor.TitlePresentation",
        TimeSpan.FromMinutes(10), false);
    internal static AutomationRunPlan Gameplay(string module) => new("핵심 흐름 검사 (PIE)",
        $"^{module}.Editor.SinglePlayerTitleReturnLoading$+^{module}.Editor.ThreeClientTravel$", TimeSpan.FromMinutes(10), false);
    internal static AutomationRunPlan Network(string module) => new("지연·패킷 손실 검사 (PIE)",
        $"^{module}.Editor.ThreeClientTravel$", TimeSpan.FromMinutes(10), false, ["-PktLag=100", "-PktLoss=2"]);
    internal static AutomationRunPlan Maps(string module) => new("경기 맵·이동 장치 검사",
        $"^{module}.Editor.ProductionMapLayout$+{module}.Elevator+{module}.CaseRecords.ShutterAuthorityAndLegacy",
        TimeSpan.FromMinutes(10), false);

    internal static AutomationRunPlan Selected(IEnumerable<string> names, string simulationPrefix)
    {
        var tests = names.Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase).ToArray();
        if (tests.Length == 0 || tests.Any(name => !AutomationTestCatalog.IsSelectableName(name)))
            throw new ArgumentException("실행할 검사를 목록에서 선택하세요.");
        int simulations = tests.Count(name => name.StartsWith(simulationPrefix + ".", StringComparison.OrdinalIgnoreCase));
        return new("선택 자동 검사", string.Join('+', tests.Select(name => $"^{name}$")),
            TimeSpan.FromMinutes((tests.Length > 20 ? 30 : 10) + simulations * 5), simulations > 0);
    }
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
            .Select(i => $"^{UnrealProject.Current.BotSimulationPrefix}.{scenario}.Run{i:D2}$"));
        return new("봇 경기 시뮬레이션", filter, TimeSpan.FromMinutes(5 + count * 5), true);
    }
}
