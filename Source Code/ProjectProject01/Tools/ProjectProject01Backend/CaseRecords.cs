using MySqlConnector;

// Server-owned prices, milestones and receipts. No currency/XP amount is accepted from a client.
internal static class CaseRecords
{
    internal const string Season="RECORD 0";
    internal sealed record Item(string Id,string Name,string Slot,int Price);
    internal static readonly Item[] Catalogue=[new("frame_crimson","붉은 봉인 테두리","Frame",150),new("frame_silver","조사관 테두리","Frame",120),new("icon_file","사건 기록 아이콘","Icon",60),new("icon_seal","증거 봉인 아이콘","Icon",100),new("title_witness","목격자 명찰","Nameplate",200)];
    internal sealed record Mission(string Id,string Name,string Metric,int Target,int Reward,bool Weekly);
    internal static readonly Mission[] Missions=[new("daily_play","오늘의 조사: 경기 1회 완료","Matches",1,30,false),new("daily_win","오늘의 성과: 경기 1회 성공","Wins",1,50,false),new("weekly_play","주간 조사: 경기 5회 완료","Matches",5,150,true),new("weekly_win","주간 성과: 경기 3회 성공","Wins",3,200,true)];
    internal static readonly HashSet<string> LocalAchievements=["campaign_checkpoint","campaign_escape","campaign_complete"];
    internal static readonly string[] AchievementIds=["campaign_checkpoint","campaign_escape","campaign_complete","pvp_first","bot_first","survivor_escape","mannequin_victory"];
    internal static int Level(long xp)=>Math.Min(100,1+(int)Math.Sqrt(Math.Clamp(xp,0,1_000_000_000)/100.0));
    internal static (string Daily,string Weekly) Periods(DateTimeOffset utc)
    {
        var date=DateOnly.FromDateTime(utc.ToOffset(TimeSpan.FromHours(9)).DateTime);
        int offset=((int)date.DayOfWeek+6)%7;
        return ("D:"+date.ToString("yyyy-MM-dd"),"W:"+date.AddDays(-offset).ToString("yyyy-MM-dd"));
    }
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_accounts(user_id BIGINT UNSIGNED PRIMARY KEY,xp BIGINT NOT NULL DEFAULT 0,credits BIGINT NOT NULL DEFAULT 0,
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_receipts(match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
          PRIMARY KEY(match_id,user_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_match_modes(match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,mode VARCHAR(8) NOT NULL);
        CREATE TABLE IF NOT EXISTS case_mission_progress(user_id BIGINT UNSIGNED NOT NULL,mode VARCHAR(8) NOT NULL,period_id VARCHAR(16) NOT NULL,
          metric VARCHAR(16) NOT NULL,value BIGINT NOT NULL DEFAULT 0,PRIMARY KEY(user_id,mode,period_id,metric),
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_claims(user_id BIGINT UNSIGNED NOT NULL,reward_key VARCHAR(100) NOT NULL,PRIMARY KEY(user_id,reward_key),
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_inventory(user_id BIGINT UNSIGNED NOT NULL,item_id VARCHAR(32) NOT NULL,PRIMARY KEY(user_id,item_id),
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_loadout(user_id BIGINT UNSIGNED NOT NULL,slot VARCHAR(16) NOT NULL,item_id VARCHAR(32) NOT NULL,PRIMARY KEY(user_id,slot),
          FOREIGN KEY(user_id,item_id) REFERENCES case_inventory(user_id,item_id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_achievements(user_id BIGINT UNSIGNED NOT NULL,code VARCHAR(40) NOT NULL,source VARCHAR(16) NOT NULL,
          earned_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),PRIMARY KEY(user_id,code),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_role_xp(user_id BIGINT UNSIGNED NOT NULL,role VARCHAR(16) NOT NULL,xp BIGINT NOT NULL DEFAULT 0,
          PRIMARY KEY(user_id,role),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_campaign_bindings(save_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,user_id BIGINT UNSIGNED NOT NULL,
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
    internal static async Task CaptureModeAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c,tx,"""
            INSERT IGNORE INTO case_match_modes(match_id,mode)
            SELECT r.match_id,IF(EXISTS(SELECT 1 FROM bot_rooms b WHERE b.room_id=r.id),'Bot','Pvp')
            FROM game_rooms r WHERE r.match_id=@m AND r.status='Started'
            """,ct,("m",match));
    }
    internal static async Task<Dictionary<string,object?>> LockAccountAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        // Acquire the account's exclusive lock directly; INSERT IGNORE + a later
        // S-to-X lock upgrade can deadlock simultaneous duplicate claim requests.
        await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_accounts(user_id) VALUES(@u) ON DUPLICATE KEY UPDATE user_id=VALUES(user_id)",ct,("u",user));
        return (await CommunityEndpoints.Rows(c,tx,"SELECT xp,credits FROM case_accounts WHERE user_id=@u FOR UPDATE",ct,("u",user)))[0];
    }
    internal static async Task AwardVerifiedAsync(MySqlConnection c,MySqlTransaction tx,string match,ulong user,CancellationToken ct)
    {
        // Re-read the immutable verified row, rather than caller-provided counters.
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT role,success FROM verified_match_results WHERE match_id=@m AND user_id=@u",ct,("m",match),("u",user));
        if(rows.Count!=1)return;
        await LockAccountAsync(c,tx,user,ct);
        if((await CommunityEndpoints.Rows(c,tx,"SELECT match_id FROM case_receipts WHERE match_id=@m AND user_id=@u",ct,("m",match),("u",user))).Count>0)return;
        var modeRows=await CommunityEndpoints.Rows(c,tx,"SELECT mode FROM case_match_modes WHERE match_id=@m",ct,("m",match));
        string mode=modeRows.Count==1?(string)modeRows[0]["mode"]!:"Pvp";
        string role=(string)rows[0]["role"]!;if(role is not ("Survivor" or "Mannequin"))return;
        bool won=Convert.ToBoolean(rows[0]["success"]);int xp=won?150:100;
        var nowRows=await CommunityEndpoints.Rows(c,tx,"SELECT UTC_TIMESTAMP(6) utc",ct);
        var periods=Periods(new DateTimeOffset(DateTime.SpecifyKind(Convert.ToDateTime(nowRows[0]["utc"]),DateTimeKind.Utc)));
        await CommunityEndpoints.Exec(c,tx,"""
            INSERT INTO case_receipts(match_id,user_id) VALUES(@m,@u);
            UPDATE case_accounts SET xp=LEAST(1000000000,xp+@xp) WHERE user_id=@u;
            INSERT INTO case_role_xp(user_id,role,xp) VALUES(@u,@role,@xp) ON DUPLICATE KEY UPDATE xp=LEAST(1000000000,xp+@xp);
            """,ct,("m",match),("u",user),("xp",xp),("role",role));
        foreach(string period in new[]{periods.Daily,periods.Weekly})
            foreach(var metric in new[]{("Matches",1),("Wins",won?1:0),(role+"Matches",1)})
                await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_mission_progress(user_id,mode,period_id,metric,value) VALUES(@u,@mode,@period,@metric,@v) ON DUPLICATE KEY UPDATE value=LEAST(1000000,value+@v)",ct,("u",user),("mode",mode),("period",period),("metric",metric.Item1),("v",metric.Item2));
        await Unlock(c,tx,user,mode=="Bot"?"bot_first":"pvp_first","Server",ct);
        if(won)await Unlock(c,tx,user,role=="Survivor"?"survivor_escape":"mannequin_victory","Server",ct);
    }
    private static Task Unlock(MySqlConnection c,MySqlTransaction tx,ulong user,string code,string source,CancellationToken ct)=>
        CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO case_achievements(user_id,code,source) VALUES(@u,@code,@source)",ct,("u",user),("code",code),("source",source));
    internal static async Task<object> PublicProfileAsync(MySqlConnection c,ulong user,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,null,"SELECT xp FROM case_accounts WHERE user_id=@u",ct,("u",user));long xp=rows.Count==0?0:Convert.ToInt64(rows[0]["xp"]);
        return new{level=Level(xp),roleExperience=await CommunityEndpoints.Rows(c,null,"SELECT role,xp FROM case_role_xp WHERE user_id=@u ORDER BY role",ct,("u",user)),loadout=await CommunityEndpoints.Rows(c,null,"SELECT slot,item_id itemId FROM case_loadout WHERE user_id=@u ORDER BY slot",ct,("u",user))};
    }
    private static async Task<object> Snapshot(MySqlConnection c,MySqlTransaction tx,ulong user,IConfiguration config,CancellationToken ct)
    {
        var account=await LockAccountAsync(c,tx,user,ct);long xp=Convert.ToInt64(account["xp"]);
        var dateRows=await CommunityEndpoints.Rows(c,tx,"SELECT UTC_TIMESTAMP(6) utc",ct);
        var now=new DateTimeOffset(DateTime.SpecifyKind(Convert.ToDateTime(dateRows[0]["utc"]),DateTimeKind.Utc));var periods=Periods(now);
        var progress=await CommunityEndpoints.Rows(c,tx,"SELECT mode,period_id periodId,metric,value FROM case_mission_progress WHERE user_id=@u AND period_id IN (@d,@w)",ct,("u",user),("d",periods.Daily),("w",periods.Weekly));
        var claims=await CommunityEndpoints.Rows(c,tx,"SELECT reward_key rewardKey FROM case_claims WHERE user_id=@u AND (reward_key LIKE 'AccountLevel:%' OR reward_key LIKE 'RECORD 0:%' OR reward_key LIKE @d OR reward_key LIKE @w)",ct,("u",user),("d",periods.Daily+":%"),("w",periods.Weekly+":%"));
        var choices=await CommunityEndpoints.Rows(c,tx,"SELECT mode,period_id,slot_id,definition_id,baseline,rerolls FROM case_mission_choices WHERE user_id=@u AND period_id IN (@d,@w)",ct,("u",user),("d",periods.Daily),("w",periods.Weekly));
        var claimed=claims.Select(r=>(string)r["rewardKey"]!).ToHashSet();
        var missions=new List<object>();
        foreach(string mode in new[]{"Pvp","Bot"})foreach(var m in Missions)
        {
            var state=CaseMissionSlots.FromRows(mode,m,now,choices,progress,claimed);var selected=state.Definition;bool completed=state.Progress>=selected.Target;
            missions.Add(new{id=m.Id,definitionId=selected.Id,name=selected.Name,mode,weekly=m.Weekly,progress=Math.Min(state.Progress,selected.Target),target=selected.Target,reward=selected.Reward,claimed=state.Claimed,completed,period=state.Period,rerolls=state.Rerolls,rerollCost=state.Cost,canReroll=!state.Claimed&&!completed});
        }
        string? incident=config["CaseRecords:IncidentTimestamp"]; // A story timestamp, not the season start date.
        int level=Level(xp);long levelStartXp=(long)(level-1)*(level-1)*100,nextLevelXp=level>=100?xp:(long)level*level*100;
        return new{userId=user.ToString(),level,xp,levelStartXp,nextLevelXp,levelProgress=level>=100?1.0:Math.Clamp((xp-levelStartXp)/(double)(nextLevelXp-levelStartXp),0,1),credits=Convert.ToInt64(account["credits"]),currencyName="기록 포인트",missions,
            levelRewards=Enumerable.Range(1,20).Select(n=>n*5).Select(n=>new{level=n,credits=CaseLevelRewards.Credits(n),itemId=CaseLevelRewards.Item(n),claimed=claimed.Contains(CaseLevelRewards.Key(n)),available=level>=n&&!claimed.Contains(CaseLevelRewards.Key(n))}),
            season=new{id=Season,title="_OMINOUS ENTRY",incidentTimestamp=string.IsNullOrWhiteSpace(incident)?"202X-XX-XX 18:24:01":incident,availability="(~ 정식 출시 전까지)",free=true,investigation=await CaseInvestigation.SnapshotAsync(c,tx,user,config,ct)},
            catalogue=Catalogue,inventory=await CommunityEndpoints.Rows(c,tx,"SELECT item_id itemId FROM case_inventory WHERE user_id=@u ORDER BY item_id",ct,("u",user)),
            loadout=await CommunityEndpoints.Rows(c,tx,"SELECT slot,item_id itemId FROM case_loadout WHERE user_id=@u ORDER BY slot",ct,("u",user)),
            achievements=await CommunityEndpoints.Rows(c,tx,"SELECT code,source,earned_at_utc earnedAtUtc FROM case_achievements WHERE user_id=@u ORDER BY code",ct,("u",user)),partyState=await OpenParties.SnapshotAsync(c,tx,user,ct)};
    }
    internal sealed record ActionRequest(string? Action,string? Id=null,string? Mode=null,int? ExpectedRerolls=null,string? ExpectedPeriod=null);
    internal sealed record ImportRequest(string? SaveId,string[]? Codes,bool Confirmed=false);
    internal static void MapCaseRecords(this WebApplication app)
    {
        app.MapOpenParties();
        app.MapCaseInvestigation();
        app.MapGet("/api/case-records",async(HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            var snapshot=await Snapshot(c,tx,user.Id,app.Configuration,ct);await tx.CommitAsync(ct);return Results.Ok(snapshot);
        }).RequireRateLimiting("social");
        app.MapPost("/api/case-records/action",async(ActionRequest body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            if(body.Action is not ("Buy" or "Equip" or "ClaimMission" or "RerollMission" or "ClaimLevelReward" or "ClaimRecord" or "ClaimInvestigationStage")||body.Id is null||body.Id.Length>48)return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);var account=await LockAccountAsync(c,tx,user.Id,ct);
            if(body.Action is "Buy" or "Equip")
            {
                var item=Catalogue.FirstOrDefault(i=>i.Id==body.Id);
                if(item is null&&body.Action=="Equip"&&CaseInvestigation.IsRareItem(body.Id))item=new Item(body.Id,"희귀 조사 완성 테두리","Frame",0);
                if(item is null&&body.Action=="Equip"&&CaseLevelRewards.IsItem(body.Id))item=new Item(body.Id,"레벨 기념 테두리","Frame",0);
                if(item is null)return Results.BadRequest();
                bool owned=(await CommunityEndpoints.Rows(c,tx,"SELECT item_id FROM case_inventory WHERE user_id=@u AND item_id=@id",ct,("u",user.Id),("id",item.Id))).Count>0;
                if(body.Action=="Buy"&&!owned)
                {
                    if(Convert.ToInt64(account["credits"])<item.Price)return Results.Conflict(new{message="기록 포인트가 부족합니다."});
                    await CommunityEndpoints.Exec(c,tx,"UPDATE case_accounts SET credits=credits-@p WHERE user_id=@u;INSERT INTO case_inventory(user_id,item_id) VALUES(@u,@id)",ct,("p",item.Price),("u",user.Id),("id",item.Id));
                }
                if(body.Action=="Equip")
                {
                    if(!owned)return Results.Conflict(new{message="보유한 꾸미기만 사용할 수 있습니다."});
                    await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_loadout(user_id,slot,item_id) VALUES(@u,@s,@id) ON DUPLICATE KEY UPDATE item_id=@id",ct,("u",user.Id),("s",item.Slot),("id",item.Id));
                }
            }
            else if(body.Action=="ClaimLevelReward")
            {
                if(!await CaseLevelRewards.ClaimAsync(c,tx,user.Id,Level(Convert.ToInt64(account["xp"])),body.Id,ct))return Results.Conflict(new{message="해당 레벨에 도달한 뒤 보상을 받을 수 있습니다."});
            }
            else if(body.Action is "ClaimRecord" or "ClaimInvestigationStage")
            {
                if(!await CaseInvestigation.ClaimAsync(c,tx,user.Id,body.Action,body.Id,app.Configuration,ct))return Results.Conflict(new{message="현장 조사와 파츠 완성을 먼저 완료하세요."});
            }
            else
            {
                var mission=Missions.FirstOrDefault(m=>m.Id==body.Id);if(mission is null||body.Mode is not ("Bot" or "Pvp"))return Results.BadRequest();
                var times=await CommunityEndpoints.Rows(c,tx,"SELECT UTC_TIMESTAMP(6) utc",ct);
                var now=new DateTimeOffset(DateTime.SpecifyKind(Convert.ToDateTime(times[0]["utc"]),DateTimeKind.Utc));
                var periods=Periods(now);string currentPeriod=mission.Weekly?periods.Weekly:periods.Daily;
                if((body.Action=="RerollMission"||body.ExpectedPeriod is not null)&&body.ExpectedPeriod!=currentPeriod)
                    return Results.Conflict(new{message="미션이 초기화되었습니다. 새 목록을 확인하세요."});
                if(body.Action=="RerollMission")
                {
                    string? error=await CaseMissionSlots.ChangeAsync(c,tx,user.Id,body.Mode,mission,now,body.ExpectedRerolls,Convert.ToInt64(account["credits"]),ct);
                    if(error is not null)return Results.Conflict(new{message=error});
                }
                else
                {
                    var state=await CaseMissionSlots.LoadAsync(c,tx,user.Id,body.Mode,mission,now,ct);
                    if(state.Progress<state.Definition.Target)return Results.Conflict(new{message="미션 목표를 먼저 완료하세요."});
                    if(!state.Claimed)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_claims(user_id,reward_key) VALUES(@u,@k);UPDATE case_accounts SET credits=LEAST(1000000000,credits+@v) WHERE user_id=@u",ct,("u",user.Id),("k",state.Key),("v",state.Definition.Reward));
                }
            }
            var snapshot=await Snapshot(c,tx,user.Id,app.Configuration,ct);await tx.CommitAsync(ct);return Results.Ok(new{message="처리되었습니다.",snapshot});
        }).RequireRateLimiting("social");
        app.MapPost("/api/case-records/import-campaign",async(ImportRequest body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            if(!body.Confirmed||!Guid.TryParse(body.SaveId,out _)||body.Codes is null||body.Codes.Length>LocalAchievements.Count||body.Codes.Any(code=>!LocalAchievements.Contains(code)))return Results.BadRequest(new{message="로컬 캠페인 업적의 계정 귀속을 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await LockAccountAsync(c,tx,user.Id,ct);
            string save=Guid.Parse(body.SaveId).ToString();
            await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_campaign_bindings(save_id,user_id) VALUES(@s,@u) ON DUPLICATE KEY UPDATE save_id=VALUES(save_id)",ct,("s",save),("u",user.Id));
            var binding=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_campaign_bindings WHERE save_id=@s FOR UPDATE",ct,("s",save));
            if(Convert.ToUInt64(binding[0]["user_id"])!=user.Id)return Results.Conflict(new{message="이미 다른 계정에 귀속한 캠페인 업적입니다."});
            foreach(string code in body.Codes.Distinct())await Unlock(c,tx,user.Id,code,"LocalCampaign",ct);
            await tx.CommitAsync(ct);return Results.Ok(new{message="싱글 업적을 병합했습니다. 로컬 신고로 재화·경쟁 업적은 지급되지 않습니다."});
        }).RequireRateLimiting("social");
    }
}
