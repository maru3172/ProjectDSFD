// File: Tools/ProjectProject01Backend/Program.cs
// Target: .NET 9 / MySQL Server 8.0

using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.Identity;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.AspNetCore.WebUtilities;
using MySqlConnector;

const int MaximumFailedLoginCount = 5;
const int AccountLockMinutes = 15;
const int AccessTokenMinutes = 30;
const int RefreshTokenDays = 30;

var builder = WebApplication.CreateBuilder(args);
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
builder.Logging.ClearProviders();
builder.Logging.AddConsole();

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
    options.AddPolicy("auth", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            context.Connection.RemoteIpAddress?.ToString() ?? "unknown",
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = 10,
                Window = TimeSpan.FromMinutes(1),
                QueueLimit = 0,
                AutoReplenishment = true
            }));
    options.AddPolicy("lobby", context =>
        RateLimitPartition.GetFixedWindowLimiter(
            context.Connection.RemoteIpAddress?.ToString() ?? "unknown",
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = 600,
                Window = TimeSpan.FromMinutes(1),
                QueueLimit = 0,
                AutoReplenishment = true
            }));
});

var app = builder.Build();
app.UseRateLimiter();

var startupDatabase = app.Services.GetRequiredService<AuthDatabase>();
if (startupDatabase.IsConfigured)
{
    try
    {
        await startupDatabase.InitializeSchemaAsync(app.Environment.ContentRootPath, app.Lifetime.ApplicationStopping);
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

app.MapGet("/health", async (AuthDatabase database, CancellationToken cancellationToken) =>
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
        return Results.Ok(new { status = "healthy" });
    }
    catch
    {
        return Results.Json(new { status = "unavailable", message = "MySQL connection failed." }, statusCode: 503);
    }
});

