// File: Tools/ProjectProject01Backend/ServiceAdministration.cs
// Server-operator-only maintenance, announcement, and session revocation support.

using System.Security.Cryptography;
using System.Text;
using MySqlConnector;

internal sealed record ServiceControlSnapshot(
    bool MaintenanceEnabled,
    bool ManualMaintenanceEnabled,
    string Announcement,
    string BaseAnnouncement,
    DateTime? MaintenanceStartAtUtc,
    DateTime? ShutdownAtUtc,
    ulong Revision,
    DateTime UpdatedAtUtc)
{
    public object ToResponse() => new
    {
        maintenanceEnabled = MaintenanceEnabled,
        manualMaintenanceEnabled = ManualMaintenanceEnabled,
        announcement = Announcement,
        baseAnnouncement = BaseAnnouncement,
        maintenanceStartAtUtc = MaintenanceStartAtUtc,
        shutdownAtUtc = ShutdownAtUtc,
        revision = Revision,
        updatedAtUtc = UpdatedAtUtc,
        message = MaintenanceEnabled
            ? string.IsNullOrWhiteSpace(Announcement)
                ? "현재 멀티플레이 서버를 점검 중입니다. 싱글플레이는 정상 이용할 수 있습니다."
                : Announcement
            : MaintenanceStartAtUtc is DateTime scheduledStart && scheduledStart > DateTime.UtcNow
                ? string.IsNullOrWhiteSpace(Announcement)
                    ? $"멀티플레이 서버 점검이 {scheduledStart:O}에 시작될 예정입니다."
                    : Announcement
                : Announcement
    };
}

internal sealed class ServiceControlState
{
    private readonly object gate = new();
    private ServiceControlSnapshot snapshot = new(false, false, string.Empty, string.Empty, null, null, 1, DateTime.UtcNow);

    public ServiceControlSnapshot Get()
    {
        lock (gate) return snapshot;
    }

    public void Set(ServiceControlSnapshot value)
    {
        lock (gate) snapshot = value;
    }
}

internal sealed class ServiceControlScheduleWorker(
    AuthDatabase database,
    ServiceControlState state,
    ILogger<ServiceControlScheduleWorker> logger) : BackgroundService
{
    private static readonly TimeSpan IdlePollInterval = TimeSpan.FromSeconds(1);

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                var previous = state.Get();
                var refreshed = await ServiceControlStore.LoadAsync(database, stoppingToken);
                if (refreshed != previous)
                {
                    state.Set(refreshed);
                    logger.LogInformation(
                        "Scheduled maintenance state refreshed. Maintenance={Maintenance}; Window={Start}..{End}; Revision={Revision}",
                        refreshed.MaintenanceEnabled,
                        refreshed.MaintenanceStartAtUtc,
                        refreshed.ShutdownAtUtc,
                        refreshed.Revision);
                }
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception exception)
            {
                logger.LogWarning(exception, "Failed to evaluate the scheduled maintenance window.");
            }

            await Task.Delay(IdlePollInterval, stoppingToken);
        }
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
    DateTime? MaintenanceStartAtUtc,
    DateTime? ShutdownAtUtc);

internal sealed record CreateMaintenanceScheduleRequest(
    DateTime StartsAtUtc,
    DateTime EndsAtUtc,
    string? Announcement);

internal sealed record MaintenanceScheduleSnapshot(
    ulong Id,
    DateTime StartsAtUtc,
    DateTime EndsAtUtc,
    string Announcement,
    DateTime CreatedAtUtc)
{
    public object ToResponse(DateTime nowUtc) => new
    {
        id = Id,
        startsAtUtc = StartsAtUtc,
        endsAtUtc = EndsAtUtc,
        announcement = Announcement,
        createdAtUtc = CreatedAtUtc,
        status = StartsAtUtc <= nowUtc && EndsAtUtc > nowUtc ? "Active" : "Scheduled"
    };
}

