using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace ProjectProject01ClientLauncher;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        string installIdentity = Path.GetFullPath(AppContext.BaseDirectory).ToUpperInvariant();
        string identityHash = Convert.ToHexString(
            SHA256.HashData(Encoding.UTF8.GetBytes(installIdentity)))[..24];
        using var singleInstance = new Mutex(
            false, $"Local\\ProjectProject01ClientLauncher_{identityHash}");
        bool ownsMutex;
        try
        {
            ownsMutex = singleInstance.WaitOne(0);
        }
        catch (AbandonedMutexException)
        {
            ownsMutex = true;
        }
        if (!ownsMutex)
        {
            MessageBox.Show(
                "이 설치 폴더의 업데이트 런처가 이미 실행 중입니다.",
                "ProjectProject01 업데이트 런처",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
            return;
        }

        ApplicationConfiguration.Initialize();
        try
        {
            Application.Run(new LauncherForm());
        }
        finally
        {
            singleInstance.ReleaseMutex();
        }
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
    private readonly Button pauseButton = new() { Text = "다운로드 중단", AutoSize = true, Enabled=false };
    private readonly CancellationTokenSource lifetime = new();
    private CancellationTokenSource? operationCancellation;
    private CancellationToken OperationToken=>operationCancellation?.Token??lifetime.Token;
    private bool closeRequested;
    private readonly string installRoot;
    private readonly string settingsPath;
    private bool busy;

    public LauncherForm(string? installationRoot=null)
    {
        installRoot=Path.GetFullPath(installationRoot??AppContext.BaseDirectory);
        Text = "ProjectProject01 업데이트 런처";
        Width = 760;
        Height = 500;
        MinimumSize = new Size(640, 420);
        StartPosition = FormStartPosition.CenterScreen;
        settingsPath = installationRoot is not null?Path.Combine(installRoot,"launcher-test-settings.json"):Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ProjectProject01", "ClientLauncher.json");
        BuildUi();
        LoadSettings();
        FormClosing += (_, e) =>
        {
            if(busy){e.Cancel=true;closeRequested=true;}
            lifetime.Cancel();
        };
        pauseButton.Click+=(_,_)=>{pauseButton.Enabled=false;operationCancellation?.Cancel();};
        checkButton.Click += async (_, _) => await RunBusyAsync(CheckForUpdatesAsync);
        updateButton.Click += async (_, _) => await RunBusyAsync(() => UpdateAsync(true));
        launchButton.Click += (_, _) => LaunchGame();
        Shown += async (_, _) =>
        {
            if(installationRoot is not null)return;
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
        buttons.Controls.Add(pauseButton);
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
        operationCancellation=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        checkButton.Enabled = updateButton.Enabled = launchButton.Enabled = false;
        try
        {
            SaveSettings();
            await action();
        }
        catch (OperationCanceledException) when (OperationToken.IsCancellationRequested)
        {
            SetStatus("다운로드를 중단했습니다. 업데이트를 다시 누르면 보관한 부분부터 이어받습니다.");
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
            pauseButton.Enabled=false;
            operationCancellation?.Dispose();operationCancellation=null;
            checkButton.Enabled = updateButton.Enabled = launchButton.Enabled = true;
            if(closeRequested&&!IsDisposed)BeginInvoke(new Action(Close));
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
        List<ClientFileUpdatePlan> plans = await BuildUpdatePlansAsync(remote);
        long bytes = plans.Sum(plan => plan.DownloadBytes);
        Append(plans.Count == 0
            ? $"[OK] 최신 상태입니다. 게시 ID: {remote.ReleaseId}"
            : $"[정보] 변경 파일 {plans.Count}개를 확인했습니다. 실제 다운로드: {FormatBytes(bytes)}");
        SetStatus(plans.Count == 0 ? "현재 클라이언트가 최신 상태입니다." : "증분 업데이트가 있습니다.");
    }

    private async Task UpdateAsync(bool launchAfterUpdate)
    {
        string baseUrl = GetBackendBaseUrl();
        ClientUpdateManifest remote = await FetchManifestAsync(baseUrl);
        ValidateManifest(remote);
        EnsureGameIsNotRunning(remote.ExecutablePath);
        ClientUpdateManifest? local = await LoadLocalManifestAsync();
        List<ClientFileUpdatePlan> plans = await BuildUpdatePlansAsync(remote);
        HashSet<string> remotePaths = remote.Files.Select(file => NormalizeRelativePath(file.Path))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        List<string> stalePaths = local?.Files
            .Select(file => NormalizeRelativePath(file.Path))
            .Where(path => !remotePaths.Contains(path))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList() ?? [];

        if (plans.Count > 0 || stalePaths.Count > 0)
        {
            await DownloadAndCommitAsync(baseUrl, remote, plans, stalePaths);
        }
        await WriteLocalManifestAsync(remote);
        currentVersion.Text = $"설치 버전: {remote.ClientVersion} ({remote.ReleaseId})";
        latestVersion.Text = $"서버 버전: {remote.ClientVersion} ({remote.ReleaseId})";
        progress.Value = 100;
        Append("[OK] 클라이언트 업데이트와 파일 검증이 완료되었습니다.");
        SetStatus("최신 버전입니다.");
        if (launchAfterUpdate) LaunchGame(remote.ExecutablePath);
    }

    private async Task DownloadAndCommitAsync(
        string baseUrl,ClientUpdateManifest manifest,IReadOnlyList<ClientFileUpdatePlan> plans,IReadOnlyList<string> stalePaths)
    {
        string operationId=Guid.NewGuid().ToString("N");
        string tempRoot=Path.Combine(installRoot,".update-temp",operationId);
        string backupRoot=Path.Combine(installRoot,".update-backup",operationId);
        Directory.CreateDirectory(tempRoot);
        long totalBytes=Math.Max(1,plans.Sum(plan=>plan.DownloadBytes)),downloadedBytes=0;
        var completedCache=new List<(string Url,ClientUpdateFile File)>();
        var downloader=new ResumableUpdateDownloader(Path.Combine(installRoot,".update-cache"),bytes=>
        {
            downloadedBytes=Math.Max(0,downloadedBytes+bytes);
            SetProgress(downloadedBytes,totalBytes,"다운로드/이어받기 중");
        },Append);
        using var http=new HttpClient {Timeout=Timeout.InfiniteTimeSpan};
        try
        {
            pauseButton.Enabled=plans.Count>0;
            foreach(var plan in plans)
            {
                OperationToken.ThrowIfCancellationRequested();
                string relative=NormalizeRelativePath(plan.File.Path),tempPath=ResolveInsideRoot(tempRoot,relative);
                string uriPath=string.Join('/',relative.Split(Path.DirectorySeparatorChar).Select(Uri.EscapeDataString));
                string url=$"{baseUrl}/api/client-updates/files/{uriPath}?release={Uri.EscapeDataString(manifest.ReleaseId)}";
                Append("[다운로드] "+plan.File.Path);
                await downloader.DownloadAsync(http,url,plan.File,tempPath,ResolveInsideRoot(installRoot,relative),plan.ChangedBlocks,OperationToken);
                string hash=await ComputeSha256Async(tempPath,OperationToken);
                if(!hash.Equals(plan.File.Sha256,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("다운로드 파일 검증 실패: "+plan.File.Path);
                completedCache.Add((url,plan.File));
            }
            OperationToken.ThrowIfCancellationRequested();
            EnsureGameIsNotRunning(manifest.ExecutablePath);
            // Once replacement begins, cancellation cannot leave a half-installed client.
            pauseButton.Enabled=false;SetStatus("다운로드 검증 완료 — 안전하게 적용 중...");
            var committed=new List<(string Target,string Backup,bool HadOriginal)>();
            try
            {
                foreach(var plan in plans)
                {
                    string relative=NormalizeRelativePath(plan.File.Path);
                    string source=ResolveInsideRoot(tempRoot,relative),target=ResolveInsideRoot(installRoot,relative),backup=ResolveInsideRoot(backupRoot,relative);
                    bool existed=File.Exists(target);Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                    if(existed){Directory.CreateDirectory(Path.GetDirectoryName(backup)!);File.Move(target,backup,true);}
                    committed.Add((target,backup,existed));File.Move(source,target,true);
                }
                foreach(string relative in stalePaths)
                {
                    string target=ResolveInsideRoot(installRoot,relative);
                    if(!File.Exists(target))continue;
                    string backup=ResolveInsideRoot(backupRoot,relative);Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                    File.Move(target,backup,true);committed.Add((target,backup,true));
                }
            }
            catch
            {
                foreach(var item in committed.AsEnumerable().Reverse())
                {
                    if(File.Exists(item.Target))File.Delete(item.Target);
                    if(item.HadOriginal&&File.Exists(item.Backup))
                    {Directory.CreateDirectory(Path.GetDirectoryName(item.Target)!);File.Move(item.Backup,item.Target,true);}
                }
                throw;
            }
            TryDeleteDirectory(backupRoot,"업데이트 백업 폴더");
            foreach(var item in completedCache)
            {
                try{downloader.Forget(item.Url,item.File);}
                catch(Exception ex) when(ex is IOException or UnauthorizedAccessException){Append("[주의] 적용된 다운로드 캐시 정리 실패: "+ex.Message);}
            }
        }
        finally
        {
            pauseButton.Enabled=false;
            // Interrupted downloads live in .update-cache, never in the install/rollback staging area.
            TryDeleteDirectory(tempRoot,"임시 업데이트 폴더");
        }
    }

    private async Task<List<ClientFileUpdatePlan>> BuildUpdatePlansAsync(ClientUpdateManifest manifest)
    {
        ValidateManifest(manifest);
        var result = new List<ClientFileUpdatePlan>();
        int checkedCount = 0;
        foreach (ClientUpdateFile file in manifest.Files)
        {
            OperationToken.ThrowIfCancellationRequested();
            string relativePath = NormalizeRelativePath(file.Path);
            string localPath = ResolveInsideRoot(installRoot, relativePath);
            var info = new FileInfo(localPath);
            bool matches = false;
            if (info.Exists && info.Length == file.Size)
            {
                // 업데이트 기록만 신뢰하지 않고 실제 설치 파일을 매번 SHA-256으로 확인한다.
                // 같은 크기의 손상·변조 파일도 반드시 다시 내려받는다.
                string hash = await ComputeSha256Async(localPath, OperationToken);
                matches = string.Equals(hash, file.Sha256, StringComparison.OrdinalIgnoreCase);
            }

            if (!matches)
            {
                List<ClientUpdateBlock>? changedBlocks = null;
                long downloadBytes = file.Size;
                if (HasValidBlockMap(file))
                {
                    changedBlocks = [];
                    foreach (ClientUpdateBlock block in file.Blocks!)
                    {
                        bool blockMatches = info.Exists && info.Length >= block.Offset + block.Size &&
                            await DoesLocalBlockMatchAsync(localPath, block);
                        if (!blockMatches) changedBlocks.Add(block);
                    }
                    downloadBytes = changedBlocks.Sum(block => (long)block.Size);
                }
                result.Add(new ClientFileUpdatePlan(file, changedBlocks, downloadBytes));
            }
            checkedCount++;
            progress.Value = Math.Clamp((int)((long)checkedCount * 100 / Math.Max(1, manifest.Files.Count)), 0, 100);
            status.Text = $"설치 파일 검사 중... {checkedCount}/{manifest.Files.Count}";
            await Task.Yield();
        }
        return result;
    }

    private static bool HasValidBlockMap(ClientUpdateFile file) =>
        file.BlockSize is > 0 && file.Blocks is { Count: > 0 };

    private async Task<bool> DoesLocalBlockMatchAsync(string path, ClientUpdateBlock block)
    {
        try
        {
            string hash = await ComputeRangeSha256Async(path, block.Offset, block.Size, OperationToken);
            return string.Equals(hash, block.Sha256, StringComparison.OrdinalIgnoreCase);
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }


    private static async Task<string> ComputeRangeSha256Async(
        string path,
        long offset,
        int count,
        CancellationToken cancellationToken)
    {
        await using var stream = new FileStream(
            path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 128,
            FileOptions.Asynchronous | FileOptions.RandomAccess);
        stream.Position = offset;
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer = new byte[1024 * 128];
        int remaining = count;
        while (remaining > 0)
        {
            int read = await stream.ReadAsync(
                buffer.AsMemory(0, Math.Min(buffer.Length, remaining)), cancellationToken);
            if (read == 0) throw new EndOfStreamException("설치 파일이 예상보다 짧습니다.");
            hash.AppendData(buffer, 0, read);
            remaining -= read;
        }
        return Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private async Task<ClientUpdateManifest> FetchManifestAsync(string baseUrl)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        HttpResponseMessage response;
        try
        {
            response = await http.GetAsync(
                $"{baseUrl}/api/client-updates/manifest", OperationToken);
        }
        catch (TaskCanceledException) when (!lifetime.IsCancellationRequested)
        {
            throw new TimeoutException("업데이트 서버가 5초 안에 응답하지 않아 연결 시도를 중단했습니다.");
        }
        using (response)
        {
        string json = await response.Content.ReadAsStringAsync(OperationToken);
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
            string json = await File.ReadAllTextAsync(path, OperationToken);
            ClientUpdateManifest? manifest = JsonSerializer.Deserialize<ClientUpdateManifest>(json, JsonOptions);
            if (manifest is null) return null;
            ValidateManifest(manifest);
            return manifest;
        }
        catch
        {
            Append("[주의] 기존 설치 기록을 읽지 못해 전체 파일을 다시 검사합니다.");
            return null;
        }
    }

    private async Task WriteLocalManifestAsync(ClientUpdateManifest manifest)
    {
        string path = Path.Combine(installRoot, LocalManifestFileName);
        string temporaryPath = path + $".new-{Guid.NewGuid():N}";
        try
        {
            string json = JsonSerializer.Serialize(manifest, JsonOptions);
            await File.WriteAllTextAsync(temporaryPath, json, OperationToken);
            File.Move(temporaryPath, path, true);
        }
        finally
        {
            try
            {
                if (File.Exists(temporaryPath)) File.Delete(temporaryPath);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
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
            string first=path.Split(Path.DirectorySeparatorChar)[0];
            if(first.Equals(".update-cache",StringComparison.OrdinalIgnoreCase)||first.Equals(".update-temp",StringComparison.OrdinalIgnoreCase)||first.Equals(".update-backup",StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("런처 전용 작업 폴더는 업데이트 대상이 될 수 없습니다.");
            if (!paths.Add(path) || file.Size < 0 ||
                !Regex.IsMatch(file.Sha256 ?? string.Empty, "^[A-Fa-f0-9]{64}$"))
                throw new InvalidDataException($"업데이트 파일 정보가 올바르지 않습니다: {file.Path}");
            ValidateBlockMap(file);
            if (file.Size > 100L * 1024 * 1024 * 1024 - totalSize)
                throw new InvalidDataException("업데이트 전체 크기가 허용 범위를 초과했습니다.");
            totalSize += file.Size;
        }
        string executablePath = NormalizeRelativePath(manifest.ExecutablePath);
        if (!executablePath.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) || !paths.Contains(executablePath))
            throw new InvalidDataException("게임 실행 파일이 업데이트 목록에 포함되어 있지 않습니다.");
    }

    private static void ValidateBlockMap(ClientUpdateFile file)
    {
        bool hasBlockSize = file.BlockSize.HasValue;
        bool hasBlocks = file.Blocks is not null;
        if (!hasBlockSize && !hasBlocks) return; // 구형 매니페스트: 전체 파일 다운로드
        if (!hasBlockSize || !hasBlocks || file.BlockSize is < 64 * 1024 or > 64 * 1024 * 1024)
            throw new InvalidDataException($"업데이트 블록 정보가 올바르지 않습니다: {file.Path}");
        if (file.Size == 0)
        {
            if (file.Blocks!.Count != 0)
                throw new InvalidDataException($"빈 파일에 블록 정보가 있습니다: {file.Path}");
            return;
        }
        List<ClientUpdateBlock> blocks = file.Blocks!;
        int blockSize = file.BlockSize!.Value;
        if (blocks.Count == 0 || blocks.Count > 2_000_000)
            throw new InvalidDataException($"업데이트 블록 목록이 올바르지 않습니다: {file.Path}");

        long expectedOffset = 0;
        foreach (ClientUpdateBlock block in blocks)
        {
            if (block.Offset != expectedOffset || block.Size <= 0 || block.Size > blockSize ||
                !Regex.IsMatch(block.Sha256 ?? string.Empty, "^[A-Fa-f0-9]{64}$") ||
                block.Size > file.Size - expectedOffset)
                throw new InvalidDataException($"업데이트 블록 범위가 올바르지 않습니다: {file.Path}");
            expectedOffset += block.Size;
        }
        if (expectedOffset != file.Size)
            throw new InvalidDataException($"업데이트 블록 전체 크기가 파일과 다릅니다: {file.Path}");
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
        foreach(string name in new[]{processName,"ProjectProject01-Win64-Shipping"}.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var processes=Process.GetProcessesByName(name);
            try{if(processes.Length>0)throw new InvalidOperationException("게임이 실행 중입니다. 게임을 완전히 종료한 뒤 업데이트하세요.");}
            finally{foreach(var process in processes)process.Dispose();}
        }
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

    private void TryDeleteDirectory(string path, string description)
    {
        for (int attempt = 1; attempt <= 3; attempt++)
        {
            try
            {
                if (Directory.Exists(path)) Directory.Delete(path, true);
                return;
            }
            catch (IOException) when (attempt < 3)
            {
                Thread.Sleep(100);
            }
            catch (UnauthorizedAccessException) when (attempt < 3)
            {
                Thread.Sleep(100);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                Append($"[주의] {description} 정리를 나중에 다시 시도해야 합니다: {exception.Message}");
                return;
            }
        }
    }

    private static string FormatBytes(long bytes) => bytes switch
    {
        >= 1024L * 1024 * 1024 => $"{bytes / (1024d * 1024 * 1024):0.00} GB",
        >= 1024L * 1024 => $"{bytes / (1024d * 1024):0.00} MB",
        >= 1024L => $"{bytes / 1024d:0.00} KB",
        _ => $"{bytes} B"
    };
}

internal sealed record LauncherSettings(string ServerAddress);
internal sealed record ClientUpdateBlock(long Offset, int Size, string Sha256);
internal sealed record ClientUpdateFile(
    string Path,
    long Size,
    string Sha256,
    int? BlockSize = null,
    List<ClientUpdateBlock>? Blocks = null);
internal sealed record ClientFileUpdatePlan(
    ClientUpdateFile File,
    List<ClientUpdateBlock>? ChangedBlocks,
    long DownloadBytes);
internal sealed record ClientUpdateManifest(
    string ReleaseId,
    string ClientVersion,
    DateTime PublishedAtUtc,
    string ExecutablePath,
    List<ClientUpdateFile> Files);
