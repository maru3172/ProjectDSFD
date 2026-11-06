using System.Diagnostics;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed class WorkReviewForm : Form
{
    private sealed record AreaChoice(WorkArea? Area, string Text)
    {
        public override string ToString() => Text;
    }

    private readonly WorkReviewReport report;
    private readonly ListBox areas = new() { Dock = DockStyle.Fill, IntegralHeight = false };
    private readonly CheckBox candidatesOnly = new() { Text = "확인 필요만", AutoSize = true, Anchor = AnchorStyles.Right };
    private readonly TextBox details = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly DataGridView table = new()
    {
        Dock = DockStyle.Fill, ReadOnly = true, AutoGenerateColumns = false, RowHeadersVisible = false,
        AllowUserToAddRows = false, AllowUserToDeleteRows = false, AllowUserToResizeRows = false,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect, MultiSelect = false,
        AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
        BackgroundColor = SystemColors.Window, BorderStyle = BorderStyle.FixedSingle
    };

    internal WorkReviewForm(WorkReviewReport report, string output)
    {
        this.report = report;
        Text = "작업 누락 검토 · 상세 결과";
        StartPosition = FormStartPosition.CenterParent;
        ClientSize = new Size(1040, 615);
        MinimumSize = new Size(680, 430);
        AutoScaleMode = AutoScaleMode.Font;

        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 4, Padding = new Padding(12) };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 260));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 60));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 40));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var header = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 2, Padding = new Padding(0, 0, 0, 8) };
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var summary = new Label { Text = report.Summary, Dock = DockStyle.Fill, AutoSize = true, Padding = new Padding(0, 4, 0, 4) };
        header.Controls.Add(summary, 0, 0);
        header.Controls.Add(candidatesOnly, 1, 0);
        layout.Controls.Add(header, 0, 0);
        layout.SetColumnSpan(header, 2);
        areas.Margin = new Padding(0, 0, 10, 0);
        layout.Controls.Add(areas, 0, 1);
        layout.SetRowSpan(areas, 2);

        table.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "상태", FillWeight = 18, MinimumWidth = 86, SortMode = DataGridViewColumnSortMode.NotSortable });
        table.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "항목", FillWeight = 34, MinimumWidth = 130, SortMode = DataGridViewColumnSortMode.NotSortable });
        table.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "경로", FillWeight = 48, MinimumWidth = 120, SortMode = DataGridViewColumnSortMode.NotSortable });
        layout.Controls.Add(table, 1, 1);
        layout.Controls.Add(details, 1, 2);

        var footer = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, Padding = new Padding(0, 7, 0, 0) };
        var close = new Button { Text = "닫기", AutoSize = true, DialogResult = DialogResult.Cancel };
        var folder = new Button { Text = "결과 폴더", AutoSize = true };
        folder.Click += (_, _) => OpenOutput(output);
        var textReport = new Button { Text = "텍스트 보고서", AutoSize = true };
        textReport.Click += (_, _) => OpenOutput(Path.Combine(output, "Details.txt"));
        footer.Controls.AddRange([folder, textReport, close]);
        layout.Controls.Add(footer, 0, 3);
        layout.SetColumnSpan(footer, 2);
        Controls.Add(layout);
        CancelButton = close;

        areas.Items.Add(new AreaChoice(null, $"전체  {report.Items.Count}"));
        foreach (WorkArea area in Enum.GetValues<WorkArea>())
        {
            int total = report.Items.Count(item => item.Area == area);
            int candidates = report.Items.Count(item => item.Area == area && item.Status != WorkStatus.Observed);
            areas.Items.Add(new AreaChoice(area, $"{WorkReviewReport.AreaName(area)}  {total}" + (candidates > 0 ? $"  ·  확인 {candidates}" : "")));
        }
        areas.SelectedIndexChanged += (_, _) => RefreshRows();
        candidatesOnly.CheckedChanged += (_, _) => RefreshRows();
        table.SelectionChanged += (_, _) => ShowItem(table.CurrentRow?.Tag as WorkReviewItem);
        areas.SelectedIndex = 0;
    }

    private void RefreshRows()
    {
        var area = (areas.SelectedItem as AreaChoice)?.Area;
        table.Rows.Clear();
        foreach (var item in report.Items.Where(item => (!area.HasValue || item.Area == area) &&
                     (!candidatesOnly.Checked || item.Status != WorkStatus.Observed)))
        {
            string path = item.Path + (item.Line is int line ? $":{line}" : "");
            int row = table.Rows.Add(WorkReviewReport.StatusName(item.Status), item.Name, path);
            table.Rows[row].Tag = item;
            table.Rows[row].Cells[2].ToolTipText = path;
        }
        if (table.Rows.Count > 0) table.CurrentCell = table.Rows[0].Cells[1];
        ShowItem(table.CurrentRow?.Tag as WorkReviewItem);
    }

    private void ShowItem(WorkReviewItem? item)
    {
        details.Text = item is null ? "선택한 영역에 표시할 항목이 없습니다." :
            $"{WorkReviewReport.AreaName(item.Area)}  /  {WorkReviewReport.StatusName(item.Status)}\r\n{item.Name}\r\n" +
            (item.Path.Length > 0 ? item.Path + (item.Line is int line ? $"  ·  {line}줄" : "") + "\r\n" : "") +
            "\r\n" + item.Detail.ReplaceLineEndings("\r\n");
        details.Select(0, 0);
    }

    private void OpenOutput(string path)
    {
        try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
        catch (Exception e) when (e is IOException or System.ComponentModel.Win32Exception)
        { MessageBox.Show(this, e.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning); }
    }
}
