using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;

namespace ProjectProject01BuildServerHelper;

internal sealed class SurveyQuestionDraft
{
    public string Id { get; set; } = Guid.NewGuid().ToString("D");
    public string Type { get; set; } = "Choice";
    public string Prompt { get; set; } = string.Empty;
    public List<string> Options { get; set; } = [];
    public bool Required { get; set; } = true;

    public override string ToString()
    {
        string type = Type switch { "Text" => "주관식", "OX" => "OX", _ => "객관식" };
        return $"[{type}] {Prompt}{(Required ? " (필수)" : string.Empty)}";
    }
}

internal sealed class SurveyAdministrationForm : Form
{
    private readonly string administratorKey;
    private readonly Action<string> log;
    private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(8) };
    private readonly TextBox baseUrl;
    private readonly TextBox title = new() { Dock = DockStyle.Fill, MaxLength = 128 };
    private readonly TextBox description = new()
    {
        Dock = DockStyle.Fill, Multiline = true, Height = 58, MaxLength = 512,
        ScrollBars = ScrollBars.Vertical
    };
    private readonly ListBox questionList = new() { Dock = DockStyle.Fill, IntegralHeight = false };
    private readonly ListBox surveyList = new() { Dock = DockStyle.Fill, IntegralHeight = false };
    private readonly Label status = new() { Dock = DockStyle.Fill, AutoSize = true, Text = "준비" };
    private readonly List<SurveyQuestionDraft> questions = [];

    private sealed class SurveySummary
    {
        public string SurveyId { get; set; } = string.Empty;
        public string Title { get; set; } = string.Empty;
        public string Description { get; set; } = string.Empty;
        public List<SurveyQuestionDraft> Questions { get; set; } = [];
        public bool IsActive { get; set; }
        public DateTime CreatedAtUtc { get; set; }
        public DateTime? ActivatedAtUtc { get; set; }
        public DateTime? ClosedAtUtc { get; set; }
        public int ResponseCount { get; set; }

        public override string ToString()
        {
            string state = IsActive ? "진행 중" : ClosedAtUtc.HasValue ? "종료" : "초안";
            return $"[{state}] {Title} · {Questions.Count}문항 · {ResponseCount}응답";
        }
    }

    private sealed class SurveyListResponse
    {
        public List<SurveySummary> Surveys { get; set; } = [];
    }

    public SurveyAdministrationForm(string initialBaseUrl, string administratorKey, Action<string> log)
    {
        this.administratorKey = administratorKey;
        this.log = log;
        baseUrl = new TextBox { Dock = DockStyle.Fill, Text = initialBaseUrl };
        Text = "ProjectProject01 피드백/설문조사 관리";
        Width = 980;
        Height = 760;
        MinimumSize = new Size(850, 650);
        StartPosition = FormStartPosition.CenterParent;
        BuildUi();
        Shown += async (_, _) => await RefreshSurveysAsync();
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
            Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new Padding(10)
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 52));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 48));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var addressRow = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2 };
        addressRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
        addressRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        addressRow.Controls.Add(new Label { Text = "백엔드 주소", AutoSize = true, Padding = new Padding(0, 7, 0, 0) });
        addressRow.Controls.Add(baseUrl, 1, 0);
        root.Controls.Add(addressRow);

        var editor = new GroupBox { Text = "새 설문 초안", Dock = DockStyle.Fill, Padding = new Padding(8) };
        var editorLayout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 4 };
        editorLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90));
        editorLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        editorLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        editorLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        editorLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        editorLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        editorLayout.Controls.Add(new Label { Text = "제목", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, 0, 0);
        editorLayout.Controls.Add(title, 1, 0);
        editorLayout.Controls.Add(new Label { Text = "설명", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, 0, 1);
        editorLayout.Controls.Add(description, 1, 1);
        editorLayout.Controls.Add(new Label { Text = "문항", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, 0, 2);
        editorLayout.Controls.Add(questionList, 1, 2);
        var questionButtons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = true };
        AddButton(questionButtons, "객관식 추가", () => EditQuestionAsync("Choice"));
        AddButton(questionButtons, "주관식 추가", () => EditQuestionAsync("Text"));
        AddButton(questionButtons, "OX 추가", () => EditQuestionAsync("OX"));
        AddButton(questionButtons, "선택 문항 수정", EditSelectedQuestionAsync);
        AddButton(questionButtons, "선택 문항 삭제", DeleteSelectedQuestionAsync);
        AddButton(questionButtons, "위로", () => MoveSelectedQuestionAsync(-1));
        AddButton(questionButtons, "아래로", () => MoveSelectedQuestionAsync(1));
        editorLayout.Controls.Add(questionButtons, 1, 3);
        editor.Controls.Add(editorLayout);
        root.Controls.Add(editor);

        var createButtons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true };
        AddButton(createButtons, "설문 초안 저장", CreateSurveyAsync);
        AddButton(createButtons, "작성 내용 초기화", ResetDraftAsync);
        root.Controls.Add(createButtons);

        var existing = new GroupBox { Text = "저장된 설문", Dock = DockStyle.Fill, Padding = new Padding(8) };
        var existingLayout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2 };
        existingLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        existingLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        existingLayout.Controls.Add(surveyList);
        var existingButtons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = true };
        AddButton(existingButtons, "새로고침", RefreshSurveysAsync);
        AddButton(existingButtons, "선택 설문 활성화", ActivateSelectedAsync);
        AddButton(existingButtons, "선택 설문 종료", CloseSelectedAsync);
        AddButton(existingButtons, "선택 설문 삭제", DeleteSelectedSurveyAsync);
        AddButton(existingButtons, "결과/통계 보기", ViewResultsAsync);
        existingLayout.Controls.Add(existingButtons, 0, 1);
        existing.Controls.Add(existingLayout);
        root.Controls.Add(existing);
        root.Controls.Add(status);
        Controls.Add(root);
    }

    private static void AddButton(FlowLayoutPanel panel, string text, Func<Task> action)
    {
        var button = new Button { Text = text, AutoSize = true, Height = 32 };
        button.Click += async (_, _) =>
        {
            button.Enabled = false;
            try { await action(); }
            catch (Exception ex) { MessageBox.Show(ex.Message, "설문 관리 오류", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            finally { button.Enabled = true; }
        };
        panel.Controls.Add(button);
    }

    private Task EditQuestionAsync(string type)
    {
        using var editor = new SurveyQuestionEditorForm(new SurveyQuestionDraft { Type = type });
        if (editor.ShowDialog(this) == DialogResult.OK)
        {
            questions.Add(editor.Question);
            RefreshQuestionList();
        }
        return Task.CompletedTask;
    }

    private Task EditSelectedQuestionAsync()
    {
        if (questionList.SelectedIndex < 0) throw new InvalidOperationException("수정할 문항을 선택하세요.");
        int index = questionList.SelectedIndex;
        using var editor = new SurveyQuestionEditorForm(questions[index]);
        if (editor.ShowDialog(this) == DialogResult.OK)
        {
            questions[index] = editor.Question;
            RefreshQuestionList(index);
        }
        return Task.CompletedTask;
    }

    private Task DeleteSelectedQuestionAsync()
    {
        if (questionList.SelectedIndex < 0) throw new InvalidOperationException("삭제할 문항을 선택하세요.");
        int index = questionList.SelectedIndex;
        questions.RemoveAt(index);
        RefreshQuestionList(Math.Min(index, questions.Count - 1));
        return Task.CompletedTask;
    }

    private Task MoveSelectedQuestionAsync(int direction)
    {
        int from = questionList.SelectedIndex;
        int to = from + direction;
        if (from < 0 || to < 0 || to >= questions.Count) return Task.CompletedTask;
        (questions[from], questions[to]) = (questions[to], questions[from]);
        RefreshQuestionList(to);
        return Task.CompletedTask;
    }

    private void RefreshQuestionList(int selected = -1)
    {
        questionList.BeginUpdate();
        questionList.Items.Clear();
        foreach (SurveyQuestionDraft question in questions) questionList.Items.Add(question);
        questionList.EndUpdate();
        if (selected >= 0 && selected < questionList.Items.Count) questionList.SelectedIndex = selected;
    }

    private async Task CreateSurveyAsync()
    {
        string surveyTitle = title.Text.Trim();
        if (surveyTitle.Length == 0) throw new InvalidOperationException("설문 제목을 입력하세요.");
        if (questions.Count == 0) throw new InvalidOperationException("문항을 하나 이상 추가하세요.");
        SetStatus("설문 초안 저장 중...");
        object body = new
        {
            title = surveyTitle,
            description = description.Text.Trim(),
            questions = questions.Select(q => new
            {
                id = q.Id, type = q.Type, prompt = q.Prompt,
                options = q.Type == "OX" ? new[] { "O", "X" } : q.Options.ToArray(),
                required = q.Required
            }).ToArray()
        };
        using HttpResponseMessage response = await SendAdminAsync(HttpMethod.Post, "/api/admin/surveys", body);
        await EnsureSuccessAsync(response);
        log($"[설문] '{surveyTitle}' 초안을 저장했습니다. 활성화 전에는 클라이언트 버튼이 비활성입니다.");
        await ResetDraftAsync();
        await RefreshSurveysAsync();
        SetStatus("설문 초안 저장 완료");
    }

    private Task ResetDraftAsync()
    {
        title.Clear();
        description.Clear();
        questions.Clear();
        RefreshQuestionList();
        return Task.CompletedTask;
    }

    private async Task RefreshSurveysAsync()
    {
        SetStatus("설문 목록 확인 중...");
        using HttpResponseMessage response = await SendAdminAsync(HttpMethod.Get, "/api/admin/surveys");
        SurveyListResponse result = await ReadAsync<SurveyListResponse>(response);
        surveyList.BeginUpdate();
        surveyList.Items.Clear();
        foreach (SurveySummary survey in result.Surveys) surveyList.Items.Add(survey);
        surveyList.EndUpdate();
        SetStatus($"설문 {result.Surveys.Count}개 확인 완료");
    }

    private SurveySummary SelectedSurvey() => surveyList.SelectedItem as SurveySummary ??
        throw new InvalidOperationException("처리할 설문을 선택하세요.");

    private async Task ActivateSelectedAsync()
    {
        SurveySummary survey = SelectedSurvey();
        using HttpResponseMessage response = await SendAdminAsync(HttpMethod.Post,
            $"/api/admin/surveys/{Uri.EscapeDataString(survey.SurveyId)}/activate");
        await EnsureSuccessAsync(response);
        log($"[설문] '{survey.Title}' 활성화 — 클라이언트 피드백/설문조사 버튼이 활성화됩니다.");
        await RefreshSurveysAsync();
    }

    private async Task CloseSelectedAsync()
    {
        SurveySummary survey = SelectedSurvey();
        using HttpResponseMessage response = await SendAdminAsync(HttpMethod.Post,
            $"/api/admin/surveys/{Uri.EscapeDataString(survey.SurveyId)}/close");
        await EnsureSuccessAsync(response);
        log($"[설문] '{survey.Title}' 응답 접수를 종료했습니다.");
        await RefreshSurveysAsync();
    }

    private async Task DeleteSelectedSurveyAsync()
    {
        SurveySummary survey = SelectedSurvey();
        if (MessageBox.Show($"'{survey.Title}' 설문과 모든 응답을 삭제하시겠습니까?", "설문 삭제",
                MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
        using HttpResponseMessage response = await SendAdminAsync(HttpMethod.Delete,
            $"/api/admin/surveys/{Uri.EscapeDataString(survey.SurveyId)}");
        await EnsureSuccessAsync(response);
        log($"[설문] '{survey.Title}' 설문과 응답을 삭제했습니다.");
        await RefreshSurveysAsync();
    }

    private async Task ViewResultsAsync()
    {
        SurveySummary survey = SelectedSurvey();
        using HttpResponseMessage response = await SendAdminAsync(HttpMethod.Get,
            $"/api/admin/surveys/{Uri.EscapeDataString(survey.SurveyId)}/results");
        string json = await ReadTextEnsuringSuccessAsync(response);
        using var viewer = new SurveyResultsForm(survey.Title, json);
        viewer.ShowDialog(this);
    }

    private async Task<HttpResponseMessage> SendAdminAsync(HttpMethod method, string endpoint, object? body = null)
    {
        string root = baseUrl.Text.Trim().TrimEnd('/');
        if (!Uri.TryCreate(root, UriKind.Absolute, out Uri? uri) ||
            (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
            throw new InvalidOperationException("백엔드 주소가 올바르지 않습니다.");
        using var request = new HttpRequestMessage(method, root + endpoint);
        request.Headers.Add("X-ProjectProject01-Admin-Key", administratorKey);
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        if (body is not null)
            request.Content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json");
        return await http.SendAsync(request);
    }

    private static async Task<T> ReadAsync<T>(HttpResponseMessage response)
    {
        string json = await ReadTextEnsuringSuccessAsync(response);
        return JsonSerializer.Deserialize<T>(json, new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ??
               throw new InvalidOperationException("서버 응답을 읽지 못했습니다.");
    }

    private static async Task EnsureSuccessAsync(HttpResponseMessage response) =>
        _ = await ReadTextEnsuringSuccessAsync(response);

    private static async Task<string> ReadTextEnsuringSuccessAsync(HttpResponseMessage response)
    {
        string json = await response.Content.ReadAsStringAsync();
        if (response.IsSuccessStatusCode) return json;
        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            if (document.RootElement.TryGetProperty("message", out JsonElement message))
                throw new InvalidOperationException(message.GetString() ?? $"HTTP {(int)response.StatusCode}");
        }
        catch (JsonException) { }
        throw new InvalidOperationException($"서버 요청 실패: HTTP {(int)response.StatusCode}");
    }

    private void SetStatus(string value) => status.Text = value;
}

internal sealed class SurveyQuestionEditorForm : Form
{
    private readonly TextBox prompt = new() { Dock = DockStyle.Fill, MaxLength = 256 };
    private readonly ComboBox type = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 140 };
    private readonly CheckBox required = new() { Text = "필수 응답", AutoSize = true, Checked = true };
    private readonly TextBox options = new()
    {
        Dock = DockStyle.Fill, Multiline = true, ScrollBars = ScrollBars.Vertical,
        PlaceholderText = "객관식 선택지를 한 줄에 하나씩 입력 (2~10개)"
    };
    private readonly string questionId;
    public SurveyQuestionDraft Question { get; private set; }

    public SurveyQuestionEditorForm(SurveyQuestionDraft source)
    {
        questionId = string.IsNullOrWhiteSpace(source.Id) ? Guid.NewGuid().ToString("D") : source.Id;
        Question = source;
        Text = "설문 문항 편집";
        Width = 560;
        Height = 430;
        MinimumSize = new Size(500, 380);
        StartPosition = FormStartPosition.CenterParent;
        type.Items.AddRange(["객관식", "주관식", "OX"]);
        type.SelectedIndex = source.Type switch { "Text" => 1, "OX" => 2, _ => 0 };
        prompt.Text = source.Prompt;
        required.Checked = source.Required;
        options.Lines = source.Options.ToArray();
        type.SelectedIndexChanged += (_, _) => UpdateTypeUi();
        BuildUi();
        UpdateTypeUi();
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 4, Padding = new Padding(12)
        };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.Controls.Add(new Label { Text = "질문", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, 0, 0);
        root.Controls.Add(prompt, 1, 0);
        root.Controls.Add(new Label { Text = "유형", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, 0, 1);
        var typeRow = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        typeRow.Controls.Add(type);
        typeRow.Controls.Add(required);
        root.Controls.Add(typeRow, 1, 1);
        root.Controls.Add(new Label { Text = "선택지", AutoSize = true, Padding = new Padding(0, 7, 0, 0) }, 0, 2);
        root.Controls.Add(options, 1, 2);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Right, AutoSize = true };
        var save = new Button { Text = "확인", AutoSize = true };
        var cancel = new Button { Text = "취소", AutoSize = true, DialogResult = DialogResult.Cancel };
        save.Click += (_, _) => SaveQuestion();
        actions.Controls.Add(save);
        actions.Controls.Add(cancel);
        root.Controls.Add(actions, 1, 3);
        Controls.Add(root);
        CancelButton = cancel;
    }

    private void UpdateTypeUi()
    {
        bool isChoice = type.SelectedIndex == 0;
        bool isOx = type.SelectedIndex == 2;
        options.Enabled = isChoice;
        options.Text = isOx ? "O\r\nX" : isChoice ? options.Text : string.Empty;
    }

    private void SaveQuestion()
    {
        string questionPrompt = prompt.Text.Trim();
        if (questionPrompt.Length == 0)
        {
            MessageBox.Show("질문 문구를 입력하세요.", "문항 확인", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        string questionType = type.SelectedIndex switch { 1 => "Text", 2 => "OX", _ => "Choice" };
        List<string> choices = questionType switch
        {
            "OX" => ["O", "X"],
            "Choice" => options.Lines.Select(line => line.Trim()).Where(line => line.Length > 0).ToList(),
            _ => []
        };
        if (questionType == "Choice" && (choices.Count is < 2 or > 10 ||
            choices.Distinct(StringComparer.Ordinal).Count() != choices.Count))
        {
            MessageBox.Show("객관식 선택지는 중복 없이 2~10개를 입력하세요.", "문항 확인",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        Question = new SurveyQuestionDraft
        {
            Id = questionId, Type = questionType, Prompt = questionPrompt,
            Options = choices, Required = required.Checked
        };
        DialogResult = DialogResult.OK;
        Close();
    }
}

internal sealed class SurveyResultsForm : Form
{
    public SurveyResultsForm(string title, string json)
    {
        Text = $"설문 결과 - {title}";
        Width = 900;
        Height = 700;
        StartPosition = FormStartPosition.CenterParent;
        var tabs = new TabControl { Dock = DockStyle.Fill };
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement root = document.RootElement;
        int responseCount = root.GetProperty("responseCount").GetInt32();
        tabs.TabPages.Add(CreatePage("전체 통계", FormatStatistics(root, responseCount)));
        tabs.TabPages.Add(CreatePage("주관식 답변", FormatTextAnswers(root)));
        tabs.TabPages.Add(CreatePage("개별 응답", FormatIndividuals(root)));
        Controls.Add(tabs);
    }

    private static TabPage CreatePage(string name, string content)
    {
        var page = new TabPage(name);
        page.Controls.Add(new RichTextBox
        {
            Dock = DockStyle.Fill, ReadOnly = true, BackColor = Color.White,
            ForeColor = Color.Black, Font = new Font("맑은 고딕", 10), Text = content
        });
        return page;
    }

    private static string FormatStatistics(JsonElement root, int responseCount)
    {
        var text = new StringBuilder($"전체 응답: {responseCount}명\r\n\r\n");
        foreach (JsonElement question in root.GetProperty("choiceStatistics").EnumerateArray())
        {
            text.AppendLine(question.GetProperty("prompt").GetString()).AppendLine();
            int answered = question.GetProperty("options").EnumerateArray().Sum(option => option.GetProperty("count").GetInt32());
            foreach (JsonElement option in question.GetProperty("options").EnumerateArray())
            {
                int count = option.GetProperty("count").GetInt32();
                double percent = answered == 0 ? 0 : count * 100.0 / answered;
                text.AppendLine($"  {option.GetProperty("option").GetString()}: {count}명 ({percent:0.0}%)");
            }
            text.AppendLine($"  무응답: {Math.Max(0, responseCount - answered)}명").AppendLine();
        }
        return text.ToString();
    }

    private static string FormatTextAnswers(JsonElement root)
    {
        var text = new StringBuilder();
        foreach (JsonElement question in root.GetProperty("textAnswers").EnumerateArray())
        {
            text.AppendLine(question.GetProperty("prompt").GetString()).AppendLine(new string('-', 60));
            int index = 1;
            foreach (JsonElement answer in question.GetProperty("answers").EnumerateArray())
            {
                string writer = DisplayName(answer);
                text.AppendLine($"{index++}. {writer} · {answer.GetProperty("submittedAtUtc").GetDateTime().ToLocalTime():yyyy-MM-dd HH:mm:ss}");
                text.AppendLine(answer.GetProperty("value").GetString()).AppendLine();
            }
            if (index == 1) text.AppendLine("응답 없음\r\n");
        }
        return text.ToString();
    }

    private static string FormatIndividuals(JsonElement root)
    {
        var text = new StringBuilder();
        int index = 1;
        foreach (JsonElement response in root.GetProperty("individualResponses").EnumerateArray())
        {
            text.AppendLine($"응답 #{index++} · {DisplayName(response)} · " +
                            $"{response.GetProperty("submittedAtUtc").GetDateTime().ToLocalTime():yyyy-MM-dd HH:mm:ss}");
            text.AppendLine($"빌드 {response.GetProperty("buildVersion").GetString()} · " +
                            $"{response.GetProperty("mapName").GetString()} · " +
                            $"{response.GetProperty("processRole").GetString()}/{NullableString(response, "playerRole")}");
            foreach (JsonElement answer in response.GetProperty("answers").EnumerateArray())
                text.AppendLine($"  - {answer.GetProperty("prompt").GetString()}: {answer.GetProperty("value").GetString()}");
            text.AppendLine(new string('=', 70));
        }
        if (index == 1) text.AppendLine("수집된 응답이 없습니다.");
        return text.ToString();
    }

    private static string DisplayName(JsonElement element)
    {
        string? display = NullableString(element, "displayName");
        string? account = NullableString(element, "accountId");
        return !string.IsNullOrWhiteSpace(display) ? $"{display} ({account ?? "계정 없음"})" : account ?? "익명";
    }

    private static string? NullableString(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() : null;
}