app.MapPost("/api/auth/register", async (
    RegisterRequest request,
    AuthDatabase database,
    IPasswordHasher<AuthUser> passwordHasher,
    CancellationToken cancellationToken) =>
{
    var validationError = AuthValidation.Validate(request.AccountId, request.Password, request.DisplayName);
    if (validationError is not null)
    {
        return Results.BadRequest(new AuthFailure(validationError));
    }
    if (!database.IsConfigured)
    {
        return DatabaseUnavailable();
    }

    var accountId = request.AccountId.Trim();
    var displayName = request.DisplayName.Trim();
    var user = new AuthUser(0, accountId, displayName, string.Empty, 0, null);
    var passwordHash = passwordHasher.HashPassword(user, request.Password);

    try
    {
        await using var connection = await database.OpenConnectionAsync(cancellationToken);
        await using var transaction = await connection.BeginTransactionAsync(cancellationToken);
        await using var insertUser = new MySqlCommand(
            """
            INSERT INTO users (account_id, display_name, password_hash)
            VALUES (@accountId, @displayName, @passwordHash);
            """, connection, transaction)
        {
            CommandTimeout = 5
        };
        insertUser.Parameters.AddWithValue("@accountId", accountId);
        insertUser.Parameters.AddWithValue("@displayName", displayName);
        insertUser.Parameters.AddWithValue("@passwordHash", passwordHash);
        await insertUser.ExecuteNonQueryAsync(cancellationToken);

        var userId = checked((ulong)insertUser.LastInsertedId);
        var tokens = await CreateSessionAsync(connection, transaction, userId, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return Results.Created("/api/auth/me", new AuthSuccess(
            "계정이 생성되었습니다.", displayName, tokens.AccessToken, tokens.RefreshToken,
            tokens.AccessExpiresAtUtc, tokens.RefreshExpiresAtUtc));
    }
    catch (MySqlException exception) when (exception.Number == 1062)
    {
        return Results.Conflict(new AuthFailure("이미 사용 중인 계정 ID입니다."));
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("auth");

app.MapPost("/api/auth/login", async (
    LoginRequest request,
    AuthDatabase database,
    IPasswordHasher<AuthUser> passwordHasher,
    CancellationToken cancellationToken) =>
{
    var validationError = AuthValidation.Validate(request.AccountId, request.Password, null);
    if (validationError is not null)
    {
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
        if (user is null || (user.LockedUntilUtc.HasValue && user.LockedUntilUtc.Value > DateTime.UtcNow))
        {
            return InvalidCredentials();
        }

        var verifyResult = passwordHasher.VerifyHashedPassword(user, user.PasswordHash, request.Password);
        if (verifyResult == PasswordVerificationResult.Failed)
        {
            await RecordFailedLoginAsync(connection, user.Id, user.FailedLoginCount + 1, cancellationToken);
            return InvalidCredentials();
        }

        await using var transaction = await connection.BeginTransactionAsync(cancellationToken);
        await using (var resetFailure = new MySqlCommand(
            "UPDATE users SET failed_login_count = 0, locked_until_utc = NULL WHERE id = @userId;",
            connection,
            transaction))
        {
            resetFailure.CommandTimeout = 5;
            resetFailure.Parameters.AddWithValue("@userId", user.Id);
            await resetFailure.ExecuteNonQueryAsync(cancellationToken);
        }

        var tokens = await CreateSessionAsync(connection, transaction, user.Id, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return Results.Ok(new AuthSuccess(
            "로그인되었습니다.", user.DisplayName, tokens.AccessToken, tokens.RefreshToken,
            tokens.AccessExpiresAtUtc, tokens.RefreshExpiresAtUtc));
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("auth");

app.MapPost("/api/auth/refresh", async (
    RefreshRequest request,
    AuthDatabase database,
    CancellationToken cancellationToken) =>
{
    if (string.IsNullOrWhiteSpace(request.RefreshToken) || request.RefreshToken.Length > 512)
    {
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
        await using (var select = new MySqlCommand(
            """
            SELECT s.id, s.user_id, u.display_name
            FROM auth_sessions s
            INNER JOIN users u ON u.id = s.user_id
            WHERE s.refresh_token_hash = @refreshHash
              AND s.revoked_at_utc IS NULL
              AND s.refresh_expires_at_utc > UTC_TIMESTAMP(6)
            FOR UPDATE;
            """, connection, transaction))
        {
            select.CommandTimeout = 5;
            select.Parameters.Add("@refreshHash", MySqlDbType.Binary, 32).Value = refreshHash;
            await using var reader = await select.ExecuteReaderAsync(cancellationToken);
            if (!await reader.ReadAsync(cancellationToken))
            {
                await transaction.RollbackAsync(cancellationToken);
                return Results.Unauthorized();
            }
            sessionId = reader.GetUInt64(0);
            userId = reader.GetUInt64(1);
            displayName = reader.GetString(2);
        }

        await RevokeSessionAsync(connection, transaction, sessionId, cancellationToken);
        var tokens = await CreateSessionAsync(connection, transaction, userId, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return Results.Ok(new AuthSuccess(
            "세션이 갱신되었습니다.", displayName, tokens.AccessToken, tokens.RefreshToken,
            tokens.AccessExpiresAtUtc, tokens.RefreshExpiresAtUtc));
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("auth");

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
        await using var command = new MySqlCommand(
            "UPDATE auth_sessions SET revoked_at_utc = UTC_TIMESTAMP(6) WHERE access_token_hash = @accessHash AND revoked_at_utc IS NULL;",
            connection)
        {
            CommandTimeout = 5
        };
        command.Parameters.Add("@accessHash", MySqlDbType.Binary, 32).Value = HashToken(accessToken);
        await command.ExecuteNonQueryAsync(cancellationToken);
        return Results.Ok(new { message = "로그아웃되었습니다." });
    }
    catch (MySqlException)
    {
        return DatabaseUnavailable();
    }
}).RequireRateLimiting("auth");

app.MapProjectProject01Lobby(builder.Configuration["Lobby:GameServerTravelUrl"] ?? "127.0.0.1:7777");

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
        SELECT id, account_id, display_name, password_hash, failed_login_count, locked_until_utc
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
        reader.IsDBNull(5) ? null : DateTime.SpecifyKind(reader.GetDateTime(5), DateTimeKind.Utc));
}

static async Task RecordFailedLoginAsync(
    MySqlConnection connection,
    ulong userId,
    uint failedLoginCount,
    CancellationToken cancellationToken)
{
    var shouldLock = failedLoginCount >= MaximumFailedLoginCount;
    await using var command = new MySqlCommand(
        """
        UPDATE users
        SET failed_login_count = @failedLoginCount,
            locked_until_utc = CASE WHEN @shouldLock THEN @lockedUntilUtc ELSE locked_until_utc END
        WHERE id = @userId;
        """, connection)
    {
        CommandTimeout = 5
    };
    command.Parameters.AddWithValue("@failedLoginCount", failedLoginCount);
    command.Parameters.AddWithValue("@shouldLock", shouldLock);
    command.Parameters.AddWithValue("@lockedUntilUtc", DateTime.UtcNow.AddMinutes(AccountLockMinutes));
    command.Parameters.AddWithValue("@userId", userId);
    await command.ExecuteNonQueryAsync(cancellationToken);
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
    }

    public async Task<MySqlConnection> OpenConnectionAsync(CancellationToken cancellationToken)
    {
        if (_databaseConnectionString is null)
        {
            throw new InvalidOperationException("The MySQL connection string is not configured.");
        }
        var connection = new MySqlConnection(_databaseConnectionString);
        await connection.OpenAsync(cancellationToken);
        return connection;
    }
}

internal sealed record RegisterRequest(string AccountId, string Password, string DisplayName);
internal sealed record LoginRequest(string AccountId, string Password);
internal sealed record RefreshRequest(string RefreshToken);
internal sealed record AuthFailure(string Message);
internal sealed record AuthSuccess(
    string Message,
    string DisplayName,
    string AccessToken,
    string RefreshToken,
    DateTime AccessExpiresAtUtc,
    DateTime RefreshExpiresAtUtc);
internal sealed record AuthUser(
    ulong Id,
    string AccountId,
    string DisplayName,
    string PasswordHash,
    uint FailedLoginCount,
    DateTime? LockedUntilUtc);
internal sealed record TokenPair(
    string AccessToken,
    string RefreshToken,
    DateTime AccessExpiresAtUtc,
    DateTime RefreshExpiresAtUtc);
