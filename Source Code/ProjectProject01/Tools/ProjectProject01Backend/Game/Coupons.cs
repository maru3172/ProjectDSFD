using System.Security.Cryptography;
using MySqlConnector;

/// <summary>
/// 쿠폰 코드 (Docs/MailAndCoupons.md). A shared code (one per account, optional total limit) or a batch of one-time codes
/// (each usable once). Either kind has a period and a reward that arrives in the mailbox. Wrong codes are limited per
/// account (10 an hour) so codes cannot be guessed.
/// </summary>
internal static class Coupons
{
    internal const int MaxSingleCodes = 5000, FailuresPerHour = 10;
    const string Alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // No 0/O or 1/I.
    internal sealed record CreateRequest(string Name, string Kind, string? Code, int Count, int MaxUses, DateTimeOffset StartsAtUtc, DateTimeOffset EndsAtUtc,
        string Title, string Body, Mailbox.Reward Reward, int ExpiresDays);
    internal sealed record RedeemRequest(string? Code);

    /// <summary>What players type: spaces and hyphens ignored, any case.</summary>
    internal static string Normalize(string? code) => new string((code ?? "").Where(char.IsLetterOrDigit).Select(char.ToUpperInvariant).ToArray());
    internal static string NewCode(int length = 12) => new string(Enumerable.Range(0, length).Select(_ => Alphabet[RandomNumberGenerator.GetInt32(Alphabet.Length)]).ToArray());
    internal static string Display(string code) => code.Length == 12 ? $"{code[..4]}-{code[4..8]}-{code[8..]}" : code;

    internal static async Task EnsureAsync(MySqlConnection c, CancellationToken ct) => await CommunityEndpoints.Exec(c, null, """
        CREATE TABLE IF NOT EXISTS coupon_campaigns(id BIGINT AUTO_INCREMENT PRIMARY KEY,name VARCHAR(80) NOT NULL,kind VARCHAR(8) NOT NULL,
          shared_code VARCHAR(20) NULL,max_uses INT NOT NULL DEFAULT 0,starts_utc DATETIME(6) NOT NULL,ends_utc DATETIME(6) NOT NULL,
          title VARCHAR(80) NOT NULL,body VARCHAR(1000) NOT NULL,credits INT NOT NULL,tickets INT NOT NULL,items VARCHAR(400) NOT NULL,expires_days INT NOT NULL,
          disabled BOOLEAN NOT NULL DEFAULT FALSE,actor VARCHAR(64) NOT NULL,created_utc DATETIME(6) NOT NULL,UNIQUE KEY uq_coupon_shared(shared_code));
        CREATE TABLE IF NOT EXISTS coupon_codes(code VARCHAR(20) PRIMARY KEY,campaign_id BIGINT NOT NULL,
          FOREIGN KEY(campaign_id) REFERENCES coupon_campaigns(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS coupon_redemptions(campaign_id BIGINT NOT NULL,user_id BIGINT UNSIGNED NOT NULL,code VARCHAR(20) NOT NULL,redeemed_utc DATETIME(6) NOT NULL,
          PRIMARY KEY(campaign_id,user_id),KEY ix_coupon_code_use(code,campaign_id),
          FOREIGN KEY(campaign_id) REFERENCES coupon_campaigns(id) ON DELETE CASCADE,FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS coupon_failures(id BIGINT AUTO_INCREMENT PRIMARY KEY,user_id BIGINT UNSIGNED NOT NULL,at_utc DATETIME(6) NOT NULL,KEY ix_coupon_fail(user_id,at_utc),
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """, ct);

