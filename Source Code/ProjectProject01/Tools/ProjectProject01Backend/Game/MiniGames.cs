using System.Text.Json;
using MySqlConnector;

/// <summary>
/// Offline phone mini games (Docs/MiniGames.md): one open run per game and account that can be paused and continued on
/// any PC, and weekly / all-time boards apart from the match boards.
/// - 움직였다 (spot): the server makes every round and grades the picks; the run's progress lives here.
/// - 정전 복구 (blackout): runs on the client; the server keeps its saved snapshot and bounds the finishing time.
/// - 한 칸씩 (steps): the server replays the submitted moves on the stage before it records anything.
/// - 어디서 났지 (listen): like 움직였다, the server picks every round and grades the answer.
/// - 다가가기 (approach): the mannequin's game; runs on the client, the server bounds the time.
/// - 정전 복구 무한 (endless), 셔터 내려 (shutter), 흉내 내기 (mimic): played to a score on the client; the server bounds
///   the score by the active time (ScorePace) and the time by the run's wall clock.
/// </summary>
internal static class MiniGames
{
    internal static readonly string[] Games = ["spot", "blackout", "steps", "listen", "approach", "endless", "shutter", "mimic"];
    // 정전 복구 cannot be won faster than this: three fuses to fetch and two seconds at the board for each.
    internal const int BlackoutMinimumMs = 12000;
    /// <summary>Endless-score games: the fewest active milliseconds one point can take (seating a fuse, stopping a runner, holding a pose).</summary>
    internal static readonly Dictionary<string, int> ScorePace = new() { ["endless"] = 3000, ["shutter"] = 600, ["mimic"] = 1200 };
    const int StateLimit = 32 * 1024;

    internal sealed record StartBody(string? Game, string? Stage);
    internal sealed record SaveBody(string? RunId, int ActiveMs, JsonElement? State);
    internal sealed record SpotBody(string? RunId, int Round, int[]? Picks, int ActiveMs);
    internal sealed record FinishBody(string? RunId, bool Cleared, int ActiveMs, int Battery, string[]? Moves, int Strikes, int Hints = 0, int Score = 0);
    internal sealed record ListenBody(string? RunId, int Round, int Direction, int ActiveMs);
    internal sealed record AbandonBody(string? Game);
    sealed record SpotState(int Seed, int Round, int Cleared, int Lives, int Score);

    internal static async Task EnsureAsync(MySqlConnection c, CancellationToken ct) => await CommunityEndpoints.Exec(c, null, """
        CREATE TABLE IF NOT EXISTS minigame_runs(id CHAR(32) PRIMARY KEY,user_id BIGINT UNSIGNED NOT NULL,game VARCHAR(12) NOT NULL,stage VARCHAR(8) NOT NULL DEFAULT '',server_state VARCHAR(400) NULL,client_state MEDIUMTEXT NULL,active_ms INT NOT NULL DEFAULT 0,started_at DATETIME(6) NOT NULL,saved_at DATETIME(6) NOT NULL,finished_at DATETIME(6) NULL,INDEX(user_id,game,finished_at),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS minigame_scores(user_id BIGINT UNSIGNED NOT NULL,game VARCHAR(12) NOT NULL,board VARCHAR(16) NOT NULL,period CHAR(10) NOT NULL,primary_value INT NOT NULL,secondary_value INT NOT NULL,achieved_at DATETIME(6) NOT NULL,PRIMARY KEY(user_id,game,board,period),INDEX(game,board,period),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """, ct);

    // ---- boards: which way is better for each value ----
    // spot: rounds cleared (more), then score (more). blackout: time (less), then battery left (more).
    // steps stage boards: actions (fewer), then hints used (fewer). steps "stars": stars (more), then total actions (fewer).
    internal static (bool PrimaryHigher, bool SecondaryHigher) Order(string game, string board) => game switch
    {
        "spot" => (true, true),
        "blackout" => (false, true),
        "listen" => (true, true),
        "approach" => (false, false), // time, then times seen moving
        "endless" => (true, true),   // 정전 복구 무한: fuses seated, then time survived
        "shutter" => (true, true),   // 셔터 내려: mannequins stopped, then time survived
        "mimic" => (true, false),    // 흉내 내기: poses held, then time taken
        _ => board == "stars" ? (true, false) : (false, false),
    };
    internal static bool Better(string game, string board, (int P, int S) value, (int P, int S) than)
    {
        var (ph, sh) = Order(game, board);
        if (value.P != than.P) return ph ? value.P > than.P : value.P < than.P;
        return sh ? value.S > than.S : value.S < than.S;
    }
    static string WeekPeriod() => MatchAffixes.WeekStart(DateTime.UtcNow).ToString("yyyy-MM-dd");

