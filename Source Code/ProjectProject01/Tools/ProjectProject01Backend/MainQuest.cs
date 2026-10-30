using System.Text.Json;
using MySqlConnector;

internal static class MainQuest
{
    internal sealed record Goal(string Id,string Metric,string Label,int Target=1);
    internal sealed record Step(string Id,string Title,Goal[] Goals,string Story="");
    internal sealed record Plan(string Name,string BaseStory,Step[] Steps);
    internal sealed record Configure(long Revision,Plan Plan);
    internal sealed record ClientAction(string Action,string ExpectedStep,string? Role=null);
    internal sealed record FloorEvent(string MatchId,string UserId,string Map,int ServerPort,DateTimeOffset ObservedAtUtc);
    internal static readonly JsonSerializerOptions Json=new(JsonSerializerDefaults.Web);
    internal static readonly Plan Default=new("탐사","",[
        new("survivor_tutorial","생존자 튜토리얼 완료",[new("tutorial","SurvivorTutorial","생존자 튜토리얼 완료")]),
        new("first_match","어떤 매칭이든 게임 1번 플레이",[new("play","Match","경기 1회 플레이")]),
        new("third_floor","3층 맵 탈출하기",[new("escape","ThirdFloor","3층 탈출")]),
        new("second_floor","2층 맵 탈출하기",[new("escape","SecondFloor","2층 탈출")]),
        new("department_escape","백화점을 무사히 탈출하기",[new("escape","Escape","최종 탈출")]),
        new("mannequin_tutorial","마네킹 튜토리얼 완료",[new("tutorial","MannequinTutorial","마네킹 튜토리얼 완료")]),
        new("capture","마네킹으로 플레이어 1명 잡기",[new("capture","Capture","생존자 1명 붙잡기")]),
        new("mannequin_win","마네킹으로 승리하기",[new("win","MannequinWin","마네킹 승리")])]);
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS main_quest_config(id TINYINT PRIMARY KEY,revision BIGINT NOT NULL DEFAULT 0,plan_json MEDIUMTEXT NULL);
        INSERT IGNORE INTO main_quest_config(id) VALUES(1);
        CREATE TABLE IF NOT EXISTS main_quest_state(user_id BIGINT UNSIGNED PRIMARY KEY,stage INT NOT NULL DEFAULT 0,revision BIGINT NOT NULL DEFAULT 0,FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS main_quest_goals(user_id BIGINT UNSIGNED NOT NULL,stage INT NOT NULL,goal_id VARCHAR(40) NOT NULL,progress INT NOT NULL DEFAULT 0,PRIMARY KEY(user_id,stage,goal_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS main_quest_events(user_id BIGINT UNSIGNED NOT NULL,event_key VARCHAR(120) NOT NULL,PRIMARY KEY(user_id,event_key),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS main_quest_notices(user_id BIGINT UNSIGNED NOT NULL,revision BIGINT NOT NULL,kind VARCHAR(16) NOT NULL,quest_name VARCHAR(100) NOT NULL,step_title VARCHAR(180) NOT NULL,PRIMARY KEY(user_id,revision),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
    internal static async Task<(long Revision,Plan Plan)> ReadPlan(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision,plan_json FROM main_quest_config WHERE id=1",ct);
        var plan=rows[0]["plan_json"] is string encoded?JsonSerializer.Deserialize<Plan>(encoded,Json):null;
        return(Convert.ToInt64(rows[0]["revision"]),plan??Default);
    }
    internal static bool Valid(Plan? p)=>p is not null&&!string.IsNullOrWhiteSpace(p.Name)&&p.Name.Length<=100&&p.BaseStory is not null&&p.BaseStory.Length<=16000&&p.Steps is {Length:8}&&p.Steps.Select(s=>s?.Id).SequenceEqual(Default.Steps.Select(s=>s.Id))&&p.Steps.All(s=>s is not null&&!string.IsNullOrWhiteSpace(s.Title)&&s.Title.Length<=180&&s.Story is not null&&s.Story.Length<=16000&&s.Goals is {Length:>0 and <=8}&&s.Goals.All(g=>g is not null&&!string.IsNullOrWhiteSpace(g.Id)&&g.Id.Length<=40&&g.Id.All(ch=>char.IsAsciiLetterOrDigit(ch)||ch=='_')&&g.Metric is "SurvivorTutorial" or "MannequinTutorial" or "Match" or "ThirdFloor" or "SecondFloor" or "Escape" or "Capture" or "MannequinWin"&&g.Label is not null&&g.Label.Length<=180&&g.Target is >0 and <=100)&&s.Goals.Select(g=>g.Id).Distinct().Count()==s.Goals.Length);
    internal static async Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var (_,plan)=await ReadPlan(c,tx,ct);await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO main_quest_state(user_id) VALUES(@u)",ct,("u",user));
        var state=(await CommunityEndpoints.Rows(c,tx,"SELECT stage,revision FROM main_quest_state WHERE user_id=@u",ct,("u",user)))[0];int current=Convert.ToInt32(state["stage"]);
        var goals=await CommunityEndpoints.Rows(c,tx,"SELECT stage,goal_id,progress FROM main_quest_goals WHERE user_id=@u",ct,("u",user));
        return new{name=plan.Name,baseStory=plan.BaseStory,revision=Convert.ToInt64(state["revision"]),stage=current,completed=current>=plan.Steps.Length,
            steps=plan.Steps.Select((s,i)=>new{id=s.Id,unlocked=i<=current,title=i<=current?s.Title:"",active=i==current,completed=i<current,canSkip=i==current&&(i==0||i==5),story=i<current?s.Story:"",
                goals=s.Goals.Where(_=>i<=current).Select(g=>new{id=g.Id,label=g.Label,target=g.Target,progress=i<current?g.Target:Math.Min(g.Target,goals.Where(r=>Convert.ToInt32(r["stage"])==i&&(string)r["goal_id"]! ==g.Id).Select(r=>Convert.ToInt32(r["progress"])).FirstOrDefault())})}),
            notices=await CommunityEndpoints.Rows(c,tx,"SELECT revision,kind,quest_name questName,step_title stepTitle FROM main_quest_notices WHERE user_id=@u ORDER BY revision DESC LIMIT 24",ct,("u",user))};
    }
    internal static async Task AdvanceAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string eventKey,IReadOnlyDictionary<string,int> metrics,CancellationToken ct,bool skip=false,string? expectedStep=null)
    {
        await CaseRecords.LockAccountAsync(c,tx,user,ct);await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO main_quest_state(user_id) VALUES(@u)",ct,("u",user));
        var state=(await CommunityEndpoints.Rows(c,tx,"SELECT stage,revision FROM main_quest_state WHERE user_id=@u FOR UPDATE",ct,("u",user)))[0];int index=Convert.ToInt32(state["stage"]);long revision=Convert.ToInt64(state["revision"]);
        var (_,plan)=await ReadPlan(c,tx,ct);if(index>=plan.Steps.Length)return;var step=plan.Steps[index];if(expectedStep is not null&&step.Id!=expectedStep)return;
        if(await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO main_quest_events(user_id,event_key) VALUES(@u,@key)",ct,("u",user),("key",eventKey))==0)return;
        bool changed=false,all=true;var completedConditions=new List<string>();
        foreach(var goal in step.Goals)
        {
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT progress FROM main_quest_goals WHERE user_id=@u AND stage=@s AND goal_id=@g",ct,("u",user),("s",index),("g",goal.Id));int previous=rows.Count==0?0:Convert.ToInt32(rows[0]["progress"]);
            int value=skip?goal.Target:Math.Min(goal.Target,previous+Math.Clamp(metrics.GetValueOrDefault(goal.Metric),0,100));all&=value>=goal.Target;
            if(value>previous){changed=true;if(previous<goal.Target&&value>=goal.Target)completedConditions.Add(goal.Label);await CommunityEndpoints.Exec(c,tx,"INSERT INTO main_quest_goals(user_id,stage,goal_id,progress) VALUES(@u,@s,@g,@v) ON DUPLICATE KEY UPDATE progress=@v",ct,("u",user),("s",index),("g",goal.Id),("v",value));}
        }
        if(!changed)return;
        async Task Notice(string kind,string? detail=null){++revision;await CommunityEndpoints.Exec(c,tx,"INSERT INTO main_quest_notices(user_id,revision,kind,quest_name,step_title) VALUES(@u,@r,@k,@n,@t)",ct,("u",user),("r",revision),("k",kind),("n",plan.Name),("t",(detail??step.Title)[..Math.Min((detail??step.Title).Length,180)]));}
        // A completing condition is represented only by the stage notice, never a simultaneous partial notice.
        if(all){++index;await Notice("Stage");if(index==plan.Steps.Length)await Notice("Complete");}
        else if(completedConditions.Count>0)await Notice("Condition",string.Join(" · ",completedConditions));
        await CommunityEndpoints.Exec(c,tx,"UPDATE main_quest_state SET stage=@s,revision=@r WHERE user_id=@u",ct,("s",index),("r",revision),("u",user));
    }
    internal static Task VerifiedAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,string role,bool success,int captures,double escape,CancellationToken ct)=>
        AdvanceAsync(c,tx,user,"Match:"+match,new Dictionary<string,int>{{"Match",1},{"Escape",role=="Survivor"&&success&&escape>0?1:0},{"Capture",role=="Mannequin"?captures:0},{"MannequinWin",role=="Mannequin"&&success?1:0}},ct);
    internal static void Map(WebApplication app)
    {
        app.MapPost("/api/case-records/main-quest",async(ClientAction body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            if(body.Action is not ("SkipTutorial" or "TutorialComplete")||body.ExpectedStep is not ("survivor_tutorial" or "mannequin_tutorial"))return Results.BadRequest();
            string metric=body.ExpectedStep=="survivor_tutorial"?"SurvivorTutorial":"MannequinTutorial";
            if(body.Action=="TutorialComplete"&&body.Role!=(metric=="SurvivorTutorial"?"Survivor":"Mannequin"))return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            await AdvanceAsync(c,tx,user.Id,"Tutorial:"+body.ExpectedStep,new Dictionary<string,int>{{metric,1}},ct,body.Action=="SkipTutorial",body.ExpectedStep);
            var snapshot=await CaseRecords.Snapshot(c,tx,user.Id,app.Configuration,ct);await tx.CommitAsync(ct);return Results.Ok(new{snapshot});
        }).RequireRateLimiting("social");
        app.MapPost("/api/server/main-quest/floor",async(FloorEvent body,HttpRequest request,AuthDatabase db,ProjectSecurityOptions security,CancellationToken ct)=>
        {
            if(!security.IsAuthorizedGameServer(request.Headers["X-ProjectProject01-Server-Secret"].ToString()))return Results.Unauthorized();
            if(!Guid.TryParse(body.MatchId,out var match)||!ulong.TryParse(body.UserId,out var user)||body.ServerPort is <1 or >65535)return Results.BadRequest();
            string? metric=body.Map=="/Game/MyProject/Level/MultiplayTest"?"ThirdFloor":body.Map=="/Game/MyProject/Level/MultiplayTest1"?"SecondFloor":null;if(metric is null)return Results.BadRequest();
            var now=DateTimeOffset.UtcNow;if(body.ObservedAtUtc>now.AddMinutes(1)||body.ObservedAtUtc<now.AddHours(-48))return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            if(await CustomRoomRules.IsCustomAsync(c,tx,match.ToString(),ct))return Results.Conflict();
            await CaseInvestigation.CaptureServerAsync(c,tx,match.ToString(),ct);
            var admission=await CommunityEndpoints.Rows(c,tx,"SELECT p.user_id FROM authoritative_match_players p JOIN authoritative_matches a ON a.match_id=p.match_id JOIN case_match_servers s ON s.match_id=p.match_id WHERE p.match_id=@m AND p.user_id=@u AND p.role='Survivor' AND s.port=@port AND a.created_at_utc<=@at AND (a.ended_at_utc IS NULL OR a.ended_at_utc>=@at)",ct,("m",match.ToString()),("u",user),("port",body.ServerPort),("at",body.ObservedAtUtc.UtcDateTime));
            if(admission.Count!=1)return Results.StatusCode(403);
            await AdvanceAsync(c,tx,user,"Floor:"+match+":"+metric,new Dictionary<string,int>{{metric,1}},ct);await tx.CommitAsync(ct);return Results.Ok(new{message="확인되었습니다."});
        }).RequireRateLimiting("game-server");
        app.MapGet("/api/admin/main-quest",async(HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();await using var c=await db.OpenConnectionAsync(ct);var p=await ReadPlan(c,null,ct);return Results.Ok(new{revision=p.Revision,plan=p.Plan});}).RequireRateLimiting("admin");
        app.MapPut("/api/admin/main-quest",async(Configure body,HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();if(!Valid(body.Plan))return Results.BadRequest(new{message="퀘스트 제목·조건·스토리 구성을 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision FROM main_quest_config WHERE id=1 FOR UPDATE",ct);if(Convert.ToInt64(rows[0]["revision"])!=body.Revision)return Results.Conflict(new{message="구성이 변경되었습니다. 다시 불러오세요."});
            await CommunityEndpoints.Exec(c,tx,"UPDATE main_quest_config SET revision=revision+1,plan_json=@j WHERE id=1",ct,("j",JsonSerializer.Serialize(body.Plan,Json)));await tx.CommitAsync(ct);return Results.Ok(new{message="저장했습니다."});
        }).RequireRateLimiting("admin");
    }
}
