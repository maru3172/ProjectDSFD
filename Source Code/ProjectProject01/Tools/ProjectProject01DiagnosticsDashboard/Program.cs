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
        if (args.Contains("--self-test", StringComparer.OrdinalIgnoreCase)) return DashboardSelfTest.Run();
        ApplicationConfiguration.Initialize();
        Application.Run(new DashboardForm());
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
        var client = new DiagnosticEvent("2026-01-01T00:00:00Z", "Client_PIE1_P0", "Client", 1.1, "Normal", "ActorStateChanged", "Mannequin;Slot=0;Manual=1;Frozen=0;Command=None;Controller=MultiplayTestPlayerController;Location=(300,0,0)", "UEDPIE_1_BP_Mannequin_0");
        if (DashboardForm.FindMultiplayerMismatchCandidates([server, client]).Count != 1) return 2;
        var directory = Path.Combine(Path.GetTempPath(), "ProjectProject01DiagnosticsDashboardSelfTest", Guid.NewGuid().ToString("N"));
        var roleDirectory = Path.Combine(directory, "ListenServer_PIE0_P0");
        Directory.CreateDirectory(roleDirectory);
        var report = new DiagnosticReport("SelfTest", "SelfTest", "ListenServer_PIE0_P0", "ListenServer", "Map", "GameMode", "Pass", Path.Combine(roleDirectory, "TestReport.xml"), 0, 1, 1, null, "Tuning");
        var sample = new PerformanceSample("2026-01-01T00:00:00Z", report.Role, report.NetMode, 1, 60, 16.7, 5, 4, 3, 100, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0.1, 1);
        DashboardForm.GenerateMergedArtifacts(directory, [report], [sample], [server, client]);
        if (!File.Exists(Path.Combine(directory, "CombinedReport.xml")) || !File.Exists(Path.Combine(directory, "MergedEvents.csv")) || !File.Exists(Path.Combine(directory, "MergedPerformance.csv"))) return 3;
        var batchDirectory = Path.Combine(directory, "Batch Path With Spaces");
        Directory.CreateDirectory(batchDirectory);
        var batchPath = Path.Combine(batchDirectory, "verify arguments.cmd");
        File.WriteAllText(batchPath, "@echo off\r\nif \"%~1\"==\"argument with spaces\" exit /b 0\r\nexit /b 9\r\n", Encoding.ASCII);
        var batchResult = DashboardForm.RunProcessAsync(batchPath, ["argument with spaces"]).GetAwaiter().GetResult();
        if (batchResult.ExitCode != 0) return 4;
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
    double GameMs, double DrawMs, double GpuMs, double MemoryMiB, double MemoryGrowthMiB,
    double InKiB, double OutKiB, int Players, int Mannequins, int Helpers, int PathFailures,
    int VisionChecks, int LineTraces, double AiTickMs, int Connections);

internal sealed record DiagnosticEvent(
    string UtcTime, string Role, string NetMode, double GameSeconds, string Severity,
    string Code, string Message, string Actor);

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
}

internal sealed record ChartPoint(string Series, double X, double Y);

internal sealed class TrendChart : Control
{
    private List<ChartPoint> points = [];
    private string title = "No data";
    private static readonly Color[] Palette = [Color.DodgerBlue, Color.OrangeRed, Color.ForestGreen, Color.MediumPurple, Color.Goldenrod, Color.DeepPink];

    public TrendChart() { DoubleBuffered = true; BackColor = Color.White; }

