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
    internal sealed record ClientAction(string Action,string ExpectedStep,string? Role=null,int[]? Choices=null);
    internal sealed record FloorEvent(string MatchId,string UserId,string Map,int ServerPort,DateTimeOffset ObservedAtUtc);
    internal sealed record ProgressEvent(string MatchId,string UserId,string Kind,int Count,int ServerPort,DateTimeOffset ObservedAtUtc);
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
        """,ct);await MigrateTalkStepsAsync(c,ct);}
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
    internal static async Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var (_,plan)=await ReadPlan(c,tx,ct);await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO main_quest_state(user_id) VALUES(@u)",ct,("u",user));
        var state=(await CommunityEndpoints.Rows(c,tx,"SELECT stage,revision FROM main_quest_state WHERE user_id=@u",ct,("u",user)))[0];int current=Convert.ToInt32(state["stage"]);
        var goals=await CommunityEndpoints.Rows(c,tx,"SELECT stage,goal_id,progress FROM main_quest_goals WHERE user_id=@u",ct,("u",user));
        var talked=(await CommunityEndpoints.Rows(c,tx,"SELECT step_id,choices FROM main_quest_talks WHERE user_id=@u",ct,("u",user))).ToDictionary(r=>(string)r["step_id"]!,r=>(string)r["choices"]!);
        int[] Choices(string id)=>talked.TryGetValue(id,out var saved)?saved.Split(',',StringSplitOptions.RemoveEmptyEntries).Select(int.Parse).ToArray():[];
        return new{name=plan.Name,baseStory=plan.BaseStory,revision=Convert.ToInt64(state["revision"]),stage=current,completed=current>=plan.Steps.Length,
            steps=plan.Steps.Select((s,i)=>new{id=s.Id,unlocked=i<=current,title=i<=current?s.Title:"",active=i==current,completed=i<current,canSkip=i==current&&s.Id is "survivor_tutorial" or "mannequin_tutorial",story=i<current?s.Story:"",
                // An unlocked conversation is readable on the phone; "talked" and the saved choices let it be read back as it went.
                npc=i<=current?s.Talk?.Npc:null,talk=i<=current?s.Talk:null,talked=talked.ContainsKey(s.Id),choices=Choices(s.Id),
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
    internal static Task StartedAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,string role,CancellationToken ct)=>
        AdvanceAsync(c,tx,user,"Start:"+match,new Dictionary<string,int>{{"Match",1},{"SurvivorMatch",role=="Survivor"?1:0}},ct);
    internal static async Task<object?> StartUpdateAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,string role,CancellationToken ct)
    {
        if(role is not("Survivor" or "Mannequin")||await CustomRoomRules.IsCustomAsync(c,tx,match,ct))return null;
        await CaseRecords.LockAccountAsync(c,tx,user,ct);
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision FROM main_quest_state WHERE user_id=@u",ct,("u",user));long before=rows.Count==0?0:Convert.ToInt64(rows[0]["revision"]);
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
        app.MapPost("/api/case-records/main-quest",async(ClientAction body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            bool talk=body.Action=="TalkComplete";
            if(talk?Default.Steps.All(s=>s.Talk is null||s.Id!=body.ExpectedStep):body.Action is not ("SkipTutorial" or "TutorialComplete")||body.ExpectedStep is not ("survivor_tutorial" or "mannequin_tutorial"))return Results.BadRequest();
            string metric=talk?"Dialogue":body.ExpectedStep=="survivor_tutorial"?"SurvivorTutorial":"MannequinTutorial";
            if(body.Action=="TutorialComplete"&&body.Role!=(metric=="SurvivorTutorial"?"Survivor":"Mannequin"))return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            var previous=await CommunityEndpoints.Rows(c,tx,"SELECT stage,revision FROM main_quest_state WHERE user_id=@u",ct,("u",user.Id));
            long before=previous.Count==0?0:Convert.ToInt64(previous[0]["revision"]);
            if(talk)
            {
                // A conversation can be finished when it is the current step, or read later once unlocked; a locked one cannot.
                var (_,plan)=await ReadPlan(c,tx,ct);int index=Array.FindIndex(plan.Steps,s=>s.Id==body.ExpectedStep),stage=previous.Count==0?0:Convert.ToInt32(previous[0]["stage"]);
                if(index<0||index>stage)return Results.Conflict(new{message="아직 열리지 않은 대화입니다."});
                if(!ValidChoices(plan.Steps[index].Talk!,body.Choices))return Results.BadRequest(new{message="대화 선택을 확인하세요."});
                await CommunityEndpoints.Exec(c,tx,"INSERT INTO main_quest_talks(user_id,step_id,choices,talked_at) VALUES(@u,@s,@c,UTC_TIMESTAMP(6)) ON DUPLICATE KEY UPDATE choices=@c,talked_at=UTC_TIMESTAMP(6)",ct,("u",user.Id),("s",body.ExpectedStep),("c",string.Join(',',body.Choices!)));
            }
            await AdvanceAsync(c,tx,user.Id,(talk?"Talk:":"Tutorial:")+body.ExpectedStep,new Dictionary<string,int>{{metric,1}},ct,body.Action=="SkipTutorial",body.ExpectedStep);
            var questNotices=await CommunityEndpoints.Rows(c,tx,"SELECT revision,kind,quest_name questName,step_title stepTitle FROM main_quest_notices WHERE user_id=@u AND revision>@r ORDER BY revision",ct,("u",user.Id),("r",before));
            var snapshot=await CaseRecords.Snapshot(c,tx,user.Id,app.Configuration,ct);await tx.CommitAsync(ct);return Results.Ok(new{snapshot,questNotices});
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
        app.MapPost("/api/server/main-quest/event",async(ProgressEvent body,HttpRequest request,AuthDatabase db,ProjectSecurityOptions security,CancellationToken ct)=>
        {
            if(!security.IsAuthorizedGameServer(request.Headers["X-ProjectProject01-Server-Secret"].ToString()))return Results.Unauthorized();
            if(!Guid.TryParse(body.MatchId,out var match)||!ulong.TryParse(body.UserId,out var user)||body.ServerPort is <1 or >65535||body.Kind is not("Capture" or "Escape")||body.Count is <1 or >100||body.Kind=="Escape"&&body.Count!=1)return Results.BadRequest();
            var now=DateTimeOffset.UtcNow;if(body.ObservedAtUtc>now.AddMinutes(1)||body.ObservedAtUtc<now.AddHours(-48))return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            if(await CustomRoomRules.IsCustomAsync(c,tx,match.ToString(),ct))return Results.Conflict();await CaseInvestigation.CaptureServerAsync(c,tx,match.ToString(),ct);
            var role=body.Kind=="Capture"?"Mannequin":"Survivor";
            var admission=await CommunityEndpoints.Rows(c,tx,"SELECT p.user_id FROM authoritative_match_players p JOIN authoritative_matches a ON a.match_id=p.match_id JOIN case_match_servers s ON s.match_id=p.match_id WHERE p.match_id=@m AND p.user_id=@u AND p.role=@role AND s.port=@port AND a.created_at_utc<=@at AND (a.ended_at_utc IS NULL OR a.ended_at_utc>=@at)",ct,("m",match.ToString()),("u",user),("role",role),("port",body.ServerPort),("at",body.ObservedAtUtc.UtcDateTime));
            if(admission.Count!=1)return Results.StatusCode(403);
            if(body.Kind=="Capture")await CaptureAsync(c,tx,user,match.ToString(),body.Count,ct);else await EscapeAsync(c,tx,user,match.ToString(),ct);
            await tx.CommitAsync(ct);return Results.Ok(new{message="확인되었습니다."});
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
