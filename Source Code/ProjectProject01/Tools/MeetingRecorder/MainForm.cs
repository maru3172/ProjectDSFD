using System.Diagnostics;
using System.Text.Json;

namespace MeetingRecorder;

/// <summary>
/// One window: record (microphone + PC sound such as Discord), type timestamped notes while it runs, then turn the
/// audio into a readable transcript and a summary. Every meeting is a folder under the storage folder.
/// </summary>
internal sealed class MainForm : Form
{
    private readonly MeetingSettings settings = MeetingSettings.Load();
    private readonly AudioRecorder recorder = new();
    private readonly TextBox title = new() { Text = "회의", Width = 260, Margin = new Padding(4, 6, 12, 4) };
    private readonly Button record = new() { Text = "녹음 시작", AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(10, 4, 10, 4) };
    private readonly CheckBox systemAudio = new() { Text = "PC 소리도 녹음 (Discord 등)", AutoSize = true, Margin = new Padding(12, 10, 12, 4) };
    private readonly Label elapsed = new() { Text = "00:00:00", AutoSize = true, Font = new Font("Segoe UI", 14, FontStyle.Bold), Margin = new Padding(8, 6, 4, 4) };
    private readonly LinkLabel storage = new() { AutoSize = true, Margin = new Padding(4, 8, 4, 4) };
    private readonly TextBox noteInput = new() { PlaceholderText = "메모 입력 후 Enter", Dock = DockStyle.Fill, Margin = new Padding(3, 5, 3, 3) };
    private readonly ListBox notes = new() { Dock = DockStyle.Fill, IntegralHeight = false };
    private readonly ListBox meetings = new() { Dock = DockStyle.Fill, IntegralHeight = false };
    private readonly ComboBox view = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160, Margin = new Padding(8, 2, 3, 3) };
    private readonly TextBox viewer = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, BackColor = SystemColors.Window };
    private readonly Label status = new() { Dock = DockStyle.Bottom, AutoEllipsis = true, TextAlign = ContentAlignment.MiddleLeft, Padding = new Padding(8, 0, 8, 0) };
    private readonly System.Windows.Forms.Timer tick = new() { Interval = 250 };
    private MeetingSession? current;private string? currentFolder;private DateTime textStarted;
    private CancellationTokenSource? work;
    // What the viewer can show, in this order: the summary (overview first, then everything), the transcript, the notes.
    private static readonly (string Name, string File)[] Views = [("요약 + 전체 내용", MeetingSession.SummaryFile), ("대화문", MeetingSession.TranscriptFile), ("텍스트 기록", MeetingSession.NotesFile)];

    public MainForm()
    {
        // Sizes follow the text and the screen scale (no fixed heights), so nothing is cut off at 125-200% scaling.
        AutoScaleMode = AutoScaleMode.Dpi;
        Text = "회의 기록";MinimumSize = new Size(1000, 720);Size = new Size(1180, 780);Font = new Font("Malgun Gothic", 10);
        systemAudio.Checked = settings.RecordSystemAudio;
        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(8, 6, 8, 2) };
        top.Controls.AddRange([new Label { Text = "제목", AutoSize = true, Margin = new Padding(4, 10, 0, 4) }, title, record, systemAudio, elapsed]);
        // Where meetings are saved, always visible; click to open it.
        var storageRow = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(8, 0, 8, 4) };
        var changeStorage = new Button { Text = "바꾸기", AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(6, 0, 6, 0), Margin = new Padding(8, 3, 4, 3) };
        changeStorage.Click += async (_, _) => await Guarded(ChangeStorage);
        storageRow.Controls.AddRange([new Label { Text = "저장 위치", AutoSize = true, Margin = new Padding(4, 8, 4, 4) }, storage, changeStorage]);
        storage.LinkClicked += (_, _) => { Directory.CreateDirectory(settings.StorageFolder); Process.Start("explorer.exe", settings.StorageFolder); };

        var important = new Button { Text = "중요 표시", AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(8, 2, 8, 2), Margin = new Padding(3) };
        important.Click += (_, _) => AddNote(true);
        var inputRow = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 2, Margin = Padding.Empty };
        inputRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));inputRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        inputRow.Controls.Add(noteInput, 0, 0);inputRow.Controls.Add(important, 1, 0);

        var notesPanel = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 4, ColumnCount = 1, Padding = new Padding(8) };
        notesPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));notesPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        notesPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));notesPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        notesPanel.Controls.Add(new Label { Text = "텍스트 기록", AutoSize = true, Font = new Font(Font, FontStyle.Bold) }, 0, 0);
        notesPanel.Controls.Add(inputRow, 0, 1);
        var hint = new Label { Text = "Enter로 추가합니다. 앞에 !를 붙이거나 '중요 표시'를 누르면 그 순간의 말이 요약 맨 위에 그대로 실립니다.",
            AutoSize = true, MaximumSize = new Size(400, 0), ForeColor = SystemColors.GrayText, Margin = new Padding(3, 2, 3, 6) };
        notesPanel.Controls.Add(hint, 0, 2);
        notesPanel.Controls.Add(notes, 0, 3);
        notesPanel.Resize += (_, _) => hint.MaximumSize = new Size(Math.Max(100, notesPanel.ClientSize.Width - 24), 0);

        // Buttons sit in a fixed grid (equal columns, text-high rows): they shrink with the window instead of pushing past it
        // or covering the list.
        var actions = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 2, Margin = new Padding(0, 6, 0, 6) };
        for (int c = 0; c < 2; c++) actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        int buttonHeight = TextRenderer.MeasureText("가", Font).Height + 16;
        void Action(string text, Func<Task> run)
        {
            var b = new Button { Text = text, Dock = DockStyle.Fill, Height = buttonHeight, AutoEllipsis = true, Margin = new Padding(2) };
            b.Click += async (_, _) => await Guarded(run); actions.Controls.Add(b);
        }
        Action("음성 → 텍스트", TranscribeSelected);
        Action("로컬 요약", () => SummarizeSelected(false));
        Action("Claude 요약", () => SummarizeSelected(true));
        Action("텍스트 가져오기", ImportText);
        Action("회의 폴더 열기", () => { if (Selected() is { } f) Process.Start("explorer.exe", f); return Task.CompletedTask; });
        Action("작업 취소", () => { work?.Cancel(); return Task.CompletedTask; });

        var viewRow = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Margin = Padding.Empty };
        viewRow.Controls.AddRange([new Label { Text = "내용 보기", AutoSize = true, Font = new Font(Font, FontStyle.Bold), Margin = new Padding(3, 6, 3, 3) }, view]);
        foreach (var (name, _) in Views) view.Items.Add(name);
        view.Width = Views.Max(v => TextRenderer.MeasureText(v.Name, Font).Width) + SystemInformation.VerticalScrollBarWidth + 16;
        view.SelectedIndex = 0;view.SelectedIndexChanged += (_, _) => ShowSelected();

        var meetingsPanel = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 5, ColumnCount = 1, Padding = new Padding(8) };
        meetingsPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));meetingsPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 30));
        meetingsPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));meetingsPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        meetingsPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 70));
        meetingsPanel.Controls.Add(new Label { Text = "회의 목록", AutoSize = true, Font = new Font(Font, FontStyle.Bold), Margin = new Padding(3, 0, 3, 4) }, 0, 0);
        meetingsPanel.Controls.Add(meetings, 0, 1);meetingsPanel.Controls.Add(actions, 0, 2);
        meetingsPanel.Controls.Add(viewRow, 0, 3);meetingsPanel.Controls.Add(viewer, 0, 4);
        meetings.SelectedIndexChanged += (_, _) => ShowSelected();

        var split = new SplitContainer { Dock = DockStyle.Fill };
        split.Panel1.Controls.Add(notesPanel);split.Panel2.Controls.Add(meetingsPanel);
        status.Height = TextRenderer.MeasureText("가", Font).Height + 12;
        Controls.Add(split);Controls.Add(status);Controls.Add(storageRow);Controls.Add(top);
        Load += (_, _) => split.SplitterDistance = Math.Max(300, split.Width * 2 / 5);

        record.Click += async (_, _) => await Guarded(ToggleRecording);
        noteInput.KeyDown += (_, e) => { if (e.KeyCode == Keys.Enter) { e.SuppressKeyPress = true; AddNote(false); } };
        systemAudio.CheckedChanged += (_, _) => { settings.RecordSystemAudio = systemAudio.Checked; settings.Save(); };
        tick.Tick += (_, _) => { if (recorder.IsRecording) elapsed.Text = TranscriptFormatter.Clock(recorder.Elapsed.TotalSeconds); };
        recorder.Failed += message => BeginInvoke(() => SetStatus($"녹음 장치 오류: {message}"));
        FormClosing += (_, e) =>
        {
            if (recorder.IsRecording && MessageBox.Show(this, "녹음 중입니다. 녹음을 저장하고 닫을까요?", "회의 기록", MessageBoxButtons.YesNo) != DialogResult.Yes) { e.Cancel = true; return; }
            FinishCurrent();
        };
        tick.Start();RefreshMeetings();
        storage.Text = settings.StorageFolder;SetStatus("녹음 시작을 누르거나 메모를 쓰면 새 회의가 만들어집니다.");
    }

    private async Task Guarded(Func<Task> run)
    {
        try { await run(); }
        catch (OperationCanceledException) { SetStatus("취소했습니다."); }
        catch (Exception ex) { SetStatus(ex.Message); MessageBox.Show(this, ex.Message, "회의 기록", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
    }

    private void SetStatus(string text) => status.Text = text;

    private Task ToggleRecording()
    {
        if (recorder.IsRecording) { FinishCurrent(); return Task.CompletedTask; }
        if (current == null) BeginSession();
        current!.Tracks = recorder.Start(currentFolder!, settings).ToList();
        current.Save(currentFolder!);
        record.Text = "녹음 중지";title.Enabled = false;systemAudio.Enabled = false;
        SetStatus(settings.RecordSystemAudio ? "녹음 중: 마이크와 PC 소리를 따로 저장합니다." : "녹음 중: 마이크만 저장합니다.");
        return Task.CompletedTask;
    }

    private void BeginSession()
    {
        var started = DateTime.Now;
        current = new MeetingSession { Title = title.Text.Trim().Length > 0 ? title.Text.Trim() : "회의", StartedLocal = started };
        currentFolder = MeetingSession.CreateFolder(settings.StorageFolder, current.Title, started);
        textStarted = started;notes.Items.Clear();
    }

    private double NowSeconds() => recorder.IsRecording ? recorder.Elapsed.TotalSeconds : (DateTime.Now - textStarted).TotalSeconds;

    // Typed text is stored exactly as typed; "!" in front (or the 중요 표시 button) only marks it important.
    private void AddNote(bool important)
    {
        var text = noteInput.Text.Trim();
        if (text.StartsWith('!')) { important = true; text = text[1..].Trim(); }
        if (text.Length == 0) { if (!important) return; text = "중요 구간"; }
        if (current == null) BeginSession(); // A text-only meeting needs no recording.
        var note = new MeetingNote(NowSeconds(), text, important);current!.Notes.Add(note);current.Save(currentFolder!);
        notes.Items.Add($"[{TranscriptFormatter.Clock(note.Seconds)}] {(important ? "[중요] " : "")}{text}");noteInput.Clear();noteInput.Focus();
        RefreshMeetings(currentFolder);
    }

    private void FinishCurrent()
    {
        if (current == null || currentFolder == null) return;
        if (recorder.IsRecording) current.DurationSeconds = recorder.Stop();
        else current.DurationSeconds = Math.Max(current.DurationSeconds, (DateTime.Now - textStarted).TotalSeconds);
        current.Save(currentFolder);
        var folder = currentFolder;current = null;currentFolder = null;
        record.Text = "녹음 시작";title.Enabled = true;systemAudio.Enabled = true;elapsed.Text = "00:00:00";
        SetStatus($"저장했습니다: {folder}");RefreshMeetings(folder);
    }

    private void RefreshMeetings(string? select = null)
    {
        meetings.Items.Clear();
        if (Directory.Exists(settings.StorageFolder))
            foreach (var dir in Directory.GetDirectories(settings.StorageFolder).Where(d => File.Exists(Path.Combine(d, MeetingSession.MetaFile))).OrderByDescending(d => d))
                meetings.Items.Add(Path.GetFileName(dir));
        if (select != null) meetings.SelectedItem = Path.GetFileName(select);
        else if (meetings.Items.Count > 0) meetings.SelectedIndex = 0;
        ShowSelected();
    }

    /// <summary>Shows the chosen file of the selected meeting; a missing summary falls back to what exists, with a hint.</summary>
    private void ShowSelected()
    {
        if (meetings.SelectedItem is not string name) { viewer.Text = ""; return; }
        var folder = Path.Combine(settings.StorageFolder, name);
        var (_, file) = Views[Math.Max(0, view.SelectedIndex)];
        var path = Path.Combine(folder, file);
        if (File.Exists(path)) { viewer.Text = File.ReadAllText(path).ReplaceLineEndings("\r\n"); return; }
        var fallback = Views.Select(v => Path.Combine(folder, v.File)).FirstOrDefault(File.Exists);
        var hint = file == MeetingSession.SummaryFile ? "아직 요약이 없습니다. '음성 → 텍스트' 다음 '로컬 요약'이나 'Claude 요약'을 누르세요." : "아직 이 파일이 없습니다.";
        viewer.Text = (hint + "\r\n\r\n" + (fallback != null ? File.ReadAllText(fallback).ReplaceLineEndings("\r\n") : "")).TrimEnd();
    }

    private string? Selected()
    {
        if (meetings.SelectedItem is not string name) { SetStatus("회의를 먼저 고르세요."); return null; }
        return Path.Combine(settings.StorageFolder, name);
    }

    private (string Folder, MeetingSession Session)? SelectedSession()
    {
        if (Selected() is not { } folder) return null;
        if (folder == currentFolder) { SetStatus("녹음이 끝난 회의만 변환할 수 있습니다."); return null; }
        return MeetingSession.Load(folder) is { } s ? (folder, s) : null;
    }

    private async Task TranscribeSelected()
    {
        if (SelectedSession() is not var (folder, session)) return;
        if (session.Tracks.Count == 0) { SetStatus("이 회의에는 녹음이 없습니다(텍스트 기록만)."); return; }
        var progress = new Progress<string>(SetStatus);
        work = new CancellationTokenSource();
        if (!Transcriber.HasModel(settings.WhisperModel))
        {
            if (MessageBox.Show(this, $"음성 인식 모델(Whisper {settings.WhisperModel})을 처음 한 번 내려받습니다. 받을까요?\n저장 위치: {Transcriber.ModelFolder}",
                "회의 기록", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
            await Transcriber.DownloadModelAsync(settings.WhisperModel, progress, work.Token);
        }
        var segments = await Task.Run(() => Transcriber.TranscribeAsync(folder, session, settings, progress, work.Token));
        File.WriteAllText(Path.Combine(folder, MeetingSession.RawTranscriptFile), JsonSerializer.Serialize(segments, new JsonSerializerOptions { WriteIndented = true }));
        File.WriteAllText(Path.Combine(folder, MeetingSession.TranscriptFile), TranscriptFormatter.TranscriptMarkdown(session, segments));
        // The local summary needs no network, so it is written right away; 'Claude 요약' can replace it later.
        File.WriteAllText(Path.Combine(folder, MeetingSession.SummaryFile), Summarizer.Local(session, segments));
        SetStatus($"텍스트로 바꾸고 요약을 저장했습니다: {MeetingSession.SummaryFile}");
        view.SelectedIndex = 0;ShowSelected();
    }

    private static List<TranscriptSegment> LoadSegments(string folder)
    {
        var path = Path.Combine(folder, MeetingSession.RawTranscriptFile);
        return File.Exists(path) ? JsonSerializer.Deserialize<List<TranscriptSegment>>(File.ReadAllText(path)) ?? [] : [];
    }

    private async Task SummarizeSelected(bool claude)
    {
        if (SelectedSession() is not var (folder, session)) return;
        var segments = LoadSegments(folder);
        if (segments.Count == 0 && session.Notes.Count == 0) { SetStatus("요약할 내용이 없습니다. 먼저 '음성 → 텍스트'를 하세요."); return; }
        string summary;
        if (claude)
        {
            if (MessageBox.Show(this, "회의 내용을 Claude API로 보내 요약합니다. 보낼까요?", "회의 기록", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
            SetStatus("Claude로 요약하는 중입니다…");work = new CancellationTokenSource();
            summary = await Summarizer.WithClaudeAsync(session, segments, settings.ClaudeModel, work.Token);
        }
        else summary = Summarizer.Local(session, segments);
        File.WriteAllText(Path.Combine(folder, MeetingSession.SummaryFile), summary);
        SetStatus($"요약을 저장했습니다: {MeetingSession.SummaryFile}");
        view.SelectedIndex = 0;ShowSelected();
    }

    // A chat log or notes typed elsewhere (e.g. a copied Discord text channel) joins the meeting's text record as is.
    private Task ImportText()
    {
        if (SelectedSession() is not var (folder, session)) return Task.CompletedTask;
        using var dialog = new OpenFileDialog { Filter = "텍스트|*.txt;*.md|모든 파일|*.*" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return Task.CompletedTask;
        var lines = File.ReadAllLines(dialog.FileName).Select(l => l.Trim()).Where(l => l.Length > 0).ToList();
        double at = session.DurationSeconds;
        foreach (var line in lines) session.Notes.Add(new MeetingNote(at, line));
        session.Save(folder);SetStatus($"{lines.Count}줄을 텍스트 기록에 추가했습니다.");ShowSelected();
        return Task.CompletedTask;
    }

    private Task ChangeStorage()
    {
        using var dialog = new FolderBrowserDialog { SelectedPath = settings.StorageFolder };
        if (dialog.ShowDialog(this) != DialogResult.OK) return Task.CompletedTask;
        settings.StorageFolder = dialog.SelectedPath;settings.Save();storage.Text = settings.StorageFolder;RefreshMeetings();SetStatus($"저장 위치: {settings.StorageFolder}");
        return Task.CompletedTask;
    }

    protected override void Dispose(bool disposing) { if (disposing) { recorder.Dispose(); tick.Dispose(); work?.Dispose(); } base.Dispose(disposing); }
}