    static async Task RecordAsync(MySqlConnection c, MySqlTransaction tx, ulong user, string game, string board, int primary, int secondary, CancellationToken ct)
    {
        foreach (var period in new[] { "all", WeekPeriod() })
        {
            var rows = await CommunityEndpoints.Rows(c, tx, "SELECT primary_value p,secondary_value s FROM minigame_scores WHERE user_id=@u AND game=@g AND board=@b AND period=@p FOR UPDATE", ct,
                ("u", user), ("g", game), ("b", board), ("p", period));
            if (rows.Count > 0 && !Better(game, board, (primary, secondary), (Convert.ToInt32(rows[0]["p"]), Convert.ToInt32(rows[0]["s"])))) continue;
            await CommunityEndpoints.Exec(c, tx, "INSERT INTO minigame_scores(user_id,game,board,period,primary_value,secondary_value,achieved_at) VALUES(@u,@g,@b,@p,@v,@w,UTC_TIMESTAMP(6)) ON DUPLICATE KEY UPDATE primary_value=@v,secondary_value=@w,achieved_at=UTC_TIMESTAMP(6)", ct,
                ("u", user), ("g", game), ("b", board), ("p", period), ("v", primary), ("w", secondary));
        }
    }

    /// <summary>한 칸씩: the stars board is the sum of the player's best stage results, per period.</summary>
    static async Task RecordStarsAsync(MySqlConnection c, MySqlTransaction tx, ulong user, CancellationToken ct)
    {
        foreach (var period in new[] { "all", WeekPeriod() })
        {
            var rows = await CommunityEndpoints.Rows(c, tx, "SELECT board,primary_value p,secondary_value s FROM minigame_scores WHERE user_id=@u AND game='steps' AND board LIKE 'stage-%' AND period=@p", ct, ("u", user), ("p", period));
            int stars = 0, actions = 0;
            foreach (var row in rows)
            {
                var stage = MiniGameRules.Stages.FirstOrDefault(s => "stage-" + s.Id == Convert.ToString(row["board"]));
                if (stage is null) continue;
                int a = Convert.ToInt32(row["p"]); stars += MiniGameRules.Stars(stage, a, Convert.ToInt32(row["s"])); actions += a;
            }
            await CommunityEndpoints.Exec(c, tx, "INSERT INTO minigame_scores(user_id,game,board,period,primary_value,secondary_value,achieved_at) VALUES(@u,'steps','stars',@p,@v,@w,UTC_TIMESTAMP(6)) ON DUPLICATE KEY UPDATE primary_value=@v,secondary_value=@w,achieved_at=UTC_TIMESTAMP(6)", ct,
                ("u", user), ("p", period), ("v", stars), ("w", actions));
        }
    }

    static object SpotView(SpotState s)
    {
        var round = MiniGameRules.MakeSpotRound(s.Seed, s.Round);
        // The changed figures are not sent: the picture pair is all the client needs, and the grade comes from here.
        return new { round = s.Round, cleared = s.Cleared, lives = s.Lives, score = s.Score, before = round.Before, after = round.After, changes = round.Changed.Length };
    }
    static SpotState ReadSpot(object? json) => JsonSerializer.Deserialize<SpotState>(Convert.ToString(json) ?? "{}")!;
    // 어디서 났지 keeps the same progress record as 움직였다 (seed, round, cleared, lives, score).
    static object ListenView(SpotState s)
    {
        var round = MiniGameRules.MakeListenRound(s.Seed, s.Round);
        return new { round = s.Round, cleared = s.Cleared, lives = s.Lives, score = s.Score, gain = round.Gain, length = round.Length, sounds = round.Sounds };
    }

