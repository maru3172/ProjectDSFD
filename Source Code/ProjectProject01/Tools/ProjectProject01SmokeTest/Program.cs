using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Xml.Linq;

var options = SmokeOptions.Parse(args);
var runner = new SmokeRunner(options);
return await runner.RunAsync();

internal sealed record SmokeOptions(string BaseUrl, string ProjectRoot, TimeSpan StepTimeout)
{
    public static SmokeOptions Parse(string[] args)
    {
        string baseUrl = "http://127.0.0.1:5080";
        string projectRoot = FindProjectRoot() ?? Environment.CurrentDirectory;
        double timeoutSeconds = 10;
        for (var index = 0; index < args.Length - 1; index++)
        {
            if (args[index] == "--base-url") baseUrl = args[++index];
            else if (args[index] == "--project-root") projectRoot = Path.GetFullPath(args[++index]);
            else if (args[index] == "--timeout-seconds" && double.TryParse(args[++index], out var parsed))
                timeoutSeconds = Math.Clamp(parsed, 2, 60);
        }
        return new SmokeOptions(baseUrl.TrimEnd('/'), projectRoot, TimeSpan.FromSeconds(timeoutSeconds));
    }

    private static string? FindProjectRoot()
    {
        DirectoryInfo? directory = new(AppContext.BaseDirectory);
        while (directory != null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "ProjectProject01.uproject"))) return directory.FullName;
            directory = directory.Parent;
        }
        return null;
    }
}

internal sealed class SmokeRunner(SmokeOptions options)
{
    private readonly HttpClient http = new() { BaseAddress = new Uri(options.BaseUrl + "/") };
    private readonly List<SmokeEvent> events = [];
    private readonly StringBuilder rawLog = new();
    private readonly Stopwatch runClock = Stopwatch.StartNew();
    private readonly string testRunId = $"Smoke_{DateTime.UtcNow:yyyyMMdd_HHmmss}_{Guid.NewGuid():N}"[..38];
    private CompatibilityContract contract = null!;
    private string currentStep = "Initialize";
    private string? failure;

