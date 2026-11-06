namespace ProjectProject01DiagnosticsDashboard;

internal sealed class AutomationCatalogForm : Form
{
    private sealed record Group(string Key, string Label)
    {
        public override string ToString() => Label;
    }

    private readonly UnrealProject project;
    private readonly CancellationToken lifetime;
    private readonly TextBox search = new() { Dock = DockStyle.Fill, PlaceholderText = "검사 이름 또는 동작 검색 (예: 마네킹, 파트너, 길찾기)" };
    private readonly ComboBox groups = new() { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ListView list = new()
    {
        Dock = DockStyle.Fill, View = View.Details, CheckBoxes = true, FullRowSelect = true,
        HeaderStyle = ColumnHeaderStyle.Nonclickable, ShowItemToolTips = true
    };
    private readonly Label status = new() { Dock = DockStyle.Fill, AutoSize = true, UseMnemonic = false };
    private readonly Label counts = new() { Dock = DockStyle.Fill, AutoSize = true };
    private readonly Button reload = new() { Text = "목록 갱신", AutoSize = true };
    private readonly Button rebuild = new() { Text = "빌드 후 갱신", AutoSize = true };
    private readonly Button checkVisible = new() { Text = "현재 목록 모두 선택", AutoSize = true };
    private readonly Button clear = new() { Text = "선택 해제", AutoSize = true };
    private readonly Button start = new() { Text = "선택한 검사 실행", AutoSize = true, Enabled = false };
    private readonly HashSet<string> selected = new(StringComparer.OrdinalIgnoreCase);
    private string[] names = [];
    private CancellationTokenSource? load;
    private bool closing;
    private bool updating;
    internal AutomationRunPlan? Plan { get; private set; }

    internal AutomationCatalogForm(UnrealProject project, CancellationToken lifetime)
    {
        this.project = project;
        this.lifetime = lifetime;
        Text = "유닛·기능 검사 선택";
        ClientSize = new Size(1040, 630);
        MinimumSize = new Size(740, 440);
        StartPosition = FormStartPosition.CenterParent;
        AutoScaleMode = AutoScaleMode.Font;

        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new Padding(12) };
        for (int i = 0; i < 5; i++) layout.RowStyles.Add(new RowStyle(i == 2 ? SizeType.Percent : SizeType.AutoSize, i == 2 ? 100 : 0));
        var filters = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 2 };
        filters.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 245));
        filters.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        filters.Controls.Add(groups, 0, 0);
        filters.Controls.Add(search, 1, 0);
        list.Columns.Add("검사 이름", 590);
        list.Columns.Add("확인할 동작", 380);
        list.Resize += (_, _) =>
        {
            int width = Math.Max(500, list.ClientSize.Width - SystemInformation.VerticalScrollBarWidth);
            list.Columns[0].Width = width * 58 / 100;
            list.Columns[1].Width = width - list.Columns[0].Width;
        };
        status.Padding = new Padding(4, 5, 4, 7);
        counts.Padding = new Padding(4, 5, 4, 2);
        layout.Controls.Add(filters, 0, 0);
        layout.Controls.Add(status, 0, 1);
        layout.Controls.Add(list, 0, 2);
        layout.Controls.Add(counts, 0, 3);

        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        var close = new Button { Text = "닫기", AutoSize = true, DialogResult = DialogResult.Cancel };
        actions.Controls.AddRange([reload, rebuild, checkVisible, clear, start, close]);
        layout.Controls.Add(actions, 0, 4);
        Controls.Add(layout);
        CancelButton = close;
        AcceptButton = start;
        search.TextChanged += (_, _) => RenderList();
        groups.SelectedIndexChanged += (_, _) => RenderList();
        list.ItemChecked += (_, e) =>
        {
            if (updating || e.Item.Tag is not string name) return;
            if (e.Item.Checked) selected.Add(name);
            else selected.Remove(name);
            UpdateCounts();
        };
        checkVisible.Click += (_, _) =>
        {
            foreach (ListViewItem item in list.Items) selected.Add((string)item.Tag!);
            RenderList();
        };
        clear.Click += (_, _) => { selected.Clear(); RenderList(); };
        reload.Click += async (_, _) => await RefreshAsync(false);
        rebuild.Click += async (_, _) => await RefreshAsync(true);
        start.Click += (_, _) =>
        {
            Plan = AutomationRunPlan.Selected(selected, project.BotSimulationPrefix);
            DialogResult = DialogResult.OK;
        };
        Shown += async (_, _) =>
        {
            var cached = AutomationTestCatalog.TryRead(project);
            if (cached is not null) ShowCatalog(cached);
            if (cached is null || cached.BuildStamp != AutomationTestCatalog.BuildStamp(project))
                await RefreshAsync(false);
        };
        FormClosing += (_, e) =>
        {
            if (load is null) return;
            e.Cancel = true;
            closing = true;
            status.Text = "명단 갱신을 취소하고 있습니다.";
            load.Cancel();
        };
        UpdateCounts();
    }

    private async Task RefreshAsync(bool build)
    {
        if (load is not null) return;
        using var cancel = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        load = cancel;
        UpdateCounts();
        var progress = new Progress<string>(message => { if (load == cancel && !closing) status.Text = message; });
        try
        {
            ShowCatalog(await AutomationTestCatalog.RefreshAsync(project, build, cancel.Token, progress));
        }
        catch (OperationCanceledException)
        {
            status.Text = "명단 갱신이 중단되었습니다.";
        }
        catch (Exception e)
        {
            status.Text = (names.Length == 0 ? "" : "이전에 저장한 명단을 표시합니다. ") + e.Message;
        }
        finally
        {
            load = null;
            if (closing) Close();
            else UpdateCounts();
        }
    }

    internal void ShowCatalog(AutomationCatalogSnapshot catalog)
    {
        names = catalog.Tests;
        selected.IntersectWith(names);
        string oldGroup = (groups.SelectedItem as Group)?.Key ?? "";
        groups.BeginUpdate();
        try
        {
            groups.Items.Clear();
            groups.Items.Add(new Group("", "모든 검사"));
            groups.Items.Add(new Group("*regular", "일반 검사만"));
            foreach (string category in names.Select(Category).Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase))
                groups.Items.Add(new Group(category, CategoryLabel(category)));
            var current = groups.Items.Cast<Group>().FirstOrDefault(group => group.Key == oldGroup);
            groups.SelectedItem = current ?? groups.Items[0];
        }
        finally { groups.EndUpdate(); }
        status.Text = $"Unreal 등록 목록 · {catalog.ReadAt.LocalDateTime:g}";
        RenderList();
    }

    private void RenderList()
    {
        string group = (groups.SelectedItem as Group)?.Key ?? "";
        string[] terms = search.Text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        updating = true;
        list.BeginUpdate();
        try
        {
            list.Items.Clear();
            foreach (string name in names)
            {
                string category = Category(name);
                if (group == "*regular" ? category == "Simulation" : group.Length > 0 && group != category) continue;
                string hint = Describe(name, category);
                string searchable = name + " " + hint + " " + CategoryLabel(category);
                if (!terms.All(term => searchable.Contains(term, StringComparison.OrdinalIgnoreCase))) continue;
                var row = new ListViewItem([name, hint]) { Tag = name, Checked = selected.Contains(name), ToolTipText = name + "\n" + hint };
                list.Items.Add(row);
            }
        }
        finally { list.EndUpdate(); updating = false; }
        UpdateCounts();
    }

    private void UpdateCounts()
    {
        int simulations = selected.Count(name => name.StartsWith(project.BotSimulationPrefix + ".", StringComparison.OrdinalIgnoreCase));
        counts.Text = $"전체 {names.Length}개 · 표시 {list.Items.Count}개 · 선택 {selected.Count}개"
            + (simulations > 0 ? $" · 장시간 봇 시뮬레이션 {simulations}개 포함" : "");
        bool idle = load is null;
        start.Enabled = idle && selected.Count > 0;
        reload.Enabled = rebuild.Enabled = idle;
        clear.Enabled = selected.Count > 0;
        checkVisible.Enabled = list.Items.Count > 0;
    }

    private string Category(string name)
    {
        string relative = name[(project.PrimaryModule.Length + 1)..];
        int dot = relative.IndexOf('.');
        return dot < 0 ? "Other" : relative[..dot];
    }

    private static string CategoryLabel(string category) => category switch
    {
        "Helper" => "파트너 AI (Helper)", "Multiplayer" => "멀티플레이", "Bots" => "봇 AI",
        "Simulation" => "봇 경기 시뮬레이션", "Frontend" => "프런트엔드", "UITheme" => "UI 테마",
        "SinglePlayer" => "싱글플레이", "Editor" => "에디터·PIE", "Review" => "회귀 검사",
        "Audio" => "오디오", "Player" => "플레이어", "CaseRecords" => "사건 기록",
        "Diagnostics" => "진단", "Other" => "기타", _ => category
    };

    private static string Describe(string name, string category) => name[(name.LastIndexOf('.') + 1)..] switch
    {
        "MannequinAIStateAndFormation" => "멀티 마네킹 정찰·추격 전환, 따라오기 대형",
        "MannequinNavigationSafety" => "멀티 마네킹 길찾기, 벽 우회와 바닥 이탈 복구",
        "LastSeenSearch" => "마네킹의 마지막 발견 위치 수색과 중단",
        "VisionBounds" when category == "Helper" => "싱글 파트너 시야 범위",
        "RetreatContact" => "싱글 파트너 후퇴와 접촉 피해",
        "SingleAIState" => "싱글 마네킹·파트너 AI 상태 복원",
        "AdaptivePolicy" => "봇 AI 난이도별 판단·적응",
        "SinglePlayerTitleReturnLoading" => "싱글 체크포인트·타이틀 복귀",
        "ThreeClientTravel" => "멀티 3클라이언트 맵 전환·관전",
        _ => CategoryLabel(category)
    };
}