internal static class ServiceControlStore
{
    public static async Task<ServiceControlSnapshot> LoadAsync(AuthDatabase database, CancellationToken ct)
    {
        await using var connection = await database.OpenConnectionAsync(ct);
        bool manualMaintenance;
        string baseAnnouncement;
        ulong revision;
        DateTime updatedAtUtc;
        await using (var command = new MySqlCommand(
            "SELECT maintenance_enabled, announcement, revision, updated_at_utc FROM service_control WHERE id = 1;",
            connection) { CommandTimeout = 5 })
        await using (var reader = await command.ExecuteReaderAsync(ct))
        {
            if (!await reader.ReadAsync(ct))
                return new ServiceControlSnapshot(false, false, string.Empty, string.Empty, null, null, 1, DateTime.UtcNow);
            manualMaintenance = reader.GetBoolean(0);
            baseAnnouncement = reader.GetString(1);
            revision = reader.GetUInt64(2);
            updatedAtUtc = DateTime.SpecifyKind(reader.GetDateTime(3), DateTimeKind.Utc);
        }

        MaintenanceScheduleSnapshot? currentWindow = null;
        await using (var scheduleCommand = new MySqlCommand(
            """
            SELECT id, starts_at_utc, ends_at_utc, announcement, created_at_utc
            FROM maintenance_schedules
            WHERE ends_at_utc > UTC_TIMESTAMP(6)
            ORDER BY CASE WHEN starts_at_utc <= UTC_TIMESTAMP(6) THEN 0 ELSE 1 END,
                     starts_at_utc, id
            LIMIT 1;
            """, connection) { CommandTimeout = 5 })
        await using (var reader = await scheduleCommand.ExecuteReaderAsync(ct))
        {
            if (await reader.ReadAsync(ct)) currentWindow = ReadSchedule(reader);
        }

        var now = DateTime.UtcNow;
        var scheduledMaintenance = currentWindow is not null &&
            currentWindow.StartsAtUtc <= now && currentWindow.EndsAtUtc > now;
        var effectiveAnnouncement = scheduledMaintenance && !string.IsNullOrWhiteSpace(currentWindow!.Announcement)
            ? currentWindow.Announcement
            : baseAnnouncement;
        return new ServiceControlSnapshot(
            manualMaintenance || scheduledMaintenance,
            manualMaintenance,
            effectiveAnnouncement,
            baseAnnouncement,
            currentWindow?.StartsAtUtc,
            currentWindow?.EndsAtUtc,
            revision,
            updatedAtUtc);
    }

    public static async Task<ServiceControlSnapshot> UpdateAsync(
        AuthDatabase database, UpdateServiceControlRequest request, CancellationToken ct)
    {
        var announcement = (request.Announcement ?? string.Empty).Trim();
        if (announcement.Length > 512) announcement = announcement[..512];
        await using var connection = await database.OpenConnectionAsync(ct);
        await using var command = new MySqlCommand(
            """
            UPDATE service_control
            SET maintenance_enabled = @maintenance, announcement = @announcement,
                maintenance_start_at_utc = NULL,
                shutdown_at_utc = NULL, revision = revision + 1
            WHERE id = 1;
            """, connection) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@maintenance", request.MaintenanceEnabled);
        command.Parameters.AddWithValue("@announcement", announcement);
        await command.ExecuteNonQueryAsync(ct);
        return await LoadAsync(database, ct);
    }

    public static async Task<IReadOnlyList<MaintenanceScheduleSnapshot>> ListSchedulesAsync(
        AuthDatabase database, CancellationToken ct)
    {
        await using var connection = await database.OpenConnectionAsync(ct);
        await using var command = new MySqlCommand(
            """
            SELECT id, starts_at_utc, ends_at_utc, announcement, created_at_utc
            FROM maintenance_schedules
            WHERE ends_at_utc > UTC_TIMESTAMP(6)
            ORDER BY starts_at_utc, id;
            """, connection) { CommandTimeout = 5 };
        var schedules = new List<MaintenanceScheduleSnapshot>();
        await using var reader = await command.ExecuteReaderAsync(ct);
        while (await reader.ReadAsync(ct)) schedules.Add(ReadSchedule(reader));
        return schedules;
    }