    public async Task<int> RunAsync()
    {
        var outputDirectory = Path.Combine(options.ProjectRoot, "Saved", "Diagnostics", testRunId);
        Directory.CreateDirectory(outputDirectory);
        var clients = new List<SmokeClient>();
        try
        {
            await StepAsync("BackendHealth", async ct =>
            {
                await SendAsync(HttpMethod.Get, "health", null, null, false, ct);
            });
            await StepAsync("LoadVersionContract", async ct =>
            {
                var json = await SendAsync(HttpMethod.Get, "api/compatibility", null, null, false, ct);
                contract = CompatibilityContract.From(json);
            });
            await StepAsync("RejectIncompatibleClient", async ct =>
            {
                using var request = NewRequest(HttpMethod.Post, "api/auth/login", null, new { accountId = "invalid", password = "invalid-password" });
                ApplyVersionHeaders(request, contract with { NetworkProtocolVersion = "intentionally-mismatched" });
                using var response = await http.SendAsync(request, ct);
                var body = await response.Content.ReadAsStringAsync(ct);
                LogResponse(request, response, body);
                if (response.StatusCode != HttpStatusCode.UpgradeRequired)
                    throw new InvalidOperationException($"버전 불일치 요청이 차단되지 않았습니다. HTTP {(int)response.StatusCode}");
            });

            var suffix = Guid.NewGuid().ToString("N")[..10];
            for (var index = 0; index < 3; index++)
            {
                var client = new SmokeClient($"smk{index}_{suffix}", $"Smoke{index}_{suffix}", $"Smoke!{suffix}Aa7");
                clients.Add(client);
                await StepAsync($"RegisterPlayer{index + 1}", async ct =>
                {
                    var json = await SendAsync(HttpMethod.Post, "api/auth/register", null,
                        new { accountId = client.AccountId, password = client.Password, displayName = client.DisplayName }, true, ct);
                    client.AccessToken = RequiredString(json, "accessToken");
                });
            }

            string roomId = string.Empty;
            string joinCode = string.Empty;
            await StepAsync("CreateRoom", async ct =>
            {
                var json = await SendAsync(HttpMethod.Post, "api/rooms", clients[0].AccessToken,
                    new { name = $"Smoke-{suffix}", isPublic = false, password = "SmokeRoom!1" }, true, ct);
                var room = RequiredObject(json, "room");
                roomId = RequiredString(room, "roomId");
                joinCode = RequiredString(room, "joinCode");
            });
            for (var index = 1; index < clients.Count; index++)
            {
                var playerIndex = index;
                await StepAsync($"JoinPlayer{index + 1}", async ct =>
                {
                    var json = await SendAsync(HttpMethod.Post, $"api/rooms/{joinCode}/join", clients[playerIndex].AccessToken,
                        new { password = "SmokeRoom!1" }, true, ct);
                    if (RequiredString(RequiredObject(json, "room"), "roomId") != roomId)
                        throw new InvalidOperationException("참가한 방 ID가 생성한 방과 다릅니다.");
                });
            }
            for (var index = 1; index < clients.Count; index++)
            {
                var playerIndex = index;
                await StepAsync($"ReadyPlayer{index + 1}", async ct =>
                {
                    await SendAsync(HttpMethod.Post, "api/rooms/current/ready", clients[playerIndex].AccessToken,
                        new { ready = true }, true, ct);
                });
            }

            string matchId = string.Empty;
            await StepAsync("StartMatchAndAssignRoles", async ct =>
            {
                var json = await SendAsync(HttpMethod.Post, "api/rooms/current/start", clients[0].AccessToken, new { }, true, ct);
                var room = RequiredObject(json, "room");
                var members = RequiredArray(room, "members");
                if (members.Count(item => RequiredString(item!, "assignedRole") == "Mannequin") != 1 ||
                    members.Count(item => RequiredString(item!, "assignedRole") == "Survivor") != 2)
                    throw new InvalidOperationException("역할 배정이 마네킹 1명/생존자 2명이 아닙니다.");
            });

            var serverSecret = LoadServerSecret(options.ProjectRoot);
            if (serverSecret.Length < 32)
                throw new InvalidOperationException("PROJECTPROJECT01_GAME_SERVER_SECRET 또는 로컬 백엔드 설정의 서버 비밀키가 필요합니다.");

            for (var index = 0; index < clients.Count; index++)
            {
                var playerIndex = index;
                await StepAsync($"IssueAndConsumeGameTicket{index + 1}", async ct =>
                {
                    var ticketJson = await SendAsync(HttpMethod.Post, "api/game-tickets", clients[playerIndex].AccessToken,
                        new { }, true, ct);
                    var ticket = RequiredString(ticketJson, "ticket");
                    matchId = RequiredString(ticketJson, "matchId");
                    clients[playerIndex].Role = RequiredString(ticketJson, "role");
                    var consumed = await SendAsync(HttpMethod.Post, "api/server/game-tickets/consume", null,
                        new { ticket }, true, ct, serverSecret);
                    clients[playerIndex].UserId = RequiredString(consumed, "userId");
                    if (RequiredString(consumed, "matchId") != matchId || RequiredString(consumed, "roomId") != roomId ||
                        RequiredString(consumed, "role") != clients[playerIndex].Role)
                        throw new InvalidOperationException("게임 티켓의 방·경기·역할 claim이 일치하지 않습니다.");
                    contract.AssertMatches(consumed);
                });
            }

            await StepAsync("AuthoritativeEscapeAndWinLoss", async ct =>
            {
                foreach (var client in clients)
                {
                    var survivor = client.Role == "Survivor";
                    await SendAsync(HttpMethod.Post, "api/server/matches/results", null, new
                    {
                        matchId,
                        userId = client.UserId,
                        role = client.Role,
                        success = survivor,
                        captureCount = (int?)null,
                        firstCaptureSeconds = (double?)null,
                        allCapturedSeconds = (double?)null,
                        rescueCount = survivor ? 1 : (int?)null,
                        escapeSeconds = survivor ? 42.0 : (double?)null
                    }, true, ct, serverSecret);
                }
            });

            await StepAsync("PublishSuccessfulResults", async ct =>
            {
                foreach (var client in clients.Where(item => item.Role == "Survivor"))
                {
                    await SendAsync(HttpMethod.Post, "api/leaderboards/records", client.AccessToken, new
                    {
                        matchId,
                        role = client.Role,
                        success = true,
                        captureCount = (int?)null,
                        firstCaptureSeconds = (double?)null,
                        allCapturedSeconds = (double?)null,
                        rescueCount = 1,
                        escapeSeconds = 42.0
                    }, true, ct);
                }
            });

            for (var index = 0; index < clients.Count; index++)
            {
                var playerIndex = index;
                await StepAsync($"ReturnPlayer{index + 1}ToRoom", async ct =>
                {
                    await SendAsync(HttpMethod.Post, "api/rooms/current/return", clients[playerIndex].AccessToken,
                        new { }, true, ct);
                });
            }
            await StepAsync("VerifyExistingRoomRestored", async ct =>
            {
                var json = await SendAsync(HttpMethod.Get, "api/rooms/current", clients[0].AccessToken, null, true, ct);
                var room = RequiredObject(json, "room");
                if (RequiredString(room, "roomId") != roomId || room.GetProperty("started").GetBoolean())
                    throw new InvalidOperationException("기존 방이 대기 상태로 복구되지 않았습니다.");
            });
        }
        catch (Exception exception)
        {
            failure = $"{currentStep}: {exception.Message}";
            AddEvent("Risk", "SmokeStepFailed", failure);
            Console.Error.WriteLine($"[FAIL] {failure}");
        }
        finally
        {
            foreach (var client in clients.Where(item => !string.IsNullOrEmpty(item.AccessToken)))
            {
                try
                {
                    using var cleanupCts = new CancellationTokenSource(options.StepTimeout);
                    await SendAsync(HttpMethod.Post, "api/rooms/current/leave", client.AccessToken, new { }, false, cleanupCts.Token);
                }
                catch { /* cleanup must not replace the real smoke result */ }
            }
            await WriteReportsAsync(outputDirectory);
            http.Dispose();
        }

        Console.WriteLine(failure is null
            ? $"[PASS] 전체 흐름 스모크 테스트 통과: {outputDirectory}"
            : $"[REPORT] 실패 보고서: {outputDirectory}");
        return failure is null ? 0 : 1;
    }

