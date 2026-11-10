using System.Text.Json;

namespace ProjectProject01BuildServerHelper;

internal sealed class LiveOperationsForm : Form
{
    private readonly Func<HttpMethod, string, object?, HttpRequestMessage> create;
    private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(8) };
    private readonly TabControl tabs = new() { Dock = DockStyle.Fill };
    private readonly Label status = new() { Dock = DockStyle.Bottom, AutoSize = true, Padding = new Padding(8) };
    private readonly System.Windows.Forms.Timer healthTimer = new() { Interval = 20000 };
    private const string Root = "/api/admin/operations/";
    private bool busy;

    public LiveOperationsForm(Func<HttpMethod, string, object?, HttpRequestMessage> request, bool manage)
    {
        create = request;
        Text = manage ? "공개 범위·예약 보상" : "장애 알림·플레이 분석·운영 이력";
        Size = new Size(1050, 640);
        MinimumSize = new Size(800, 500);
        StartPosition = FormStartPosition.CenterParent;
        Controls.Add(tabs);
        Controls.Add(status);
        if (manage)
        {
            AddRollout();
            AddRewards();
        }
        else
        {
            AddHealth();
            AddAnalytics();
            AddAudit();
        }
        FormClosed += (_, _) => { healthTimer.Dispose(); http.Dispose(); };
    }

    private (TabPage Page, FlowLayoutPanel Header, DataGridView Grid) TablePage(string name)
    {
        var page = new TabPage(name);
        var header = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(8) };
        var grid = new DataGridView
        {
            Dock = DockStyle.Fill, ReadOnly = true, MultiSelect = false, AllowUserToAddRows = false,
            AllowUserToDeleteRows = false, SelectionMode = DataGridViewSelectionMode.FullRowSelect,
            RowHeadersVisible = false, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill
        };
        page.Controls.Add(grid);
        page.Controls.Add(header);
        tabs.TabPages.Add(page);
        return (page, header, grid);
    }

    private static void Fill(DataGridView grid, JsonElement rows, params (string Field, string Title)[] columns)
    {
        grid.Rows.Clear();
        grid.Columns.Clear();
        foreach (var column in columns) grid.Columns.Add(column.Field, column.Title);
        foreach (var row in rows.EnumerateArray())
            grid.Rows.Add(columns.Select(column => (object)(row.TryGetProperty(column.Field, out var value) ? value.ToString() : "")).ToArray());
    }

    private void AddHealth()
    {
        var (page, header, grid) = TablePage("장애 알림");
        async Task Refresh()
        {
            var data = await Send(HttpMethod.Get, "health");
            if (IsDisposed) return;
            Fill(grid, data.GetProperty("incidents"), ("at", "감지 시각 (UTC)"), ("area", "기능"), ("build", "빌드"),
                ("metric", "지표"), ("value", "측정값"), ("limit", "기준"));
            page.Text = grid.Rows.Count > 0 ? $"장애 알림 ({grid.Rows.Count})" : "장애 알림";
        }
        Button(header, "새로고침", Refresh);
        Shown += async (_, _) => await Run(Refresh);
        healthTimer.Tick += async (_, _) => { if (tabs.SelectedTab == page) await Run(Refresh); };
        healthTimer.Start();
    }

    private void AddAnalytics()
    {
        var (_, header, grid) = TablePage("플레이 분석");
        var period = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 95 };
        period.Items.AddRange(["7일", "30일", "90일"]);
        period.SelectedIndex = 1;
        var view = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 175 };
        view.Items.AddRange(["튜토리얼 단계별", "층별 포획·탈출", "실제 생존자 수별"]);
        view.SelectedIndex = 0;
        header.Controls.AddRange([period, view]);
        Button(header, "조회", async () =>
        {
            int days = new[] { 7, 30, 90 }[period.SelectedIndex];
            var data = await Send(HttpMethod.Get, $"analytics?days={days}");
            if (IsDisposed) return;
            switch (view.SelectedIndex)
            {
                case 0:
                    Fill(grid, data.GetProperty("tutorial"), ("role", "역할"), ("build", "빌드"), ("step", "단계"),
                        ("visits", "도달"), ("lastObserved", "이후 기록 없음"));
                    break;
                case 1:
                    Fill(grid, data.GetProperty("floors"), ("kind", "기록 종류"), ("map", "맵"), ("build", "빌드"),
                        ("events", "횟수"), ("matches", "경기"));
                    break;
                default:
                    Fill(grid, data.GetProperty("survivors"), ("humans", "실제 생존자"), ("build", "빌드"), ("matches", "경기"),
                        ("survivorResults", "결과 수"), ("escapes", "탈출"), ("escapeRate", "탈출률 (%)"));
                    break;
            }
        });
    }

    private void AddAudit()
    {
        var (_, header, grid) = TablePage("운영 변경 이력");
        Button(header, "조회", async () =>
        {
            var data = await Send(HttpMethod.Get, "audit");
            if (!IsDisposed) Fill(grid, data.GetProperty("changes"), ("utc", "시각 (UTC)"), ("outcome", "결과"), ("details", "운영자·작업"));
        });
    }

    private void AddRollout()
    {
        var page = new TabPage("설문 공개 범위");
        tabs.TabPages.Add(page);
        var controls = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Padding = new Padding(12) };
        page.Controls.Add(controls);
        var mode = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 250 };
        string[] modes = ["Everyone", "Off", "Testers", "Experiment"];
        mode.Items.AddRange(["전체 공개", "비공개", "테스트 계정만", "A/B · 선택 비율에 공개"]);
        mode.SelectedIndex = 0;
        var percent = new NumericUpDown { Minimum = 0, Maximum = 100, Value = 50, Width = 105 };
        var accounts = new TextBox { Width = 580, PlaceholderText = "테스트 계정 ID (쉼표로 구분)" };
        var starts = DateBox(optional: true);
        var ends = DateBox(optional: true);
        controls.Controls.AddRange([mode, new Label { Text = "A/B 공개 비율 (%)", AutoSize = true }, percent, accounts,
            new Label { Text = "공개 적용 시작·종료 (이 PC 시간)", AutoSize = true }, starts, ends]);
        long? revision = null;
        async Task Refresh()
        {
            var data = await Send(HttpMethod.Get, "rollout");
            var item = data.GetProperty("settings");
            revision = data.GetProperty("revision").GetInt64();
            mode.SelectedIndex = Math.Max(0, Array.IndexOf(modes, item.GetProperty("mode").GetString()));
            percent.Value = item.GetProperty("percent").GetInt32();
            accounts.Text = string.Join(", ", item.GetProperty("testAccounts").EnumerateArray().Select(x => x.GetString()));
            SetDate(starts, item.GetProperty("startsAtUtc"));
            SetDate(ends, item.GetProperty("endsAtUtc"));
        }
        Button(controls, "새로고침", Refresh);
        Button(controls, "적용", async () =>
        {
            if (revision is null) throw new InvalidOperationException("서버 설정을 먼저 불러오세요.");
            var result = await Send(HttpMethod.Put, "rollout", new
            {
                revision,
                settings = new { mode = modes[mode.SelectedIndex], percent = (int)percent.Value, testAccounts = Accounts(accounts),
                    startsAtUtc = Date(starts), endsAtUtc = Date(ends) }
            });
            revision = result.GetProperty("revision").GetInt64();
            status.Text = "공개 범위를 적용했습니다.";
        });
        Shown += async (_, _) => await Run(Refresh);
    }

    private void AddRewards()
    {
        var (_, header, grid) = TablePage("포인트 예약 지급");
        var title = new TextBox { Width = 220, PlaceholderText = "지급 제목", MaxLength = 120 };
        var accounts = new TextBox { Width = 320, PlaceholderText = "대상 계정 ID (쉼표로 구분)" };
        var credits = new NumericUpDown { Minimum = 1, Maximum = 10000, Value = 100, Width = 90 };
        var starts = DateBox(optional: false);
        string? pendingSignature = null;
        Guid requestId = Guid.Empty;
        header.Controls.AddRange([title, accounts, new Label { Text = "1인당 포인트", AutoSize = true }, credits, starts]);
        async Task Refresh()
        {
            var data = await Send(HttpMethod.Get, "rewards");
            if (!IsDisposed) Fill(grid, data.GetProperty("rewards"), ("id", "번호"), ("title", "지급 제목"),
                ("credits", "1인당 포인트"), ("startsUtc", "예정 시각 (UTC)"), ("status", "상태"),
                ("actor", "운영자"), ("recipients", "대상 수"));
        }
        Button(header, "미리보기·예약", async () =>
        {
            var input = new { title = title.Text, credits = (int)credits.Value, startsAtUtc = Date(starts), accounts = Accounts(accounts) };
            string signature = JsonSerializer.Serialize(input);
            if (pendingSignature != signature)
            {
                pendingSignature = signature;
                requestId = Guid.NewGuid();
            }
            var body = new { input.title, input.credits, input.startsAtUtc, input.accounts, requestId };
            var preview = await Send(HttpMethod.Post, "rewards/preview", body);
            string recipients = string.Join(", ", preview.GetProperty("accounts").EnumerateArray().Select(item => item.GetString()));
            if (MessageBox.Show(this, $"{recipients}\n\n1인당 {body.credits:N0} · 총 {preview.GetProperty("total").GetInt32():N0} 포인트",
                "이대로 지급을 예약할까요?", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
            await Send(HttpMethod.Post, "rewards", body);
            pendingSignature = null;
            await Refresh();
        });
        Button(header, "새로고침", Refresh);
        async Task Change(string action, string question)
        {
            if (grid.CurrentRow?.Cells["id"].Value is not string id) return;
            if (MessageBox.Show(this, question, "포인트 지급", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
            await Send(HttpMethod.Post, $"rewards/{id}/{action}");
            await Refresh();
        }
        Button(header, "선택 예약 취소", () => Change("cancel", "선택한 지급 예약을 취소할까요?"));
        Button(header, "선택 지급 회수", () => Change("reverse", "선택한 지급을 회수할까요?"));
    }

    private static string[] Accounts(TextBox accounts) =>
        accounts.Text.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);

    private static DateTimePicker DateBox(bool optional) => new()
    {
        Width = 205, Format = DateTimePickerFormat.Custom, CustomFormat = "yyyy-MM-dd HH:mm",
        ShowCheckBox = optional, Checked = !optional, Value = DateTime.Now.AddMinutes(5)
    };

    private static DateTimeOffset? Date(DateTimePicker box) =>
        box.Checked ? new DateTimeOffset(box.Value).ToUniversalTime() : null;

    private static void SetDate(DateTimePicker box, JsonElement date)
    {
        box.Checked = date.ValueKind == JsonValueKind.String;
        if (box.Checked) box.Value = date.GetDateTimeOffset().LocalDateTime;
    }

    private void Button(FlowLayoutPanel panel, string label, Func<Task> action)
    {
        var button = new Button { Text = label, AutoSize = true };
        button.Click += async (_, _) => await Run(action);
        panel.Controls.Add(button);
    }

    private async Task Run(Func<Task> action)
    {
        if (busy) return;
        busy = true;
        status.Text = "";
        try { await action(); }
        catch (Exception ex) { if (!IsDisposed) status.Text = ex.Message; }
        finally { busy = false; }
    }

    private async Task<JsonElement> Send(HttpMethod method, string route, object? body = null)
    {
        using var request = create(method, Root + route, body);
        using var response = await http.SendAsync(request);
        string content = await response.Content.ReadAsStringAsync();
        if (!response.IsSuccessStatusCode)
        {
            string message = $"요청 실패 · HTTP {(int)response.StatusCode}";
            if (content.StartsWith('{'))
            {
                using var error = JsonDocument.Parse(content);
                if (error.RootElement.TryGetProperty("message", out var value)) message = value.GetString() ?? message;
            }
            throw new InvalidOperationException(message);
        }
        if (content.Length == 0) return default;
        using var json = JsonDocument.Parse(content);
        return json.RootElement.Clone();
    }
}