    public static async Task<MaintenanceScheduleSnapshot> CreateScheduleAsync(
        AuthDatabase database, CreateMaintenanceScheduleRequest request, CancellationToken ct)
    {
        var announcement = (request.Announcement ?? string.Empty).Trim();
        if (announcement.Length > 512) announcement = announcement[..512];
        await using var connection = await database.OpenConnectionAsync(ct);
        await using var transaction = await connection.BeginTransactionAsync(ct);
        await using (var countCommand = new MySqlCommand(
            "SELECT COUNT(*) FROM maintenance_schedules WHERE ends_at_utc > UTC_TIMESTAMP(6);",
            connection, transaction) { CommandTimeout = 5 })
        {
            if (Convert.ToInt32(await countCommand.ExecuteScalarAsync(ct)) >= 100)
                throw new InvalidOperationException("진행 중이거나 예정된 점검 예약은 최대 100개까지 저장할 수 있습니다.");
        }
        ulong id;
        await using (var insert = new MySqlCommand(
            """
            INSERT INTO maintenance_schedules (starts_at_utc, ends_at_utc, announcement)
            VALUES (@start, @end, @announcement);
            SELECT LAST_INSERT_ID();
            """, connection, transaction) { CommandTimeout = 5 })
        {
            insert.Parameters.AddWithValue("@start", request.StartsAtUtc.ToUniversalTime());
            insert.Parameters.AddWithValue("@end", request.EndsAtUtc.ToUniversalTime());
            insert.Parameters.AddWithValue("@announcement", announcement);
            id = Convert.ToUInt64(await insert.ExecuteScalarAsync(ct));
        }
        await IncrementRevisionAsync(connection, transaction, ct);
        await transaction.CommitAsync(ct);
        return new MaintenanceScheduleSnapshot(
            id, request.StartsAtUtc.ToUniversalTime(), request.EndsAtUtc.ToUniversalTime(),
            announcement, DateTime.UtcNow);
    }

