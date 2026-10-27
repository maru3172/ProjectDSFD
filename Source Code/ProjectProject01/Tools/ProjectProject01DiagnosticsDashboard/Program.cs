// File: Tools/ProjectProject01DiagnosticsDashboard/Program.cs
// Target: Win64 .NET 8 Windows desktop companion for Unreal Engine 5.8.2 diagnostic artifacts

using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Text.Json;
using System.Xml.Linq;

namespace ProjectProject01DiagnosticsDashboard;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        if (args.Contains("--self-test", StringComparer.OrdinalIgnoreCase)) return DashboardSelfTest.Run();
        Application.Run(new DashboardForm(args.Length==2&&args[0]=="--diagnostics-root"?args[1]:null));
        return 0;
    }
}

internal static class DashboardSelfTest
{
    public static int Run()
    {
        var cells = DashboardForm.ParseCsvLine("a,\"b,c\",\"d\"\"e\"");
        if (cells.Count != 3 || cells[1] != "b,c" || cells[2] != "d\"e") return 1;
        var server = new DiagnosticEvent("2026-01-01T00:00:00Z", "ListenServer_PIE0_P0", "ListenServer", 1.0, "Normal", "ActorStateChanged", "Mannequin;Slot=0;Manual=0;Frozen=0;Command=None;Controller=MannequinAIController;Location=(0,0,0)", "UEDPIE_0_BP_Mannequin_0");
        var client = new DiagnosticEvent("2026-01-01T00:00:00.100Z", "Client_PIE1_P0", "Client", 1.1, "Normal", "ActorStateChanged", "Mannequin;Slot=0;Manual=1;Frozen=0;Command=None;Controller=MultiplayTestPlayerController;Location=(300,0,0)", "UEDPIE_1_BP_Mannequin_0");
        if (DashboardForm.FindMultiplayerMismatchCandidates([server, client]).Count != 1) return 2;
        var aiProxyServer = new DiagnosticEvent("2026-01-01T00:00:01Z", "DedicatedServer_PIE0_P-1", "DedicatedServer", 2.0, "Normal", "ActorStateChanged", "Mannequin;Slot=-1;Manual=0;Frozen=0;Command=None;Controller=MannequinAIController;Location=(100,200,0)", "UEDPIE_0_Level:PersistentLevel.BP_MannequinAICharacter_C_10");
        var aiProxyClient = new DiagnosticEvent("2026-01-01T00:00:01.050Z", "Client_PIE1_P0", "Client", 2.05, "Normal", "ActorStateChanged", "Mannequin;Slot=-1;Manual=0;Frozen=0;Command=None;Controller=None;Location=(100,200,0)", "UEDPIE_1_Level:PersistentLevel.BP_MannequinAICharacter_C_10");
        var differentUnassignedClient = aiProxyClient with { Actor = "UEDPIE_1_Level:PersistentLevel.BP_MannequinAICharacter_C_11", Message = "Mannequin;Slot=-1;Manual=0;Frozen=0;Command=None;Controller=None;Location=(3000,200,0)" };
        if (DashboardForm.FindMultiplayerMismatchCandidates([aiProxyServer, aiProxyClient, differentUnassignedClient]).Count != 0) return 11;
        // 2026-10-22 capture: 226 of 242 position candidates were exactly speed x 0.33 s of per-world sampling skew.
        var movingServer = aiProxyServer with { GameSeconds = 5.0, Message = "Mannequin;Slot=0;Manual=0;Frozen=0;Command=None;Controller=MannequinAIController;Location=(0,0,90);Velocity=600.0" };
        var skewedClient = aiProxyClient with { GameSeconds = 5.0, Actor = movingServer.Actor, Message = "Mannequin;Slot=0;Manual=0;Frozen=0;Command=None;Controller=None;Location=(200,0,90);Velocity=600.5" };
        if (DashboardForm.FindMultiplayerMismatchCandidates([movingServer, skewedClient]).Count != 0) return 18;
        var desyncedClient = skewedClient with { Message = "Mannequin;Slot=0;Manual=0;Frozen=0;Command=None;Controller=None;Location=(1200,0,90);Velocity=600.5" };
        if (DashboardForm.FindMultiplayerMismatchCandidates([movingServer, desyncedClient]).Count != 1) return 19;
        if (DashboardForm.FindMultiplayerMismatchCandidates([movingServer with { GameSeconds = 0.008 }, desyncedClient with { GameSeconds = 0.008 }]).Count != 0) return 20;
        var remotePlayerServer = new DiagnosticEvent("2026-01-01T00:00:05Z", "DedicatedServer_PIE0_P-1", "DedicatedServer", 5.0, "Normal", "ActorStateChanged", "Player;Controller=MultiplayTestPlayerController_1;Location=(0,0,90);Velocity=0.0", "UEDPIE_0_Level:PersistentLevel.BP_PlayerCharacter_C_0");
        var remotePlayerProxy = remotePlayerServer with { Role = "Client_PIE1_P0", NetMode = "Client", Message = "Player;Controller=None;Location=(0,0,90);Velocity=0.0", Actor = "UEDPIE_1_Level:PersistentLevel.BP_PlayerCharacter_C_0" };
        if (DashboardForm.FindMultiplayerMismatchCandidates([remotePlayerServer, remotePlayerProxy]).Count != 0) return 21;
        if (DashboardForm.FindMultiplayerMismatchCandidates([remotePlayerServer, remotePlayerProxy with { Message = "Player;Controller=SomeAIController;Location=(0,0,90);Velocity=0.0" }]).Count != 1) return 22;
        var searchableEvent = DashboardForm.EventSearchText(server, "All searchable fields");
        if (!searchableEvent.Contains("ActorStateChanged", StringComparison.Ordinal) || searchableEvent.Contains(server.UtcTime, StringComparison.Ordinal)) return 9;
        if (DashboardForm.SortTimelineEvents([server, client], "GameSeconds", false).First().Role != client.Role) return 10;
        var directory = Path.Combine(Path.GetTempPath(), "ProjectProject01DiagnosticsDashboardSelfTest", Guid.NewGuid().ToString("N"));
        var roleDirectory = Path.Combine(directory, "ListenServer_PIE0_P0");
        Directory.CreateDirectory(roleDirectory);
        var report = new DiagnosticReport("SelfTest", "SelfTest", "ListenServer_PIE0_P0", "ListenServer", "Map", "GameMode", "Pass", Path.Combine(roleDirectory, "TestReport.xml"), 0, 1, 1, null, "Tuning");
        var directReportDirectory = Path.Combine(directory, "DirectSmokeRun");
        Directory.CreateDirectory(directReportDirectory);
        var directReport = new DiagnosticReport("DirectSmokeRun", "FullGameFlowSmoke", "SmokeTest", "External", "Map", "SmokeRunner", "Pass", Path.Combine(directReportDirectory, "TestReport.xml"), 0, 0, 3, null, "Unavailable");
        if (DashboardForm.ResolveRunDirectory(directReport, directory) != directReportDirectory ||
            DashboardForm.ResolveRunDirectory(report, directory) != directory) return 12;
        var sample = new PerformanceSample(
            UtcTime: "2026-01-01T00:00:00Z", Role: report.Role, NetMode: report.NetMode, GameSeconds: 1,
            Fps: 60, FrameMs: 16.7, GameMs: 5, DrawMs: 4, GpuMs: 3,
            PhysicalMemoryMiB: 100, VirtualMemoryMiB: 160, MemoryGrowthMiBPerSecond: 0, MemoryGrowthMiBPerMinute: 0,
            StreamingTextureMemoryMiB: 20, NonStreamingTextureMemoryMiB: 10, TexturePoolMiB: 256,
            UObjectCount: 1000, ActorCount: 20, InKiB: 1, OutKiB: 1, Players: 1, Mannequins: 1, Helpers: 0,
            PathFailures: 0, VisionChecks: 1, LineTraces: 1, AiTickMs: 0.1,
            DiagnosticsOverheadMs: 0.25, DiagnosticsActorAIOverheadMs: 0.10, DiagnosticsNavigationOverheadMs: 0.01,
            DiagnosticsNetworkOverheadMs: 0.02, DiagnosticsFrameTimingOverheadMs: 0.02, DiagnosticsMemoryOverheadMs: 0.03,
            DiagnosticsTextureRHIOverheadMs: 0.04, DiagnosticsBookkeepingOverheadMs: 0.03, DiagnosticsBufferedMemoryMiB: 0.5,
            HasDiagnosticsOverhead: true, HasDiagnosticsBreakdown: true, HasDiagnosticsBufferedMemory: true, Connections: 1);
        CacheMemoryAnalysisTests.Run(sample);
        ExtendedDiagnosticsTests.Run(sample);
        LongTermDiagnosticsTests.Run(sample);
        AutomationVerificationTests.Run();
        EngineCsvProfileTests.Run();
        DashboardForm.GenerateMergedArtifacts(directory, [report], [sample], [server, client]);
        if (!File.Exists(Path.Combine(directory, "CombinedReport.xml")) || !File.Exists(Path.Combine(directory, "MergedEvents.csv")) || !File.Exists(Path.Combine(directory, "MergedPerformance.csv"))) return 3;
        var combined = XDocument.Load(Path.Combine(directory, "CombinedReport.xml"));
        var mismatchRisk = combined.Root?.Element("RiskBreakdown")?.Elements("Risk")
            .FirstOrDefault(item => item.Attribute("Code")?.Value == "MultiplayerStateMismatchCandidate");
        if (mismatchRisk?.Attribute("Category")?.Value != "Multiplayer" || string.IsNullOrWhiteSpace(mismatchRisk.Element("Reason")?.Value) || string.IsNullOrWhiteSpace(mismatchRisk.Element("Evidence")?.Value)) return 6;
        if (combined.Root?.Element("PerformanceSummary")?.Attribute("AverageFps")?.Value != "60.000" ||
            combined.Root?.Element("NetworkSummary")?.Attribute("MaxConnectionCount")?.Value != "1") return 7;
        if(combined.Root?.Element("CacheMemoryAnalysis")?.Elements("Finding").Any()!=true)return 16;
        if(combined.Root?.Element("RuntimeCounters")?.Elements("Metric").Any()!=true)return 17;
        if (combined.Root?.Element("DiagnosticsOverheadSummary")?.Attribute("MaxMilliseconds")?.Value != "0.250" ||
            combined.Root?.Element("DiagnosticsOverheadSummary")?.Attribute("MaxActorAIMilliseconds")?.Value != "0.100" ||
            combined.Root?.Element("DiagnosticsOverheadSummary")?.Attribute("MaxTextureRHIMilliseconds")?.Value != "0.040" ||
            combined.Root?.Element("DiagnosticsOverheadSummary")?.Attribute("MaxBufferedMemoryMiB")?.Value != "0.500" ||
            combined.Root?.Element("ServerClientTimeAlignment")?.Attribute("MatchedPairCount")?.Value != "1") return 8;
        var performanceHeader = File.ReadLines(Path.Combine(directory, "MergedPerformance.csv")).FirstOrDefault() ?? string.Empty;
        if (!performanceHeader.Contains("UsedVirtualMiB", StringComparison.Ordinal) || !performanceHeader.Contains("UObjectCount", StringComparison.Ordinal) ||
            !performanceHeader.Contains("MemoryGrowthMiBPerMinute", StringComparison.Ordinal) || !performanceHeader.Contains("DiagnosticsOverheadMilliseconds", StringComparison.Ordinal) ||
            !performanceHeader.Contains("DiagnosticsActorAIOverheadMilliseconds", StringComparison.Ordinal) ||
            !performanceHeader.Contains("DiagnosticsBookkeepingOverheadMilliseconds", StringComparison.Ordinal) ||
            !performanceHeader.Contains("DiagnosticsBufferedMemoryMiB", StringComparison.Ordinal)) return 5;
        var batchDirectory = Path.Combine(directory, "Batch Path With Spaces");
        Directory.CreateDirectory(batchDirectory);
        var batchPath = Path.Combine(batchDirectory, "verify arguments.cmd");
        File.WriteAllText(batchPath, "@echo off\r\nif \"%~1\"==\"argument with spaces\" exit /b 0\r\nexit /b 9\r\n", Encoding.ASCII);
        var batchResult = DashboardForm.RunProcessAsync(batchPath, ["argument with spaces"]).GetAwaiter().GetResult();
        if (batchResult.ExitCode != 0) return 4;
        var streamedLog=Path.Combine(batchDirectory,"Streamed.log");var observed=new List<string>();
        var live=Path.Combine(batchDirectory,"live.cmd");
        File.WriteAllText(live,"@echo off\r\necho progress-marker\r\npowershell.exe -NoProfile -Command \"Start-Sleep -Seconds 20\"\r\n",Encoding.ASCII);
        try{DashboardForm.RunProcessAsync(live,[],timeout:TimeSpan.FromSeconds(2),outputFile:streamedLog,onLine:line=>observed.Add(line)).GetAwaiter().GetResult();return 23;}
        catch(OperationCanceledException){}
        if(!File.ReadAllText(streamedLog).Contains("progress-marker")||!observed.Contains("progress-marker"))return 24;
        var streams=Path.Combine(batchDirectory,"streams.cmd");
        File.WriteAllText(streams,"@echo off\r\nfor /l %%i in (1,1,6000) do @echo stderr-%%i-012345678901234567890123456789012345678901234567890123456789 1>&2\r\nfor /l %%i in (1,1,6000) do @echo stdout-%%i-012345678901234567890123456789012345678901234567890123456789\r\n",Encoding.ASCII);
        using(var limit=new CancellationTokenSource(TimeSpan.FromSeconds(15)))
        {
            var result=DashboardForm.RunProcessAsync(streams,[],limit.Token).GetAwaiter().GetResult();
            if(result.ExitCode!=0||!result.Output.Contains("stdout-6000")||!result.Output.Contains("stderr-6000"))return 13;
        }
        // Exercise the actual form-close cancellation source without displaying a window.
        using(var form=new DashboardForm())
        {
            var sleep=Path.Combine(batchDirectory,"owned-sleep.cmd");
            File.WriteAllText(sleep,"@echo off\r\necho started\r\npowershell.exe -NoProfile -Command \"Start-Sleep -Seconds 30\"\r\n",Encoding.ASCII);
            var task=DashboardForm.RunProcessAsync(sleep,[],form.LifetimeTokenForTest);
            form.CloseForTest();
            try{task.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();return 14;}
            catch(OperationCanceledException){}
        }
        Console.WriteLine("ProjectProject01 diagnostics dashboard self-test passed.");
        return 0;
    }
}

internal sealed record DiagnosticReport(
    string TestRunId, string TestName, string Role, string NetMode, string Map, string GameMode,
    string Outcome, string FilePath, int RiskCount, int SampleCount, int ConnectedPlayerCount,
    string? TracePath, string TuningFingerprint);

