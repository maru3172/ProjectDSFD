// File: Tools/ProjectProject01Backend/Program.cs
// Target: .NET 9 / MySQL Server 8.0

using System.Security.Cryptography;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.Identity;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.AspNetCore.WebUtilities;
using MySqlConnector;

const int AccessTokenMinutes = 30;
const int RefreshTokenDays = 30;

// Match the helper's redirected UTF-8 reader; never inherit a legacy Windows code page.
Console.OutputEncoding = new UTF8Encoding(false);
var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<CaseDebugCurrency>();
var localConfigurationPath = Path.Combine(
    builder.Environment.ContentRootPath,
    "LocalMySql",
    "ProjectProject01Backend.local.json");
builder.Configuration.AddUserSecrets<Program>(optional: true, reloadOnChange: false);
builder.Configuration.AddEnvironmentVariables();
// 프로젝트 전용 LocalMySql 인스턴스가 준비된 개발 PC에서는 해당 인스턴스와 함께 생성된
// 접속 정보를 최종 우선합니다. 이 파일은 Git에 포함되지 않으며, 파일이 없는 배포/CI 환경은
// 기존과 같이 User Secrets 또는 환경변수를 사용합니다.
if (File.Exists(localConfigurationPath))
{
    builder.Configuration.AddJsonFile(localConfigurationPath, optional: false, reloadOnChange: false);
}
// 개발용 직접 IP 접속에서도 ASP.NET Host 필터가 이 PC의 활성 IPv4 주소를 허용하도록 한다.
// 실제 HTTP 허용 여부는 아래 RequestSecurity에서 VPN/사설망과 동일 서브넷까지 다시 검사한다.
var configuredAllowedHosts = (builder.Configuration["AllowedHosts"] ?? string.Empty)
    .Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
var localIpv4Hosts = NetworkInterface.GetAllNetworkInterfaces()
    .Where(network => network.OperationalStatus == OperationalStatus.Up)
    .SelectMany(network => network.GetIPProperties().UnicastAddresses)
    .Where(unicast => unicast.Address.AddressFamily == AddressFamily.InterNetwork)
    .Select(unicast => unicast.Address.ToString());
builder.Configuration["AllowedHosts"] = string.Join(';', configuredAllowedHosts
    .Concat(localIpv4Hosts)
    .Append("localhost")
    .Append("127.0.0.1")
    .Distinct(StringComparer.OrdinalIgnoreCase));
builder.Logging.ClearProviders();
builder.Logging.AddConsole();

var securityOptions = ProjectSecurityOptions.FromConfiguration(builder.Configuration);
builder.Services.AddSingleton(securityOptions);
var versionCompatibility = VersionCompatibilityOptions.FromConfiguration(builder.Configuration);
builder.Services.AddSingleton(versionCompatibility);
var administratorOptions = AdministratorOptions.FromConfiguration(builder.Configuration);
builder.Services.AddSingleton(administratorOptions);
builder.Services.AddSingleton<ServiceControlState>();
builder.Services.AddHostedService<ServiceControlScheduleWorker>();
builder.Services.AddHostedService<ClientSessionLivenessWorker>();
builder.Services.AddHostedService<MatchSearchCleanupWorker>();
builder.Services.AddHostedService<MatchResultRecoveryWorker>();
builder.Services.AddSingleton<GameServerPresenceState>();
builder.Services.AddSingleton<IGameServerProcessFactory,GameServerProcessFactory>();
builder.Services.AddSingleton(TimeProvider.System);
builder.Services.AddSingleton<GameServerDemandManager>();
builder.Services.AddHostedService(services=>services.GetRequiredService<GameServerDemandManager>());
builder.Services.AddSingleton<EosVoiceTokenService>();
builder.Services.AddSingleton<VoiceParticipantDirectory>();
builder.Services.AddSingleton<VoiceAccountRateLimit>();
builder.Services.AddSingleton(BackendDiagnostics.Shared);
builder.Services.AddHostedService<BackendDiagnosticsWorker>();
builder.Services.AddSingleton<CrashArtifactStore>();
builder.Services.AddHostedService<CrashArtifactRetentionWorker>();
builder.Services.AddHostedService(services => services.GetRequiredService<EosVoiceTokenService>());

builder.Services.AddSingleton<IPasswordHasher<AuthUser>, PasswordHasher<AuthUser>>();
builder.Services.AddSingleton<IPasswordHasher<RoomPasswordRecord>, PasswordHasher<RoomPasswordRecord>>();
builder.Services.AddSingleton(sp =>
{
    var configuredConnectionString = builder.Configuration.GetConnectionString("ProjectProject01");
    return new AuthDatabase(configuredConnectionString);
});
builder.Services.AddRateLimiter(options =>
{
    options.RejectionStatusCode = StatusCodes.Status429TooManyRequests;
    options.OnRejected = async (context, cancellationToken) =>
    {
        var logger = context.HttpContext.RequestServices
            .GetRequiredService<ILoggerFactory>()
            .CreateLogger("ProjectProject01Security");
        logger.LogWarning(
            "Security rate limit rejected {Method} {Path} from {RemoteAddress}.",
            context.HttpContext.Request.Method,
            context.HttpContext.Request.Path.Value,
            RequestSecurity.GetClientAddress(context.HttpContext));
        var database = context.HttpContext.RequestServices.GetRequiredService<AuthDatabase>();
        await SecurityAudit.WriteAsync(
            database,
            context.HttpContext.Request,
            "RateLimit",
            "Rejected",
            null,
            $"Method={context.HttpContext.Request.Method}; Path={context.HttpContext.Request.Path.Value}",
            cancellationToken);
    };
    NetworkRequestBudget.AddPolicies(options,builder.Configuration);
    // Social polling must not consume the shared classroom/VPN IP's lobby request budget.
    options.AddPolicy("social", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(context.Request.Headers.Authorization.ToString()[..Math.Min(context.Request.Headers.Authorization.ToString().Length,600)]))),
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = 240,
                Window = TimeSpan.FromMinutes(1),
                QueueLimit = 0,
                AutoReplenishment = true
            }));
    // A crowded school/VPN address must not exhaust login liveness checks for everyone.
    options.AddPolicy("session-heartbeat", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(context.Request.Headers.Authorization.ToString()[..Math.Min(context.Request.Headers.Authorization.ToString().Length,600)]))),
            _ => new FixedWindowRateLimiterOptions { PermitLimit=30,Window=TimeSpan.FromMinutes(1),QueueLimit=0,AutoReplenishment=true }));
    options.AddPolicy("game-server", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            RequestSecurity.GetClientAddress(context) + (
                securityOptions.IsAuthorizedGameServer(context.Request.Headers["X-ProjectProject01-Server-Secret"].ToString()) &&
                int.TryParse(context.Request.Headers["X-ProjectProject01-Server-Port"],out int serverPort) && serverPort is >=1024 and <=65535
                    ? $":{serverPort}" : ":unassigned"),
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = 600,
                Window = TimeSpan.FromMinutes(1),
                QueueLimit = 0,
                AutoReplenishment = true
            }));
    options.AddPolicy("crash-report", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            RequestSecurity.GetClientAddress(context),
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = 30,
                Window = TimeSpan.FromMinutes(1),
                QueueLimit = 0,
                AutoReplenishment = true
            }));
    options.AddPolicy("survey-read", context =>
        RateLimitPartition.GetFixedWindowLimiter(RequestSecurity.GetClientAddress(context),
            _ => new FixedWindowRateLimiterOptions { PermitLimit=300, Window=TimeSpan.FromMinutes(1), QueueLimit=0, AutoReplenishment=true }));
    options.AddPolicy("survey", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            RequestSecurity.GetClientAddress(context),
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = 10,
                Window = TimeSpan.FromMinutes(10),
                QueueLimit = 0,
                AutoReplenishment = true
            }));
    options.AddPolicy("admin", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            RequestSecurity.GetClientAddress(context),
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = 60,
                Window = TimeSpan.FromMinutes(1),
                QueueLimit = 0,
                AutoReplenishment = true
            }));
    options.AddPolicy("voice-token", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            RequestSecurity.GetClientAddress(context),
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = Math.Clamp(builder.Configuration.GetValue("EosVoice:TokenRequestsPerIpPerMinute",600),60,6000),
                Window = TimeSpan.FromMinutes(1),
                QueueLimit = 0,
                AutoReplenishment = true
            }));
    options.AddPolicy("voice-roster", context =>
        RateLimitPartition.GetFixedWindowLimiter(RequestSecurity.GetClientAddress(context),
            _ => new FixedWindowRateLimiterOptions { PermitLimit=600, Window=TimeSpan.FromMinutes(1), QueueLimit=0, AutoReplenishment=true }));
});

