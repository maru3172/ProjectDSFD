using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MySqlConnector;

internal static class LiveOperations
{
    private const string AdminKey = "X-ProjectProject01-Admin-Key";
    private const string SurveyFeature = "Survey";
    private const int MaximumCredits = 1_000_000_000;
    private sealed record Rollout(string Mode, int Percent, string[] TestAccounts,
        DateTimeOffset? StartsAtUtc, DateTimeOffset? EndsAtUtc);
    private sealed record RolloutUpdate(long Revision, Rollout Settings);
    private sealed record RewardRequest(string Title, int Credits, DateTimeOffset StartsAtUtc, string[] Accounts, Guid RequestId);
    private static readonly Rollout Default = new("Everyone", 100, [], null, null);
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    private static async Task<(long Revision, Rollout Settings)> ReadRollout(
        MySqlConnection connection, MySqlTransaction? transaction, CancellationToken ct)
    {
        var rows = await CommunityEndpoints.Rows(connection, transaction,
            "SELECT revision,settings_json AS settings FROM operator_rollouts WHERE feature=@f", ct, ("f", SurveyFeature));
        return rows.Count == 0 ? (0, Default) : (Convert.ToInt64(rows[0]["revision"]),
            JsonSerializer.Deserialize<Rollout>((string)rows[0]["settings"]!, Json) ?? Default);
    }

    internal static async Task<bool> SurveyAllowed(MySqlConnection connection, ulong userId, CancellationToken ct)
    {
        var (revision, rollout) = await ReadRollout(connection, null, ct);
        var now = DateTimeOffset.UtcNow;
        if (now < rollout.StartsAtUtc || now >= rollout.EndsAtUtc) return true;
        if (rollout.Mode == "Everyone") return true;
        if (rollout.Mode == "Off") return false;
        if (rollout.Mode == "Experiment")
        {
            byte[] digest = SHA256.HashData(Encoding.ASCII.GetBytes(FormattableString.Invariant($"{SurveyFeature}:{revision}:{userId}")));
            return BinaryPrimitives.ReadUInt32LittleEndian(digest) % 100 < rollout.Percent;
        }
        var rows = await CommunityEndpoints.Rows(connection, null, "SELECT account_id FROM users WHERE id=@u", ct, ("u", userId));
        return rows.Count == 1 && rollout.TestAccounts.Contains((string)rows[0]["account_id"]!, StringComparer.OrdinalIgnoreCase);
    }