    public void SetData(string newTitle, IEnumerable<ChartPoint> newPoints)
    {
        title = newTitle;
        points = newPoints.Where(point => double.IsFinite(point.X) && double.IsFinite(point.Y)).ToList();
        Invalidate();
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
    private readonly TextBox rootPath = new() { ReadOnly = true, Width = 330 };
    private readonly TextBox searchBox = new() { Width = 180, PlaceholderText = "TestRunId / 맵 / 게임모드" };
    private readonly ComboBox mapFilter = new() { Width = 130, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox outcomeFilter = new() { Width = 130, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox metricSelector = new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Top };
    private readonly DataGridView reportsGrid = CreateGrid();
    private readonly DataGridView timelineGrid = CreateGrid();
    private readonly TextBox details = new() { Dock = DockStyle.Fill, ReadOnly = true, Multiline = true, ScrollBars = ScrollBars.Both, Font = new Font(FontFamily.GenericMonospace, 9f) };
    private readonly TrendChart runChart = new() { Dock = DockStyle.Fill };
    private readonly TrendChart historyChart = new() { Dock = DockStyle.Fill };
    private readonly List<DiagnosticReport> reports = [];
    private readonly List<RunSummary> runSummaries = [];

    public DashboardForm()
    {
        Text = "ProjectProject01 Diagnostics Dashboard";
        Width = 1380;
        Height = 860;
        StartPosition = FormStartPosition.CenterScreen;

        var browse = new Button { Text = "Open Diagnostics", AutoSize = true };
        browse.Click += (_, _) => SelectRootFolder();
        var refresh = new Button { Text = "Refresh", AutoSize = true };
        refresh.Click += (_, _) => LoadReports(rootPath.Text);
        var insights = new Button { Text = "Open .utrace", AutoSize = true };
        insights.Click += (_, _) => OpenSelectedTrace();
        var verify = new Button { Text = "Run Build + Automation", AutoSize = true };
        verify.Click += async (_, _) => await VerifySelectedRunAsync(verify);

        mapFilter.Items.Add("All maps");
        outcomeFilter.Items.AddRange(["All outcomes", "Pass", "ReviewRequired", "TimedOut", "Inconclusive"]);
        mapFilter.SelectedIndex = outcomeFilter.SelectedIndex = 0;
        metricSelector.Items.AddRange(["FPS", "Frame ms", "Game thread ms", "Draw thread ms", "GPU ms", "Memory MiB", "Memory growth MiB/s", "Inbound KiB/s", "Outbound KiB/s", "AI tick ms", "Line traces", "Path failures"]);
        metricSelector.SelectedIndex = 0;

        var toolbar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 40, WrapContents = false, Padding = new Padding(3) };
        toolbar.Controls.AddRange([browse, rootPath, refresh, searchBox, mapFilter, outcomeFilter, insights, verify]);
        var split = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 310 };
        split.Panel1.Controls.Add(reportsGrid);
        var tabs = new TabControl { Dock = DockStyle.Fill };
        tabs.TabPages.Add(new TabPage("Summary") { Controls = { details } });
        tabs.TabPages.Add(new TabPage("Merged Timeline") { Controls = { timelineGrid } });
        var runChartPage = new TabPage("Run Trend");
        runChartPage.Controls.Add(runChart);
        runChartPage.Controls.Add(metricSelector);
        tabs.TabPages.Add(runChartPage);
        tabs.TabPages.Add(new TabPage("Long-term Comparison") { Controls = { historyChart } });
        split.Panel2.Controls.Add(tabs);
        Controls.Add(split);
        Controls.Add(toolbar);

        reportsGrid.SelectionChanged += (_, _) => ShowSelectedRun();
        searchBox.TextChanged += (_, _) => ApplyFilters();
        mapFilter.SelectedIndexChanged += (_, _) => ApplyFilters();
        outcomeFilter.SelectedIndexChanged += (_, _) => ApplyFilters();
        metricSelector.SelectedIndexChanged += (_, _) => UpdateRunChart();

        var defaultPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "Saved", "Diagnostics"));
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
            details.Text = "Diagnostics directory was not found.";
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
                var runDirectory = Directory.GetParent(Path.GetDirectoryName(file)!)?.FullName ?? path;
                reports.Add(new DiagnosticReport(
                    Value("TestRunId"), Value("TestName"), Value("Role"), Value("NetMode"), Value("Map"), Value("GameMode"), Value("Outcome"), file,
                    ParseInt(collection?.Attribute("RiskCandidateCount")?.Value), ParseInt(collection?.Attribute("SampleCount")?.Value),
                    ParseInt(root.Attribute("ConnectedPlayerCount")?.Value), Directory.EnumerateFiles(runDirectory, "*.utrace", SearchOption.TopDirectoryOnly).FirstOrDefault(),
                    ComputeTuningFingerprint(Path.GetDirectoryName(file)!)));
            }
            catch (Exception exception)
            {
                details.Text = $"Could not read {file}{Environment.NewLine}{exception.Message}";
            }
        }

        foreach (var group in reports.GroupBy(report => report.TestRunId, StringComparer.Ordinal))
        {
            var runReports = group.ToList();
            var samples = LoadPerformance(runReports);
            var runDirectory = Directory.GetParent(Path.GetDirectoryName(runReports[0].FilePath)!)?.FullName ?? path;
            GenerateMergedArtifacts(runDirectory, runReports, samples, LoadEvents(runReports));
            var combinedOutcome = ReadCombinedOutcome(runDirectory) ?? string.Join(",", runReports.Select(report => report.Outcome).Distinct());
            runSummaries.Add(new RunSummary
            {
                TestRunId = group.Key,
                Roles = string.Join(", ", runReports.Select(report => report.Role).Distinct().Order()),
                Map = string.Join(", ", runReports.Select(report => report.Map).Distinct()),
                GameMode = string.Join(", ", runReports.Select(report => report.GameMode).Distinct()),
                Outcome = combinedOutcome,
                RiskCount = runReports.Sum(report => report.RiskCount),
                Samples = samples.Count,
                Players = samples.Count > 0 ? samples.Max(sample => sample.Players) : runReports.Max(report => report.ConnectedPlayerCount),
                Mannequins = samples.Count > 0 ? samples.Max(sample => sample.Mannequins) : 0,
                Tuning = string.Join("/", runReports.Select(report => report.TuningFingerprint).Distinct()),
                RunDirectory = runDirectory
            });
        }

        mapFilter.Items.Clear();
        mapFilter.Items.Add("All maps");
        foreach (var map in runSummaries.Select(summary => summary.Map).Distinct().Order()) mapFilter.Items.Add(map);
        mapFilter.SelectedIndex = 0;
        ApplyFilters();
    }

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
        historyChart.SetData("장기 위험 후보 추세 (맵·인원·마네킹 수별 계열)", filtered.OrderBy(summary => summary.TestRunId).Select((summary, index) => new ChartPoint($"{summary.Map} P{summary.Players} M{summary.Mannequins} T{summary.Tuning}", index, summary.RiskCount)));
        ShowSelectedRun();
    }

    private RunSummary? SelectedRun() => reportsGrid.CurrentRow?.DataBoundItem as RunSummary;

    private void ShowSelectedRun()
    {
        var summary = SelectedRun();
        if (summary is null) return;
        var selectedReports = reports.Where(report => report.TestRunId == summary.TestRunId).ToList();
        var samples = LoadPerformance(selectedReports);
        var events = LoadEvents(selectedReports);
        var mismatches = FindMultiplayerMismatchCandidates(events);
        var roleSummary = string.Join(Environment.NewLine, samples.GroupBy(sample => sample.Role).Select(group =>
            $"{group.Key}: FPS avg={group.Average(item => item.Fps):F1}, Game/Draw/GPU max={group.Max(item => item.GameMs):F2}/{group.Max(item => item.DrawMs):F2}/{group.Max(item => item.GpuMs):F2} ms, " +
            $"Memory max={group.Max(item => item.MemoryMiB):F1} MiB, Network in/out max={group.Max(item => item.InKiB):F1}/{group.Max(item => item.OutKiB):F1} KiB/s, Connections max={group.Max(item => item.Connections)}"));
        details.Text = $"Run: {summary.TestRunId}{Environment.NewLine}Outcome: {summary.Outcome}{Environment.NewLine}Roles: {summary.Roles}{Environment.NewLine}" +
            $"Map / GameMode: {summary.Map} / {summary.GameMode}{Environment.NewLine}Players / Mannequins: {summary.Players} / {summary.Mannequins}{Environment.NewLine}" +
            $"Tuning fingerprint: {summary.Tuning}{Environment.NewLine}Risk candidates: {summary.RiskCount}, State mismatches: {mismatches.Count}{Environment.NewLine}{Environment.NewLine}" +
            $"Performance by role:{Environment.NewLine}{roleSummary}{Environment.NewLine}{Environment.NewLine}" +
            $"Derived artifacts:{Environment.NewLine}{Path.Combine(summary.RunDirectory, "CombinedReport.xml")}{Environment.NewLine}{Path.Combine(summary.RunDirectory, "MergedEvents.csv")}{Environment.NewLine}{Path.Combine(summary.RunDirectory, "MergedPerformance.csv")}";
        timelineGrid.DataSource = events.Concat(mismatches).OrderBy(item => item.UtcTime, StringComparer.Ordinal).ThenBy(item => item.Role).ToList();
        UpdateRunChart();
    }

    private void UpdateRunChart()
    {
        var summary = SelectedRun();
        if (summary is null) return;
        var samples = LoadPerformance(reports.Where(report => report.TestRunId == summary.TestRunId));
        var metric = metricSelector.SelectedItem?.ToString() ?? "FPS";
        double Value(PerformanceSample sample) => metric switch
        {
            "Frame ms" => sample.FrameMs, "Game thread ms" => sample.GameMs, "Draw thread ms" => sample.DrawMs,
            "GPU ms" => sample.GpuMs, "Memory MiB" => sample.MemoryMiB, "Memory growth MiB/s" => sample.MemoryGrowthMiB,
            "Inbound KiB/s" => sample.InKiB, "Outbound KiB/s" => sample.OutKiB, "AI tick ms" => sample.AiTickMs,
            "Line traces" => sample.LineTraces, "Path failures" => sample.PathFailures, _ => sample.Fps
        };
        runChart.SetData($"{summary.TestRunId} — {metric}", samples.Select(sample => new ChartPoint(sample.Role, sample.GameSeconds, Value(sample))));
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
            MessageBox.Show(this, "UnrealInsights.exe was not found under UE 5.8.", Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        Process.Start(new ProcessStartInfo { FileName = insights, Arguments = $"\"{trace}\"", UseShellExecute = true });
    }

    private async Task VerifySelectedRunAsync(Button button)
    {
        var summary = SelectedRun();
        if (summary is null) return;
        var projectRoot = FindProjectRoot(rootPath.Text);
        var projectFile = projectRoot is null ? null : Path.Combine(projectRoot, "ProjectProject01.uproject");
        if (projectFile is null || !File.Exists(projectFile))
        {
            MessageBox.Show(this, "ProjectProject01.uproject could not be found above the Diagnostics directory.", Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        button.Enabled = false;
        try
        {
            var verificationDirectory = Path.Combine(summary.RunDirectory, "Verification");
            Directory.CreateDirectory(verificationDirectory);
            const string buildTool = @"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat";
            const string editorCmd = @"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe";
            var build = await RunProcessAsync(buildTool, ["ProjectProject01Editor", "Win64", "Development", $"-Project={projectFile}", "-WaitMutex", "-NoHotReload"]);
            File.WriteAllText(Path.Combine(verificationDirectory, "Build.log"), build.Output, Encoding.UTF8);
            ProcessResult automation = new(-1, "Automation was not started because the build failed.");
            var automationReport = Path.Combine(verificationDirectory, "AutomationReport");
            if (build.ExitCode == 0)
            {
                automation = await RunProcessAsync(editorCmd, [projectFile, "-unattended", "-nop4", "-nosplash", "-nullrhi", "-TestExit=Automation Test Queue Empty", "-ExecCmds=Automation RunTests ProjectProject01.;Quit", $"-ReportOutputPath={automationReport}"]);
            }
            File.WriteAllText(Path.Combine(verificationDirectory, "Automation.log"), automation.Output, Encoding.UTF8);
            var automationErrors = -1;
            var automationSuccesses = 0;
            var automationIndex = Path.Combine(automationReport, "index.json");
            if (File.Exists(automationIndex))
            {
                using var automationJson = JsonDocument.Parse(File.ReadAllText(automationIndex));
                automationErrors = automationJson.RootElement.TryGetProperty("failed", out var failed) ? failed.GetInt32() : -1;
                automationSuccesses = automationJson.RootElement.TryGetProperty("succeeded", out var succeeded) ? succeeded.GetInt32() : 0;
            }
            var buildStatus = build.ExitCode == 0 ? "Succeeded" : "Failed";
            var automationStatus = automation.ExitCode == 0 && automationErrors == 0 && automationSuccesses > 0 ? "Succeeded" : "Failed";
            var roleOutcomesPass = reports.Where(report => report.TestRunId == summary.TestRunId).All(report => report.Outcome == "Pass");
            var finalOutcome = buildStatus == "Succeeded" && automationStatus == "Succeeded" && roleOutcomesPass ? "Pass" : "ReviewRequired";
            new XDocument(new XElement("Verification", new XAttribute("TestRunId", summary.TestRunId), new XAttribute("FinalOutcome", finalOutcome),
                new XElement("Build", new XAttribute("Status", buildStatus), new XAttribute("ExitCode", build.ExitCode), new XAttribute("Log", "Verification/Build.log")),
                new XElement("Automation", new XAttribute("Status", automationStatus), new XAttribute("ExitCode", automation.ExitCode), new XAttribute("SucceededCount", automationSuccesses), new XAttribute("FailedCount", automationErrors), new XAttribute("Log", "Verification/Automation.log"))))
                .Save(Path.Combine(summary.RunDirectory, "Verification.xml"));
            LoadReports(rootPath.Text);
            MessageBox.Show(this, $"Build={buildStatus}, Automation={automationStatus}, Final={finalOutcome}", Text, MessageBoxButtons.OK,
                finalOutcome == "Pass" ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
        }
        catch (Exception exception)
        {
            MessageBox.Show(this, exception.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally { button.Enabled = true; }
    }

    internal static async Task<ProcessResult> RunProcessAsync(string fileName, IReadOnlyList<string> arguments)
    {
        var startInfo = new ProcessStartInfo { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true };
        if (Path.GetExtension(fileName).Equals(".bat", StringComparison.OrdinalIgnoreCase))
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
        var output = new StringBuilder();
        process.OutputDataReceived += (_, args) => { if (args.Data is not null) output.AppendLine(args.Data); };
        process.ErrorDataReceived += (_, args) => { if (args.Data is not null) output.AppendLine(args.Data); };
        if (!process.Start()) return new ProcessResult(-1, "Process could not be started.");
        process.BeginOutputReadLine(); process.BeginErrorReadLine();
        await process.WaitForExitAsync();
        return new ProcessResult(process.ExitCode, output.ToString());
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
                result.Add(new PerformanceSample(
                    Get(row, "UtcTime"), Get(row, "Role", report.Role), Get(row, "NetMode", report.NetMode), Number(row, "GameSeconds"), Number(row, "FramesPerSecond"), Number(row, "FrameMilliseconds"),
                    Number(row, "GameThreadMilliseconds"), Number(row, "DrawThreadMilliseconds"), Number(row, "GpuMilliseconds"), Number(row, "UsedPhysicalBytes") / 1048576.0,
                    Number(row, "MemoryBytesPerSecondDelta") / 1048576.0, Number(row, "InBytesPerSecond") / 1024.0, Number(row, "OutBytesPerSecond") / 1024.0,
                    Integer(row, "PlayerCount"), Integer(row, "MannequinCount"), Integer(row, "HelperCount"), Integer(row, "PathFailureCount"), Integer(row, "VisionCheckCount"),
                    Integer(row, "LineTraceCount"), Number(row, "AITickMilliseconds"), Integer(row, "NetworkConnectionCount")));
            }
        }
        return result.OrderBy(sample => sample.UtcTime, StringComparer.Ordinal).ThenBy(sample => sample.Role).ToList();
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
                var reasons = CompareActorStates(source.Event.Message, match.Event.Message);
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
            if (slot.Success) return $"Mannequin:Slot{slot.Groups[1].Value}";
        }
        var actor = Regex.Replace(item.Actor, @"UEDPIE_\d+_", string.Empty, RegexOptions.IgnoreCase);
        return actor.Length > 0 ? $"{type}:{actor.Split('.').Last()}" : type;
    }

    private static List<string> CompareActorStates(string serverMessage, string clientMessage)
    {
        var server = ParseState(serverMessage);
        var client = ParseState(clientMessage);
        var reasons = new List<string>();
        if (TryVector(server, "Location", out var serverPosition) && TryVector(client, "Location", out var clientPosition))
        {
            var distance = Math.Sqrt(Math.Pow(serverPosition.X - clientPosition.X, 2) + Math.Pow(serverPosition.Y - clientPosition.Y, 2) + Math.Pow(serverPosition.Z - clientPosition.Z, 2));
            if (distance > 150) reasons.Add($"PositionDelta={distance:F1}cm");
        }
        foreach (var key in new[] { "Manual", "Frozen", "Command", "Target", "Retreat" })
        {
            if (server.TryGetValue(key, out var a) && client.TryGetValue(key, out var b) && a != b) reasons.Add($"{key} server={a} client={b}");
        }
        if (server.TryGetValue("Controller", out var serverController) && client.TryGetValue("Controller", out var clientController) && ControllerKind(serverController) != ControllerKind(clientController))
            reasons.Add($"Controller server={ControllerKind(serverController)} client={ControllerKind(clientController)}");
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
            var eventLines = new List<string> { "UtcTime,Role,NetMode,GameSeconds,Severity,Code,Message,Actor" };
            eventLines.AddRange(mergedEvents.Select(item => string.Join(',', EscapeCsv(item.UtcTime), EscapeCsv(item.Role), EscapeCsv(item.NetMode), item.GameSeconds.ToString("F3", CultureInfo.InvariantCulture), EscapeCsv(item.Severity), EscapeCsv(item.Code), EscapeCsv(item.Message), EscapeCsv(item.Actor))));
            File.WriteAllLines(Path.Combine(runDirectory, "MergedEvents.csv"), eventLines, new UTF8Encoding(true));

            var performanceLines = new List<string> { "UtcTime,Role,NetMode,GameSeconds,FramesPerSecond,FrameMilliseconds,GameThreadMilliseconds,DrawThreadMilliseconds,GpuMilliseconds,UsedPhysicalMiB,MemoryGrowthMiBPerSecond,InKiBPerSecond,OutKiBPerSecond,PlayerCount,MannequinCount,HelperCount,PathFailureCount,VisionCheckCount,LineTraceCount,AITickMilliseconds,NetworkConnectionCount" };
            performanceLines.AddRange(samples.Select(item => string.Join(',', EscapeCsv(item.UtcTime), EscapeCsv(item.Role), EscapeCsv(item.NetMode), Format(item.GameSeconds), Format(item.Fps), Format(item.FrameMs), Format(item.GameMs), Format(item.DrawMs), Format(item.GpuMs), Format(item.MemoryMiB), Format(item.MemoryGrowthMiB), Format(item.InKiB), Format(item.OutKiB), item.Players, item.Mannequins, item.Helpers, item.PathFailures, item.VisionChecks, item.LineTraces, Format(item.AiTickMs), item.Connections)));
            File.WriteAllLines(Path.Combine(runDirectory, "MergedPerformance.csv"), performanceLines, new UTF8Encoding(true));

            var verificationPath = Path.Combine(runDirectory, "Verification.xml");
            var verification = File.Exists(verificationPath) ? XDocument.Load(verificationPath).Root : null;
            var roleRiskCount = runReports.Sum(report => report.RiskCount);
            var hasTimeout = runReports.Any(report => report.Outcome == "TimedOut");
            var rolePassed = runReports.All(report => report.Outcome == "Pass");
            var verificationPassed = verification?.Attribute("FinalOutcome")?.Value == "Pass";
            var runtimeNeedsReview = runReports.Any(report => report.Outcome == "ReviewRequired");
            var combinedOutcome = hasTimeout ? "TimedOut" : runtimeNeedsReview || mismatches.Count > 0 ? "ReviewRequired" : verification is null ? "Inconclusive" : rolePassed && verificationPassed ? "Pass" : "ReviewRequired";
            var combined = new XDocument(new XElement("CombinedTestReport", new XAttribute("TestRunId", runReports[0].TestRunId), new XAttribute("Outcome", combinedOutcome),
                new XAttribute("Map", string.Join(",", runReports.Select(report => report.Map).Distinct())), new XAttribute("GameMode", string.Join(",", runReports.Select(report => report.GameMode).Distinct())),
                new XAttribute("RoleCount", runReports.Count), new XAttribute("RiskCandidateCount", roleRiskCount + mismatches.Count), new XAttribute("StateMismatchCount", mismatches.Count),
                new XElement("Roles", runReports.Select(report => new XElement("Role", new XAttribute("Name", report.Role), new XAttribute("NetMode", report.NetMode), new XAttribute("Outcome", report.Outcome), new XAttribute("Report", Path.GetRelativePath(runDirectory, report.FilePath))))),
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

    private static string? FindProjectRoot(string diagnosticsPath)
    {
        var directory = new DirectoryInfo(diagnosticsPath);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "ProjectProject01.uproject"))) return directory.FullName;
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