    /// <summary>Redeems a code for a player: the reward is mailed. Returns (ok, message).</summary>
    internal static async Task<(bool Ok, string Message)> RedeemAsync(MySqlConnection c, MySqlTransaction tx, ulong user, string? typed, CancellationToken ct)
    {
        await CaseRecords.LockAccountAsync(c, tx, user, ct);
        int fails = Convert.ToInt32((await CommunityEndpoints.Rows(c, tx, "SELECT COUNT(*) n FROM coupon_failures WHERE user_id=@u AND at_utc>UTC_TIMESTAMP(6)-INTERVAL 1 HOUR", ct, ("u", user)))[0]["n"]);
        if (fails >= FailuresPerHour) return (false, "잘못된 쿠폰을 너무 많이 입력했습니다. 1시간 뒤에 다시 시도하세요.");
        async Task<(bool, string)> Fail(string message, bool count = true)
        {
            if (count) await CommunityEndpoints.Exec(c, tx, "INSERT INTO coupon_failures(user_id,at_utc) VALUES(@u,UTC_TIMESTAMP(6))", ct, ("u", user));
            return (false, message);
        }
        string code = Normalize(typed);
        if (code.Length is < 4 or > 20) return await Fail("없는 쿠폰이거나 기간이 지났습니다.");
        // A shared code names its campaign; a one-time code is looked up in the code list.
        var rows = await CommunityEndpoints.Rows(c, tx, """
            SELECT c.* FROM coupon_campaigns c WHERE c.shared_code=@k
            UNION ALL SELECT c.* FROM coupon_codes k JOIN coupon_campaigns c ON c.id=k.campaign_id WHERE k.code=@k
            """, ct, ("k", code));
        if (rows.Count == 0) return await Fail("없는 쿠폰이거나 기간이 지났습니다.");
        var campaign = rows[0]; long id = Convert.ToInt64(campaign["id"]);
        await CommunityEndpoints.Rows(c, tx, "SELECT id FROM coupon_campaigns WHERE id=@i FOR UPDATE", ct, ("i", id)); // Serializes uses of one campaign.
        var now = DateTime.UtcNow;
        if (Convert.ToBoolean(campaign["disabled"]) || now < (DateTime)campaign["starts_utc"]! || now >= (DateTime)campaign["ends_utc"]!) return await Fail("없는 쿠폰이거나 기간이 지났습니다.");
        if ((await CommunityEndpoints.Rows(c, tx, "SELECT 1 FROM coupon_redemptions WHERE campaign_id=@c AND user_id=@u", ct, ("c", id), ("u", user))).Count > 0)
            return await Fail("이미 사용한 쿠폰입니다.", false);
        bool single = (string)campaign["kind"]! == "Single";
        if (single && (await CommunityEndpoints.Rows(c, tx, "SELECT 1 FROM coupon_redemptions WHERE campaign_id=@c AND code=@k", ct, ("c", id), ("k", code))).Count > 0)
            return await Fail("이미 사용된 쿠폰입니다.", false);
        int maxUses = Convert.ToInt32(campaign["max_uses"]);
        if (!single && maxUses > 0 && Convert.ToInt32((await CommunityEndpoints.Rows(c, tx, "SELECT COUNT(*) n FROM coupon_redemptions WHERE campaign_id=@c", ct, ("c", id)))[0]["n"]) >= maxUses)
            return await Fail("선착순 사용이 모두 끝난 쿠폰입니다.", false);
        await CommunityEndpoints.Exec(c, tx, "INSERT INTO coupon_redemptions(campaign_id,user_id,code,redeemed_utc) VALUES(@c,@u,@k,UTC_TIMESTAMP(6))", ct, ("c", id), ("u", user), ("k", code));
        var reward = new Mailbox.Reward(Convert.ToInt32(campaign["credits"]), Convert.ToInt32(campaign["tickets"]), ((string)campaign["items"]!).Split(',', StringSplitOptions.RemoveEmptyEntries));
        await Mailbox.SendAsync(c, tx, (string)campaign["title"]!, (string)campaign["body"]!, reward, [user], Convert.ToInt32(campaign["expires_days"]), $"coupon:{id}", "coupon", null, ct);
        return (true, "쿠폰 보상을 우편함으로 보냈습니다.");
    }

