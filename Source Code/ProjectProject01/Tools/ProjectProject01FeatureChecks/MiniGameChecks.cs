using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

/// <summary>
/// Phone mini games (Docs/MiniGames.md): the 한 칸씩 rules and stages, saved runs that can be continued, server grading
/// of 움직였다, checked finishes, and the weekly / all-time boards.
/// </summary>
internal static class MiniGameChecks
{
    /// <summary>Rules only, no database: every stage can be won in exactly its par, and the replay refuses illegal moves.</summary>
    internal static void RunRules()
    {
        int count = 0; void Check(bool ok, string name) { if (!ok) throw new Exception(name); count++; Console.WriteLine("PASS " + name); }
        foreach (var stage in MiniGameRules.Stages)
        {
            var solution = MiniGameRules.Solve(stage, 40);
            Check(solution is not null, $"한 칸씩 {stage.Id}: 풀 수 있음");
            var (result, actions) = MiniGameRules.Replay(stage, solution!);
            Check(result == MiniGameRules.StepResult.Won && actions == stage.Par, $"한 칸씩 {stage.Id}: 최소 행동 수가 기준 수({stage.Par})와 같음");
            Check(stage.Map.All(r => r.Length == stage.Map[0].Length) && stage.Map.Sum(r => r.Count(ch => ch == 'S')) == 1 && stage.Map.Sum(r => r.Count(ch => ch == 'E')) == 1, $"한 칸씩 {stage.Id}: 맵 모양과 시작·출구 하나");
            if (stage.Shutters > 0) Check(solution!.Contains("S"), $"한 칸씩 {stage.Id}: 셔터 스테이지의 최단 풀이는 셔터를 씀");
        }
        var first = MiniGameRules.Stages[0];
        Check(MiniGameRules.Replay(first, ["L", "L", "L"]).Result == MiniGameRules.StepResult.Invalid, "벽으로 걷기는 거부");
        Check(MiniGameRules.Replay(first, ["S"]).Result == MiniGameRules.StepResult.Invalid, "셔터 없는 스테이지에서 셔터 거부");
        Check(MiniGameRules.Replay(first, ["tR"]).Result == MiniGameRules.StepResult.Invalid, "이미 보고 있는 방향으로 돌기 거부");
        var turn = new MiniGameRules.StepState(first); turn.Apply("tU");
        Check(turn.Actions == 0, "제자리 방향 전환은 행동 수에 들어가지 않음");
        // Watching a mannequin freezes it; it moves as soon as the player looks away.
        var watch = new MiniGameRules.StepState(new("t", "t", ["#######", "#..M..#", "#.....#", "#..S..#", "#######"], 'U', 0, 0));
        watch.Apply("tL"); watch.Apply("tU"); var frozen = watch.Mannequins[0];
        Check(frozen == (3, 1), "보고 있는 마네킹은 제자리");
        watch.Apply("L"); Check(watch.Mannequins[0] != (3, 1), "시선을 돌리면 마네킹이 다가옴");
        Check(MiniGameRules.Stars(first, first.Par) == 3 && MiniGameRules.Stars(first, first.Par + 3) == 2 && MiniGameRules.Stars(first, first.Par + 4) == 1, "별: 기준 수 3개, +3까지 2개, 그 밖 1개");
        var a = MiniGameRules.MakeSpotRound(42, 3); var b = MiniGameRules.MakeSpotRound(42, 3);
        Check(JsonSerializer.Serialize(a) == JsonSerializer.Serialize(b), "움직였다: 같은 판·라운드는 같은 그림");
        var late = MiniGameRules.MakeSpotRound(42, 9);
        Check(late.Before.Length > a.Before.Length && late.Changed.Length > a.Changed.Length, "움직였다: 라운드가 오를수록 마네킹과 변화가 늘어남");
        Check(Enumerable.Range(1, 12).All(r => { var x = MiniGameRules.MakeSpotRound(7, r); return x.Changed.All(i => x.Before[i] != x.After[i]) && Enumerable.Range(0, x.Before.Length).Except(x.Changed).All(i => x.Before[i] == x.After[i]); }),
            "움직였다: 바뀐 마네킹만 다르고 나머지는 그대로");
        var l1 = MiniGameRules.MakeListenRound(42, 1); var l7 = MiniGameRules.MakeListenRound(42, 7);
        Check(JsonSerializer.Serialize(l1) == JsonSerializer.Serialize(MiniGameRules.MakeListenRound(42, 1)), "어디서 났지: 같은 판·라운드는 같은 소리");
        Check(l1.Sounds.Length == 1 && l1.Sounds[0].Kind == "creak" && l1.Sounds[0].Direction == l1.Direction, "1라운드는 삐걱 소리 하나");
        Check(l7.Sounds.Length == 3 && l7.Sounds.Count(x => x.Kind == "creak") == 1 && l7.Sounds.Select(x => x.Direction).Distinct().Count() == 3, "6라운드부터 다른 방향의 가짜 소리 둘");
        Check(l7.Gain < l1.Gain && l7.Length < l1.Length, "라운드가 오를수록 작고 짧은 소리");
        Check(Enumerable.Range(1, 40).All(r => { var x = MiniGameRules.MakeListenRound(9, r); return x.Direction is >= 0 and <= 7 && x.Gain >= .35 && x.Length >= .25; }), "방향 0~7, 소리 크기·길이 하한 유지");
        Console.WriteLine($"PASS: {count} mini game rule checks");
    }

