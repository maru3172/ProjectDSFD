using System.Security.Cryptography;
// Draft only. This directory is outside the backend project and is not compiled or mapped.
using MySqlConnector;

// Bots are not accounts: no auth session, friendship, voice token or forged user ID.
internal sealed record RoomBot(string BotId, string DisplayName, int Difficulty, string AssignedRole = "");
internal sealed record BotRoomAction(string Operation, string? Target = null, int Difficulty = 2);
internal static partial class LobbyEndpoints
{
    internal static async Task EnsureBotRoomSchemaAsync(MySqlConnection c, CancellationToken ct)
    {
        await c.ChangeDatabaseAsync("projectproject01", ct);
        await using var check = new MySqlCommand("SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='room_members' AND column_name='is_observer'", c);
        if (Convert.ToInt32(await check.ExecuteScalarAsync(ct)) == 0)
        {
            await using var alter = new MySqlCommand("ALTER TABLE room_members ADD COLUMN is_observer BOOLEAN NOT NULL DEFAULT FALSE", c);
            await alter.ExecuteNonQueryAsync(ct);
        }
        await CommunityEndpoints.Exec(c, null, """
            CREATE TABLE IF NOT EXISTS room_bots(
              bot_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
              room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              difficulty TINYINT NOT NULL DEFAULT 2,
              assigned_role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NULL,
              KEY ix_room_bots_room(room_id), FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS bot_match_rosters(
              match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              bot_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              difficulty TINYINT NOT NULL, role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              PRIMARY KEY(match_id,bot_id));
            CREATE TABLE IF NOT EXISTS player_match_ratings(
              user_id BIGINT UNSIGNED NOT NULL, role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              rating INT NOT NULL DEFAULT 1000, games INT NOT NULL DEFAULT 0,
              PRIMARY KEY(user_id,role), FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS match_rating_members(
              match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
              role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,rating INT NOT NULL,games INT NOT NULL,
              applied BOOLEAN NOT NULL DEFAULT FALSE,PRIMARY KEY(match_id,user_id));
            """, ct);
    }