    public static void Map(WebApplication app)
    {
        var rollout = app.MapGroup("/api/admin/operations/rollout").RequireRateLimiting("admin");
        rollout.MapGet("", async (HttpRequest request, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers[AdminKey])) return Results.Unauthorized();
            await using var connection = await db.OpenConnectionAsync(ct);
            var (revision, settings) = await ReadRollout(connection, null, ct);
            return Results.Ok(new { revision, settings });
        });
        rollout.MapPut("", async (RolloutUpdate body, HttpRequest request, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers[AdminKey])) return Results.Unauthorized();
            var settings = body.Settings;
            if (settings is null || settings.Mode is not ("Everyone" or "Off" or "Testers" or "Experiment") ||
                settings.Percent is < 0 or > 100 || !ValidAccounts(settings.TestAccounts, settings.Mode == "Testers") ||
                (settings.StartsAtUtc is {} start && settings.EndsAtUtc is {} end && end <= start))
                return Results.BadRequest(new { message = "공개 범위와 시간을 확인하세요." });
            await using var c = await db.OpenConnectionAsync(ct);
            await using var tx = await c.BeginTransactionAsync(ct);
            await CommunityEndpoints.Exec(c, tx, "INSERT IGNORE INTO operator_rollouts(feature,settings_json) VALUES(@f,@s)", ct,
                ("f", SurveyFeature), ("s", JsonSerializer.Serialize(Default, Json)));
            int updated = await CommunityEndpoints.Exec(c, tx, """
                UPDATE operator_rollouts SET revision=revision+1, settings_json=@s WHERE feature=@f AND revision=@r
                """, ct, ("f", SurveyFeature), ("r", body.Revision), ("s", JsonSerializer.Serialize(settings, Json)));
            if (updated != 1) return Results.Conflict(new { message = "다른 운영자가 수정했습니다. 다시 불러오세요." });
            await tx.CommitAsync(ct);
            return Results.Ok(new { revision = body.Revision + 1, settings });
        });

        var rewards = app.MapGroup("/api/admin/operations/rewards").RequireRateLimiting("admin");
        rewards.MapGet("", async (HttpRequest request, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers[AdminKey])) return Results.Unauthorized();
            await using var connection = await db.OpenConnectionAsync(ct);
            return Results.Ok(new { rewards = await CommunityEndpoints.Rows(connection, null, """
                SELECT r.id,r.title,r.credits,r.starts_utc AS startsUtc,r.status,r.actor,
                  (SELECT COUNT(*) FROM operator_reward_recipients p WHERE p.reward_id=r.id) AS recipients
                FROM operator_rewards r ORDER BY r.id DESC LIMIT 100
                """, ct) });
        });
        rewards.MapPost("/preview", (RewardRequest body, HttpRequest request, AuthDatabase db, AdministratorOptions admin, CancellationToken ct)
            => Schedule(body, true, request, db, admin, ct));
        rewards.MapPost("", (RewardRequest body, HttpRequest request, AuthDatabase db, AdministratorOptions admin, CancellationToken ct)
            => Schedule(body, false, request, db, admin, ct));
        rewards.MapPost("/{id:long}/cancel", async (long id, HttpRequest request, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers[AdminKey])) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            int updated = await CommunityEndpoints.Exec(c, null, """
                UPDATE operator_rewards SET status='Cancelled',updated_utc=UTC_TIMESTAMP(6) WHERE id=@i AND status='Scheduled'
                """, ct, ("i", id));
            return updated == 1 ? Results.Ok() : Results.Conflict(new { message = "이미 처리된 예약입니다." });
        });
        rewards.MapPost("/{id:long}/reverse", async (long id, HttpRequest request, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers[AdminKey])) return Results.Unauthorized();
            return await Apply(db, id, true, ct) ? Results.Ok() :
                Results.Conflict(new { message = "지급 기록이 없거나 사용된 포인트가 있어 회수할 수 없습니다." });
        });
    }

    private static bool ValidAccounts(string[]? accounts, bool required) =>
        accounts is { Length: <= 64 } && (!required || accounts.Length > 0) &&
        accounts.All(name => CommunityEndpoints.ValidText(name, 32) && name == name.Trim()) &&
        accounts.Distinct(StringComparer.OrdinalIgnoreCase).Count() == accounts.Length;

    private static async Task<IResult> Schedule(RewardRequest body, bool preview, HttpRequest request,
        AuthDatabase db, AdministratorOptions admin, CancellationToken ct)
    {
        if (!admin.IsAuthorized(request.Headers[AdminKey])) return Results.Unauthorized();
        if (!ValidAccounts(body.Accounts, true) || !CommunityEndpoints.ValidText(body.Title, 120) || body.RequestId == Guid.Empty ||
            body.Credits is < 1 or > 10000 || body.StartsAtUtc < DateTimeOffset.UtcNow.AddMinutes(-1) ||
            body.StartsAtUtc > DateTimeOffset.UtcNow.AddYears(1))
            return Results.BadRequest(new { message = "제목, 지급 포인트, 시간 및 계정 목록을 확인하세요." });
        await using var c = await db.OpenConnectionAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var names = body.Accounts.Select((value, index) => ($"a{index}", (object?)value)).ToArray();
        string parameters = string.Join(",", names.Select(arg => "@" + arg.Item1));
        var users = await CommunityEndpoints.Rows(c, tx,
            $"SELECT id,account_id AS accountId FROM users WHERE account_id IN ({parameters}) ORDER BY id FOR SHARE", ct, names);
        if (users.Count != body.Accounts.Length) return Results.BadRequest(new { message = "존재하지 않는 계정이 포함되어 있습니다." });
        var accountIds = users.Select(user => (string)user["accountId"]!).ToArray();
        if (preview) return Results.Ok(new { accounts = accountIds, perAccount = body.Credits, total = users.Count * body.Credits });

        string hash = Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new
        {
            title = body.Title.Trim(), body.Credits, utc = body.StartsAtUtc.UtcDateTime,
            users = users.Select(user => Convert.ToUInt64(user["id"])).ToArray()
        })));
        await CommunityEndpoints.Exec(c, tx, """
            INSERT INTO operator_rewards(title,credits,starts_utc,actor,request_id,payload_hash)
            VALUES(@title,@credits,@start,@actor,@request,@hash)
            ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id)
            """, ct, ("title", body.Title.Trim()), ("credits", body.Credits), ("start", body.StartsAtUtc.UtcDateTime),
            ("actor", OperatorAccess.Actor(request.HttpContext)), ("request", body.RequestId.ToString("D")), ("hash", hash));
        var ids = await CommunityEndpoints.Rows(c, tx, "SELECT LAST_INSERT_ID() AS id", ct);
        long id = Convert.ToInt64(ids[0]["id"]);
        var saved = await CommunityEndpoints.Rows(c, tx, "SELECT payload_hash FROM operator_rewards WHERE id=@i", ct, ("i", id));
        if (!Equals(saved[0]["payload_hash"], hash)) return Results.Conflict(new { message = "이미 사용된 지급 요청입니다." });
        foreach (var user in users)
            await CommunityEndpoints.Exec(c, tx,
                "INSERT IGNORE INTO operator_reward_recipients(reward_id,user_id) VALUES(@r,@u)", ct, ("r", id), ("u", user["id"]));
        await tx.CommitAsync(ct);
        return Results.Ok(new { id });
    }

    internal static async Task<bool> Apply(AuthDatabase db, long id, bool reverse, CancellationToken ct)
    {
        await using var c = await db.OpenConnectionAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var batch = await CommunityEndpoints.Rows(c, tx,
            "SELECT status,credits,starts_utc<=UTC_TIMESTAMP(6) AS due FROM operator_rewards WHERE id=@i FOR UPDATE", ct, ("i", id));
        if (batch.Count != 1 || !Equals(batch[0]["status"], reverse ? "Paid" : "Scheduled") ||
            (!reverse && !Convert.ToBoolean(batch[0]["due"]))) return false;
        long amount = Convert.ToInt64(batch[0]["credits"]);
        var recipients = await CommunityEndpoints.Rows(c, tx,
            "SELECT user_id FROM operator_reward_recipients WHERE reward_id=@r ORDER BY user_id FOR UPDATE", ct, ("r", id));
        bool available = recipients.Count > 0;
        foreach (var recipient in recipients)
        {
            ulong user = Convert.ToUInt64(recipient["user_id"]);
            var account = await CaseRecords.LockAccountAsync(c, tx, user, ct);
            long balance = Convert.ToInt64(account["credits"]);
            if (reverse ? balance < amount : balance > MaximumCredits - amount)
            {
                available = false;
                break;
            }
        }
        if (!available && reverse) return false;
        if (available)
            await CommunityEndpoints.Exec(c, tx, """
                UPDATE case_accounts a JOIN operator_reward_recipients p ON p.user_id=a.user_id
                SET a.credits=CAST(a.credits AS SIGNED)+@delta WHERE p.reward_id=@r
                """, ct, ("delta", reverse ? -amount : amount), ("r", id));
        await CommunityEndpoints.Exec(c, tx,
            "UPDATE operator_rewards SET status=@s,updated_utc=UTC_TIMESTAMP(6) WHERE id=@r", ct,
            ("s", available ? (reverse ? "Reversed" : "Paid") : "Failed"), ("r", id));
        await tx.CommitAsync(ct);
        return available;
    }
}

internal sealed class ScheduledRewardWorker(AuthDatabase database, ILogger<ScheduledRewardWorker> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        if (!database.IsConfigured) return;
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(15));
        while (await timer.WaitForNextTickAsync(ct))
        {
            try
            {
                long[] ids;
                await using (var connection = await database.OpenConnectionAsync(ct))
                    ids = (await CommunityEndpoints.Rows(connection, null, """
                        SELECT id FROM operator_rewards WHERE status='Scheduled' AND starts_utc<=UTC_TIMESTAMP(6)
                        ORDER BY starts_utc,id LIMIT 16
                        """, ct)).Select(row => Convert.ToInt64(row["id"])).ToArray();
                foreach (long id in ids) await LiveOperations.Apply(database, id, false, ct);
            }
            catch (Exception ex) when (!ct.IsCancellationRequested)
            {
                logger.LogWarning("예약 지급 처리 실패 ({Error})", ex.GetType().Name);
            }
        }
    }
}
