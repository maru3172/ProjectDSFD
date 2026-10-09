using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;

namespace ProjectProject01BuildServerHelper;

/// <summary>
/// Local operator UI for the backend's administrator-only service controls.
/// The administrator key is loaded from the ignored local configuration and is never displayed.
/// </summary>
internal sealed class ServerAdministrationForm : Form
{
    private readonly string projectRoot;
    private readonly Action<string> mainLog;
    private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(8) };
    private readonly TextBox baseUrl = new() { Dock = DockStyle.Fill, Text = "http://127.0.0.1:5080" };
    private readonly Label serviceState = new()
    {
        AutoSize = true,
        Text = "상태를 불러오지 않았습니다.",
        Padding = new Padding(0, 7, 0, 7)
    };
    private readonly CheckBox maintenanceEnabled = new()
    {
        AutoSize = true,
        Text = "점검 모드 — 신규 로그인·회원가입·방 생성을 차단"
    };
    private readonly TextBox announcement = new()
    {
        Dock = DockStyle.Fill,
        Multiline = true,
        Height = 74,
        ScrollBars = ScrollBars.Vertical,
        MaxLength = 512
    };
    private readonly CheckBox useShutdownTime = new() { AutoSize = true, Text = "서버 종료 예정 시각 사용" };
    private readonly DateTimePicker shutdownTime = new()
    {
        Width = 210,
        Format = DateTimePickerFormat.Custom,
        CustomFormat = "yyyy-MM-dd HH:mm:ss",
        ShowUpDown = true,
        Value = DateTime.Now.AddMinutes(10)
    };
    private readonly TextBox accountId = new() { Width = 230, PlaceholderText = "강제 로그아웃할 계정 ID" };
    private readonly RichTextBox operationLog = new()
    {
        Dock = DockStyle.Fill,
        ReadOnly = true,
        BackColor = Color.FromArgb(24, 24, 24),
        ForeColor = Color.Gainsboro
    };
    private readonly Label operationStatus = new() { AutoSize = true, Text = "준비" };

    private sealed class ServiceControlDto
    {
        public bool MaintenanceEnabled { get; set; }
        public string Announcement { get; set; } = string.Empty;
        public DateTime? ShutdownAtUtc { get; set; }
        public ulong Revision { get; set; }
        public DateTime UpdatedAtUtc { get; set; }
        public string Message { get; set; } = string.Empty;
    }

    public ServerAdministrationForm(string projectRoot, Action<string> mainLog)
    {
        this.projectRoot = projectRoot;
        this.mainLog = mainLog;
        Text = "ProjectProject01 서버 운영";
        Width = 840;
        Height = 690;
        MinimumSize = new Size(740, 590);
        StartPosition = FormStartPosition.CenterParent;
        BuildUi();
        Shown += async (_, _) => await RefreshStatusAsync();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) http.Dispose();
        base.Dispose(disposing);
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 9,
            Padding = new Padding(12)
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var address = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 2, AutoSize = true };
        address.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
        address.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        address.Controls.Add(new Label
        {
            Text = "백엔드 주소",
            AutoSize = true,
            Padding = new Padding(0, 7, 0, 0)
        }, 0, 0);
        address.Controls.Add(baseUrl, 1, 0);
        root.Controls.Add(address);

        root.Controls.Add(serviceState);
        root.Controls.Add(maintenanceEnabled);

        var noticeGroup = new GroupBox { Text = "전체 공지", Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(8) };
        var noticeLayout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, AutoSize = true };
        noticeLayout.Controls.Add(announcement, 0, 0);
        var shutdownPanel = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = false };
        shutdownPanel.Controls.Add(useShutdownTime);
        shutdownPanel.Controls.Add(shutdownTime);
        shutdownPanel.Controls.Add(new Label { Text = "(현재 PC의 로컬 시각)", AutoSize = true, Padding = new Padding(5, 7, 0, 0) });
        noticeLayout.Controls.Add(shutdownPanel, 0, 1);
        noticeGroup.Controls.Add(noticeLayout);
        root.Controls.Add(noticeGroup);

        var serviceButtons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = true };
        AddButton(serviceButtons, "상태 새로고침", RefreshStatusAsync);
        AddButton(serviceButtons, "점검·공지 적용", ApplyServiceControlAsync);
        AddButton(serviceButtons, "공지 지우기", ClearAnnouncementAsync);
        root.Controls.Add(serviceButtons);

        var explanation = new Label
        {
            AutoSize = true,
            ForeColor = Color.DarkRed,
            Text = "점검 모드는 신규 로그인·회원가입·방 생성만 막습니다. 이미 로그인한 사용자는 아래 강제 로그아웃 기능으로 정리할 수 있습니다.",
            Padding = new Padding(0, 4, 0, 8)
        };
        root.Controls.Add(explanation);

        var logoutGroup = new GroupBox { Text = "로그인 세션 관리", Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(8) };
        var logoutPanel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = true };
        logoutPanel.Controls.Add(accountId);
        AddButton(logoutPanel, "선택 계정 로그아웃", ForceLogoutAccountAsync);
        AddButton(logoutPanel, "접속자 전원 로그아웃", ForceLogoutAllAsync);
        logoutGroup.Controls.Add(logoutPanel);
        root.Controls.Add(logoutGroup);

        root.Controls.Add(operationLog);
        root.Controls.Add(operationStatus);
        Controls.Add(root);
    }

    private static void AddButton(FlowLayoutPanel panel, string text, Func<Task> action)
    {
        var button = new Button { Text = text, AutoSize = true, Height = 34 };
        button.Click += async (_, _) =>
        {
            button.Enabled = false;
            try { await action(); }
            catch (Exception ex) { MessageBox.Show(ex.Message, "서버 운영 오류", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            finally { button.Enabled = true; }
        };
        panel.Controls.Add(button);
    }

    private async Task RefreshStatusAsync()
    {
        SetOperationStatus("서버 상태 확인 중...");
        try
        {
            using var request = CreateAdminRequest(HttpMethod.Get, "/api/admin/service");
            using var response = await http.SendAsync(request);
            ServiceControlDto dto = await ReadResponseAsync<ServiceControlDto>(response);
            maintenanceEnabled.Checked = dto.MaintenanceEnabled;
            announcement.Text = dto.Announcement;
            useShutdownTime.Checked = dto.ShutdownAtUtc.HasValue;
            if (dto.ShutdownAtUtc.HasValue)
                shutdownTime.Value = dto.ShutdownAtUtc.Value.ToLocalTime();
            serviceState.Text = dto.MaintenanceEnabled
                ? $"현재 상태: 점검 중 · 로그인/회원가입/방 생성 차단 · Revision {dto.Revision}"
                : $"현재 상태: 정상 운영 · Revision {dto.Revision}";
            serviceState.ForeColor = dto.MaintenanceEnabled ? Color.DarkRed : Color.DarkGreen;
            Append("[OK] 서버 운영 상태를 불러왔습니다.");
            SetOperationStatus("상태 확인 완료");
        }
        catch (Exception ex)
        {
            serviceState.Text = "서버 상태를 확인할 수 없습니다.";
            serviceState.ForeColor = Color.DarkRed;
            Append("[실패] " + ex.Message);
            SetOperationStatus("상태 확인 실패");
        }
    }

    private async Task ApplyServiceControlAsync()
    {
        SetOperationStatus("점검·공지 적용 중...");
        var body = new
        {
            maintenanceEnabled = maintenanceEnabled.Checked,
            announcement = announcement.Text.Trim(),
            shutdownAtUtc = useShutdownTime.Checked ? shutdownTime.Value.ToUniversalTime() : (DateTime?)null
        };
        using var request = CreateAdminRequest(HttpMethod.Put, "/api/admin/service", body);
        using var response = await http.SendAsync(request);
        ServiceControlDto dto = await ReadResponseAsync<ServiceControlDto>(response);
        Append(dto.MaintenanceEnabled
            ? "[OK] 점검 모드와 공지를 적용했습니다. 신규 로그인·회원가입·방 생성이 차단됩니다."
            : "[OK] 정상 운영 상태와 공지를 적용했습니다.");
        mainLog(dto.MaintenanceEnabled ? "[운영] 서버 점검 모드 적용" : "[운영] 서버 정상 모드 적용");
        SetOperationStatus("점검·공지 적용 완료");
        await RefreshStatusAsync();
    }

    private async Task ClearAnnouncementAsync()
    {
        announcement.Clear();
        useShutdownTime.Checked = false;
        await ApplyServiceControlAsync();
    }

    private async Task ForceLogoutAllAsync()
    {
        if (MessageBox.Show(
                "현재 멀티플레이 로그인 세션을 전부 폐기할까요? 모든 사용자가 다시 로그인해야 합니다.",
                "접속자 전원 로그아웃",
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning) != DialogResult.Yes)
            return;

        SetOperationStatus("전체 세션 폐기 중...");
        using var request = CreateAdminRequest(HttpMethod.Post, "/api/admin/sessions/revoke-all");
        using var response = await http.SendAsync(request);
        string message = await ReadMessageAsync(response);
        Append("[OK] " + message);
        mainLog("[운영] 전체 로그인 세션 강제 로그아웃 실행");
        SetOperationStatus("전체 강제 로그아웃 완료");
    }

    private async Task ForceLogoutAccountAsync()
    {
        string target = accountId.Text.Trim();
        if (target.Length == 0)
        {
            MessageBox.Show("계정 ID를 입력하세요.", "선택 계정 로그아웃", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        if (MessageBox.Show(
                $"'{target}' 계정의 모든 로그인 세션을 폐기할까요?",
                "선택 계정 로그아웃",
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning) != DialogResult.Yes)
            return;

        SetOperationStatus("선택 계정 세션 폐기 중...");
        string encoded = Uri.EscapeDataString(target);
        using var request = CreateAdminRequest(HttpMethod.Post, $"/api/admin/users/{encoded}/sessions/revoke");
        using var response = await http.SendAsync(request);
        string message = await ReadMessageAsync(response);
        Append("[OK] " + message);
        mainLog($"[운영] 계정 '{target}' 강제 로그아웃 실행");
        SetOperationStatus("선택 계정 강제 로그아웃 완료");
    }

    private HttpRequestMessage CreateAdminRequest(HttpMethod method, string endpoint, object? body = null)
    {
        string key = LoadAdministratorKey();
        string root = baseUrl.Text.Trim().TrimEnd('/');
        if (!Uri.TryCreate(root, UriKind.Absolute, out Uri? baseUri) ||
            (baseUri.Scheme != Uri.UriSchemeHttp && baseUri.Scheme != Uri.UriSchemeHttps))
            throw new InvalidOperationException("백엔드 주소가 올바르지 않습니다.");

        var request = new HttpRequestMessage(method, root + endpoint);
        request.Headers.Add("X-ProjectProject01-Admin-Key", key);
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        if (body != null)
        {
            string json = JsonSerializer.Serialize(body);
            request.Content = new StringContent(json, Encoding.UTF8, "application/json");
        }
        return request;
    }

    private string LoadAdministratorKey()
    {
        string path = Path.Combine(projectRoot, "Tools", "ProjectProject01Backend", "LocalMySql",
            "ProjectProject01Backend.local.json");
        if (!File.Exists(path))
            throw new InvalidOperationException("로컬 관리자 설정이 없습니다. 먼저 MySQL 초기화/시작을 실행하세요.");

        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(path, Encoding.UTF8));
        if (!document.RootElement.TryGetProperty("Administrator", out JsonElement administrator) ||
            !administrator.TryGetProperty("ApiKey", out JsonElement keyElement))
            throw new InvalidOperationException("로컬 설정에서 관리자 API 키를 찾지 못했습니다.");
        string key = keyElement.GetString()?.Trim() ?? string.Empty;
        if (key.Length < 32)
            throw new InvalidOperationException("관리자 API 키가 유효하지 않습니다. MySQL 설정 동기화를 다시 실행하세요.");
        return key;
    }

    private static async Task<T> ReadResponseAsync<T>(HttpResponseMessage response)
    {
        string content = await response.Content.ReadAsStringAsync();
        if (!response.IsSuccessStatusCode)
            throw new InvalidOperationException(BuildHttpError(response, content));
        T? value = JsonSerializer.Deserialize<T>(content, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
        return value ?? throw new InvalidOperationException("백엔드 응답을 읽지 못했습니다.");
    }

    private static async Task<string> ReadMessageAsync(HttpResponseMessage response)
    {
        string content = await response.Content.ReadAsStringAsync();
        if (!response.IsSuccessStatusCode)
            throw new InvalidOperationException(BuildHttpError(response, content));
        try
        {
            using JsonDocument document = JsonDocument.Parse(content);
            if (document.RootElement.TryGetProperty("message", out JsonElement message))
                return message.GetString() ?? "요청이 완료되었습니다.";
        }
        catch (JsonException) { }
        return "요청이 완료되었습니다.";
    }

    private static string BuildHttpError(HttpResponseMessage response, string content)
    {
        string message = content;
        try
        {
            using JsonDocument document = JsonDocument.Parse(content);
            if (document.RootElement.TryGetProperty("message", out JsonElement element))
                message = element.GetString() ?? content;
        }
        catch (JsonException) { }
        return $"백엔드 요청 실패 HTTP {(int)response.StatusCode}: {message}";
    }

    private void Append(string text)
    {
        operationLog.AppendText($"[{DateTime.Now:HH:mm:ss}] {text}{Environment.NewLine}");
        operationLog.SelectionStart = operationLog.TextLength;
        operationLog.ScrollToCaret();
    }

    private void SetOperationStatus(string text) => operationStatus.Text = text;
}