internal sealed record PerformanceSample(
    string UtcTime, string Role, string NetMode, double GameSeconds, double Fps, double FrameMs,
    double GameMs, double DrawMs, double GpuMs, double PhysicalMemoryMiB, double VirtualMemoryMiB,
    double MemoryGrowthMiBPerSecond, double MemoryGrowthMiBPerMinute,
    double StreamingTextureMemoryMiB, double NonStreamingTextureMemoryMiB, double TexturePoolMiB,
    int UObjectCount, int ActorCount, double InKiB, double OutKiB, int Players, int Mannequins, int Helpers, int PathFailures,
    int VisionChecks, int LineTraces, double AiTickMs, double DiagnosticsOverheadMs,
    double DiagnosticsActorAIOverheadMs, double DiagnosticsNavigationOverheadMs, double DiagnosticsNetworkOverheadMs,
    double DiagnosticsFrameTimingOverheadMs, double DiagnosticsMemoryOverheadMs, double DiagnosticsTextureRHIOverheadMs,
    double DiagnosticsBookkeepingOverheadMs, double DiagnosticsBufferedMemoryMiB,
    bool HasDiagnosticsOverhead, bool HasDiagnosticsBreakdown, bool HasDiagnosticsBufferedMemory, int Connections)
{
    public double? AllocatorUsedMiB { get; init; }
    public double? AllocatorCommittedMiB { get; init; }
    public double? AllocatorRetainedMiB { get; init; }
    public double? AvailablePhysicalMiB { get; init; }
    public double? TotalPhysicalMiB { get; init; }
    public bool HasTextureCounters { get; init; }
    public Dictionary<string,double> RuntimeCounters {get;init;}=[];
    public double? TexturePoolUsePercent => HasTextureCounters&&double.IsFinite(TexturePoolMiB)&&double.IsFinite(StreamingTextureMemoryMiB)&&TexturePoolMiB>0&&StreamingTextureMemoryMiB>=0
        ?StreamingTextureMemoryMiB/TexturePoolMiB*100:null;
}

internal sealed record RunMetrics(
    double AverageFps, double MaxFrameMs, double MaxGameMs, double MaxDrawMs, double MaxGpuMs,
    double MaxPhysicalMemoryMiB, double MaxVirtualMemoryMiB, double MaxMemoryGrowthMiBPerMinute,
    int MaxUObjectCount, int MaxActorCount, double MaxStreamingTextureMemoryMiB,
    double MaxNonStreamingTextureMemoryMiB, double MaxTexturePoolMiB,
    double MaxInKiB, double MaxOutKiB, double MaxAiTickMs, int MaxLineTraces,
    int MaxPathFailures, int MaxConnections, double AverageDiagnosticsOverheadMs, double MaxDiagnosticsOverheadMs,
    double MaxDiagnosticsActorAIOverheadMs, double MaxDiagnosticsNavigationOverheadMs, double MaxDiagnosticsNetworkOverheadMs,
    double MaxDiagnosticsFrameTimingOverheadMs, double MaxDiagnosticsMemoryOverheadMs, double MaxDiagnosticsTextureRHIOverheadMs,
    double MaxDiagnosticsBookkeepingOverheadMs, double MaxDiagnosticsBufferedMemoryMiB,
    bool DiagnosticsOverheadAvailable, bool DiagnosticsBreakdownAvailable, bool DiagnosticsBufferedMemoryAvailable);

internal sealed record TimeAlignmentSummary(int MatchedPairCount, double MedianClockOffsetMilliseconds, double MaxAbsoluteClockOffsetMilliseconds);

internal sealed record DiagnosticEvent(
    string UtcTime, string Role, string NetMode, double GameSeconds, string Severity,
    string Code, string Message, string Actor);

internal sealed record RiskBreakdownRow(
    string Category, string Code, int Count, string Roles, string RelatedActors,
    double FirstGameSeconds, double LastGameSeconds, string Reason, string Evidence);

internal sealed record OverheadBreakdownRow(string Role, string Category, string Unit, int Samples, double Average, double Maximum, double? AveragePercentOfTotalTime);

internal sealed class RunSummary
{
    public required string TestRunId { get; init; }
    public required string Roles { get; init; }
    public required string Map { get; init; }
    public required string GameMode { get; init; }
    public required string Outcome { get; init; }
    public int RiskCount { get; init; }
    public int Samples { get; init; }
    public int Players { get; init; }
    public int Mannequins { get; init; }
    public required string Tuning { get; init; }
    public required string RunDirectory { get; init; }
    public required RunMetrics Metrics { get; init; }
    public required IReadOnlyDictionary<string, int> RiskByCategory { get; init; }
    public required IReadOnlyDictionary<string, int> RiskByCode { get; init; }
    public required TimeAlignmentSummary TimeAlignment { get; init; }
    public IReadOnlyList<DiagnosticHistoryCapture> ExtendedCaptures { get; init; } = [];
    public DateTimeOffset? FirstSampleUtc { get; init; }
    public DateTimeOffset? LastSampleUtc { get; init; }
}

internal sealed record ChartPoint(string Series, double X, double Y);

internal sealed class TrendChart : Control
{
    private List<ChartPoint> points = [];
    private string title = "자료 없음";
    private static readonly Color[] Palette = [Color.DodgerBlue, Color.OrangeRed, Color.ForestGreen, Color.MediumPurple, Color.Goldenrod, Color.DeepPink];

    public TrendChart() { DoubleBuffered = true; BackColor = Color.White; }

    public void SetData(string newTitle, IEnumerable<ChartPoint> newPoints)
    {
        title = newTitle;
        points = ReducePoints(newPoints);
        Invalidate();
    }

    // Preserve peaks/troughs for diagnosis, while bounding draw calls for long captures.
    internal static List<ChartPoint> ReducePoints(IEnumerable<ChartPoint> source)
    {
        List<ChartPoint> result=[];
        foreach(var group in source.Where(p=>double.IsFinite(p.X)&&double.IsFinite(p.Y)).GroupBy(p=>p.Series))
        {
            var sorted=group.OrderBy(p=>p.X).ToArray();
            if(sorted.Length<=800){result.AddRange(sorted);continue;}
            result.Add(sorted[0]);int bucketSize=(int)Math.Ceiling((sorted.Length-2)/399d);
            for(int start=1;start<sorted.Length-1;start+=bucketSize)
            {
                var bucket=sorted.Skip(start).Take(Math.Min(bucketSize,sorted.Length-1-start));
                var minimum=bucket.MinBy(p=>p.Y)!;var maximum=bucket.MaxBy(p=>p.Y)!;
                result.AddRange(new[]{minimum,maximum}.Distinct().OrderBy(p=>p.X));
            }
            result.Add(sorted[^1]);
        }
        return result;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        var g = e.Graphics;
        g.DrawString(title, Font, Brushes.Black, 12, 8);
        var area = new RectangleF(62, 36, Math.Max(10, Width - 82), Math.Max(10, Height - 76));
        g.DrawRectangle(Pens.Gray, area.X, area.Y, area.Width, area.Height);
        if (points.Count == 0)
        {
            g.DrawString("선택한 조건에 표시할 표본이 없습니다.", Font, Brushes.DimGray, area.X + 10, area.Y + 10);
            return;
        }
        var minX = points.Min(point => point.X);
        var maxX = points.Max(point => point.X);
        var minY = points.Min(point => point.Y);
        var maxY = points.Max(point => point.Y);
        if (Math.Abs(maxX - minX) < 0.0001) maxX = minX + 1;
        if (Math.Abs(maxY - minY) < 0.0001) { minY -= 1; maxY += 1; }
        g.DrawString(maxY.ToString("F2", CultureInfo.InvariantCulture), Font, Brushes.DimGray, 4, area.Y - 4);
        g.DrawString(minY.ToString("F2", CultureInfo.InvariantCulture), Font, Brushes.DimGray, 4, area.Bottom - 14);
        var groups = points.GroupBy(point => point.Series, StringComparer.Ordinal).ToList();
        for (var groupIndex = 0; groupIndex < groups.Count; ++groupIndex)
        {
            var color = Palette[groupIndex % Palette.Length];
            using var pen = new Pen(color, 2f);
            using var brush = new SolidBrush(color);
            var plotted = groups[groupIndex].OrderBy(point => point.X).Select(point => new PointF(
                area.Left + (float)((point.X - minX) / (maxX - minX) * area.Width),
                area.Bottom - (float)((point.Y - minY) / (maxY - minY) * area.Height))).ToArray();
            if (plotted.Length > 1) g.DrawLines(pen, plotted);
            else if (plotted.Length == 1) g.FillEllipse(brush, plotted[0].X - 2, plotted[0].Y - 2, 4, 4);
            g.DrawString(groups[groupIndex].Key, Font, brush, area.Left + groupIndex * 145, area.Bottom + 8);
        }
    }
}