    private static void MapBotRoomSupport(this WebApplication app, Func<bool> available)
    {
        app.MapPost("/api/rooms/current/composition", async (BotRoomAction body, HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            if (body.Difficulty is < 1 or > 5) return Results.BadRequest(new { message = "봇 수준은 1~5단계입니다." });
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            await LockLobbyStateAsync(c, tx, ct);
            var membership = await LoadMembershipAsync(c, tx, user.Id, true, ct);
            if (membership is null) return Results.NotFound();
            if (membership.Status != "Waiting" || await IsRandomPartyAsync(c, tx, membership.RoomId, ct))
                return Results.Conflict(new { message = "대기 중인 오픈매칭 방에서만 인원을 변경할 수 있습니다." });
            string room = membership.RoomId, target = body.Target ?? "";
            var humans = await CommunityEndpoints.Rows(c, tx, "SELECT user_id,is_observer FROM room_members WHERE room_id=@r FOR UPDATE", ct, ("r", room));
            var bots = await LoadBotsAsync(c, tx, room, ct);
            int players = humans.Count(h => !Convert.ToBoolean(h["is_observer"])) + bots.Count;
            int observers = humans.Count(h => Convert.ToBoolean(h["is_observer"]));
            if (body.Operation is "AddBot" or "RemoveBot" or "BotDifficulty" or "Kick" ||
                (body.Operation is "Observe" or "Play" && target != user.Id.ToString()))
                if (!membership.IsHost) return Results.StatusCode(403);
            switch (body.Operation)
            {
                case "AddBot":
                    if (players >= 3) return Results.Conflict(new { message = "경기 참가자 정원은 봇을 포함해 3명입니다." });
                    await CommunityEndpoints.Exec(c, tx, "INSERT INTO room_bots(bot_id,room_id,difficulty) VALUES(@b,@r,@d)", ct, ("b", Guid.NewGuid().ToString("D")), ("r", room), ("d", body.Difficulty)); break;
                case "RemoveBot":
                case "BotDifficulty":
                    if (!bots.Any(b => b.BotId == target)) return Results.NotFound();
                    await CommunityEndpoints.Exec(c, tx, body.Operation == "RemoveBot" ? "DELETE FROM room_bots WHERE room_id=@r AND bot_id=@b" : "UPDATE room_bots SET difficulty=@d WHERE room_id=@r AND bot_id=@b", ct, ("r", room), ("b", target), ("d", body.Difficulty)); break;
                case "Observe":
                case "Play":
                    if (!ulong.TryParse(target, out var id)) return Results.BadRequest();
                    var member = humans.FirstOrDefault(h => Convert.ToUInt64(h["user_id"]) == id); if (member is null) return Results.NotFound();
                    bool wasObserver = Convert.ToBoolean(member["is_observer"]), observe = body.Operation == "Observe";
                    if (observe != wasObserver && (observe ? observers >= 2 : players >= 3)) return Results.Conflict(new { message = observe ? "관전 정원은 2명입니다." : "경기 참가자 자리가 없습니다." });
                    await CommunityEndpoints.Exec(c, tx, "UPDATE room_members SET is_observer=@o,is_ready=FALSE,assigned_role=NULL WHERE room_id=@r AND user_id=@u", ct, ("o", observe), ("r", room), ("u", id)); break;
                case "Kick":
                    if (!ulong.TryParse(target, out var kicked) || kicked == user.Id) return Results.BadRequest(new { message = "자신을 강퇴할 수 없습니다." });
                    await CommunityEndpoints.Exec(c, tx, "DELETE FROM room_members WHERE room_id=@r AND user_id=@u", ct, ("r", room), ("u", kicked)); break;
                default: return Results.BadRequest();
            }
            await tx.CommitAsync(ct);
            return Results.Ok(new { message = "방 구성을 변경했습니다.", room = await LoadCurrentRoomAsync(c, null, user.Id, available(), ct) });
        }).RequireRateLimiting("lobby");
    }
    internal static async Task<List<RoomBot>> LoadBotsAsync(MySqlConnection c, MySqlTransaction? tx, string room, CancellationToken ct)
    {
        var rows = await CommunityEndpoints.Rows(c, tx, "SELECT bot_id,difficulty,assigned_role FROM room_bots WHERE room_id=@r ORDER BY bot_id", ct, ("r", room));
        return rows.Select((r, i) => new RoomBot(Convert.ToString(r["bot_id"])!, $"봇 {i + 1}", Convert.ToInt32(r["difficulty"]), r["assigned_role"] is DBNull ? "" : Convert.ToString(r["assigned_role"])!)).ToList();
    }
    internal static async Task<int> PlayingCountAsync(MySqlConnection c, MySqlTransaction? tx, string room, CancellationToken ct) =>
        Convert.ToInt32((await CommunityEndpoints.Rows(c, tx, "SELECT (SELECT COUNT(*) FROM room_members WHERE room_id=@r AND is_observer=FALSE)+(SELECT COUNT(*) FROM room_bots WHERE room_id=@r) n", ct, ("r", room)))[0]["n"]);

