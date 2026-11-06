using System.Diagnostics;
using System.Text;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed partial class DashboardForm
{
    private string? VerificationProjectFile() =>
        UnrealProject.FindProjectFile(rootPath.Text) ?? UnrealProject.FindFrom(AppContext.BaseDirectory)?.ProjectFile;

    private static string VerificationWorkspace(string projectFile, string name) =>
        Path.Combine(Path.GetDirectoryName(projectFile)!, "Saved", "Verification", name);

    private async Task ShowVerificationOptionsAsync(Button button)
    {
        string? projectFile = VerificationProjectFile();
        if (projectFile is null)
        {
            MessageBox.Show(this, "진단 폴더에서 Unreal 프로젝트를 찾을 수 없습니다.", Text);
            return;
        }
        var project = UnrealProject.Load(projectFile);
        using var menu = new ContextMenuStrip();
        var selection = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        string[] choices = ["일반 자동 검사", "유닛·기능 검사 선택", "UI 실험 검사", "에셋 검증 (Unreal)",
            "핵심 흐름 검사 (PIE)", "패키지 에셋 확인", "작업 누락 검토"];
        for (int i = 0; i < choices.Length; i++)
        {
            int index = i;
            menu.Items.Add(choices[i], null, (_, _) => selection.TrySetResult(index));
        }
        menu.Closed += (_, _) => BeginInvoke(() => selection.TrySetResult(-1));
        menu.Show(button, new Point(0, button.Height));
        switch (await selection.Task)
        {
            case 0:
                await VerifySelectedRunAsync(button, AutomationRunPlan.General with { Filter = project.GeneralTestGroup });
                break;
            case 1:
                using (var catalog = new AutomationCatalogForm(project, windowLifetime.Token))
                    if (catalog.ShowDialog(this) == DialogResult.OK && catalog.Plan is { } plan)
                        await VerifySelectedRunAsync(button, plan, attachToSelectedRun: false);
                break;
            case 2:
                await VerifySelectedRunAsync(button, AutomationRunPlan.Frontend(project.PrimaryModule), attachToSelectedRun: false);
                break;
            case 3:
                using (var options = new VerificationInputForm("에셋 검증 (Unreal)",
                    "에셋 클래스를 입력해 범위를 좁힐 수 있습니다. 예: /Script/Engine.World. 비워 두면 전체 에셋을 검사합니다.",
                    "", multiline: false))
                {
                    options.ValidateInput = value => ProjectVerification.IsValidAssetClass(value.Trim()) ? null : "올바른 Unreal 에셋 클래스 이름을 입력하세요.";
                    if (options.ShowDialog(this) == DialogResult.OK)
                        RunProjectCheck(project, "에셋 검증", (folder, token, progress) =>
                            ProjectVerification.ValidateAssetsAsync(project, options.Input.Text.Trim(), folder, token, progress));
                }
                break;
            case 4:
                await VerifySelectedRunAsync(button, AutomationRunPlan.Gameplay(project.PrimaryModule), attachToSelectedRun: false);
                break;
            case 5:
                using (var picker = new FolderBrowserDialog
                {
                    Description = "패키징한 Windows 클라이언트 또는 데디 서버 폴더 선택",
                    UseDescriptionForTitle = true,
                    SelectedPath = Directory.Exists(Path.Combine(project.ProjectRoot, "Builds")) ? Path.Combine(project.ProjectRoot, "Builds") : project.ProjectRoot,
                    ShowNewFolderButton = false
                })
                    if (picker.ShowDialog(this) == DialogResult.OK)
                        RunProjectCheck(project, "패키지 에셋 확인", (folder, token, progress) =>
                            ProjectVerification.VerifyPackageAsync(project, picker.SelectedPath, folder, token, progress));
                break;
            case 6:
                using (var options = new VerificationInputForm("작업 누락 검토",
                    "비워 두면 Git 변경을 자동으로 탐색합니다. 계획을 대조하려면 한 줄에 하나씩 입력하세요.\n"
                    + "예: 방 입장 기능 | JoinRoom | /api/rooms    또는 Git 저장소 기준 파일 경로",
                    "", multiline: true))
                    if (options.ShowDialog(this) == DialogResult.OK)
                        RunProjectCheck(project, "작업 누락 검토", (folder, token, progress) =>
                            WorkReview.RunAsync(project, options.Input.Lines, folder, token, progress));
                break;
        }
    }

    private void RunProjectCheck(UnrealProject project, string name,
        Func<string, CancellationToken, IProgress<string>, Task<ProjectCheckResult>> run)
    {
        string? directory = null;
        string message = "";
        WorkReviewReport? workReview = null;
        var icon = MessageBoxIcon.Warning;
        using var progress = new AutomationProgressForm(name, windowLifetime.Token, async (token, status) =>
        {
            try
            {
                directory = AutomationVerification.CreateAttempt(VerificationWorkspace(project.ProjectFile, name)).Directory;
                var result = await run(directory, token, status);
                message = result.Summary;
                workReview = result.WorkReview;
                icon = result.Passed ? MessageBoxIcon.Information : MessageBoxIcon.Warning;
            }
            catch (OperationCanceledException)
            {
                message = token.IsCancellationRequested ? "검사를 취소했습니다." : "제한 시간이 지나 검사를 중단했습니다.";
            }
            catch (Exception e) { message = e.Message; }
            if (directory is not null)
            {
                try { File.WriteAllText(Path.Combine(directory, "Result.txt"), message, Encoding.UTF8); }
                catch (IOException) { message += "\n결과 요약을 저장할 수 없습니다."; }
                catch (UnauthorizedAccessException) { message += "\n결과 요약을 저장할 권한이 없습니다."; }
            }
        });
        progress.ShowDialog(this);
        if (IsDisposed || Disposing) return;
        if (workReview is not null && directory is not null)
        {
            using var detail = new WorkReviewForm(workReview, directory);
            detail.ShowDialog(this);
        }
        else ShowVerificationResult(message, icon, directory);
    }

    private void ShowVerificationResult(string message, MessageBoxIcon icon, string? directory)
    {
        if (directory is null)
        {
            MessageBox.Show(this, message, Text, MessageBoxButtons.OK, icon);
            return;
        }
        using var result = new Form
        {
            Text = icon == MessageBoxIcon.Information ? "검사 결과" : "검사 결과 · 확인 필요",
            ClientSize = new Size(660, 265), StartPosition = FormStartPosition.CenterParent,
            FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false
        };
        var body = new TextBox { Text = message, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill };
        var footer = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 42, Padding = new Padding(5) };
        var close = new Button { Text = "닫기", AutoSize = true, DialogResult = DialogResult.OK };
        void AddLink(string label, string path)
        {
            if (!Directory.Exists(path) && !File.Exists(path)) return;
            var link = new Button { Text = label, AutoSize = true };
            link.Click += (_, _) =>
            {
                try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
                catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or IOException)
                { MessageBox.Show(result, ex.Message, result.Text); }
            };
            footer.Controls.Add(link);
        }
        AddLink("결과 폴더", directory);
        AddLink("실패 항목", Path.Combine(directory, "FailedTests.txt"));
        string buildLog = Path.Combine(directory, "Build.log");
        AddLink("빌드 로그", File.Exists(buildLog) ? buildLog : Path.Combine(directory, "BuildDriver.log"));
        string engineLog = Path.Combine(directory, "Unreal.log");
        if (File.Exists(engineLog)) AddLink("Unreal 로그", engineLog);
        else AddLink("자동 검사 로그", Path.Combine(directory, "Automation.log"));
        AddLink("검사 상세", Path.Combine(directory, "Details.txt"));
        footer.Controls.Add(close);
        result.Controls.Add(body);
        result.Controls.Add(footer);
        result.AcceptButton = close;
        result.ShowDialog(this);
    }
}

