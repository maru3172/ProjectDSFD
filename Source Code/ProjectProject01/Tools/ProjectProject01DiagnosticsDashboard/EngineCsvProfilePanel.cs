// File: Tools/ProjectProject01DiagnosticsDashboard/cs
// Target: Win64 .NET 8 Windows desktop companion for Unreal Engine 5.8.2 diagnostic artifacts
// Lightweight viewer for engine CSV Profiler captures. Nothing runs until the tab is opened; a file is read
// only when selected, on a background task, and the reduced result is cached by path + size + write time.

using System.Globalization;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed class EngineCsvProfilePanel : UserControl
{
    private const int MaxListedFiles = 200;
    private const int MaxCachedResults = 4;
    private readonly TextBox folder = new() { Width = 420, ReadOnly = true };
    private readonly Label status = new() { AutoSize = true, Padding = new Padding(6, 7, 0, 0), Text = "탭을 열면 Saved/Profiling/CSV 목록만 읽습니다." };
    private readonly DataGridView files = Grid();
    private readonly DataGridView stats = Grid();
    private readonly ComboBox group = new() { Dock = DockStyle.Top, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox summary = new() { Dock = DockStyle.Fill, ReadOnly = true, Multiline = true, ScrollBars = ScrollBars.Both, WordWrap = false, Font = new Font(FontFamily.GenericMonospace, 9f) };
    private readonly TrendChart chart = new() { Dock = DockStyle.Fill };
    private readonly LinkedList<(string Key, EngineCsvProfileResult Result)> cache = new();
    private IReadOnlyList<(string RunId, DateTimeOffset First, DateTimeOffset Last)> runs = [];
    private EngineCsvProfileResult? current;
    private string? diagnosticsRoot;
    private CancellationTokenSource? loading;
    private bool listed;

    public EngineCsvProfilePanel()
    {
        Dock = DockStyle.Fill;
        var choose = new Button { Text = "폴더 선택", AutoSize = true };
        var refresh = new Button { Text = "목록 새로고침", AutoSize = true };
        var top = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 36, WrapContents = false, Padding = new Padding(3) };
        top.Controls.AddRange([new Label { Text = "CSV 폴더", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, folder, choose, refresh, status]);
        group.Items.AddRange(EngineCsvProfile.Groups.Select(g => (object)g.Label).ToArray());
        group.SelectedIndex = 0;
        var tabs = new TabControl { Dock = DockStyle.Fill };
        tabs.TabPages.Add(new TabPage("요약·병목 추정") { Controls = { summary } });
        var statsPage = new TabPage("항목별 평균 시간"); statsPage.Controls.Add(stats); statsPage.Controls.Add(group); tabs.TabPages.Add(statsPage);
        tabs.TabPages.Add(new TabPage("프레임 그래프") { Controls = { chart } });
        var split = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Vertical, FixedPanel = FixedPanel.Panel1 };
        split.Panel1.Controls.Add(files); split.Panel2.Controls.Add(tabs);
        Controls.Add(split); Controls.Add(top);
        Layout += (_, _) => { if (split.Width > 700 && split.SplitterDistance != 360) split.SplitterDistance = 360; };
        files.DataBindingComplete += (_, _) => { if (files.Columns.Contains("FullPath")) files.Columns["FullPath"]!.Visible = false; };
        files.SelectionChanged += async (_, _) => await LoadSelectedAsync();
        group.SelectedIndexChanged += (_, _) => ShowStats();
        refresh.Click += (_, _) => ListFiles();
        choose.Click += (_, _) =>
        {
            using var dialog = new FolderBrowserDialog { Description = "Saved/Profiling/CSV 선택" };
            if (dialog.ShowDialog(this) == DialogResult.OK) { folder.Text = dialog.SelectedPath; ListFiles(); }
        };
        VisibleChanged += (_, _) => { if (Visible && !listed) ListFiles(); };
    }

    private static DataGridView Grid() => new()
    {
        Dock = DockStyle.Fill, ReadOnly = true, AutoGenerateColumns = true, SelectionMode = DataGridViewSelectionMode.FullRowSelect, MultiSelect = false,
        AllowUserToAddRows = false, AllowUserToDeleteRows = false, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.DisplayedCells
    };

    // Called by the dashboard after diagnostics are (re)loaded. Cheap: stores the folder and run windows only.
    internal void SetContext(string? diagnosticsRoot, IEnumerable<(string RunId, DateTimeOffset First, DateTimeOffset Last)> runWindows)
    {
        runs = runWindows.ToList();
        if (diagnosticsRoot is not null)
        {
            bool changed = !string.Equals(this.diagnosticsRoot, diagnosticsRoot, StringComparison.OrdinalIgnoreCase);
            this.diagnosticsRoot = diagnosticsRoot;
            if (folder.Text.Length == 0) folder.Text = Path.GetFullPath(Path.Combine(diagnosticsRoot, "..", "Profiling", "CSV"));
            if (changed && listed) ListFiles();
        }
        if (current is not null) ShowResult(current);
    }

    private void ListFiles()
    {
        listed = true;
        try
        {
            // Diagnostics-run captures (Start/Stop menu) and manual CsvProfile captures, newest first.
            var runRows = diagnosticsRoot is null ? new List<EngineCsvFileRow>() : EngineCsvProfile.ListRunCsvFiles(diagnosticsRoot, MaxListedFiles);
            var rows = runRows.Concat(EngineCsvProfile.ListCsvFiles(folder.Text, MaxListedFiles))
                .OrderByDescending(r => r.수정시각, StringComparer.Ordinal).Take(MaxListedFiles).ToList();
            files.DataSource = rows;
            status.Text = rows.Count > 0 ? $"CSV {rows.Count}개 (진단 실행 {runRows.Count}개 포함, 최근 {MaxListedFiles}개까지) · 선택한 파일만 읽음"
                : "CSV가 없습니다. 진단 Start/Stop 또는 CsvProfile Start/Stop 후 생성됩니다.";
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { status.Text = "목록 읽기 실패: " + e.Message; }
    }

    private static string CacheKey(FileInfo info) => $"{info.FullName}|{info.Length}|{info.LastWriteTimeUtc.Ticks}";

    private async Task LoadSelectedAsync()
    {
        if (files.CurrentRow?.DataBoundItem is not EngineCsvFileRow row) return;
        loading?.Cancel();
        var info = new FileInfo(row.FullPath);
        if (!info.Exists) { status.Text = "파일이 사라졌습니다."; return; }
        string key = CacheKey(info);
        var hit = cache.FirstOrDefault(c => c.Key == key);
        if (hit.Result is not null) { cache.Remove(hit); cache.AddFirst(hit); ShowResult(hit.Result); return; }
        var cts = loading = new CancellationTokenSource();
        status.Text = $"읽는 중: {row.파일}";
        try
        {
            var result = await Task.Run(() => EngineCsvProfile.Load(row.FullPath, cts.Token), cts.Token);
            if (IsDisposed || cts.IsCancellationRequested) return;
            cache.AddFirst((key, result));
            while (cache.Count > MaxCachedResults) cache.RemoveLast();
            ShowResult(result);
            status.Text = $"읽음: {row.파일} · {result.Frames} 프레임 · 열 {result.Stats.Count}개";
        }
        catch (OperationCanceledException) { }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            if (!IsDisposed) { status.Text = "읽기 실패"; summary.Text = e.Message; stats.DataSource = null; chart.SetData("자료 없음", []); }
        }
    }

    private void ShowResult(EngineCsvProfileResult result)
    {
        current = result;
        var linked = EngineCsvProfile.OverlappingRuns(result, runs);
        if (EngineCsvProfile.RunIdFromPath(result.FilePath, diagnosticsRoot) is { } ownRun && !linked.Contains(ownRun)) linked.Insert(0, ownRun);
        var (_, bottleneckRows) = EngineCsvProfile.Bottleneck(result);
        summary.Text = EngineCsvProfile.Summary(result, linked) + Environment.NewLine + Environment.NewLine +
            string.Join(Environment.NewLine, bottleneckRows.Select(r => $"  {r.항목,-14} {r.평균,10} ms   ({r.최대})"));
        ShowStats();
        var points = new List<ChartPoint>();
        foreach (var name in new[] { "FrameTime", "GameThreadTime", "GPUTime" })
            if (result.KeyFrames.TryGetValue(name, out var values)) points.AddRange(EngineCsvProfile.BucketPeaks(name, values));
        chart.SetData("프레임별 시간 (ms) · FrameTime / GameThreadTime / GPUTime", points);
    }

    private void ShowStats()
    {
        stats.DataSource = current is null ? null : EngineCsvProfile.Top(current, group.SelectedIndex);
    }

    internal EngineCsvProfileResult? LoadForTest(string path)
    {
        var result = EngineCsvProfile.Load(path, CancellationToken.None);
        ShowResult(result);
        return result;
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) { loading?.Cancel(); loading?.Dispose(); cache.Clear(); }
        base.Dispose(disposing);
    }
}
