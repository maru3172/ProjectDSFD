using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

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
        AddButton(buttons, "클라이언트 업데이트 게시", PublishClientUpdateAsync);
        AddButton(buttons, "데디케이티드 서버 패키징", async () => await PackageAsync(true));
        AddButton(buttons, "MySQL 초기화/시작", async () => await RunMySqlAsync("Initialize"));
        AddButton(buttons, "MySQL 중지", async () => await RunMySqlAsync("Stop"));
        AddButton(buttons, "백엔드 시작", StartBackend);
        AddButton(buttons, "백엔드 중지", () => { StopProcess(backendProcess, "백엔드"); return Task.CompletedTask; });
        AddButton(buttons, "EOS 보이스 자격 증명", OpenEosVoiceSetupAsync);
        AddButton(buttons, "IP 직접 연결 방화벽 설정", ConfigureDirectIpFirewallAsync);
        AddButton(buttons, "IP 직접 연결 검사", () => ValidateDirectIpConnectivityAsync(false));
        AddButton(buttons, "서버 운영", OpenServerAdministrationAsync);
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

    private Task OpenServerAdministrationAsync()
    {
        using var dialog = new ServerAdministrationForm(ProjectRoot(), Append);
        dialog.ShowDialog(this);
        return Task.CompletedTask;
    }

    private static void AddField(TableLayoutPanel panel, string label, Control control)
    {
        int row = panel.RowCount++;
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left, Padding = new Padding(0, 7, 0, 0) }, 0, row);
        panel.Controls.Add(control, 1, row);
    }

    private void AddButton(FlowLayoutPanel panel, string text, Func<Task> action)
    {
        var button = new Button { Text = text, AutoSize = true, Height = 34 };
        button.Click += async (_, _) =>
        {
            button.Enabled = false;
            try
            {
                await action();
            }
            catch (OperationCanceledException)
            {
                if (!lifetime.IsCancellationRequested && !IsDisposed && !Disposing)
                {
                    Append($"[실패] {text}: 서버 응답 대기 시간이 초과되었습니다.");
                }
            }
            catch (Exception exception)
            {
                if (!IsDisposed && !Disposing)
                {
                    Append($"[실패] {text}: {exception.Message}");
                }
            }
            finally
            {
                if (!button.IsDisposed)
                {
                    button.Enabled = true;
                }
            }
        };
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
        Append(Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_GAME_CLIENT_SECRET") is { Length: > 0 }
            ? "[OK] EOS 게임 클라이언트 자격 증명이 구성되었습니다."
            : "[주의] EOS 게임 클라이언트 비밀키가 없습니다. 'EOS 보이스 자격 증명'을 실행하세요.");
        Append(Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_VOICE_SERVER_CLIENT_SECRET") is { Length: > 0 }
            ? "[OK] EOS 보이스 서버 자격 증명이 구성되었습니다."
            : "[주의] EOS 보이스 서버 비밀키가 없습니다. 'EOS 보이스 자격 증명'을 실행하세요.");
        string backendDirectory = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend");
        var directAddresses = GetDirectConnectionAddresses(backendDirectory);
        if (directAddresses.Count == 0)
        {
            Append("[정보] 클라이언트가 직접 입력할 수 있는 활성 IPv4 주소가 없습니다.");
        }
        else
        {
            foreach (var (address, interfaceName, _) in directAddresses)
            {
                Append($"[OK] IP 직접 입력 주소: {address} ({interfaceName}) / 백엔드 {address}:5080 / 게임 서버 {address}:7777");
            }
        }
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
        if (!server)
        {
            // 패키징이 기존 파일을 바꾸는 동안 이전 매니페스트를 노출하면 클라이언트가
            // 이전 해시와 새 파일을 함께 받아 검증에 실패할 수 있다.
            InvalidateClientUpdateManifests(Path.Combine(archiveRoot.Text.Trim(), role));
        }
        string args = $"BuildCookRun -project=\"{projectFile.Text.Trim()}\" -noP4 -platform=Win64 " +
            (server ? "-server -serverconfig=Development -noclient " : "-clientconfig=Shipping ") +
            $"-build -cook -stage -pak -archive -archivedirectory=\"{Path.Combine(archiveRoot.Text.Trim(), role)}\" -utf8output";
        SetStatus($"{role} 패키징 중...");
        int code;
        if (server)
        {
            code = await RunCapturedAsync(uat, args, ProjectRoot());
        }
        else
        {
            code = await PackageClientWithLocalEosCredentialAsync(uat, args);
        }
        if (code == 0 && !server)
        {
            Append("[OK] 클라이언트 패키징 성공. 업데이트 게시를 이어서 진행합니다.");
            await PublishClientUpdateAsync();
            return;
        }
        SetStatus(code == 0 ? $"{role} 패키징 성공" : $"{role} 패키징 실패 (코드 {code})");
    }

    private async Task PublishClientUpdateAsync()
    {
        string clientArchive = Path.Combine(archiveRoot.Text.Trim(), "Client");
        string? gameExecutable = Directory.Exists(clientArchive)
            ? Directory.EnumerateFiles(clientArchive, "ProjectProject01.exe", SearchOption.AllDirectories)
                .OrderByDescending(File.GetLastWriteTimeUtc)
                .FirstOrDefault()
            : null;
        if (gameExecutable is null)
        {
            Append("[차단] 패키징된 클라이언트를 찾지 못했습니다. 먼저 '클라이언트 패키징'을 실행하세요.");
            SetStatus("업데이트 게시 실패: 패키징 클라이언트 없음");
            return;
        }

        string publishedRoot = Path.GetDirectoryName(gameExecutable)!;
        InvalidateClientUpdateManifests(publishedRoot);
        string launcherProject = Path.Combine(
            ProjectRoot(), "Tools", "ProjectProject01ClientLauncher", "ProjectProject01ClientLauncher.csproj");
        if (!File.Exists(launcherProject))
        {
            Append("[실패] 클라이언트 업데이트 런처 프로젝트가 없습니다.");
            SetStatus("업데이트 게시 실패");
            return;
        }

        SetStatus("업데이트 런처 준비 중...");
        string launcherPublish = Path.Combine(ProjectRoot(), "Saved", "ClientLauncherPublish");
        Directory.CreateDirectory(launcherPublish);
        int publishCode = await RunCapturedAsync(
            "dotnet",
            $"publish \"{launcherProject}\" -c Release -r win-x64 --self-contained true " +
            $"-p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o \"{launcherPublish}\"",
            ProjectRoot());
        if (publishCode != 0)
        {
            SetStatus($"업데이트 런처 빌드 실패 (코드 {publishCode})");
            return;
        }

        string launcherSource = Path.Combine(launcherPublish, "ProjectProject01ClientLauncher.exe");
        if (!File.Exists(launcherSource))
        {
            Append("[실패] 게시된 런처 실행 파일을 찾지 못했습니다.");
            SetStatus("업데이트 런처 준비 실패");
            return;
        }
        string launcherTarget = Path.Combine(publishedRoot, "ProjectProject01ClientLauncher.exe");
        bool launcherUpdated = await TryUpdatePublishedLauncherAsync(launcherSource, launcherTarget);
        await File.WriteAllTextAsync(
            Path.Combine(publishedRoot, "업데이트_실행방법.txt"),
            "ProjectProject01ClientLauncher.exe를 실행하고 서버 PC의 IPv4 주소를 입력한 뒤 " +
            "'업데이트 및 실행'을 누르세요.\r\n게임 실행 파일을 직접 실행하면 자동 업데이트를 확인하지 않습니다.\r\n",
            new UTF8Encoding(true), lifetime.Token);

        SetStatus("클라이언트 파일 해시 계산 중...");
        string manifestPath = Path.Combine(publishedRoot, "client-update-manifest.json");
        var files = new List<PublishedClientUpdateFile>();
        string[] packageFiles = Directory.EnumerateFiles(publishedRoot, "*", SearchOption.AllDirectories)
            .Where(path => !ShouldExcludeFromClientUpdate(publishedRoot, path))
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .ToArray();
        for (int index = 0; index < packageFiles.Length; index++)
        {
            lifetime.Token.ThrowIfCancellationRequested();
            string path = packageFiles[index];
            var info = new FileInfo(path);
            await using var stream = new FileStream(
                path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 128, true);
            byte[] hash = await SHA256.HashDataAsync(stream, lifetime.Token);
            files.Add(new PublishedClientUpdateFile(
                Path.GetRelativePath(publishedRoot, path).Replace('\\', '/'),
                info.Length,
                Convert.ToHexString(hash).ToLowerInvariant()));
            SetStatus($"클라이언트 파일 해시 계산 중... {index + 1}/{packageFiles.Length}");
        }
        if (files.Count == 0)
        {
            Append("[실패] 업데이트에 포함할 클라이언트 파일이 없습니다.");
            SetStatus("업데이트 게시 실패");
            return;
        }

        string clientVersion = ReadClientVersion();
        string releaseId = $"{clientVersion}-{DateTime.UtcNow:yyyyMMddHHmmss}";
        var manifest = new PublishedClientUpdateManifest(
            releaseId,
            clientVersion,
            DateTime.UtcNow,
            Path.GetRelativePath(publishedRoot, gameExecutable).Replace('\\', '/'),
            files);
        string pendingManifestPath = manifestPath + ".publishing";
        try
        {
            await File.WriteAllTextAsync(
                pendingManifestPath,
                JsonSerializer.Serialize(manifest, new JsonSerializerOptions
                {
                    PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                    WriteIndented = true
                }),
                new UTF8Encoding(false), lifetime.Token);
            File.Move(pendingManifestPath, manifestPath, true);
        }
        finally
        {
            if (File.Exists(pendingManifestPath)) File.Delete(pendingManifestPath);
        }

        long totalBytes = files.Sum(file => file.Size);
        Append($"[OK] 클라이언트 업데이트 게시 완료: {releaseId}");
        Append($"[정보] 파일 {files.Count}개 / {FormatBytes(totalBytes)} / 위치: {publishedRoot}");
        foreach (var (address, _, _) in GetDirectConnectionAddresses(
            Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend")))
        {
            Append($"[안내] 런처 업데이트 주소: http://{address}:5080/api/client-updates/manifest");
        }
        Append("[안내] 최초 배포 때는 이 폴더 전체를 한 번 전달하고, 이후 사용자는 런처만 실행하면 됩니다.");
        if (!launcherUpdated)
        {
            Append("[주의] 실행 중인 런처는 Windows가 덮어쓰기를 막았습니다. 게임 업데이트 게시에는 성공했지만, 런처 자체 변경을 반영하려면 모든 런처 창을 닫고 '클라이언트 업데이트 게시'를 한 번 더 실행하세요.");
        }
        SetStatus(launcherUpdated ? "클라이언트 업데이트 게시 완료" : "게임 업데이트 게시 완료 (런처 교체 보류)");
    }

    private async Task<bool> TryUpdatePublishedLauncherAsync(string source, string target)
    {
        if (File.Exists(target) && await FilesHaveSameHashAsync(source, target))
        {
            Append("[OK] 배포 폴더의 업데이트 런처가 이미 최신 상태입니다.");
            return true;
        }

        try
        {
            File.Copy(source, target, true);
            return true;
        }
        catch (IOException) when (File.Exists(target))
        {
            // 실행 중인 EXE는 Windows에서 교체할 수 없다. 런처는 업데이트 매니페스트의
            // 대상이 아니므로 기존 런처를 유지하고 게임 파일 게시만 계속할 수 있다.
            return false;
        }
    }

    private static async Task<bool> FilesHaveSameHashAsync(string left, string right)
    {
        try
        {
            var leftInfo = new FileInfo(left);
            var rightInfo = new FileInfo(right);
            if (leftInfo.Length != rightInfo.Length) return false;

            await using var leftStream = new FileStream(
                left, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1024 * 128, true);
            await using var rightStream = new FileStream(
                right, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1024 * 128, true);
            byte[] leftHash = await SHA256.HashDataAsync(leftStream);
            byte[] rightHash = await SHA256.HashDataAsync(rightStream);
            return leftHash.AsSpan().SequenceEqual(rightHash);
        }
        catch (IOException)
        {
            return false;
        }
    }

    private void InvalidateClientUpdateManifests(string searchRoot)
    {
        if (!Directory.Exists(searchRoot)) return;

        foreach (string manifestPath in Directory.EnumerateFiles(
            searchRoot, "client-update-manifest.json", SearchOption.AllDirectories))
        {
            try
            {
                File.Delete(manifestPath);
                Append($"[정보] 새 게시가 완료될 때까지 이전 업데이트 목록을 비활성화했습니다: {manifestPath}");
            }
            catch (IOException exception)
            {
                Append($"[주의] 이전 업데이트 목록을 비활성화하지 못했습니다: {exception.Message}");
            }
        }
    }

    private static bool ShouldExcludeFromClientUpdate(string publishedRoot, string path)
    {
        string relative = Path.GetRelativePath(publishedRoot, path).Replace('\\', '/');
        return relative.Equals("client-update-manifest.json", StringComparison.OrdinalIgnoreCase) ||
            relative.Equals(".projectproject01-update.json", StringComparison.OrdinalIgnoreCase) ||
            relative.StartsWith(".update-temp/", StringComparison.OrdinalIgnoreCase) ||
            relative.StartsWith(".update-backup/", StringComparison.OrdinalIgnoreCase) ||
            Path.GetFileName(relative).StartsWith("ProjectProject01ClientLauncher", StringComparison.OrdinalIgnoreCase);
    }

    private string ReadClientVersion()
    {
        string configPath = Path.Combine(ProjectRoot(), "Config", "DefaultGame.ini");
        if (!File.Exists(configPath)) return "1.0.0";
        string config = File.ReadAllText(configPath);
        Match match = Regex.Match(config, @"(?im)^ProjectVersion\s*=\s*([^\r\n;]+)");
        return match.Success ? match.Groups[1].Value.Trim() : "1.0.0";
    }

    private static string FormatBytes(long bytes) => bytes switch
    {
        >= 1024L * 1024 * 1024 => $"{bytes / (1024d * 1024 * 1024):0.00} GB",
        >= 1024L * 1024 => $"{bytes / (1024d * 1024):0.00} MB",
        >= 1024L => $"{bytes / 1024d:0.00} KB",
        _ => $"{bytes} B"
    };

    private async Task<int> PackageClientWithLocalEosCredentialAsync(string uat, string args)
    {
        string? clientSecret = Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_GAME_CLIENT_SECRET");
        if (string.IsNullOrWhiteSpace(clientSecret))
        {
            Append("[차단] EOS 게임 클라이언트 비밀키가 없습니다. 'EOS 보이스 자격 증명'을 먼저 실행하고 도우미를 다시 시작하세요.");
            return 2;
        }
        if (clientSecret.Length is < 32 or > 64 || clientSecret.Contains('\r') || clientSecret.Contains('\n'))
        {
            Append("[차단] EOS 게임 클라이언트 비밀키 형식이 올바르지 않습니다. 포털에서 발급된 Client Secret 전체를 다시 입력하세요.");
            return 2;
        }

        string engineConfig = Path.Combine(ProjectRoot(), "Config", "DefaultEngine.ini");
        string originalConfig = await File.ReadAllTextAsync(engineConfig, lifetime.Token);
        string temporaryConfig = originalConfig.TrimEnd() + Environment.NewLine + Environment.NewLine +
            "[EOSVoiceChat]" + Environment.NewLine +
            "ClientSecret=" + clientSecret + Environment.NewLine;
        try
        {
            await File.WriteAllTextAsync(engineConfig, temporaryConfig, new UTF8Encoding(false), lifetime.Token);
            Append("[정보] 로컬 EOS GameClient 자격 증명을 패키징 설정에 임시 주입했습니다(로그·Git에는 표시하지 않음). ");
            return await RunCapturedAsync(uat, args, ProjectRoot());
        }
        finally
        {
            await File.WriteAllTextAsync(engineConfig, originalConfig, new UTF8Encoding(false), CancellationToken.None);
            Append("[OK] 소스 설정에서 임시 EOS GameClient 비밀키를 제거했습니다.");
        }
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
        string urls = GetBackendListenUrls(backend);
        Append($"[정보] 백엔드 수신 주소: {urls}");
        backendProcess = StartStreamingProcess("dotnet", $"run --urls \"{urls}\"", backend, "BACKEND");
        await Task.Delay(1800);
        if (backendProcess is not { HasExited: false })
        {
            Append("[실패] 백엔드가 즉시 종료되었습니다. 위 로그를 확인하세요.");
            return;
        }
        Append("[OK] 백엔드 프로세스 시작");
        await ValidateDirectIpConnectivityAsync(true);
    }

    private Task OpenEosVoiceSetupAsync()
    {
        string script = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend", "ConfigureProjectProject01EosVoice.ps1");
        if (!File.Exists(script))
        {
            Append("[실패] EOS 보이스 설정 스크립트를 찾지 못했습니다.");
            return Task.CompletedTask;
        }
        Process.Start(new ProcessStartInfo("powershell.exe")
        {
            Arguments = $"-NoProfile -ExecutionPolicy Bypass -NoExit -File \"{script}\" -Action Configure",
            WorkingDirectory = Path.GetDirectoryName(script)!,
            UseShellExecute = true
        });
        Append("[정보] 별도 창에서 포털의 두 Client Secret을 입력하세요. 값은 화면이나 로그에 출력되지 않습니다.");
        return Task.CompletedTask;
    }

    private static string GetBackendListenUrls(string backendDirectory)
    {
        const string loopbackUrl = "http://127.0.0.1:5080";
        string? host = GetConfiguredVpnHost(backendDirectory);
        var urls = new List<string> { loopbackUrl };
        if (host is not null) urls.Add($"http://{host}:5080");
        urls.AddRange(GetLocalLanAddresses().Select(item => $"http://{item.Address}:5080"));
        return string.Join(';', urls.Distinct(StringComparer.OrdinalIgnoreCase));
    }

    private static IReadOnlyList<(string Address, string InterfaceName)> GetLocalLanAddresses() =>
        NetworkInterface.GetAllNetworkInterfaces()
            .Where(network => network.OperationalStatus == OperationalStatus.Up &&
                network.NetworkInterfaceType is NetworkInterfaceType.Wireless80211 or NetworkInterfaceType.Ethernet &&
                !network.Name.Contains("Hamachi", StringComparison.OrdinalIgnoreCase) &&
                !network.Name.Contains("vEthernet", StringComparison.OrdinalIgnoreCase) &&
                !network.Description.Contains("Virtual", StringComparison.OrdinalIgnoreCase))
            .SelectMany(network => network.GetIPProperties().UnicastAddresses
                .Where(unicast => unicast.Address.AddressFamily == AddressFamily.InterNetwork &&
                    IsPrivateIpv4(unicast.Address))
                .Select(unicast => (unicast.Address.ToString(), network.Name)))
            .Distinct()
            .ToArray();

    private static bool IsPrivateIpv4(IPAddress address)
    {
        byte[] bytes = address.GetAddressBytes();
        return bytes[0] == 10 || (bytes[0] == 172 && bytes[1] is >= 16 and <= 31) ||
            (bytes[0] == 192 && bytes[1] == 168);
    }

    private static IReadOnlyList<(string Address, string InterfaceName, bool IsVpn)> GetDirectConnectionAddresses(
        string backendDirectory)
    {
        var result = GetLocalLanAddresses()
            .Select(item => (item.Address, item.InterfaceName, IsVpn: false))
            .ToList();
        string? vpnHost = GetConfiguredVpnHost(backendDirectory);
        if (vpnHost is not null && IsLocalIpv4Address(vpnHost))
        {
            string interfaceName = NetworkInterface.GetAllNetworkInterfaces()
                .FirstOrDefault(network => network.OperationalStatus == OperationalStatus.Up &&
                    network.GetIPProperties().UnicastAddresses.Any(unicast =>
                        unicast.Address.AddressFamily == AddressFamily.InterNetwork &&
                        string.Equals(unicast.Address.ToString(), vpnHost, StringComparison.OrdinalIgnoreCase)))
                ?.Name ?? "VPN";
            result.Add((vpnHost, interfaceName, IsVpn: true));
        }
        return result
            .GroupBy(item => item.Address, StringComparer.OrdinalIgnoreCase)
            .Select(group => group.First())
            .ToArray();
    }

    private static string? GetConfiguredVpnHost(string backendDirectory)
    {
        string localConfigurationPath = Path.Combine(
            backendDirectory, "LocalMySql", "ProjectProject01Backend.local.json");
        if (!File.Exists(localConfigurationPath)) return null;
        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(localConfigurationPath));
            if (document.RootElement.TryGetProperty("Security", out JsonElement security) &&
                security.TryGetProperty("AllowedInsecureVpnHost", out JsonElement hostElement))
            {
                string? host = hostElement.GetString()?.Trim();
                if (IPAddress.TryParse(host, out IPAddress? address) && address.GetAddressBytes()[0] == 25)
                {
                    return host;
                }
            }
        }
        catch (Exception)
        {
            // 로컬 설정을 읽지 못하면 안전하게 루프백만 사용한다.
        }
        return null;
    }

    private async Task ConfigureDirectIpFirewallAsync()
    {
        string backend = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend");
        var addresses = GetDirectConnectionAddresses(backend);
        if (addresses.Count == 0)
        {
            Append("[실패] 클라이언트가 직접 입력할 수 있는 활성 IPv4 주소를 찾지 못했습니다.");
            return;
        }
        string? serverExecutable = Directory.Exists(archiveRoot.Text.Trim())
            ? Directory.GetFiles(archiveRoot.Text.Trim(), "ProjectProject01Server.exe", SearchOption.AllDirectories)
                .FirstOrDefault()
            : null;
        var script = new StringBuilder("$ErrorActionPreference='Stop';");
        foreach (var (address, interfaceName, isVpn) in addresses)
        {
            string suffix = address.Replace('.', '-');
            string backendName = $"ProjectProject01 Direct IP Backend {suffix}";
            string serverName = $"ProjectProject01 Direct IP Game Server {suffix}";
            string remoteAddress = isVpn ? "25.0.0.0/8" : "LocalSubnet";
            script.Append($"Get-NetFirewallRule -DisplayName '{backendName}' -ErrorAction SilentlyContinue | Remove-NetFirewallRule;");
            // dotnet run은 실제 수신 소켓을 dotnet.exe가 아니라 생성된 Backend apphost EXE에서
            // 열 수 있다. 프로그램 경로로 제한하면 같은 PC 검사만 성공하고 외부 요청은
            // 방화벽에서 거부될 수 있으므로 주소·인터페이스·원격 대역·포트로 제한한다.
            script.Append($"New-NetFirewallRule -DisplayName '{backendName}' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 5080 -LocalAddress '{address}' -RemoteAddress '{remoteAddress}' -InterfaceAlias '{interfaceName.Replace("'", "''")}' -Profile Any | Out-Null;");
            if (serverExecutable is not null)
            {
                script.Append($"Get-NetFirewallRule -DisplayName '{serverName}' -ErrorAction SilentlyContinue | Remove-NetFirewallRule;");
                script.Append($"New-NetFirewallRule -DisplayName '{serverName}' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 7777 -LocalAddress '{address}' -RemoteAddress '{remoteAddress}' -InterfaceAlias '{interfaceName.Replace("'", "''")}' -Program '{serverExecutable.Replace("'", "''")}' -Profile Any | Out-Null;");
            }
        }
        string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(script.ToString()));
        try
        {
            using var process = Process.Start(new ProcessStartInfo("powershell.exe")
            {
                Arguments = $"-NoProfile -ExecutionPolicy Bypass -EncodedCommand {encoded}",
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden
            });
            if (process is null) return;
            await process.WaitForExitAsync(lifetime.Token);
            if (process.ExitCode != 0)
            {
                Append($"[실패] IP 직접 연결 방화벽 설정이 종료 코드 {process.ExitCode}로 실패했습니다.");
                return;
            }
            Append($"[OK] IP 직접 연결용 백엔드 TCP 5080을 허용했습니다: {string.Join(", ", addresses.Select(item => item.Address))}");
            Append(serverExecutable is null
                ? "[안내] 패키징된 데디케이티드 서버가 없어 UDP 7777 규칙은 만들지 않았습니다. 서버 패키징 후 다시 실행하세요."
                : "[OK] IP 직접 연결용 게임 서버 UDP 7777을 해당 연결 대역에 허용했습니다.");
        }
        catch (System.ComponentModel.Win32Exception)
        {
            Append("[취소] Windows 관리자 권한 요청이 취소되었습니다.");
        }
    }

    private async Task ValidateDirectIpConnectivityAsync(bool waitForBackendStartup = false)
    {
        string backend = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend");
        var addresses = GetDirectConnectionAddresses(backend);
        if (addresses.Count == 0)
        {
            Append("[실패] IP 직접 연결에 사용할 활성 IPv4 주소가 없습니다.");
            return;
        }
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(3) };
        foreach (var (address, interfaceName, _) in addresses)
        {
            string lastFailure = string.Empty;
            bool succeeded = false;
            int attempts = waitForBackendStartup ? 20 : 1;
            for (int attempt = 1; attempt <= attempts; attempt++)
            {
                try
                {
                    using var response = await http.GetAsync($"http://{address}:5080/health", lifetime.Token);
                    succeeded = response.IsSuccessStatusCode;
                    lastFailure = succeeded ? string.Empty : $"HTTP {(int)response.StatusCode}";
                    if (succeeded) break;
                }
                catch (OperationCanceledException) when (lifetime.IsCancellationRequested)
                {
                    return;
                }
                catch (OperationCanceledException)
                {
                    lastFailure = "서버 응답 대기 시간 초과";
                }
                catch (Exception exception)
                {
                    lastFailure = exception.Message;
                }
                if (attempt < attempts) await Task.Delay(750, lifetime.Token);
            }
            Append(succeeded
                ? $"[OK] IP 직접 연결 주소 http://{address}:5080 ({interfaceName}) 응답 확인"
                : $"[실패] {address}:5080 연결 실패: {lastFailure}");
        }
        Append(IsUdpPortBound(7777)
            ? "[OK] 게임 서버 UDP 7777 수신 중"
            : "[정보] 게임 서버 UDP 7777은 아직 수신 중이 아닙니다.");
        Append("[안내] 외부 PC의 환경설정에 위 IPv4 주소 중 실제로 공유할 주소를 직접 입력하세요.");
    }

    private async Task StartServer()
    {
        if (serverProcess is { HasExited: false }) { Append("[정보] 게임 서버가 이미 실행 중입니다."); return; }
        string[] candidates = Directory.Exists(archiveRoot.Text.Trim())
            ? Directory.GetFiles(archiveRoot.Text.Trim(), "ProjectProject01Server.exe", SearchOption.AllDirectories) : [];
        if (candidates.Length == 0) { Append("[실패] 패키징된 ProjectProject01Server.exe가 없습니다."); return; }
        string arguments = serverArgs.Text.Trim();
        if (arguments.Contains("-MULTIHOME=", StringComparison.OrdinalIgnoreCase))
        {
            Append("[주의] -MULTIHOME이 지정되어 있으면 그 주소만 수신합니다. 여러 IP 경로를 함께 쓰려면 해당 인자를 지우세요.");
        }
        serverProcess = StartStreamingProcess(candidates[0], arguments, Path.GetDirectoryName(candidates[0])!, "SERVER");
        await Task.Delay(1200);
        if (serverProcess is { HasExited: false })
        {
            Append("[OK] 데디케이티드 서버 시작: 활성 네트워크의 UDP 7777 수신");
        }
        else
        {
            Append("[실패] 서버가 즉시 종료되었습니다. 위 로그를 확인하세요.");
        }
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

    private static bool IsUdpPortBound(int port) => IPGlobalProperties.GetIPGlobalProperties()
        .GetActiveUdpListeners().Any(endpoint => endpoint.Port == port);

    private static bool IsLocalIpv4Address(string address) => NetworkInterface.GetAllNetworkInterfaces()
        .Where(network => network.OperationalStatus == OperationalStatus.Up)
        .SelectMany(network => network.GetIPProperties().UnicastAddresses)
        .Any(unicast => unicast.Address.AddressFamily == AddressFamily.InterNetwork &&
            string.Equals(unicast.Address.ToString(), address, StringComparison.OrdinalIgnoreCase));

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

internal sealed record PublishedClientUpdateFile(string Path, long Size, string Sha256);
internal sealed record PublishedClientUpdateManifest(
    string ReleaseId,
    string ClientVersion,
    DateTime PublishedAtUtc,
    string ExecutablePath,
    List<PublishedClientUpdateFile> Files);
