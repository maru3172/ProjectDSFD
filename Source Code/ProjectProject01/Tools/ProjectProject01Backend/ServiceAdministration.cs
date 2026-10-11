// File: Tools/ProjectProject01Backend/ServiceAdministration.cs
// Server-operator-only maintenance, announcement, and session revocation support.

using System.Security.Cryptography;
using System.Text;
using MySqlConnector;

internal sealed record ServiceControlSnapshot(
    bool MaintenanceEnabled,
    string Announcement,
    DateTime? ShutdownAtUtc,
    ulong Revision,
    DateTime UpdatedAtUtc)
{
    public object ToResponse() => new
    {
        maintenanceEnabled = MaintenanceEnabled,
        announcement = Announcement,
        shutdownAtUtc = ShutdownAtUtc,
        revision = Revision,
        updatedAtUtc = UpdatedAtUtc,
        message = MaintenanceEnabled
            ? string.IsNullOrWhiteSpace(Announcement)
                ? "현재 멀티플레이 서버를 점검 중입니다. 싱글플레이는 정상 이용할 수 있습니다."
                : Announcement
            : Announcement
    };
}

internal sealed class ServiceControlState
{
    private readonly object gate = new();
    private ServiceControlSnapshot snapshot = new(false, string.Empty, null, 1, DateTime.UtcNow);

    public ServiceControlSnapshot Get()
    {
        lock (gate) return snapshot;
    }

    public void Set(ServiceControlSnapshot value)
    {
        lock (gate) snapshot = value;
    }
}

internal sealed class AdministratorOptions
{
    private AdministratorOptions(string apiKey) => ApiKey = apiKey;
    public string ApiKey { get; }
    public bool IsConfigured => ApiKey.Length >= 32;

    public static AdministratorOptions FromConfiguration(IConfiguration configuration) => new(
        configuration["Administrator:ApiKey"]?.Trim()
        ?? Environment.GetEnvironmentVariable("PROJECTPROJECT01_ADMIN_API_KEY")?.Trim()
        ?? string.Empty);

    public bool IsAuthorized(string? supplied)
    {
        if (!IsConfigured || string.IsNullOrEmpty(supplied)) return false;
        var expectedBytes = Encoding.UTF8.GetBytes(ApiKey);
        var suppliedBytes = Encoding.UTF8.GetBytes(supplied);
        return expectedBytes.Length == suppliedBytes.Length &&
               CryptographicOperations.FixedTimeEquals(expectedBytes, suppliedBytes);
    }
}

internal sealed record UpdateServiceControlRequest(
    bool MaintenanceEnabled,
    string? Announcement,
    DateTime? ShutdownAtUtc);

internal static class ServiceControlStore
{
    public static async Task<ServiceControlSnapshot> LoadAsync(AuthDatabase database, CancellationToken ct)
    {
        await using var connection = await database.OpenConnectionAsync(ct);
        await using var command = new MySqlCommand(
            "SELECT maintenance_enabled, announcement, shutdown_at_utc, revision, updated_at_utc FROM service_control WHERE id = 1;",
            connection) { CommandTimeout = 5 };
        await using var reader = await command.ExecuteReaderAsync(ct);
        if (!await reader.ReadAsync(ct))
            return new ServiceControlSnapshot(false, string.Empty, null, 1, DateTime.UtcNow);
        return new ServiceControlSnapshot(
            reader.GetBoolean(0), reader.GetString(1), reader.IsDBNull(2) ? null : reader.GetDateTime(2),
            reader.GetUInt64(3), DateTime.SpecifyKind(reader.GetDateTime(4), DateTimeKind.Utc));
    }

    public static async Task<ServiceControlSnapshot> UpdateAsync(
        AuthDatabase database, UpdateServiceControlRequest request, CancellationToken ct)
    {
        var announcement = (request.Announcement ?? string.Empty).Trim();
        if (announcement.Length > 512) announcement = announcement[..512];
        var shutdown = request.ShutdownAtUtc?.ToUniversalTime();
        await using var connection = await database.OpenConnectionAsync(ct);
        await using var command = new MySqlCommand(
            """
            UPDATE service_control
            SET maintenance_enabled = @maintenance, announcement = @announcement,
                shutdown_at_utc = @shutdown, revision = revision + 1
            WHERE id = 1;
            """, connection) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@maintenance", request.MaintenanceEnabled);
        command.Parameters.AddWithValue("@announcement", announcement);
        command.Parameters.AddWithValue("@shutdown", shutdown.HasValue ? shutdown.Value : DBNull.Value);
        await command.ExecuteNonQueryAsync(ct);
        return await LoadAsync(database, ct);
    }
}