var app = builder.Build();
// Attach the fixed-cardinality metrics listener before startup creates any connection pools.
app.UseBackendDiagnostics();
var allowedInsecureVpnHost = builder.Configuration["Security:AllowedInsecureVpnHost"]?.Trim();
app.Use(async (context, next) =>
{
    var remoteAddress = context.Connection.RemoteIpAddress;
    var isLoopback = remoteAddress is not null && IPAddress.IsLoopback(remoteAddress);
    var forwardedByLocalProxy = isLoopback && context.Request.Headers.ContainsKey("X-Forwarded-For");
    var forwardedHttps = string.Equals(
        context.Request.Headers["X-Forwarded-Proto"].ToString(), "https", StringComparison.OrdinalIgnoreCase);
    var isAllowedTestHttp = RequestSecurity.IsAllowedInsecureTestHttp(context, allowedInsecureVpnHost);
    if (!context.Request.IsHttps && !isAllowedTestHttp && (!isLoopback || (forwardedByLocalProxy && !forwardedHttps)))
    {
        app.Logger.LogWarning(
            "Rejected insecure public HTTP request {Method} {Path} from {RemoteAddress}.",
            context.Request.Method, context.Request.Path.Value, remoteAddress?.ToString() ?? "unknown");
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        await context.Response.WriteAsJsonAsync(new AuthFailure(
            "공개 접속에서는 HTTPS가 필요합니다. HTTP는 같은 컴퓨터의 개발 테스트에만 허용됩니다."));
        return;
    }
    await next();
});
app.UseProjectProject01VersionCompatibility();
app.UseProjectProject01ServiceControl();
app.UseRateLimiter();

var startupDatabase = app.Services.GetRequiredService<AuthDatabase>();
if (startupDatabase.IsConfigured)
{
    try
    {
        await startupDatabase.InitializeSchemaAsync(app.Environment.ContentRootPath, app.Lifetime.ApplicationStopping);
        await app.Services.GetRequiredService<GameServerDemandManager>().ReconcileStartupAsync(app.Lifetime.ApplicationStopping);
        var startupServiceState = app.Services.GetRequiredService<ServiceControlState>();
        startupServiceState.Set(await ServiceControlStore.LoadAsync(
            startupDatabase, app.Lifetime.ApplicationStopping));
        app.Logger.LogInformation("ProjectProject01 authentication schema is ready.");
    }
    catch (Exception exception)
    {
        app.Logger.LogError(exception, "MySQL schema initialization failed. The API will stay available for health diagnostics.");
    }
}
else
{
    app.Logger.LogWarning("ConnectionStrings:ProjectProject01 is not configured. Set it with dotnet user-secrets.");
}

app.MapGet("/health", async (AuthDatabase database, EosVoiceTokenService eosVoice, CancellationToken cancellationToken) =>
{
    if (!database.IsConfigured)
    {
        return Results.Json(new { status = "unconfigured", message = "MySQL connection string is not configured." }, statusCode: 503);
    }

    try
    {
        await using var connection = await database.OpenConnectionAsync(cancellationToken);
        await using var command = new MySqlCommand("SELECT 1;", connection) { CommandTimeout = 5 };
        await command.ExecuteScalarAsync(cancellationToken);
        return Results.Ok(new { status = "healthy", eosVoiceReady = eosVoice.IsReady });
    }
    catch
    {
        return Results.Json(new { status = "unavailable", message = "MySQL connection failed." }, statusCode: 503);
    }
});

app.MapGet("/api/compatibility", (VersionCompatibilityOptions contract) =>
    Results.Ok(contract.ToResponse()));
app.MapProjectProject01ClientUpdates();
app.MapBackendDiagnostics();
app.MapCommunity();
app.MapCaseRecords();
app.MapProjectProject01ServiceAdministration();
app.MapProjectProject01Surveys();
app.MapProjectProject01Voice();

app.MapRegistration(CreateSessionAsync);