    public static async Task<bool> DeleteScheduleAsync(AuthDatabase database, ulong id, CancellationToken ct)
    {
        await using var connection = await database.OpenConnectionAsync(ct);
        await using var transaction = await connection.BeginTransactionAsync(ct);
        await using var command = new MySqlCommand(
            "DELETE FROM maintenance_schedules WHERE id = @id;", connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@id", id);
        var deleted = await command.ExecuteNonQueryAsync(ct) > 0;
        if (deleted) await IncrementRevisionAsync(connection, transaction, ct);
        await transaction.CommitAsync(ct);
        return deleted;
    }

    public static async Task<MaintenanceScheduleSnapshot?> UpdateScheduleAsync(
        AuthDatabase database, ulong id, CreateMaintenanceScheduleRequest request, CancellationToken ct)
    {
        var announcement = (request.Announcement ?? string.Empty).Trim();
        if (announcement.Length > 512) announcement = announcement[..512];
        await using var connection = await database.OpenConnectionAsync(ct);
        await using var transaction = await connection.BeginTransactionAsync(ct);
        await using var command = new MySqlCommand(
            """
            UPDATE maintenance_schedules
            SET starts_at_utc = @start, ends_at_utc = @end, announcement = @announcement
            WHERE id = @id;
            """, connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@id", id);
        command.Parameters.AddWithValue("@start", request.StartsAtUtc.ToUniversalTime());
        command.Parameters.AddWithValue("@end", request.EndsAtUtc.ToUniversalTime());
        command.Parameters.AddWithValue("@announcement", announcement);
        var updated = await command.ExecuteNonQueryAsync(ct) > 0;
        if (updated) await IncrementRevisionAsync(connection, transaction, ct);
        await transaction.CommitAsync(ct);
        return updated
            ? new MaintenanceScheduleSnapshot(
                id, request.StartsAtUtc.ToUniversalTime(), request.EndsAtUtc.ToUniversalTime(),
                announcement, DateTime.UtcNow)
            : null;
    }

    private static MaintenanceScheduleSnapshot ReadSchedule(MySqlDataReader reader) => new(
        reader.GetUInt64(0),
        DateTime.SpecifyKind(reader.GetDateTime(1), DateTimeKind.Utc),
        DateTime.SpecifyKind(reader.GetDateTime(2), DateTimeKind.Utc),
        reader.GetString(3),
        DateTime.SpecifyKind(reader.GetDateTime(4), DateTimeKind.Utc));

    private static async Task IncrementRevisionAsync(
        MySqlConnection connection, MySqlTransaction transaction, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "UPDATE service_control SET revision = revision + 1 WHERE id = 1;",
            connection, transaction) { CommandTimeout = 5 };
        await command.ExecuteNonQueryAsync(ct);
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
                    maintenanceStartAtUtc = snapshot.MaintenanceStartAtUtc,
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

        app.MapGet("/api/admin/crash-reports", async (
            HttpRequest request, AuthDatabase database, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            var limit = ReadLimit(request);
            var reports = new List<object>();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var command = new MySqlCommand(
                """
                SELECT report_id, detected_at_utc, started_at_utc, last_updated_at_utc,
                       build_version, map_name, game_mode, process_role, player_role,
                       last_log_path, diagnostic_run_id, match_id
                FROM crash_reports
                WHERE LEFT(map_name, 7) <> 'UEDPIE_'
                ORDER BY detected_at_utc DESC, id DESC
                LIMIT @limit;
                """, connection) { CommandTimeout = 5 };
            command.Parameters.AddWithValue("@limit", limit);
            await using var reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                reports.Add(new
                {
                    reportId = ReadIdentifier(reader, 0),
                    detectedAtUtc = DateTime.SpecifyKind(reader.GetDateTime(1), DateTimeKind.Utc),
                    startedAtUtc = DateTime.SpecifyKind(reader.GetDateTime(2), DateTimeKind.Utc),
                    lastUpdatedAtUtc = DateTime.SpecifyKind(reader.GetDateTime(3), DateTimeKind.Utc),
                    buildVersion = reader.GetString(4),
                    mapName = reader.GetString(5),
                    gameMode = reader.GetString(6),
                    processRole = reader.GetString(7),
                    playerRole = reader.IsDBNull(8) ? null : reader.GetString(8),
                    lastLogPath = reader.GetString(9),
                    diagnosticRunId = reader.GetString(10),
                    matchId = reader.IsDBNull(11) ? null : ReadIdentifier(reader, 11)
                });
            }
            return Results.Ok(new { crashReports = reports, count = reports.Count });
        }).RequireRateLimiting("admin");

        app.MapGet("/api/admin/service/schedules", async (
            HttpRequest request, AuthDatabase database, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            var now = DateTime.UtcNow;
            var schedules = await ServiceControlStore.ListSchedulesAsync(database, ct);
            return Results.Ok(new { schedules = schedules.Select(item => item.ToResponse(now)).ToArray(), maximum = 100 });
        }).RequireRateLimiting("admin");

        app.MapPost("/api/admin/service/schedules", async (
            HttpRequest request, CreateMaintenanceScheduleRequest body, AuthDatabase database,
            ServiceControlState state, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            var start = body.StartsAtUtc.ToUniversalTime();
            var end = body.EndsAtUtc.ToUniversalTime();
            if (end <= start)
                return Results.BadRequest(new { message = "점검 종료 시각은 시작 시각보다 늦어야 합니다." });
            if (end <= DateTime.UtcNow)
                return Results.BadRequest(new { message = "이미 종료된 시각으로 점검을 예약할 수 없습니다." });
            try
            {
                var created = await ServiceControlStore.CreateScheduleAsync(database,
                    new CreateMaintenanceScheduleRequest(start, end, body.Announcement), ct);
                state.Set(await ServiceControlStore.LoadAsync(database, ct));
                await SecurityAudit.WriteAsync(database, request, "AdminMaintenanceSchedule", "Created", null,
                    $"ScheduleId={created.Id}; Start={created.StartsAtUtc:O}; End={created.EndsAtUtc:O}", ct);
                return Results.Ok(created.ToResponse(DateTime.UtcNow));
            }
            catch (InvalidOperationException exception)
            {
                return Results.BadRequest(new { message = exception.Message });
            }
        }).RequireRateLimiting("admin");

        app.MapPut("/api/admin/service/schedules/{scheduleId:long}", async (
            long scheduleId, HttpRequest request, CreateMaintenanceScheduleRequest body, AuthDatabase database,
            ServiceControlState state, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            if (scheduleId <= 0) return Results.BadRequest(new { message = "점검 예약 ID가 올바르지 않습니다." });
            var start = body.StartsAtUtc.ToUniversalTime();
            var end = body.EndsAtUtc.ToUniversalTime();
            if (end <= start)
                return Results.BadRequest(new { message = "점검 종료 시각은 시작 시각보다 늦어야 합니다." });
            if (end <= DateTime.UtcNow)
                return Results.BadRequest(new { message = "이미 종료된 시각으로 점검 예약을 변경할 수 없습니다." });
            var updated = await ServiceControlStore.UpdateScheduleAsync(database, (ulong)scheduleId,
                new CreateMaintenanceScheduleRequest(start, end, body.Announcement), ct);
            if (updated is null) return Results.NotFound(new { message = "변경할 점검 예약을 찾지 못했습니다." });
            state.Set(await ServiceControlStore.LoadAsync(database, ct));
            await SecurityAudit.WriteAsync(database, request, "AdminMaintenanceSchedule", "Updated", null,
                $"ScheduleId={scheduleId}; Start={start:O}; End={end:O}", ct);
            return Results.Ok(updated.ToResponse(DateTime.UtcNow));
        }).RequireRateLimiting("admin");

        app.MapDelete("/api/admin/service/schedules/{scheduleId:long}", async (
            long scheduleId, HttpRequest request, AuthDatabase database,
            ServiceControlState state, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            if (scheduleId <= 0) return Results.BadRequest(new { message = "점검 예약 ID가 올바르지 않습니다." });
            var deleted = await ServiceControlStore.DeleteScheduleAsync(database, (ulong)scheduleId, ct);
            if (!deleted) return Results.NotFound(new { message = "취소할 점검 예약을 찾지 못했습니다." });
            state.Set(await ServiceControlStore.LoadAsync(database, ct));
            await SecurityAudit.WriteAsync(database, request, "AdminMaintenanceSchedule", "Cancelled", null,
                $"ScheduleId={scheduleId}", ct);
            return Results.Ok(new { message = "선택한 점검 예약을 취소했습니다.", scheduleId });
        }).RequireRateLimiting("admin");

        app.MapPut("/api/admin/service", async (
            HttpRequest request, UpdateServiceControlRequest body, AuthDatabase database,
            ServiceControlState state, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAuthorized(request, admin)) return Results.Unauthorized();
            var scheduledStart = body.MaintenanceStartAtUtc?.ToUniversalTime();
            var scheduledEnd = body.ShutdownAtUtc?.ToUniversalTime();
            if (scheduledStart.HasValue != scheduledEnd.HasValue)
            {
                return Results.BadRequest(new { message = "예약 점검은 시작 시각과 종료 시각을 모두 지정해야 합니다." });
            }
            if (scheduledStart.HasValue && scheduledEnd <= scheduledStart)
            {
                return Results.BadRequest(new { message = "점검 종료 시각은 시작 시각보다 늦어야 합니다." });
            }
            if (scheduledStart.HasValue && scheduledEnd.HasValue)
            {
                await ServiceControlStore.CreateScheduleAsync(database,
                    new CreateMaintenanceScheduleRequest(scheduledStart.Value, scheduledEnd.Value, body.Announcement), ct);
            }
            var updated = await ServiceControlStore.UpdateAsync(database,
                body with { MaintenanceStartAtUtc = null, ShutdownAtUtc = null }, ct);
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

    private static string ReadIdentifier(MySqlDataReader reader, int ordinal)
    {
        // MySqlConnector recognizes CHAR(36) as Guid, including report_id and match_id.
        object value = reader.GetValue(ordinal);
        return value is Guid id ? id.ToString("D")
            : Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture) ?? string.Empty;
    }

    private static int ReadLimit(HttpRequest request) =>
        int.TryParse(request.Query["limit"], out var requested)
            ? Math.Clamp(requested, 1, 200)
            : 100;
}