    internal static void Map(WebApplication app)
    {
        app.MapPost("/api/coupons/redeem", async (RedeemRequest body, HttpRequest req, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(req, db, ct); if (user is null) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            var (ok, message) = await RedeemAsync(c, tx, user.Id, body.Code, ct);
            await tx.CommitAsync(ct); // Failures are recorded too (the guessing limit).
            return ok ? Results.Ok(new { message, mailbox = await Mailbox.ListAsync(c, user.Id, ct) }) : Results.BadRequest(new { message });
        }).RequireRateLimiting("social");

        const string AdminKey = "X-ProjectProject01-Admin-Key";
        app.MapGet("/api/admin/operations/coupons", async (HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers[AdminKey])) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            return Results.Ok(new { campaigns = await CommunityEndpoints.Rows(c, null, """
                SELECT c.id,c.name,c.kind,c.shared_code sharedCode,c.max_uses maxUses,c.starts_utc startsUtc,c.ends_utc endsUtc,c.credits,c.tickets,c.items,c.disabled,c.actor,
                  (SELECT COUNT(*) FROM coupon_redemptions r WHERE r.campaign_id=c.id) used,(SELECT COUNT(*) FROM coupon_codes k WHERE k.campaign_id=c.id) codes
                FROM coupon_campaigns c ORDER BY c.id DESC LIMIT 100
                """, ct) });
        }).RequireRateLimiting("admin");
        app.MapPost("/api/admin/operations/coupons", async (CreateRequest body, HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers[AdminKey])) return Results.Unauthorized();
            string? error = Mailbox.Check(body.Title, body.Body, body.Reward, body.ExpiresDays);
            if (body.Reward?.IsEmpty != false) error ??= "쿠폰에는 보상이 하나 이상 있어야 합니다.";
            if (!CommunityEndpoints.ValidText(body.Name, 80) || body.Kind is not ("Shared" or "Single") || body.EndsAtUtc <= body.StartsAtUtc || body.EndsAtUtc > DateTimeOffset.UtcNow.AddYears(2))
                error ??= "이름, 종류, 기간을 확인하세요.";
            string shared = Normalize(body.Code);
            if (body.Kind == "Shared" && (shared.Length is < 4 or > 20 || body.MaxUses < 0)) error ??= "공용 코드는 글자·숫자 4~20자입니다.";
            if (body.Kind == "Single" && body.Count is < 1 or > MaxSingleCodes) error ??= $"1회용 코드는 1~{MaxSingleCodes}개입니다.";
            if (error is not null) return Results.BadRequest(new { message = error });
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            if (body.Kind == "Shared" && (await CommunityEndpoints.Rows(c, tx, "SELECT 1 FROM coupon_campaigns WHERE shared_code=@k UNION ALL SELECT 1 FROM coupon_codes WHERE code=@k", ct, ("k", shared))).Count > 0)
                return Results.Conflict(new { message = "이미 있는 코드입니다." });
            await CommunityEndpoints.Exec(c, tx, """
                INSERT INTO coupon_campaigns(name,kind,shared_code,max_uses,starts_utc,ends_utc,title,body,credits,tickets,items,expires_days,actor,created_utc)
                VALUES(@n,@k,@s,@m,@a,@e,@t,@b,@cr,@ti,@it,@d,@ac,UTC_TIMESTAMP(6))
                """, ct, ("n", body.Name.Trim()), ("k", body.Kind), ("s", body.Kind == "Shared" ? shared : null), ("m", body.Kind == "Shared" ? body.MaxUses : 0),
                ("a", body.StartsAtUtc.UtcDateTime), ("e", body.EndsAtUtc.UtcDateTime), ("t", body.Title.Trim()), ("b", body.Body.Trim()), ("cr", body.Reward.Credits),
                ("ti", body.Reward.Tickets), ("it", string.Join(',', body.Reward.ItemList)), ("d", body.ExpiresDays), ("ac", OperatorAccess.Actor(req.HttpContext)));
            long id = Convert.ToInt64((await CommunityEndpoints.Rows(c, tx, "SELECT LAST_INSERT_ID() AS id", ct))[0]["id"]);
            var codes = new List<string>();
            while (body.Kind == "Single" && codes.Count < body.Count)
            {
                string code = NewCode();
                if (await CommunityEndpoints.Exec(c, tx, "INSERT IGNORE INTO coupon_codes(code,campaign_id) SELECT @k,@c FROM DUAL WHERE NOT EXISTS(SELECT 1 FROM coupon_campaigns WHERE shared_code=@k)", ct, ("k", code), ("c", id)) == 1) codes.Add(code);
            }
            await tx.CommitAsync(ct);
            return Results.Ok(new { id, codes = codes.Select(Display).ToArray(), sharedCode = body.Kind == "Shared" ? shared : null });
        }).RequireRateLimiting("admin");
        app.MapGet("/api/admin/operations/coupons/{id:long}/codes", async (long id, HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers[AdminKey])) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            var rows = await CommunityEndpoints.Rows(c, null, "SELECT k.code,(SELECT COUNT(*) FROM coupon_redemptions r WHERE r.campaign_id=k.campaign_id AND r.code=k.code) used FROM coupon_codes k WHERE k.campaign_id=@i ORDER BY k.code", ct, ("i", id));
            return Results.Ok(new { codes = rows.Select(r => new { code = Display((string)r["code"]!), used = Convert.ToInt32(r["used"]) > 0 }) });
        }).RequireRateLimiting("admin");
        app.MapPost("/api/admin/operations/coupons/{id:long}/disable", async (long id, HttpRequest req, AuthDatabase db, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers[AdminKey])) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            return await CommunityEndpoints.Exec(c, null, "UPDATE coupon_campaigns SET disabled=TRUE WHERE id=@i AND disabled=FALSE", ct, ("i", id)) == 1
                ? Results.Ok(new { message = "쿠폰을 중지했습니다." }) : Results.Conflict(new { message = "없거나 이미 중지된 쿠폰입니다." });
        }).RequireRateLimiting("admin");
    }
}
