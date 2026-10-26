using MySqlConnector;

internal static class CaseMissionBudget
{
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_mission_budget_settings(id TINYINT PRIMARY KEY,weekly_limit INT NOT NULL DEFAULT 2000);
        INSERT IGNORE INTO case_mission_budget_settings(id) VALUES(1);
        CREATE TABLE IF NOT EXISTS case_mission_budget_usage(user_id BIGINT UNSIGNED NOT NULL,week_id VARCHAR(16) NOT NULL,earned BIGINT NOT NULL DEFAULT 0,
          PRIMARY KEY(user_id,week_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
    internal static async Task<int> LimitAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)=>Convert.ToInt32((await CommunityEndpoints.Rows(c,tx,"SELECT weekly_limit FROM case_mission_budget_settings WHERE id=1",ct))[0]["weekly_limit"]);
    // Call under the account lock, including reads that seed pre-upgrade mission receipts.
    internal static async Task<(long Earned,int Limit)> StateAsync(MySqlConnection c,MySqlTransaction tx,ulong user,DateTimeOffset now,CancellationToken ct)
    {
        var periods=CaseRecords.Periods(now);var rows=await CommunityEndpoints.Rows(c,tx,"SELECT earned FROM case_mission_budget_usage WHERE user_id=@u AND week_id=@w",ct,("u",user),("w",periods.Weekly));
        long earned;
        if(rows.Count>0)earned=Convert.ToInt64(rows[0]["earned"]);
        else
        {
            string monday="D:"+periods.Weekly[2..];
            var receipts=await CommunityEndpoints.Rows(c,tx,"SELECT reward_key FROM case_claims WHERE user_id=@u AND (reward_key LIKE @weekly OR (reward_key LIKE 'D:%' AND SUBSTRING(reward_key,1,12)>=@monday AND SUBSTRING(reward_key,1,12)<=@today))",ct,("u",user),("weekly",periods.Weekly+":%"),("monday",monday),("today",periods.Daily));
            var choices=await CommunityEndpoints.Rows(c,tx,"SELECT mode,period_id,slot_id,definition_id FROM case_mission_choices WHERE user_id=@u AND (period_id=@w OR period_id BETWEEN @monday AND @today)",ct,("u",user),("w",periods.Weekly),("monday",monday),("today",periods.Daily));
            earned=0;
            foreach(var r in receipts)
            {
                var key=((string)r["reward_key"]!).Split(':');if(key.Length!=4||key[2] is not ("Pvp" or "Bot"))continue;
                string period=key[0]+":"+key[1];var choice=choices.FirstOrDefault(v=>(string)v["period_id"]! ==period&&(string)v["mode"]! ==key[2]&&(string)v["slot_id"]! ==key[3]);
                earned+=CaseMissionSlots.RewardFor(choice is null?key[3]:(string)choice["definition_id"]!);
            }
            await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_mission_budget_usage(user_id,week_id,earned) VALUES(@u,@w,@n)",ct,("u",user),("w",periods.Weekly),("n",earned));
        }
        return(earned,await LimitAsync(c,tx,ct));
    }
    internal static async Task<int> GrantAsync(MySqlConnection c,MySqlTransaction tx,ulong user,int reward,DateTimeOffset now,CancellationToken ct)
    {
        var state=await StateAsync(c,tx,user,now,ct);int granted=(int)Math.Min(reward,Math.Max(0,state.Limit-state.Earned));
        if(granted>0)await CommunityEndpoints.Exec(c,tx,"UPDATE case_mission_budget_usage SET earned=earned+@n WHERE user_id=@u AND week_id=@w",ct,("n",granted),("u",user),("w",CaseRecords.Periods(now).Weekly));
        return granted;
    }
    internal sealed record Settings(int WeeklyLimit);
    internal static void Map(WebApplication app)
    {
        app.MapGet("/api/admin/case-records/mission-budget",async(HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();await using var c=await db.OpenConnectionAsync(ct);return Results.Ok(new{weeklyLimit=await LimitAsync(c,null,ct)});}).RequireRateLimiting("admin");
        app.MapPut("/api/admin/case-records/mission-budget",async(Settings body,HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();if(body.WeeklyLimit is <0 or >1000000)return Results.BadRequest();await using var c=await db.OpenConnectionAsync(ct);await CommunityEndpoints.Exec(c,null,"UPDATE case_mission_budget_settings SET weekly_limit=@n WHERE id=1",ct,("n",body.WeeklyLimit));return Results.Ok(new{weeklyLimit=body.WeeklyLimit});}).RequireRateLimiting("admin");
    }
}
