using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace ProjectProject01ClientLauncher;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new LauncherForm());
    }
}

internal sealed class LauncherForm : Form
{
    private const string ManifestFileName = "client-update-manifest.json";
    private const string LocalManifestFileName = ".projectproject01-update.json";
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = true
    };

    private readonly TextBox serverAddress = new() { Dock = DockStyle.Fill };
    private readonly Label currentVersion = new() { AutoSize = true, Text = "설치 버전: 확인 전" };
    private readonly Label latestVersion = new() { AutoSize = true, Text = "서버 버전: 확인 전" };
    private readonly Label status = new() { AutoSize = true, Text = "서버 PC의 IPv4 주소를 입력하세요." };
    private readonly ProgressBar progress = new() { Dock = DockStyle.Fill, Minimum = 0, Maximum = 100 };
    private readonly RichTextBox log = new()
    {
        Dock = DockStyle.Fill,
        ReadOnly = true,
        BackColor = Color.FromArgb(24, 24, 24),
        ForeColor = Color.Gainsboro
    };
    private readonly Button checkButton = new() { Text = "업데이트 확인", AutoSize = true };
    private readonly Button updateButton = new() { Text = "업데이트 및 실행", AutoSize = true };
    private readonly Button launchButton = new() { Text = "현재 버전 실행", AutoSize = true };
    private readonly CancellationTokenSource lifetime = new();
    private readonly string installRoot = Path.GetFullPath(AppContext.BaseDirectory);
    private readonly string settingsPath;
    private bool busy;

    public LauncherForm()
    {
        Text = "ProjectProject01 업데이트 런처";
        Width = 760;
        Height = 500;
        MinimumSize = new Size(640, 420);
        StartPosition = FormStartPosition.CenterScreen;
        settingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ProjectProject01", "ClientLauncher.json");
        BuildUi();
        LoadSettings();
        FormClosing += (_, _) => lifetime.Cancel();
        checkButton.Click += async (_, _) => await RunBusyAsync(CheckForUpdatesAsync);
        updateButton.Click += async (_, _) => await RunBusyAsync(() => UpdateAsync(true));
        launchButton.Click += (_, _) => LaunchGame();
        Shown += async (_, _) =>
        {
            if (HasValidSavedServerAddress())
            {
                await RunBusyAsync(CheckForUpdatesAsync);
            }
            else
            {
                SetStatus("서버 PC의 IPv4 주소를 입력한 뒤 업데이트 확인을 누르세요.");
            }
        };
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 7,
            Padding = new Padding(12)
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var title = new Label
        {
            AutoSize = true,
            Font = new Font(Font.FontFamily, 18, FontStyle.Bold),
            Text = "ProjectProject01"
        };
        root.Controls.Add(title);

        var addressRow = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 2, AutoSize = true };
        addressRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 140));
        addressRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        addressRow.Controls.Add(new Label
        {
            Text = "서버 PC IPv4",
            AutoSize = true,
            Anchor = AnchorStyles.Left,
            Padding = new Padding(0, 7, 0, 0)
        }, 0, 0);
        addressRow.Controls.Add(serverAddress, 1, 0);
        root.Controls.Add(addressRow);

        var versionRow = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true };
        versionRow.Controls.Add(currentVersion);
        versionRow.Controls.Add(new Label { AutoSize = true, Text = "    " });
        versionRow.Controls.Add(latestVersion);
        root.Controls.Add(versionRow);

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true };
        buttons.Controls.Add(checkButton);
        buttons.Controls.Add(updateButton);
        buttons.Controls.Add(launchButton);
        root.Controls.Add(buttons);
        root.Controls.Add(progress);
        root.Controls.Add(log);
        root.Controls.Add(status);
        Controls.Add(root);
    }

    private async Task RunBusyAsync(Func<Task> action)
    {
        if (busy) return;
        busy = true;
        checkButton.Enabled = updateButton.Enabled = launchButton.Enabled = false;
        try
        {
            SaveSettings();
            await action();
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested)
        {
            SetStatus("작업을 취소했습니다.");
        }
        catch (TimeoutException exception)
        {
            Append("[실패] " + exception.Message);
            progress.Value = 0;
            SetStatus("서버가 응답하지 않습니다. 연결 대기 상태를 해제했습니다.");
        }
        catch (HttpRequestException exception)
        {
            Append("[실패] " + exception.Message);
            progress.Value = 0;
            SetStatus("서버에 연결할 수 없습니다. 연결 대기 상태를 해제했습니다.");
        }
        catch (Exception exception)
        {
            Append("[실패] " + exception.Message);
            progress.Value = 0;
            SetStatus("업데이트 작업에 실패했습니다.");
        }
        finally
        {
            busy = false;
            checkButton.Enabled = updateButton.Enabled = launchButton.Enabled = true;
        }
    }

    private async Task CheckForUpdatesAsync()
    {
        string baseUrl = GetBackendBaseUrl();
        SetStatus("업데이트 정보를 확인하는 중...");
        progress.Value = 0;
        ClientUpdateManifest remote = await FetchManifestAsync(baseUrl);
        ClientUpdateManifest? local = await LoadLocalManifestAsync();
        currentVersion.Text = "설치 버전: " + (local?.ClientVersion ?? "초기 배포본");
        latestVersion.Text = $"서버 버전: {remote.ClientVersion} ({remote.ReleaseId})";
        List<ClientUpdateFile> changed = await FindChangedFilesAsync(remote);
        long bytes = changed.Sum(file => file.Size);
        Append(changed.Count == 0
            ? $"[OK] 최신 상태입니다. 게시 ID: {remote.ReleaseId}"
            : $"[정보] 변경 파일 {changed.Count}개, {FormatBytes(bytes)}를 받을 수 있습니다.");
        SetStatus(changed.Count == 0 ? "현재 클라이언트가 최신 상태입니다." : "업데이트가 있습니다.");
    }

    private async Task UpdateAsync(bool launchAfterUpdate)
    {
        string baseUrl = GetBackendBaseUrl();
        ClientUpdateManifest remote = await FetchManifestAsync(baseUrl);
        ValidateManifest(remote);
        EnsureGameIsNotRunning(remote.ExecutablePath);
        ClientUpdateManifest? local = await LoadLocalManifestAsync();
        List<ClientUpdateFile> changed = await FindChangedFilesAsync(remote);
        HashSet<string> remotePaths = remote.Files.Select(file => NormalizeRelativePath(file.Path))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        List<string> stalePaths = local?.Files
            .Select(file => NormalizeRelativePath(file.Path))
            .Where(path => !remotePaths.Contains(path))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList() ?? [];

        if (changed.Count > 0 || stalePaths.Count > 0)
        {
            await DownloadAndCommitAsync(baseUrl, remote, changed, stalePaths);
        }
        await File.WriteAllTextAsync(
            Path.Combine(installRoot, LocalManifestFileName),
            JsonSerializer.Serialize(remote, JsonOptions),
            lifetime.Token);
        currentVersion.Text = $"설치 버전: {remote.ClientVersion} ({remote.ReleaseId})";
        latestVersion.Text = $"서버 버전: {remote.ClientVersion} ({remote.ReleaseId})";
        progress.Value = 100;
        Append("[OK] 클라이언트 업데이트와 파일 검증이 완료되었습니다.");
        SetStatus("최신 버전입니다.");
        if (launchAfterUpdate) LaunchGame(remote.ExecutablePath);
    }

    private async Task DownloadAndCommitAsync(
        string baseUrl,
        ClientUpdateManifest manifest,
        IReadOnlyList<ClientUpdateFile> changed,
        IReadOnlyList<string> stalePaths)
    {
        string safeReleaseId = Regex.Replace(manifest.ReleaseId, "[^A-Za-z0-9._-]", "_");
        string tempRoot = Path.Combine(installRoot, ".update-temp", safeReleaseId);
        string backupRoot = Path.Combine(installRoot, ".update-backup", safeReleaseId);
        if (Directory.Exists(tempRoot)) Directory.Delete(tempRoot, true);
        Directory.CreateDirectory(tempRoot);
        long totalBytes = Math.Max(1, changed.Sum(file => file.Size));
        long downloadedBytes = 0;
        using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(30) };

        foreach (ClientUpdateFile file in changed)
        {
            lifetime.Token.ThrowIfCancellationRequested();
            string relativePath = NormalizeRelativePath(file.Path);
            string tempPath = ResolveInsideRoot(tempRoot, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(tempPath)!);
            string uriPath = string.Join('/', relativePath.Split(Path.DirectorySeparatorChar)
                .Select(Uri.EscapeDataString));
            using HttpResponseMessage response = await http.GetAsync(
                $"{baseUrl}/api/client-updates/files/{uriPath}",
                HttpCompletionOption.ResponseHeadersRead,
                lifetime.Token);
            response.EnsureSuccessStatusCode();
            await using Stream source = await response.Content.ReadAsStreamAsync(lifetime.Token);
            await using var destination = new FileStream(
                tempPath, FileMode.Create, FileAccess.Write, FileShare.None, 1024 * 128, true);
            byte[] buffer = new byte[1024 * 128];
            int read;
            while ((read = await source.ReadAsync(buffer, lifetime.Token)) > 0)
            {
                await destination.WriteAsync(buffer.AsMemory(0, read), lifetime.Token);
                downloadedBytes += read;
                SetProgress(downloadedBytes, totalBytes, $"받는 중: {file.Path}");
            }
            await destination.FlushAsync(lifetime.Token);
            string hash = await ComputeSha256Async(tempPath, lifetime.Token);
            if (!string.Equals(hash, file.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"다운로드 파일 검증 실패: {file.Path}");
        }

        var committed = new List<(string Target, string Backup, bool HadOriginal)>();
        try
        {
            foreach (ClientUpdateFile file in changed)
            {
                string relativePath = NormalizeRelativePath(file.Path);
                string sourcePath = ResolveInsideRoot(tempRoot, relativePath);
                string targetPath = ResolveInsideRoot(installRoot, relativePath);
                string backupPath = ResolveInsideRoot(backupRoot, relativePath);
                bool hadOriginal = File.Exists(targetPath);
                Directory.CreateDirectory(Path.GetDirectoryName(targetPath)!);
                if (hadOriginal)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(backupPath)!);
                    File.Move(targetPath, backupPath, true);
                }
                committed.Add((targetPath, backupPath, hadOriginal));
                File.Move(sourcePath, targetPath, true);
            }
            foreach (string relativePath in stalePaths)
            {
                string targetPath = ResolveInsideRoot(installRoot, relativePath);
                if (!File.Exists(targetPath)) continue;
                string backupPath = ResolveInsideRoot(backupRoot, relativePath);
                Directory.CreateDirectory(Path.GetDirectoryName(backupPath)!);
                File.Move(targetPath, backupPath, true);
                committed.Add((targetPath, backupPath, true));
            }
        }
        catch
        {
            foreach (var item in committed.AsEnumerable().Reverse())
            {
                if (File.Exists(item.Target)) File.Delete(item.Target);
                if (item.HadOriginal && File.Exists(item.Backup))
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(item.Target)!);
                    File.Move(item.Backup, item.Target, true);
                }
            }
            throw;
        }
        finally
        {
            if (Directory.Exists(tempRoot)) Directory.Delete(tempRoot, true);
        }
        if (Directory.Exists(backupRoot)) Directory.Delete(backupRoot, true);
    }

    private async Task<List<ClientUpdateFile>> FindChangedFilesAsync(ClientUpdateManifest manifest)
    {
        ValidateManifest(manifest);
        var result = new List<ClientUpdateFile>();
        int checkedCount = 0;
        foreach (ClientUpdateFile file in manifest.Files)
        {
            lifetime.Token.ThrowIfCancellationRequested();
            string localPath = ResolveInsideRoot(installRoot, NormalizeRelativePath(file.Path));
            var info = new FileInfo(localPath);
            bool matches = info.Exists && info.Length == file.Size;
            if (matches)
            {
                string hash = await ComputeSha256Async(localPath, lifetime.Token);
                matches = string.Equals(hash, file.Sha256, StringComparison.OrdinalIgnoreCase);
            }
            if (!matches) result.Add(file);
            checkedCount++;
            progress.Value = Math.Clamp((int)((long)checkedCount * 100 / Math.Max(1, manifest.Files.Count)), 0, 100);
            status.Text = $"설치 파일 검사 중... {checkedCount}/{manifest.Files.Count}";
            await Task.Yield();
        }
        return result;
    }

    private async Task<ClientUpdateManifest> FetchManifestAsync(string baseUrl)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        HttpResponseMessage response;
        try
        {
            response = await http.GetAsync(
                $"{baseUrl}/api/client-updates/manifest", lifetime.Token);
        }
        catch (TaskCanceledException) when (!lifetime.IsCancellationRequested)
        {
            throw new TimeoutException("업데이트 서버가 5초 안에 응답하지 않아 연결 시도를 중단했습니다.");
        }
        using (response)
        {
        string json = await response.Content.ReadAsStringAsync(lifetime.Token);
        if (!response.IsSuccessStatusCode)
            throw new InvalidOperationException($"업데이트 서버 HTTP {(int)response.StatusCode}: {json}");
        ClientUpdateManifest? manifest = JsonSerializer.Deserialize<ClientUpdateManifest>(json, JsonOptions);
        if (manifest is null) throw new InvalidDataException("업데이트 목록을 읽지 못했습니다.");
        ValidateManifest(manifest);
        return manifest;
        }
    }

    private bool HasValidSavedServerAddress()
    {
        string address = serverAddress.Text.Trim();
        return IPAddress.TryParse(address, out IPAddress? parsed) &&
            parsed.AddressFamily == AddressFamily.InterNetwork && IsSupportedAddress(parsed);
    }

    private async Task<ClientUpdateManifest?> LoadLocalManifestAsync()
    {
        string path = Path.Combine(installRoot, LocalManifestFileName);
        if (!File.Exists(path)) return null;
        try
        {
            string json = await File.ReadAllTextAsync(path, lifetime.Token);
            return JsonSerializer.Deserialize<ClientUpdateManifest>(json, JsonOptions);
        }
        catch
        {
            Append("[주의] 기존 설치 기록을 읽지 못해 전체 파일을 다시 검사합니다.");
            return null;
        }
    }

    private string GetBackendBaseUrl()
    {
        string address = serverAddress.Text.Trim();
        if (!IPAddress.TryParse(address, out IPAddress? parsed) ||
            parsed.AddressFamily != AddressFamily.InterNetwork || !IsSupportedAddress(parsed))
            throw new InvalidOperationException("서버 PC의 올바른 25.x, 사설 또는 로컬 IPv4 주소를 입력하세요.");
        return $"http://{address}:5080";
    }

    private static bool IsSupportedAddress(IPAddress address)
    {
        byte[] bytes = address.GetAddressBytes();
        return bytes[0] is 10 or 25 or 127 ||
            (bytes[0] == 172 && bytes[1] is >= 16 and <= 31) ||
            (bytes[0] == 192 && bytes[1] == 168);
    }

    private static void ValidateManifest(ClientUpdateManifest manifest)
    {
        if (string.IsNullOrWhiteSpace(manifest.ReleaseId) ||
            string.IsNullOrWhiteSpace(manifest.ClientVersion) ||
            string.IsNullOrWhiteSpace(manifest.ExecutablePath) ||
            manifest.Files is null || manifest.Files.Count == 0 || manifest.Files.Count > 20000)
            throw new InvalidDataException("업데이트 목록의 필수 정보가 올바르지 않습니다.");
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        long totalSize = 0;
        foreach (ClientUpdateFile file in manifest.Files)
        {
            string path = NormalizeRelativePath(file.Path);
            if (!paths.Add(path) || file.Size < 0 ||
                !Regex.IsMatch(file.Sha256 ?? string.Empty, "^[A-Fa-f0-9]{64}$"))
                throw new InvalidDataException($"업데이트 파일 정보가 올바르지 않습니다: {file.Path}");
            if (file.Size > 100L * 1024 * 1024 * 1024 - totalSize)
                throw new InvalidDataException("업데이트 전체 크기가 허용 범위를 초과했습니다.");
            totalSize += file.Size;
        }
        string executablePath = NormalizeRelativePath(manifest.ExecutablePath);
        if (!executablePath.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) || !paths.Contains(executablePath))
            throw new InvalidDataException("게임 실행 파일이 업데이트 목록에 포함되어 있지 않습니다.");
    }

    private static string NormalizeRelativePath(string path)
    {
        string normalized = (path ?? string.Empty).Replace('/', Path.DirectorySeparatorChar)
            .TrimStart(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        if (normalized.Length == 0 || Path.IsPathRooted(normalized) ||
            normalized.Split(Path.DirectorySeparatorChar).Any(part => part is "" or "." or ".."))
            throw new InvalidDataException("안전하지 않은 업데이트 파일 경로입니다.");
        return normalized;
    }

    private static string ResolveInsideRoot(string root, string relativePath)
    {
        string fullRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) + Path.DirectorySeparatorChar;
        string fullPath = Path.GetFullPath(Path.Combine(root, relativePath));
        if (!fullPath.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("업데이트 경로가 설치 폴더를 벗어났습니다.");
        return fullPath;
    }

    private static async Task<string> ComputeSha256Async(string path, CancellationToken cancellationToken)
    {
        await using var stream = new FileStream(
            path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 128, true);
        byte[] hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    private void EnsureGameIsNotRunning(string executablePath)
    {
        string processName = Path.GetFileNameWithoutExtension(executablePath);
        if (Process.GetProcessesByName(processName).Any())
            throw new InvalidOperationException("게임이 실행 중입니다. 게임을 완전히 종료한 뒤 업데이트하세요.");
    }

    private void LaunchGame(string? executablePath = null)
    {
        try
        {
            SaveSettings();
            string? resolvedExecutable = executablePath;
            if (string.IsNullOrWhiteSpace(resolvedExecutable))
            {
                ClientUpdateManifest? local = File.Exists(Path.Combine(installRoot, LocalManifestFileName))
                    ? JsonSerializer.Deserialize<ClientUpdateManifest>(
                        File.ReadAllText(Path.Combine(installRoot, LocalManifestFileName)), JsonOptions)
                    : null;
                resolvedExecutable = local?.ExecutablePath ?? Directory
                    .EnumerateFiles(installRoot, "ProjectProject01.exe", SearchOption.AllDirectories)
                    .FirstOrDefault();
            }
            if (string.IsNullOrWhiteSpace(resolvedExecutable))
                throw new FileNotFoundException("ProjectProject01.exe를 찾지 못했습니다. 먼저 업데이트를 실행하세요.");
            string gamePath = ResolveInsideRoot(installRoot, NormalizeRelativePath(resolvedExecutable));
            if (!File.Exists(gamePath)) throw new FileNotFoundException("게임 실행 파일이 없습니다.", gamePath);
            string address = serverAddress.Text.Trim();
            GetBackendBaseUrl();
            Process.Start(new ProcessStartInfo(gamePath)
            {
                WorkingDirectory = Path.GetDirectoryName(gamePath)!,
                Arguments = $"-ProjectProject01Server={address}",
                UseShellExecute = true
            });
            Close();
        }
        catch (Exception exception)
        {
            Append("[실패] " + exception.Message);
            SetStatus("게임을 실행하지 못했습니다.");
        }
    }

    private void LoadSettings()
    {
        try
        {
            if (!File.Exists(settingsPath)) return;
            LauncherSettings? settings = JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllText(settingsPath), JsonOptions);
            if (!string.IsNullOrWhiteSpace(settings?.ServerAddress)) serverAddress.Text = settings.ServerAddress;
        }
        catch { }
    }

    private void SaveSettings()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(settingsPath)!);
        File.WriteAllText(settingsPath,
            JsonSerializer.Serialize(new LauncherSettings(serverAddress.Text.Trim()), JsonOptions));
    }

    private void SetProgress(long completed, long total, string message)
    {
        progress.Value = Math.Clamp((int)(completed * 100 / Math.Max(1, total)), 0, 100);
        status.Text = $"{message} ({progress.Value}%)";
        Application.DoEvents();
    }

    private void Append(string message)
    {
        log.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
        log.SelectionStart = log.TextLength;
        log.ScrollToCaret();
    }

    private void SetStatus(string message) => status.Text = message;

    private static string FormatBytes(long bytes) => bytes switch
    {
        >= 1024L * 1024 * 1024 => $"{bytes / (1024d * 1024 * 1024):0.00} GB",
        >= 1024L * 1024 => $"{bytes / (1024d * 1024):0.00} MB",
        >= 1024L => $"{bytes / 1024d:0.00} KB",
        _ => $"{bytes} B"
    };
}

internal sealed record LauncherSettings(string ServerAddress);
internal sealed record ClientUpdateFile(string Path, long Size, string Sha256);
internal sealed record ClientUpdateManifest(
    string ReleaseId,
    string ClientVersion,
    DateTime PublishedAtUtc,
    string ExecutablePath,
    List<ClientUpdateFile> Files);
