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
        string baseUrl,
        ClientUpdateManifest manifest,
        IReadOnlyList<ClientFileUpdatePlan> plans,
        IReadOnlyList<string> stalePaths)
    {
        string safeReleaseId = Regex.Replace(manifest.ReleaseId, "[^A-Za-z0-9._-]", "_");
        string operationId = $"{safeReleaseId}-{Guid.NewGuid():N}";
        string tempRoot = Path.Combine(installRoot, ".update-temp", operationId);
        string backupRoot = Path.Combine(installRoot, ".update-backup", operationId);
        Directory.CreateDirectory(tempRoot);
        long totalBytes = Math.Max(1, plans.Sum(plan => plan.DownloadBytes));
        long downloadedBytes = 0;
        using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(30) };

        foreach (ClientFileUpdatePlan plan in plans)
        {
            lifetime.Token.ThrowIfCancellationRequested();
            ClientUpdateFile file = plan.File;
            string relativePath = NormalizeRelativePath(file.Path);
            string tempPath = ResolveInsideRoot(tempRoot, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(tempPath)!);
            string uriPath = string.Join('/', relativePath.Split(Path.DirectorySeparatorChar)
                .Select(Uri.EscapeDataString));
            string fileUrl = $"{baseUrl}/api/client-updates/files/{uriPath}" +
                $"?release={Uri.EscapeDataString(manifest.ReleaseId)}";
            if (plan.ChangedBlocks is not null)
            {
                downloadedBytes = await BuildPatchedFileAsync(
                    http, fileUrl, file, plan.ChangedBlocks, tempPath,
                    downloadedBytes, totalBytes);
            }
            else
            {
                downloadedBytes = await DownloadFullFileAsync(
                    http, fileUrl, file, tempPath, downloadedBytes, totalBytes);
            }

            string hash = await ComputeSha256Async(tempPath, lifetime.Token);
            if (!string.Equals(hash, file.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"다운로드 파일 검증 실패: {file.Path}");
        }

        var committed = new List<(string Target, string Backup, bool HadOriginal)>();
        try
        {
            foreach (ClientFileUpdatePlan plan in plans)
            {
                ClientUpdateFile file = plan.File;
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
            TryDeleteDirectory(tempRoot, "임시 업데이트 폴더");
        }
        TryDeleteDirectory(backupRoot, "업데이트 백업 폴더");
    }

    private async Task<List<ClientFileUpdatePlan>> BuildUpdatePlansAsync(ClientUpdateManifest manifest)
    {
        ValidateManifest(manifest);
        var result = new List<ClientFileUpdatePlan>();
        int checkedCount = 0;
        foreach (ClientUpdateFile file in manifest.Files)
        {
            lifetime.Token.ThrowIfCancellationRequested();
            string relativePath = NormalizeRelativePath(file.Path);
            string localPath = ResolveInsideRoot(installRoot, relativePath);
            var info = new FileInfo(localPath);
            bool matches = false;
            if (info.Exists && info.Length == file.Size)
            {
                // 업데이트 기록만 신뢰하지 않고 실제 설치 파일을 매번 SHA-256으로 확인한다.
                // 같은 크기의 손상·변조 파일도 반드시 다시 내려받는다.
                string hash = await ComputeSha256Async(localPath, lifetime.Token);
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
            string hash = await ComputeRangeSha256Async(path, block.Offset, block.Size, lifetime.Token);
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

    private async Task<long> DownloadFullFileAsync(
        HttpClient http,
        string fileUrl,
        ClientUpdateFile file,
        string tempPath,
        long downloadedBytes,
        long totalBytes)
    {
        using HttpResponseMessage response = await http.GetAsync(
            fileUrl,
            HttpCompletionOption.ResponseHeadersRead,
            lifetime.Token);
        response.EnsureSuccessStatusCode();
        await using Stream input = await response.Content.ReadAsStreamAsync(lifetime.Token);
        await using var output = new FileStream(
            tempPath, FileMode.Create, FileAccess.Write, FileShare.None, 1024 * 128, true);
        byte[] buffer = new byte[1024 * 128];
        long received = 0;
        while (true)
        {
            int read = await input.ReadAsync(buffer.AsMemory(0, buffer.Length), lifetime.Token);
            if (read == 0) break;
            received += read;
            if (received > file.Size)
                throw new InvalidDataException($"다운로드 크기가 목록보다 큽니다: {file.Path}");
            await output.WriteAsync(buffer.AsMemory(0, read), lifetime.Token);
            downloadedBytes += read;
            SetProgress(downloadedBytes, totalBytes, $"전체 파일 다운로드 중: {file.Path}");
        }
        if (received != file.Size)
            throw new InvalidDataException($"다운로드 크기가 목록과 다릅니다: {file.Path}");
        await output.FlushAsync(lifetime.Token);
        return downloadedBytes;
    }

    private async Task<long> BuildPatchedFileAsync(
        HttpClient http,
        string fileUrl,
        ClientUpdateFile file,
        IReadOnlyList<ClientUpdateBlock> changedBlocks,
        string tempPath,
        long downloadedBytes,
        long totalBytes)
    {
        string relativePath = NormalizeRelativePath(file.Path);
        string localPath = ResolveInsideRoot(installRoot, relativePath);
        IReadOnlyList<ClientUpdateBlock> blocks = file.Blocks ??
            throw new InvalidDataException($"업데이트 블록 목록이 없습니다: {file.Path}");
        var changedOffsets = changedBlocks.Select(block => block.Offset).ToHashSet();
        bool needsLocalSource = blocks.Any(block => !changedOffsets.Contains(block.Offset));
        FileStream? local = null;
        try
        {
            if (needsLocalSource)
            {
                local = new FileStream(
                    localPath, FileMode.Open, FileAccess.Read, FileShare.Read,
                    1024 * 128, FileOptions.Asynchronous | FileOptions.RandomAccess);
            }
            await using var output = new FileStream(
                tempPath, FileMode.Create, FileAccess.Write, FileShare.None, 1024 * 128, true);
            foreach (ClientUpdateBlock block in blocks)
            {
                lifetime.Token.ThrowIfCancellationRequested();
                if (!changedOffsets.Contains(block.Offset))
                {
                    if (local is null)
                        throw new InvalidDataException($"기존 파일의 재사용 블록을 읽을 수 없습니다: {file.Path}");
                    await CopyExactRangeAsync(local, output, block.Offset, block.Size, lifetime.Token);
                    continue;
                }

                using var request = new HttpRequestMessage(
                    HttpMethod.Get, fileUrl);
                request.Headers.Range = new RangeHeaderValue(block.Offset, block.Offset + block.Size - 1);
                using HttpResponseMessage response = await http.SendAsync(
                    request, HttpCompletionOption.ResponseHeadersRead, lifetime.Token);

                if (response.StatusCode != HttpStatusCode.PartialContent)
                {
                    // 구형 프록시나 서버가 Range를 지원하지 않으면 검증된 기존 전체 파일 방식으로 전환한다.
                    await output.DisposeAsync();
                    local?.Dispose();
                    local = null;
                    Append($"[안내] 부분 다운로드를 지원하지 않아 전체 파일로 전환합니다: {file.Path}");
                    return await DownloadFullFileAsync(
                        http, fileUrl, file, tempPath, downloadedBytes, totalBytes);
                }

                ContentRangeHeaderValue? range = response.Content.Headers.ContentRange;
                if (range is not null &&
                    (range.From != block.Offset || range.To != block.Offset + block.Size - 1))
                    throw new InvalidDataException($"서버가 잘못된 파일 범위를 반환했습니다: {file.Path}");
                if (response.Content.Headers.ContentLength is long contentLength && contentLength != block.Size)
                    throw new InvalidDataException($"부분 다운로드 크기가 올바르지 않습니다: {file.Path}");

                await using Stream input = await response.Content.ReadAsStreamAsync(lifetime.Token);
                using var blockHash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
                byte[] buffer = new byte[1024 * 128];
                int remaining = block.Size;
                while (remaining > 0)
                {
                    int read = await input.ReadAsync(
                        buffer.AsMemory(0, Math.Min(buffer.Length, remaining)), lifetime.Token);
                    if (read == 0)
                        throw new EndOfStreamException($"부분 다운로드가 일찍 종료되었습니다: {file.Path}");
                    blockHash.AppendData(buffer, 0, read);
                    await output.WriteAsync(buffer.AsMemory(0, read), lifetime.Token);
                    remaining -= read;
                    downloadedBytes += read;
                    SetProgress(downloadedBytes, totalBytes, $"변경 블록 다운로드 중: {file.Path}");
                }
                if (await input.ReadAsync(buffer.AsMemory(0, 1), lifetime.Token) != 0)
                    throw new InvalidDataException($"부분 다운로드 크기가 목록보다 큽니다: {file.Path}");
                string actualBlockHash = Convert.ToHexString(blockHash.GetHashAndReset()).ToLowerInvariant();
                if (!string.Equals(actualBlockHash, block.Sha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException($"부분 다운로드 검증 실패: {file.Path} ({block.Offset})");
            }
            await output.FlushAsync(lifetime.Token);
        }
        finally
        {
            if (local is not null) await local.DisposeAsync();
        }
        return downloadedBytes;
    }

    private static async Task CopyExactRangeAsync(
        FileStream input,
        FileStream output,
        long offset,
        int count,
        CancellationToken cancellationToken)
    {
        input.Position = offset;
        byte[] buffer = new byte[1024 * 128];
        int remaining = count;
        while (remaining > 0)
        {
            int read = await input.ReadAsync(
                buffer.AsMemory(0, Math.Min(buffer.Length, remaining)), cancellationToken);
            if (read == 0) throw new EndOfStreamException("기존 설치 파일이 예상보다 짧습니다.");
            await output.WriteAsync(buffer.AsMemory(0, read), cancellationToken);
            remaining -= read;
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
            await File.WriteAllTextAsync(temporaryPath, json, lifetime.Token);
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
