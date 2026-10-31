using System.Diagnostics;
using System.Text.Json;

namespace ProjectDFSDGitManager;

internal sealed class ManagerForm : Form
{
    private readonly TextBox repository = new() { Dock = DockStyle.Fill };
    private readonly Label location = new() { AutoSize = false, Dock = DockStyle.Fill, AutoEllipsis = true, MinimumSize = new Size(0, 38) };
    private readonly Label status = new() { Dock = DockStyle.Fill, Text = "저장소를 선택하세요.", AutoEllipsis = true, TextAlign = ContentAlignment.MiddleLeft };
    private readonly TabControl tabs = new() { Dock = DockStyle.Fill, Multiline = true };
    private readonly TextBox output = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false };
    private readonly ComboBox source = Combo(), target = Combo(), historyRef = Combo(), remote = Combo();
    private readonly DataGridView assets = Grid(), changes = Grid(), history = Grid(), branches = Grid(), stashes = Grid(), worktrees = Grid();
    private readonly TextBox commitMessage = new() { Multiline = true, Dock = DockStyle.Fill, ScrollBars = ScrollBars.Vertical };
    private readonly TextBox details = new() { Multiline = true, Dock = DockStyle.Fill, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false };
    private readonly TextBox assetReason = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true };
    private readonly Label mergeSummary = new() { Dock = DockStyle.Fill, AutoEllipsis = true, MinimumSize = new Size(0, 38) };
    private readonly Label remoteDetails = new() { AutoSize = true, MaximumSize = new Size(1100, 0) };
    private readonly Button prepare;
    private readonly TextBox advanced = new() { Width = 740, PlaceholderText = "예: log --oneline --all --graph -30" };
    private readonly CheckBox amend = new() { Text = "마지막 커밋 수정 (amend)", AutoSize = true };
    private GitService? git;
    private List<BranchInfo> branchData = [];
    private MergePlan? plan;
    private string? planFile, previewPath;
    private bool busy, loading;
    internal List<string> ActionNames { get; } = [];
    internal TabControl Pages => tabs;
    internal bool SuppressInitialRefresh { get; set; }
    internal bool Busy => busy;

    public ManagerForm(string? initialRepository)
    {
        Text = "ProjectDFSD · Git 관리"; StartPosition = FormStartPosition.CenterScreen;
        Size = new Size(1360, 900); MinimumSize = new Size(1080, 740); AutoScaleMode = AutoScaleMode.None;
        Font = new Font("맑은 고딕", 9.5f);
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new Padding(14) };
        root.ColumnStyles.Add(new(SizeType.Percent, 100));
        root.RowStyles.Add(new(SizeType.AutoSize)); root.RowStyles.Add(new(SizeType.AutoSize));
        root.RowStyles.Add(new(SizeType.Percent, 100)); root.RowStyles.Add(new(SizeType.Absolute, 108)); root.RowStyles.Add(new(SizeType.AutoSize));
        var top = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 6 };
        top.AutoSize = true; top.AutoSizeMode = AutoSizeMode.GrowAndShrink;
        top.ColumnStyles.Add(new(SizeType.AutoSize)); top.ColumnStyles.Add(new(SizeType.Percent, 100));
        top.ColumnStyles.Add(new(SizeType.AutoSize)); top.ColumnStyles.Add(new(SizeType.AutoSize));
        top.ColumnStyles.Add(new(SizeType.AutoSize)); top.ColumnStyles.Add(new(SizeType.AutoSize));
        top.Controls.Add(new Label { Text = "저장소", AutoSize = true, Anchor = AnchorStyles.Left }); top.Controls.Add(repository);
        top.Controls.Add(Button("찾아보기", Browse)); top.Controls.Add(Button("새로고침", () => RefreshRepository(repository.Text)));
        top.Controls.Add(Button("복제", CloneRepository)); top.Controls.Add(Button("새 저장소", InitRepository));
        root.Controls.Add(top, 0, 0); root.Controls.Add(location, 0, 1); root.Controls.Add(tabs, 0, 2);

        var merge = Page("에셋 보호 병합");
        var mergeLayout = PageLayout(5); mergeLayout.RowStyles.Add(new(SizeType.AutoSize)); mergeLayout.RowStyles.Add(new(SizeType.Percent, 100));
        mergeLayout.RowStyles.Add(new(SizeType.Absolute, 62)); mergeLayout.RowStyles.Add(new(SizeType.AutoSize)); mergeLayout.RowStyles.Add(new(SizeType.AutoSize));
        var selectors = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 5, RowCount = 2 };
        selectors.AutoSize = true; selectors.AutoSizeMode = AutoSizeMode.GrowAndShrink;
        selectors.RowStyles.Add(new(SizeType.AutoSize)); selectors.RowStyles.Add(new(SizeType.AutoSize));
        selectors.ColumnStyles.Add(new(SizeType.Percent, 50)); selectors.ColumnStyles.Add(new(SizeType.AutoSize));
        selectors.ColumnStyles.Add(new(SizeType.Percent, 50)); selectors.ColumnStyles.Add(new(SizeType.AutoSize)); selectors.ColumnStyles.Add(new(SizeType.AutoSize));
        selectors.Controls.Add(new Label { Text = "가져올 브랜치", AutoSize = true }, 0, 0);
        selectors.Controls.Add(new Label { Text = "병합 대상 브랜치", AutoSize = true }, 2, 0);
        source.Dock = target.Dock = DockStyle.Fill; selectors.Controls.Add(source, 0, 1); selectors.Controls.Add(target, 2, 1);
        selectors.Controls.Add(Button("⇄", SwapBranches), 1, 1); selectors.Controls.Add(Button("브랜치 비교", Compare), 3, 1);
        selectors.Controls.Add(Button("보고서 열기", OpenReport), 4, 1);
        mergeLayout.Controls.Add(selectors, 0, 0);
        assets.Columns.Add("Path", "에셋 경로"); assets.Columns[0].FillWeight = 56;
        assets.Columns.Add("Kind", "판정"); assets.Columns[1].FillWeight = 14;
        assets.Columns.Add("Default", "기본 선택"); assets.Columns[2].FillWeight = 15;
        assets.Columns.Add(new DataGridViewComboBoxColumn { Name = "Selection", HeaderText = "사용할 버전", FillWeight = 15,
            Items = { "자동 판정", "가져올 브랜치", "대상 브랜치" }, FlatStyle = FlatStyle.Flat });
        assets.Columns[3].ReadOnly = false; assets.ReadOnly = false;
        for (int i = 0; i < 3; i++) assets.Columns[i].ReadOnly = true;
        assets.CurrentCellDirtyStateChanged += (_, _) => { if (assets.IsCurrentCellDirty) assets.CommitEdit(DataGridViewDataErrorContexts.Commit); };
        assets.CellValueChanged += (_, _) => UpdateMergeSummary();
        assets.SelectionChanged += (_, _) => { if (assets.CurrentRow?.Tag is AssetDecision item) assetReason.Text = item.Reason; };
        assets.DataError += (_, e) => { e.ThrowException = false; status.Text = "버전 선택을 다시 확인하세요."; };
        mergeLayout.Controls.Add(assets, 0, 1); mergeLayout.Controls.Add(assetReason, 0, 2);
        var mergeActions = Flow(); prepare = Button("선택대로 병합 준비", Prepare); prepare.Enabled = false;
        mergeActions.Controls.Add(prepare); mergeActions.Controls.Add(Button("검토 폴더 열기", () => { OpenPath(previewPath); return Task.CompletedTask; }));
        mergeActions.Controls.Add(Button("검토 폴더를 관리 대상으로 열기", () => RefreshRepository(previewPath ?? throw new InvalidOperationException("먼저 병합을 준비하세요."))));
        mergeLayout.Controls.Add(mergeActions, 0, 3); mergeLayout.Controls.Add(mergeSummary, 0, 4); merge.Controls.Add(mergeLayout);
        source.SelectedIndexChanged += (_, _) => InvalidatePlan(); target.SelectedIndexChanged += (_, _) => InvalidatePlan();

        var changePage = Page("변경 · 커밋"); var changeLayout = PageLayout(3);
        changeLayout.RowStyles.Add(new(SizeType.Percent, 100)); changeLayout.RowStyles.Add(new(SizeType.AutoSize)); changeLayout.RowStyles.Add(new(SizeType.Absolute, 140));
        changes.Columns.Add("Index", "스테이지"); changes.Columns[0].FillWeight = 10;
        changes.Columns.Add("Work", "작업 폴더"); changes.Columns[1].FillWeight = 10; changes.Columns.Add("Path", "파일 경로"); changes.Columns[2].FillWeight = 80;
        changeLayout.Controls.Add(changes, 0, 0);
        var fileActions = Flow();
        fileActions.Controls.Add(Button("선택 스테이지", () => FileAction("add")));
        fileActions.Controls.Add(Button("전체 스테이지", () => Execute("전체 변경을 스테이지에 올립니다.", ["add", "--all"])));
        fileActions.Controls.Add(Button("선택 스테이지 취소", Unstage));
        fileActions.Controls.Add(Button("선택 변경 비교", FileDiff));
        fileActions.Controls.Add(Button("선택 작업 변경 버리기", Discard));
        changeLayout.Controls.Add(fileActions, 0, 1);
        var commitLayout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2 };
        commitLayout.ColumnStyles.Add(new(SizeType.Percent, 100)); commitLayout.ColumnStyles.Add(new(SizeType.AutoSize));
        commitMessage.PlaceholderText = "커밋 메시지"; commitLayout.Controls.Add(commitMessage);
        var commitActions = Flow(); commitActions.FlowDirection = FlowDirection.TopDown; commitActions.Controls.Add(amend);
        commitActions.Controls.Add(Button("커밋 만들기 / 수정", Commit));
        commitActions.Controls.Add(Button("마지막 커밋 취소 · 변경 유지", () => Execute("마지막 커밋만 취소하고 변경 내용은 스테이지에 유지합니다. 공개한 이력이라면 이후 push에 주의하세요.", ["reset", "--soft", "HEAD^"], true)));
        commitLayout.Controls.Add(commitActions); changeLayout.Controls.Add(commitLayout, 0, 2); changePage.Controls.Add(changeLayout);

        var historyPage = Page("커밋 이력"); var historyLayout = PageLayout(3);
        historyLayout.RowStyles.Add(new(SizeType.AutoSize)); historyLayout.RowStyles.Add(new(SizeType.Percent, 50)); historyLayout.RowStyles.Add(new(SizeType.Percent, 50));
        var historyActions = Flow(); historyRef.Width = 245; historyActions.Controls.Add(historyRef);
        historyActions.Controls.Add(Button("이력 보기", LoadHistory)); historyActions.Controls.Add(Button("선택 커밋 revert", Revert));
        historyActions.Controls.Add(Button("cherry-pick", CherryPick)); historyActions.Controls.Add(Button("이 커밋으로 reset", Reset));
        historyActions.Controls.Add(Button("커밋 메시지 수정", Reword)); historyActions.Controls.Add(Button("커밋 삭제", DropCommit));
        historyActions.Controls.Add(Button("태그 만들기", CreateTag)); historyActions.Controls.Add(Button("그래프", () => ReadCommand(["log", "--oneline", "--graph", "--all", "--decorate", "-80"])));
        historyActions.Controls.Add(Button("커밋 변경 비교", async () => ShowText("커밋 변경", (await Service.Git("show", "--no-ext-diff", SelectedCommit().Hash, "--")).Output)));
        historyLayout.Controls.Add(historyActions, 0, 0);
        history.Columns.Add("Hash", "커밋"); history.Columns[0].FillWeight = 12; history.Columns.Add("Date", "일시"); history.Columns[1].FillWeight = 23;
        history.Columns.Add("Author", "작성자"); history.Columns[2].FillWeight = 15; history.Columns.Add("Subject", "내용"); history.Columns[3].FillWeight = 50;
        history.SelectionChanged += async (_, _) => { if (!busy && !loading && history.CurrentRow?.Tag is CommitInfo selected) await ReadSelectedCommit(selected.Hash); };
        historyLayout.Controls.Add(history, 0, 1); historyLayout.Controls.Add(details, 0, 2); historyPage.Controls.Add(historyLayout);

        var branchPage = Page("브랜치"); var branchLayout = PageLayout(2);
        branchLayout.RowStyles.Add(new(SizeType.AutoSize)); branchLayout.RowStyles.Add(new(SizeType.Percent, 100));
        var branchActions = Flow();
        branchActions.Controls.Add(Button("선택 체크아웃", Checkout)); branchActions.Controls.Add(Button("새 브랜치", CreateBranch));
        branchActions.Controls.Add(Button("이름 변경", RenameBranch)); branchActions.Controls.Add(Button("브랜치 삭제", DeleteBranch));
        branchActions.Controls.Add(Button("현재 브랜치에 일반 merge", MergeCurrent)); branchActions.Controls.Add(Button("현재 브랜치 rebase", Rebase));
        branchActions.Controls.Add(Button("merge / rebase 계속", Continue)); branchActions.Controls.Add(Button("merge / rebase 중단", Abort));
        branchActions.Controls.Add(Button("cherry-pick / revert 계속", ContinueOther)); branchActions.Controls.Add(Button("cherry-pick / revert 중단", AbortOther));
        branchLayout.Controls.Add(branchActions, 0, 0);
        branches.Columns.Add("Name", "브랜치"); branches.Columns[0].FillWeight = 23;
        branches.Columns.Add("Type", "구분"); branches.Columns[1].FillWeight = 9;
        branches.Columns.Add("Hash", "최신 커밋"); branches.Columns[2].FillWeight = 12;
        branches.Columns.Add("Date", "최신 변경 일시"); branches.Columns[3].FillWeight = 21;
        branches.Columns.Add("Subject", "최신 커밋 내용"); branches.Columns[4].FillWeight = 35;
        branchLayout.Controls.Add(branches, 0, 1); branchPage.Controls.Add(branchLayout);

        var stashPage = Page("스태시"); var stashLayout = PageLayout(2);
        stashLayout.RowStyles.Add(new(SizeType.AutoSize)); stashLayout.RowStyles.Add(new(SizeType.Percent, 100));
        var stashActions = Flow(); stashActions.Controls.Add(Button("변경 임시 보관", StashSave));
        stashActions.Controls.Add(Button("선택 내용 보기", StashShow)); stashActions.Controls.Add(Button("적용 · 보관 유지", () => StashAction("apply")));
        stashActions.Controls.Add(Button("적용 · 보관 제거", () => StashAction("pop"))); stashActions.Controls.Add(Button("선택 보관 삭제", () => StashAction("drop")));
        stashLayout.Controls.Add(stashActions, 0, 0); stashes.Columns.Add("Ref", "이름"); stashes.Columns[0].FillWeight = 15;
        stashes.Columns.Add("Message", "보관 내용"); stashes.Columns[1].FillWeight = 85; stashLayout.Controls.Add(stashes, 0, 1); stashPage.Controls.Add(stashLayout);

        var remotePage = Page("원격 · 태그 · LFS"); var remoteLayout = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Padding = new Padding(10), AutoScroll = true };
        var remoteActions = Flow(); remote.Width = 180; remoteActions.Controls.Add(remote);
        remoteActions.Controls.Add(Button("fetch", () => RemoteAction("fetch"))); remoteActions.Controls.Add(Button("pull · fast-forward", () => RemoteAction("pull")));
        remoteActions.Controls.Add(Button("push", () => RemoteAction("push"))); remoteActions.Controls.Add(Button("안전 강제 push", () => RemoteAction("force")));
        remoteLayout.Controls.Add(remoteActions); remoteLayout.Controls.Add(remoteDetails);
        var remoteEdit = Flow(); remoteEdit.Controls.Add(Button("원격 추가", AddRemote)); remoteEdit.Controls.Add(Button("원격 주소 변경", ChangeRemote)); remoteEdit.Controls.Add(Button("원격 제거", RemoveRemote));
        remoteEdit.Controls.Add(Button("원격 브랜치 삭제", DeleteRemoteBranch)); remoteLayout.Controls.Add(remoteEdit);
        var tagsActions = Flow(); tagsActions.Controls.Add(Button("태그 목록", () => ReadCommand(["tag", "-n"]))); tagsActions.Controls.Add(Button("태그 삭제", DeleteTag));
        tagsActions.Controls.Add(Button("선택 원격에 태그 push", () => Execute("모든 로컬 태그를 선택한 원격에 올립니다.", ["push", SelectedRemote(), "--tags"]))); remoteLayout.Controls.Add(tagsActions);
        var lfsActions = Flow(); lfsActions.Controls.Add(Button("LFS 상태", () => ReadCommand(["lfs", "status"])));
        lfsActions.Controls.Add(Button("LFS pull", () => Execute("현재 체크아웃의 LFS 객체를 내려받아 작업 파일에 반영합니다.", ["lfs", "pull"])));
        lfsActions.Controls.Add(Button("LFS checkout", () => Execute("로컬에 있는 LFS 객체를 작업 파일로 복원합니다.", ["lfs", "checkout"])));
        lfsActions.Controls.Add(Button("객체 무결성 검사", () => ReadCommand(["fsck", "--no-reflogs"]))); remoteLayout.Controls.Add(lfsActions); remotePage.Controls.Add(remoteLayout);
        remoteLayout.SizeChanged += (_, _) => { foreach (Control child in remoteLayout.Controls) if (child is FlowLayoutPanel) child.Width = Math.Max(600, remoteLayout.ClientSize.Width - 28); };

        var treePage = Page("작업 폴더 · 복구"); var treeLayout = PageLayout(2);
        treeLayout.RowStyles.Add(new(SizeType.AutoSize)); treeLayout.RowStyles.Add(new(SizeType.Percent, 100));
        var treeActions = Flow(); treeActions.Controls.Add(Button("worktree 추가", AddWorktree));
        treeActions.Controls.Add(Button("선택 폴더 열기", () => { OpenPath(SelectedWorktree().Path); return Task.CompletedTask; }));
        treeActions.Controls.Add(Button("선택 폴더 관리", () => RefreshRepository(SelectedWorktree().Path)));
        treeActions.Controls.Add(Button("선택 worktree 제거", RemoveWorktree)); treeActions.Controls.Add(Button("worktree 정리", () => Execute("존재하지 않는 worktree의 관리 정보만 정리합니다.", ["worktree", "prune"])));
        treeActions.Controls.Add(Button("reflog 확인", () => ReadCommand(["reflog", "-80", "--date=local"])));
        treeActions.Controls.Add(Button("미추적 파일 정리", Clean));
        treeLayout.Controls.Add(treeActions, 0, 0); worktrees.Columns.Add("Path", "작업 폴더"); worktrees.Columns[0].FillWeight = 55;
        worktrees.Columns.Add("Branch", "브랜치"); worktrees.Columns[1].FillWeight = 30; worktrees.Columns.Add("Hash", "커밋"); worktrees.Columns[2].FillWeight = 15;
        treeLayout.Controls.Add(worktrees, 0, 1); treePage.Controls.Add(treeLayout);

        var advancedPage = Page("추가 Git 명령"); var advancedLayout = Flow(); advancedLayout.Dock = DockStyle.Top; advancedLayout.Height = 100;
        advancedLayout.Controls.Add(new Label { Text = "git", Width = 28, TextAlign = ContentAlignment.MiddleLeft }); advancedLayout.Controls.Add(advanced);
        advancedLayout.Controls.Add(Button("명령 실행", Advanced)); advancedPage.Controls.Add(advancedLayout);
        root.Controls.Add(output, 0, 3); var footer = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2 };
        footer.ColumnStyles.Add(new(SizeType.Percent, 100)); footer.ColumnStyles.Add(new(SizeType.AutoSize)); footer.Controls.Add(status);
        footer.Controls.Add(Button("로그 복사", () => { if (output.TextLength > 0) Clipboard.SetText(output.Text); return Task.CompletedTask; })); root.Controls.Add(footer, 0, 4);
        Controls.Add(root); ApplyTheme(this);
        FormClosing += (_, e) => { if (busy) { e.Cancel = true; status.Text = "진행 중인 작업이 끝난 뒤 창을 닫을 수 있습니다."; } };
        if (initialRepository != null) repository.Text = initialRepository;
        else { var directory = new DirectoryInfo(AppContext.BaseDirectory); while (directory != null) { if (Directory.Exists(Path.Combine(directory.FullName, ".git")) || File.Exists(Path.Combine(directory.FullName, ".git"))) { repository.Text = directory.FullName; break; } directory = directory.Parent; } }
        Shown += async (_, _) => { if (!SuppressInitialRefresh && !string.IsNullOrWhiteSpace(repository.Text)) await Guard(() => RefreshRepository(repository.Text)); };
    }

    private static ComboBox Combo() => new() { DropDownStyle = ComboBoxStyle.DropDownList, IntegralHeight = false, DropDownHeight = 360 };
    private static DataGridView Grid() => new() { Dock = DockStyle.Fill, ReadOnly = true, AllowUserToAddRows = false, AllowUserToDeleteRows = false,
        RowHeadersVisible = false, SelectionMode = DataGridViewSelectionMode.FullRowSelect, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
        MultiSelect = false, BackgroundColor = Color.FromArgb(29, 34, 40), BorderStyle = BorderStyle.None, ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize, RowTemplate = { Height = 40 } };
    private static FlowLayoutPanel Flow() => new() { Dock = DockStyle.Fill, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = true, Padding = new Padding(0, 5, 0, 0) };
    private static TableLayoutPanel PageLayout(int rows)
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = rows, Padding = new Padding(8) };
        panel.ColumnStyles.Add(new(SizeType.Percent, 100)); return panel;
    }
    private TabPage Page(string name) { var page = new TabPage(name); tabs.TabPages.Add(page); return page; }
    private Button Button(string text, Func<Task> action)
    {
        var button = new Button { Text = text, AutoSize = true, MinimumSize = new Size(85, 34), Margin = new Padding(3), FlatStyle = FlatStyle.Flat };
        button.Click += async (_, _) => await Guard(action); ActionNames.Add(text); return button;
    }
    private static void ApplyTheme(Control parent)
    {
        parent.BackColor = Color.FromArgb(24, 28, 33); parent.ForeColor = Color.FromArgb(229, 232, 235);
        foreach (Control child in parent.Controls)
        {
            ApplyTheme(child);
            if (child is Button button) { button.BackColor = Color.FromArgb(46, 56, 66); button.FlatAppearance.BorderColor = Color.FromArgb(72, 83, 94); }
            if (child is TextBox or ComboBox) child.BackColor = Color.FromArgb(33, 39, 46);
            if (child is DataGridView grid)
            {
                grid.EnableHeadersVisualStyles = false; grid.GridColor = Color.FromArgb(51, 59, 67);
                grid.ColumnHeadersDefaultCellStyle = new DataGridViewCellStyle { BackColor = Color.FromArgb(44, 52, 61), ForeColor = Color.White, Padding = new Padding(4) };
                grid.DefaultCellStyle = new DataGridViewCellStyle { BackColor = Color.FromArgb(29, 34, 40), ForeColor = Color.FromArgb(229, 232, 235), SelectionBackColor = Color.FromArgb(48, 79, 99), SelectionForeColor = Color.White };
            }
        }
    }
    private GitService Service => git ?? throw new InvalidOperationException("저장소를 먼저 선택하세요.");
    private async Task Guard(Func<Task> action)
    {
        if (busy) return;
        busy = true; tabs.Enabled = false; repository.Enabled = false; UseWaitCursor = true; status.Text = "작업 중…";
        try { await action(); }
        catch (Exception error) { Append(error.Message); status.Text = "작업 실패 · 로그를 확인하세요."; MessageBox.Show(this, error.Message, "Git 관리", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        finally { busy = false; tabs.Enabled = true; repository.Enabled = true; UseWaitCursor = false; }
    }
    private void Append(string text)
    {
        output.AppendText($"[{DateTime.Now:HH:mm:ss}] {text}\r\n");
        if (output.TextLength > 180000) output.Text = output.Text[^120000..]; output.SelectionStart = output.TextLength; output.ScrollToCaret();
    }
    private async Task Browse()
    {
        using var folder = new FolderBrowserDialog { Description = "Git 저장소 폴더 선택", UseDescriptionForTitle = true };
        if (folder.ShowDialog(this) == DialogResult.OK) await RefreshRepository(folder.SelectedPath);
    }
    private async Task CloneRepository()
    {
        var answer = Ask("저장소 복제", ("원격 주소 / 원본 경로", ""), ("복제할 새 폴더 전체 경로", Path.Combine(Directory.GetParent(git?.Repository ?? AppContext.BaseDirectory)!.FullName, "GitRepository")));
        if (answer == null) return;
        if (string.IsNullOrWhiteSpace(answer[0])) throw new InvalidOperationException("원본 주소를 입력하세요.");
        var path = Path.GetFullPath(answer[1]); var parent = Directory.GetParent(path)?.FullName ?? throw new InvalidOperationException("대상 폴더를 확인하세요.");
        if (Directory.Exists(path) || !Directory.Exists(parent)) throw new InvalidOperationException("기존 상위 폴더 안의 새 폴더 경로를 지정하세요.");
        if (!Confirm($"원본: {answer[0]}\n새 폴더: {path}\n\n이 위치에 저장소를 복제합니다.")) return;
        Append((await GitService.RunProcess("git", parent, ["clone", "--", answer[0], path], timeoutSeconds: 900)).Display);
        await RefreshRepository(path);
    }
    private async Task InitRepository()
    {
        var answer = Ask("새 Git 저장소", ("폴더 전체 경로", ""), ("최초 브랜치 이름", "main")); if (answer == null) return;
        var path = Path.GetFullPath(answer[0]); var branch = ValidBranch(answer[1]);
        if (Directory.Exists(Path.Combine(path, ".git")) || File.Exists(Path.Combine(path, ".git"))) throw new InvalidOperationException("이미 Git 저장소인 폴더입니다.");
        if (!Confirm($"폴더: {path}\n최초 브랜치: {branch}\n\nGit 저장소를 초기화합니다. 기존 파일은 유지됩니다.")) return;
        Directory.CreateDirectory(path); Append((await GitService.RunProcess("git", path, ["init", "-b", branch])).Display); await RefreshRepository(path);
    }
    internal async Task RefreshRepository(string directory)
    {
        if (!Directory.Exists(directory)) throw new DirectoryNotFoundException("저장소 폴더를 확인하세요.");
        var root = await new GitService(directory).ResolveRoot(); var changed = git?.Repository != root;
        git = new GitService(root); repository.Text = root;
        if (changed) previewPath = null;
        InvalidatePlan();
        branchData = await Service.Branches(); loading = true;
        try
        {
            var oldSource = (source.SelectedItem as BranchInfo)?.Name; var oldTarget = (target.SelectedItem as BranchInfo)?.Name;
            var oldHistory = (historyRef.SelectedItem as BranchInfo)?.Name;
            FillBranches(source, branchData, oldSource ?? branchData.FirstOrDefault(x => x.Current)?.Name);
            FillBranches(target, branchData.Where(x => !x.Remote), oldTarget ?? (branchData.Any(x => x.Name == "main" && !x.Remote) ? "main" : null));
            FillBranches(historyRef, branchData, oldHistory ?? branchData.FirstOrDefault(x => x.Current)?.Name);
        }
        finally { loading = false; }
        changes.Rows.Clear(); changes.MultiSelect = true;
        foreach (var item in await Service.Changes()) changes.Rows[changes.Rows.Add(item.Index, item.Work, item.Path)].Tag = item;
        branches.Rows.Clear();
        var branchOutput = (await Service.Git("for-each-ref", "--format=%(refname)%09%(objectname:short)%09%(committerdate:iso8601)%09%(subject)", "refs/heads", "refs/remotes")).Output;
        foreach (var line in branchOutput.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            var fields = line.TrimEnd('\r').Split('\t'); if (fields.Length < 4) continue;
            var item = branchData.FirstOrDefault(x => GitService.Ref(x) == fields[0]); if (item == null) continue;
            var date = DateTimeOffset.TryParse(fields[2], out var at) ? at.ToLocalTime().ToString("yyyy-MM-dd HH:mm") : fields[2];
            branches.Rows[branches.Rows.Add(item.Name, item.Current ? "현재" : item.Remote ? "원격" : "로컬", fields[1], date, fields[3])].Tag = item;
        }
        stashes.Rows.Clear(); var stashText = (await Service.Git("stash", "list", "--format=%gd%x09%s")).Output;
        foreach (var line in stashText.Split('\n', StringSplitOptions.RemoveEmptyEntries)) { var p = line.TrimEnd('\r').Split('\t', 2); stashes.Rows.Add(p[0], p.Length > 1 ? p[1] : ""); }
        worktrees.Rows.Clear(); foreach (var tree in await Service.Worktrees()) worktrees.Rows[worktrees.Rows.Add(tree.Path, tree.Branch, tree.Head.Length > 10 ? tree.Head[..10] : tree.Head)].Tag = tree;
        var oldRemote = remote.SelectedItem as string; remote.Items.Clear();
        foreach (var name in (await Service.Git("remote")).Output.Split('\n', StringSplitOptions.RemoveEmptyEntries)) remote.Items.Add(name.Trim());
        remote.SelectedIndex = remote.Items.Contains(oldRemote ?? "origin") ? remote.Items.IndexOf(oldRemote ?? "origin") : remote.Items.Count > 0 ? 0 : -1;
        remoteDetails.Text = (await Service.Git("remote", "-v")).Output.Trim();
        await LoadHistory();
        location.Text = $"현재 작업 브랜치: {await Service.CurrentBranch()}    ·    {root}";
        var operation = await PendingOperation(); status.Text = operation.Length > 0 ? $"{operation} 진행 중 · 충돌 해결 후 계속 / 중단" : $"파일 {changes.Rows.Count}개 변경 · 로컬 브랜치 {branchData.Count(x => !x.Remote)}개";
    }
    private static void FillBranches(ComboBox box, IEnumerable<BranchInfo> values, string? selected)
    {
        box.Items.Clear(); foreach (var value in values) box.Items.Add(value);
        box.SelectedIndex = Enumerable.Range(0, box.Items.Count).FirstOrDefault(i => (box.Items[i] as BranchInfo)?.Name == selected, -1);
        if (box.SelectedIndex < 0 && box.Items.Count > 0) box.SelectedIndex = 0;
    }
    private void InvalidatePlan() { if (loading) return; plan = null; planFile = null; assets.Rows.Clear(); prepare.Enabled = false; assetReason.Text = ""; mergeSummary.Text = "브랜치를 선택하고 비교하세요."; }
    private Task SwapBranches()
    {
        if (source.SelectedItem is not BranchInfo a || target.SelectedItem is not BranchInfo b) return Task.CompletedTask;
        if (a.Remote) throw new InvalidOperationException("병합 대상은 로컬 브랜치를 선택하세요. 원격은 로컬 추적 브랜치를 만든 뒤 사용하세요.");
        loading = true; try { FillBranches(source, branchData, b.Name); FillBranches(target, branchData.Where(x => !x.Remote), a.Name); } finally { loading = false; }
        InvalidatePlan(); return Task.CompletedTask;
    }
    private async Task Compare()
    {
        if (source.SelectedItem is not BranchInfo a || target.SelectedItem is not BranchInfo b) throw new InvalidOperationException("두 브랜치를 선택하세요.");
        if (GitService.Ref(a) == GitService.Ref(b)) throw new InvalidOperationException("서로 다른 브랜치를 선택하세요.");
        var result = await Service.Review(GitService.Ref(a), b.Name); planFile = result.PlanFile; plan = GitService.ReadPlan(planFile);
        assets.Rows.Clear(); foreach (var item in plan.Assets) assets.Rows[assets.Rows.Add(item.Path, KindName(item.Kind), item.Choice == "Source" ? "가져올 브랜치" : "대상 브랜치", "자동 판정")].Tag = item;
        assetReason.Text = assets.CurrentRow?.Tag is AssetDecision selected ? selected.Reason : "서로 다른 에셋이 없습니다.";
        prepare.Enabled = true; UpdateMergeSummary(); status.Text = $"비교 완료 · {a.Name} → {b.Name}"; Append($"에셋 비교: {a.Name} → {b.Name}\r\n{planFile}");
    }
    internal async Task CompareForTest(string from, string to)
    {
        source.SelectedItem = branchData.Single(x => x.Name == from && !x.Remote);
        target.SelectedItem = branchData.Single(x => x.Name == to && !x.Remote);
        await Compare();
    }
    internal Task CheckoutForTest(string name)
    {
        foreach (DataGridViewRow row in branches.Rows) if (row.Tag is BranchInfo branch && branch.Name == name && !branch.Remote)
        { branches.CurrentCell = row.Cells[0]; return Checkout(); }
        throw new InvalidOperationException("시험 브랜치를 찾지 못했습니다.");
    }
    private static string KindName(string kind) => kind switch { "NewAsset" => "새 에셋", "NewRevision" => "이후 버전", "OlderRevision" => "이전 버전", "TargetOnly" => "대상 전용", "Review" => "검토 필요", _ => "대상 유지" };
    private void UpdateMergeSummary()
    {
        if (plan == null) return;
        int fromSource = 0, review = 0;
        foreach (DataGridViewRow row in assets.Rows) if (row.Tag is AssetDecision item)
        {
            var selection = row.Cells[3].Value as string ?? "자동 판정";
            if (selection == "가져올 브랜치" || selection == "자동 판정" && item.Choice == "Source") fromSource++;
            if (selection == "자동 판정" && item.ReviewRequired) review++;
        }
        mergeSummary.Text = $"가져오기 {fromSource} · 대상 유지 {assets.Rows.Count - fromSource} · 검토 필요 {review}";
    }
    private async Task Prepare()
    {
        assets.EndEdit(); if (plan == null || planFile == null) throw new InvalidOperationException("먼저 브랜치를 비교하세요.");
        var selections = assets.Rows.Cast<DataGridViewRow>().Where(x => x.Tag is AssetDecision).Select(x => new { path = ((AssetDecision)x.Tag!).Path,
            choice = (x.Cells[3].Value as string) switch { "가져올 브랜치" => "Source", "대상 브랜치" => "Target", _ => "Automatic" } }).ToArray();
        if (!Confirm($"{plan.SourceRef} → {plan.TargetBranch}\n{mergeSummary.Text}\n\n선택한 버전으로 별도 검토 폴더를 만듭니다. 원래 작업 폴더와 대상 브랜치는 변경하지 않습니다.\n검토 표시된 에셋은 기본적으로 대상 버전을 유지합니다.")) return;
        Directory.CreateDirectory(Service.ReportDirectory); var selectionFile = Path.Combine(Service.ReportDirectory, $"selection-{Guid.NewGuid():N}.json");
        await File.WriteAllTextAsync(selectionFile, JsonSerializer.Serialize(selections, GitService.Json));
        var result = await Service.Review(plan.SourceRef, plan.TargetBranch, planFile, selectionFile); planFile = result.PlanFile; previewPath = result.PreviewPath;
        Append($"병합 검토 폴더: {result.PreviewPath}\r\n브랜치: {result.PreviewBranch}\r\n코드 충돌: {string.Join(", ", result.CodeConflicts)}");
        status.Text = $"병합 준비 완료 · 코드 충돌 {result.CodeConflicts.Length}개 · 검토 후 커밋하세요.";
    }
    private Task OpenReport() { if (planFile == null) throw new InvalidOperationException("먼저 비교하세요."); OpenPath(Path.ChangeExtension(planFile, ".md")); return Task.CompletedTask; }
    private static void OpenPath(string? path) { if (string.IsNullOrEmpty(path)) throw new InvalidOperationException("열 대상이 없습니다."); Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
    private bool Confirm(string text, bool danger = false) => MessageBox.Show(this, text, danger ? "변경 범위 확인" : "실행 확인", MessageBoxButtons.YesNo, danger ? MessageBoxIcon.Warning : MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) == DialogResult.Yes;
    private async Task Execute(string description, string[] args, bool danger = false, bool confirmation = true)
    {
        var current = await Service.CurrentBranch();
        if (confirmation && !Confirm($"저장소: {Service.Repository}\n현재 브랜치: {current}\n\n{description}\n\ngit {string.Join(" ", args.Select(Quote))}", danger)) return;
        Append("git " + string.Join(" ", args.Select(Quote)));
        var result = await Service.Git(args, false); if (result.Display.Length > 0) Append(result.Display);
        await RefreshRepository(Service.Repository);
        if (result.Code != 0) throw new InvalidOperationException(result.Display); status.Text = "완료";
    }
    private static string Quote(string text) => text.Any(char.IsWhiteSpace) ? "\"" + text.Replace("\"", "\\\"") + "\"" : text;
    private async Task ReadCommand(string[] args) { Append("git " + string.Join(" ", args.Select(Quote))); Append((await Service.Git(args)).Display); status.Text = "조회 완료"; }
    private List<FileChange> SelectedChanges() { var items = changes.SelectedRows.Cast<DataGridViewRow>().Select(x => (FileChange)x.Tag!).ToList(); if (items.Count == 0) throw new InvalidOperationException("파일을 선택하세요."); return items; }
    private async Task FileAction(string command) => await Execute("선택한 파일을 스테이지에 올립니다.", GitService.SelectedPathsCommand(command, SelectedChanges()), confirmation: false);
    private async Task Unstage()
    {
        var paths = SelectedChanges().SelectMany(x => x.Original == null ? new[] { x.Path } : new[] { x.Path, x.Original }).Distinct();
        var hasHead = (await Service.Git(new[] { "rev-parse", "--verify", "HEAD" }, false)).Code == 0;
        await Execute("선택 파일의 스테이지만 취소합니다. 작업 파일은 유지됩니다.", (hasHead ? new[] { "--literal-pathspecs", "restore", "--staged", "--" } : new[] { "--literal-pathspecs", "rm", "--cached", "--ignore-unmatch", "--" }).Concat(paths).ToArray(), confirmation: false);
    }
    private async Task Discard()
    {
        var files = SelectedChanges(); if (files.Any(x => x.Index == "?")) throw new InvalidOperationException("미추적 파일은 이 버튼으로 지우지 않습니다. 작업 폴더 · 복구의 정리 기능을 사용하세요.");
        await Execute("선택한 작업 파일 변경을 스테이지 버전으로 되돌립니다. 되돌린 미커밋 변경은 복원되지 않을 수 있습니다.\n" + string.Join("\n", files.Select(x => x.Path)),
            new[] { "--literal-pathspecs", "restore", "--worktree", "--" }.Concat(files.Select(x => x.Path)).ToArray(), true);
    }
    private async Task FileDiff()
    {
        var paths = SelectedChanges().Select(x => x.Path).ToArray(); var args = new[] { "--literal-pathspecs", "diff", "--no-ext-diff", "--" }.Concat(paths).ToArray();
        var work = (await Service.Git(args)).Output; var staged = (await Service.Git(new[] { "--literal-pathspecs", "diff", "--cached", "--no-ext-diff", "--" }.Concat(paths))).Output;
        ShowText("선택 파일 변경", "[작업 변경]\r\n" + work + "\r\n[스테이지 변경]\r\n" + staged);
    }
    private async Task Commit()
    {
        if (string.IsNullOrWhiteSpace(commitMessage.Text)) throw new InvalidOperationException("커밋 메시지를 입력하세요.");
        var args = new List<string> { "commit" }; if (amend.Checked) args.Add("--amend"); args.AddRange(["-m", commitMessage.Text]);
        await Execute(amend.Checked ? "마지막 커밋을 새 커밋으로 수정합니다. 스테이지 변경도 포함됩니다. 공개한 이력은 달라집니다." : "스테이지에 있는 변경을 커밋합니다.", args.ToArray(), amend.Checked);
    }
    private CommitInfo SelectedCommit() => history.CurrentRow?.Tag as CommitInfo ?? throw new InvalidOperationException("커밋을 선택하세요.");
    private async Task LoadHistory()
    {
        var reference = historyRef.SelectedItem is BranchInfo branch ? GitService.Ref(branch) : "HEAD";
        history.Rows.Clear();
        if ((await Service.Git(new[] { "rev-parse", "--verify", "HEAD" }, false)).Code != 0) { details.Text = "아직 커밋이 없습니다."; return; }
        var wasLoading = loading; loading = true;
        try { foreach (var item in await Service.History(reference)) history.Rows[history.Rows.Add(item.Hash[..10], item.Date, item.Author, item.Subject)].Tag = item; }
        finally { loading = wasLoading; }
        if (history.CurrentRow?.Tag is CommitInfo selected) await ShowCommit(selected.Hash); status.Text = "최근 커밋 100개까지 표시";
    }
    private async Task ShowCommit(string hash) => details.Text = (await Service.Git("show", "--no-ext-diff", "--stat", "--format=fuller", hash, "--")).Output.Replace("\n", "\r\n");
    private async Task ReadSelectedCommit(string hash)
    {
        var service = git; if (service == null) return;
        try
        {
            var text = (await service.Git("show", "--no-ext-diff", "--stat", "--format=fuller", hash, "--")).Output;
            if (!IsDisposed && ReferenceEquals(service, git) && history.CurrentRow?.Tag is CommitInfo selected && selected.Hash == hash)
                details.Text = text.Replace("\n", "\r\n");
        }
        catch (Exception error) { if (!IsDisposed && ReferenceEquals(service, git)) { status.Text = "커밋 상세 조회 실패"; Append(error.Message); } }
    }
    private Task CherryPick() => Execute("선택한 커밋 변경을 현재 브랜치에 적용합니다.", ["cherry-pick", SelectedCommit().Hash]);
    private Task Revert() => Execute("선택한 커밋을 취소하는 새 커밋을 현재 브랜치에 추가합니다.", ["revert", "--no-edit", SelectedCommit().Hash]);
    private async Task Reset()
    {
        var selected = SelectedCommit(); var values = Ask("선택 커밋으로 reset", ("방식: soft / mixed / hard", "soft")); if (values == null) return;
        var mode = values[0].Trim(); if (mode is not ("soft" or "mixed" or "hard")) throw new InvalidOperationException("soft, mixed, hard 중 하나를 입력하세요.");
        await Execute($"현재 브랜치를 {selected.Hash[..10]}로 옮깁니다.\n" + (mode == "hard" ? "작업 파일과 스테이지의 미커밋 변경이 삭제될 수 있습니다." : mode == "mixed" ? "변경은 작업 파일에 남고 스테이지는 취소됩니다." : "변경은 스테이지에 남습니다."), ["reset", "--" + mode, selected.Hash], true);
    }
    private async Task Reword()
    {
        var selected = SelectedCommit(); var head = (await Service.Git("rev-parse", "HEAD")).Output.Trim();
        if (head != selected.Hash) throw new InvalidOperationException("이 버튼은 현재 브랜치의 마지막 커밋 메시지를 수정합니다. 중간 커밋은 추가 Git 명령에서 rebase 등으로 관리하세요.");
        var values = Ask("커밋 메시지 수정", ("새 커밋 메시지", selected.Subject)); if (values == null || string.IsNullOrWhiteSpace(values[0])) return;
        await Execute("마지막 커밋 메시지만 바꿉니다. 현재 스테이지 변경은 포함하지 않습니다. 공개한 이력은 달라집니다.", ["commit", "--amend", "--only", "-m", values[0]], true);
    }
    private async Task DropCommit()
    {
        var selected = SelectedCommit();
        if ((await Service.Changes()).Count > 0) throw new InvalidOperationException("커밋 삭제 전 작업 변경을 커밋하거나 스태시에 보관하세요.");
        var ancestor = await Service.Git(new[] { "merge-base", "--is-ancestor", selected.Hash, "HEAD" }, false);
        if (ancestor.Code != 0) throw new InvalidOperationException("현재 브랜치의 조상 커밋만 삭제할 수 있습니다.");
        var parents = (await Service.Git("rev-list", "--parents", "-n", "1", selected.Hash)).Output.Trim().Split(' ');
        if (parents.Length != 2) throw new InvalidOperationException("최초 커밋이나 병합 커밋은 이 버튼으로 삭제하지 않습니다.");
        var merges = (await Service.Git("rev-list", "--merges", selected.Hash + "..HEAD")).Output.Trim();
        if (merges.Length > 0) throw new InvalidOperationException("이후에 병합 커밋이 있습니다. 이력을 보존하려면 revert를 사용하세요.");
        await Execute($"커밋 {selected.Hash[..10]} ({selected.Subject})을 이력에서 빼고 이후 커밋을 다시 적용합니다.\n공개한 브랜치 이력이 변경됩니다. 다른 사람이 사용하는 이력은 revert를 권장합니다.", ["rebase", "--onto", parents[1], selected.Hash], true);
    }
    private BranchInfo SelectedBranch() => branches.CurrentRow?.Tag as BranchInfo ?? throw new InvalidOperationException("브랜치를 선택하세요.");
    private async Task Checkout()
    {
        var branch = SelectedBranch();
        if (branch.Current) { status.Text = "이미 체크아웃된 브랜치입니다."; return; }
        if ((await Service.Changes()).Count > 0) throw new InvalidOperationException("미커밋 변경이 다른 브랜치로 따라가지 않도록 전환을 중단했습니다. 변경을 커밋하거나 스태시에 보관한 뒤 전환하세요.");
        if (!branch.Remote) await Execute("선택한 로컬 브랜치로 전환합니다. 사용 중인 에디터·테스트가 있으면 해당 파일 전환이 실패할 수 있습니다.", ["switch", branch.Name]);
        else await TrackRemote(branch);
    }
    private async Task TrackRemote(BranchInfo branch)
    {
        var answer = Ask("원격 추적 브랜치", ("새 로컬 브랜치 이름", branch.Name[(branch.Name.IndexOf('/') + 1)..])); if (answer == null) return;
        await Execute("원격 브랜치를 추적하는 로컬 브랜치를 만들고 전환합니다.", ["switch", "--track", "-c", ValidBranch(answer[0]), GitService.Ref(branch)]);
    }
    private async Task CreateBranch()
    {
        var answer = Ask("새 브랜치", ("브랜치 이름", ""), ("시작 지점", branches.CurrentRow?.Tag is BranchInfo b ? GitService.Ref(b) : "HEAD")); if (answer == null) return;
        await Execute("시작 지점에 로컬 브랜치를 만듭니다. 현재 브랜치는 전환하지 않습니다.", ["branch", ValidBranch(answer[0]), await CommitReference(answer[1])]);
    }
    private static string ValidBranch(string value) { if (string.IsNullOrWhiteSpace(value) || value.StartsWith('-')) throw new InvalidOperationException("올바른 브랜치 이름을 입력하세요."); return value.Trim(); }
    private async Task<string> CommitReference(string reference) => (await Service.Git("rev-parse", "--verify", "--end-of-options", reference.Trim() + "^{commit}")).Output.Trim();
    private async Task RenameBranch()
    {
        var branch = SelectedBranch(); if (branch.Remote) throw new InvalidOperationException("로컬 브랜치만 이름을 변경할 수 있습니다.");
        var answer = Ask("브랜치 이름 변경", ("새 이름", branch.Name)); if (answer == null) return; await Execute("로컬 브랜치 이름을 변경합니다.", ["branch", "-m", branch.Name, ValidBranch(answer[0])]);
    }
    private async Task DeleteBranch()
    {
        var branch = SelectedBranch(); if (branch.Remote) { await DeleteRemoteBranch(); return; }
        if (branch.Current) throw new InvalidOperationException("현재 브랜치는 삭제할 수 없습니다. 다른 브랜치로 먼저 전환하세요.");
        var answer = Ask("브랜치 삭제", ("삭제 방식: normal / force", "normal")); if (answer == null) return;
        if (answer[0] is not ("normal" or "force")) throw new InvalidOperationException("normal 또는 force를 입력하세요.");
        await Execute($"로컬 브랜치 {branch.Name}를 삭제합니다." + (answer[0] == "force" ? " 병합되지 않은 커밋의 브랜치 참조도 삭제합니다." : " 병합되지 않은 변경이 있으면 Git이 거부합니다."), ["branch", answer[0] == "force" ? "-D" : "-d", branch.Name], true);
    }
    private Task MergeCurrent() => Execute("선택한 브랜치를 현재 작업 브랜치에 일반 병합합니다. 에셋 보호 선택은 에셋 보호 병합 탭을 사용하세요.", ["merge", "--no-edit", GitService.Ref(SelectedBranch())]);
    private async Task Rebase()
    {
        if ((await Service.Changes()).Count > 0) throw new InvalidOperationException("rebase 전 변경을 커밋하거나 스태시에 보관하세요.");
        await Execute("현재 브랜치의 커밋을 선택한 브랜치 위로 다시 적용합니다. 커밋 이력이 바뀝니다.", ["rebase", GitService.Ref(SelectedBranch())], true);
    }
    private async Task<string> PendingOperation()
    {
        foreach (var (name, marker) in new[] { ("rebase", "rebase-merge"), ("rebase", "rebase-apply"), ("merge", "MERGE_HEAD"), ("cherry-pick", "CHERRY_PICK_HEAD"), ("revert", "REVERT_HEAD") })
        {
            var path = (await Service.Git("rev-parse", "--git-path", marker)).Output.Trim(); if (!Path.IsPathRooted(path)) path = Path.Combine(Service.Repository, path);
            if (File.Exists(path) || Directory.Exists(path)) return name;
        }
        return "";
    }
    private async Task Continue() { var name = await PendingOperation(); if (name is not ("rebase" or "merge")) throw new InvalidOperationException("진행 중인 merge/rebase가 없습니다."); await Execute("충돌 해결을 스테이지에 반영한 뒤 계속 진행합니다.", [name, "--continue"]); }
    private async Task Abort() { var name = await PendingOperation(); if (name is not ("rebase" or "merge")) throw new InvalidOperationException("진행 중인 merge/rebase가 없습니다."); await Execute("진행 중 작업을 중단합니다. 충돌 해결 중 추가한 변경이 사라질 수 있습니다.", [name, "--abort"], true); }
    private async Task ContinueOther() { var name = await PendingOperation(); if (name is not ("cherry-pick" or "revert")) throw new InvalidOperationException("진행 중인 cherry-pick/revert가 없습니다."); await Execute("충돌 해결 후 계속 진행합니다.", [name, "--continue"]); }
    private async Task AbortOther() { var name = await PendingOperation(); if (name is not ("cherry-pick" or "revert")) throw new InvalidOperationException("진행 중인 cherry-pick/revert가 없습니다."); await Execute("진행 중인 변경 적용을 중단합니다.", [name, "--abort"], true); }
    private string SelectedStash() => stashes.CurrentRow?.Cells[0].Value as string ?? throw new InvalidOperationException("스태시를 선택하세요.");
    private async Task StashSave() { var answer = Ask("변경 임시 보관", ("보관 이름", "작업 보관")); if (answer == null) return; await Execute("미추적 파일을 포함해 변경을 임시 보관합니다. 무시된 파일은 포함하지 않습니다.", ["stash", "push", "--include-untracked", "-m", answer[0]]); }
    private Task StashAction(string action) => Execute(action == "drop" ? "선택한 보관 내용을 삭제합니다." : "선택한 보관 변경을 현재 작업 폴더에 적용합니다.", ["stash", action, SelectedStash()], action == "drop");
    private async Task StashShow() => ShowText("스태시 변경", (await Service.Git("stash", "show", "--patch", "--no-ext-diff", SelectedStash())).Output);
    private string SelectedRemote() => remote.SelectedItem as string ?? throw new InvalidOperationException("원격을 선택하세요.");
    private async Task RemoteAction(string action)
    {
        var name = SelectedRemote(); var current = await Service.CurrentBranch(); if (current == "분리된 HEAD" && action != "fetch") throw new InvalidOperationException("로컬 브랜치를 먼저 체크아웃하세요.");
        if (action == "fetch") { await Execute("선택 원격의 최신 참조를 받아옵니다. 로컬 작업 파일은 바꾸지 않습니다.", ["fetch", "--prune", name]); return; }
        if (action == "pull") { var answer = Ask("원격 변경 내려받기", ("받아올 원격 브랜치", current)); if (answer == null) return; await Execute("원격 변경을 현재 브랜치에 fast-forward로만 반영합니다. 이력이 갈라지면 중단합니다.", ["pull", "--ff-only", name, ValidBranch(answer[0])]); return; }
        var dest = Ask("커밋 올리기", ("원격 대상 브랜치", current)); if (dest == null) return;
        var refspec = "HEAD:refs/heads/" + ValidBranch(dest[0]);
        await Execute(action == "force" ? "원격 이력을 로컬 이력으로 교체합니다. 원격 추적 이후 다른 변경이 있으면 force-with-lease가 거부합니다." : "현재 브랜치의 커밋을 지정한 원격 브랜치로 올립니다.", action == "force" ? ["push", "--force-with-lease", name, refspec] : ["push", "--set-upstream", name, refspec], action == "force");
    }
    private async Task AddRemote() { var answer = Ask("원격 추가", ("이름", "origin"), ("주소", "")); if (answer == null) return; await Execute("원격을 추가합니다.", ["remote", "add", ValidBranch(answer[0]), answer[1]]); }
    private async Task ChangeRemote() { var name = SelectedRemote(); var url = (await Service.Git("remote", "get-url", name)).Output.Trim(); var answer = Ask("원격 주소 변경", ("주소", url)); if (answer == null) return; await Execute("선택 원격의 주소를 바꿉니다.", ["remote", "set-url", name, answer[0]]); }
    private Task RemoveRemote() => Execute("원격 설정과 원격 추적 참조를 제거합니다. 서버의 저장소는 삭제하지 않습니다.", ["remote", "remove", SelectedRemote()], true);
    private async Task DeleteRemoteBranch()
    {
        var branch = branches.CurrentRow?.Tag as BranchInfo;
        var remoteName = branch?.Remote == true ? remote.Items.Cast<string>().Where(x => branch.Name.StartsWith(x + "/", StringComparison.Ordinal)).OrderByDescending(x => x.Length).FirstOrDefault() : null;
        remoteName ??= remote.SelectedItem as string ?? "origin";
        var defaultName = branch?.Remote == true && branch.Name.StartsWith(remoteName + "/", StringComparison.Ordinal) ? branch.Name[(remoteName.Length + 1)..] : "";
        var answer = Ask("원격 브랜치 삭제", ("원격 이름", remoteName), ("삭제할 원격 브랜치", defaultName)); if (answer == null) return;
        await Execute("서버의 원격 브랜치를 삭제합니다.", ["push", ValidBranch(answer[0]), "--delete", ValidBranch(answer[1])], true);
    }
    private async Task CreateTag() { var commit = SelectedCommit(); var answer = Ask("태그 만들기", ("태그 이름", ""), ("태그 메시지", commit.Subject)); if (answer == null) return; await Execute("선택 커밋에 로컬 주석 태그를 만듭니다.", ["tag", "-a", ValidBranch(answer[0]), commit.Hash, "-m", answer[1]]); }
    private async Task DeleteTag() { var answer = Ask("로컬 태그 삭제", ("태그 이름", "")); if (answer == null) return; await Execute("로컬 태그만 삭제합니다.", ["tag", "-d", ValidBranch(answer[0])], true); }
    private WorktreeInfo SelectedWorktree() => worktrees.CurrentRow?.Tag as WorktreeInfo ?? throw new InvalidOperationException("worktree를 선택하세요.");
    private async Task AddWorktree()
    {
        var answer = Ask("별도 작업 폴더", ("새 폴더의 전체 경로", Path.Combine(Directory.GetParent(Service.Repository)!.FullName, "GitWorktree")), ("새 브랜치 이름 (비우면 기존 브랜치 사용)", ""), ("시작 브랜치 / 커밋", branches.CurrentRow?.Tag is BranchInfo b ? GitService.Ref(b) : "HEAD")); if (answer == null) return;
        var path = Path.GetFullPath(answer[0]); if (Directory.Exists(path)) throw new InvalidOperationException("이미 존재하지 않는 새 폴더를 지정하세요.");
        var commit = await CommitReference(answer[2]);
        await Execute("별도 작업 폴더를 만듭니다. 현재 폴더의 브랜치는 전환하지 않습니다.", answer[1].Length > 0 ? ["worktree", "add", "-b", ValidBranch(answer[1]), path, commit] : ["worktree", "add", path, answer[2]]);
    }
    private async Task RemoveWorktree()
    {
        var tree = SelectedWorktree(); if (Path.GetFullPath(tree.Path).TrimEnd('\\') == Service.Repository.TrimEnd('\\')) throw new InvalidOperationException("현재 관리 중인 폴더는 제거할 수 없습니다.");
        await Execute($"worktree 폴더를 제거합니다: {tree.Path}\n미커밋 변경이나 잠금이 있으면 Git이 거부합니다. 브랜치는 유지됩니다.", ["worktree", "remove", tree.Path], true);
    }
    private async Task Clean()
    {
        var dry = (await Service.Git("clean", "-nd")).Display; if (dry.Length == 0) { status.Text = "정리할 미추적 파일이 없습니다."; return; }
        if (!Confirm("아래 미추적 파일/폴더가 영구 삭제됩니다. 무시된 빌드 파일은 제외됩니다.\n\n" + dry, true)) return;
        await Execute("", ["clean", "-fd"], true, false);
    }
    private async Task Advanced()
    {
        var args = GitService.ParseArguments(advanced.Text); if (args.Length == 0) return;
        if (args.Any(x => x is "--interactive" or "-i")) throw new InvalidOperationException("별도 편집기가 필요한 대화형 명령은 이 창에서 실행하지 않습니다. 일반 옵션이나 외부 터미널을 사용하세요.");
        await Execute("입력한 Git 명령을 현재 작업 폴더에서 실행합니다. 지정한 옵션의 변경·삭제 범위를 확인하세요.", args, true);
    }
    private string[]? Ask(string title, params (string Label, string Value)[] fields)
    {
        using var dialog = new Form { Text = title, Size = new Size(620, 140 + fields.Length * 66), StartPosition = FormStartPosition.CenterParent, FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false, Font = Font };
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, Padding = new Padding(16) }; var boxes = new List<TextBox>();
        foreach (var field in fields) { panel.Controls.Add(new Label { Text = field.Label, AutoSize = true }); var box = new TextBox { Text = field.Value, Dock = DockStyle.Top }; panel.Controls.Add(box); boxes.Add(box); }
        var buttons = Flow(); var yes = new Button { Text = "확인", DialogResult = DialogResult.OK, Width = 90 }; var no = new Button { Text = "취소", DialogResult = DialogResult.Cancel, Width = 90 }; buttons.Controls.AddRange([yes, no]); panel.Controls.Add(buttons);
        dialog.Controls.Add(panel); dialog.AcceptButton = yes; dialog.CancelButton = no; ApplyTheme(dialog);
        return dialog.ShowDialog(this) == DialogResult.OK ? boxes.Select(x => x.Text).ToArray() : null;
    }
    private void ShowText(string title, string text)
    {
        using var dialog = new Form { Text = title, Size = new Size(1000, 700), StartPosition = FormStartPosition.CenterParent, Font = new Font("Consolas", 10) };
        dialog.Controls.Add(new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false, Text = text.Replace("\r\n", "\n").Replace("\n", "\r\n") }); ApplyTheme(dialog); dialog.ShowDialog(this);
    }
}