    internal static async Task AssignBotAndHumanRolesAsync(MySqlConnection c, MySqlTransaction tx, string room, CancellationToken ct)
    {
        var humans = await CommunityEndpoints.Rows(c, tx, "SELECT user_id,assigned_role FROM room_members WHERE room_id=@r AND is_observer=FALSE ORDER BY joined_at_utc,user_id FOR UPDATE", ct, ("r", room));
        var bots = await LoadBotsAsync(c, tx, room, ct);
        if (humans.Count + bots.Count != 3 || humans.Count == 0) throw new InvalidOperationException("Invalid playing roster");
        // Random groups already have immutable roles assigned before their countdown.
        bool random = (await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM random_match_rooms WHERE room_id=@r",ct,("r",room))).Count>0;
        int existing = random ? humans.FindIndex(h => Convert.ToString(h["assigned_role"]) == "Mannequin") : -1;
        int existingBot = random ? bots.FindIndex(b => b.AssignedRole == "Mannequin") : -1;
        int pick = existing >= 0 ? existing : existingBot >= 0 ? humans.Count + existingBot : RandomNumberGenerator.GetInt32(3);
        await CommunityEndpoints.Exec(c, tx, "UPDATE room_members SET assigned_role=IF(is_observer,'Observer','Survivor'),returned_to_room=FALSE WHERE room_id=@r;UPDATE room_bots SET assigned_role='Survivor' WHERE room_id=@r", ct, ("r", room));
        if (pick < humans.Count) await CommunityEndpoints.Exec(c, tx, "UPDATE room_members SET assigned_role='Mannequin' WHERE room_id=@r AND user_id=@u", ct, ("r", room), ("u", humans[pick]["user_id"]));
        else await CommunityEndpoints.Exec(c, tx, "UPDATE room_bots SET assigned_role='Mannequin' WHERE room_id=@r AND bot_id=@b", ct, ("r", room), ("b", bots[pick - humans.Count].BotId));
    }

    internal static int DifficultyForRating(double rating, int games) => games < 5 ? 1 : rating < 800 ? 1 : rating < 1100 ? 2 : rating < 1450 ? 3 : rating < 1850 ? 4 : 5;
    internal static async Task MatchBotDifficultyToOpponentsAsync(MySqlConnection c,MySqlTransaction tx,string room,CancellationToken ct)
    {
        var humans=await CommunityEndpoints.Rows(c,tx,"SELECT m.assigned_role,COALESCE(p.rating,1000) rating,COALESCE(p.games,0) games FROM room_members m LEFT JOIN player_match_ratings p ON p.user_id=m.user_id AND p.role=m.assigned_role WHERE m.room_id=@r AND m.is_observer=FALSE",ct,("r",room));
        foreach(var bot in await LoadBotsAsync(c,tx,room,ct))
        {
            var opposite=humans.Where(h=>Convert.ToString(h["assigned_role"])!=bot.AssignedRole).ToArray();
            // With no human opponent (e.g. one human survivor), use the present humans' experience.
            if(opposite.Length==0)opposite=humans.ToArray();
            int level=DifficultyForRating(opposite.Average(h=>Convert.ToDouble(h["rating"])),opposite.Min(h=>Convert.ToInt32(h["games"])));
            await CommunityEndpoints.Exec(c,tx,"UPDATE room_bots SET difficulty=@d WHERE room_id=@r AND bot_id=@b",ct,("d",level),("r",room),("b",bot.BotId));
        }
    }
    internal static async Task SnapshotBotRosterAsync(MySqlConnection c, MySqlTransaction tx, string room, string match, CancellationToken ct)
    {
        // Freeze server-owned bot config for admission; a room edit cannot change an ongoing match.
        await CommunityEndpoints.Exec(c, tx, "INSERT IGNORE INTO bot_match_rosters(match_id,bot_id,difficulty,role) SELECT @m,bot_id,difficulty,assigned_role FROM room_bots WHERE room_id=@r AND assigned_role IN ('Mannequin','Survivor')", ct, ("m", match), ("r", room));
        await CommunityEndpoints.Exec(c,tx,"""
            INSERT IGNORE INTO match_rating_members(match_id,user_id,role,rating,games)
            SELECT @m,m.user_id,m.assigned_role,COALESCE(p.rating,1000),COALESCE(p.games,0)
            FROM room_members m JOIN random_match_rooms q ON q.room_id=m.room_id
            LEFT JOIN player_match_ratings p ON p.user_id=m.user_id AND p.role=m.assigned_role
            WHERE m.room_id=@r AND m.is_observer=FALSE;
            """,ct,("m",match),("r",room));
    }
    internal static async Task ApplyMatchRatingAsync(MySqlConnection c,MySqlTransaction tx,string match,ulong user,bool success,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT role,rating,applied FROM match_rating_members WHERE match_id=@m AND user_id=@u FOR UPDATE",ct,("m",match),("u",user));
        if(rows.Count!=1 || Convert.ToBoolean(rows[0]["applied"]))return;
        string role=Convert.ToString(rows[0]["role"])!;double own=Convert.ToDouble(rows[0]["rating"]);
        var others=await CommunityEndpoints.Rows(c,tx,"SELECT rating FROM match_rating_members WHERE match_id=@m AND role<>@role",ct,("m",match),("role",role));
        double opponent=others.Count>0?others.Average(r=>Convert.ToDouble(r["rating"])):own;
        int change=(int)Math.Round(16*((success?1:0)-1/(1+Math.Pow(10,(opponent-own)/400))));
        await CommunityEndpoints.Exec(c,tx,"INSERT INTO player_match_ratings(user_id,role,rating,games) VALUES(@u,@role,@v,1) ON DUPLICATE KEY UPDATE rating=GREATEST(0,LEAST(3000,rating+@delta)),games=games+1;UPDATE match_rating_members SET applied=TRUE WHERE match_id=@m AND user_id=@u",ct,("u",user),("role",role),("v",Math.Clamp(1000+change,0,3000)),("delta",change),("m",match));
    }
}