internal static class ServiceAdministration
{
    public static IApplicationBuilder UseProjectProject01ServiceControl(this IApplicationBuilder app) =>
        app.Use(async (context, next) =>
        {
            var snapshot = context.RequestServices.GetRequiredService<ServiceControlState>().Get();
            var path = context.Request.Path;
            var blocksLogin = context.Request.Method == HttpMethods.Post &&
                (path.Equals("/api/auth/login") || path.Equals("/api/auth/register"));
            var blocksRoomCreation = context.Request.Method == HttpMethods.Post && path.Equals("/api/rooms");
            if (snapshot.MaintenanceEnabled && (blocksLogin || blocksRoomCreation))
            {
                context.Response.StatusCode = StatusCodes.Status503ServiceUnavailable;
                await context.Response.WriteAsJsonAsync(new
                {
                    code = "Maintenance",
                    message = string.IsNullOrWhiteSpace(snapshot.Announcement)
                        ? "현재 멀티플레이 서버를 점검 중입니다. 싱글플레이는 정상 이용할 수 있습니다."
                        : snapshot.Announcement,
                    maintenanceEnabled = true,
                    announcement = snapshot.Announcement,
                    shutdownAtUtc = snapshot.ShutdownAtUtc,
                    revision = snapshot.Revision
                });
                return;
            }
            await next();
        });

    public static void MapProjectProject01ServiceAdministration(this WebApplication app)
    {
        app.MapGet("/api/service/status", (ServiceControlState state) => Results.Ok(state.Get().ToResponse()));

        app.MapGet("/api/admin/service", (HttpRequest request, ServiceControlState state, AdministratorOptions admin) =>
            IsAuthorized(request, admin) ? Results.Ok(state.Get().ToResponse()) : Results.Unauthorized());

        app.MapPut("/api/admin/service", async (
            HttpRequest request, UpdateServiceControlRequest body, AuthDatabase database,
            ServiceControlState state, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            var updated = await ServiceControlStore.UpdateAsync(database, body, ct);
            state.Set(updated);
            await SecurityAudit.WriteAsync(database, request, "AdminServiceControl", "Updated", null,
                $"Maintenance={updated.MaintenanceEnabled}; Revision={updated.Revision}", ct);
            return Results.Ok(updated.ToResponse());
        }).RequireRateLimiting("admin");

        app.MapPost("/api/admin/sessions/revoke-all", async (
            HttpRequest request, AuthDatabase database, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var transaction = await connection.BeginTransactionAsync(ct);
            await using var command = new MySqlCommand(
                "UPDATE auth_sessions SET revoked_at_utc = UTC_TIMESTAMP(6) WHERE revoked_at_utc IS NULL;",
                connection, transaction) { CommandTimeout = 5 };
            var count = await command.ExecuteNonQueryAsync(ct);
            await using var deleteRooms = new MySqlCommand(
                "DELETE FROM game_rooms;", connection, transaction) { CommandTimeout = 5 };
            var deletedRoomCount = await deleteRooms.ExecuteNonQueryAsync(ct);
            await transaction.CommitAsync(ct);
            await SecurityAudit.WriteAsync(database, request, "AdminForceLogout", "AllSessions", null,
                $"Revoked={count}; DeletedRooms={deletedRoomCount}", ct);
            return Results.Ok(new
            {
                message = "모든 멀티플레이 로그인 세션을 폐기하고 남은 방을 안전하게 삭제했습니다.",
                revokedSessionCount = count,
                deletedRoomCount
            });
        }).RequireRateLimiting("admin");

        app.MapPost("/api/admin/users/{accountId}/sessions/revoke", async (
            string accountId, HttpRequest request, AuthDatabase database, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var transaction = await connection.BeginTransactionAsync(ct);
            ulong? userId = null;
            await using (var selectUser = new MySqlCommand(
                "SELECT id FROM users WHERE account_id = @accountId LIMIT 1 FOR UPDATE;",
                connection, transaction) { CommandTimeout = 5 })
            {
                selectUser.Parameters.AddWithValue("@accountId", accountId.Trim());
                var value = await selectUser.ExecuteScalarAsync(ct);
                if (value is not null and not DBNull)
                {
                    userId = Convert.ToUInt64(value, System.Globalization.CultureInfo.InvariantCulture);
                }
            }
            await using var command = new MySqlCommand(
                """
                UPDATE auth_sessions s INNER JOIN users u ON u.id = s.user_id
                SET s.revoked_at_utc = UTC_TIMESTAMP(6)
                WHERE u.account_id = @accountId AND s.revoked_at_utc IS NULL;
                """, connection, transaction) { CommandTimeout = 5 };
            command.Parameters.AddWithValue("@accountId", accountId.Trim());
            var count = await command.ExecuteNonQueryAsync(ct);
            if (userId is ulong selectedUserId)
            {
                await LobbyEndpoints.RemoveUserFromCurrentRoomAsync(connection, transaction, selectedUserId, ct);
            }
            await transaction.CommitAsync(ct);
            await SecurityAudit.WriteAsync(database, request, "AdminForceLogout", "AccountSessions", null,
                $"AccountId={accountId.Trim()}; Revoked={count}", ct);
            return Results.Ok(new { message = "선택한 계정의 멀티플레이 세션을 폐기했습니다.", revokedSessionCount = count });
        }).RequireRateLimiting("admin");
    }

    private static bool IsAuthorized(HttpRequest request, AdministratorOptions options) =>
        options.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"].ToString());
}