    static object RunView(Dictionary<string, object?> run)
    {
        string game = Convert.ToString(run["game"])!;
        object? state = null;
        if (run["client_state"] is string raw && raw.Length > 0) { try { state = JsonSerializer.Deserialize<JsonElement>(raw); } catch (JsonException) { } }
        return new
        {
            id = Convert.ToString(run["id"]), game, stage = Convert.ToString(run["stage"]), activeMs = Convert.ToInt32(run["active_ms"]), state,
            spot = game == "spot" ? SpotView(ReadSpot(run["server_state"])) : null,
            listen = game == "listen" ? ListenView(ReadSpot(run["server_state"])) : null,
        };
    }

    static async Task<Dictionary<string, object?>?> OpenRunAsync(MySqlConnection c, MySqlTransaction? tx, ulong user, string? id, CancellationToken ct, bool lockRow = true)
    {
        if (string.IsNullOrEmpty(id) || id.Length != 32) return null;
        var rows = await CommunityEndpoints.Rows(c, tx, "SELECT * FROM minigame_runs WHERE id=@i AND user_id=@u AND finished_at IS NULL" + (lockRow ? " FOR UPDATE" : ""), ct, ("i", id), ("u", user));
        return rows.Count == 0 ? null : rows[0];
    }
    /// <summary>Active play time can only grow, and never faster than the real time since the run started.</summary>
    static bool PlausibleActive(Dictionary<string, object?> run, int activeMs)
    {
        var started = Convert.ToDateTime(run["started_at"]); int before = Convert.ToInt32(run["active_ms"]);
        return activeMs >= before && activeMs <= (DateTime.UtcNow - DateTime.SpecifyKind(started, DateTimeKind.Utc)).TotalMilliseconds + 3000;
    }

    internal static async Task<object> BestAsync(MySqlConnection c, ulong user, CancellationToken ct)
    {
        var rows = await CommunityEndpoints.Rows(c, null, "SELECT game,board,period,primary_value p,secondary_value s FROM minigame_scores WHERE user_id=@u AND (period='all' OR period=@w)", ct, ("u", user), ("w", WeekPeriod()));
        return rows.Select(r => new { game = r["game"], board = r["board"], weekly = Convert.ToString(r["period"]) != "all", primary = Convert.ToInt32(r["p"]), secondary = Convert.ToInt32(r["s"]) }).ToList();
    }