    internal static async Task RunAsync(AuthDatabase db, HttpClient admin)
    {
        int count = 0; void Check(bool ok, string name) { if (!ok) throw new Exception(name); count++; Console.WriteLine("PASS " + name); }
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending) { using var r = await pending; string t = await r.Content.ReadAsStringAsync(); if (!r.IsSuccessStatusCode) throw new Exception($"Mini game API {(int)r.StatusCode}: {t}"); return JsonDocument.Parse(t).RootElement.Clone(); }
        async Task Expect(Task<HttpResponseMessage> pending, HttpStatusCode code, string name) { using var r = await pending; Check(r.StatusCode == code, name + " " + r.StatusCode); }
        async Task Sql(string sql, params (string, object?)[] p) { await using var c = await db.OpenConnectionAsync(default); await CommunityEndpoints.Exec(c, null, sql, default, p); }
        var one = await OperationsIntegration.Player(db); var two = await OperationsIntegration.Player(db);
        using var p1 = new HttpClient { BaseAddress = admin.BaseAddress }; p1.DefaultRequestHeaders.Authorization = new("Bearer", one.Token);
        using var p2 = new HttpClient { BaseAddress = admin.BaseAddress }; p2.DefaultRequestHeaders.Authorization = new("Bearer", two.Token);
        // Lets a run look as if it started a while ago, so play times up to that long are possible.
        Task Age(string run, int seconds) => Sql("UPDATE minigame_runs SET started_at=UTC_TIMESTAMP(6)-INTERVAL @s SECOND WHERE id=@i", ("s", seconds), ("i", run));
        try
        {
            var home = await Read(p1.GetAsync("/api/minigames"));
            Check(home.GetProperty("runs").GetArrayLength() == 0 && home.GetProperty("stages").GetArrayLength() == MiniGameRules.Stages.Length, "처음에는 이어할 판 없음, 스테이지 목록 전달");
            await Expect(p1.PostAsJsonAsync("/api/minigames/start", new { game = "chess" }), HttpStatusCode.BadRequest, "없는 미니게임 시작 거부");
            await Expect(p1.PostAsJsonAsync("/api/minigames/start", new { game = "steps", stage = "99" }), HttpStatusCode.BadRequest, "없는 스테이지 시작 거부");

            // 정전 복구: save, continue, finish.
            string black = (await Read(p1.PostAsJsonAsync("/api/minigames/start", new { game = "blackout" }))).GetProperty("run").GetProperty("id").GetString()!;
            await Age(black, 60);
            await Read(p1.PostAsJsonAsync("/api/minigames/save", new { runId = black, activeMs = 9000, state = new { x = 120, y = 300, battery = 71, fuses = 1 } }));
            var resumed = (await Read(p1.GetAsync("/api/minigames"))).GetProperty("runs").EnumerateArray().Single(r => r.GetProperty("game").GetString() == "blackout");
            Check(resumed.GetProperty("activeMs").GetInt32() == 9000 && resumed.GetProperty("state").GetProperty("battery").GetInt32() == 71, "멈춘 판을 다시 불러와 그 자리부터 이어함");
            await Expect(p1.PostAsJsonAsync("/api/minigames/save", new { runId = black, activeMs = 8000, state = new { } }), HttpStatusCode.BadRequest, "플레이 시간이 줄어드는 저장 거부");
            await Expect(p1.PostAsJsonAsync("/api/minigames/save", new { runId = black, activeMs = 600000, state = new { } }), HttpStatusCode.BadRequest, "실제 시간보다 긴 플레이 시간 거부");
            await Expect(p2.PostAsJsonAsync("/api/minigames/save", new { runId = black, activeMs = 9500, state = new { } }), HttpStatusCode.Conflict, "남의 판에는 저장할 수 없음");
            await Expect(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = black, cleared = true, activeMs = 5000, battery = 50 }), HttpStatusCode.BadRequest, "불가능하게 짧은 정전 복구 기록 거부");
            var done = await Read(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = black, cleared = true, activeMs = 41000, battery = 33 }));
            Check(done.GetProperty("result").GetProperty("timeMs").GetInt32() == 41000, "정전 복구 성공 기록");
            await Expect(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = black, cleared = true, activeMs = 30000, battery = 90 }), HttpStatusCode.Conflict, "끝난 판은 다시 끝낼 수 없음");
            string second = (await Read(p2.PostAsJsonAsync("/api/minigames/start", new { game = "blackout" }))).GetProperty("run").GetProperty("id").GetString()!;
            await Age(second, 60); await Read(p2.PostAsJsonAsync("/api/minigames/finish", new { runId = second, cleared = true, activeMs = 35000, battery = 10 }));
            var board = await Read(p1.GetAsync("/api/minigames/leaderboard?game=blackout&period=all"));
            var top = board.GetProperty("top").EnumerateArray().ToList();
            Check(top[0].GetProperty("primary").GetInt32() == 35000 && board.GetProperty("me").GetProperty("rank").GetInt32() == 2, "정전 복구 순위: 짧은 시간이 위, 내 순위 표시");
            Check((await Read(p1.GetAsync("/api/minigames/leaderboard?game=blackout&period=weekly"))).GetProperty("top").GetArrayLength() == 2, "주간 순위표에도 이번 주 기록");
            await Sql("UPDATE minigame_scores SET period='2000-01-03' WHERE user_id=@u AND game='blackout' AND period<>'all'", ("u", two.Id));
            Check((await Read(p1.GetAsync("/api/minigames/leaderboard?game=blackout&period=weekly"))).GetProperty("top").GetArrayLength() == 1
                && (await Read(p1.GetAsync("/api/minigames/leaderboard?game=blackout&period=all"))).GetProperty("top").GetArrayLength() == 2, "지난주 기록은 주간에서 빠지고 전체에는 남음");
            string slower = (await Read(p1.PostAsJsonAsync("/api/minigames/start", new { game = "blackout" }))).GetProperty("run").GetProperty("id").GetString()!;
            await Age(slower, 90); await Read(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = slower, cleared = true, activeMs = 60000, battery = 90 }));
            Check((await Read(p1.GetAsync("/api/minigames/leaderboard?game=blackout&period=all"))).GetProperty("me").GetProperty("primary").GetInt32() == 41000, "더 나쁜 기록은 최고 기록을 덮지 않음");

            // 한 칸씩: only a replayed win counts; stars add up per stage.
            var stage = MiniGameRules.Stages[0]; var solution = MiniGameRules.Solve(stage)!;
            string steps = (await Read(p1.PostAsJsonAsync("/api/minigames/start", new { game = "steps", stage = stage.Id }))).GetProperty("run").GetProperty("id").GetString()!;
            await Read(p1.PostAsJsonAsync("/api/minigames/save", new { runId = steps, activeMs = 0, state = new { moves = solution.Take(2) } }));
            var stepRun = (await Read(p1.GetAsync("/api/minigames"))).GetProperty("runs").EnumerateArray().Single(r => r.GetProperty("game").GetString() == "steps");
            Check(stepRun.GetProperty("stage").GetString() == stage.Id && stepRun.GetProperty("state").GetProperty("moves").GetArrayLength() == 2, "한 칸씩: 스테이지와 지금까지 둔 수를 저장해 이어함");
            await Expect(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = steps, cleared = true, moves = solution.Take(2) }), HttpStatusCode.BadRequest, "출구에 닿지 않은 수순은 기록 거부");
            await Expect(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = steps, cleared = true, moves = new[] { "L", "L", "L", "L" } }), HttpStatusCode.BadRequest, "규칙에 어긋난 수순 기록 거부");
            var stepDone = await Read(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = steps, cleared = true, moves = solution }));
            Check(stepDone.GetProperty("result").GetProperty("stars").GetInt32() == 3 && stepDone.GetProperty("result").GetProperty("actions").GetInt32() == stage.Par, "한 칸씩: 서버가 다시 둬 보고 기준 수면 별 3개");
            var stars = await Read(p1.GetAsync("/api/minigames/leaderboard?game=steps&board=stars&period=all"));
            Check(stars.GetProperty("me").GetProperty("primary").GetInt32() == 3, "한 칸씩 별 합계 순위표");
            Check((await Read(p1.GetAsync($"/api/minigames/leaderboard?game=steps&board=stage-{stage.Id}&period=all"))).GetProperty("me").GetProperty("primary").GetInt32() == stage.Par, "스테이지별 최소 행동 수 순위표");
            await Expect(p1.GetAsync("/api/minigames/leaderboard?game=steps&board=stage-99"), HttpStatusCode.BadRequest, "없는 순위표 거부");

            // 움직였다: the server grades every round.
            var spot = (await Read(p1.PostAsJsonAsync("/api/minigames/start", new { game = "spot" }))).GetProperty("run");
            string spotId = spot.GetProperty("id").GetString()!; var view = spot.GetProperty("spot");
            Check(view.GetProperty("round").GetInt32() == 1 && view.GetProperty("lives").GetInt32() == MiniGameRules.SpotLives && !view.TryGetProperty("changed", out _), "움직였다: 1라운드 그림을 받고, 정답은 함께 오지 않음");
            int[] Diff(JsonElement v) { var b = v.GetProperty("before").EnumerateArray().ToList(); var a = v.GetProperty("after").EnumerateArray().ToList(); return Enumerable.Range(0, b.Count).Where(i => b[i].GetRawText() != a[i].GetRawText()).ToArray(); }
            var right = await Read(p1.PostAsJsonAsync("/api/minigames/spot/answer", new { runId = spotId, round = 1, picks = Diff(view) }));
            Check(right.GetProperty("correct").GetBoolean() && right.GetProperty("cleared").GetInt32() == 1 && right.GetProperty("spot").GetProperty("round").GetInt32() == 2, "맞히면 라운드 통과, 다음 라운드");
            await Expect(p1.PostAsJsonAsync("/api/minigames/spot/answer", new { runId = spotId, round = 1, picks = Diff(view) }), HttpStatusCode.Conflict, "같은 라운드는 두 번 채점하지 않음");
            var spotHome = (await Read(p1.GetAsync("/api/minigames"))).GetProperty("runs").EnumerateArray().Single(r => r.GetProperty("game").GetString() == "spot");
            Check(spotHome.GetProperty("spot").GetProperty("round").GetInt32() == 2, "움직였다: 다시 열면 진행 중인 라운드부터");
            var v2 = right.GetProperty("spot"); int round = 2; JsonElement last = right;
            for (int miss = 0; miss < MiniGameRules.SpotLives; miss++)
            {
                last = await Read(p1.PostAsJsonAsync("/api/minigames/spot/answer", new { runId = spotId, round, picks = Array.Empty<int>() }));
                round++;
            }
            Check(!last.GetProperty("correct").GetBoolean() && last.GetProperty("over").GetBoolean() && last.GetProperty("lives").GetInt32() == 0, "세 번 틀리면 끝");
            var spotBoard = await Read(p1.GetAsync("/api/minigames/leaderboard?game=spot&period=all"));
            Check(spotBoard.GetProperty("me").GetProperty("primary").GetInt32() == 1, "움직였다 순위표: 통과한 라운드 수");
            await Expect(p1.PostAsJsonAsync("/api/minigames/spot/answer", new { runId = spotId, round, picks = Array.Empty<int>() }), HttpStatusCode.Conflict, "끝난 판은 더 채점하지 않음");

            // 어디서 났지: the server grades each direction.
            var listen = (await Read(p1.PostAsJsonAsync("/api/minigames/start", new { game = "listen" }))).GetProperty("run");
            string listenId = listen.GetProperty("id").GetString()!; var lv = listen.GetProperty("listen");
            Check(lv.GetProperty("round").GetInt32() == 1 && lv.GetProperty("sounds").GetArrayLength() == 1, "어디서 났지: 1라운드 소리를 받음");
            int heard = lv.GetProperty("sounds")[0].GetProperty("direction").GetInt32();
            await Expect(p1.PostAsJsonAsync("/api/minigames/listen/answer", new { runId = listenId, round = 1, direction = 9 }), HttpStatusCode.BadRequest, "없는 방향 거부");
            var heardRight = await Read(p1.PostAsJsonAsync("/api/minigames/listen/answer", new { runId = listenId, round = 1, direction = heard }));
            Check(heardRight.GetProperty("correct").GetBoolean() && heardRight.GetProperty("cleared").GetInt32() == 1, "맞는 방향이면 통과");
            JsonElement lastListen = heardRight;
            for (int r = 2; r < 2 + MiniGameRules.ListenLives; r++)
            {
                var sounds = lastListen.GetProperty("listen").GetProperty("sounds").EnumerateArray().ToList();
                int creak = sounds.Single(x => x.GetProperty("kind").GetString() == "creak").GetProperty("direction").GetInt32();
                lastListen = await Read(p1.PostAsJsonAsync("/api/minigames/listen/answer", new { runId = listenId, round = r, direction = (creak + 4) % 8 }));
            }
            Check(lastListen.GetProperty("over").GetBoolean() && !lastListen.GetProperty("correct").GetBoolean(), "어디서 났지: 세 번 틀리면 끝");
            Check((await Read(p1.GetAsync("/api/minigames/leaderboard?game=listen&period=all"))).GetProperty("me").GetProperty("primary").GetInt32() == 1, "어디서 났지 순위표");
            await Expect(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = listenId, cleared = true }), HttpStatusCode.Conflict, "끝난 판은 마칠 수 없음");

            // 다가가기 (the mannequin's game): time and times seen moving.
            string near = (await Read(p1.PostAsJsonAsync("/api/minigames/start", new { game = "approach" }))).GetProperty("run").GetProperty("id").GetString()!;
            await Age(near, 60);
            await Expect(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = near, cleared = true, activeMs = 3000, strikes = 0 }), HttpStatusCode.BadRequest, "불가능하게 짧은 다가가기 기록 거부");
            Check((await Read(p1.PostAsJsonAsync("/api/minigames/finish", new { runId = near, cleared = true, activeMs = 21000, strikes = 2 }))).GetProperty("result").GetProperty("strikes").GetInt32() == 2, "다가가기 성공 기록(시간·들킨 횟수)");
            string near2 = (await Read(p2.PostAsJsonAsync("/api/minigames/start", new { game = "approach" }))).GetProperty("run").GetProperty("id").GetString()!;
            await Age(near2, 60); await Read(p2.PostAsJsonAsync("/api/minigames/finish", new { runId = near2, cleared = true, activeMs = 21000, strikes = 0 }));
            var nearBoard = await Read(p1.GetAsync("/api/minigames/leaderboard?game=approach&period=all"));
            Check(nearBoard.GetProperty("top")[0].GetProperty("secondary").GetInt32() == 0 && nearBoard.GetProperty("me").GetProperty("rank").GetInt32() == 2, "다가가기 순위: 같은 시간이면 덜 들킨 쪽이 위");

            // One open run per game; a new start replaces it, abandon closes it.
            string a1 = (await Read(p1.PostAsJsonAsync("/api/minigames/start", new { game = "blackout" }))).GetProperty("run").GetProperty("id").GetString()!;
            string a2 = (await Read(p1.PostAsJsonAsync("/api/minigames/start", new { game = "blackout" }))).GetProperty("run").GetProperty("id").GetString()!;
            var open = (await Read(p1.GetAsync("/api/minigames"))).GetProperty("runs").EnumerateArray().Where(r => r.GetProperty("game").GetString() == "blackout").ToList();
            Check(open.Count == 1 && open[0].GetProperty("id").GetString() == a2, "게임마다 이어할 판은 하나, 새로 시작하면 바뀜");
            await Read(p1.PostAsJsonAsync("/api/minigames/abandon", new { game = "blackout" }));
            Check((await Read(p1.GetAsync("/api/minigames"))).GetProperty("runs").EnumerateArray().All(r => r.GetProperty("game").GetString() != "blackout"), "그만두면 이어할 판이 사라짐");
            await Expect(p1.PostAsJsonAsync("/api/minigames/save", new { runId = a1, activeMs = 0, state = new { } }), HttpStatusCode.Conflict, "바뀐 판에는 저장할 수 없음");
            using var anonymous = new HttpClient { BaseAddress = admin.BaseAddress };
            await Expect(anonymous.GetAsync("/api/minigames"), HttpStatusCode.Unauthorized, "로그인하지 않으면 사용할 수 없음");
            Console.WriteLine($"PASS: {count} mini game checks (isolated API/MySQL)");
        }
        finally { await Sql("DELETE FROM users WHERE id IN (@a,@b)", ("a", one.Id), ("b", two.Id)); }
    }
}
