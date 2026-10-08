using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.Json;

namespace ProjectProject01BuildServerHelper;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}

internal sealed class MainForm : Form
{
    private readonly TextBox engineRoot = new() { Dock = DockStyle.Fill };
    private readonly TextBox projectFile = new() { Dock = DockStyle.Fill };
    private readonly TextBox archiveRoot = new() { Dock = DockStyle.Fill };
    private readonly TextBox serverArgs = new() { Dock = DockStyle.Fill, Text = "MultiplayTest -log -port=7777" };
    private readonly RichTextBox output = new() { Dock = DockStyle.Fill, ReadOnly = true, BackColor = Color.FromArgb(24, 24, 24), ForeColor = Color.Gainsboro };
    private readonly Label status = new() { AutoSize = true, Text = "준비" };
    private readonly CancellationTokenSource lifetime = new();
    private Process? backendProcess;
    private Process? serverProcess;

    public MainForm()
    {
        Text = "ProjectProject01 Build + Server Helper";
        Width = 1160;
        Height = 760;
        MinimumSize = new Size(900, 620);
        engineRoot.Text = Environment.GetEnvironmentVariable("UE_ENGINE_ROOT") ?? @"C:\Program Files\Epic Games\UE_5.8\Engine";
        projectFile.Text = FindProjectFile() ?? @"C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\ProjectProject01.uproject";
        archiveRoot.Text = Path.Combine(Path.GetDirectoryName(projectFile.Text) ?? Environment.CurrentDirectory, "Builds");
        BuildUi();
        FormClosing += (_, _) => { lifetime.Cancel(); StopProcess(backendProcess, "백엔드"); StopProcess(serverProcess, "게임 서버"); };
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4, Padding = new Padding(10) };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var fields = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 2, AutoSize = true };
        fields.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        fields.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        AddField(fields, "UE Engine 경로", engineRoot);
        AddField(fields, ".uproject", projectFile);
        AddField(fields, "패키징 출력", archiveRoot);
        AddField(fields, "서버 실행 인자", serverArgs);
        root.Controls.Add(fields);

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = true, Padding = new Padding(0, 8, 0, 8) };
        AddButton(buttons, "환경 검사", async () => await ValidateAsync());
        AddButton(buttons, "클라이언트 패키징", async () => await PackageAsync(false));
        AddButton(buttons, "데디케이티드 서버 패키징", async () => await PackageAsync(true));
        AddButton(buttons, "MySQL 초기화/시작", async () => await RunMySqlAsync("Initialize"));
        AddButton(buttons, "MySQL 중지", async () => await RunMySqlAsync("Stop"));
        AddButton(buttons, "백엔드 시작", StartBackend);
        AddButton(buttons, "백엔드 중지", () => { StopProcess(backendProcess, "백엔드"); return Task.CompletedTask; });
        AddButton(buttons, "게임 서버 시작", StartServer);
        AddButton(buttons, "게임 서버 중지", () => { StopProcess(serverProcess, "게임 서버"); return Task.CompletedTask; });
        AddButton(buttons, "전체 흐름 스모크 테스트", RunSmokeTestAsync);
        AddButton(buttons, "로그 폴더", () => { OpenFolder(ProjectRoot(), "Saved\\Logs"); return Task.CompletedTask; });
        AddButton(buttons, "진단 결과 폴더", () => { OpenFolder(ProjectRoot(), "Saved\\Diagnostics"); return Task.CompletedTask; });
        AddButton(buttons, "빌드 폴더", () => { OpenFolder(archiveRoot.Text, ""); return Task.CompletedTask; });
        root.Controls.Add(buttons);
        root.Controls.Add(output);
        root.Controls.Add(status);
        Controls.Add(root);
    }

    private static void AddField(TableLayoutPanel panel, string label, Control control)
    {
        int row = panel.RowCount++;
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left, Padding = new Padding(0, 7, 0, 0) }, 0, row);
        panel.Controls.Add(control, 1, row);
    }

    private static void AddButton(FlowLayoutPanel panel, string text, Func<Task> action)
    {
        var button = new Button { Text = text, AutoSize = true, Height = 34 };
        button.Click += async (_, _) => { button.Enabled = false; try { await action(); } finally { button.Enabled = true; } };
        panel.Controls.Add(button);
    }

    private async Task ValidateAsync()
    {
        SetStatus("검사 중...");
        var issues = new List<string>();
        string uat = Path.Combine(engineRoot.Text.Trim(), "Build", "BatchFiles", "RunUAT.bat");
        if (!File.Exists(uat)) issues.Add("RunUAT.bat 없음: UE Engine 경로를 확인하세요.");
        if (!File.Exists(projectFile.Text.Trim())) issues.Add("uproject 파일을 찾지 못했습니다.");
        if (!File.Exists(Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend", "ProjectProject01Backend.csproj"))) issues.Add("백엔드 프로젝트가 없습니다.");
        bool installedBuild = File.Exists(Path.Combine(engineRoot.Text.Trim(), "Build", "InstalledBuild.txt"));
        Append(installedBuild
            ? "[주의] Launcher Installed Build입니다. 클라이언트는 가능하지만 전용 서버 패키징은 소스 빌드 UE가 필요할 수 있습니다."
            : "[OK] 소스 빌드 형태의 UE Engine 경로입니다.");
        Append(IsPortListening(3307) ? "[OK] MySQL 3307 포트 사용 중" : "[정보] MySQL 3307 포트가 아직 열리지 않았습니다.");
        Append(IsPortListening(7777) ? "[주의] 7777 포트가 이미 사용 중" : "[OK] 게임 서버 7777 포트 사용 가능");
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(2) };
            using var response = await http.GetAsync("http://127.0.0.1:5080/health", lifetime.Token);
            Append(response.IsSuccessStatusCode ? "[OK] 백엔드 /health 정상" : $"[주의] 백엔드 HTTP {(int)response.StatusCode}");
        }
        catch { Append("[정보] 백엔드 5080은 현재 실행 중이 아닙니다."); }
        Append(Environment.GetEnvironmentVariable("PROJECTPROJECT01_GAME_SERVER_SECRET") is { Length: > 0 }
            ? "[OK] 게임 서버 서명 비밀키가 환경변수에 존재합니다."
            : "[주의] PROJECTPROJECT01_GAME_SERVER_SECRET 환경변수가 없습니다.");
        foreach (string issue in issues) Append("[실패] " + issue);
        SetStatus(issues.Count == 0 ? "환경 검사 완료" : $"환경 검사: {issues.Count}개 필수 문제");
    }

    private async Task PackageAsync(bool server)
    {
        string uat = Path.Combine(engineRoot.Text.Trim(), "Build", "BatchFiles", "RunUAT.bat");
        if (!File.Exists(uat) || !File.Exists(projectFile.Text.Trim())) { Append("[실패] 먼저 환경 경로를 확인하세요."); return; }
        if (server && File.Exists(Path.Combine(engineRoot.Text.Trim(), "Build", "InstalledBuild.txt")))
        {
            Append("[차단] Launcher 엔진은 ProjectProject01Server 패키징을 지원하지 않습니다. 소스 빌드 UE Engine 경로로 바꾸세요.");
            return;
        }
        Directory.CreateDirectory(archiveRoot.Text.Trim());
        string role = server ? "Server" : "Client";
        string args = $"BuildCookRun -project=\"{projectFile.Text.Trim()}\" -noP4 -platform=Win64 " +
            (server ? "-server -serverconfig=Development -noclient " : "-clientconfig=Shipping ") +
            $"-build -cook -stage -pak -archive -archivedirectory=\"{Path.Combine(archiveRoot.Text.Trim(), role)}\" -utf8output";
        SetStatus($"{role} 패키징 중...");
        int code = await RunCapturedAsync(uat, args, ProjectRoot());
        SetStatus(code == 0 ? $"{role} 패키징 성공" : $"{role} 패키징 실패 (코드 {code})");
    }

    private Task RunMySqlAsync(string action)
    {
        string backend = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend");
        string script = Path.Combine(backend, "ProjectProject01MySql.ps1");
        return RunCapturedAsync("powershell.exe", $"-NoProfile -ExecutionPolicy Bypass -File \"{script}\" -Action {action}", backend);
    }

    private async Task StartBackend()
    {
        if (backendProcess is { HasExited: false }) { Append("[정보] 백엔드가 이미 실행 중입니다."); return; }
        string backend = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend");
        backendProcess = StartStreamingProcess("dotnet", "run --urls http://127.0.0.1:5080", backend, "BACKEND");
        await Task.Delay(1200);
        Append(backendProcess is { HasExited: false } ? "[OK] 백엔드 프로세스 시작" : "[실패] 백엔드가 즉시 종료되었습니다. 위 로그를 확인하세요.");
    }

    private async Task StartServer()
    {
        if (serverProcess is { HasExited: false }) { Append("[정보] 게임 서버가 이미 실행 중입니다."); return; }
        string[] candidates = Directory.Exists(archiveRoot.Text.Trim())
            ? Directory.GetFiles(archiveRoot.Text.Trim(), "ProjectProject01Server.exe", SearchOption.AllDirectories) : [];
        if (candidates.Length == 0) { Append("[실패] 패키징된 ProjectProject01Server.exe가 없습니다."); return; }
        serverProcess = StartStreamingProcess(candidates[0], serverArgs.Text, Path.GetDirectoryName(candidates[0])!, "SERVER");
        await Task.Delay(1200);
        Append(serverProcess is { HasExited: false } ? "[OK] 데디케이티드 서버 시작" : "[실패] 서버가 즉시 종료되었습니다. 위 로그를 확인하세요.");
    }

    private async Task RunSmokeTestAsync()
    {
        string smokeProject = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01SmokeTest", "ProjectProject01SmokeTest.csproj");
        if (!File.Exists(smokeProject))
        {
            Append("[실패] 전체 흐름 스모크 테스트 프로젝트가 없습니다.");
            return;
        }
        if (!IsPortListening(3307) || !IsPortListening(5080))
        {
            Append("[차단] MySQL 초기화/시작과 백엔드 시작을 먼저 실행하세요.");
            return;
        }
        SetStatus("전체 흐름 스모크 테스트 중...");
        int code = await RunCapturedAsync("dotnet",
            $"run --project \"{smokeProject}\" -- --project-root \"{ProjectRoot()}\"",
            ProjectRoot());
        SetStatus(code == 0 ? "전체 흐름 스모크 테스트 통과" : $"스모크 테스트 실패 (코드 {code})");
    }

    private async Task<int> RunCapturedAsync(string file, string args, string workingDirectory)
    {
        Append($"> {file} {args}");
        using Process process = StartStreamingProcess(file, args, workingDirectory, "BUILD");
        await process.WaitForExitAsync(lifetime.Token);
        Append($"[종료 코드] {process.ExitCode}");
        return process.ExitCode;
    }

    private Process StartStreamingProcess(string file, string args, string workingDirectory, string prefix)
    {
        var process = new Process
        {
            StartInfo = new ProcessStartInfo(file, args)
            {
                WorkingDirectory = workingDirectory,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            },
            EnableRaisingEvents = true
        };
        process.OutputDataReceived += (_, e) => { if (e.Data != null) Append($"[{prefix}] {e.Data}"); };
        process.ErrorDataReceived += (_, e) => { if (e.Data != null) Append($"[{prefix}:ERR] {e.Data}"); };
        process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine();
        return process;
    }

    private void StopProcess(Process? process, string label)
    {
        if (process is not { HasExited: false }) return;
        try { process.Kill(true); process.WaitForExit(3000); Append($"[OK] {label} 중지"); }
        catch (Exception ex) { Append($"[실패] {label} 중지: {ex.Message}"); }
    }

    private static bool IsPortListening(int port) => IPGlobalProperties.GetIPGlobalProperties()
        .GetActiveTcpListeners().Any(endpoint => endpoint.Port == port);

    private string ProjectRoot() => Path.GetDirectoryName(projectFile.Text.Trim()) ?? Environment.CurrentDirectory;
    private void OpenFolder(string root, string child)
    {
        string path = Path.GetFullPath(Path.Combine(root, child));
        Directory.CreateDirectory(path);
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{path}\"") { UseShellExecute = true });
    }
    private void Append(string text)
    {
        if (InvokeRequired) { BeginInvoke(() => Append(text)); return; }
        output.AppendText($"[{DateTime.Now:HH:mm:ss}] {text}{Environment.NewLine}");
        output.SelectionStart = output.TextLength; output.ScrollToCaret();
    }
    private void SetStatus(string text)
    {
        if (InvokeRequired) { BeginInvoke(() => SetStatus(text)); return; }
        status.Text = text;
    }
    private static string? FindProjectFile()
    {
        DirectoryInfo? directory = new(AppContext.BaseDirectory);
        while (directory != null)
        {
            string candidate = Path.Combine(directory.FullName, "ProjectProject01.uproject");
            if (File.Exists(candidate)) return candidate;
            directory = directory.Parent;
        }
        return null;
    }
}