internal sealed class DashboardForm : Form
{
    private readonly CancellationTokenSource windowLifetime=new();
    internal CancellationToken LifetimeTokenForTest=>windowLifetime.Token;
    internal void CloseForTest()=>OnFormClosing(new FormClosingEventArgs(CloseReason.UserClosing,false));
    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        base.OnFormClosing(e);if(!e.Cancel)windowLifetime.Cancel();
    }
    private readonly TextBox rootPath = new() { ReadOnly = true, Width = 330 };
    private readonly TextBox searchBox = new() { Width = 180, PlaceholderText = "실행 식별자 / 맵 / 게임 모드" };
    private readonly ComboBox mapFilter = new() { Width = 130, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox outcomeFilter = new() { Width = 130, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox metricSelector = new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Top };
    private readonly ComboBox eventSearchField = new() { Width = 150, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox eventValueFilter = new() { Width = 220, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox eventSearchBox = new() { Width = 280, PlaceholderText = "이벤트 검색 (UtcTime 제외)" };
    private readonly DataGridView reportsGrid = CreateGrid();
    private readonly DataGridView timelineGrid = CreateGrid();
    private readonly DataGridView riskBreakdownGrid = CreateGrid();
    private readonly DataGridView overheadBreakdownGrid = CreateGrid();
    private readonly DataGridView cacheMemoryGrid = CreateGrid();
    private readonly DataGridView runtimeMetricsGrid=CreateGrid();
    private readonly TextBox details = new() { Dock = DockStyle.Fill, ReadOnly = true, Multiline = true, ScrollBars = ScrollBars.Both, Font = new Font(FontFamily.GenericMonospace, 9f) };
    private readonly TrendChart runChart = new() { Dock = DockStyle.Fill };
    private readonly LongTermDiagnosticsPanel extendedHistory = new();
    private readonly EngineCsvProfilePanel engineCsvProfile = new();
    private readonly List<DiagnosticReport> reports = [];
    private readonly List<RunSummary> runSummaries = [];
    private List<DiagnosticEvent> selectedTimelineEvents = [];
    private List<PerformanceSample> selectedPerformance=[];
    private string selectedPerformanceRunId="";
    private string timelineSortColumn = "UtcTime";
    private bool timelineSortAscending = true;

    public DashboardForm(string? initialRoot=null)
    {
        Text = "백화점 화재사건 · 진단 대시보드";
        Width = 1380;
        Height = 860;
        StartPosition = FormStartPosition.CenterScreen;

        var browse = new Button { Text = "진단 폴더 열기", AutoSize = true };
        browse.Click += (_, _) => SelectRootFolder();
        var refresh = new Button { Text = "새로고침", AutoSize = true };
        refresh.Click += (_, _) => LoadReports(rootPath.Text);
        var insights = new Button { Text = "추적 파일 열기", AutoSize = true };
        insights.Click += (_, _) => OpenSelectedTrace();
        var verify = new Button { Text = "빌드·자동 검사", AutoSize = true };
        verify.Click += async (_, _) => await VerifySelectedRunAsync(verify, AutomationRunPlan.General);
        var simulate = new Button { Text = "봇 경기 시뮬레이션", AutoSize = true };
        simulate.Click += async (_, _) =>
        {
            using var options = new BotSimulationOptionsForm();
            if (options.ShowDialog(this) == DialogResult.OK && options.Plan is { } plan) await VerifySelectedRunAsync(simulate, plan);
        };

        mapFilter.Items.Add("All maps");
        outcomeFilter.Items.AddRange(["All outcomes", "Pass", "ReviewRequired", "TimedOut", "Inconclusive"]);
        mapFilter.SelectedIndex = outcomeFilter.SelectedIndex = 0;
        metricSelector.Items.AddRange(["FPS", "Frame ms", "Game thread ms", "Draw thread ms", "GPU ms", "Physical memory MiB", "Virtual memory MiB", "Memory growth MiB/s", "Memory growth MiB/min", "UObject count", "Actor count", "Streaming texture MiB", "Non-streaming texture MiB", "Texture pool MiB", "Inbound KiB/s", "Outbound KiB/s", "AI tick ms",
            "Diagnostics total overhead ms", "Diagnostics Actor/AI overhead ms", "Diagnostics navigation overhead ms", "Diagnostics network overhead ms",
            "Diagnostics frame timing overhead ms", "Diagnostics memory overhead ms", "Diagnostics texture/RHI overhead ms", "Diagnostics bookkeeping overhead ms",
            "Diagnostics buffered memory MiB", "Line traces", "Path failures"]);
        metricSelector.SelectedIndex = 0;
        metricSelector.Items.AddRange(RuntimeMetricAnalysis.Keys.Cast<object>().ToArray());
        metricSelector.Items.AddRange(["Allocator used MiB","Allocator OS-held MiB","Allocator retained/slack MiB","Available system RAM MiB","Streaming texture pool %"]);
        eventSearchField.Items.AddRange(["All searchable fields", "Role", "NetMode", "GameSeconds", "Severity", "Code", "Message", "Actor"]);
        eventSearchField.SelectedIndex = 0;
        eventValueFilter.Items.Add("All values");
        eventValueFilter.SelectedIndex = 0;
        DiagnosticUiText.Localize(metricSelector, DiagnosticUiText.Metric);
        DiagnosticUiText.Localize(mapFilter); DiagnosticUiText.Localize(outcomeFilter);
        DiagnosticUiText.Localize(eventSearchField); DiagnosticUiText.Localize(eventValueFilter);
        foreach(var grid in new[]{reportsGrid,timelineGrid,riskBreakdownGrid,overheadBreakdownGrid,cacheMemoryGrid,runtimeMetricsGrid}) DiagnosticUiText.Localize(grid);

        var toolbar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 40, WrapContents = false, Padding = new Padding(3) };
        toolbar.AutoScroll = true;
        toolbar.Controls.AddRange([browse, rootPath, refresh, searchBox, mapFilter, outcomeFilter, insights]);
        var verificationBar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 38, WrapContents = false, Padding = new Padding(3) };
        verificationBar.Controls.AddRange([verify, simulate]);
        var split = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, FixedPanel = FixedPanel.Panel1 };
        Shown += (_, _) => { if(split.Height>400) split.SplitterDistance=Math.Min(240,split.Height-300); };
        split.Panel1.Controls.Add(reportsGrid);
        var tabs = new TabControl { Dock = DockStyle.Fill };
        tabs.TabPages.Add(new TabPage("요약") { Controls = { details } });
        var timelinePage = new TabPage("통합 시간순 기록");
        var timelineFilterBar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 36, WrapContents = false, Padding = new Padding(3) };
        timelineFilterBar.Controls.AddRange([new Label { Text = "검색 항목", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, eventSearchField,
            new Label { Text = "정확히 일치하는 값", AutoSize = true, Padding = new Padding(8, 7, 0, 0) }, eventValueFilter, eventSearchBox]);
        timelinePage.Controls.Add(timelineGrid);
        timelinePage.Controls.Add(timelineFilterBar);
        tabs.TabPages.Add(timelinePage);
        tabs.TabPages.Add(new TabPage("위험 후보 상세") { Controls = { riskBreakdownGrid } });
        tabs.TabPages.Add(new TabPage("진단 수집 비용") { Controls = { overheadBreakdownGrid } });
        var cachePage=new TabPage("캐시·메모리");cachePage.Controls.Add(cacheMemoryGrid);
        cacheMemoryGrid.AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill;
        cacheMemoryGrid.AutoSizeRowsMode=DataGridViewAutoSizeRowsMode.AllCells;
        cacheMemoryGrid.DefaultCellStyle.WrapMode=DataGridViewTriState.True;
        cacheMemoryGrid.DataBindingComplete+=(_,_)=>
        {
            foreach(DataGridViewColumn column in cacheMemoryGrid.Columns)
            {
                column.HeaderText=column.Name switch {"Role"=>"역할","Category"=>"분석 항목","Status"=>"판정 범위","Code"=>"분석 코드","Evidence"=>"관측 근거","Interpretation"=>"해석·확인 방법",_=>column.Name};
                column.FillWeight=column.Name is "Evidence" or "Interpretation"?240:80;
            }
        };
        cachePage.Controls.Add(new Label { Dock=DockStyle.Top,Height=54,Text="관측·검토 후보이며 누수 확정이 아닙니다. 할당자 보유 차이에는 캐시·빈 풀·정렬·조각화가 포함됩니다.\nPIE가 같은 프로세스면 RAM 수치도 공유되므로 역할별 값을 합산하지 마세요. OS/드라이버 전체 캐시는 측정하지 않습니다." });
        tabs.TabPages.Add(cachePage);
        tabs.TabPages.Add(new TabPage("GC·프로세스·요청"){Controls={runtimeMetricsGrid}});
        runtimeMetricsGrid.AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill;
        tabs.TabPages.Add(new TabPage("백엔드·풀 진단"){Controls={new BackendDiagnosticsPanel()}});
        var runChartPage = new TabPage("실행 내 추세");
        runChartPage.Controls.Add(runChart);
        runChartPage.Controls.Add(metricSelector);
        tabs.TabPages.Add(runChartPage);
        var historyPage = new TabPage("진단 전체, 비교");
        historyPage.Controls.Add(extendedHistory);
        tabs.TabPages.Add(historyPage);
        tabs.TabPages.Add(new TabPage("엔진 프로파일러 기록") { Controls = { engineCsvProfile } });
        split.Panel2.Controls.Add(tabs);
        Controls.Add(split);
        Controls.Add(verificationBar);
        Controls.Add(toolbar);

        reportsGrid.SelectionChanged += (_, _) => ShowSelectedRun();
        searchBox.TextChanged += (_, _) => ApplyFilters();
        mapFilter.SelectedIndexChanged += (_, _) => ApplyFilters();
        outcomeFilter.SelectedIndexChanged += (_, _) => ApplyFilters();
        metricSelector.SelectedIndexChanged += (_, _) => UpdateRunChart();
        eventSearchField.SelectedIndexChanged += (_, _) => { PopulateEventValueFilter(); ApplyTimelineFilters(); };
        eventValueFilter.SelectedIndexChanged += (_, _) => ApplyTimelineFilters();
        eventSearchBox.TextChanged += (_, _) => ApplyTimelineFilters();
        timelineGrid.ColumnHeaderMouseClick += (_, args) => SortTimelineByColumn(args.ColumnIndex);

        var defaultPath = initialRoot is not null?Path.GetFullPath(initialRoot):Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "Saved", "Diagnostics"));
        engineCsvProfile.SetContext(defaultPath, []);
        if (Directory.Exists(defaultPath)) LoadReports(defaultPath);
    }

    private static DataGridView CreateGrid() => new()
    {
        Dock = DockStyle.Fill, ReadOnly = true, AutoGenerateColumns = true,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect, MultiSelect = false,
        AllowUserToAddRows = false, AllowUserToDeleteRows = false, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.DisplayedCells
    };

    private void SelectRootFolder()
    {
        using var dialog = new FolderBrowserDialog { Description = "Select ProjectProject01/Saved/Diagnostics" };
        if (dialog.ShowDialog(this) == DialogResult.OK) LoadReports(dialog.SelectedPath);
    }

    private void LoadReports(string path)
    {
        reports.Clear();
        runSummaries.Clear();
        rootPath.Text = path;
        if (!Directory.Exists(path))
        {
            reportsGrid.DataSource = null;
            details.Text = "진단 폴더를 찾을 수 없습니다.";
            engineCsvProfile.SetContext(path, []);
            return;
        }

        foreach (var file in Directory.EnumerateFiles(path, "TestReport.xml", SearchOption.AllDirectories))
        {
            try
            {
                var root = XDocument.Load(file).Root;
                if (root?.Name != "TestReport") continue;
                string Value(string name) => root.Attribute(name)?.Value ?? "Unknown";
                var collection = root.Element("Collection");
                var report = new DiagnosticReport(
                    Value("TestRunId"), Value("TestName"), Value("Role"), Value("NetMode"), Value("Map"), Value("GameMode"), Value("Outcome"), file,
                    ParseInt(collection?.Attribute("RiskCandidateCount")?.Value), ParseInt(collection?.Attribute("SampleCount")?.Value),
                    ParseInt(root.Attribute("ConnectedPlayerCount")?.Value), null,
                    ComputeTuningFingerprint(Path.GetDirectoryName(file)!));
                var runDirectory = ResolveRunDirectory(report, path);
                reports.Add(report with { TracePath = Directory.EnumerateFiles(runDirectory, "*.utrace", SearchOption.TopDirectoryOnly).FirstOrDefault() });
            }
            catch (Exception exception)
            {
                details.Text = $"기록 읽기 실패: {file}{Environment.NewLine}{exception.Message}";
            }
        }

        foreach (var group in reports.GroupBy(report => report.TestRunId, StringComparer.Ordinal))
        {
            var runReports = group.ToList();
            var samples = LoadPerformance(runReports);
            var events = LoadEvents(runReports);
            var mismatchCandidates = FindMultiplayerMismatchCandidates(events);
            var riskBreakdown = BuildRiskBreakdown(events.Concat(mismatchCandidates));
            var runDirectory = ResolveRunDirectory(runReports[0], path);
            GenerateMergedArtifacts(runDirectory, runReports, samples, events);
            var combinedOutcome = ReadCombinedOutcome(runDirectory) ?? string.Join(",", runReports.Select(report => report.Outcome).Distinct());
            runSummaries.Add(new RunSummary
            {
                TestRunId = group.Key,
                Roles = string.Join(", ", runReports.Select(report => report.Role).Distinct().Order()),
                Map = string.Join(", ", runReports.Select(report => report.Map).Distinct()),
                GameMode = string.Join(", ", runReports.Select(report => report.GameMode).Distinct()),
                Outcome = combinedOutcome,
                RiskCount = riskBreakdown.Sum(row => row.Count),
                Samples = samples.Count,
                Players = samples.Count > 0 ? samples.Max(sample => sample.Players) : runReports.Max(report => report.ConnectedPlayerCount),
                Mannequins = samples.Count > 0 ? samples.Max(sample => sample.Mannequins) : 0,
                Tuning = string.Join("/", runReports.Select(report => report.TuningFingerprint).Distinct()),
                RunDirectory = runDirectory,
                Metrics = BuildRunMetrics(samples),
                RiskByCategory = riskBreakdown.GroupBy(row => row.Category, StringComparer.Ordinal).ToDictionary(category => category.Key, category => category.Sum(row => row.Count), StringComparer.Ordinal),
                RiskByCode = riskBreakdown.ToDictionary(row => row.Code, row => row.Count, StringComparer.Ordinal),
                TimeAlignment = AnalyzeServerClientTimeAlignment(events),
                FirstSampleUtc = SampleTimes(samples).Select(time => (DateTimeOffset?)time).Min(),
                LastSampleUtc = SampleTimes(samples).Select(time => (DateTimeOffset?)time).Max(),
                ExtendedCaptures = LongTermDiagnostics.Game(group.Key,
                    $"{group.Key} · {string.Join(", ", runReports.Select(r => r.Map).Distinct())} · P{(samples.Count > 0 ? samples.Max(s => s.Players) : 0)} · M{(samples.Count > 0 ? samples.Max(s => s.Mannequins) : 0)} · T{string.Join("/", runReports.Select(r => r.TuningFingerprint).Distinct())}", samples, events)
            });
        }


        mapFilter.Items.Clear();
        mapFilter.Items.Add("All maps");
        foreach (var map in runSummaries.Select(summary => summary.Map).Distinct().Order()) mapFilter.Items.Add(map);
        mapFilter.SelectedIndex = 0;
        ApplyFilters();
        engineCsvProfile.SetContext(path, runSummaries.Where(r => r.FirstSampleUtc is not null && r.LastSampleUtc is not null)
            .Select(r => (r.TestRunId, r.FirstSampleUtc!.Value, r.LastSampleUtc!.Value)));
    }

    // Wall-clock window of a run's performance samples; used only to link engine CSV captures recorded at the same time.
    private static IEnumerable<DateTimeOffset> SampleTimes(IEnumerable<PerformanceSample> samples) =>
        samples.Select(sample => DateTimeOffset.TryParse(sample.UtcTime, CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal, out var time) ? time : (DateTimeOffset?)null)
            .Where(time => time is not null).Select(time => time!.Value);

    private void ApplyFilters()
    {
        var search = searchBox.Text.Trim();
        var map = mapFilter.SelectedItem?.ToString() ?? "All maps";
        var outcome = outcomeFilter.SelectedItem?.ToString() ?? "All outcomes";
        var filtered = runSummaries.Where(summary =>
            (search.Length == 0 || $"{summary.TestRunId} {summary.Map} {summary.GameMode} {summary.Roles} {summary.Tuning}".Contains(search, StringComparison.OrdinalIgnoreCase)) &&
            (map == "All maps" || summary.Map == map) &&
            (outcome == "All outcomes" || summary.Outcome.Contains(outcome, StringComparison.OrdinalIgnoreCase)))
            .OrderByDescending(summary => summary.TestRunId, StringComparer.Ordinal).ToList();
        reportsGrid.DataSource = filtered;
        extendedHistory.SetGameCaptures(filtered.SelectMany(s => s.ExtendedCaptures.Append(LegacyHistoryMetrics.Capture(s))));
        ShowSelectedRun();
    }

    private RunSummary? SelectedRun() => reportsGrid.CurrentRow?.DataBoundItem as RunSummary;

    private void ShowSelectedRun()
    {
        var summary = SelectedRun();
        if (summary is null) return;
        var selectedReports = reports.Where(report => report.TestRunId == summary.TestRunId).ToList();
        var samples = LoadPerformance(selectedReports);
        selectedPerformance=samples;selectedPerformanceRunId=summary.TestRunId;
        var events = LoadEvents(selectedReports);
        var mismatches = FindMultiplayerMismatchCandidates(events);
        var mergedEvents = events.Concat(mismatches).OrderBy(item => item.UtcTime, StringComparer.Ordinal).ThenBy(item => item.Role).ToList();
        var riskBreakdown = BuildRiskBreakdown(mergedEvents);
        riskBreakdownGrid.DataSource = riskBreakdown;
        overheadBreakdownGrid.DataSource = BuildOverheadBreakdown(samples);
        cacheMemoryGrid.DataSource=CacheMemoryAnalysis.Analyze(samples,events);
        runtimeMetricsGrid.DataSource=RuntimeMetricAnalysis.Analyze(samples,events);
        var riskCategorySummary = riskBreakdown.Count == 0
            ? "None"
            : string.Join(", ", riskBreakdown.GroupBy(row => row.Category, StringComparer.Ordinal).OrderBy(group => group.Key).Select(group => $"{group.Key}={group.Sum(row => row.Count)}"));
        var roleSummary = string.Join(Environment.NewLine, samples.GroupBy(sample => sample.Role).Select(group =>
            $"{group.Key}: 평균 초당 프레임={group.Average(item => item.Fps):F1}, 게임/렌더/그래픽 최대 시간={group.Max(item => item.GameMs):F2}/{group.Max(item => item.DrawMs):F2}/{group.Max(item => item.GpuMs):F2} 밀리초, " +
            $"물리/가상 메모리 최대={group.Max(item => item.PhysicalMemoryMiB):F1}/{group.Max(item => item.VirtualMemoryMiB):F1} MiB, 최대 증가량={group.Max(item => item.MemoryGrowthMiBPerMinute):F1} MiB/분, " +
            $"언리얼 객체/액터 최대={group.Max(item => item.UObjectCount)}/{group.Max(item => item.ActorCount)}, 스트리밍/비스트리밍/텍스처 풀 최대={group.Max(item => item.StreamingTextureMemoryMiB):F1}/{group.Max(item => item.NonStreamingTextureMemoryMiB):F1}/{group.Max(item => item.TexturePoolMiB):F1} MiB, " +
            $"수신/송신 최대={group.Max(item => item.InKiB):F1}/{group.Max(item => item.OutKiB):F1} KiB/초, 최대 연결 수={group.Max(item => item.Connections)}, " +
            DiagnosticsOverheadText(group)));
        var comparisonSummary = BuildComparisonSummary(summary);
        var timeAlignmentSummary = summary.TimeAlignment.MatchedPairCount > 0
            ? $"일치 쌍={summary.TimeAlignment.MatchedPairCount}, 시각 차이 중앙값={summary.TimeAlignment.MedianClockOffsetMilliseconds:F2} 밀리초, 최대 절댓값={summary.TimeAlignment.MaxAbsoluteClockOffsetMilliseconds:F2} 밀리초"
            : "측정 불가 (서버·클라이언트의 일치하는 액터 상태 이벤트 없음)";
        details.Text = $"실행: {summary.TestRunId}{Environment.NewLine}결과: {DiagnosticUiText.Common(summary.Outcome)}{Environment.NewLine}역할: {summary.Roles}{Environment.NewLine}" +
            $"맵 / 게임 모드: {summary.Map} / {summary.GameMode}{Environment.NewLine}플레이어 / 마네킹: {summary.Players} / {summary.Mannequins}{Environment.NewLine}" +
            $"튜닝 식별값: {summary.Tuning}{Environment.NewLine}위험 후보: {summary.RiskCount}, 상태 불일치: {mismatches.Count}{Environment.NewLine}" +
            $"영역별 위험: {riskCategorySummary}{Environment.NewLine}{Environment.NewLine}" +
            $"서버·클라이언트 시각 정렬 추정: {timeAlignmentSummary}{Environment.NewLine}" +
            $"오프라인 시각 차이 추정값이며 네트워크 지연 시간이 아닙니다.{Environment.NewLine}{Environment.NewLine}" +
            $"이전 동일 조건 실행과 비교:{Environment.NewLine}{comparisonSummary}{Environment.NewLine}{Environment.NewLine}" +
            $"역할별 성능:{Environment.NewLine}{roleSummary}{Environment.NewLine}{Environment.NewLine}" +
            $"생성된 비교 자료:{Environment.NewLine}{Path.Combine(summary.RunDirectory, "CombinedReport.xml")}{Environment.NewLine}{Path.Combine(summary.RunDirectory, "MergedEvents.csv")}{Environment.NewLine}{Path.Combine(summary.RunDirectory, "MergedPerformance.csv")}";
        selectedTimelineEvents = mergedEvents;
        PopulateEventValueFilter();
        ApplyTimelineFilters();
        UpdateRunChart();
    }

    private void PopulateEventValueFilter()
    {
        var field = eventSearchField.SelectedItem?.ToString() ?? "All searchable fields";
        var previous = eventValueFilter.SelectedItem?.ToString();
        eventValueFilter.BeginUpdate();
        eventValueFilter.Items.Clear();
        eventValueFilter.Items.Add("All values");
        if (field is "Role" or "NetMode" or "Severity" or "Code" or "Actor")
        {
            foreach (var value in selectedTimelineEvents.Select(item => EventFieldValue(item, field)).Where(value => value.Length > 0).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal))
                eventValueFilter.Items.Add(value);
            eventValueFilter.Enabled = true;
        }
        else eventValueFilter.Enabled = false;
        eventValueFilter.SelectedItem = previous is not null && eventValueFilter.Items.Contains(previous) ? previous : "All values";
        eventValueFilter.EndUpdate();
    }

    private void ApplyTimelineFilters()
    {
        var field = eventSearchField.SelectedItem?.ToString() ?? "All searchable fields";
        var exactValue = eventValueFilter.Enabled ? eventValueFilter.SelectedItem?.ToString() ?? "All values" : "All values";
        var query = eventSearchBox.Text.Trim();
        var filtered = selectedTimelineEvents.Where(item =>
            (exactValue == "All values" || EventFieldValue(item, field).Equals(exactValue, StringComparison.Ordinal)) &&
            (query.Length == 0 || EventSearchText(item, field).Contains(query, StringComparison.OrdinalIgnoreCase)));
        var sorted = SortTimelineEvents(filtered, timelineSortColumn, timelineSortAscending).ToList();
        timelineGrid.DataSource = null;
        timelineGrid.DataSource = sorted;
        foreach (DataGridViewColumn column in timelineGrid.Columns) column.SortMode = DataGridViewColumnSortMode.Programmatic;
        var sortColumn = timelineGrid.Columns.Cast<DataGridViewColumn>().FirstOrDefault(column => column.DataPropertyName == timelineSortColumn);
        if (sortColumn is not null) sortColumn.HeaderCell.SortGlyphDirection = timelineSortAscending ? SortOrder.Ascending : SortOrder.Descending;
    }

    private void SortTimelineByColumn(int columnIndex)
    {
        if (columnIndex < 0 || columnIndex >= timelineGrid.Columns.Count) return;
        var property = timelineGrid.Columns[columnIndex].DataPropertyName;
        if (property.Length == 0) return;
        if (timelineSortColumn == property) timelineSortAscending = !timelineSortAscending;
        else { timelineSortColumn = property; timelineSortAscending = true; }
        ApplyTimelineFilters();
    }

    internal static string EventSearchText(DiagnosticEvent item, string field) => field == "All searchable fields"
        ? string.Join(' ', item.Role, item.NetMode, item.GameSeconds.ToString("F3", CultureInfo.InvariantCulture), item.Severity, item.Code, item.Message, item.Actor)
        : EventFieldValue(item, field);

    private static string EventFieldValue(DiagnosticEvent item, string field) => field switch
    {
        "Role" => item.Role,
        "NetMode" => item.NetMode,
        "GameSeconds" => item.GameSeconds.ToString("F3", CultureInfo.InvariantCulture),
        "Severity" => item.Severity,
        "Code" => item.Code,
        "Message" => item.Message,
        "Actor" => item.Actor,
        _ => string.Empty
    };

    internal static IOrderedEnumerable<DiagnosticEvent> SortTimelineEvents(IEnumerable<DiagnosticEvent> events, string property, bool ascending)
    {
        return property switch
        {
            "GameSeconds" => ascending ? events.OrderBy(item => item.GameSeconds) : events.OrderByDescending(item => item.GameSeconds),
            "Role" => ascending ? events.OrderBy(item => item.Role, StringComparer.OrdinalIgnoreCase) : events.OrderByDescending(item => item.Role, StringComparer.OrdinalIgnoreCase),
            "NetMode" => ascending ? events.OrderBy(item => item.NetMode, StringComparer.OrdinalIgnoreCase) : events.OrderByDescending(item => item.NetMode, StringComparer.OrdinalIgnoreCase),
            "Severity" => ascending ? events.OrderBy(item => item.Severity, StringComparer.OrdinalIgnoreCase) : events.OrderByDescending(item => item.Severity, StringComparer.OrdinalIgnoreCase),
            "Code" => ascending ? events.OrderBy(item => item.Code, StringComparer.OrdinalIgnoreCase) : events.OrderByDescending(item => item.Code, StringComparer.OrdinalIgnoreCase),
            "Message" => ascending ? events.OrderBy(item => item.Message, StringComparer.OrdinalIgnoreCase) : events.OrderByDescending(item => item.Message, StringComparer.OrdinalIgnoreCase),
            "Actor" => ascending ? events.OrderBy(item => item.Actor, StringComparer.OrdinalIgnoreCase) : events.OrderByDescending(item => item.Actor, StringComparer.OrdinalIgnoreCase),
            _ => ascending ? events.OrderBy(item => item.UtcTime, StringComparer.Ordinal) : events.OrderByDescending(item => item.UtcTime, StringComparer.Ordinal)
        };
    }

    internal static double HistoryValue(RunSummary summary, string metric) => metric switch
        {
            "Risk: Performance" => RiskCountFor(summary, "Performance"),
            "Risk: Memory" => RiskCountFor(summary, "Memory"),
            "Risk: Network" => RiskCountFor(summary, "Network"),
            "Risk: AI/Navigation" => RiskCountFor(summary, "AI/Navigation"),
            "Risk: Tuning/Data" => RiskCountFor(summary, "Tuning/Data"),
            "Risk: Multiplayer" => RiskCountFor(summary, "Multiplayer"),
            "Risk: Engine/Logs" => RiskCountFor(summary, "Engine/Logs"),
            "Risk: Other" => RiskCountFor(summary, "Other"),
            "Average FPS" => summary.Metrics.AverageFps,
            "Max frame ms" => summary.Metrics.MaxFrameMs,
            "Max game thread ms" => summary.Metrics.MaxGameMs,
            "Max draw thread ms" => summary.Metrics.MaxDrawMs,
            "Max GPU ms" => summary.Metrics.MaxGpuMs,
            "Max physical memory MiB" => summary.Metrics.MaxPhysicalMemoryMiB,
            "Max virtual memory MiB" => summary.Metrics.MaxVirtualMemoryMiB,
            "Max memory growth MiB/min" => summary.Metrics.MaxMemoryGrowthMiBPerMinute,
            "Max UObject count" => summary.Metrics.MaxUObjectCount,
            "Max Actor count" => summary.Metrics.MaxActorCount,
            "Max streaming texture MiB" => summary.Metrics.MaxStreamingTextureMemoryMiB,
            "Max non-streaming texture MiB" => summary.Metrics.MaxNonStreamingTextureMemoryMiB,
            "Max texture pool MiB" => summary.Metrics.MaxTexturePoolMiB,
            "Max inbound KiB/s" => summary.Metrics.MaxInKiB,
            "Max outbound KiB/s" => summary.Metrics.MaxOutKiB,
            "Max AI tick ms" => summary.Metrics.MaxAiTickMs,
            "Max line traces" => summary.Metrics.MaxLineTraces,
            "Max path failures" => summary.Metrics.MaxPathFailures,
            "Max connections" => summary.Metrics.MaxConnections,
            "Average diagnostics overhead ms" => summary.Metrics.DiagnosticsOverheadAvailable ? summary.Metrics.AverageDiagnosticsOverheadMs : double.NaN,
            "Max diagnostics overhead ms" => summary.Metrics.DiagnosticsOverheadAvailable ? summary.Metrics.MaxDiagnosticsOverheadMs : double.NaN,
            "Max diagnostics Actor/AI overhead ms" => summary.Metrics.DiagnosticsBreakdownAvailable ? summary.Metrics.MaxDiagnosticsActorAIOverheadMs : double.NaN,
            "Max diagnostics navigation overhead ms" => summary.Metrics.DiagnosticsBreakdownAvailable ? summary.Metrics.MaxDiagnosticsNavigationOverheadMs : double.NaN,
            "Max diagnostics network overhead ms" => summary.Metrics.DiagnosticsBreakdownAvailable ? summary.Metrics.MaxDiagnosticsNetworkOverheadMs : double.NaN,
            "Max diagnostics frame timing overhead ms" => summary.Metrics.DiagnosticsBreakdownAvailable ? summary.Metrics.MaxDiagnosticsFrameTimingOverheadMs : double.NaN,
            "Max diagnostics memory overhead ms" => summary.Metrics.DiagnosticsBreakdownAvailable ? summary.Metrics.MaxDiagnosticsMemoryOverheadMs : double.NaN,
            "Max diagnostics texture/RHI overhead ms" => summary.Metrics.DiagnosticsBreakdownAvailable ? summary.Metrics.MaxDiagnosticsTextureRHIOverheadMs : double.NaN,
            "Max diagnostics bookkeeping overhead ms" => summary.Metrics.DiagnosticsBreakdownAvailable ? summary.Metrics.MaxDiagnosticsBookkeepingOverheadMs : double.NaN,
            "Max diagnostics buffered memory MiB" => summary.Metrics.DiagnosticsBufferedMemoryAvailable ? summary.Metrics.MaxDiagnosticsBufferedMemoryMiB : double.NaN,
            "Max server-client timestamp offset ms" => summary.TimeAlignment.MatchedPairCount > 0 ? summary.TimeAlignment.MaxAbsoluteClockOffsetMilliseconds : double.NaN,
            _ when metric.StartsWith("Risk code: ", StringComparison.Ordinal) => RiskCountForCode(summary, metric[11..]),
            _ => summary.RiskCount
        };

    private static RunMetrics BuildRunMetrics(IReadOnlyList<PerformanceSample> samples)
    {
        if (samples.Count == 0) return new RunMetrics(
            0, 0, 0, 0, 0,
            0, 0, 0,
            0, 0, 0,
            0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0,
            0, 0, 0,
            0, 0, false, false, false);
        var overheadSamples = samples.Where(item => item.HasDiagnosticsOverhead).ToList();
        var breakdownSamples = samples.Where(item => item.HasDiagnosticsBreakdown).ToList();
        var bufferedMemorySamples = samples.Where(item => item.HasDiagnosticsBufferedMemory).ToList();
        return new RunMetrics(
            samples.Average(item => item.Fps), samples.Max(item => item.FrameMs), samples.Max(item => item.GameMs),
            samples.Max(item => item.DrawMs), samples.Max(item => item.GpuMs), samples.Max(item => item.PhysicalMemoryMiB),
            samples.Max(item => item.VirtualMemoryMiB), samples.Max(item => item.MemoryGrowthMiBPerMinute),
            samples.Max(item => item.UObjectCount), samples.Max(item => item.ActorCount),
            samples.Max(item => item.StreamingTextureMemoryMiB), samples.Max(item => item.NonStreamingTextureMemoryMiB),
            samples.Max(item => item.TexturePoolMiB), samples.Max(item => item.InKiB), samples.Max(item => item.OutKiB),
            samples.Max(item => item.AiTickMs), samples.Max(item => item.LineTraces), samples.Max(item => item.PathFailures),
            samples.Max(item => item.Connections),
            overheadSamples.Count > 0 ? overheadSamples.Average(item => item.DiagnosticsOverheadMs) : 0,
            overheadSamples.Count > 0 ? overheadSamples.Max(item => item.DiagnosticsOverheadMs) : 0,
            breakdownSamples.Count > 0 ? breakdownSamples.Max(item => item.DiagnosticsActorAIOverheadMs) : 0,
            breakdownSamples.Count > 0 ? breakdownSamples.Max(item => item.DiagnosticsNavigationOverheadMs) : 0,
            breakdownSamples.Count > 0 ? breakdownSamples.Max(item => item.DiagnosticsNetworkOverheadMs) : 0,
            breakdownSamples.Count > 0 ? breakdownSamples.Max(item => item.DiagnosticsFrameTimingOverheadMs) : 0,
            breakdownSamples.Count > 0 ? breakdownSamples.Max(item => item.DiagnosticsMemoryOverheadMs) : 0,
            breakdownSamples.Count > 0 ? breakdownSamples.Max(item => item.DiagnosticsTextureRHIOverheadMs) : 0,
            breakdownSamples.Count > 0 ? breakdownSamples.Max(item => item.DiagnosticsBookkeepingOverheadMs) : 0,
            bufferedMemorySamples.Count > 0 ? bufferedMemorySamples.Max(item => item.DiagnosticsBufferedMemoryMiB) : 0,
            overheadSamples.Count > 0, breakdownSamples.Count > 0, bufferedMemorySamples.Count > 0);
    }

    private static string DiagnosticsOverheadText(IEnumerable<PerformanceSample> samples)
    {
        var available = samples.Where(item => item.HasDiagnosticsOverhead).ToList();
        return available.Count == 0
            ? "진단 수집 시간=측정 불가 (구형 기록)"
            : $"진단 수집 시간 평균/최대={available.Average(item => item.DiagnosticsOverheadMs):F3}/{available.Max(item => item.DiagnosticsOverheadMs):F3} 밀리초";
    }

    private static List<OverheadBreakdownRow> BuildOverheadBreakdown(IEnumerable<PerformanceSample> samples)
    {
        var rows = new List<OverheadBreakdownRow>();
        var categories = new (string Name, Func<PerformanceSample, double> Value)[]
        {
            ("Actor / AI state scan", item => item.DiagnosticsActorAIOverheadMs),
            ("Navigation query", item => item.DiagnosticsNavigationOverheadMs),
            ("Network state query", item => item.DiagnosticsNetworkOverheadMs),
            ("Frame / CPU / GPU timing query", item => item.DiagnosticsFrameTimingOverheadMs),
            ("Process memory / UObject query", item => item.DiagnosticsMemoryOverheadMs),
            ("Texture / RHI memory query", item => item.DiagnosticsTextureRHIOverheadMs),
            ("Risk, events and bookkeeping", item => item.DiagnosticsBookkeepingOverheadMs)
        };
        foreach (var roleSamples in samples.Where(item => item.HasDiagnosticsOverhead || item.HasDiagnosticsBreakdown || item.HasDiagnosticsBufferedMemory).GroupBy(item => item.Role, StringComparer.Ordinal))
        {
            var role = roleSamples.ToList();
            var totals = role.Where(item => item.HasDiagnosticsOverhead).ToList();
            if (totals.Count > 0)
            {
                var averageTotal = totals.Average(item => item.DiagnosticsOverheadMs);
                rows.Add(new OverheadBreakdownRow(roleSamples.Key, "Total", "ms / sample", totals.Count, averageTotal, totals.Max(item => item.DiagnosticsOverheadMs), 100.0));
                rows.Add(new OverheadBreakdownRow(roleSamples.Key, "Estimated one-core duty at 1 Hz", "% of one CPU core", totals.Count,
                    averageTotal / 10.0, totals.Max(item => item.DiagnosticsOverheadMs) / 10.0, null));
            }
            var breakdown = role.Where(item => item.HasDiagnosticsBreakdown).ToList();
            if (breakdown.Count > 0)
            {
                var averageTotal = breakdown.Average(item => item.DiagnosticsOverheadMs);
                foreach (var category in categories)
                {
                    var average = breakdown.Average(category.Value);
                    rows.Add(new OverheadBreakdownRow(roleSamples.Key, category.Name, "ms / sample", breakdown.Count, average, breakdown.Max(category.Value),
                        averageTotal > 0.000001 ? average / averageTotal * 100.0 : 0.0));
                }
            }
            var bufferedMemory = role.Where(item => item.HasDiagnosticsBufferedMemory).ToList();
            if (bufferedMemory.Count > 0)
                rows.Add(new OverheadBreakdownRow(roleSamples.Key, "Buffered diagnostic Samples / Events", "MiB system RAM (lower-bound)", bufferedMemory.Count,
                    bufferedMemory.Average(item => item.DiagnosticsBufferedMemoryMiB), bufferedMemory.Max(item => item.DiagnosticsBufferedMemoryMiB), null));
        }
        return rows;
    }

    private static bool IsRiskEvent(DiagnosticEvent item) =>
        item.Severity.Equals("RiskCandidate", StringComparison.OrdinalIgnoreCase) ||
        item.Severity.Equals("Risk", StringComparison.OrdinalIgnoreCase);

    private static string RiskCategory(string code)
    {
        if (ContainsAny(code, "Memory", "Garbage", "UObject", "Texture")) return "Memory";
        if (ContainsAny(code, "Network", "RPC", "Replication", "Bandwidth", "Connection")) return "Network";
        if (ContainsAny(code, "Path", "Nav", "AIController", "AIStuck", "AIWorkload", "Vision", "LineTrace", "Possess", "Mannequin", "Helper")) return "AI/Navigation";
        if (ContainsAny(code, "Tuning", "Csv", "DataTable")) return "Tuning/Data";
        if (ContainsAny(code, "Mismatch", "Multiplayer")) return "Multiplayer";
        if (ContainsAny(code, "FPS", "Frame", "GPU", "Performance", "Tick")) return "Performance";
        if (ContainsAny(code, "EngineWarning", "EngineError", "Ensure", "Log")) return "Engine/Logs";
        return "Other";
    }

    private static bool ContainsAny(string value, params string[] terms) =>
        terms.Any(term => value.Contains(term, StringComparison.OrdinalIgnoreCase));

    internal static string RiskReason(string code) => code switch
    {
        "MissingNavData" => "AI를 처리해야 할 월드에서 NavMesh 데이터를 찾지 못함",
        "RepeatedPathFailures" => "최근 계측 구간에서 경로 요청 실패가 반복됨",
        "AIStuckWithTarget" => "이동 목표가 있지만 일정 시간 이상 수평 속도가 정체 기준 이하였음",
        "AIControllerMissing" => "권한이 있는 AI Actor에 Controller가 없음",
        "MultiplayerStateMismatchCandidate" => "비슷한 시각의 서버와 클라이언트 Actor 상태가 허용 차이를 넘음",
        "SustainedMemoryGrowth" => "프로세스 물리 메모리 증가률이 여러 표본 연속 기준을 넘음",
        "NetworkBandwidthSpike" => "이전 표본 대비 송수신 대역폭이 급증함",
        "NetworkRPCOrReplicationWarning" => "엔진 로그에서 네트워크·RPC·복제 관련 Warning/Error가 수집됨",
        "NetworkConnectionStateChanged" => "멀티플레이 월드에서 필요한 NetDriver 또는 연결 상태가 비정상으로 기록됨",
        "AIWorkloadSpike" => "AI Tick 시간 또는 라인 트레이스 횟수가 경량 진단 기준을 넘음",
        "TuningDataTableUnavailable" => "튜닝 DataTable Default 행 또는 런타임 캐시를 얻지 못함",
        "TuningCacheMismatch" => "DataTable 행과 런타임 튜닝 캐시 값이 다름",
        "TuningCsvInvalid" => "CSV를 튜닝 구조체로 읽지 못함",
        "TuningCsvDataTableMismatch" => "CSV와 DataTable Default 행 값이 다름",
        "ActorTuningMismatch" => "런타임 튜닝 캐시와 실제 Actor 적용 값이 다름",
        "EngineWarningOrError" => "수집 대상 엔진 Warning/Error가 발생함",
        _ => $"진단 코드 '{code}'의 발생 조건이 만족됨; 확정 원인은 Evidence 원본 로그를 확인해야 함"
    };

    private static List<RiskBreakdownRow> BuildRiskBreakdown(IEnumerable<DiagnosticEvent> events) => events
        .Where(IsRiskEvent)
        .GroupBy(item => new { Category = RiskCategory(item.Code), item.Code })
        .Select(group => new RiskBreakdownRow(
            group.Key.Category,
            group.Key.Code,
            group.Count(),
            string.Join(", ", group.Select(item => item.Role).Where(value => value.Length > 0).Distinct().Order()),
            string.Join(", ", group.Select(item => item.Actor).Where(value => value.Length > 0).Distinct().Order()),
            group.Min(item => item.GameSeconds),
            group.Max(item => item.GameSeconds),
            RiskReason(group.Key.Code),
            string.Join(" | ", group.Select(item => item.Message).Where(value => value.Length > 0).Distinct().Take(3))))
        .OrderByDescending(row => row.Count)
        .ThenBy(row => row.Category, StringComparer.Ordinal)
        .ThenBy(row => row.Code, StringComparer.Ordinal)
        .ToList();

    private static int RiskCountFor(RunSummary summary, string category) =>
        summary.RiskByCategory.TryGetValue(category, out var count) ? count : 0;

    private static int RiskCountForCode(RunSummary summary, string code) =>
        summary.RiskByCode.TryGetValue(code, out var count) ? count : 0;

    private string BuildComparisonSummary(RunSummary current)
    {
        var previous = runSummaries
            .Where(item => item.TestRunId != current.TestRunId &&
                item.Map == current.Map && item.GameMode == current.GameMode &&
                item.Players == current.Players && item.Mannequins == current.Mannequins &&
                string.Compare(item.TestRunId, current.TestRunId, StringComparison.Ordinal) < 0)
            .OrderByDescending(item => item.TestRunId, StringComparer.Ordinal)
            .FirstOrDefault();
        if (previous is null) return "동일 맵·게임모드·플레이어 수·마네킹 수의 이전 실행이 없음";

        var lines = new List<string> { $"기준 실행={previous.TestRunId}, 이전 튜닝={previous.Tuning} → 현재 튜닝={current.Tuning}" };
        AddComparison(lines, "전체 위험 후보 수", previous.RiskCount, current.RiskCount, higherIsBetter: false, exact: true);
        foreach (var category in new[] { "Performance", "Memory", "Network", "AI/Navigation", "Tuning/Data", "Multiplayer", "Engine/Logs", "Other" })
            AddComparison(lines, $"위험 후보/{DiagnosticUiText.Common(category)}", RiskCountFor(previous, category), RiskCountFor(current, category), higherIsBetter: false, exact: true);
        foreach (var code in previous.RiskByCode.Keys.Concat(current.RiskByCode.Keys).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal))
            AddComparison(lines, $"위험 후보/{RiskReason(code)}", RiskCountForCode(previous, code), RiskCountForCode(current, code), higherIsBetter: false, exact: true);
        if (current.Samples > 0 && previous.Samples > 0)
        {
            AddComparison(lines, "평균 FPS", previous.Metrics.AverageFps, current.Metrics.AverageFps, higherIsBetter: true);
            AddComparison(lines, "최대 Frame ms", previous.Metrics.MaxFrameMs, current.Metrics.MaxFrameMs, higherIsBetter: false);
            AddComparison(lines, "최대 Game thread ms", previous.Metrics.MaxGameMs, current.Metrics.MaxGameMs, higherIsBetter: false);
            AddComparison(lines, "최대 Draw thread ms", previous.Metrics.MaxDrawMs, current.Metrics.MaxDrawMs, higherIsBetter: false);
            AddComparison(lines, "최대 GPU ms", previous.Metrics.MaxGpuMs, current.Metrics.MaxGpuMs, higherIsBetter: false);
            AddComparison(lines, "최대 물리 메모리 MiB", previous.Metrics.MaxPhysicalMemoryMiB, current.Metrics.MaxPhysicalMemoryMiB, higherIsBetter: false);
            AddComparison(lines, "최대 가상 메모리 MiB", previous.Metrics.MaxVirtualMemoryMiB, current.Metrics.MaxVirtualMemoryMiB, higherIsBetter: false);
            AddComparison(lines, "최대 메모리 증가 MiB/min", previous.Metrics.MaxMemoryGrowthMiBPerMinute, current.Metrics.MaxMemoryGrowthMiBPerMinute, higherIsBetter: false);
            AddComparison(lines, "최대 Inbound KiB/s", previous.Metrics.MaxInKiB, current.Metrics.MaxInKiB, higherIsBetter: false);
            AddComparison(lines, "최대 Outbound KiB/s", previous.Metrics.MaxOutKiB, current.Metrics.MaxOutKiB, higherIsBetter: false);
            AddComparison(lines, "최대 AI Tick ms", previous.Metrics.MaxAiTickMs, current.Metrics.MaxAiTickMs, higherIsBetter: false);
            if (previous.Metrics.DiagnosticsOverheadAvailable && current.Metrics.DiagnosticsOverheadAvailable)
            {
                AddComparison(lines, "평균 진단 오버헤드 ms", previous.Metrics.AverageDiagnosticsOverheadMs, current.Metrics.AverageDiagnosticsOverheadMs, higherIsBetter: false);
                AddComparison(lines, "최대 진단 오버헤드 ms", previous.Metrics.MaxDiagnosticsOverheadMs, current.Metrics.MaxDiagnosticsOverheadMs, higherIsBetter: false);
                if (previous.Metrics.DiagnosticsBreakdownAvailable && current.Metrics.DiagnosticsBreakdownAvailable)
                {
                    AddComparison(lines, "최대 Actor/AI 진단 오버헤드 ms", previous.Metrics.MaxDiagnosticsActorAIOverheadMs, current.Metrics.MaxDiagnosticsActorAIOverheadMs, higherIsBetter: false);
                    AddComparison(lines, "최대 Navigation 진단 오버헤드 ms", previous.Metrics.MaxDiagnosticsNavigationOverheadMs, current.Metrics.MaxDiagnosticsNavigationOverheadMs, higherIsBetter: false);
                    AddComparison(lines, "최대 Network 진단 오버헤드 ms", previous.Metrics.MaxDiagnosticsNetworkOverheadMs, current.Metrics.MaxDiagnosticsNetworkOverheadMs, higherIsBetter: false);
                    AddComparison(lines, "최대 Frame timing 진단 오버헤드 ms", previous.Metrics.MaxDiagnosticsFrameTimingOverheadMs, current.Metrics.MaxDiagnosticsFrameTimingOverheadMs, higherIsBetter: false);
                    AddComparison(lines, "최대 Memory 진단 오버헤드 ms", previous.Metrics.MaxDiagnosticsMemoryOverheadMs, current.Metrics.MaxDiagnosticsMemoryOverheadMs, higherIsBetter: false);
                    AddComparison(lines, "최대 Texture/RHI 진단 오버헤드 ms", previous.Metrics.MaxDiagnosticsTextureRHIOverheadMs, current.Metrics.MaxDiagnosticsTextureRHIOverheadMs, higherIsBetter: false);
                    AddComparison(lines, "최대 Bookkeeping 진단 오버헤드 ms", previous.Metrics.MaxDiagnosticsBookkeepingOverheadMs, current.Metrics.MaxDiagnosticsBookkeepingOverheadMs, higherIsBetter: false);
                }
                if (previous.Metrics.DiagnosticsBufferedMemoryAvailable && current.Metrics.DiagnosticsBufferedMemoryAvailable)
                    AddComparison(lines, "최대 진단 버퍼 메모리 MiB", previous.Metrics.MaxDiagnosticsBufferedMemoryMiB, current.Metrics.MaxDiagnosticsBufferedMemoryMiB, higherIsBetter: false);
            }
            AddComparison(lines, "최대 경로 실패", previous.Metrics.MaxPathFailures, current.Metrics.MaxPathFailures, higherIsBetter: false, exact: true);
            AddNeutralComparison(lines, "최대 UObject 수", previous.Metrics.MaxUObjectCount, current.Metrics.MaxUObjectCount);
            AddNeutralComparison(lines, "최대 Actor 수", previous.Metrics.MaxActorCount, current.Metrics.MaxActorCount);
        }
        if (current.TimeAlignment.MatchedPairCount > 0 && previous.TimeAlignment.MatchedPairCount > 0)
            AddComparison(lines, "서버/클라이언트 최대 타임스탬프 오프셋 ms", previous.TimeAlignment.MaxAbsoluteClockOffsetMilliseconds, current.TimeAlignment.MaxAbsoluteClockOffsetMilliseconds, higherIsBetter: false);
        if (lines.Count == 1) lines.Add("비교할 유의한 수치 변화가 없음");
        lines.Add("판정은 자동 원인 확정이 아닌 같은 조건 간 검토 후보임.");
        return string.Join(Environment.NewLine, lines);
    }

    private static void AddComparison(List<string> lines, string label, double before, double after, bool higherIsBetter, bool exact = false)
    {
        label=DiagnosticUiText.ComparisonLabel(label);
        var delta = after - before;
        var tolerance = exact ? 0.0 : Math.Max(Math.Abs(before) * 0.02, 0.01);
        if (Math.Abs(delta) <= tolerance) return;
        var improved = higherIsBetter ? delta > 0 : delta < 0;
        var percent = Math.Abs(before) > 0.0001 ? $", {delta / Math.Abs(before) * 100:+0.0;-0.0;0.0}%" : string.Empty;
        lines.Add($"{(improved ? "개선 후보" : "악화 후보")}: {label} {before:F2} → {after:F2} (Δ {delta:+0.00;-0.00;0.00}{percent})");
    }

    private static void AddNeutralComparison(List<string> lines, string label, double before, double after)
    {
        label=DiagnosticUiText.ComparisonLabel(label);
        var delta = after - before;
        if (Math.Abs(delta) <= Math.Max(Math.Abs(before) * 0.02, 1.0)) return;
        lines.Add($"중립 변화: {label} {before:F0} → {after:F0} (Δ {delta:+0;-0;0})");
    }

    private void UpdateRunChart()
    {
        var summary = SelectedRun();
        if (summary is null) return;
        var samples = selectedPerformanceRunId==summary.TestRunId?selectedPerformance:LoadPerformance(reports.Where(report => report.TestRunId == summary.TestRunId));
        var metric = metricSelector.SelectedItem?.ToString() ?? "FPS";
        double Value(PerformanceSample sample) => metric switch
        {
            "Frame ms" => sample.FrameMs, "Game thread ms" => sample.GameMs, "Draw thread ms" => sample.DrawMs,
            "GPU ms" => sample.GpuMs, "Physical memory MiB" => sample.PhysicalMemoryMiB, "Virtual memory MiB" => sample.VirtualMemoryMiB,
            "Memory growth MiB/s" => sample.MemoryGrowthMiBPerSecond, "Memory growth MiB/min" => sample.MemoryGrowthMiBPerMinute,
            "Allocator used MiB"=>sample.AllocatorUsedMiB??double.NaN,
            "Allocator OS-held MiB"=>sample.AllocatorCommittedMiB??double.NaN,
            "Allocator retained/slack MiB"=>sample.AllocatorRetainedMiB??double.NaN,
            "Available system RAM MiB"=>sample.AvailablePhysicalMiB??double.NaN,
            "Streaming texture pool %"=>sample.TexturePoolUsePercent??double.NaN,
            "UObject count" => sample.UObjectCount, "Actor count" => sample.ActorCount,
            "Streaming texture MiB" => sample.StreamingTextureMemoryMiB, "Non-streaming texture MiB" => sample.NonStreamingTextureMemoryMiB, "Texture pool MiB" => sample.TexturePoolMiB,
            "Inbound KiB/s" => sample.InKiB, "Outbound KiB/s" => sample.OutKiB, "AI tick ms" => sample.AiTickMs,
            "Diagnostics total overhead ms" => sample.HasDiagnosticsOverhead ? sample.DiagnosticsOverheadMs : double.NaN,
            "Diagnostics Actor/AI overhead ms" => sample.HasDiagnosticsBreakdown ? sample.DiagnosticsActorAIOverheadMs : double.NaN,
            "Diagnostics navigation overhead ms" => sample.HasDiagnosticsBreakdown ? sample.DiagnosticsNavigationOverheadMs : double.NaN,
            "Diagnostics network overhead ms" => sample.HasDiagnosticsBreakdown ? sample.DiagnosticsNetworkOverheadMs : double.NaN,
            "Diagnostics frame timing overhead ms" => sample.HasDiagnosticsBreakdown ? sample.DiagnosticsFrameTimingOverheadMs : double.NaN,
            "Diagnostics memory overhead ms" => sample.HasDiagnosticsBreakdown ? sample.DiagnosticsMemoryOverheadMs : double.NaN,
            "Diagnostics texture/RHI overhead ms" => sample.HasDiagnosticsBreakdown ? sample.DiagnosticsTextureRHIOverheadMs : double.NaN,
            "Diagnostics bookkeeping overhead ms" => sample.HasDiagnosticsBreakdown ? sample.DiagnosticsBookkeepingOverheadMs : double.NaN,
            "Diagnostics buffered memory MiB" => sample.HasDiagnosticsBufferedMemory ? sample.DiagnosticsBufferedMemoryMiB : double.NaN,
            "Line traces" => sample.LineTraces, "Path failures" => sample.PathFailures,"FPS"=>sample.Fps,
            _ => sample.RuntimeCounters.TryGetValue(metric,out double counter)?counter:double.NaN
        };
        runChart.SetData($"{summary.TestRunId} — {DiagnosticUiText.Metric(metric)}", samples.Select(sample => new ChartPoint(sample.Role, sample.GameSeconds, Value(sample))));
    }

    private void OpenSelectedTrace()
    {
        var summary = SelectedRun();
        var trace = summary is null ? null : reports.FirstOrDefault(report => report.TestRunId == summary.TestRunId)?.TracePath;
        if (trace is null || !File.Exists(trace))
        {
            MessageBox.Show(this, "The selected record has no .utrace file.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        const string insights = @"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealInsights.exe";
        if (!File.Exists(insights))
        {
            MessageBox.Show(this, "언리얼 5.8 엔진 폴더에서 추적 분석 프로그램을 찾을 수 없습니다.", Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        Process.Start(new ProcessStartInfo { FileName = insights, Arguments = $"\"{trace}\"", UseShellExecute = true });
    }

    private Task VerifySelectedRunAsync(Button button, AutomationRunPlan plan)
    {
        var summary = SelectedRun();
        if (summary is null) return Task.CompletedTask;
        var projectRoot = FindProjectRoot(rootPath.Text);
        var projectFile = projectRoot is null ? null : Path.Combine(projectRoot, "ProjectDFSD.uproject");
        if (projectFile is null || !File.Exists(projectFile))
        {
            MessageBox.Show(this, "진단 폴더의 상위 경로에서 게임 프로젝트 파일을 찾을 수 없습니다.", Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return Task.CompletedTask;
        }
        button.Enabled = false;
        try
        {
            var notices = new List<(string Text, MessageBoxIcon Icon)>();
            using var dialog = new AutomationProgressForm(plan.Name, windowLifetime.Token,
                (token, progress) => ExecuteVerificationAsync(summary, projectFile, plan, token, progress, (text, icon) => notices.Add((text, icon))));
            dialog.ShowDialog(this);
            if (!IsDisposed && !Disposing)
                foreach (var notice in notices) MessageBox.Show(this, notice.Text, Text, MessageBoxButtons.OK, notice.Icon);
        }
        finally { if (!button.IsDisposed) button.Enabled = true; }
        return Task.CompletedTask;
    }

    private async Task ExecuteVerificationAsync(RunSummary summary, string projectFile, AutomationRunPlan plan,
        CancellationToken cancellationToken, IProgress<string> progress, Action<string, MessageBoxIcon> notify)
    {
        VerificationAttempt? attempt=null;
        string buildStatus="NotRun";
        int? buildExitCode=null;
        var roleOutcomesPass = reports.Where(report => report.TestRunId == summary.TestRunId).All(report => report.Outcome == "Pass");
        void RecordFailure(AutomationVerdict verdict)
        {
            if(attempt is null)return;
            try{AutomationVerification.Save(attempt,summary.TestRunId,buildStatus,buildExitCode,verdict,roleOutcomesPass);}
            catch(Exception recordError)when(recordError is IOException or UnauthorizedAccessException)
            {notify("검사 상태 파일도 저장하지 못했습니다. 실행 폴더 권한을 확인하세요.\n"+attempt.Directory+"\n"+recordError.Message,MessageBoxIcon.Warning);}
        }
        try
        {
            attempt=AutomationVerification.CreateAttempt(plan.Simulation ? Path.Combine(summary.RunDirectory, "BotSimulation") : summary.RunDirectory);
            AutomationVerification.Save(attempt,summary.TestRunId,"Running",null,new("Running","이번 실행의 빌드를 진행 중입니다."),roleOutcomesPass);
            const string buildTool = @"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat";
            const string editorCmd = @"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe";
            progress.Report("에디터 빌드 중…");
            File.WriteAllText(Path.Combine(attempt.Directory, "Selection.txt"), $"{plan.Name}\n{plan.Filter}\n제한 시간: {plan.Timeout.TotalMinutes:0}분", Encoding.UTF8);
            var build = await RunProcessAsync(buildTool, ["ProjectDFSDEditor", "Win64", "Development", $"-Project={projectFile}", "-WaitMutex", "-NoHotReloadFromIDE"],cancellationToken,
                outputFile: Path.Combine(attempt.Directory, "Build.log"));
            buildExitCode=build.ExitCode; buildStatus=build.ExitCode==0?"Succeeded":"Failed";
            AutomationVerdict verdict=new("NotRun","빌드 실패로 자동 검사를 실행하지 않았습니다.");
            if (build.ExitCode == 0)
            {
                AutomationVerification.Save(attempt,summary.TestRunId,buildStatus,buildExitCode,new("Running",plan.Name+" 실행 중입니다."),roleOutcomesPass);
                progress.Report(plan.Name + " 시작 중…");
                int completed = 0, failures = 0;
                var automation = await RunProcessAsync(editorCmd, AutomationVerification.Arguments(projectFile,attempt,plan.Filter),cancellationToken,
                    timeout: plan.Timeout, outputFile: Path.Combine(attempt.Directory, "Automation.log"), onLine: line =>
                    {
                        if (line.Contains("Test Completed.", StringComparison.Ordinal))
                        { Interlocked.Increment(ref completed); if (line.Contains("Result={Fail}", StringComparison.Ordinal)) Interlocked.Increment(ref failures); }
                        int start = line.IndexOf("Path={", StringComparison.Ordinal);
                        if (start >= 0 && (line.Contains("Test Started.", StringComparison.Ordinal) || line.Contains("Test Completed.", StringComparison.Ordinal)))
                            progress.Report($"완료 {completed}개 · 실패 {failures}개\n{line[(start+6)..].TrimEnd('}')}\n{(line.Contains("Test Started.",StringComparison.Ordinal)?"실행 중":"완료")}");
                    });
                verdict=AutomationVerification.Evaluate(attempt,automation.ExitCode);
            }
            AutomationVerification.Save(attempt,summary.TestRunId,buildStatus,buildExitCode,verdict,roleOutcomesPass);
            LoadReports(rootPath.Text);
            notify($"빌드={(buildStatus=="Succeeded"?"성공":"실패")}\n자동 검사：{verdict.Description}\n"+
                (verdict.Passed&&!roleOutcomesPass?"자동 검사는 통과했지만 선택한 게임 진단에는 검토할 항목이 남아 있습니다.\n":"")+
                $"이번 실행 기록：{attempt.Directory}",verdict.Passed?MessageBoxIcon.Information:MessageBoxIcon.Warning);
        }
        catch(OperationCanceledException)
        {
            var verdict=new AutomationVerdict(cancellationToken.IsCancellationRequested?"Cancelled":"TimedOut",cancellationToken.IsCancellationRequested?"이번 검사를 취소했습니다.":"단계별 실행 제한에 도달해 이번 검사 프로세스를 종료했습니다. 이번 실행의 로그를 확인하세요.");
            RecordFailure(verdict);
            notify(verdict.Description,MessageBoxIcon.Warning);
        }
        catch (Exception exception)
        {
            RecordFailure(new("ProcessFailed","검사 실행·결과 기록 오류: "+exception.GetType().Name));
            notify(exception.Message,MessageBoxIcon.Error);
        }
    }

    internal static async Task<ProcessResult> RunProcessAsync(string fileName, IReadOnlyList<string> arguments,CancellationToken cancellationToken=default,
        TimeSpan? timeout=null, string? outputFile=null, Action<string>? onLine=null)
    {
        var startInfo = new ProcessStartInfo { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true };
        if (Path.GetExtension(fileName).Equals(".bat", StringComparison.OrdinalIgnoreCase)||Path.GetExtension(fileName).Equals(".cmd",StringComparison.OrdinalIgnoreCase))
        {
            startInfo.FileName = Environment.GetEnvironmentVariable("ComSpec") ?? "cmd.exe";
            var command = string.Join(' ', new[] { fileName }.Concat(arguments).Select(QuoteCommandArgument));
            // cmd.exe requires the entire /c command to be wrapped in an additional pair of quotes
            // when the executable batch path itself is quoted. ArgumentList escapes those quotes as
            // literal characters, so use the raw Arguments form for this Windows-specific invocation.
            startInfo.Arguments = $"/d /s /c \"{command}\"";
        }
        else
        {
            startInfo.FileName = fileName;
            foreach (var argument in arguments) startInfo.ArgumentList.Add(argument);
        }
        using var process = new Process { StartInfo = startInfo };
        using var deadline=CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);deadline.CancelAfter(timeout ?? TimeSpan.FromMinutes(30));
        using var output = outputFile is null ? null : new StreamWriter(outputFile, false, new UTF8Encoding(true)) { AutoFlush = true };
        deadline.Token.ThrowIfCancellationRequested();
        if (!process.Start()) return new ProcessResult(-1, "Process could not be started.");
        var gate = new object();
        async Task<string> ReadAsync(StreamReader reader)
        {
            var text = new StringBuilder();
            while (await reader.ReadLineAsync(deadline.Token).ConfigureAwait(false) is { } line)
            {
                lock (gate) { output?.WriteLine(line); onLine?.Invoke(line); }
                text.AppendLine(line);
                if (text.Length > 8 * 1024 * 1024) text.Remove(0, 1024 * 1024); // Full output stays on disk.
            }
            return text.ToString();
        }
        var stdout=ReadAsync(process.StandardOutput);var stderr=ReadAsync(process.StandardError);
        try{await process.WaitForExitAsync(deadline.Token).ConfigureAwait(false);}
        catch(OperationCanceledException)
        {
            try{if(!process.HasExited)process.Kill(entireProcessTree:true);}catch(InvalidOperationException){}
            // A racing child can inherit a pipe while its parent is being terminated.
            // Never await unbounded EOF during window close; both reads share the cancellation token.
            try{await Task.WhenAll(stdout,stderr).WaitAsync(TimeSpan.FromSeconds(2)).ConfigureAwait(false);}
            catch(Exception ex) when(ex is OperationCanceledException or TimeoutException or IOException){}
            throw;
        }
        await Task.WhenAll(stdout,stderr).ConfigureAwait(false);
        return new ProcessResult(process.ExitCode,await stdout.ConfigureAwait(false)+Environment.NewLine+await stderr.ConfigureAwait(false));
    }

    private static string QuoteCommandArgument(string value) => $"\"{value.Replace("\"", "\"\"")}\"";

    private static List<PerformanceSample> LoadPerformance(IEnumerable<DiagnosticReport> selectedReports)
    {
        var result = new List<PerformanceSample>();
        foreach (var report in selectedReports)
        {
            var file = Path.Combine(Path.GetDirectoryName(report.FilePath)!, "Performance.csv");
            foreach (var row in ReadCsv(file))
            {
                result.Add(CacheMemoryAnalysis.ReadOptionalCounters(new PerformanceSample(
                    Get(row, "UtcTime"), Get(row, "Role", report.Role), Get(row, "NetMode", report.NetMode), Number(row, "GameSeconds"), Number(row, "FramesPerSecond"), Number(row, "FrameMilliseconds"),
                    Number(row, "GameThreadMilliseconds"), Number(row, "DrawThreadMilliseconds"), Number(row, "GpuMilliseconds"),
                    Number(row, "UsedPhysicalBytes") / 1048576.0, Number(row, "UsedVirtualBytes") / 1048576.0,
                    Number(row, "MemoryBytesPerSecondDelta") / 1048576.0, 0,
                    Number(row, "StreamingTextureMemoryBytes") / 1048576.0, Number(row, "NonStreamingTextureMemoryBytes") / 1048576.0, Number(row, "TexturePoolSizeBytes") / 1048576.0,
                    Integer(row, "UObjectCount"), Integer(row, "ActorCount"), Number(row, "InBytesPerSecond") / 1024.0, Number(row, "OutBytesPerSecond") / 1024.0,
                    Integer(row, "PlayerCount"), Integer(row, "MannequinCount"), Integer(row, "HelperCount"), Integer(row, "PathFailureCount"), Integer(row, "VisionCheckCount"),
                    Integer(row, "LineTraceCount"), Number(row, "AITickMilliseconds"), Number(row, "DiagnosticsOverheadMilliseconds"),
                    Number(row, "DiagnosticsActorAIOverheadMilliseconds"), Number(row, "DiagnosticsNavigationOverheadMilliseconds"),
                    Number(row, "DiagnosticsNetworkOverheadMilliseconds"), Number(row, "DiagnosticsFrameTimingOverheadMilliseconds"),
                    Number(row, "DiagnosticsMemoryOverheadMilliseconds"), Number(row, "DiagnosticsTextureRHIOverheadMilliseconds"),
                    Number(row, "DiagnosticsBookkeepingOverheadMilliseconds"), Number(row, "DiagnosticsBufferedMemoryBytes") / 1048576.0,
                    row.ContainsKey("DiagnosticsOverheadMilliseconds"), row.ContainsKey("DiagnosticsActorAIOverheadMilliseconds"),
                    row.ContainsKey("DiagnosticsBufferedMemoryBytes"), Integer(row, "NetworkConnectionCount")),row));
            }
        }
        var withMinuteGrowth = new List<PerformanceSample>(result.Count);
        foreach (var roleSamples in result.GroupBy(sample => sample.Role, StringComparer.Ordinal))
        {
            var orderedRoleSamples = roleSamples.OrderBy(sample => sample.GameSeconds).ToList();
            var baselineIndex = 0;
            for (var index = 0; index < orderedRoleSamples.Count; ++index)
            {
                var current = orderedRoleSamples[index];
                var threshold = current.GameSeconds - 60.0;
                while (baselineIndex + 1 < index && orderedRoleSamples[baselineIndex + 1].GameSeconds <= threshold) ++baselineIndex;
                var baseline = orderedRoleSamples[baselineIndex];
                var elapsedSeconds = current.GameSeconds - baseline.GameSeconds;
                var growthPerMinute = elapsedSeconds >= 1.0 ? (current.PhysicalMemoryMiB - baseline.PhysicalMemoryMiB) * 60.0 / elapsedSeconds : 0.0;
                withMinuteGrowth.Add(current with { MemoryGrowthMiBPerMinute = growthPerMinute });
            }
        }
        return withMinuteGrowth.OrderBy(sample => sample.UtcTime, StringComparer.Ordinal).ThenBy(sample => sample.Role).ToList();
    }

    private static List<DiagnosticEvent> LoadEvents(IEnumerable<DiagnosticReport> selectedReports)
    {
        var result = new List<DiagnosticEvent>();
        foreach (var report in selectedReports)
        {
            var file = Path.Combine(Path.GetDirectoryName(report.FilePath)!, "Events.csv");
            foreach (var row in ReadCsv(file))
            {
                result.Add(new DiagnosticEvent(Get(row, "UtcTime"), Get(row, "Role", report.Role), Get(row, "NetMode", report.NetMode), Number(row, "GameSeconds"),
                    Get(row, "Severity"), Get(row, "Code"), Get(row, "Message"), Get(row, "Actor")));
            }
        }
        return result.OrderBy(item => item.UtcTime, StringComparer.Ordinal).ThenBy(item => item.Role).ToList();
    }

    private static TimeAlignmentSummary AnalyzeServerClientTimeAlignment(IReadOnlyList<DiagnosticEvent> events)
    {
        var states = events.Where(item => item.Code == "ActorStateChanged" && DateTimeOffset.TryParse(item.UtcTime, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out _))
            .Select(item => (Event: item, Key: ActorIdentity(item))).Where(item => item.Key.Length > 0).ToList();
        var server = states.Where(item => item.Event.NetMode is "ListenServer" or "DedicatedServer").ToList();
        var clients = states.Where(item => item.Event.NetMode == "Client").ToList();
        var offsets = new List<double>();
        foreach (var source in server)
        {
            foreach (var role in clients.Select(item => item.Event.Role).Distinct(StringComparer.Ordinal))
            {
                var match = clients.Where(item => item.Event.Role == role && item.Key == source.Key)
                    .OrderBy(item => Math.Abs(item.Event.GameSeconds - source.Event.GameSeconds)).FirstOrDefault();
                if (match.Event is null || Math.Abs(match.Event.GameSeconds - source.Event.GameSeconds) > 1.25) continue;
                if (!DateTimeOffset.TryParse(source.Event.UtcTime, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var serverUtc) ||
                    !DateTimeOffset.TryParse(match.Event.UtcTime, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var clientUtc)) continue;
                var wallClockDeltaMs = (clientUtc - serverUtc).TotalMilliseconds;
                var gameClockDeltaMs = (match.Event.GameSeconds - source.Event.GameSeconds) * 1000.0;
                offsets.Add(wallClockDeltaMs - gameClockDeltaMs);
            }
        }
        if (offsets.Count == 0) return new TimeAlignmentSummary(0, 0, 0);
        offsets.Sort();
        var middle = offsets.Count / 2;
        var median = offsets.Count % 2 == 0 ? (offsets[middle - 1] + offsets[middle]) / 2.0 : offsets[middle];
        return new TimeAlignmentSummary(offsets.Count, median, offsets.Max(Math.Abs));
    }

    internal static List<DiagnosticEvent> FindMultiplayerMismatchCandidates(IReadOnlyList<DiagnosticEvent> events)
    {
        var states = events.Where(item => item.Code == "ActorStateChanged").Select(item => (Event: item, Key: ActorIdentity(item))).Where(item => item.Key.Length > 0).ToList();
        var server = states.Where(item => item.Event.NetMode is "ListenServer" or "DedicatedServer").ToList();
        var clients = states.Where(item => item.Event.NetMode == "Client").ToList();
        var result = new List<DiagnosticEvent>();
        var emitted = new HashSet<string>(StringComparer.Ordinal);
        foreach (var source in server)
        {
            foreach (var role in clients.Select(item => item.Event.Role).Distinct())
            {
                var match = clients.Where(item => item.Event.Role == role && item.Key == source.Key)
                    .OrderBy(item => Math.Abs(item.Event.GameSeconds - source.Event.GameSeconds)).FirstOrDefault();
                if (match.Event is null || Math.Abs(match.Event.GameSeconds - source.Event.GameSeconds) > 1.25) continue;
                // The first sample is taken before the initial replication of spawned pawns can arrive.
                if (source.Event.GameSeconds < MismatchWarmupSeconds) continue;
                var reasons = CompareActorStates(source.Event.Message, match.Event.Message, Math.Abs(match.Event.GameSeconds - source.Event.GameSeconds));
                if (reasons.Count == 0) continue;
                var dedupe = $"{source.Key}|{role}|{Math.Floor(source.Event.GameSeconds)}|{string.Join('|', reasons)}";
                if (!emitted.Add(dedupe)) continue;
                result.Add(new DiagnosticEvent(source.Event.UtcTime, $"{source.Event.Role}<->{role}", "Merged", source.Event.GameSeconds, "RiskCandidate",
                    "MultiplayerStateMismatchCandidate", $"ActorKey={source.Key}; {string.Join("; ", reasons)}", source.Event.Actor));
            }
        }
        return result;
    }

    private static string ActorIdentity(DiagnosticEvent item)
    {
        var type = item.Message.Split(';')[0];
        if (type == "Mannequin")
        {
            var slot = Regex.Match(item.Message, @"(?:^|;)Slot=([^;]+)");
            if (slot.Success && slot.Groups[1].Value != "-1") return $"Mannequin:Slot{slot.Groups[1].Value}";
        }
        var actor = Regex.Replace(item.Actor, @"UEDPIE_\d+_", string.Empty, RegexOptions.IgnoreCase);
        return actor.Length > 0 ? $"{type}:{actor.Split('.').Last()}" : string.Empty;
    }

    internal const double MismatchWarmupSeconds = 1.0;
    internal const double MismatchPositionToleranceCm = 150.0;
    // Each world samples on its own 1 Hz schedule; observed skew between paired samples reached ~0.33 s.
    internal const double MismatchSampleSkewSeconds = 0.5;

    private static List<string> CompareActorStates(string serverMessage, string clientMessage, double gameSecondsApart = 0)
    {
        var server = ParseState(serverMessage);
        var client = ParseState(clientMessage);
        var reasons = new List<string>();
        if (TryVector(server, "Location", out var serverPosition) && TryVector(client, "Location", out var clientPosition))
        {
            var distance = Math.Sqrt(Math.Pow(serverPosition.X - clientPosition.X, 2) + Math.Pow(serverPosition.Y - clientPosition.Y, 2) + Math.Pow(serverPosition.Z - clientPosition.Z, 2));
            // A moving actor legitimately travels speed x (sample skew + pairing gap) between the two samples.
            var speed = Math.Max(server.TryGetValue("Velocity", out var sv) ? ParseDouble(sv) : 0, client.TryGetValue("Velocity", out var cv) ? ParseDouble(cv) : 0);
            var tolerance = MismatchPositionToleranceCm + speed * (MismatchSampleSkewSeconds + gameSecondsApart);
            if (distance > tolerance) reasons.Add($"PositionDelta={distance:F1}cm (allowed {tolerance:F0}cm)");
        }
        foreach (var key in new[] { "Manual", "Frozen", "Command", "Target", "Retreat" })
        {
            if (server.TryGetValue(key, out var a) && client.TryGetValue(key, out var b) && a != b) reasons.Add($"{key} server={a} client={b}");
        }
        if (server.TryGetValue("Controller", out var serverController) && client.TryGetValue("Controller", out var clientController))
        {
            var serverControllerKind = ControllerKind(serverController);
            var clientControllerKind = ControllerKind(clientController);
            // Controllers exist on the server and only on the owning client. A client-side proxy of an AI or of
            // another player therefore reports None; only a different, non-None controller kind is a mismatch.
            if (serverControllerKind != clientControllerKind && clientControllerKind != "None")
                reasons.Add($"Controller server={serverControllerKind} client={clientControllerKind}");
        }
        return reasons;
    }

    private static Dictionary<string, string> ParseState(string message)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var part in message.Split(';').Skip(1))
        {
            var index = part.IndexOf('=');
            if (index > 0) result[part[..index].Trim()] = part[(index + 1)..].Trim();
        }
        return result;
    }

    private static string ControllerKind(string value) => value.Contains("PlayerController", StringComparison.OrdinalIgnoreCase) ? "Player" : value.Contains("AIController", StringComparison.OrdinalIgnoreCase) ? "AI" : value is "None" or "" ? "None" : "Other";
    private static bool TryVector(IReadOnlyDictionary<string, string> values, string key, out (double X, double Y, double Z) vector)
    {
        vector = default;
        if (!values.TryGetValue(key, out var text)) return false;
        var match = Regex.Match(text, @"\((-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)\)");
        if (!match.Success) return false;
        vector = (ParseDouble(match.Groups[1].Value), ParseDouble(match.Groups[2].Value), ParseDouble(match.Groups[3].Value));
        return true;
    }

    internal static void GenerateMergedArtifacts(string runDirectory, IReadOnlyList<DiagnosticReport> runReports, IReadOnlyList<PerformanceSample> samples, IReadOnlyList<DiagnosticEvent> events)
    {
        try
        {
            Directory.CreateDirectory(runDirectory);
            var mismatches = FindMultiplayerMismatchCandidates(events);
            var mergedEvents = events.Concat(mismatches).OrderBy(item => item.UtcTime, StringComparer.Ordinal).ThenBy(item => item.Role).ToList();
            var timeAlignment = AnalyzeServerClientTimeAlignment(events);
            var eventLines = new List<string> { "UtcTime,Role,NetMode,GameSeconds,Severity,Code,Message,Actor" };
            eventLines.AddRange(mergedEvents.Select(item => string.Join(',', EscapeCsv(item.UtcTime), EscapeCsv(item.Role), EscapeCsv(item.NetMode), item.GameSeconds.ToString("F3", CultureInfo.InvariantCulture), EscapeCsv(item.Severity), EscapeCsv(item.Code), EscapeCsv(item.Message), EscapeCsv(item.Actor))));
            File.WriteAllLines(Path.Combine(runDirectory, "MergedEvents.csv"), eventLines, new UTF8Encoding(true));

            var performanceLines = new List<string> { "UtcTime,Role,NetMode,GameSeconds,FramesPerSecond,FrameMilliseconds,GameThreadMilliseconds,DrawThreadMilliseconds,GpuMilliseconds,UsedPhysicalMiB,UsedVirtualMiB,MemoryGrowthMiBPerSecond,MemoryGrowthMiBPerMinute,UObjectCount,ActorCount,StreamingTextureMemoryMiB,NonStreamingTextureMemoryMiB,TexturePoolMiB,InKiBPerSecond,OutKiBPerSecond,PlayerCount,MannequinCount,HelperCount,PathFailureCount,VisionCheckCount,LineTraceCount,AITickMilliseconds,DiagnosticsOverheadMilliseconds,DiagnosticsActorAIOverheadMilliseconds,DiagnosticsNavigationOverheadMilliseconds,DiagnosticsNetworkOverheadMilliseconds,DiagnosticsFrameTimingOverheadMilliseconds,DiagnosticsMemoryOverheadMilliseconds,DiagnosticsTextureRHIOverheadMilliseconds,DiagnosticsBookkeepingOverheadMilliseconds,DiagnosticsBufferedMemoryMiB,NetworkConnectionCount" };
            performanceLines.AddRange(samples.Select(item => string.Join(',', EscapeCsv(item.UtcTime), EscapeCsv(item.Role), EscapeCsv(item.NetMode), Format(item.GameSeconds), Format(item.Fps), Format(item.FrameMs), Format(item.GameMs), Format(item.DrawMs), Format(item.GpuMs), Format(item.PhysicalMemoryMiB), Format(item.VirtualMemoryMiB), Format(item.MemoryGrowthMiBPerSecond), Format(item.MemoryGrowthMiBPerMinute), item.UObjectCount, item.ActorCount, Format(item.StreamingTextureMemoryMiB), Format(item.NonStreamingTextureMemoryMiB), Format(item.TexturePoolMiB), Format(item.InKiB), Format(item.OutKiB), item.Players, item.Mannequins, item.Helpers, item.PathFailures, item.VisionChecks, item.LineTraces, Format(item.AiTickMs), Format(item.DiagnosticsOverheadMs), Format(item.DiagnosticsActorAIOverheadMs), Format(item.DiagnosticsNavigationOverheadMs), Format(item.DiagnosticsNetworkOverheadMs), Format(item.DiagnosticsFrameTimingOverheadMs), Format(item.DiagnosticsMemoryOverheadMs), Format(item.DiagnosticsTextureRHIOverheadMs), Format(item.DiagnosticsBookkeepingOverheadMs), Format(item.DiagnosticsBufferedMemoryMiB), item.Connections)));
            performanceLines[0]+=",AllocatorStatsAvailable,AllocatorUsedMiB,AllocatorOSHeldMiB,AllocatorRetainedMiB,AvailableSystemRAMMiB,TotalSystemRAMMiB,TextureStatsAvailable,StreamingTexturePoolPercent";
            for(int i=0;i<samples.Count;++i)
            {
                var s=samples[i];string Value(double? n)=>n.HasValue?Format(n.Value):"";
                performanceLines[i+1]+=string.Join(',',"",s.AllocatorUsedMiB.HasValue?"true":"false",Value(s.AllocatorUsedMiB),Value(s.AllocatorCommittedMiB),Value(s.AllocatorRetainedMiB),
                    Value(s.AvailablePhysicalMiB),Value(s.TotalPhysicalMiB),s.HasTextureCounters?"true":"false",Value(s.TexturePoolUsePercent));
            }
            performanceLines[0]+=","+string.Join(',',RuntimeMetricAnalysis.Keys);
            for(int index=0;index<samples.Count;++index)
                performanceLines[index+1]+=","+string.Join(',',RuntimeMetricAnalysis.Keys.Select(k=>samples[index].RuntimeCounters.TryGetValue(k,out double value)?value.ToString("R",CultureInfo.InvariantCulture):""));
            File.WriteAllLines(Path.Combine(runDirectory,"MergedPerformance.csv"),performanceLines,new UTF8Encoding(true));

            var verificationPath = Path.Combine(runDirectory, "Verification.xml");
            var verification = File.Exists(verificationPath) ? XDocument.Load(verificationPath).Root : null;
            var riskBreakdown = BuildRiskBreakdown(mergedEvents);
            var totalRiskCount = riskBreakdown.Sum(item => item.Count);
            var metrics = BuildRunMetrics(samples);
            var hasTimeout = runReports.Any(report => report.Outcome == "TimedOut");
            var rolePassed = runReports.All(report => report.Outcome == "Pass");
            var verificationPassed = verification?.Attribute("FinalOutcome")?.Value == "Pass";
            var runtimeNeedsReview = runReports.Any(report => report.Outcome == "ReviewRequired");
            var combinedOutcome = hasTimeout ? "TimedOut" : runtimeNeedsReview || mismatches.Count > 0 ? "ReviewRequired" : verification is null ? "Inconclusive" : rolePassed && verificationPassed ? "Pass" : "ReviewRequired";
            var memorySummary = samples.Count > 0
                ? new XElement("MemorySummary", new XAttribute("Available", true),
                    new XAttribute("MaxPhysicalMiB", Format(samples.Max(item => item.PhysicalMemoryMiB))),
                    new XAttribute("MaxVirtualMiB", Format(samples.Max(item => item.VirtualMemoryMiB))),
                    new XAttribute("MaxGrowthMiBPerMinute", Format(samples.Max(item => item.MemoryGrowthMiBPerMinute))),
                    new XAttribute("MaxUObjectCount", samples.Max(item => item.UObjectCount)),
                    new XAttribute("MaxActorCount", samples.Max(item => item.ActorCount)),
                    new XAttribute("MaxStreamingTextureMiB", Format(samples.Max(item => item.StreamingTextureMemoryMiB))),
                    new XAttribute("MaxNonStreamingTextureMiB", Format(samples.Max(item => item.NonStreamingTextureMemoryMiB))),
                    new XAttribute("MaxTexturePoolMiB", Format(samples.Max(item => item.TexturePoolMiB))))
                : new XElement("MemorySummary", new XAttribute("Available", false));
            var performanceSummary = samples.Count > 0
                ? new XElement("PerformanceSummary", new XAttribute("Available", true),
                    new XAttribute("AverageFps", Format(metrics.AverageFps)),
                    new XAttribute("MaxFrameMilliseconds", Format(metrics.MaxFrameMs)),
                    new XAttribute("MaxGameThreadMilliseconds", Format(metrics.MaxGameMs)),
                    new XAttribute("MaxDrawThreadMilliseconds", Format(metrics.MaxDrawMs)),
                    new XAttribute("MaxGpuMilliseconds", Format(metrics.MaxGpuMs)),
                    new XAttribute("MaxAITickMilliseconds", Format(metrics.MaxAiTickMs)),
                    new XAttribute("MaxLineTraceCount", metrics.MaxLineTraces),
                    new XAttribute("MaxPathFailureCount", metrics.MaxPathFailures))
                : new XElement("PerformanceSummary", new XAttribute("Available", false));
            var networkSummary = samples.Count > 0
                ? new XElement("NetworkSummary", new XAttribute("Available", true),
                    new XAttribute("MaxInboundKiBPerSecond", Format(metrics.MaxInKiB)),
                    new XAttribute("MaxOutboundKiBPerSecond", Format(metrics.MaxOutKiB)),
                    new XAttribute("MaxConnectionCount", metrics.MaxConnections))
                : new XElement("NetworkSummary", new XAttribute("Available", false));
            var diagnosticsOverheadSummary = metrics.DiagnosticsOverheadAvailable
                ? new XElement("DiagnosticsOverheadSummary", new XAttribute("Available", true),
                    new XAttribute("Scope", "Once-per-second lightweight sample collection only"),
                    new XAttribute("BreakdownAvailable", metrics.DiagnosticsBreakdownAvailable),
                    new XAttribute("BufferedMemoryAvailable", metrics.DiagnosticsBufferedMemoryAvailable),
                    new XAttribute("AverageMilliseconds", Format(metrics.AverageDiagnosticsOverheadMs)),
                    new XAttribute("MaxMilliseconds", Format(metrics.MaxDiagnosticsOverheadMs)),
                    new XAttribute("MaxActorAIMilliseconds", Format(metrics.MaxDiagnosticsActorAIOverheadMs)),
                    new XAttribute("MaxNavigationMilliseconds", Format(metrics.MaxDiagnosticsNavigationOverheadMs)),
                    new XAttribute("MaxNetworkMilliseconds", Format(metrics.MaxDiagnosticsNetworkOverheadMs)),
                    new XAttribute("MaxFrameTimingMilliseconds", Format(metrics.MaxDiagnosticsFrameTimingOverheadMs)),
                    new XAttribute("MaxMemoryMilliseconds", Format(metrics.MaxDiagnosticsMemoryOverheadMs)),
                    new XAttribute("MaxTextureRHIMilliseconds", Format(metrics.MaxDiagnosticsTextureRHIOverheadMs)),
                    new XAttribute("MaxBookkeepingMilliseconds", Format(metrics.MaxDiagnosticsBookkeepingOverheadMs)),
                    new XAttribute("MaxBufferedMemoryMiB", Format(metrics.MaxDiagnosticsBufferedMemoryMiB)),
                    new XAttribute("BufferedMemoryInterpretation", "Lower-bound estimate for diagnostic Samples, Events and their strings"))
                : new XElement("DiagnosticsOverheadSummary", new XAttribute("Available", false));
            var timeAlignmentSummary = new XElement("ServerClientTimeAlignment",
                new XAttribute("Available", timeAlignment.MatchedPairCount > 0),
                new XAttribute("MatchedPairCount", timeAlignment.MatchedPairCount),
                new XAttribute("MedianClockOffsetMilliseconds", Format(timeAlignment.MedianClockOffsetMilliseconds)),
                new XAttribute("MaxAbsoluteClockOffsetMilliseconds", Format(timeAlignment.MaxAbsoluteClockOffsetMilliseconds)),
                new XAttribute("Interpretation", "Offline clock-offset estimate from matching ActorStateChanged events; not network latency"));
            var riskBreakdownElement = new XElement("RiskBreakdown", riskBreakdown.Select(item =>
                new XElement("Risk",
                    new XAttribute("Category", item.Category),
                    new XAttribute("Code", item.Code),
                    new XAttribute("Count", item.Count),
                    new XAttribute("Roles", item.Roles),
                    new XAttribute("RelatedActors", item.RelatedActors),
                    new XAttribute("FirstGameSeconds", Format(item.FirstGameSeconds)),
                    new XAttribute("LastGameSeconds", Format(item.LastGameSeconds)),
                    new XElement("Reason", item.Reason),
                    new XElement("Evidence", item.Evidence))));
            var combined = new XDocument(new XElement("CombinedTestReport", new XAttribute("TestRunId", runReports[0].TestRunId), new XAttribute("Outcome", combinedOutcome),
                new XAttribute("Map", string.Join(",", runReports.Select(report => report.Map).Distinct())), new XAttribute("GameMode", string.Join(",", runReports.Select(report => report.GameMode).Distinct())),
                new XAttribute("RoleCount", runReports.Count), new XAttribute("RiskCandidateCount", totalRiskCount), new XAttribute("StateMismatchCount", mismatches.Count),
                new XElement("Roles", runReports.Select(report => new XElement("Role", new XAttribute("Name", report.Role), new XAttribute("NetMode", report.NetMode), new XAttribute("Outcome", report.Outcome), new XAttribute("Report", Path.GetRelativePath(runDirectory, report.FilePath))))),
                riskBreakdownElement,
                performanceSummary,
                memorySummary,
                new XElement("CacheMemoryAnalysis",new XAttribute("Interpretation","Candidates only; allocator slack is not exclusively cache; no forced GC or purge"),
                    CacheMemoryAnalysis.Analyze(samples,events).Select(f=>new XElement("Finding",new XAttribute("Role",f.Role),new XAttribute("Category",f.Category),
                        new XAttribute("Status",f.Status),new XAttribute("Code",f.Code),new XElement("Evidence",f.Evidence),new XElement("Interpretation",f.Interpretation)))),
                networkSummary,
                new XElement("RuntimeCounters",RuntimeMetricAnalysis.Analyze(samples,events).Select(row=>new XElement("Metric",
                    new XAttribute("Role",row.Role),new XAttribute("Name",row.Metric),new XAttribute("Value",row.Value),new XAttribute("Unit",row.Unit),row.Scope))),
                diagnosticsOverheadSummary,
                timeAlignmentSummary,
                new XElement("BuildAndAutomation", new XAttribute("Available", verification is not null), verification?.Elements()),
                new XElement("Artifacts", new XElement("Events", "MergedEvents.csv"), new XElement("Performance", "MergedPerformance.csv"), new XElement("Trace", runReports.Select(report => report.TracePath).FirstOrDefault(path => path is not null) is { } trace ? Path.GetFileName(trace) : "Unavailable"))));
            combined.Save(Path.Combine(runDirectory, "CombinedReport.xml"));
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private static IEnumerable<Dictionary<string, string>> ReadCsv(string path)
    {
        if (!File.Exists(path)) yield break;
        using var reader = new StreamReader(path, Encoding.UTF8, true);
        var headerLine = reader.ReadLine();
        if (headerLine is null) yield break;
        var headers = ParseCsvLine(headerLine);
        string? line;
        while ((line = reader.ReadLine()) is not null)
        {
            var cells = ParseCsvLine(line);
            var row = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            for (var index = 0; index < headers.Count; ++index) row[headers[index]] = index < cells.Count ? cells[index] : string.Empty;
            yield return row;
        }
    }

    internal static List<string> ParseCsvLine(string line)
    {
        var cells = new List<string>();
        var cell = new StringBuilder();
        var quoted = false;
        for (var index = 0; index < line.Length; ++index)
        {
            var character = line[index];
            if (character == '"')
            {
                if (quoted && index + 1 < line.Length && line[index + 1] == '"') { cell.Append('"'); ++index; }
                else quoted = !quoted;
            }
            else if (character == ',' && !quoted) { cells.Add(cell.ToString()); cell.Clear(); }
            else cell.Append(character);
        }
        cells.Add(cell.ToString());
        return cells;
    }

    private static string ComputeTuningFingerprint(string roleDirectory)
    {
        using var sha = SHA256.Create();
        var bytes = Directory.EnumerateFiles(roleDirectory, "*.csv", SearchOption.TopDirectoryOnly).Where(file => Path.GetFileName(file) is "Player.csv" or "AITuning.csv" or "HelperTuning.csv").Order().SelectMany(File.ReadAllBytes).ToArray();
        return bytes.Length == 0 ? "Unavailable" : Convert.ToHexString(sha.ComputeHash(bytes))[..12];
    }

    private static string? ReadCombinedOutcome(string runDirectory)
    {
        try { return XDocument.Load(Path.Combine(runDirectory, "CombinedReport.xml")).Root?.Attribute("Outcome")?.Value; }
        catch { return null; }
    }

    internal static string ResolveRunDirectory(DiagnosticReport report, string fallbackRoot)
    {
        var reportDirectory = Path.GetDirectoryName(report.FilePath);
        if (string.IsNullOrWhiteSpace(reportDirectory)) return fallbackRoot;
        if (string.Equals(Path.GetFileName(reportDirectory), report.TestRunId, StringComparison.Ordinal)) return reportDirectory;
        return Directory.GetParent(reportDirectory)?.FullName ?? fallbackRoot;
    }

    private static string? FindProjectRoot(string diagnosticsPath)
    {
        var directory = new DirectoryInfo(diagnosticsPath);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "ProjectDFSD.uproject"))) return directory.FullName;
            directory = directory.Parent;
        }
        return null;
    }

    private static string Get(IReadOnlyDictionary<string, string> row, string key, string fallback = "") => row.TryGetValue(key, out var value) ? value : fallback;
    private static double Number(IReadOnlyDictionary<string, string> row, string key) => ParseDouble(Get(row, key));
    private static int Integer(IReadOnlyDictionary<string, string> row, string key) => int.TryParse(Get(row, key), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) ? value : 0;
    private static int ParseInt(string? value) => int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed) ? parsed : 0;
    private static double ParseDouble(string? value) => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed) ? parsed : 0;
    private static string EscapeCsv(string value) => $"\"{value.Replace("\"", "\"\"")}\"";
    private static string Format(double value) => value.ToString("F3", CultureInfo.InvariantCulture);
    internal sealed record ProcessResult(int ExitCode, string Output);
}