    private async Task StepAsync(string name, Func<CancellationToken, Task> action)
    {
        currentStep = name;
        var stopwatch = Stopwatch.StartNew();
        using var cts = new CancellationTokenSource(options.StepTimeout);
        try
        {
            await action(cts.Token);
            AddEvent("Normal", "SmokeStepPassed", $"Step={name}; ElapsedMs={stopwatch.Elapsed.TotalMilliseconds:F1}");
            Console.WriteLine($"[PASS] {name} ({stopwatch.Elapsed.TotalMilliseconds:F0} ms)");
        }
        catch (OperationCanceledException) when (cts.IsCancellationRequested)
        {
            throw new TimeoutException($"단계 제한 시간 {options.StepTimeout.TotalSeconds:F0}초를 초과했습니다.");
        }
    }

    private async Task<JsonElement> SendAsync(
        HttpMethod method, string path, string? token, object? body, bool requireSuccess,
        CancellationToken cancellationToken, string? serverSecret = null)
    {
        using var request = NewRequest(method, path, token, body);
        ApplyVersionHeaders(request, contract);
        if (!string.IsNullOrEmpty(serverSecret))
            request.Headers.TryAddWithoutValidation("X-ProjectProject01-Server-Secret", serverSecret);
        using var response = await http.SendAsync(request, cancellationToken);
        var responseBody = await response.Content.ReadAsStringAsync(cancellationToken);
        LogResponse(request, response, responseBody);
        if (requireSuccess && !response.IsSuccessStatusCode)
            throw new InvalidOperationException($"{method} {path} → HTTP {(int)response.StatusCode}: {ExtractMessage(responseBody)}");
        if (string.IsNullOrWhiteSpace(responseBody)) return JsonDocument.Parse("{}").RootElement.Clone();
        return JsonDocument.Parse(responseBody).RootElement.Clone();
    }

    private static HttpRequestMessage NewRequest(HttpMethod method, string path, string? token, object? body)
    {
        var request = new HttpRequestMessage(method, path);
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        if (!string.IsNullOrEmpty(token)) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
        if (body is not null)
            request.Content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json");
        return request;
    }