    internal static void Map(WebApplication app)
    {
        // Everything the phone needs: open runs (to continue), personal bests, the stages.
        app.MapGet("/api/minigames", async (HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            var runs = new List<object>();
            foreach (var run in await CommunityEndpoints.Rows(c, null, "SELECT * FROM minigame_runs WHERE user_id=@u AND finished_at IS NULL", ct, ("u", user.Id))) runs.Add(RunView(run));
            return Results.Ok(new
            {
                week = WeekPeriod(), runs, best = await BestAsync(c, user.Id, ct),
                stages = MiniGameRules.Stages.Select(s => new { id = s.Id, name = s.Name, map = s.Map, facing = s.Facing.ToString(), shutters = s.Shutters, par = s.Par }),
            });
        }).RequireRateLimiting("social");

        // A new run replaces the game's open run (one per game). 한 칸씩 also names the stage.
        app.MapPost("/api/minigames/start", async (StartBody body, HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            if (!Games.Contains(body.Game)) return Results.BadRequest(new { message = "미니게임을 확인하세요." });
            string stage = body.Game == "steps" ? body.Stage ?? "" : "";
            if (body.Game == "steps" && MiniGameRules.Stages.All(s => s.Id != stage)) return Results.BadRequest(new { message = "스테이지를 확인하세요." });
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            await CommunityEndpoints.Exec(c, tx, "UPDATE minigame_runs SET finished_at=UTC_TIMESTAMP(6) WHERE user_id=@u AND game=@g AND finished_at IS NULL", ct, ("u", user.Id), ("g", body.Game));
            string id = Guid.NewGuid().ToString("N");
            string? server = body.Game is "spot" or "listen" ? JsonSerializer.Serialize(new SpotState(Random.Shared.Next(1, int.MaxValue), 1, 0, body.Game == "spot" ? MiniGameRules.SpotLives : MiniGameRules.ListenLives, 0)) : null;
            await CommunityEndpoints.Exec(c, tx, "INSERT INTO minigame_runs(id,user_id,game,stage,server_state,started_at,saved_at) VALUES(@i,@u,@g,@s,@v,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))", ct,
                ("i", id), ("u", user.Id), ("g", body.Game), ("s", stage), ("v", server));
            var run = (await CommunityEndpoints.Rows(c, tx, "SELECT * FROM minigame_runs WHERE id=@i", ct, ("i", id)))[0];
            await tx.CommitAsync(ct);
            return Results.Ok(new { run = RunView(run) });
        }).RequireRateLimiting("social");

        // The client's snapshot of an open run (pause, round or step end), so it can be continued later or elsewhere.
        app.MapPost("/api/minigames/save", async (SaveBody body, HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            string state = body.State is { } s ? s.GetRawText() : "";
            if (state.Length > StateLimit) return Results.BadRequest(new { message = "저장할 내용이 너무 큽니다." });
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            var run = await OpenRunAsync(c, tx, user.Id, body.RunId, ct);
            if (run is null) return Results.Conflict(new { message = "이미 끝났거나 새로 시작한 게임입니다." });
            if (!PlausibleActive(run, body.ActiveMs)) return Results.BadRequest(new { message = "플레이 시간을 확인하세요." });
            await CommunityEndpoints.Exec(c, tx, "UPDATE minigame_runs SET client_state=@s,active_ms=@a,saved_at=UTC_TIMESTAMP(6) WHERE id=@i", ct, ("s", state), ("a", body.ActiveMs), ("i", body.RunId));
            await tx.CommitAsync(ct);
            return Results.Ok(new { saved = true });
        }).RequireRateLimiting("social");

        // 움직였다: grade this round's picks. A wrong answer costs a life; the third ends the run and records it.
        app.MapPost("/api/minigames/spot/answer", async (SpotBody body, HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            if (body.Picks is null || body.Picks.Length > 12) return Results.BadRequest(new { message = "고른 마네킹을 확인하세요." });
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            var run = await OpenRunAsync(c, tx, user.Id, body.RunId, ct);
            if (run is null || Convert.ToString(run["game"]) != "spot") return Results.Conflict(new { message = "이미 끝났거나 새로 시작한 게임입니다." });
            var state = ReadSpot(run["server_state"]);
            // A repeated request for an earlier round (lost response) is not graded twice.
            if (body.Round != state.Round) return Results.Conflict(new { message = "이미 채점한 라운드입니다.", run = RunView(run) });
            var round = MiniGameRules.MakeSpotRound(state.Seed, state.Round);
            bool correct = body.Picks.Distinct().OrderBy(i => i).SequenceEqual(round.Changed);
            var next = correct ? state with { Round = state.Round + 1, Cleared = state.Cleared + 1, Score = state.Score + MiniGameRules.SpotPoints(state.Round) }
                               : state with { Round = state.Round + 1, Lives = state.Lives - 1 };
            int active = Math.Max(Convert.ToInt32(run["active_ms"]), PlausibleActive(run, body.ActiveMs) ? body.ActiveMs : 0);
            bool over = next.Lives <= 0;
            await CommunityEndpoints.Exec(c, tx, "UPDATE minigame_runs SET server_state=@s,client_state=NULL,active_ms=@a,saved_at=UTC_TIMESTAMP(6),finished_at=IF(@o,UTC_TIMESTAMP(6),NULL) WHERE id=@i", ct,
                ("s", JsonSerializer.Serialize(next)), ("a", active), ("o", over), ("i", body.RunId));
            if (over && next.Cleared > 0) await RecordAsync(c, tx, user.Id, "spot", "all", next.Cleared, next.Score, ct);
            await tx.CommitAsync(ct);
            return Results.Ok(new { correct, changed = round.Changed, over, cleared = next.Cleared, score = next.Score, lives = next.Lives, spot = over ? null : SpotView(next) });
        }).RequireRateLimiting("social");

        // 어디서 났지: grade the direction picked for this round. A wrong one costs a life; the third ends the run.
        app.MapPost("/api/minigames/listen/answer", async (ListenBody body, HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            if (body.Direction is < 0 or > 7) return Results.BadRequest(new { message = "방향을 확인하세요." });
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            var run = await OpenRunAsync(c, tx, user.Id, body.RunId, ct);
            if (run is null || Convert.ToString(run["game"]) != "listen") return Results.Conflict(new { message = "이미 끝났거나 새로 시작한 게임입니다." });
            var state = ReadSpot(run["server_state"]);
            if (body.Round != state.Round) return Results.Conflict(new { message = "이미 채점한 라운드입니다.", run = RunView(run) });
            var round = MiniGameRules.MakeListenRound(state.Seed, state.Round);
            bool correct = body.Direction == round.Direction;
            var next = correct ? state with { Round = state.Round + 1, Cleared = state.Cleared + 1, Score = state.Score + MiniGameRules.ListenPoints(state.Round) }
                               : state with { Round = state.Round + 1, Lives = state.Lives - 1 };
            int active = Math.Max(Convert.ToInt32(run["active_ms"]), PlausibleActive(run, body.ActiveMs) ? body.ActiveMs : 0);
            bool over = next.Lives <= 0;
            await CommunityEndpoints.Exec(c, tx, "UPDATE minigame_runs SET server_state=@s,client_state=NULL,active_ms=@a,saved_at=UTC_TIMESTAMP(6),finished_at=IF(@o,UTC_TIMESTAMP(6),NULL) WHERE id=@i", ct,
                ("s", JsonSerializer.Serialize(next)), ("a", active), ("o", over), ("i", body.RunId));
            if (over && next.Cleared > 0) await RecordAsync(c, tx, user.Id, "listen", "all", next.Cleared, next.Score, ct);
            await tx.CommitAsync(ct);
            return Results.Ok(new { correct, direction = round.Direction, over, cleared = next.Cleared, score = next.Score, lives = next.Lives, listen = over ? null : ListenView(next) });
        }).RequireRateLimiting("social");

        // 정전 복구, 한 칸씩 and 다가가기 end here. A cleared run is checked before it reaches a board.
        app.MapPost("/api/minigames/finish", async (FinishBody body, HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
            var run = await OpenRunAsync(c, tx, user.Id, body.RunId, ct);
            if (run is null) return Results.Conflict(new { message = "이미 끝났거나 새로 시작한 게임입니다." });
            string game = Convert.ToString(run["game"])!;
            if (game is "spot" or "listen") return Results.BadRequest(new { message = "라운드마다 채점하는 게임입니다." });
            object? result = null;
            if (body.Cleared && game == "blackout")
            {
                if (body.ActiveMs < BlackoutMinimumMs || !PlausibleActive(run, body.ActiveMs) || body.Battery is < 0 or > 100)
                    return Results.BadRequest(new { message = "기록을 확인할 수 없습니다." });
                await RecordAsync(c, tx, user.Id, "blackout", "all", body.ActiveMs, body.Battery, ct);
                result = new { timeMs = body.ActiveMs, battery = body.Battery };
            }
            else if (ScorePace.TryGetValue(game, out int pace))
            {
                // Played on the client: a score needs at least this much active time per point, and the time itself must be plausible.
                if (body.Score is < 0 or > 10000 || body.ActiveMs < body.Score * pace || !PlausibleActive(run, body.ActiveMs))
                    return Results.BadRequest(new { message = "기록을 확인할 수 없습니다." });
                if (body.Score > 0) await RecordAsync(c, tx, user.Id, game, "all", body.Score, body.ActiveMs, ct);
                result = new { score = body.Score, timeMs = body.ActiveMs };
            }
            else if (body.Cleared && game == "approach")
            {
                if (body.ActiveMs < MiniGameRules.ApproachMinimumMs || !PlausibleActive(run, body.ActiveMs) || body.Strikes is < 0 or > 50)
                    return Results.BadRequest(new { message = "기록을 확인할 수 없습니다." });
                await RecordAsync(c, tx, user.Id, "approach", "all", body.ActiveMs, body.Strikes, ct);
                result = new { timeMs = body.ActiveMs, strikes = body.Strikes };
            }
            else if (body.Cleared && game == "steps")
            {
                var stage = MiniGameRules.Stages.FirstOrDefault(s => s.Id == Convert.ToString(run["stage"]));
                if (stage is null || body.Moves is null || body.Moves.Length > 400) return Results.BadRequest(new { message = "기록을 확인할 수 없습니다." });
                var (outcome, actions) = MiniGameRules.Replay(stage, body.Moves);
                if (outcome != MiniGameRules.StepResult.Won) return Results.BadRequest(new { message = "출구에 닿지 않은 기록입니다." });
                int hints = Math.Clamp(body.Hints, 0, 99);
                await RecordAsync(c, tx, user.Id, "steps", "stage-" + stage.Id, actions, hints, ct);
                await RecordStarsAsync(c, tx, user.Id, ct);
                result = new { stage = stage.Id, actions, hints, stars = MiniGameRules.Stars(stage, actions, hints), par = stage.Par };
            }
            await CommunityEndpoints.Exec(c, tx, "UPDATE minigame_runs SET finished_at=UTC_TIMESTAMP(6),client_state=NULL WHERE id=@i", ct, ("i", body.RunId));
            await tx.CommitAsync(ct);
            return Results.Ok(new { finished = true, result, best = await BestAsync(c, user.Id, ct) });
        }).RequireRateLimiting("social");

        app.MapPost("/api/minigames/abandon", async (AbandonBody body, HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            if (!Games.Contains(body.Game)) return Results.BadRequest(new { message = "미니게임을 확인하세요." });
            await using var c = await db.OpenConnectionAsync(ct);
            await CommunityEndpoints.Exec(c, null, "UPDATE minigame_runs SET finished_at=UTC_TIMESTAMP(6) WHERE user_id=@u AND game=@g AND finished_at IS NULL", ct, ("u", user.Id), ("g", body.Game));
            return Results.Ok(new { abandoned = true });
        }).RequireRateLimiting("social");

        // Top 20 of a board and the caller's own place on it.
        app.MapGet("/api/minigames/leaderboard", async (string? game, string? board, string? period, HttpRequest request, AuthDatabase db, CancellationToken ct) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct); if (user is null) return Results.Unauthorized();
            if (!Games.Contains(game)) return Results.BadRequest(new { message = "미니게임을 확인하세요." });
            board ??= game == "steps" ? "stars" : "all";
            if (game != "steps" ? board != "all" : board != "stars" && MiniGameRules.Stages.All(s => "stage-" + s.Id != board)) return Results.BadRequest(new { message = "순위표를 확인하세요." });
            string key = period == "weekly" ? WeekPeriod() : "all";
            var (ph, sh) = Order(game!, board);
            string order = $"primary_value {(ph ? "DESC" : "ASC")},secondary_value {(sh ? "DESC" : "ASC")},achieved_at ASC";
            await using var c = await db.OpenConnectionAsync(ct);
            var top = await CommunityEndpoints.Rows(c, null, $"SELECT s.user_id,u.display_name name,s.primary_value p,s.secondary_value v FROM minigame_scores s JOIN users u ON u.id=s.user_id WHERE s.game=@g AND s.board=@b AND s.period=@p ORDER BY {order} LIMIT 20", ct,
                ("g", game), ("b", board), ("p", key));
            var mine = await CommunityEndpoints.Rows(c, null, "SELECT primary_value p,secondary_value v,achieved_at a FROM minigame_scores WHERE user_id=@u AND game=@g AND board=@b AND period=@p", ct, ("u", user.Id), ("g", game), ("b", board), ("p", key));
            object? me = null;
            if (mine.Count > 0)
            {
                int p = Convert.ToInt32(mine[0]["p"]), v = Convert.ToInt32(mine[0]["v"]);
                string pc = ph ? ">" : "<", vc = sh ? ">" : "<";
                var ahead = await CommunityEndpoints.Rows(c, null, $"SELECT COUNT(*) n FROM minigame_scores WHERE game=@g AND board=@b AND period=@p AND (primary_value{pc}@x OR (primary_value=@x AND (secondary_value{vc}@y OR (secondary_value=@y AND achieved_at<@a))))", ct,
                    ("g", game), ("b", board), ("p", key), ("x", p), ("y", v), ("a", mine[0]["a"]));
                me = new { rank = Convert.ToInt32(ahead[0]["n"]) + 1, primary = p, secondary = v };
            }
            return Results.Ok(new
            {
                game, board, period = period == "weekly" ? "weekly" : "all", me,
                top = top.Select((r, i) => new { rank = i + 1, name = Convert.ToString(r["name"]), primary = Convert.ToInt32(r["p"]), secondary = Convert.ToInt32(r["v"]), mine = Convert.ToUInt64(r["user_id"]) == user.Id }),
            });
        }).RequireRateLimiting("social");
    }
}
