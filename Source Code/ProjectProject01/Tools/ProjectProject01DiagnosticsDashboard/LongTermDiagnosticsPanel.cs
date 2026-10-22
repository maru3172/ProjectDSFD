using System.Globalization;
using System.Text.Json;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed record HistoryMetricChoice(string Key)
{
    public override string ToString() => LongTermDiagnostics.Label(Key);
}

internal sealed class LongTermDiagnosticsPanel : UserControl
{
    private readonly ComboBox metric = new() { Dock = DockStyle.Top, DropDownStyle = ComboBoxStyle.DropDownList, DropDownWidth = 1000 };
    private readonly ComboBox aggregation = new() { Width = 145, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox sourceFilter = new() { Width = 100, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox categoryFilter = new() { Width = 210, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TrendChart chart = new() { Dock = DockStyle.Fill };
    private readonly DataGridView grid = new() { Dock = DockStyle.Fill, ReadOnly = true, AllowUserToAddRows = false,
        AllowUserToDeleteRows = false, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.DisplayedCells, SelectionMode = DataGridViewSelectionMode.FullRowSelect };
    private readonly TextBox evidence = new() { Dock = DockStyle.Bottom, Height = 74, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly Label status = new() { AutoSize = true, Text = "게임: 기존 진단 폴더에서 자동 연결 · 백엔드: 원본 기록 가져오기 · 요약 저장으로 장기 보관" };
    private readonly Dictionary<string, DiagnosticHistoryCapture> loaded = [];
    private readonly Dictionary<string, DiagnosticHistoryCapture> game = [];
    private bool busy;
    public LongTermDiagnosticsPanel()
    {
        Dock = DockStyle.Fill;
        sourceFilter.Items.AddRange(["전체 범위", "게임", "백엔드"]); sourceFilter.SelectedIndex = 0;
        categoryFilter.Items.Add("전체 영역");
        categoryFilter.Items.AddRange(LongTermDiagnostics.KnownKeys.Select(LongTermDiagnostics.Category).Distinct().Order().ToArray()); categoryFilter.SelectedIndex = 0;
        aggregation.Items.AddRange(["권장 집계", "평균", "최대", "최소", "마지막", "합계·관측 증가량"]); aggregation.SelectedIndex = 0;
        var open = new Button { Text = "백엔드 원본 기록 가져오기", AutoSize = true };
        var load = new Button { Text = "비교 요약 열기", AutoSize = true };
        var save = new Button { Text = "비교 요약 저장", AutoSize = true };
        var top = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 65, AutoScroll = true };
        top.Controls.AddRange([sourceFilter, categoryFilter, aggregation, open, load, save, status]);
        var split = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 180 };
        split.Panel1.Controls.Add(chart); split.Panel2.Controls.Add(grid);
        Controls.Add(split); Controls.Add(evidence); Controls.Add(metric); Controls.Add(top);
        open.Click += async (_, _) => await Import(false);
        load.Click += async (_, _) => await Import(true);
        save.Click += (_, _) => Save();
        aggregation.SelectedIndexChanged += (_, _) => Draw(); metric.SelectedIndexChanged += (_, _) => Draw();
        sourceFilter.SelectedIndexChanged += (_, _) => RefreshMetrics(); categoryFilter.SelectedIndexChanged += (_, _) => RefreshMetrics();
        grid.SelectionChanged += (_, _) => ShowEvidence();
        RefreshMetrics();
    }
    internal void SetGameCaptures(IEnumerable<DiagnosticHistoryCapture> captures)
    {
        var incoming = captures.Take(DiagnosticHistoryFiles.MaximumCaptures + 1).ToArray();
        if (incoming.Length > DiagnosticHistoryFiles.MaximumCaptures)
        {
            game.Clear(); status.Text = "게임 역할별 캡처가 2,048개를 초과합니다. 맵·결과·검색 필터로 비교 범위를 줄이세요. 일부만 표시하지 않았습니다.";
        }
        else { game.Clear(); foreach (var capture in incoming) game[capture.Id] = capture; }
        RefreshMetrics();
    }
    // Live samples are deliberately not silently mixed with imported records. Import original files
    // after stopping capture to include the complete available ring, rather than the 720-point display window.
    private List<DiagnosticHistoryCapture> Captures() => loaded.Values.Concat(game.Values)
        .GroupBy(c => c.Id).Select(g => g.Last()).OrderBy(c => c.Start).ThenBy(c => c.Id, StringComparer.Ordinal).ToList();
    private async Task Import(bool archive)
    {
        if (busy) return;
        using var dialog = new OpenFileDialog { Multiselect = !archive,
            Filter = archive ? "장기 비교 요약 (*.diagnostic-history.json)|*.diagnostic-history.json|JSON|*.json" : "백엔드 원본 (*.jsonl)|*.jsonl" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        busy = true;
        try
        {
            status.Text = "기록 집계 중…";
            var imported = await Task.Run(() => archive ? DiagnosticHistoryFiles.Load(dialog.FileName) : DiagnosticHistoryFiles.ReadBackend(dialog.FileNames));
            if (IsDisposed) return;
            if (loaded.Keys.Union(game.Keys).Union(imported.Select(c => c.Id)).Count() > DiagnosticHistoryFiles.MaximumCaptures)
                throw new InvalidDataException("최대 2,048개 캡처입니다. 비교 파일을 나눠 사용하세요.");
            // Replace the same capture as one unit: overlapping files never add the same requests twice.
            foreach (var capture in imported) loaded[capture.Id] = capture;
            RefreshMetrics(); status.Text = $"{imported.Count}개 역할·프로세스 캡처 반영 · 같은 ID는 교체 · 원본 구간/표본 수를 아래에서 확인하세요.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or InvalidDataException)
        { if (!IsDisposed) status.Text = "가져오기 실패: " + ex.Message; }
        finally { busy = false; }
    }
    private void Save()
    {
        if (busy) return;
        using var dialog = new SaveFileDialog { Filter = "장기 비교 요약|*.diagnostic-history.json", FileName = "Comparison.diagnostic-history.json" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try { DiagnosticHistoryFiles.Save(dialog.FileName, Captures()); status.Text = "비교 요약 저장 완료 · 다음 실행에서 ‘비교 요약 열기’로 이전 측정과 비교하세요."; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
        { status.Text = "저장 실패: " + ex.Message; }
    }
    private void RefreshMetrics()
    {
        string? selected = (metric.SelectedItem as HistoryMetricChoice)?.Key;
        var keys = LongTermDiagnostics.KnownKeys.Concat(Captures().SelectMany(c => c.Metrics.Keys)).Distinct()
            .Where(k => sourceFilter.SelectedIndex == 0 || k.StartsWith(sourceFilter.SelectedIndex == 1 ? "Game | " : "Backend | ", StringComparison.Ordinal))
            .Where(k => categoryFilter.SelectedIndex == 0 || LongTermDiagnostics.Category(k) == categoryFilter.SelectedItem as string)
            .OrderBy(k => k, StringComparer.Ordinal).ToArray();
        metric.BeginUpdate();
        try
        {
            metric.Items.Clear(); metric.Items.AddRange(keys.Select(k => new HistoryMetricChoice(k)).ToArray());
            if (metric.Items.Count > 0) metric.SelectedIndex = Math.Max(0, Array.IndexOf(keys, selected));
        }
        finally { metric.EndUpdate(); }
        Draw();
    }
    private void Draw()
    {
        if (metric.SelectedItem is not HistoryMetricChoice choice) return;
        string mode = aggregation.SelectedItem as string ?? "권장 집계";
        string source = choice.Key.StartsWith("Game | ", StringComparison.Ordinal) ? "Game" : "Backend";
        var rows = Captures().Where(c => c.Source == source).Select((c, i) =>
        {
            c.Metrics.TryGetValue(choice.Key, out var stat);
            double? number = stat?.Select(mode);
            return new HistoryDisplayRow(i + 1, c.Start.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss"), c.Scope, c.Samples,
                stat?.ValidSamples ?? 0, number?.ToString("0.######", CultureInfo.InvariantCulture) ?? "측정 불가 / 해당 집계 미지원",
                c.Context, c.Id, c.End.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss"), number);
        }).ToList();
        grid.DataSource = rows;
        if (grid.Columns.Contains(nameof(HistoryDisplayRow.Number))) grid.Columns[nameof(HistoryDisplayRow.Number)]!.Visible = false;
        // A missing capture breaks the line rather than implying that an unmeasured interval was observed.
        List<ChartPoint> points = [];
        foreach (var group in rows.GroupBy(r => r.범위))
        {
            int segment = 0;
            foreach (var row in group)
            {
                if (!row.Number.HasValue) { ++segment; continue; }
                points.Add(new ChartPoint(group.Key + (segment == 0 ? "" : " #" + segment), row.순서, row.Number.Value));
            }
        }
        chart.SetData($"{choice} · {mode} · 가로축: 시간순 캡처 번호 (세부 시각은 아래 표)", points);
        ShowEvidence();
    }
    private void ShowEvidence()
    {
        string? id = (grid.CurrentRow?.DataBoundItem as HistoryDisplayRow)?.ID;
        var capture = id is null ? null : Captures().FirstOrDefault(c => c.Id == id);
        evidence.Text = "공유 PIE 프로세스 지표는 역할끼리 합산하지 않습니다. 단일 요약 지표는 ‘권장 집계’에서 확인하세요.\r\n" +
            (capture is null ? "원본 또는 비교 요약을 불러오세요. 값이 없는 항목은 정상 0으로 간주하지 않습니다." :
            $"관측 구간 {capture.Start:O} ~ {capture.End:O} · {capture.Samples}표본\r\n" + string.Join("\r\n", capture.Notes));
    }
    internal IReadOnlyList<string> MetricKeysForTest => metric.Items.Cast<HistoryMetricChoice>().Select(c => c.Key).ToArray();
    internal void LoadForTest(IEnumerable<DiagnosticHistoryCapture> captures) { foreach (var capture in captures) loaded[capture.Id] = capture; RefreshMetrics(); }
    internal void SelectForTest(string key, string mode) { metric.SelectedItem = metric.Items.Cast<HistoryMetricChoice>().First(c => c.Key == key); aggregation.SelectedItem = mode; Draw(); }
    internal int VisibleRowsForTest => grid.Rows.Count;
    internal void FilterForTest(string source, string category) { sourceFilter.SelectedItem = source; categoryFilter.SelectedItem = category; }
    internal sealed record HistoryDisplayRow(int 순서, string 시작시각, string 범위, int 전체표본, int 유효표본, string 값, string 조건, string ID, string 종료시각, double? Number);
}