    private static void ApplyVersionHeaders(HttpRequestMessage request, CompatibilityContract? value)
    {
        if (value is null) return;
        request.Headers.TryAddWithoutValidation("X-ProjectProject01-Client-Build", value.ClientBuildVersion);
        request.Headers.TryAddWithoutValidation("X-ProjectProject01-Server-Build", value.DedicatedServerBuildVersion);
        request.Headers.TryAddWithoutValidation("X-ProjectProject01-Api-Version", value.ApiVersion);
        request.Headers.TryAddWithoutValidation("X-ProjectProject01-Game-Data-Version", value.GameDataVersion);
        request.Headers.TryAddWithoutValidation("X-ProjectProject01-Network-Protocol", value.NetworkProtocolVersion);
    }

    private void LogResponse(HttpRequestMessage request, HttpResponseMessage response, string responseBody)
    {
        rawLog.Append(DateTime.UtcNow.ToString("O")).Append(' ')
            .Append(request.Method).Append(' ').Append(request.RequestUri).Append(" HTTP ")
            .Append((int)response.StatusCode).Append(' ').Append(response.StatusCode).AppendLine();
        if (!string.IsNullOrWhiteSpace(responseBody)) rawLog.AppendLine(Redact(responseBody));
    }

    private void AddEvent(string severity, string code, string message) =>
        events.Add(new SmokeEvent(DateTime.UtcNow, runClock.Elapsed.TotalSeconds, severity, code, message));

    private async Task WriteReportsAsync(string outputDirectory)
    {
        var outcome = failure is null ? "Pass" : "ReviewRequired";
        var report = new XDocument(
            new XElement("TestReport",
                new XAttribute("TestRunId", testRunId),
                new XAttribute("TestName", "FullGameFlowSmoke"),
                new XAttribute("Outcome", outcome),
                new XAttribute("Role", "SmokeTest"),
                new XAttribute("NetMode", "External"),
                new XAttribute("Map", "Login-Lobby-MultiplayTest"),
                new XAttribute("GameMode", "SmokeRunner"),
                new XAttribute("EngineVersion", ".NET 8"),
                new XAttribute("ConnectedPlayerCount", "3"),
                new XElement("TestDefinition",
                    new XAttribute("Purpose", "로그인부터 경기 결과 등록과 기존 방 복귀까지의 최소 정상 흐름 검증"),
                    new XAttribute("TimeoutSeconds", options.StepTimeout.TotalSeconds.ToString("F0"))),
                new XElement("Compatibility",
                    new XAttribute("ClientBuildVersion", contract?.ClientBuildVersion ?? "Unavailable"),
                    new XAttribute("DedicatedServerBuildVersion", contract?.DedicatedServerBuildVersion ?? "Unavailable"),
                    new XAttribute("ApiVersion", contract?.ApiVersion ?? "Unavailable"),
                    new XAttribute("GameDataVersion", contract?.GameDataVersion ?? "Unavailable"),
                    new XAttribute("NetworkProtocolVersion", contract?.NetworkProtocolVersion ?? "Unavailable")),
                new XElement("Collection",
                    new XAttribute("SamplingSeconds", "0"),
                    new XAttribute("Mode", "Smoke"),
                    new XAttribute("SampleCount", "0"),
                    new XAttribute("EventCount", events.Count),
                    new XAttribute("RiskCandidateCount", failure is null ? 0 : 1)),
                new XElement("SmokeResult",
                    new XAttribute("Passed", failure is null),
                    new XAttribute("FailedStep", failure is null ? "None" : currentStep),
                    new XAttribute("StepCount", events.Count),
                    new XAttribute("RawLog", "SmokeTest.log"),
                    failure is null ? "전체 최소 정상 흐름이 통과했습니다." : failure)));
        await File.WriteAllTextAsync(Path.Combine(outputDirectory, "TestReport.xml"), report.ToString(), new UTF8Encoding(true));
        var csv = new StringBuilder("UtcTime,Role,NetMode,LocalPlayerNumber,GameSeconds,Severity,Code,Message,Actor\n");
        foreach (var item in events)
            csv.Append(Csv(item.UtcTime.ToString("O"))).Append(",SmokeTest,External,-1,")
                .Append(item.GameSeconds.ToString("F3", System.Globalization.CultureInfo.InvariantCulture)).Append(',')
                .Append(Csv(item.Severity)).Append(',').Append(Csv(item.Code)).Append(',')
                .Append(Csv(item.Message)).Append(",SmokeRunner\n");
        await File.WriteAllTextAsync(Path.Combine(outputDirectory, "Events.csv"), csv.ToString(), new UTF8Encoding(true));
        await File.WriteAllTextAsync(Path.Combine(outputDirectory, "SmokeTest.log"), rawLog.ToString(), new UTF8Encoding(true));
    }

