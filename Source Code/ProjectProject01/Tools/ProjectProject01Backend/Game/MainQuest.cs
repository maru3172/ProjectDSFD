using System.Text.Json;
using MySqlConnector;

internal static class MainQuest
{
    internal sealed record Goal(string Id,string Metric,string Label,int Target=1);
    // A talk step is a phone conversation with one NPC: NPC messages (Text) and player choices (Options). Each option is
    // what the player sends, followed by the NPC's replies to it; the conversation then continues with the next line.
    internal sealed record TalkOption(string Text,string[] Replies);
    internal sealed record TalkLine(string? Text,TalkOption[]? Options);
    internal sealed record Talk(string Npc,TalkLine[] Lines);
    internal sealed record Step(string Id,string Title,Goal[] Goals,string Story="",Talk? Talk=null);
    internal sealed record Plan(string Name,string BaseStory,Step[] Steps);
    internal sealed record Configure(long Revision,Plan Plan);
    internal sealed record ClientAction(string Action,string ExpectedStep,string? Role=null,int[]? Choices=null,string? Quest=null);
    internal sealed record FloorEvent(string MatchId,string UserId,string Map,int ServerPort,DateTimeOffset ObservedAtUtc);
    internal sealed record ProgressEvent(string MatchId,string UserId,string Kind,int Count,int ServerPort,DateTimeOffset ObservedAtUtc,string? Map=null);
    internal static readonly JsonSerializerOptions Json=new(JsonSerializerDefaults.Web);
    static TalkLine Say(string text)=>new(text,null);
    static TalkLine Ask(params TalkOption[] options)=>new(null,options);
    static TalkOption Opt(string text,string[] replies)=>new(text,replies);
    internal static readonly Talk KalinTalk=new("캘린",[
        Say("...이 메시지 보여? 보이면 아무거나 대답해 줘."),
        Ask(Opt("누구세요?",["캘린. 너보다 조금 먼저 여기서 눈을 뜬 사람이야."]),Opt("여기가 어디예요?",["나도 정확히는 몰라. 문 닫은 백화점 같아. 우리 둘 다 3층에 있어."])),
        Say("방금 움직이는 거 봤어. 몸은 좀 익혔네."),
        Say("그럼 잘 들어. 여기엔 우리만 있는 게 아니야."),
        Say("마네킹. 진열대에 서 있는 그거. 그게 움직여. 그리고 사람을 사냥해."),
        Ask(Opt("농담이죠?",["나도 그랬으면 좋겠어. 아까 한 명이 끌려가는 걸 봤어."]),Opt("어떻게 피해요?",["눈을 떼지 마. 보고 있는 동안에는 움직이지 못하는 것 같아."])),
        Say("혼자서는 오래 못 버텨. 다른 사람들이랑 같이 움직여."),
        Say("한 번이라도 같이 돌아보면 감이 올 거야. 내가 여기서 지켜볼게."),
        Ask(Opt("알겠어요. 해 볼게요.",["살아서 1층에서 보자."]))]);
    internal static readonly Talk AteaTalk=new("아테아",[
        Say("또 처음으로 돌아왔지? 분명 문을 나섰는데, 다시 그 3층."),
        Ask(Opt("당신은 누구죠?",["아테아. 너처럼 몇 번이나 돌아온 사람."]),Opt("캘린은 어디 있어요?",["캘린 얘기는 나중에. 지금은 네가 먼저야."])),
        Say("여기서 오래 버틴 사람들은 다 알아. 도망치는 것만으로는 끝나지 않아."),
        Say("가끔은 우리가 마네킹이 돼."),
        Ask(Opt("그게 무슨 소리예요?",["말 그대로야. 마네킹을 움직이는 쪽에 서는 거지.","그쪽에 서 있는 동안에는 아무도 너를 사냥하지 못해."]),Opt("...사람을 사냥하라고요?",["서로가 서로를 잡아야 그 존재의 눈 밖에 날 수 있어.","나도 처음엔 거절했어. 그래서 몇 번을 더 끌려갔지."])),
        Say("마네킹을 조종하는 그 존재가 우리를 보고 있어. 살아남으려면 그쪽 방식에 익숙해져야 해."),
        Ask(Opt("...해 볼게요.",["처음은 연습이야. 몸이 기억하게 해."]),Opt("싫어요. 다른 방법을 찾을 거예요.",["찾으면 나한테도 알려 줘. 그 전까지는 살아 있어야 하잖아."])),
        Say("준비되면 마네킹 쪽 연습부터 해 봐.")]);
    // 캘린 talks to the player once the survivor tutorial is done; 아테아 right before the mannequin tutorial.
    internal static readonly Plan Default=new("탐사","",[
        new("survivor_tutorial","생존자 튜토리얼 완료",[new("tutorial","SurvivorTutorial","생존자 튜토리얼 완료")]),
        new("kalin_talk","캘린과 대화하기",[new("talk","Dialogue","캘린과 대화하기")],Talk:KalinTalk),
        new("first_match","어떤 매칭이든 생존자로 게임 1번 플레이",[new("play","SurvivorMatch","생존자로 경기 1회 플레이")]),
        new("third_floor","3층 맵 탈출하기",[new("escape","ThirdFloor","3층 탈출")]),
        new("second_floor","2층 맵 탈출하기",[new("escape","SecondFloor","2층 탈출")]),
        new("department_escape","백화점을 무사히 탈출하기",[new("escape","Escape","최종 탈출")]),
        new("atea_talk","아테아와 대화하기",[new("talk","Dialogue","아테아와 대화하기")],Talk:AteaTalk),
        new("mannequin_tutorial","마네킹 튜토리얼 완료",[new("tutorial","MannequinTutorial","마네킹 튜토리얼 완료")]),
        new("capture","마네킹으로 플레이어 1명 잡기",[new("capture","Capture","생존자 1명 붙잡기")]),
        new("mannequin_win","마네킹으로 승리하기",[new("win","MannequinWin","마네킹 승리")])]);
    internal static async Task EnsureAsync(MySqlConnection c,CancellationToken ct){await CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS main_quest_config(id TINYINT PRIMARY KEY,revision BIGINT NOT NULL DEFAULT 0,plan_json MEDIUMTEXT NULL);
        CREATE TABLE IF NOT EXISTS main_quest_migrations(name VARCHAR(40) PRIMARY KEY,applied_at DATETIME(6) NOT NULL);
        CREATE TABLE IF NOT EXISTS main_quest_talks(user_id BIGINT UNSIGNED NOT NULL,step_id VARCHAR(40) NOT NULL,choices VARCHAR(400) NOT NULL,talked_at DATETIME(6) NOT NULL,PRIMARY KEY(user_id,step_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        INSERT IGNORE INTO main_quest_config(id) VALUES(1);
        CREATE TABLE IF NOT EXISTS main_quest_state(user_id BIGINT UNSIGNED PRIMARY KEY,stage INT NOT NULL DEFAULT 0,revision BIGINT NOT NULL DEFAULT 0,FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS main_quest_goals(user_id BIGINT UNSIGNED NOT NULL,stage INT NOT NULL,goal_id VARCHAR(40) NOT NULL,progress INT NOT NULL DEFAULT 0,PRIMARY KEY(user_id,stage,goal_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS main_quest_events(user_id BIGINT UNSIGNED NOT NULL,event_key VARCHAR(120) NOT NULL,PRIMARY KEY(user_id,event_key),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS main_quest_notices(user_id BIGINT UNSIGNED NOT NULL,revision BIGINT NOT NULL,kind VARCHAR(16) NOT NULL,quest_name VARCHAR(100) NOT NULL,step_title VARCHAR(180) NOT NULL,PRIMARY KEY(user_id,revision),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);await MigrateTalkStepsAsync(c,ct);await QuestProgress.EnsureAsync(c,ct);}
    // Progress is stored as a step index, so a database saved in an earlier layout is moved to this one by step id, once
    // (main_quest_migrations "talk_steps_v2"). Two earlier layouts exist:
    //  - LegacyIds: the 8 steps before talk steps. A player whose current step now has a talk right before it starts at
    //    that talk, so nobody skips the conversation that introduces the next step.
    //  - FirstTalkIds ("talk_steps"): 캘린 before the survivor tutorial. Players who had not done the tutorial yet do it
    //    first and talk to 캘린 after it; their earlier 캘린 talk is cleared so the phone shows it as new again.
    internal static readonly string[] LegacyIds=["survivor_tutorial","first_match","third_floor","second_floor","department_escape","mannequin_tutorial","capture","mannequin_win"];
    internal static readonly string[] FirstTalkIds=["kalin_talk","survivor_tutorial","first_match","third_floor","second_floor","department_escape","atea_talk","mannequin_tutorial","capture","mannequin_win"];
    internal static int MigratedStage(string[] from,int stage,bool current)
    {
        if(stage>=from.Length)return Default.Steps.Length;
        string id=from[Math.Max(0,stage)];
        if(from==FirstTalkIds&&current&&id is "kalin_talk" or "survivor_tutorial")id="survivor_tutorial";
        int index=Array.FindIndex(Default.Steps,s=>s.Id==id);
        return from==LegacyIds&&current&&index>0&&Default.Steps[index-1].Talk is not null?index-1:index;
    }
    static async Task MigrateTalkStepsAsync(MySqlConnection c,CancellationToken ct)
    {
        await using var tx=await c.BeginTransactionAsync(ct);
        await CommunityEndpoints.Rows(c,tx,"SELECT id FROM main_quest_config WHERE id=1 FOR UPDATE",ct); // One server migrates; others wait and then see the marker.
        var applied=(await CommunityEndpoints.Rows(c,tx,"SELECT name FROM main_quest_migrations",ct)).Select(r=>(string)r["name"]!).ToHashSet();
        if(applied.Contains("talk_steps_v2"))return;
        var from=applied.Contains("talk_steps")?FirstTalkIds:LegacyIds;
        string Case(bool current)=>"CASE stage "+string.Concat(Enumerable.Range(0,from.Length+1).Select(i=>$"WHEN {i} THEN {MigratedStage(from,i,current)} "))+"ELSE stage END";
        // Rows are moved through negative numbers first, so a moved row never collides with one that has not moved yet.
        await CommunityEndpoints.Exec(c,tx,$"UPDATE main_quest_goals SET stage=-1-({Case(false)}) WHERE stage>=0",ct);
        await CommunityEndpoints.Exec(c,tx,"UPDATE main_quest_goals SET stage=-1-stage WHERE stage<0",ct);
        await CommunityEndpoints.Exec(c,tx,$"UPDATE main_quest_state SET stage={Case(true)}",ct);
        if(from==FirstTalkIds)
        {
            // The 캘린 talk now comes after the tutorial: anyone who has not passed it yet does it again from the start
            // (saved choices, its goal progress and its once-only event key are all cleared, or it could never complete).
            var kalin=("k",(object?)Array.FindIndex(Default.Steps,s=>s.Id=="kalin_talk"));
            await CommunityEndpoints.Exec(c,tx,"DELETE t FROM main_quest_talks t JOIN main_quest_state s ON s.user_id=t.user_id WHERE t.step_id='kalin_talk' AND s.stage<=@k",ct,kalin);
            await CommunityEndpoints.Exec(c,tx,"DELETE g FROM main_quest_goals g JOIN main_quest_state s ON s.user_id=g.user_id WHERE g.stage=@k AND s.stage<=@k",ct,kalin);
            await CommunityEndpoints.Exec(c,tx,"DELETE e FROM main_quest_events e JOIN main_quest_state s ON s.user_id=e.user_id WHERE e.event_key='Talk:kalin_talk' AND s.stage<=@k",ct,kalin);
        }
        await CommunityEndpoints.Exec(c,tx,"INSERT INTO main_quest_migrations(name,applied_at) VALUES('talk_steps_v2',UTC_TIMESTAMP(6))",ct);
        await tx.CommitAsync(ct);
    }
    internal static async Task<(long Revision,Plan Plan)> ReadPlan(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision,plan_json FROM main_quest_config WHERE id=1",ct);
        var plan=rows[0]["plan_json"] is string encoded?JsonSerializer.Deserialize<Plan>(encoded,Json):null;
        return(Convert.ToInt64(rows[0]["revision"]),UpgradePlan(plan??Default));
    }
    internal static Plan UpgradePlan(Plan plan)
    {
        var steps=plan.Steps.Select(s=>s.Id!="first_match"?s:s with{
            Title=s.Title=="어떤 매칭이든 게임 1번 플레이"?"어떤 매칭이든 생존자로 게임 1번 플레이":s.Title,
            Goals=s.Goals.Select(g=>g.Metric=="Match"?g with{Metric="SurvivorMatch",Label=g.Label=="경기 1회 플레이"?"생존자로 경기 1회 플레이":g.Label}:g).ToArray()}).ToList();
        // A plan saved before talk steps existed gets them (with the default conversation) in their places; written stories stay.
        foreach(var (talk,index) in Default.Steps.Select((s,i)=>(s,i)).Where(p=>p.s.Talk is not null))
        {
            int at=steps.FindIndex(s=>s.Id==talk.Id);
            if(at<0){int next=steps.FindIndex(s=>s.Id==Default.Steps[index+1].Id);steps.Insert(next<0?steps.Count:next,talk);}
            else if(steps[at].Talk is null)steps[at]=steps[at] with{Talk=talk.Talk};
        }
        // A plan saved when a talk step sat elsewhere is put in the current order (its own titles, stories and talks stay).
        if(steps.Count==Default.Steps.Length&&steps.Select(s=>s.Id).Order().SequenceEqual(Default.Steps.Select(s=>s.Id).Order()))
            steps=[..Default.Steps.Select(d=>steps.First(s=>s.Id==d.Id))];
        return plan with{Steps=[..steps]};
    }
    internal static bool ValidTalk(Talk? t)=>t is not null&&!string.IsNullOrWhiteSpace(t.Npc)&&t.Npc.Length<=20&&t.Lines is {Length:>0 and <=80}&&t.Lines.All(l=>l is not null&&
        (l.Options is null?!string.IsNullOrWhiteSpace(l.Text)&&l.Text.Length<=400:l.Text is null&&l.Options is {Length:>0 and <=4}&&l.Options.All(o=>o is not null&&!string.IsNullOrWhiteSpace(o.Text)&&o.Text.Length<=120&&o.Replies is {Length:<=6}&&o.Replies.All(r=>!string.IsNullOrWhiteSpace(r)&&r.Length<=400))));
    /// <summary>The choices a player made, one option index per choice line, are valid for this conversation.</summary>
    internal static bool ValidChoices(Talk talk,int[]? choices)
    {
        var asks=talk.Lines.Where(l=>l.Options is not null).ToArray();
        return choices is not null&&choices.Length==asks.Length&&choices.Select((c,i)=>c>=0&&c<asks[i].Options!.Length).All(ok=>ok);
    }
    internal static bool Valid(Plan? p)=>p is not null&&!string.IsNullOrWhiteSpace(p.Name)&&p.Name.Length<=100&&p.BaseStory is not null&&p.BaseStory.Length<=16000&&p.Steps is not null&&p.Steps.Length==Default.Steps.Length&&p.Steps.Select(s=>s?.Id).SequenceEqual(Default.Steps.Select(s=>s.Id))&&p.Steps.All(s=>s is not null&&(Default.Steps.First(d=>d.Id==s.Id).Talk is null?s.Talk is null:ValidTalk(s.Talk))&&!string.IsNullOrWhiteSpace(s.Title)&&s.Title.Length<=180&&s.Story is not null&&s.Story.Length<=16000&&s.Goals is {Length:>0 and <=8}&&s.Goals.All(g=>g is not null&&!string.IsNullOrWhiteSpace(g.Id)&&g.Id.Length<=40&&g.Id.All(ch=>char.IsAsciiLetterOrDigit(ch)||ch=='_')&&g.Metric is "SurvivorTutorial" or "MannequinTutorial" or "Match" or "MatchComplete" or "SurvivorMatch" or "ThirdFloor" or "SecondFloor" or "Escape" or "Capture" or "MannequinWin" or "Dialogue"&&g.Label is not null&&g.Label.Length<=180&&g.Target is >0 and <=100)&&s.Goals.Select(g=>g.Id).Distinct().Count()==s.Goals.Length);
    // Progress now lives on the quest graph (QuestProgress, Docs/QuestGraph.md); these keep the original entry points.
    /// <summary>The original 탐사 snapshot shape, for game builds before the quest graph screens.</summary>
    internal static Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)=>QuestProgress.LegacySnapshotAsync(c,tx,user,ct);
    /// <summary>A game event for every open quest's current step (once per event key).</summary>
    internal static Task AdvanceAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string eventKey,IReadOnlyDictionary<string,int> metrics,CancellationToken ct)=>
        QuestProgress.AdvanceAsync(c,tx,user,eventKey,metrics,ct);
    internal static Task StartedAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,string role,CancellationToken ct)=>
        AdvanceAsync(c,tx,user,"Start:"+match,new Dictionary<string,int>{{"Match",1},{"SurvivorMatch",role=="Survivor"?1:0}},ct);
    internal static async Task<object?> StartUpdateAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,string role,CancellationToken ct)
    {
        if(role is not("Survivor" or "Mannequin")||await CustomRoomRules.IsCustomAsync(c,tx,match,ct))return null;
        await CaseRecords.LockAccountAsync(c,tx,user,ct);
        long before=await QuestProgress.RevisionAsync(c,tx,user,ct);
        await StartedAsync(c,tx,user,match,role,ct);
        return new{userId=user.ToString(),mainQuest=await SnapshotAsync(c,tx,user,ct),questNotices=await CommunityEndpoints.Rows(c,tx,"SELECT revision,kind,quest_name questName,step_title stepTitle FROM main_quest_notices WHERE user_id=@u AND revision>@r ORDER BY revision",ct,("u",user),("r",before))};
    }
    internal static async Task CaptureAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,int captures,CancellationToken ct)
    {
        // Cumulative server counters tolerate retries/out-of-order delivery without counting a capture twice.
        for(int i=1;i<=Math.Clamp(captures,0,100);i++)await AdvanceAsync(c,tx,user,$"Capture:{match}:{i}",new Dictionary<string,int>{{"Capture",1}},ct);
    }
    internal static Task EscapeAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,CancellationToken ct)=>AdvanceAsync(c,tx,user,"Escape:"+match,new Dictionary<string,int>{{"Escape",1}},ct);
    internal static async Task VerifiedAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,string role,bool success,int captures,double escape,CancellationToken ct)
    {
        // Recovery for a result whose earlier real-time reports were delayed or sent by an older server.
        await StartedAsync(c,tx,user,match,role,ct);
        if(role=="Mannequin")await CaptureAsync(c,tx,user,match,captures,ct);
        if(role=="Survivor"&&success&&escape>0)await EscapeAsync(c,tx,user,match,ct);
        await AdvanceAsync(c,tx,user,"Result:"+match,new Dictionary<string,int>{{"MatchComplete",1},{"MannequinWin",role=="Mannequin"&&success?1:0}},ct);
    }
    internal static void Map(WebApplication app)
    {
        QuestProgress.Map(app);
        // The game's quest actions: a conversation read on the phone, or a tutorial finished or skipped. Quest names the
        // quest (older builds leave it out and mean the main line).
        app.MapPost("/api/case-records/main-quest",async(ClientAction body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            bool talk=body.Action=="TalkComplete";
            if(!talk&&body.Action is not("SkipTutorial" or "TutorialComplete")||string.IsNullOrEmpty(body.ExpectedStep)||body.ExpectedStep.Length>40||body.Quest is {Length:>40})return Results.BadRequest();
            string quest=string.IsNullOrEmpty(body.Quest)?QuestGraph.MainLine:body.Quest;
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            long before=await QuestProgress.RevisionAsync(c,tx,user.Id,ct);
            if(talk)
            {
                var error=await QuestProgress.TalkAsync(c,tx,user.Id,quest,body.ExpectedStep,body.Choices,ct);
                if(error is not null)return error.StartsWith("아직")?Results.Conflict(new{message=error}):Results.BadRequest(new{message=error});
            }
            else
            {
                // Only a tutorial step: its goals say which tutorial, and the reported role must match it.
                var (_,graph)=await QuestProgress.ReadAsync(c,tx,ct);
                var step=graph.Quests.FirstOrDefault(q=>q.Id==quest)?.Steps.FirstOrDefault(s=>s.Id==body.ExpectedStep);
                if(step is null||!QuestGraph.IsTutorialStep(step))return Results.BadRequest();
                string metric=step.Goals[0].Metric;
                if(body.Action=="TutorialComplete"&&body.Role!=(metric=="SurvivorTutorial"?"Survivor":"Mannequin"))return Results.BadRequest();
                await QuestProgress.AdvanceAsync(c,tx,user.Id,$"Tutorial:{quest}:{body.ExpectedStep}",new Dictionary<string,int>{{metric,1}},ct,body.Action=="SkipTutorial",(quest,body.ExpectedStep));
            }
            var questNotices=await CommunityEndpoints.Rows(c,tx,"SELECT revision,kind,quest_name questName,step_title stepTitle FROM main_quest_notices WHERE user_id=@u AND revision>@r ORDER BY revision",ct,("u",user.Id),("r",before));
            var snapshot=await CaseRecords.Snapshot(c,tx,user.Id,app.Configuration,ct);await tx.CommitAsync(ct);return Results.Ok(new{snapshot,questNotices});
        }).RequireRateLimiting("social");
        app.MapPost("/api/server/main-quest/floor",async(FloorEvent body,HttpRequest request,AuthDatabase db,ProjectSecurityOptions security,VersionCompatibilityOptions version,CancellationToken ct)=>
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
            await PlayAnalytics.RecordFloor(c,tx,match.ToString(),user,body.Map,version,ct);
            await AdvanceAsync(c,tx,user,"Floor:"+match+":"+metric,new Dictionary<string,int>{{metric,1}},ct);await tx.CommitAsync(ct);return Results.Ok(new{message="확인되었습니다."});
        }).RequireRateLimiting("game-server");
        app.MapPost("/api/server/main-quest/event",async(ProgressEvent body,HttpRequest request,AuthDatabase db,ProjectSecurityOptions security,VersionCompatibilityOptions version,CancellationToken ct)=>
        {
            if(!security.IsAuthorizedGameServer(request.Headers["X-ProjectProject01-Server-Secret"].ToString()))return Results.Unauthorized();
            if(!Guid.TryParse(body.MatchId,out var match)||!ulong.TryParse(body.UserId,out var user)||body.ServerPort is <1 or >65535||body.Kind is not("Capture" or "Escape")||body.Count is <1 or >100||body.Kind=="Escape"&&body.Count!=1)return Results.BadRequest();
            var now=DateTimeOffset.UtcNow;if(body.ObservedAtUtc>now.AddMinutes(1)||body.ObservedAtUtc<now.AddHours(-48))return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            if(await CustomRoomRules.IsCustomAsync(c,tx,match.ToString(),ct))return Results.Conflict();await CaseInvestigation.CaptureServerAsync(c,tx,match.ToString(),ct);
            var role=body.Kind=="Capture"?"Mannequin":"Survivor";
            var admission=await CommunityEndpoints.Rows(c,tx,"SELECT p.user_id FROM authoritative_match_players p JOIN authoritative_matches a ON a.match_id=p.match_id JOIN case_match_servers s ON s.match_id=p.match_id WHERE p.match_id=@m AND p.user_id=@u AND p.role=@role AND s.port=@port AND a.created_at_utc<=@at AND (a.ended_at_utc IS NULL OR a.ended_at_utc>=@at)",ct,("m",match.ToString()),("u",user),("role",role),("port",body.ServerPort),("at",body.ObservedAtUtc.UtcDateTime));
            if(admission.Count!=1)return Results.StatusCode(403);
            await PlayAnalytics.RecordOutcome(c,tx,match.ToString(),user,body.Kind,body.Count,body.Map,version,ct);
            if(body.Kind=="Capture")await CaptureAsync(c,tx,user,match.ToString(),body.Count,ct);else await EscapeAsync(c,tx,user,match.ToString(),ct);
            await tx.CommitAsync(ct);return Results.Ok(new{message="확인되었습니다."});
        }).RequireRateLimiting("game-server");
        // The original 탐사 screen (one line of steps) keeps working on the graph while the main line has no branches:
        // its titles, goals, stories and conversation lines are written back to the main line on the graph.
        app.MapGet("/api/admin/main-quest",async(HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);var (revision,graph)=await QuestProgress.ReadAsync(c,null,ct);
            var plan=LegacyPlan(graph);return plan is null?Results.Conflict(new{message="메인 라인에 갈림길이 있어 퀘스트·대화 그래프 편집기에서만 고칠 수 있습니다."}):Results.Ok(new{revision,plan});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/main-quest",async(Configure body,HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();if(!Valid(body.Plan))return Results.BadRequest(new{message="퀘스트 제목·조건·스토리 구성을 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision FROM quest_graph_config WHERE id=1 FOR UPDATE",ct);if(Convert.ToInt64(rows[0]["revision"])!=body.Revision)return Results.Conflict(new{message="구성이 변경되었습니다. 다시 불러오세요."});
            var (_,graph)=await QuestProgress.ReadAsync(c,tx,ct);if(LegacyPlan(graph) is null)return Results.Conflict(new{message="메인 라인에 갈림길이 있어 퀘스트·대화 그래프 편집기에서만 고칠 수 있습니다."});
            var updated=ApplyLegacyPlan(graph,body.Plan);var errors=QuestGraph.Validate(updated);if(errors.Count>0)return Results.BadRequest(new{message=errors[0]});
            await CommunityEndpoints.Exec(c,tx,"UPDATE quest_graph_config SET revision=revision+1,graph_json=@j WHERE id=1",ct,("j",JsonSerializer.Serialize(updated,QuestGraph.Json)));await tx.CommitAsync(ct);return Results.Ok(new{message="저장했습니다."});
        }).RequireRateLimiting("admin");
    }
    /// <summary>The main line as the original plan, when it is one line of steps in the 탐사 order with one-line talks.</summary>
    internal static Plan? LegacyPlan(QuestGraph.Graph g)
    {
        var q=g.Quests.FirstOrDefault(x=>x.Id==QuestGraph.MainLine);if(q is null)return null;
        var steps=new List<Step>();var seen=new HashSet<string>();var npcs=g.Npcs.ToDictionary(n=>n.Id,n=>n.Name);
        for(var id=q.Start;id is not null;)
        {
            var s=q.Steps.FirstOrDefault(x=>x.Id==id);if(s is null||!seen.Add(id)||(s.Next??[]).Length>1)return null;
            Talk? talk=null;
            if(s.Talk is not null)
            {
                var t=g.Talks.FirstOrDefault(x=>x.Id==s.Talk);if(t is null)return null;var nodes=t.Nodes.ToDictionary(n=>n.Id);var lines=new List<TalkLine>();var seenNodes=new HashSet<string>();
                for(var n=t.Start;n is not null;)
                {
                    if(!seenNodes.Add(n)||!nodes.TryGetValue(n,out var node))return null;
                    if(node.Options is null){lines.Add(Say(node.Text!));n=node.Next;continue;}
                    if(node.Options.Select(o=>o.Next).Distinct().Count()!=1||node.Options.Any(o=>o.Flags is {Length:>0}||o.Gives is not null))return null;
                    lines.Add(Ask([..node.Options.Select(o=>Opt(o.Text,o.Replies))]));n=node.Options[0].Next;
                }
                talk=new(npcs.GetValueOrDefault(t.Npc,t.Npc),[..lines]);
            }
            steps.Add(new(s.Id,s.Title,[..s.Goals.Select(x=>new Goal(x.Id,x.Metric,x.Label,x.Target))],s.Story,talk));id=s.Next is {Length:1}?s.Next[0].Step:null;
        }
        return steps.Count==q.Steps.Length?new(q.Name,q.BaseStory,[..steps]):null;
    }
    internal static QuestGraph.Graph ApplyLegacyPlan(QuestGraph.Graph g,Plan plan)
    {
        var npcs=g.Npcs.ToList();var talks=g.Talks.ToList();
        var quest=g.Quests.First(x=>x.Id==QuestGraph.MainLine);var steps=quest.Steps.ToList();
        foreach(var p in plan.Steps)
        {
            int i=steps.FindIndex(s=>s.Id==p.Id);if(i<0)continue;var s=steps[i];
            if(p.Talk is not null&&s.Talk is not null)
            {
                var npc=npcs.FirstOrDefault(n=>n.Name==p.Talk.Npc);if(npc is null){npc=new(QuestGraph.NpcId(p.Talk.Npc),p.Talk.Npc);npcs.Add(npc);}
                int t=talks.FindIndex(x=>x.Id==s.Talk);var rebuilt=QuestGraph.FromLegacy(s.Talk,p.Talk) with{Npc=npc.Id};
                if(t>=0)talks[t]=rebuilt with{Nodes=[..rebuilt.Nodes.Select(n=>talks[t].Nodes.FirstOrDefault(o=>o.Id==n.Id) is {} old?n with{X=old.X,Y=old.Y}:n)]};
            }
            steps[i]=s with{Title=p.Title,Story=p.Story,Goals=[..p.Goals.Select(x=>new QuestGraph.Goal(x.Id,x.Metric,x.Label,x.Target))]};
        }
        return g with{Npcs=[..npcs],Talks=[..talks],Quests=[..g.Quests.Select(q=>q.Id==quest.Id?q with{Name=plan.Name,BaseStory=plan.BaseStory,Steps=[..steps]}:q)]};
    }
}
