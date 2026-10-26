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
    public event Action<int>? CapacityChanged;
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
    private string announcementTag="";
    private int announcementRepeatCount=1;
    private DateTimeOffset? announcementStartsAtUtc,announcementEndsAtUtc;
    private readonly DateTimePicker maintenanceStartTime = new()
    {
        Width = 190,
        Format = DateTimePickerFormat.Custom,
        CustomFormat = "yyyy-MM-dd HH:mm:ss",
        ShowUpDown = true,
        Value = DateTime.Now.AddMinutes(5)
    };
    private readonly DateTimePicker maintenanceEndTime = new()
    {
        Width = 190,
        Format = DateTimePickerFormat.Custom,
        CustomFormat = "yyyy-MM-dd HH:mm:ss",
        ShowUpDown = true,
        Value = DateTime.Now.AddMinutes(15)
    };
    private readonly TextBox scheduleAnnouncement = new()
    {
        Width = 300,
        PlaceholderText = "예약 공지 (비우면 전체 공지 사용)",
        MaxLength = 512
    };
    private readonly ListBox maintenanceSchedules = new()
    {
        Dock = DockStyle.Fill,
        Height = 110,
        IntegralHeight = false
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
        public bool ManualMaintenanceEnabled { get; set; }
        public string Announcement { get; set; } = string.Empty;
        public string BaseAnnouncement { get; set; } = string.Empty;
        public string AnnouncementTag {get;set;}="";
        public int AnnouncementRepeatCount {get;set;}=1;
        public DateTimeOffset? AnnouncementStartsAtUtc {get;set;}
        public DateTimeOffset? AnnouncementEndsAtUtc {get;set;}
        public DateTime? MaintenanceStartAtUtc { get; set; }
        public DateTime? ShutdownAtUtc { get; set; }
        public ulong Revision { get; set; }
        public DateTime UpdatedAtUtc { get; set; }
        public string Message { get; set; } = string.Empty;
    }

    private sealed class MaintenanceScheduleDto
    {
        public ulong Id { get; set; }
        public DateTime StartsAtUtc { get; set; }
        public DateTime EndsAtUtc { get; set; }
        public string Announcement { get; set; } = string.Empty;
        public string Status { get; set; } = string.Empty;

        public override string ToString()
        {
            string status = Status.Equals("Active", StringComparison.OrdinalIgnoreCase) ? "진행 중" : "예약";
            string notice = string.IsNullOrWhiteSpace(Announcement) ? "공지 없음" : Announcement;
            return $"#{Id} [{status}] {StartsAtUtc.ToLocalTime():yyyy-MM-dd HH:mm:ss} ~ {EndsAtUtc.ToLocalTime():yyyy-MM-dd HH:mm:ss} · {notice}";
        }
    }

    private sealed class MaintenanceScheduleListDto
    {
        public List<MaintenanceScheduleDto> Schedules { get; set; } = [];
        public int Maximum { get; set; }
    }

    private sealed class CrashReportDto
    {
        public string ReportId { get; set; } = string.Empty;
        public DateTime DetectedAtUtc { get; set; }
        public string BuildVersion { get; set; } = string.Empty;
        public string MapName { get; set; } = string.Empty;
        public string GameMode { get; set; } = string.Empty;
        public string ProcessRole { get; set; } = string.Empty;
        public string? PlayerRole { get; set; }
        public string LastLogPath { get; set; } = string.Empty;
        public string DiagnosticRunId { get; set; } = string.Empty;
        public string? MatchId { get; set; }
    }

    private sealed class CrashReportListDto
    {
        public List<CrashReportDto> CrashReports { get; set; } = [];
        public int Count { get; set; }
    }

    public ServerAdministrationForm(string projectRoot, Action<string> mainLog, bool refreshOnShow = true)
    {
        this.projectRoot = projectRoot;
        this.mainLog = mainLog;
        Text = "ProjectDFSD 서버 운영";
        Width = 940;
        Height = 760;
        MinimumSize = new Size(740, 590);
        StartPosition = FormStartPosition.CenterParent;
        BuildUi();
        if (refreshOnShow) Shown += async (_, _) => await RefreshStatusAsync();
        maintenanceSchedules.SelectedIndexChanged += (_, _) => LoadSelectedScheduleIntoEditor();
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
            Name = "AdministrationLayout", Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5,
            Padding = new Padding(12)
        };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 132));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var address = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 3, AutoSize = true };
        address.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));
        address.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        address.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        address.Controls.Add(new Label { Text = "백엔드 주소", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, 0, 0);
        baseUrl.Name = "AdministrationBaseUrl";
        address.Controls.Add(baseUrl, 1, 0);
        var refresh = ActionRow();
        AddButton(refresh, "상태 새로고침", RefreshStatusAsync);
        address.Controls.Add(refresh, 2, 0);
        root.Controls.Add(address, 0, 0);
        root.Controls.Add(serviceState, 0, 1);

        var tabs = new TabControl { Name = "ServerAdministrationTabs", Dock = DockStyle.Fill, Padding = new Point(14, 7) };
        root.Controls.Add(tabs, 0, 2);
        TableLayoutPanel Category(string title)
        {
            var page = new TabPage(title) { Padding = new Padding(10) };
            var content = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, AutoScroll = true, GrowStyle = TableLayoutPanelGrowStyle.AddRows };
            content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            page.Controls.Add(content); tabs.TabPages.Add(page);
            return content;
        }
        void Group(TableLayoutPanel page, string title, params Control[] controls)
        {
            var group = new GroupBox { Text = title, Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(10), Margin = new Padding(0, 0, 0, 10) };
            var content = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 1, AutoSize = true };
            content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            foreach (var control in controls)
            {
                content.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                content.Controls.Add(control, 0, content.RowCount++);
            }
            group.Controls.Add(content);
            page.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            page.Controls.Add(group, 0, page.RowCount++);
        }

        var service = Category("점검·공지");
        var maintenanceActions = ActionRow();
        AddButton(maintenanceActions, "점검·공지 적용", ApplyServiceControlAsync);
        Group(service, "점검 모드", maintenanceEnabled, maintenanceActions);
        var notices = ActionRow();
        AddButton(notices, "안내 공지", OpenAnnouncementEditorAsync);
        AddButton(notices, "공지 지우기", ClearAnnouncementAsync);
        Group(service, "안내 공지", notices);
        var help = new ToolTip { AutoPopDelay = 20000 };
        help.SetToolTip(maintenanceEnabled, "점검 모드는 신규 로그인·회원가입·방 생성만 막습니다. 각 예약은 종료 시각에 자동 해제되며, 시간이 겹친 다른 예약이 있으면 점검을 유지합니다. 예약 공지는 해당 시간에만 표시되고 전체 공지는 별도로 유지됩니다.");
        Disposed += (_, _) => help.Dispose();

        var schedulePage = new TabPage("점검 예약") { Padding = new Padding(10) };
        tabs.TabPages.Add(schedulePage);
        var schedules = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5 };
        schedules.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        schedules.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        schedules.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        schedules.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        schedules.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        schedules.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var times = ActionRow();
        times.Controls.Add(new Label { Text = "시작", AutoSize = true, Padding = new Padding(0, 7, 4, 0) });
        times.Controls.Add(maintenanceStartTime);
        times.Controls.Add(new Label { Text = "종료", AutoSize = true, Padding = new Padding(10, 7, 4, 0) });
        times.Controls.Add(maintenanceEndTime);
        times.Controls.Add(new Label { Text = "(현재 PC의 로컬 시각)", AutoSize = true, Padding = new Padding(5, 7, 0, 0) });
        schedules.Controls.Add(times, 0, 0);
        scheduleAnnouncement.Dock = DockStyle.Fill;
        scheduleAnnouncement.Name = "MaintenanceScheduleAnnouncement";
        schedules.Controls.Add(scheduleAnnouncement, 0, 1);
        schedules.Controls.Add(new Label { Text = "진행 중·예정된 점검 예약 (최대 100개)", AutoSize = true, Margin = new Padding(0, 8, 0, 6) }, 0, 2);
        maintenanceSchedules.Name = "MaintenanceSchedules";
        schedules.Controls.Add(maintenanceSchedules, 0, 3);
        var scheduleActions = ActionRow();
        AddButton(scheduleActions, "점검 예약 추가", AddMaintenanceScheduleAsync);
        AddButton(scheduleActions, "선택 예약 변경", UpdateSelectedMaintenanceScheduleAsync);
        AddButton(scheduleActions, "점검 예약 취소", CancelMaintenanceScheduleAsync);
        schedules.Controls.Add(scheduleActions, 0, 4);
        schedulePage.Controls.Add(schedules);

        var accounts = Category("계정·신고");
        var safety = ActionRow();
        AddButton(safety, "계정 이용 제한", () => { using var dialog = new AccountRestrictionForm(CreateAdminRequest); dialog.ShowDialog(this); return Task.CompletedTask; });
        AddButton(safety, "플레이어 신고 확인", () => { using var dialog = new PlayerReportsForm(CreateAdminRequest); dialog.ShowDialog(this); return Task.CompletedTask; });
        Group(accounts, "계정·신고 관리", safety);
        var logout = ActionRow();
        accountId.Name = "ForceLogoutAccountId";
        logout.Controls.Add(accountId);
        AddButton(logout, "선택 계정 로그아웃", ForceLogoutAccountAsync);
        AddButton(logout, "접속자 전원 로그아웃", ForceLogoutAllAsync);
        Group(accounts, "로그인 세션 관리", logout);

        var contentPage = Category("콘텐츠·소통");
        var community = ActionRow();
        AddButton(community, "런처 뉴스·패치노트·문의", () => { using var dialog = new CommunityAdministrationForm(CreateAdminRequest); dialog.ShowDialog(this); return Task.CompletedTask; });
        AddButton(community, "피드백/설문조사 관리", OpenSurveyAdministrationAsync);
        Group(contentPage, "공지·문의·설문", community);
        var rewards = ActionRow();
        AddButton(rewards, "사건 기록 조사 수량", () => { using var dialog = new CaseInvestigationForm(CreateAdminRequest); dialog.ShowDialog(this); return Task.CompletedTask; });
        AddButton(rewards, "주간 미션 재화 상한", () => { using var dialog = new MissionBudgetForm(CreateAdminRequest); dialog.ShowDialog(this); return Task.CompletedTask; });
        Group(contentPage, "사건 기록·보상", rewards);

        var operations = Category("진단·운영");
        var crashes = ActionRow();
        AddButton(crashes, "크래시 보고서 보기", LoadCrashReportsAsync);
        AddButton(crashes, "크래시 보관 정책", () => { using var dialog = new CrashArtifactPolicyForm(CreateAdminRequest); dialog.ShowDialog(this); return Task.CompletedTask; });
        Group(operations, "크래시 보고서", crashes);
        var capacity = ActionRow();
        AddButton(capacity, "최대 방 수", () => { using var dialog = new RoomCapacityForm(CreateAdminRequest, value => CapacityChanged?.Invoke(value)); dialog.ShowDialog(this); return Task.CompletedTask; });
        Group(operations, "운영 한도", capacity);

        foreach (var page in new[] { service, accounts, contentPage, operations })
        {
            page.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            page.RowCount++;
        }
        var log = new GroupBox { Text = "작업 로그", Dock = DockStyle.Fill, Padding = new Padding(8) };
        operationLog.Name = "AdministrationOperationLog";
        log.Controls.Add(operationLog);
        root.Controls.Add(log, 0, 3);
        root.Controls.Add(operationStatus, 0, 4);
        Controls.Add(root);
        root.SizeChanged += (_, _) => serviceState.MaximumSize = new Size(Math.Max(250, root.ClientSize.Width - root.Padding.Horizontal), 0);
    }

    private static FlowLayoutPanel ActionRow() => new()
    {
        Dock = DockStyle.Top, AutoSize = true, WrapContents = true,
        Margin = Padding.Empty, Padding = Padding.Empty
    };

    private static void AddButton(FlowLayoutPanel panel, string text, Func<Task> action)
    {
        var button = new Button { Text = text, AutoSize = true, MinimumSize = new Size(150, 34), Margin = new Padding(0, 0, 8, 6) };
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
            maintenanceEnabled.Checked = dto.ManualMaintenanceEnabled;
            announcement.Text = dto.BaseAnnouncement;
            announcementTag=dto.AnnouncementTag;announcementRepeatCount=dto.AnnouncementRepeatCount;
            announcementStartsAtUtc=dto.AnnouncementStartsAtUtc;announcementEndsAtUtc=dto.AnnouncementEndsAtUtc;
            if (dto.MaintenanceStartAtUtc.HasValue)
                maintenanceStartTime.Value = dto.MaintenanceStartAtUtc.Value.ToLocalTime();
            if (dto.ShutdownAtUtc.HasValue)
                maintenanceEndTime.Value = dto.ShutdownAtUtc.Value.ToLocalTime();
            serviceState.Text = dto.MaintenanceEnabled
                ? $"현재 상태: 점검 중 · 로그인/회원가입/방 생성 차단 · Revision {dto.Revision}"
                : dto.MaintenanceStartAtUtc.HasValue
                    ? $"현재 상태: 점검 예약 · {dto.MaintenanceStartAtUtc.Value.ToLocalTime():yyyy-MM-dd HH:mm:ss} 시작 · Revision {dto.Revision}"
                    : $"현재 상태: 정상 운영 · Revision {dto.Revision}";
            serviceState.ForeColor = dto.MaintenanceEnabled ? Color.DarkRed : Color.DarkGreen;

            using var schedulesRequest = CreateAdminRequest(HttpMethod.Get, "/api/admin/service/schedules");
            using var schedulesResponse = await http.SendAsync(schedulesRequest);
            MaintenanceScheduleListDto scheduleList = await ReadResponseAsync<MaintenanceScheduleListDto>(schedulesResponse);
            ulong? selectedId = (maintenanceSchedules.SelectedItem as MaintenanceScheduleDto)?.Id;
            maintenanceSchedules.BeginUpdate();
            maintenanceSchedules.Items.Clear();
            foreach (MaintenanceScheduleDto schedule in scheduleList.Schedules)
                maintenanceSchedules.Items.Add(schedule);
            if (selectedId.HasValue)
            {
                for (var index = 0; index < maintenanceSchedules.Items.Count; ++index)
                {
                    if (maintenanceSchedules.Items[index] is MaintenanceScheduleDto item && item.Id == selectedId.Value)
                    {
                        maintenanceSchedules.SelectedIndex = index;
                        break;
                    }
                }
            }
            maintenanceSchedules.EndUpdate();
            using var capacityRequest=CreateAdminRequest(HttpMethod.Get,"/api/admin/room-capacity");
            using var capacityResponse=await http.SendAsync(capacityRequest);
            capacityResponse.EnsureSuccessStatusCode();
            using var capacity=JsonDocument.Parse(await capacityResponse.Content.ReadAsStringAsync());
            CapacityChanged?.Invoke(capacity.RootElement.GetProperty("maximumRooms").GetInt32());
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
            maintenanceStartAtUtc = (DateTime?)null,
            shutdownAtUtc = (DateTime?)null
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
        await SendAnnouncementAsync();
    }
    private Task OpenAnnouncementEditorAsync()
    {
        using var dialog=new AnnouncementEditorForm(announcement.Text,(text,tag,count,start,end)=>SendAnnouncementAsync(text,tag,count,start,end),announcementTag,announcementRepeatCount,announcementStartsAtUtc,announcementEndsAtUtc,ClearAnnouncementAsync);dialog.ShowDialog(this);return Task.CompletedTask;
    }
    private async Task SendAnnouncementAsync(string? text=null,string tag="",int repeatCount=1,DateTimeOffset? startsAtUtc=null,DateTimeOffset? endsAtUtc=null)
    {
        SetOperationStatus("안내 공지 전송 중...");
        using var request=CreateAdminRequest(HttpMethod.Put,"/api/admin/service/announcement",new{announcement=text??announcement.Text,tag,repeatCount,startsAtUtc,endsAtUtc});
        using var response=await http.SendAsync(request);await ReadResponseAsync<ServiceControlDto>(response);
        SetOperationStatus("안내 공지 전송 완료");await RefreshStatusAsync();
    }

    private async Task AddMaintenanceScheduleAsync()
    {
        if (maintenanceEndTime.Value <= maintenanceStartTime.Value)
            throw new InvalidOperationException("점검 종료 시각은 시작 시각보다 늦어야 합니다.");
        if (maintenanceEndTime.Value.ToUniversalTime() <= DateTime.UtcNow)
            throw new InvalidOperationException("이미 종료된 시각으로 점검을 예약할 수 없습니다.");

        SetOperationStatus("점검 예약 추가 중...");
        var body = new
        {
            startsAtUtc = maintenanceStartTime.Value.ToUniversalTime(),
            endsAtUtc = maintenanceEndTime.Value.ToUniversalTime(),
            announcement = scheduleAnnouncement.Text.Trim()
        };
        using var request = CreateAdminRequest(HttpMethod.Post, "/api/admin/service/schedules", body);
        using var response = await http.SendAsync(request);
        MaintenanceScheduleDto created = await ReadResponseAsync<MaintenanceScheduleDto>(response);
        Append($"[OK] 점검 예약 #{created.Id}을 추가했습니다.");
        mainLog($"[운영] 점검 예약 #{created.Id} 추가");
        SetOperationStatus("점검 예약 추가 완료");
        await RefreshStatusAsync();
    }

    private async Task UpdateSelectedMaintenanceScheduleAsync()
    {
        if (maintenanceSchedules.SelectedItem is not MaintenanceScheduleDto selected)
            throw new InvalidOperationException("변경할 점검 예약을 목록에서 선택하세요.");
        if (maintenanceEndTime.Value <= maintenanceStartTime.Value)
            throw new InvalidOperationException("점검 종료 시각은 시작 시각보다 늦어야 합니다.");
        if (maintenanceEndTime.Value.ToUniversalTime() <= DateTime.UtcNow)
            throw new InvalidOperationException("이미 종료된 시각으로 점검 예약을 변경할 수 없습니다.");

        SetOperationStatus("선택한 점검 예약 변경 중...");
        var body = new
        {
            startsAtUtc = maintenanceStartTime.Value.ToUniversalTime(),
            endsAtUtc = maintenanceEndTime.Value.ToUniversalTime(),
            announcement = scheduleAnnouncement.Text.Trim()
        };
        using var request = CreateAdminRequest(HttpMethod.Put, $"/api/admin/service/schedules/{selected.Id}", body);
        using var response = await http.SendAsync(request);
        MaintenanceScheduleDto updated = await ReadResponseAsync<MaintenanceScheduleDto>(response);
        Append($"[OK] 점검 예약 #{updated.Id}의 시간을 변경했습니다.");
        mainLog($"[운영] 점검 예약 #{updated.Id} 변경");
        SetOperationStatus("점검 예약 변경 완료");
        await RefreshStatusAsync();
    }

    private void LoadSelectedScheduleIntoEditor()
    {
        if (maintenanceSchedules.SelectedItem is not MaintenanceScheduleDto selected) return;
        maintenanceStartTime.Value = selected.StartsAtUtc.ToLocalTime();
        maintenanceEndTime.Value = selected.EndsAtUtc.ToLocalTime();
        scheduleAnnouncement.Text = selected.Announcement;
    }

    private async Task CancelMaintenanceScheduleAsync()
    {
        if (maintenanceSchedules.SelectedItem is not MaintenanceScheduleDto selected)
            throw new InvalidOperationException("취소할 점검 예약을 목록에서 선택하세요.");

        SetOperationStatus("점검 예약 취소 중...");
        using var request = CreateAdminRequest(HttpMethod.Delete, $"/api/admin/service/schedules/{selected.Id}");
        using var response = await http.SendAsync(request);
        string message = await ReadMessageAsync(response);
        Append($"[OK] {message} (#{selected.Id})");
        mainLog($"[운영] 점검 예약 #{selected.Id} 취소");
        SetOperationStatus("점검 예약 취소 완료");
        await RefreshStatusAsync();
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

    private async Task LoadCrashReportsAsync()
    {
        SetOperationStatus("크래시 보고서 불러오는 중...");
        using var request = CreateAdminRequest(HttpMethod.Get, "/api/admin/crash-reports?limit=100");
        using var response = await http.SendAsync(request);
        CrashReportListDto result = await ReadResponseAsync<CrashReportListDto>(response);
        operationLog.Clear();
        Append($"[수집] 최근 크래시 보고서 {result.Count}건");
        if (result.CrashReports.Count == 0)
        {
            Append("수집된 크래시 보고서가 없습니다.");
        }
        foreach (CrashReportDto report in result.CrashReports)
        {
            Append($"[{report.DetectedAtUtc.ToLocalTime():yyyy-MM-dd HH:mm:ss}] " +
                   $"{report.ProcessRole}/{report.PlayerRole ?? "역할 없음"} · {report.MapName} · 빌드 {report.BuildVersion}\n" +
                   $"  Report={report.ReportId} Match={report.MatchId ?? "없음"} TestRun={report.DiagnosticRunId}\n" +
                   $"  GameMode={report.GameMode} Log={report.LastLogPath}");
        }
        if (result.CrashReports.Count > 0)
        {
            using var viewer = new CrashReportDownloadForm(result.CrashReports.Select(r =>
                (r.ReportId, $"{r.DetectedAtUtc.ToLocalTime():MM-dd HH:mm:ss} · {r.MapName} · {r.ProcessRole}")).ToArray(),
                async id =>
                {
                    using var downloadRequest = CreateAdminRequest(HttpMethod.Get, $"/api/admin/crash-reports/{id}/artifacts");
                    using var artifactHttp = new HttpClient { Timeout=TimeSpan.FromMinutes(3) };
                    using var download = await artifactHttp.SendAsync(downloadRequest);
                    if (!download.IsSuccessStatusCode) throw new InvalidOperationException(
                        download.StatusCode==System.Net.HttpStatusCode.NotFound ? "해당 보고서에 업로드된 덤프/로그가 없습니다. JSON 보고서만 수집된 경우입니다." :
                        $"첨부파일 다운로드 실패: HTTP {(int)download.StatusCode} {download.ReasonPhrase}");
                    return await download.Content.ReadAsByteArrayAsync();
                });
            viewer.ShowDialog(this);
        }
        SetOperationStatus("크래시 보고서 확인 완료");
    }

    private Task OpenSurveyAdministrationAsync()
    {
        string key = LoadAdministratorKey();
        using var form = new SurveyAdministrationForm(baseUrl.Text.Trim(), key, message =>
        {
            Append(message);
            mainLog(message);
        });
        form.ShowDialog(this);
        return Task.CompletedTask;
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