    private static string LoadServerSecret(string projectRoot)
    {
        var environment = Environment.GetEnvironmentVariable("PROJECTPROJECT01_GAME_SERVER_SECRET")?.Trim();
        if (!string.IsNullOrEmpty(environment)) return environment;
        var path = Path.Combine(projectRoot, "Tools", "ProjectProject01Backend", "LocalMySql", "ProjectProject01Backend.local.json");
        if (!File.Exists(path)) return string.Empty;
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        return document.RootElement.TryGetProperty("GameServer", out var server) &&
               server.TryGetProperty("SharedSecret", out var secret) ? secret.GetString()?.Trim() ?? string.Empty : string.Empty;
    }

    private static JsonElement RequiredObject(JsonElement source, string name) =>
        source.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Object
            ? value : throw new InvalidOperationException($"응답에 {name} 객체가 없습니다.");
    private static JsonElement.ArrayEnumerator RequiredArray(JsonElement source, string name) =>
        source.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Array
            ? value.EnumerateArray() : throw new InvalidOperationException($"응답에 {name} 배열이 없습니다.");
    private static string RequiredString(JsonElement source, string name) =>
        source.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String && !string.IsNullOrEmpty(value.GetString())
            ? value.GetString()! : throw new InvalidOperationException($"응답에 {name} 값이 없습니다.");
    private static string ExtractMessage(string json)
    {
        try { return JsonNode.Parse(json)?["message"]?.GetValue<string>() ?? json; }
        catch { return json; }
    }
    private static string Redact(string json)
    {
        try
        {
            var node = JsonNode.Parse(json);
            RedactNode(node);
            return node?.ToJsonString() ?? string.Empty;
        }
        catch { return "<non-json response omitted>"; }
    }
    private static void RedactNode(JsonNode? node)
    {
        if (node is JsonObject obj)
        {
            foreach (var property in obj.ToList())
            {
                if (property.Key.Equals("accessToken", StringComparison.OrdinalIgnoreCase) ||
                    property.Key.Equals("refreshToken", StringComparison.OrdinalIgnoreCase) ||
                    property.Key.Equals("ticket", StringComparison.OrdinalIgnoreCase) ||
                    property.Key.Equals("encryptionKey", StringComparison.OrdinalIgnoreCase))
                    obj[property.Key] = "<redacted>";
                else RedactNode(property.Value);
            }
        }
        else if (node is JsonArray array) foreach (var item in array) RedactNode(item);
    }
    private static string Csv(string value) => $"\"{value.Replace("\"", "\"\"")}\"";
}

internal sealed record CompatibilityContract(
    string ClientBuildVersion, string DedicatedServerBuildVersion, string ApiVersion, string GameDataVersion, string NetworkProtocolVersion)
{
    public static CompatibilityContract From(JsonElement json) => new(
        Required(json, "clientBuildVersion"), Required(json, "dedicatedServerBuildVersion"), Required(json, "apiVersion"),
        Required(json, "gameDataVersion"), Required(json, "networkProtocolVersion"));
    public void AssertMatches(JsonElement json)
    {
        var actual = From(json);
        if (actual != this) throw new InvalidOperationException("발급 티켓의 버전 계약이 백엔드 계약과 다릅니다.");
    }
    private static string Required(JsonElement json, string name) =>
        json.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()! : throw new InvalidOperationException($"버전 계약에 {name}이 없습니다.");
}

internal sealed record SmokeEvent(DateTime UtcTime, double GameSeconds, string Severity, string Code, string Message);
internal sealed class SmokeClient(string accountId, string displayName, string password)
{
    public string AccountId { get; } = accountId;
    public string DisplayName { get; } = displayName;
    public string Password { get; } = password;
    public string AccessToken { get; set; } = string.Empty;
    public string UserId { get; set; } = string.Empty;
    public string Role { get; set; } = string.Empty;
}