internal sealed class VerificationInputForm : Form
{
    internal TextBox Input { get; }
    internal Func<string, string?>? ValidateInput { get; set; }

    internal VerificationInputForm(string title, string prompt, string initial, bool multiline)
    {
        Text = title;
        ClientSize = new Size(610, multiline ? 300 : 145);
        StartPosition = FormStartPosition.CenterParent;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        Input = new TextBox { Text = initial, Dock = DockStyle.Fill, Multiline = multiline,
            ScrollBars = multiline ? ScrollBars.Vertical : ScrollBars.None };
        var description = new Label { Text = prompt, Dock = DockStyle.Top, Height = 55, Padding = new Padding(8, 6, 8, 0), UseMnemonic = false };
        var footer = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 42, Padding = new Padding(5) };
        var start = new Button { Text = "실행", AutoSize = true };
        var cancel = new Button { Text = "취소", AutoSize = true, DialogResult = DialogResult.Cancel };
        start.Click += (_, _) =>
        {
            string? error = ValidateInput?.Invoke(Input.Text);
            if (error is null) DialogResult = DialogResult.OK;
            else MessageBox.Show(this, error, Text);
        };
        footer.Controls.AddRange([start, cancel]);
        Controls.Add(Input);
        Controls.Add(description);
        Controls.Add(footer);
        AcceptButton = start;
        CancelButton = cancel;
    }
}
