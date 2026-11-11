using System.Text.Json;
using MySqlConnector;

/// <summary>
/// 우편함 (Docs/MailAndCoupons.md): rewards (credits, draw tickets, cosmetic items) sent as mail that the player claims,
/// with an expiry (default 30 days). Operators send to everyone or to accounts; scheduled rewards and coupons arrive as
/// mail too. A batch can be recalled: unclaimed mail of the batch is removed.
/// </summary>
internal static class Mailbox
{
    internal const int DefaultDays = 30, MaxCredits = 10000, MaxTickets = 100, MaxItems = 8, PageSize = 50;
    internal const long CreditCap = 1_000_000_000;
    internal sealed record Reward(int Credits = 0, int Tickets = 0, string[]? Items = null)
    {
        internal string[] ItemList => Items ?? [];
        internal bool IsEmpty => Credits == 0 && Tickets == 0 && ItemList.Length == 0;
    }
    internal sealed record SendRequest(string Title, string Body, Reward Reward, bool Everyone, string[]? Accounts, int ExpiresDays, Guid RequestId);

    internal static async Task EnsureAsync(MySqlConnection c, CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c, null, """
            CREATE TABLE IF NOT EXISTS mail_batches(id BIGINT AUTO_INCREMENT PRIMARY KEY,title VARCHAR(80) NOT NULL,body VARCHAR(1000) NOT NULL,
              credits INT NOT NULL,tickets INT NOT NULL,items VARCHAR(400) NOT NULL,source VARCHAR(40) NOT NULL,actor VARCHAR(64) NOT NULL,
              request_id CHAR(36) NULL,created_utc DATETIME(6) NOT NULL,expires_utc DATETIME(6) NULL,recalled BOOLEAN NOT NULL DEFAULT FALSE,
              UNIQUE KEY uq_mail_request(request_id));
            CREATE TABLE IF NOT EXISTS mail_messages(id BIGINT AUTO_INCREMENT PRIMARY KEY,batch_id BIGINT NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
              claimed_utc DATETIME(6) NULL,deleted BOOLEAN NOT NULL DEFAULT FALSE,
              UNIQUE KEY uq_mail_user(batch_id,user_id),KEY ix_mail_user(user_id,deleted),
              FOREIGN KEY(batch_id) REFERENCES mail_batches(id) ON DELETE CASCADE,FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            """, ct);
    }

    internal static string? Check(string? title, string? body, Reward? reward, int expiresDays)
    {
        if (!CommunityEndpoints.ValidText(title, 80) || body is null || body.Length > 1000) return "제목(80자)과 내용(1000자)을 확인하세요.";
        if (reward is null || reward.Credits is < 0 or > MaxCredits || reward.Tickets is < 0 or > MaxTickets || reward.ItemList.Length > MaxItems ||
            reward.ItemList.Distinct().Count() != reward.ItemList.Length || reward.ItemList.Any(id => CaseRecords.Catalogue.All(i => i.Id != id)))
            return $"보상은 크레딧 {MaxCredits} 이하, 뽑기권 {MaxTickets}장 이하, 목록에 있는 아이템 {MaxItems}개 이하입니다.";
        if (expiresDays is < 0 or > 365) return "기한은 0(없음)~365일입니다.";
        return null;
    }

    /// <summary>Creates a batch and one message per user (in the caller's transaction). Returns the batch id.</summary>
    internal static async Task<long> SendAsync(MySqlConnection c, MySqlTransaction tx, string title, string body, Reward reward, IEnumerable<ulong> users,
        int expiresDays, string source, string actor, Guid? requestId, CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c, tx, """
            INSERT INTO mail_batches(title,body,credits,tickets,items,source,actor,request_id,created_utc,expires_utc)
            VALUES(@t,@b,@cr,@ti,@it,@s,@a,@r,UTC_TIMESTAMP(6),IF(@d=0,NULL,UTC_TIMESTAMP(6)+INTERVAL @d DAY))
            """, ct, ("t", title.Trim()), ("b", body.Trim()), ("cr", reward.Credits), ("ti", reward.Tickets), ("it", string.Join(',', reward.ItemList)),
            ("s", source), ("a", actor), ("r", requestId?.ToString("D")), ("d", expiresDays));
        long batch = Convert.ToInt64((await CommunityEndpoints.Rows(c, tx, "SELECT LAST_INSERT_ID() AS id", ct))[0]["id"]);
        foreach (var user in users)
            await CommunityEndpoints.Exec(c, tx, "INSERT IGNORE INTO mail_messages(batch_id,user_id) VALUES(@b,@u)", ct, ("b", batch), ("u", user));
        return batch;
    }

    const string Visible = "m.deleted=FALSE AND b.recalled=FALSE AND (b.expires_utc IS NULL OR b.expires_utc>UTC_TIMESTAMP(6) OR m.claimed_utc IS NOT NULL)";
    internal static async Task<int> UnclaimedAsync(MySqlConnection c, MySqlTransaction? tx, ulong user, CancellationToken ct) => Convert.ToInt32((await CommunityEndpoints.Rows(c, tx,
        $"SELECT COUNT(*) n FROM mail_messages m JOIN mail_batches b ON b.id=m.batch_id WHERE m.user_id=@u AND m.claimed_utc IS NULL AND {Visible}", ct, ("u", user)))[0]["n"]);

    internal static async Task<object> ListAsync(MySqlConnection c, ulong user, CancellationToken ct)
    {
        var rows = await CommunityEndpoints.Rows(c, null, $"""
            SELECT m.id,b.title,b.body,b.credits,b.tickets,b.items,b.created_utc createdUtc,b.expires_utc expiresUtc,m.claimed_utc claimedUtc
            FROM mail_messages m JOIN mail_batches b ON b.id=m.batch_id WHERE m.user_id=@u AND {Visible} ORDER BY m.id DESC LIMIT {PageSize}
            """, ct, ("u", user));
        var names = CaseRecords.Catalogue.ToDictionary(i => i.Id, i => i.Name);
        return new
        {
            unclaimed = await UnclaimedAsync(c, null, user, ct),
            mail = rows.Select(r => new
            {
                id = Convert.ToInt64(r["id"]), title = r["title"], body = r["body"], credits = Convert.ToInt32(r["credits"]), tickets = Convert.ToInt32(r["tickets"]),
                items = ((string)r["items"]!).Split(',', StringSplitOptions.RemoveEmptyEntries).Select(id => new { id, name = names.GetValueOrDefault(id, id) }).ToArray(),
                createdUtc = r["createdUtc"], expiresUtc = r["expiresUtc"], claimed = r["claimedUtc"] is not null and not DBNull
            }).ToArray()
        };
    }

    /// <summary>Claims messages (one, or all unclaimed): rewards go to the account; owned items are skipped.</summary>
    internal static async Task<(int Claimed, string? Error)> ClaimAsync(MySqlConnection c, MySqlTransaction tx, ulong user, long? id, CancellationToken ct)
    {
        var account = await CaseRecords.LockAccountAsync(c, tx, user, ct);
        var rows = await CommunityEndpoints.Rows(c, tx, $"""
            SELECT m.id,b.credits,b.tickets,b.items FROM mail_messages m JOIN mail_batches b ON b.id=m.batch_id
            WHERE m.user_id=@u AND m.claimed_utc IS NULL AND {Visible} {(id is null ? "" : "AND m.id=@i")} ORDER BY m.id FOR UPDATE
            """, ct, ("u", user), ("i", id ?? 0));
        if (rows.Count == 0) return (0, id is null ? null : "받을 수 없는 우편입니다.");
        long credits = rows.Sum(r => Convert.ToInt64(r["credits"]));
        if (Convert.ToInt64(account["credits"]) + credits > CreditCap) return (0, "크레딧이 최대치를 넘어 받을 수 없습니다.");
        long tickets = rows.Sum(r => Convert.ToInt64(r["tickets"]));
        if (credits > 0) await CommunityEndpoints.Exec(c, tx, "UPDATE case_accounts SET credits=credits+@c WHERE user_id=@u", ct, ("c", credits), ("u", user));
        if (tickets > 0) await CommunityEndpoints.Exec(c, tx, "INSERT INTO case_draw_wallet(user_id,tickets) VALUES(@u,@t) ON DUPLICATE KEY UPDATE tickets=tickets+@t", ct, ("u", user), ("t", tickets));
        foreach (var item in rows.SelectMany(r => ((string)r["items"]!).Split(',', StringSplitOptions.RemoveEmptyEntries)).Distinct())
            await CommunityEndpoints.Exec(c, tx, "INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,@i)", ct, ("u", user), ("i", item));
        foreach (var r in rows) await CommunityEndpoints.Exec(c, tx, "UPDATE mail_messages SET claimed_utc=UTC_TIMESTAMP(6) WHERE id=@i", ct, ("i", r["id"]));
        return (rows.Count, null);
    }

    internal static void Map(WebApplication app)
    {
        // ---- Player ----
        app.MapGet("/api/mail", async (HttpRequest req, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(req, db, ct); if (user is null) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct); return Results.Ok(await ListAsync(c, user.Id, ct));
        }).RequireRateLimiting("social");
        async Task<IResult> Claim(HttpRequest req, AuthDatabase db, long? id, CancellationToken ct)
        {
            var user = await LobbyEndpoints.AuthenticateAsync(req, db, ct); if (user is null) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            var (claimed, error) = await ClaimAsync(c, tx, user.Id, id, ct);
            if (error is not null) return Results.Conflict(new { message = error });
            await tx.CommitAsync(ct);
            return Results.Ok(new { message = claimed == 0 ? "받을 우편이 없습니다." : $"우편 {claimed}통의 보상을 받았습니다.", claimed, mailbox = await ListAsync(c, user.Id, ct) });
        }
        app.MapPost("/api/mail/{id:long}/claim", (long id, HttpRequest req, AuthDatabase db, CancellationToken ct) => Claim(req, db, id, ct)).RequireRateLimiting("social");
        app.MapPost("/api/mail/claim-all", (HttpRequest req, AuthDatabase db, CancellationToken ct) => Claim(req, db, null, ct)).RequireRateLimiting("social");
        app.MapPost("/api/mail/{id:long}/delete", async (long id, HttpRequest req, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(req, db, ct); if (user is null) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            // Only mail with nothing left to claim can be removed, so a reward is never thrown away by mistake.
            int n = await CommunityEndpoints.Exec(c, null, """
                UPDATE mail_messages m JOIN mail_batches b ON b.id=m.batch_id SET m.deleted=TRUE
                WHERE m.id=@i AND m.user_id=@u AND (m.claimed_utc IS NOT NULL OR (b.credits=0 AND b.tickets=0 AND b.items=''))
                """, ct, ("i", id), ("u", user.Id));
            return n == 1 ? Results.Ok(new { mailbox = await ListAsync(c, user.Id, ct) }) : Results.Conflict(new { message = "보상을 받은 우편만 지울 수 있습니다." });
        }).RequireRateLimiting("social");

        // ---- Admin (owner key: it hands out rewards, like scheduled rewards) ----
        const string AdminKey = "X-ProjectProject01-Admin-Key";
        app.MapGet("/api/admin/operations/mail", async (HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers[AdminKey])) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            return Results.Ok(new { batches = await CommunityEndpoints.Rows(c, null, """
                SELECT b.id,b.title,b.credits,b.tickets,b.items,b.source,b.actor,b.created_utc createdUtc,b.expires_utc expiresUtc,b.recalled,
                  (SELECT COUNT(*) FROM mail_messages m WHERE m.batch_id=b.id) recipients,(SELECT COUNT(*) FROM mail_messages m WHERE m.batch_id=b.id AND m.claimed_utc IS NOT NULL) claimed
                FROM mail_batches b ORDER BY b.id DESC LIMIT 100
                """, ct), items = CaseRecords.Catalogue.Select(i => new { id = i.Id, name = i.Name }) });
        }).RequireRateLimiting("admin");
        async Task<IResult> Send(SendRequest body, bool preview, HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct)
        {
            if (!admin.IsAuthorized(req.Headers[AdminKey])) return Results.Unauthorized();
            var error = Check(body.Title, body.Body, body.Reward, body.ExpiresDays);
            var accounts = body.Accounts ?? [];
            if (error is null && (body.RequestId == Guid.Empty || (!body.Everyone && accounts.Length is 0 or > 500) || accounts.Any(a => !CommunityEndpoints.ValidText(a, 32))))
                error = "받는 사람(전체 또는 계정 1~500개)을 확인하세요.";
            if (error is not null) return Results.BadRequest(new { message = error });
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            List<ulong> users;
            if (body.Everyone) users = (await CommunityEndpoints.Rows(c, tx, "SELECT id FROM users ORDER BY id", ct)).Select(r => Convert.ToUInt64(r["id"])).ToList();
            else
            {
                var args = accounts.Select((a, i) => ($"a{i}", (object?)a.Trim())).ToArray();
                var rows = await CommunityEndpoints.Rows(c, tx, $"SELECT id,account_id FROM users WHERE account_id IN ({string.Join(',', args.Select(a => "@" + a.Item1))})", ct, args);
                if (rows.Count != accounts.Distinct(StringComparer.OrdinalIgnoreCase).Count()) return Results.BadRequest(new { message = "존재하지 않는 계정이 있습니다." });
                users = rows.Select(r => Convert.ToUInt64(r["id"])).ToList();
            }
            if (preview) return Results.Ok(new { recipients = users.Count });
            var existing = await CommunityEndpoints.Rows(c, tx, "SELECT id FROM mail_batches WHERE request_id=@r", ct, ("r", body.RequestId.ToString("D")));
            if (existing.Count > 0) return Results.Ok(new { id = Convert.ToInt64(existing[0]["id"]), recipients = users.Count }); // A retried send.
            long id = await SendAsync(c, tx, body.Title, body.Body, body.Reward, users, body.ExpiresDays, "admin", OperatorAccess.Actor(req.HttpContext), body.RequestId, ct);
            await tx.CommitAsync(ct);
            return Results.Ok(new { id, recipients = users.Count });
        }
        app.MapPost("/api/admin/operations/mail/preview", (SendRequest body, HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) => Send(body, true, req, db, admin, ct)).RequireRateLimiting("admin");
        app.MapPost("/api/admin/operations/mail", (SendRequest body, HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) => Send(body, false, req, db, admin, ct)).RequireRateLimiting("admin");
        app.MapPost("/api/admin/operations/mail/{id:long}/recall", async (long id, HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers[AdminKey])) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            int marked = await CommunityEndpoints.Exec(c, tx, "UPDATE mail_batches SET recalled=TRUE WHERE id=@i AND recalled=FALSE", ct, ("i", id));
            if (marked == 0) return Results.Conflict(new { message = "없거나 이미 회수한 우편입니다." });
            // Unclaimed mail of the batch disappears; what was already claimed stays with the players.
            int removed = await CommunityEndpoints.Exec(c, tx, "DELETE FROM mail_messages WHERE batch_id=@i AND claimed_utc IS NULL", ct, ("i", id));
            await tx.CommitAsync(ct);
            return Results.Ok(new { removed });
        }).RequireRateLimiting("admin");
    }
}