app.MapPost("/api/auth/login", async (
    HttpRequest httpRequest,
    LoginRequest request,
    AuthDatabase database,
    IPasswordHasher<AuthUser> passwordHasher,
    CancellationToken cancellationToken) =>
{
    var validationError = AuthValidation.Validate(request.AccountId, request.Password, null);
    if (validationError is not null)
    {
        await SecurityAudit.WriteAsync(database, httpRequest, "AuthLogin", "RejectedInput", null, validationError, cancellationToken);
        return Results.BadRequest(new AuthFailure(validationError));
    }
    if (!database.IsConfigured)
    {
        return DatabaseUnavailable();
    }

    try
    {
        await using var connection = await database.OpenConnectionAsync(cancellationToken);
        var user = await LoadUserAsync(connection, request.AccountId.Trim(), cancellationToken);
        if (user is null)
        {
            await SecurityAudit.WriteAsync(database, httpRequest, "AuthLogin", "InvalidCredentials", null, null, cancellationToken);
            return InvalidCredentials();
        }
        if (user.LockedUntilUtc.HasValue && user.LockedUntilUtc.Value > DateTime.UtcNow)
        {
            await SecurityAudit.WriteAsync(database, httpRequest, "AuthLogin", "AccountLocked", user.Id, null, cancellationToken);
            return InvalidCredentials();
        }

        var verifyResult = passwordHasher.VerifyHashedPassword(user, user.PasswordHash, request.Password);
        if (verifyResult == PasswordVerificationResult.Failed)
        {
            await LoginFailurePolicy.RecordAsync(connection, user.Id, cancellationToken);
            await SecurityAudit.WriteAsync(database, httpRequest, "AuthLogin", "InvalidCredentials", user.Id,
                "FailureCountIncrementedAtomically", cancellationToken);
            return InvalidCredentials();
        }

        await using var transaction = await connection.BeginTransactionAsync(cancellationToken);
        await using (var resetFailure = new MySqlCommand(
            "UPDATE users SET failed_login_count = 0, locked_until_utc = NULL WHERE id = @userId;",
            connection,
            transaction))
        {
            await using var userLock = new MySqlCommand("SELECT id FROM users WHERE id=@id FOR UPDATE;", connection, transaction);
            userLock.Parameters.AddWithValue("@id",user.Id);
            await userLock.ExecuteScalarAsync(cancellationToken);
            string? restriction = await AccountRestrictions.GetMessageAsync(connection,transaction,user.Id,cancellationToken);
            if(restriction is not null) return Results.Json(new {code="AccountRestricted",message=restriction},statusCode:403);
            resetFailure.CommandTimeout = 5;
            resetFailure.Parameters.AddWithValue("@userId", user.Id);
            await resetFailure.ExecuteNonQueryAsync(cancellationToken);
        }

        if (verifyResult == PasswordVerificationResult.SuccessRehashNeeded)
        {
            await using var updateHash = new MySqlCommand(
                "UPDATE users SET password_hash = @passwordHash WHERE id = @userId;", connection, transaction)
            {
                CommandTimeout = 5
            };
            updateHash.Parameters.AddWithValue("@passwordHash", passwordHasher.HashPassword(user, request.Password));
            updateHash.Parameters.AddWithValue("@userId", user.Id);
            await updateHash.ExecuteNonQueryAsync(cancellationToken);
        }

        // 한 계정은 최신 로그인 세션 하나만 유지한다. 이전 PC/중복 로그인 토큰은 즉시 폐기된다.
        await using (var revokePreviousSessions = new MySqlCommand(
            "UPDATE auth_sessions SET revoked_at_utc = UTC_TIMESTAMP(6) WHERE user_id = @userId AND revoked_at_utc IS NULL;",
            connection, transaction) { CommandTimeout = 5 })
        {
            revokePreviousSessions.Parameters.AddWithValue("@userId", user.Id);
            await revokePreviousSessions.ExecuteNonQueryAsync(cancellationToken);
        }

        var tokens = await CreateSessionAsync(connection, transaction, user.Id, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        await SecurityAudit.WriteAsync(database, httpRequest, "AuthLogin", "Success", user.Id, null, cancellationToken);
        return Results.Ok(new AuthSuccess(
            "로그인되었습니다.", user.DisplayName, tokens.AccessToken, tokens.RefreshToken,
            tokens.AccessExpiresAtUtc, tokens.RefreshExpiresAtUtc,
            user.SeenSurvivorGuide, user.SeenMannequinGuide, user.SeenInitialTutorial));
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("auth");

app.MapPost("/api/auth/refresh", async (
    HttpRequest httpRequest,
    RefreshRequest request,
    AuthDatabase database,
    CancellationToken cancellationToken) =>
{
    if (string.IsNullOrWhiteSpace(request.RefreshToken) || request.RefreshToken.Length > 512)
    {
        await SecurityAudit.WriteAsync(database, httpRequest, "AuthRefresh", "RejectedInput", null, null, cancellationToken);
        return Results.BadRequest(new AuthFailure("리프레시 토큰 형식이 잘못되었습니다."));
    }
    if (!database.IsConfigured)
    {
        return DatabaseUnavailable();
    }

    try
    {
        await using var connection = await database.OpenConnectionAsync(cancellationToken);
        await using var transaction = await connection.BeginTransactionAsync(cancellationToken);
        var refreshHash = HashToken(request.RefreshToken);
        ulong sessionId;
        ulong userId;
        string displayName;
        bool seenSurvivorGuide;
        bool seenMannequinGuide;
        bool seenInitialTutorial;
        await using (var select = new MySqlCommand(
            """
            SELECT s.id, s.user_id, u.display_name,
                   u.seen_survivor_guide, u.seen_mannequin_guide, u.seen_initial_tutorial
            FROM auth_sessions s
            INNER JOIN users u ON u.id = s.user_id
            WHERE s.refresh_token_hash = @refreshHash
              AND s.revoked_at_utc IS NULL
              AND s.refresh_expires_at_utc > UTC_TIMESTAMP(6)
              AND NOT EXISTS(SELECT 1 FROM auth_session_liveness l WHERE l.session_id=s.id AND l.deadline_utc<=UTC_TIMESTAMP(6))
              AND NOT EXISTS (SELECT 1 FROM account_restrictions r WHERE r.user_id=u.id AND r.lifted_at_utc IS NULL
                  AND (r.expires_at_utc IS NULL OR r.expires_at_utc>UTC_TIMESTAMP(6)))
            FOR UPDATE;
            """, connection, transaction))
        {
            select.CommandTimeout = 5;
            select.Parameters.Add("@refreshHash", MySqlDbType.Binary, 32).Value = refreshHash;
            await using var reader = await select.ExecuteReaderAsync(cancellationToken);
            if (!await reader.ReadAsync(cancellationToken))
            {
                await transaction.RollbackAsync(cancellationToken);
                await SecurityAudit.WriteAsync(database, httpRequest, "AuthRefresh", "InvalidOrExpired", null, null, cancellationToken);
                return Results.Unauthorized();
            }
            sessionId = reader.GetUInt64(0);
            userId = reader.GetUInt64(1);
            displayName = reader.GetString(2);
            seenSurvivorGuide = reader.GetBoolean(3);
            seenMannequinGuide = reader.GetBoolean(4);
            seenInitialTutorial = reader.GetBoolean(5);
        }

        await RevokeSessionAsync(connection, transaction, sessionId, cancellationToken);
        var tokens = await CreateSessionAsync(connection, transaction, userId, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        await SecurityAudit.WriteAsync(database, httpRequest, "AuthRefresh", "Success", userId, null, cancellationToken);
        return Results.Ok(new AuthSuccess(
            "세션이 갱신되었습니다.", displayName, tokens.AccessToken, tokens.RefreshToken,
            tokens.AccessExpiresAtUtc, tokens.RefreshExpiresAtUtc,
            seenSurvivorGuide, seenMannequinGuide, seenInitialTutorial));
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("auth");

// 로그인 이후 어느 화면에 있든 관리자 강제 로그아웃과 백엔드 단절을 빠르게 감지하기 위한
// 가벼운 세션 확인 경로입니다. 유효한 액세스 토큰만 성공하며 상태를 변경하지 않습니다.
app.MapClientLiveness();
app.MapGet("/api/auth/session", async (
    HttpRequest request,
    AuthDatabase database,
    CancellationToken cancellationToken) =>
{
    try
    {
        var user = await LobbyEndpoints.AuthenticateAsync(request, database, cancellationToken);
        return user is null
            ? Results.Unauthorized()
            : Results.Ok(new { message = "로그인 세션이 유효합니다.", displayName = user.DisplayName });
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("lobby");

app.MapGuideProgress();

app.MapPost("/api/auth/logout", async (
    HttpRequest request,
    AuthDatabase database,
    CancellationToken cancellationToken) =>
{
    var accessToken = ReadBearerToken(request);
    if (accessToken is null)
    {
        return Results.Unauthorized();
    }
    if (!database.IsConfigured)
    {
        return DatabaseUnavailable();
    }

    try
    {
        await using var connection = await database.OpenConnectionAsync(cancellationToken);
        await using var transaction = await connection.BeginTransactionAsync(cancellationToken);
        ulong? userId = null;
        await using (var select = new MySqlCommand(
            "SELECT user_id FROM auth_sessions WHERE access_token_hash = @accessHash AND revoked_at_utc IS NULL LIMIT 1 FOR UPDATE;",
            connection, transaction)
        {
            CommandTimeout = 5
        })
        {
            select.Parameters.Add("@accessHash", MySqlDbType.Binary, 32).Value = HashToken(accessToken);
            var value = await select.ExecuteScalarAsync(cancellationToken);
            if (value is not null and not DBNull)
            {
                userId = Convert.ToUInt64(value, System.Globalization.CultureInfo.InvariantCulture);
            }
        }

        var revokedCount = 0;
        if (userId is ulong authenticatedUserId)
        {
            await using var revoke = new MySqlCommand(
                "UPDATE auth_sessions SET revoked_at_utc = UTC_TIMESTAMP(6) WHERE access_token_hash = @accessHash AND revoked_at_utc IS NULL;",
                connection, transaction) { CommandTimeout = 5 };
            revoke.Parameters.Add("@accessHash", MySqlDbType.Binary, 32).Value = HashToken(accessToken);
            revokedCount = await revoke.ExecuteNonQueryAsync(cancellationToken);

            // Logout is also a definitive lobby departure. This transfers host ownership when possible
            // and deletes the room only when its final member logs out.
            await LobbyEndpoints.RemoveUserFromCurrentRoomAsync(
                connection, transaction, authenticatedUserId, cancellationToken);
            await OpenParties.LeaveAsync(connection,transaction,authenticatedUserId,cancellationToken);
        }
        await transaction.CommitAsync(cancellationToken);
        await SecurityAudit.WriteAsync(database, request, "AuthLogout", revokedCount > 0 ? "Success" : "UnknownSession",
            userId, null, cancellationToken);
        return Results.Ok(new { message = "로그아웃되었습니다." });
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("auth");

app.MapProjectProject01Lobby(
    builder.Configuration["Lobby:GameServerTravelUrl"] ?? "127.0.0.1:7777",
    securityOptions,
    versionCompatibility,
    app.Services.GetRequiredService<GameServerPresenceState>(),
    app.Services.GetRequiredService<GameServerDemandManager>());

app.MapPost("/api/crash-reports", async (
    CrashReportRequest report,
    AuthDatabase database,
    CrashArtifactStore artifacts,
    CancellationToken cancellationToken) =>
{
    if (!database.IsConfigured)
    {
        return DatabaseUnavailable();
    }
    if (!CrashReportValidation.TryValidate(report, out var validationError))
    {
        return Results.BadRequest(new AuthFailure(validationError));
    }
    try
    {
        await using var connection = await database.OpenConnectionAsync(cancellationToken);
        await using var command = new MySqlCommand(
            """
            INSERT INTO crash_reports
                (report_id, detected_at_utc, started_at_utc, last_updated_at_utc, build_version,
                 map_name, game_mode, process_role, player_role, last_log_path, diagnostic_run_id, match_id)
            VALUES
                (@reportId, @detectedAt, @startedAt, @lastUpdatedAt, @buildVersion,
                 @mapName, @gameMode, @processRole, @playerRole, @lastLogPath, @diagnosticRunId, @matchId)
            ON DUPLICATE KEY UPDATE detected_at_utc = VALUES(detected_at_utc);
            """, connection) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@reportId", report.ReportId);
        command.Parameters.AddWithValue("@detectedAt", report.DetectedAtUtc.ToUniversalTime());
        command.Parameters.AddWithValue("@startedAt", report.StartedAtUtc.ToUniversalTime());
        command.Parameters.AddWithValue("@lastUpdatedAt", report.LastUpdatedAtUtc.ToUniversalTime());
        command.Parameters.AddWithValue("@buildVersion", report.BuildVersion.Trim());
        command.Parameters.AddWithValue("@mapName", report.MapName.Trim());
        command.Parameters.AddWithValue("@gameMode", report.GameMode.Trim());
        command.Parameters.AddWithValue("@processRole", report.ProcessRole.Trim());
        command.Parameters.AddWithValue("@playerRole", string.IsNullOrWhiteSpace(report.PlayerRole) ? DBNull.Value : report.PlayerRole.Trim());
        command.Parameters.AddWithValue("@lastLogPath", report.LastLogPath.Trim());
        command.Parameters.AddWithValue("@diagnosticRunId", report.DiagnosticRunId.Trim());
        command.Parameters.AddWithValue("@matchId", string.IsNullOrWhiteSpace(report.MatchId) ? DBNull.Value : report.MatchId.Trim());
        await command.ExecuteNonQueryAsync(cancellationToken);
        return Results.Created($"/api/crash-reports/{report.ReportId}", new { reportId = report.ReportId, allowMinidumpUploads=artifacts.Policy.AllowMinidumps });
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("crash-report");

app.MapProjectProject01CrashArtifacts();
app.MapAccountRestrictions();
app.MapRoomCapacity();
app.MapGameServerDemand();
app.Run();

static IResult DatabaseUnavailable() =>
    Results.Json(new AuthFailure("인증 데이터베이스를 사용할 수 없습니다."), statusCode: 503);

static IResult InvalidCredentials() =>
    Results.Json(new AuthFailure("계정 ID 또는 비밀번호가 올바르지 않습니다."), statusCode: 401);

static async Task<AuthUser?> LoadUserAsync(
    MySqlConnection connection,
    string accountId,
    CancellationToken cancellationToken)
{
    await using var command = new MySqlCommand(
        """
        SELECT id, account_id, display_name, password_hash, failed_login_count, locked_until_utc,
               seen_survivor_guide, seen_mannequin_guide, seen_initial_tutorial
        FROM users
        WHERE account_id = @accountId
        LIMIT 1;
        """, connection)
    {
        CommandTimeout = 5
    };
    command.Parameters.AddWithValue("@accountId", accountId);
    await using var reader = await command.ExecuteReaderAsync(cancellationToken);
    if (!await reader.ReadAsync(cancellationToken))
    {
        return null;
    }

    return new AuthUser(
        reader.GetUInt64(0),
        reader.GetString(1),
        reader.GetString(2),
        reader.GetString(3),
        reader.GetUInt32(4),
        reader.IsDBNull(5) ? null : DateTime.SpecifyKind(reader.GetDateTime(5), DateTimeKind.Utc),
        reader.GetBoolean(6),
        reader.GetBoolean(7),
        reader.GetBoolean(8));
}

static async Task<TokenPair> CreateSessionAsync(
    MySqlConnection connection,
    MySqlTransaction transaction,
    ulong userId,
    CancellationToken cancellationToken)
{
    var accessToken = CreateToken();
    var refreshToken = CreateToken();
    var accessExpiresAtUtc = DateTime.UtcNow.AddMinutes(AccessTokenMinutes);
    var refreshExpiresAtUtc = DateTime.UtcNow.AddDays(RefreshTokenDays);
    await using var command = new MySqlCommand(
        """
        INSERT INTO auth_sessions
            (user_id, access_token_hash, refresh_token_hash, access_expires_at_utc, refresh_expires_at_utc)
        VALUES
            (@userId, @accessHash, @refreshHash, @accessExpires, @refreshExpires);
        """, connection, transaction)
    {
        CommandTimeout = 5
    };
    command.Parameters.AddWithValue("@userId", userId);
    command.Parameters.Add("@accessHash", MySqlDbType.Binary, 32).Value = HashToken(accessToken);
    command.Parameters.Add("@refreshHash", MySqlDbType.Binary, 32).Value = HashToken(refreshToken);
    command.Parameters.AddWithValue("@accessExpires", accessExpiresAtUtc);
    command.Parameters.AddWithValue("@refreshExpires", refreshExpiresAtUtc);
    await command.ExecuteNonQueryAsync(cancellationToken);
    await CommunityEndpoints.Exec(connection,transaction,$"INSERT INTO auth_session_liveness(session_id,deadline_utc) VALUES(@id,UTC_TIMESTAMP(6)+INTERVAL {ClientSessionLiveness.GraceSeconds} SECOND)",cancellationToken,("id",command.LastInsertedId));
    return new TokenPair(accessToken, refreshToken, accessExpiresAtUtc, refreshExpiresAtUtc);
}

static async Task RevokeSessionAsync(
    MySqlConnection connection,
    MySqlTransaction transaction,
    ulong sessionId,
    CancellationToken cancellationToken)
{
    await using var command = new MySqlCommand(
        "UPDATE auth_sessions SET revoked_at_utc = UTC_TIMESTAMP(6) WHERE id = @sessionId;",
        connection,
        transaction)
    {
        CommandTimeout = 5
    };
    command.Parameters.AddWithValue("@sessionId", sessionId);
    await command.ExecuteNonQueryAsync(cancellationToken);
}

static string CreateToken() => WebEncoders.Base64UrlEncode(RandomNumberGenerator.GetBytes(32));
static byte[] HashToken(string token) => SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(token));

static string? ReadBearerToken(HttpRequest request)
{
    var authorization = request.Headers.Authorization.ToString();
    const string prefix = "Bearer ";
    return authorization.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
        ? authorization[prefix.Length..].Trim()
        : null;
}

internal static class AuthValidation
{
    private static readonly Regex AccountPattern = new("^[A-Za-z0-9_]{3,32}$", RegexOptions.CultureInvariant);

    public static string? Validate(string? accountId, string? password, string? displayName)
    {
        if (string.IsNullOrWhiteSpace(accountId) || !AccountPattern.IsMatch(accountId.Trim()))
        {
            return "계정 ID에는 영문, 숫자, 밑줄 3~32자만 사용할 수 있습니다.";
        }
        if (string.IsNullOrEmpty(password) || password.Length is < 10 or > 128)
        {
            return "비밀번호는 10~128자여야 합니다.";
        }
        if (displayName is not null && (displayName.Trim().Length is < 2 or > 32))
        {
            return "표시 이름은 2~32자여야 합니다.";
        }
        return null;
    }
}

internal static class CrashReportValidation
{
    private static readonly HashSet<string> ProcessRoles = new(StringComparer.Ordinal)
    {
        "Client", "DedicatedServer", "ListenServer", "Standalone"
    };
    private static readonly HashSet<string> PlayerRoles = new(StringComparer.Ordinal)
    {
        "", "Mannequin", "Survivor"
    };

    public static bool TryValidate(CrashReportRequest report, out string error)
    {
        error = "크래시 보고서 형식이 올바르지 않습니다.";
        if (!Guid.TryParse(report.ReportId, out _) ||
            report.DetectedAtUtc == default || report.StartedAtUtc == default || report.LastUpdatedAtUtc == default ||
            !ValidText(report.BuildVersion, 128) || !ValidText(report.MapName, 128) ||
            report.MapName.StartsWith("UEDPIE_", StringComparison.OrdinalIgnoreCase) ||
            !ValidText(report.GameMode, 128) || !ProcessRoles.Contains(report.ProcessRole ?? "") ||
            !PlayerRoles.Contains(report.PlayerRole?.Trim() ?? "") ||
            !ValidText(report.DiagnosticRunId, 128) || !ValidText(report.LastLogPath, 256) ||
            report.LastLogPath.Contains("..", StringComparison.Ordinal) ||
            report.LastLogPath.Contains(':') ||
            (!string.IsNullOrWhiteSpace(report.MatchId) && !Guid.TryParse(report.MatchId, out _)))
        {
            return false;
        }
        error = string.Empty;
        return true;
    }

    private static bool ValidText(string? value, int maximumLength) =>
        !string.IsNullOrWhiteSpace(value) && value.Length <= maximumLength &&
        !value.Contains('\r') && !value.Contains('\n');
}

internal sealed class AuthDatabase
{
    private readonly string? _serverConnectionString;
    private readonly string? _databaseConnectionString;

    public AuthDatabase(string? configuredConnectionString)
    {
        if (string.IsNullOrWhiteSpace(configuredConnectionString))
        {
            return;
        }

        var serverBuilder = new MySqlConnectionStringBuilder(configuredConnectionString)
        {
            Database = string.Empty,
            ConnectionTimeout = 5,
            DefaultCommandTimeout = 5
        };
        _serverConnectionString = serverBuilder.ConnectionString;
        serverBuilder.Database = "projectproject01";
        _databaseConnectionString = serverBuilder.ConnectionString;
    }

    public bool IsConfigured => _databaseConnectionString is not null;

    public async Task InitializeSchemaAsync(string contentRootPath, CancellationToken cancellationToken)
    {
        if (_serverConnectionString is null)
        {
            throw new InvalidOperationException("The MySQL connection string is not configured.");
        }
        var schemaPath = Path.Combine(contentRootPath, "Database", "001_CreateAuthSchema.sql");
        var schemaSql = await File.ReadAllTextAsync(schemaPath, cancellationToken);
        await using var connection = new MySqlConnection(_serverConnectionString);
        await connection.OpenAsync(cancellationToken);
        await using var command = new MySqlCommand(schemaSql, connection) { CommandTimeout = 15 };
        await command.ExecuteNonQueryAsync(cancellationToken);
        var communitySql = await File.ReadAllTextAsync(Path.Combine(contentRootPath, "Database", "002_Community.sql"), cancellationToken);
        await using var communityCommand = new MySqlCommand(communitySql, connection) { CommandTimeout = 15 };
        await communityCommand.ExecuteNonQueryAsync(cancellationToken);
        await using var previewColumn = new MySqlCommand("SELECT COUNT(*) FROM information_schema.columns WHERE table_schema='projectproject01' AND table_name='launcher_posts' AND column_name='preview_image_id'", connection);
        if (Convert.ToInt32(await previewColumn.ExecuteScalarAsync(cancellationToken)) == 0)
        {
            await using var addPreview = new MySqlCommand("ALTER TABLE projectproject01.launcher_posts ADD COLUMN preview_image_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL", connection);
            await addPreview.ExecuteNonQueryAsync(cancellationToken);
        }
        await using var featuredColumn = new MySqlCommand("SELECT COUNT(*) FROM information_schema.columns WHERE table_schema='projectproject01' AND table_name='launcher_posts' AND column_name='featured'", connection);
        if (Convert.ToInt32(await featuredColumn.ExecuteScalarAsync(cancellationToken)) == 0)
        {
            await using var addFeatured = new MySqlCommand("ALTER TABLE projectproject01.launcher_posts ADD COLUMN featured BOOLEAN NOT NULL DEFAULT FALSE", connection);
            await addFeatured.ExecuteNonQueryAsync(cancellationToken);
        }
        await using var summaryColumn = new MySqlCommand("SELECT COUNT(*) FROM information_schema.columns WHERE table_schema='projectproject01' AND table_name='launcher_posts' AND column_name='summary'", connection);
        if (Convert.ToInt32(await summaryColumn.ExecuteScalarAsync(cancellationToken)) == 0)
        {
            await using var addSummary = new MySqlCommand("ALTER TABLE projectproject01.launcher_posts ADD COLUMN summary VARCHAR(500) NOT NULL DEFAULT ''", connection);
            await addSummary.ExecuteNonQueryAsync(cancellationToken);
        }
        await EnsureLobbyJoinCodeSchemaAsync(connection, cancellationToken);
        await EnsureSecuritySchemaAsync(connection, cancellationToken);
        await EnsureGuideProgressSchemaAsync(connection, cancellationToken);
        await EnsureServiceControlScheduleSchemaAsync(connection, cancellationToken);
        await ServiceAnnouncementDelivery.EnsureAsync(connection,cancellationToken);
        await EnsureLeaderboardRoomAndTeamSchemaAsync(connection, cancellationToken);
        await EnsureSurveyReviewSchemaAsync(connection, cancellationToken);
        await LobbyEndpoints.EnsureMatchBotSchemaAsync(connection, cancellationToken);
        await CaseRecords.EnsureAsync(connection,cancellationToken);
        await CaseCollection.EnsureAsync(connection,cancellationToken);
        await MainQuest.EnsureAsync(connection,cancellationToken);
        await CaseDraw.EnsureAsync(connection,cancellationToken);
        await CustomRoomRules.EnsureAsync(connection,cancellationToken);
        await CaseMissionSlots.EnsureAsync(connection,cancellationToken);
        await CaseInvestigation.EnsureAsync(connection,cancellationToken);
        await CaseMissionBudget.EnsureAsync(connection,cancellationToken);
        await OpenParties.EnsureAsync(connection,cancellationToken);
    }

    private static async Task EnsureSurveyReviewSchemaAsync(MySqlConnection connection, CancellationToken ct)
    {
        await connection.ChangeDatabaseAsync("projectproject01", ct);
        foreach (var (name, definition) in new[] {
            ("review_status", "VARCHAR(16) NOT NULL DEFAULT 'Accepted'"),
            ("review_reason", "TEXT NULL") })
        {
            await using var check = new MySqlCommand("SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='player_survey_responses' AND column_name=@name;", connection);
            check.Parameters.AddWithValue("@name", name);
            if (Convert.ToInt32(await check.ExecuteScalarAsync(ct)) == 0)
            {
                await using var alter = new MySqlCommand($"ALTER TABLE player_survey_responses ADD COLUMN {name} {definition};", connection);
                await alter.ExecuteNonQueryAsync(ct);
            }
        }
        await using var normalize = new MySqlCommand("UPDATE player_survey_responses SET review_reason='' WHERE review_reason IS NULL;", connection);
        await normalize.ExecuteNonQueryAsync(ct);
    }

    private static async Task EnsureGuideProgressSchemaAsync(
        MySqlConnection connection,
        CancellationToken cancellationToken)
    {
        await connection.ChangeDatabaseAsync("projectproject01", cancellationToken);
        foreach (var columnName in new[] { "seen_survivor_guide", "seen_mannequin_guide", "seen_initial_tutorial" })
        {
            await using var columnCommand = new MySqlCommand(
                "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = 'projectproject01' AND table_name = 'users' AND column_name = @columnName;",
                connection) { CommandTimeout = 5 };
            columnCommand.Parameters.AddWithValue("@columnName", columnName);
            if (Convert.ToInt32(await columnCommand.ExecuteScalarAsync(cancellationToken)) == 0)
            {
                await using var addColumn = new MySqlCommand(
                    $"ALTER TABLE users ADD COLUMN {columnName} BOOLEAN NOT NULL DEFAULT FALSE AFTER locked_until_utc;",
                    connection) { CommandTimeout = 15 };
                await addColumn.ExecuteNonQueryAsync(cancellationToken);
                if (columnName == "seen_initial_tutorial")
                {
                    await using var backfill = new MySqlCommand(
                        "UPDATE users SET seen_initial_tutorial=TRUE WHERE seen_survivor_guide=TRUE OR seen_mannequin_guide=TRUE;", connection);
                    await backfill.ExecuteNonQueryAsync(cancellationToken);
                }
            }
        }
    }

    private static async Task EnsureLobbyJoinCodeSchemaAsync(
        MySqlConnection connection,
        CancellationToken cancellationToken)
    {
        await connection.ChangeDatabaseAsync("projectproject01", cancellationToken);
        await using (var columnCommand = new MySqlCommand(
            "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = 'projectproject01' AND table_name = 'game_rooms' AND column_name = 'join_code';",
            connection) { CommandTimeout = 5 })
        {
            if (Convert.ToInt32(await columnCommand.ExecuteScalarAsync(cancellationToken)) == 0)
            {
                await using var addColumn = new MySqlCommand(
                    "ALTER TABLE game_rooms ADD COLUMN join_code CHAR(8) CHARACTER SET ascii COLLATE ascii_bin NULL AFTER id;",
                    connection) { CommandTimeout = 15 };
                await addColumn.ExecuteNonQueryAsync(cancellationToken);
            }
        }

        var roomIdsWithoutCode = new List<string>();
        var existingCodes = new HashSet<string>(StringComparer.Ordinal);
        await using (var selectRooms = new MySqlCommand(
            "SELECT id, join_code FROM game_rooms;", connection) { CommandTimeout = 5 })
        {
            await using var reader = await selectRooms.ExecuteReaderAsync(cancellationToken);
            while (await reader.ReadAsync(cancellationToken))
            {
                if (reader.IsDBNull(1) || string.IsNullOrWhiteSpace(reader.GetString(1)))
                {
                    roomIdsWithoutCode.Add(reader.GetValue(0) switch
                    {
                        Guid guid => guid.ToString("D"),
                        string text => text,
                        var value => Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture)
                            ?? throw new InvalidOperationException("기존 방 ID를 읽지 못했습니다.")
                    });
                }
                else
                {
                    existingCodes.Add(reader.GetString(1));
                }
            }
        }
        foreach (var roomId in roomIdsWithoutCode)
        {
            string joinCode;
            do
            {
                joinCode = GenerateJoinCode();
            }
            while (!existingCodes.Add(joinCode));

            await using var updateRoom = new MySqlCommand(
                "UPDATE game_rooms SET join_code = @joinCode WHERE id = @roomId;", connection) { CommandTimeout = 5 };
            updateRoom.Parameters.AddWithValue("@joinCode", joinCode);
            updateRoom.Parameters.AddWithValue("@roomId", roomId);
            await updateRoom.ExecuteNonQueryAsync(cancellationToken);
        }

        await using (var nullableCommand = new MySqlCommand(
            "SELECT is_nullable FROM information_schema.columns WHERE table_schema = 'projectproject01' AND table_name = 'game_rooms' AND column_name = 'join_code' LIMIT 1;",
            connection) { CommandTimeout = 5 })
        {
            if (string.Equals(Convert.ToString(await nullableCommand.ExecuteScalarAsync(cancellationToken)), "YES", StringComparison.Ordinal))
            {
                await using var makeRequired = new MySqlCommand(
                    "ALTER TABLE game_rooms MODIFY COLUMN join_code CHAR(8) CHARACTER SET ascii COLLATE ascii_bin NOT NULL;",
                    connection) { CommandTimeout = 15 };
                await makeRequired.ExecuteNonQueryAsync(cancellationToken);
            }
        }

        await using var indexCommand = new MySqlCommand(
            "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema = 'projectproject01' AND table_name = 'game_rooms' AND index_name = 'uq_game_rooms_join_code';",
            connection) { CommandTimeout = 5 };
        if (Convert.ToInt32(await indexCommand.ExecuteScalarAsync(cancellationToken)) == 0)
        {
            await using var addIndex = new MySqlCommand(
                "ALTER TABLE game_rooms ADD UNIQUE KEY uq_game_rooms_join_code (join_code);",
                connection) { CommandTimeout = 15 };
            await addIndex.ExecuteNonQueryAsync(cancellationToken);
        }
    }

    private static string GenerateJoinCode()
    {
        const string alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
        Span<char> characters = stackalloc char[8];
        for (var index = 0; index < characters.Length; ++index)
        {
            characters[index] = alphabet[RandomNumberGenerator.GetInt32(alphabet.Length)];
        }
        return new string(characters);
    }

    private static async Task EnsureSecuritySchemaAsync(
        MySqlConnection connection,
        CancellationToken cancellationToken)
    {
        await connection.ChangeDatabaseAsync("projectproject01", cancellationToken);
        await using var columnCommand = new MySqlCommand(
            "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = 'projectproject01' AND table_name = 'game_rooms' AND column_name = 'match_id';",
            connection) { CommandTimeout = 5 };
        if (Convert.ToInt32(await columnCommand.ExecuteScalarAsync(cancellationToken)) == 0)
        {
            await using var addColumn = new MySqlCommand(
                "ALTER TABLE game_rooms ADD COLUMN match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL AFTER travel_url;",
                connection) { CommandTimeout = 15 };
            await addColumn.ExecuteNonQueryAsync(cancellationToken);
        }

		await using var returnedColumnCommand = new MySqlCommand(
			"SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = 'projectproject01' AND table_name = 'room_members' AND column_name = 'returned_to_room';",
			connection) { CommandTimeout = 5 };
		if (Convert.ToInt32(await returnedColumnCommand.ExecuteScalarAsync(cancellationToken)) == 0)
		{
			await using var addReturnedColumn = new MySqlCommand(
				"ALTER TABLE room_members ADD COLUMN returned_to_room BOOLEAN NOT NULL DEFAULT FALSE AFTER assigned_role;",
				connection) { CommandTimeout = 15 };
			await addReturnedColumn.ExecuteNonQueryAsync(cancellationToken);
		}
    }

    private static async Task EnsureServiceControlScheduleSchemaAsync(
        MySqlConnection connection,
        CancellationToken cancellationToken)
    {
        await connection.ChangeDatabaseAsync("projectproject01", cancellationToken);
        await using var columnCommand = new MySqlCommand(
            "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = 'projectproject01' AND table_name = 'service_control' AND column_name = 'maintenance_start_at_utc';",
            connection) { CommandTimeout = 5 };
        if (Convert.ToInt32(await columnCommand.ExecuteScalarAsync(cancellationToken)) == 0)
        {
            await using var addColumn = new MySqlCommand(
                "ALTER TABLE service_control ADD COLUMN maintenance_start_at_utc DATETIME(6) NULL AFTER announcement;",
                connection) { CommandTimeout = 15 };
            await addColumn.ExecuteNonQueryAsync(cancellationToken);
        }

        // Preserve a reservation created by the former single-window implementation.
        // The singleton columns remain for wire compatibility, but new reservations live in
        // maintenance_schedules so operators can keep and cancel several windows independently.
        await using var migrateLegacySchedule = new MySqlCommand(
            """
            INSERT INTO maintenance_schedules (starts_at_utc, ends_at_utc, announcement)
            SELECT maintenance_start_at_utc, shutdown_at_utc, announcement
            FROM service_control
            WHERE id = 1
              AND maintenance_start_at_utc IS NOT NULL
              AND shutdown_at_utc IS NOT NULL
              AND shutdown_at_utc > UTC_TIMESTAMP(6)
              AND NOT EXISTS (
                  SELECT 1 FROM maintenance_schedules s
                  WHERE s.starts_at_utc = service_control.maintenance_start_at_utc
                    AND s.ends_at_utc = service_control.shutdown_at_utc
              );

            UPDATE service_control
            SET maintenance_start_at_utc = NULL, shutdown_at_utc = NULL
            WHERE id = 1
              AND (maintenance_start_at_utc IS NOT NULL OR shutdown_at_utc IS NOT NULL);
            """, connection) { CommandTimeout = 15 };
        await migrateLegacySchedule.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task EnsureLeaderboardRoomAndTeamSchemaAsync(
        MySqlConnection connection,
        CancellationToken cancellationToken)
    {
        await connection.ChangeDatabaseAsync("projectproject01", cancellationToken);
        await using var roomNameColumn = new MySqlCommand(
            "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = 'projectproject01' AND table_name = 'leaderboard_records' AND column_name = 'room_name';",
            connection) { CommandTimeout = 5 };
        if (Convert.ToInt32(await roomNameColumn.ExecuteScalarAsync(cancellationToken)) == 0)
        {
            await using var addRoomName = new MySqlCommand(
                "ALTER TABLE leaderboard_records ADD COLUMN room_name VARCHAR(48) NOT NULL DEFAULT '알 수 없는 방' AFTER user_id;",
                connection) { CommandTimeout = 15 };
            await addRoomName.ExecuteNonQueryAsync(cancellationToken);
        }

        await using var backfillRoomNames = new MySqlCommand(
            """
            UPDATE leaderboard_records lr
            INNER JOIN verified_match_results vr
                ON vr.match_id = lr.match_id AND vr.user_id = lr.user_id
            INNER JOIN game_rooms r ON r.id = vr.room_id
            SET lr.room_name = r.name
            WHERE lr.room_name = '알 수 없는 방';
            """, connection) { CommandTimeout = 15 };
        await backfillRoomNames.ExecuteNonQueryAsync(cancellationToken);

        // Keep the legacy column/API name for packaged-client compatibility, but store the
        // team's total rescue count and the last (maximum) successful-survivor escape time.
        await using var refreshTeamAggregates = new MySqlCommand(
            """
            UPDATE leaderboard_survivor_team_records team
            INNER JOIN
            (
                SELECT vr.match_id, COUNT(*) AS survivor_count,
                       SUM(vr.rescue_count) AS total_rescue_count,
                       MAX(vr.escape_seconds) AS last_escape_seconds
                FROM verified_match_results vr
                WHERE vr.role = 'Survivor' AND vr.success = TRUE
                GROUP BY vr.match_id
            ) aggregate ON aggregate.match_id = team.match_id
            SET team.survivor_count = aggregate.survivor_count,
                team.average_rescue_count = aggregate.total_rescue_count,
                team.average_escape_seconds = aggregate.last_escape_seconds,
                team.updated_at_utc = UTC_TIMESTAMP(6);
            """, connection) { CommandTimeout = 15 };
        await refreshTeamAggregates.ExecuteNonQueryAsync(cancellationToken);
    }

    public async Task<MySqlConnection> OpenConnectionAsync(CancellationToken cancellationToken)
    {
        if (_databaseConnectionString is null)
        {
            throw new InvalidOperationException("The MySQL connection string is not configured.");
        }
        var connection = new MySqlConnection(_databaseConnectionString);
        try
        {
            await connection.OpenAsync(cancellationToken);
            return connection;
        }
        catch(Exception exception)
        {
            BackendDiagnostics.Shared.Failure("DB",cancellationToken.IsCancellationRequested?"cancellation":
                exception is TimeoutException||exception.InnerException is TimeoutException?"timeout":"connection-failure");
            await connection.DisposeAsync();
            throw;
        }
    }
}

internal sealed record RegisterRequest(string AccountId, string Password, string? DisplayName, bool LoginAfterRegistration = true);
internal sealed record LoginRequest(string AccountId, string Password);
internal sealed record RefreshRequest(string RefreshToken);
internal sealed record GuideProgressRequest(string? Role);
internal sealed record CrashReportRequest(
    string ReportId,
    DateTime DetectedAtUtc,
    DateTime StartedAtUtc,
    DateTime LastUpdatedAtUtc,
    string BuildVersion,
    string MapName,
    string GameMode,
    string ProcessRole,
    string? PlayerRole,
    string LastLogPath,
    string DiagnosticRunId,
    string? MatchId);
internal sealed record AuthFailure(string Message);
internal sealed record AuthSuccess(
    string Message,
    string DisplayName,
    string AccessToken,
    string RefreshToken,
    DateTime AccessExpiresAtUtc,
    DateTime RefreshExpiresAtUtc,
    bool SeenSurvivorGuide,
    bool SeenMannequinGuide,
    bool SeenInitialTutorial = false);
internal sealed record AuthUser(
    ulong Id,
    string AccountId,
    string DisplayName,
    string PasswordHash,
    uint FailedLoginCount,
    DateTime? LockedUntilUtc,
    bool SeenSurvivorGuide,
    bool SeenMannequinGuide,
    bool SeenInitialTutorial = false);
internal sealed record TokenPair(
    string AccessToken,
    string RefreshToken,
    DateTime AccessExpiresAtUtc,
    DateTime RefreshExpiresAtUtc);

internal sealed class ProjectSecurityOptions
{
    private readonly byte[]? _ticketKey;

    private ProjectSecurityOptions(
        string gameServerSharedSecret,
        byte[]? ticketKey,
        bool allowUnverifiedLeaderboardSubmissions)
    {
        GameServerSharedSecret = gameServerSharedSecret;
        _ticketKey = ticketKey;
        AllowUnverifiedLeaderboardSubmissions = allowUnverifiedLeaderboardSubmissions;
    }

    public string GameServerSharedSecret { get; }
    public bool AllowUnverifiedLeaderboardSubmissions { get; }
    public bool IsGameServerSecurityConfigured =>
        GameServerSharedSecret.Length >= 32 && _ticketKey is { Length: >= 32 };

    public static ProjectSecurityOptions FromConfiguration(IConfiguration configuration)
    {
        var sharedSecret = configuration["GameServer:SharedSecret"]?.Trim()
            ?? Environment.GetEnvironmentVariable("PROJECTPROJECT01_GAME_SERVER_SECRET")?.Trim()
            ?? string.Empty;
        var ticketKeyText = configuration["GameServer:TicketKey"]?.Trim()
            ?? Environment.GetEnvironmentVariable("PROJECTPROJECT01_TICKET_KEY")?.Trim()
            ?? string.Empty;
        byte[]? ticketKey = null;
        if (!string.IsNullOrWhiteSpace(ticketKeyText))
        {
            try
            {
                ticketKey = Convert.FromBase64String(ticketKeyText);
            }
            catch (FormatException)
            {
                ticketKey = null;
            }
        }

        return new ProjectSecurityOptions(
            sharedSecret,
            ticketKey,
            configuration.GetValue("Security:AllowUnverifiedLeaderboardSubmissions", false));
    }

    public bool IsAuthorizedGameServer(string? suppliedSecret)
    {
        if (!IsGameServerSecurityConfigured || string.IsNullOrEmpty(suppliedSecret))
        {
            return false;
        }
        var expected = Encoding.UTF8.GetBytes(GameServerSharedSecret);
        var supplied = Encoding.UTF8.GetBytes(suppliedSecret);
        return expected.Length == supplied.Length && CryptographicOperations.FixedTimeEquals(expected, supplied);
    }

    public byte[] DeriveConnectionEncryptionKey(string gameTicket)
    {
        if (_ticketKey is not { Length: >= 32 })
        {
            throw new InvalidOperationException("GameServer:TicketKey is not configured.");
        }
        using var hmac = new HMACSHA256(_ticketKey);
        return hmac.ComputeHash(Encoding.UTF8.GetBytes(gameTicket));
    }
}

internal static class SecurityAudit
{
    public static async Task WriteAsync(
        AuthDatabase database,
        HttpRequest request,
        string eventType,
        string outcome,
        ulong? userId,
        string? details,
        CancellationToken cancellationToken)
    {
        if (!database.IsConfigured)
        {
            return;
        }
        try
        {
            await using var connection = await database.OpenConnectionAsync(cancellationToken);
            await using var command = new MySqlCommand(
                "INSERT INTO security_audit_events (user_id, event_type, outcome, remote_address, details) VALUES (@userId, @eventType, @outcome, @remoteAddress, @details);",
                connection) { CommandTimeout = 5 };
            command.Parameters.AddWithValue("@userId", userId.HasValue ? userId.Value : DBNull.Value);
            command.Parameters.AddWithValue("@eventType", eventType.Length <= 64 ? eventType : eventType[..64]);
            command.Parameters.AddWithValue("@outcome", outcome.Length <= 32 ? outcome : outcome[..32]);
            command.Parameters.AddWithValue("@remoteAddress", RequestSecurity.GetClientAddress(request.HttpContext));
            var safeDetails = details ?? string.Empty;
            command.Parameters.AddWithValue("@details", safeDetails.Length <= 512 ? safeDetails : safeDetails[..512]);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }
        catch (Exception)
        {
            // 감사 로그 실패가 인증 또는 게임 요청의 성공/실패를 바꾸지 않도록 한다.
        }
    }
}

internal static class RequestSecurity
{
    public static bool IsAllowedInsecureTestHttp(HttpContext context, string? allowedVpnHost)
    {
        var remoteAddress = context.Connection.RemoteIpAddress?.MapToIPv4();
        var localAddress = context.Connection.LocalIpAddress?.MapToIPv4();
        if (remoteAddress is null || localAddress is null ||
            !string.Equals(context.Request.Host.Host, localAddress.ToString(), StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var localBytes = localAddress.GetAddressBytes();
        var remoteBytes = remoteAddress.GetAddressBytes();
        if (localBytes[0] == 25)
        {
            return !string.IsNullOrWhiteSpace(allowedVpnHost) && remoteBytes[0] == 25 &&
                string.Equals(localAddress.ToString(), allowedVpnHost, StringComparison.OrdinalIgnoreCase);
        }
        return IsPrivateIpv4(localBytes) && IsPrivateIpv4(remoteBytes) && IsOnSameLocalSubnet(localAddress, remoteAddress);
    }

    private static bool IsPrivateIpv4(byte[] bytes) =>
        bytes[0] == 10 || (bytes[0] == 172 && bytes[1] is >= 16 and <= 31) ||
        (bytes[0] == 192 && bytes[1] == 168);

    private static bool IsOnSameLocalSubnet(IPAddress localAddress, IPAddress remoteAddress)
    {
        var localBytes = localAddress.GetAddressBytes();
        var remoteBytes = remoteAddress.GetAddressBytes();
        var prefixLength = NetworkInterface.GetAllNetworkInterfaces()
            .Where(network => network.OperationalStatus == OperationalStatus.Up)
            .SelectMany(network => network.GetIPProperties().UnicastAddresses)
            .Where(unicast => unicast.Address.AddressFamily == AddressFamily.InterNetwork &&
                unicast.Address.Equals(localAddress))
            .Select(unicast => unicast.PrefixLength)
            .FirstOrDefault(24);
        var wholeBytes = prefixLength / 8;
        var remainingBits = prefixLength % 8;
        for (var index = 0; index < wholeBytes; index++)
        {
            if (localBytes[index] != remoteBytes[index]) return false;
        }
        if (remainingBits == 0) return true;
        var mask = (byte)(0xff << (8 - remainingBits));
        return (localBytes[wholeBytes] & mask) == (remoteBytes[wholeBytes] & mask);
    }

    public static string GetClientAddress(HttpContext context)
    {
        var directAddress = context.Connection.RemoteIpAddress;
        if (directAddress is not null && IPAddress.IsLoopback(directAddress) &&
            context.Request.Headers.TryGetValue("X-Forwarded-For", out var forwardedValues))
        {
            var firstForwardedAddress = forwardedValues.ToString().Split(',', 2)[0].Trim();
            if (IPAddress.TryParse(firstForwardedAddress, out var parsedAddress))
            {
                return parsedAddress.ToString();
            }
        }
        return directAddress?.ToString() ?? "unknown";
    }
}
