using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
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
        AddButton(buttons, "EOS 보이스 자격 증명", OpenEosVoiceSetupAsync);
        AddButton(buttons, "자동 연결 방화벽 설정", ConfigureHamachiFirewallAsync);
        AddButton(buttons, "자동 연결 검사", () => ValidateHamachiConnectivityAsync(false));
        AddButton(buttons, "IP 직접 연결 방화벽 설정", ConfigureDirectIpFirewallAsync);
        AddButton(buttons, "IP 직접 연결 검사", ValidateDirectIpConnectivityAsync);
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
        string? vpnHost = GetConfiguredVpnHost(backendDirectory);
        if (vpnHost is null)
        {
            Append("[주의] 외부 테스트용 자동 연결 주소가 구성되지 않았습니다.");
        }
        else
        {
            Append(IsLocalIpv4Address(vpnHost)
                ? $"[OK] 자동 연결 주소 {vpnHost}가 이 PC에 활성화되어 있습니다."
                : $"[실패] 설정된 자동 연결 주소 {vpnHost}가 현재 이 PC에 없습니다. 자동 연결 프로그램을 먼저 켜세요.");
            Append($"[정보] 외부 클라이언트 백엔드: http://{vpnHost}:5080 / 게임 서버: {vpnHost}:7777");
        }
        var directAddresses = GetLocalLanAddresses();
        if (directAddresses.Count == 0)
        {
            Append("[정보] IP 직접 입력에 사용할 활성 사설 IPv4 주소가 없습니다.");
        }
        else
        {
            foreach (var (address, interfaceName) in directAddresses)
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
        SetStatus(code == 0 ? $"{role} 패키징 성공" : $"{role} 패키징 실패 (코드 {code})");
    }

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
        await ValidateHamachiConnectivityAsync(true);
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

    private async Task ConfigureHamachiFirewallAsync()
    {
        string backend = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend");
        string? host = GetConfiguredVpnHost(backend);
        if (host is null)
        {
            Append("[실패] LocalMySql 설정에서 유효한 자동 연결 25.x 주소를 찾지 못했습니다.");
            return;
        }

        string dotnetPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "dotnet", "dotnet.exe");
        if (!File.Exists(dotnetPath))
        {
            Append("[실패] dotnet.exe 경로를 찾지 못해 백엔드 방화벽 규칙을 만들 수 없습니다.");
            return;
        }
        string? serverExecutable = Directory.Exists(archiveRoot.Text.Trim())
            ? Directory.GetFiles(archiveRoot.Text.Trim(), "ProjectProject01Server.exe", SearchOption.AllDirectories)
                .FirstOrDefault()
            : null;
        string escapedDotnetPath = dotnetPath.Replace("'", "''");
        string escapedServerPath = serverExecutable?.Replace("'", "''") ?? string.Empty;
        string script =
            "$ErrorActionPreference='Stop';" +
            "$backendName='ProjectProject01 Hamachi Backend';" +
            "$serverName='ProjectProject01 Hamachi Game Server';" +
            "Get-NetFirewallRule -DisplayName $backendName -ErrorAction SilentlyContinue | Remove-NetFirewallRule;" +
            "Get-NetFirewallRule -DisplayName $serverName -ErrorAction SilentlyContinue | Remove-NetFirewallRule;" +
            $"New-NetFirewallRule -DisplayName $backendName -Direction Inbound -Action Allow -Protocol TCP -LocalPort 5080 -LocalAddress '{host}' -RemoteAddress '25.0.0.0/8' -InterfaceAlias 'Hamachi' -Program '{escapedDotnetPath}' -Profile Any | Out-Null;";
        if (serverExecutable is not null)
        {
            script +=
                $"New-NetFirewallRule -DisplayName $serverName -Direction Inbound -Action Allow -Protocol UDP -LocalPort 7777 -LocalAddress '{host}' -RemoteAddress '25.0.0.0/8' -InterfaceAlias 'Hamachi' -Program '{escapedServerPath}' -Profile Any | Out-Null;";
        }
        string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(script));
        try
        {
            using var process = Process.Start(new ProcessStartInfo("powershell.exe")
            {
                Arguments = $"-NoProfile -ExecutionPolicy Bypass -EncodedCommand {encoded}",
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden
            });
            if (process is null)
            {
                Append("[실패] 관리자 권한 방화벽 설정을 시작하지 못했습니다.");
                return;
            }
            await process.WaitForExitAsync(lifetime.Token);
            if (process.ExitCode != 0)
            {
                Append($"[실패] 방화벽 설정이 종료 코드 {process.ExitCode}로 실패했습니다.");
                return;
            }
            Append($"[OK] 자동 연결 {host}의 백엔드 TCP 5080을 같은 가상 네트워크 참가자에게 허용했습니다.");
            Append(serverExecutable is null
                ? "[안내] 패키징된 데디케이티드 서버가 없어 UDP 7777 규칙은 만들지 않았습니다. 서버 패키징 후 이 버튼을 다시 누르세요."
                : $"[OK] 자동 연결 {host}의 게임 서버 UDP 7777을 같은 가상 네트워크 참가자에게 허용했습니다.");
        }
        catch (System.ComponentModel.Win32Exception)
        {
            Append("[취소] Windows 관리자 권한 요청이 취소되었습니다.");
        }
    }

    private async Task ValidateHamachiConnectivityAsync(bool waitForBackendStartup)
    {
        string backend = Path.Combine(ProjectRoot(), "Tools", "ProjectProject01Backend");
        string? host = GetConfiguredVpnHost(backend);
        if (host is null)
        {
            Append("[실패] LocalMySql 설정에서 유효한 자동 연결 25.x 주소를 찾지 못했습니다.");
            return;
        }
        if (!IsLocalIpv4Address(host))
        {
            Append($"[실패] 자동 연결 주소 {host}가 현재 이 PC에 할당되어 있지 않습니다.");
            return;
        }

        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(2) };
        bool backendReady = false;
        string lastFailure = string.Empty;
        int attempts = waitForBackendStartup ? 20 : 1;
        for (int attempt = 1; attempt <= attempts; attempt++)
        {
            try
            {
                using var response = await http.GetAsync($"http://{host}:5080/health", lifetime.Token);
                if (response.IsSuccessStatusCode)
                {
                    backendReady = true;
                    break;
                }
                lastFailure = $"HTTP {(int)response.StatusCode}";
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
            if (attempt < attempts)
            {
                await Task.Delay(750, lifetime.Token);
            }
        }
        if (backendReady)
        {
            Append($"[OK] 이 PC에서 자동 연결 백엔드 http://{host}:5080/health 응답 확인");
        }
        else
        {
            Append($"[실패] 자동 연결 백엔드 {host}:5080 연결 실패: {lastFailure}");
        }

        Append(IsUdpPortBound(7777)
            ? $"[OK] 게임 서버 UDP {host}:7777 수신 중"
            : "[정보] 게임 서버 UDP 7777은 아직 수신 중이 아닙니다. 멀티 경기 전에 게임 서버를 시작하세요.");
        Append("[안내] 클라이언트는 같은 가상 네트워크에 참가한 뒤 환경설정에서 '자동 연결'만 선택하면 됩니다. 상대방 IP를 서버에 입력할 필요는 없습니다.");
    }

    private async Task ConfigureDirectIpFirewallAsync()
    {
        var lanAddresses = GetLocalLanAddresses();
        if (lanAddresses.Count == 0)
        {
            Append("[실패] 활성화된 사설 IPv4 유선/Wi-Fi 어댑터를 찾지 못했습니다.");
            return;
        }
        string dotnetPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "dotnet", "dotnet.exe");
        string? serverExecutable = Directory.Exists(archiveRoot.Text.Trim())
            ? Directory.GetFiles(archiveRoot.Text.Trim(), "ProjectProject01Server.exe", SearchOption.AllDirectories)
                .FirstOrDefault()
            : null;
        if (!File.Exists(dotnetPath))
        {
            Append("[실패] dotnet.exe 경로를 찾지 못했습니다.");
            return;
        }
        var script = new StringBuilder("$ErrorActionPreference='Stop';");
        foreach (var (address, interfaceName) in lanAddresses)
        {
            string suffix = address.Replace('.', '-');
            string backendName = $"ProjectProject01 Direct IP Backend {suffix}";
            string serverName = $"ProjectProject01 Direct IP Game Server {suffix}";
            script.Append($"Get-NetFirewallRule -DisplayName '{backendName}' -ErrorAction SilentlyContinue | Remove-NetFirewallRule;");
            script.Append($"New-NetFirewallRule -DisplayName '{backendName}' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 5080 -LocalAddress '{address}' -RemoteAddress LocalSubnet -InterfaceAlias '{interfaceName.Replace("'", "''")}' -Program '{dotnetPath.Replace("'", "''")}' -Profile Any | Out-Null;");
            if (serverExecutable is not null)
            {
                script.Append($"Get-NetFirewallRule -DisplayName '{serverName}' -ErrorAction SilentlyContinue | Remove-NetFirewallRule;");
                script.Append($"New-NetFirewallRule -DisplayName '{serverName}' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 7777 -LocalAddress '{address}' -RemoteAddress LocalSubnet -InterfaceAlias '{interfaceName.Replace("'", "''")}' -Program '{serverExecutable.Replace("'", "''")}' -Profile Any | Out-Null;");
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
            Append($"[OK] IP 직접 연결용 백엔드 TCP 5080을 로컬 서브넷에만 허용했습니다: {string.Join(", ", lanAddresses.Select(item => item.Address))}");
            Append(serverExecutable is null
                ? "[안내] 패키징된 데디케이티드 서버가 없어 UDP 7777 규칙은 만들지 않았습니다. 서버 패키징 후 다시 실행하세요."
                : "[OK] IP 직접 연결용 게임 서버 UDP 7777을 로컬 서브넷에만 허용했습니다.");
        }
        catch (System.ComponentModel.Win32Exception)
        {
            Append("[취소] Windows 관리자 권한 요청이 취소되었습니다.");
        }
    }

    private async Task ValidateDirectIpConnectivityAsync()
    {
        var lanAddresses = GetLocalLanAddresses();
        if (lanAddresses.Count == 0)
        {
            Append("[실패] IP 직접 연결에 사용할 활성 사설 IPv4 주소가 없습니다.");
            return;
        }
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(3) };
        foreach (var (address, interfaceName) in lanAddresses)
        {
            try
            {
                using var response = await http.GetAsync($"http://{address}:5080/health", lifetime.Token);
                Append(response.IsSuccessStatusCode
                    ? $"[OK] IP 직접 연결 주소 http://{address}:5080 ({interfaceName}) 응답 확인"
                    : $"[실패] {address}:5080이 HTTP {(int)response.StatusCode}를 반환했습니다.");
            }
            catch (Exception exception)
            {
                Append($"[실패] {address}:5080 연결 실패: {exception.Message}");
            }
        }
        Append(IsUdpPortBound(7777)
            ? "[OK] 게임 서버 UDP 7777 수신 중"
            : "[정보] 게임 서버 UDP 7777은 아직 수신 중이 아닙니다.");
        Append("[안내] 외부 PC의 환경설정에서 'IP 직접 입력'을 선택하고 위 IPv4 주소를 입력하세요.");
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
            Append("[주의] -MULTIHOME이 지정되어 있으면 그 주소만 수신합니다. 자동 연결과 IP 직접 입력을 함께 쓰려면 해당 인자를 지우세요.");
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
