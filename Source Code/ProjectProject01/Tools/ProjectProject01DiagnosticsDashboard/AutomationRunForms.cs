namespace ProjectProject01DiagnosticsDashboard;

internal sealed class BotSimulationOptionsForm : Form
{
    internal AutomationRunPlan? Plan { get; private set; }
    internal BotSimulationOptionsForm()
    {
        Text = "봇 경기 시뮬레이션";
        AutoScaleDimensions = new SizeF(96, 96); AutoScaleMode = AutoScaleMode.Dpi;
        AutoSize = true; AutoSizeMode = AutoSizeMode.GrowAndShrink;
        FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = MinimizeBox = false; StartPosition = FormStartPosition.CenterParent;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(16), ColumnCount = 2, RowCount = 4 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize)); layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        for (int i = 0; i < 4; ++i) layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var scenario = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill, MinimumSize = new Size(300, 0) };
        foreach (string key in AutomationRunPlan.Scenarios)
        {
            string title = key.StartsWith("Level", StringComparison.Ordinal) ? $"양쪽 난이도 {key[^1]}" :
                key.StartsWith("Ladder.M", StringComparison.Ordinal) ? $"마네킹 {key[8]} / 생존자 3" : $"마네킹 3 / 생존자 {key[10]}";
            scenario.Items.Add(title);
        }
        var first = new NumericUpDown { Minimum = 1, Maximum = 20, Value = 1, Dock = DockStyle.Fill };
        var count = new NumericUpDown { Minimum = 1, Maximum = 20, Value = 5, Dock = DockStyle.Fill };
        void Limits() { int max = scenario.SelectedIndex < 5 ? 20 : 30; first.Maximum = max; count.Maximum = max - first.Value + 1; }
        scenario.SelectedIndexChanged += (_, _) => Limits(); first.ValueChanged += (_, _) => Limits(); scenario.SelectedIndex = 0;
        layout.Controls.Add(new Label { Text = "경기 조합", AutoSize = true }, 0, 0); layout.Controls.Add(scenario, 1, 0);
        layout.Controls.Add(new Label { Text = "시작 회차", AutoSize = true }, 0, 1); layout.Controls.Add(first, 1, 1);
        layout.Controls.Add(new Label { Text = "반복 횟수", AutoSize = true }, 0, 2); layout.Controls.Add(count, 1, 2);
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = false, Padding = new Padding(0, 8, 0, 0), FlowDirection = FlowDirection.RightToLeft };
        var start = new Button { Text = "시작", AutoSize = true }; var cancel = new Button { Text = "취소", AutoSize = true, DialogResult = DialogResult.Cancel };
        start.Click += (_, _) => { Plan = AutomationRunPlan.Bot(AutomationRunPlan.Scenarios[scenario.SelectedIndex], (int)first.Value, (int)count.Value); DialogResult = DialogResult.OK; };
        buttons.Controls.AddRange([start, cancel]); layout.Controls.Add(buttons, 0, 3); layout.SetColumnSpan(buttons, 2); Controls.Add(layout);
        AcceptButton = start; CancelButton = cancel;
    }
}

internal sealed class AutomationProgressForm : Form
{
    private readonly Label status = new() { Dock = DockStyle.Fill, Padding = new Padding(16), AutoEllipsis = true };
    private readonly Button cancel = new() { Text = "검사 취소", Dock = DockStyle.Bottom, Height = 34 };
    private readonly CancellationTokenSource cancellation;
    private readonly System.Windows.Forms.Timer elapsed = new() { Interval = 1000 };
    private bool finished;
    internal AutomationProgressForm(string title, CancellationToken lifetime, Func<CancellationToken, IProgress<string>, Task> run)
    {
        AutoScaleDimensions = new SizeF(96, 96); AutoScaleMode = AutoScaleMode.Dpi;
        Text = title; ClientSize = new Size(660, 140); StartPosition = FormStartPosition.CenterParent; MaximizeBox = MinimizeBox = false;
        cancellation = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        var started = System.Diagnostics.Stopwatch.StartNew();
        elapsed.Tick += (_, _) => Text = title + " · 경과 " + started.Elapsed.ToString(@"hh\:mm\:ss");
        elapsed.Start();
        Controls.Add(status); Controls.Add(cancel);
        void Cancel() { cancellation.Cancel(); cancel.Enabled = false; status.Text = "검사를 취소하고 실행 프로세스를 정리하는 중입니다."; }
        cancel.Click += (_, _) => Cancel();
        FormClosing += (_, e) => { if (!finished) { e.Cancel = true; Cancel(); } };
        Shown += async (_, _) =>
        {
            var progress = new Progress<string>(message => { if (!IsDisposed && !cancellation.IsCancellationRequested) status.Text = message; });
            try { await run(cancellation.Token, progress); }
            finally { finished = true; Close(); }
        };
    }
    protected override void Dispose(bool disposing) { if (disposing) { elapsed.Dispose(); cancellation.Dispose(); } base.Dispose(disposing); }
}
