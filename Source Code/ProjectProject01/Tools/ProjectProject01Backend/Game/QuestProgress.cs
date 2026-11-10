using System.Text.Json;
using MySqlConnector;
using static QuestGraph;

/// <summary>
/// Players' quest progress on the quest graph (Docs/QuestGraph.md 3-4): which quests are open, the step each one is on,
/// goal progress, conversations read (the picks made), story flags and quests handed out in a conversation. Game events
/// reach every open quest's current step once (event keys); finishing a step follows its routes by flags.
/// </summary>
internal static class QuestProgress
{
    internal sealed record SaveRequest(long Revision,Graph Graph,Move[]? Moves=null);
    // A step that players stand on is removed or renamed: they are moved to another step of the same quest.
    internal sealed record Move(string Quest,string From,string? To);

    internal static async Task EnsureAsync(MySqlConnection c,CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c,null,"""
            CREATE TABLE IF NOT EXISTS quest_graph_config(id TINYINT PRIMARY KEY,revision BIGINT NOT NULL DEFAULT 0,graph_json MEDIUMTEXT NULL);
            INSERT IGNORE INTO quest_graph_config(id) VALUES(1);
            CREATE TABLE IF NOT EXISTS quest_user(user_id BIGINT UNSIGNED PRIMARY KEY,revision BIGINT NOT NULL DEFAULT 0,FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS quest_state(user_id BIGINT UNSIGNED NOT NULL,quest_id VARCHAR(40) NOT NULL,step_id VARCHAR(40) NULL,status VARCHAR(8) NOT NULL,opened_at DATETIME(6) NOT NULL,
                PRIMARY KEY(user_id,quest_id),KEY ix_quest_step(quest_id,step_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS quest_step_log(user_id BIGINT UNSIGNED NOT NULL,quest_id VARCHAR(40) NOT NULL,seq INT NOT NULL,step_id VARCHAR(40) NOT NULL,
                PRIMARY KEY(user_id,quest_id,seq),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS quest_goal_progress(user_id BIGINT UNSIGNED NOT NULL,quest_id VARCHAR(40) NOT NULL,step_id VARCHAR(40) NOT NULL,goal_id VARCHAR(40) NOT NULL,progress INT NOT NULL DEFAULT 0,
                PRIMARY KEY(user_id,quest_id,step_id,goal_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS quest_talk_paths(user_id BIGINT UNSIGNED NOT NULL,talk_id VARCHAR(40) NOT NULL,picks VARCHAR(800) NOT NULL,talked_at DATETIME(6) NOT NULL,
                PRIMARY KEY(user_id,talk_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS quest_flags(user_id BIGINT UNSIGNED NOT NULL,flag VARCHAR(40) NOT NULL,PRIMARY KEY(user_id,flag),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS quest_offers(user_id BIGINT UNSIGNED NOT NULL,quest_id VARCHAR(40) NOT NULL,PRIMARY KEY(user_id,quest_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            """,ct);
        await MigrateMainQuestAsync(c,ct);
    }

    // ---- Moving the original 탐사 over, once (main_quest_migrations "quest_graph_v1"). ----
    // The graph starts as the saved 탐사 plan (its own titles, stories and talks). A player's step number becomes the
    // step id, finished steps go to the log in order, goal progress and saved conversation picks keep their meaning
    // (the 탐사 talks are one line, so the picks are the same list), and the notice counter carries on.
    // The old tables stay as they were, so the move can be checked or undone.
    static async Task MigrateMainQuestAsync(MySqlConnection c,CancellationToken ct)
    {
        await using var tx=await c.BeginTransactionAsync(ct);
        await CommunityEndpoints.Rows(c,tx,"SELECT id FROM main_quest_config WHERE id=1 FOR UPDATE",ct);
        if((await CommunityEndpoints.Rows(c,tx,"SELECT name FROM main_quest_migrations WHERE name='quest_graph_v1'",ct)).Count>0)return;
        var (_,plan)=await MainQuest.ReadPlan(c,tx,ct);
        var config=await CommunityEndpoints.Rows(c,tx,"SELECT graph_json FROM quest_graph_config WHERE id=1 FOR UPDATE",ct);
        if(config[0]["graph_json"] is not string)
            await CommunityEndpoints.Exec(c,tx,"UPDATE quest_graph_config SET graph_json=@j WHERE id=1",ct,("j",JsonSerializer.Serialize(FromPlan(plan),Json)));
        var ids=plan.Steps.Select(s=>s.Id).ToArray();
        string Q=MainLine;
        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO quest_user(user_id,revision) SELECT user_id,revision FROM main_quest_state",ct);
        string StepCase(string column)=>"CASE "+column+" "+string.Concat(ids.Select((id,i)=>$"WHEN {i} THEN '{id}' "))+"ELSE NULL END";
        await CommunityEndpoints.Exec(c,tx,$"INSERT IGNORE INTO quest_state(user_id,quest_id,step_id,status,opened_at) SELECT user_id,@q,{StepCase("stage")},IF(stage>={ids.Length},'Done','Active'),UTC_TIMESTAMP(6) FROM main_quest_state",ct,("q",Q));
        for(int i=0;i<ids.Length;i++)
            await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO quest_step_log(user_id,quest_id,seq,step_id) SELECT user_id,@q,@i,@s FROM main_quest_state WHERE stage>@i",ct,("q",Q),("i",i),("s",ids[i]));
        await CommunityEndpoints.Exec(c,tx,$"INSERT IGNORE INTO quest_goal_progress(user_id,quest_id,step_id,goal_id,progress) SELECT user_id,@q,{StepCase("stage")},goal_id,progress FROM main_quest_goals WHERE stage>=0 AND stage<{ids.Length}",ct,("q",Q));
        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO quest_talk_paths(user_id,talk_id,picks,talked_at) SELECT user_id,step_id,choices,talked_at FROM main_quest_talks",ct);
        await CommunityEndpoints.Exec(c,tx,"INSERT INTO main_quest_migrations(name,applied_at) VALUES('quest_graph_v1',UTC_TIMESTAMP(6))",ct);
        await tx.CommitAsync(ct);
    }

    internal static async Task<(long Revision,Graph Graph)> ReadAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision,graph_json FROM quest_graph_config WHERE id=1",ct);
        var graph=rows.Count>0&&rows[0]["graph_json"] is string j?JsonSerializer.Deserialize<Graph>(j,Json):null;
        return(rows.Count>0?Convert.ToInt64(rows[0]["revision"]):0,graph??Default);
    }

    sealed class Player
    {
        public required ulong User;public long Revision;
        public Dictionary<string,(string? Step,string Status)> States=new();public HashSet<string> Flags=new();public HashSet<string> Offered=new();
        public Dictionary<string,List<string>> Log=new();
    }
    static async Task<Player> LoadAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct,bool forUpdate)
    {
        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO quest_user(user_id) VALUES(@u)",ct,("u",user));
        var p=new Player{User=user};
        p.Revision=Convert.ToInt64((await CommunityEndpoints.Rows(c,tx,"SELECT revision FROM quest_user WHERE user_id=@u"+(forUpdate?" FOR UPDATE":""),ct,("u",user)))[0]["revision"]);
        foreach(var r in await CommunityEndpoints.Rows(c,tx,"SELECT quest_id,step_id,status FROM quest_state WHERE user_id=@u",ct,("u",user)))p.States[(string)r["quest_id"]!]=(r["step_id"] as string,(string)r["status"]!);
        foreach(var r in await CommunityEndpoints.Rows(c,tx,"SELECT flag FROM quest_flags WHERE user_id=@u",ct,("u",user)))p.Flags.Add((string)r["flag"]!);
        foreach(var r in await CommunityEndpoints.Rows(c,tx,"SELECT quest_id FROM quest_offers WHERE user_id=@u",ct,("u",user)))p.Offered.Add((string)r["quest_id"]!);
        foreach(var r in await CommunityEndpoints.Rows(c,tx,"SELECT quest_id,step_id FROM quest_step_log WHERE user_id=@u ORDER BY quest_id,seq",ct,("u",user)))
        {var q=(string)r["quest_id"]!;if(!p.Log.TryGetValue(q,out var list))p.Log[q]=list=new();list.Add((string)r["step_id"]!);}
        return p;
    }
    static async Task NoticeAsync(MySqlConnection c,MySqlTransaction tx,Player p,string kind,string quest,string title,CancellationToken ct)
    {
        ++p.Revision;
        await CommunityEndpoints.Exec(c,tx,"INSERT INTO main_quest_notices(user_id,revision,kind,quest_name,step_title) VALUES(@u,@r,@k,@n,@t)",ct,
            ("u",p.User),("r",p.Revision),("k",kind),("n",quest[..Math.Min(quest.Length,100)]),("t",title[..Math.Min(title.Length,180)]));
    }
    static async Task SaveRevisionAsync(MySqlConnection c,MySqlTransaction tx,Player p,CancellationToken ct)=>
        await CommunityEndpoints.Exec(c,tx,"UPDATE quest_user SET revision=@r WHERE user_id=@u",ct,("r",p.Revision),("u",p.User));

    /// <summary>Opens every quest whose conditions now hold (repeats, since a newly open quest can be another's condition).</summary>
    static async Task OpenQuestsAsync(MySqlConnection c,MySqlTransaction tx,Graph g,Player p,CancellationToken ct)
    {
        bool Done(string q)=>p.States.TryGetValue(q,out var s)&&s.Status=="Done";
        bool Reached(string q,string step)=>p.States.TryGetValue(q,out var s)&&(s.Step==step||p.Log.TryGetValue(q,out var log)&&log.Contains(step));
        for(int round=0;round<20;round++)
        {
            var open=g.Quests.Where(q=>!p.States.ContainsKey(q.Id)&&Opens(q,p.Flags,p.Offered,Done,Reached)).ToList();
            if(open.Count==0)return;
            foreach(var q in open)
            {
                p.States[q.Id]=(q.Start,"Active");
                await CommunityEndpoints.Exec(c,tx,"INSERT INTO quest_state(user_id,quest_id,step_id,status,opened_at) VALUES(@u,@q,@s,'Active',UTC_TIMESTAMP(6))",ct,("u",p.User),("q",q.Id),("s",q.Start));
                await CommunityEndpoints.Exec(c,tx,"DELETE FROM quest_goal_progress WHERE user_id=@u AND quest_id=@q",ct,("u",p.User),("q",q.Id));
                // The first line of a fresh account opens silently, as 탐사 always did.
                if(!(q.Id==MainLine&&p.States.Count==1))await NoticeAsync(c,tx,p,"Open",q.Name,q.Steps.First(s=>s.Id==q.Start).Title,ct);
            }
        }
    }

    /// <summary>
    /// Counts a game event (once per event key) on every open quest's current step, or only on the expected one.
    /// Finished steps follow their routes; finished quests can open others.
    /// </summary>
    internal static async Task AdvanceAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string eventKey,IReadOnlyDictionary<string,int> metrics,CancellationToken ct,
        bool skip=false,(string Quest,string Step)? expected=null)
    {
        await CaseRecords.LockAccountAsync(c,tx,user,ct);
        var (_,g)=await ReadAsync(c,tx,ct);var p=await LoadAsync(c,tx,user,ct,true);
        await OpenQuestsAsync(c,tx,g,p,ct);
        if(expected is {} e&&!(p.States.TryGetValue(e.Quest,out var at)&&at.Status=="Active"&&at.Step==e.Step)){await SaveRevisionAsync(c,tx,p,ct);return;}
        if(await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO main_quest_events(user_id,event_key) VALUES(@u,@key)",ct,("u",user),("key",eventKey))==0){await SaveRevisionAsync(c,tx,p,ct);return;}
        foreach(var quest in g.Quests)
        {
            if(!p.States.TryGetValue(quest.Id,out var state)||state.Status!="Active"||expected is {} x&&x.Quest!=quest.Id)continue;
            var step=quest.Steps.FirstOrDefault(s=>s.Id==state.Step);if(step is null)continue;
            bool changed=false,all=true;var completed=new List<string>();
            foreach(var goal in step.Goals)
            {
                var rows=await CommunityEndpoints.Rows(c,tx,"SELECT progress FROM quest_goal_progress WHERE user_id=@u AND quest_id=@q AND step_id=@s AND goal_id=@g",ct,("u",user),("q",quest.Id),("s",step.Id),("g",goal.Id));
                int previous=rows.Count==0?0:Convert.ToInt32(rows[0]["progress"]);
                int value=skip?goal.Target:Math.Min(goal.Target,previous+Math.Clamp(metrics.GetValueOrDefault(goal.Metric),0,100));all&=value>=goal.Target;
                if(value<=previous)continue;
                changed=true;if(previous<goal.Target&&value>=goal.Target)completed.Add(goal.Label);
                await CommunityEndpoints.Exec(c,tx,"INSERT INTO quest_goal_progress(user_id,quest_id,step_id,goal_id,progress) VALUES(@u,@q,@s,@g,@v) ON DUPLICATE KEY UPDATE progress=@v",ct,
                    ("u",user),("q",quest.Id),("s",step.Id),("g",goal.Id),("v",value));
            }
            if(!changed)continue;
            // A finishing condition is shown only by the step notice, never a partial notice at the same time.
            if(all)await FinishStepAsync(c,tx,quest,step,p,ct);
            else if(completed.Count>0)await NoticeAsync(c,tx,p,"Condition",quest.Name,string.Join(" · ",completed),ct);
        }
        await OpenQuestsAsync(c,tx,g,p,ct);
        await SaveRevisionAsync(c,tx,p,ct);
    }
    static async Task FinishStepAsync(MySqlConnection c,MySqlTransaction tx,Quest quest,Step step,Player p,CancellationToken ct)
    {
        if(!p.Log.TryGetValue(quest.Id,out var log))p.Log[quest.Id]=log=new();
        await CommunityEndpoints.Exec(c,tx,"INSERT INTO quest_step_log(user_id,quest_id,seq,step_id) VALUES(@u,@q,@n,@s)",ct,("u",p.User),("q",quest.Id),("n",log.Count),("s",step.Id));log.Add(step.Id);
        await NoticeAsync(c,tx,p,"Stage",quest.Name,step.Title,ct);
        var next=NextStep(step,p.Flags);
        p.States[quest.Id]=(next,next is null?"Done":"Active");
        await CommunityEndpoints.Exec(c,tx,"UPDATE quest_state SET step_id=@s,status=@st WHERE user_id=@u AND quest_id=@q",ct,("s",next),("st",next is null?"Done":"Active"),("u",p.User),("q",quest.Id));
        // A step can be visited again on a route that loops back: it starts from zero each time.
        if(next is not null)await CommunityEndpoints.Exec(c,tx,"DELETE FROM quest_goal_progress WHERE user_id=@u AND quest_id=@q AND step_id=@s",ct,("u",p.User),("q",quest.Id),("s",next));
        else await NoticeAsync(c,tx,p,"Complete",quest.Name,step.Title,ct);
    }

    /// <summary>
    /// A conversation read on the phone. Only a conversation of an open quest's current or finished step can be read;
    /// the first reading is kept (picks, flags and handed-out quests never change on a re-read). Reading the current
    /// step's conversation counts its Dialogue goal.
    /// </summary>
    internal static async Task<string?> TalkAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string quest,string stepId,IReadOnlyList<int>? picks,CancellationToken ct)
    {
        await CaseRecords.LockAccountAsync(c,tx,user,ct);
        var (_,g)=await ReadAsync(c,tx,ct);var p=await LoadAsync(c,tx,user,ct,true);await OpenQuestsAsync(c,tx,g,p,ct);
        var q=g.Quests.FirstOrDefault(x=>x.Id==quest);var step=q?.Steps.FirstOrDefault(s=>s.Id==stepId);
        var talk=step?.Talk is null?null:g.Talks.FirstOrDefault(t=>t.Id==step.Talk);
        bool unlocked=q is not null&&p.States.TryGetValue(q.Id,out var st)&&(st.Step==stepId||p.Log.TryGetValue(q.Id,out var log)&&log.Contains(stepId));
        if(talk is null){await SaveRevisionAsync(c,tx,p,ct);return "대화가 없는 단계입니다.";}
        if(!unlocked){await SaveRevisionAsync(c,tx,p,ct);return "아직 열리지 않은 대화입니다.";}
        var taken=Walk(talk,picks);if(taken is null){await SaveRevisionAsync(c,tx,p,ct);return "대화 선택을 확인하세요.";}
        if(await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO quest_talk_paths(user_id,talk_id,picks,talked_at) VALUES(@u,@t,@p,UTC_TIMESTAMP(6))",ct,
            ("u",user),("t",talk.Id),("p",string.Join(',',picks!)))>0)
        {
            foreach(var flag in taken.SelectMany(o=>o.Flags??[]).Distinct())
                if(p.Flags.Add(flag))await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO quest_flags(user_id,flag) VALUES(@u,@f)",ct,("u",user),("f",flag));
            foreach(var give in taken.Select(o=>o.Gives).OfType<string>().Distinct())
                if(p.Offered.Add(give))await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO quest_offers(user_id,quest_id) VALUES(@u,@q)",ct,("u",user),("q",give));
        }
        await OpenQuestsAsync(c,tx,g,p,ct);await SaveRevisionAsync(c,tx,p,ct);
        await AdvanceAsync(c,tx,user,$"Talk:{quest}:{stepId}",new Dictionary<string,int>{{"Dialogue",1}},ct,expected:(quest,stepId));
        return null;
    }

    /// <summary>A tutorial step of the given role: the active one (any quest), for the tutorial screens.</summary>
    internal static async Task<(string Quest,string Step)?> ActiveTutorialAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string metric,CancellationToken ct)
    {
        var (_,g)=await ReadAsync(c,tx,ct);var p=await LoadAsync(c,tx,user,ct,false);
        foreach(var q in g.Quests)if(p.States.TryGetValue(q.Id,out var s)&&s.Status=="Active"&&q.Steps.FirstOrDefault(x=>x.Id==s.Step) is {} step&&IsTutorialStep(step)&&step.Goals.Any(x=>x.Metric==metric))return(q.Id,step.Id);
        return null;
    }

    /// <summary>
    /// Everything the game shows: every open quest (main lines first) with its finished steps in the order played and
    /// the current one, and the conversations the player can read.
    /// </summary>
    internal static async Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var (_,g)=await ReadAsync(c,tx,ct);var p=await LoadAsync(c,tx,user,ct,false);
        var before=p.Revision;await OpenQuestsAsync(c,tx,g,p,ct);if(p.Revision!=before)await SaveRevisionAsync(c,tx,p,ct);
        var goals=await CommunityEndpoints.Rows(c,tx,"SELECT quest_id,step_id,goal_id,progress FROM quest_goal_progress WHERE user_id=@u",ct,("u",user));
        var talked=(await CommunityEndpoints.Rows(c,tx,"SELECT talk_id,picks FROM quest_talk_paths WHERE user_id=@u",ct,("u",user))).ToDictionary(r=>(string)r["talk_id"]!,r=>(string)r["picks"]!);
        int[] Picks(string? id)=>id is not null&&talked.TryGetValue(id,out var s)?s.Split(',',StringSplitOptions.RemoveEmptyEntries).Select(int.Parse).ToArray():[];
        int Progress(string q,string s,Goal goal)=>Math.Min(goal.Target,goals.Where(r=>(string)r["quest_id"]! ==q&&(string)r["step_id"]! ==s&&(string)r["goal_id"]! ==goal.Id).Select(r=>Convert.ToInt32(r["progress"])).FirstOrDefault());
        var npcs=g.Npcs.ToDictionary(n=>n.Id,n=>n.Name);
        object Step(Quest q,Step s,bool active,bool done)
        {
            var talk=s.Talk is null?null:g.Talks.FirstOrDefault(t=>t.Id==s.Talk);
            return new{id=s.Id,title=s.Title,active,completed=done,canSkip=active&&IsTutorialStep(s),story=done?s.Story:"",
                npc=talk is null?null:npcs.GetValueOrDefault(talk.Npc,talk.Npc),talkId=talk?.Id,talk=talk is null?null:new{npc=npcs.GetValueOrDefault(talk.Npc,talk.Npc),start=talk.Start,nodes=talk.Nodes},
                talked=talk is not null&&talked.ContainsKey(talk.Id),choices=Picks(talk?.Id),
                goals=s.Goals.Select(x=>new{id=x.Id,label=x.Label,target=x.Target,progress=done?x.Target:Progress(q.Id,s.Id,x)}).ToArray()};
        }
        var quests=g.Quests.Where(q=>p.States.ContainsKey(q.Id)).OrderBy(q=>q.Kind=="Main"?0:1).Select(q=>
        {
            var (current,status)=p.States[q.Id];var log=p.Log.GetValueOrDefault(q.Id)??new();
            var steps=log.Select(id=>q.Steps.FirstOrDefault(s=>s.Id==id)).OfType<Step>().Select(s=>Step(q,s,false,true)).ToList();
            if(status=="Active"&&q.Steps.FirstOrDefault(s=>s.Id==current) is {} now)steps.Add(Step(q,now,true,false));
            return new{id=q.Id,name=q.Name,kind=q.Kind,baseStory=q.BaseStory,completed=status=="Done",steps};
        }).ToArray();
        return new{revision=p.Revision,quests,notices=await CommunityEndpoints.Rows(c,tx,"SELECT revision,kind,quest_name questName,step_title stepTitle FROM main_quest_notices WHERE user_id=@u ORDER BY revision DESC LIMIT 24",ct,("u",user))};
    }
    internal static async Task<long> RevisionAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision FROM quest_user WHERE user_id=@u",ct,("u",user));return rows.Count==0?0:Convert.ToInt64(rows[0]["revision"]);
    }

    /// <summary>
    /// The original 탐사 snapshot shape (one quest, step list with indexes), still read by game builds before the quest
    /// graph screens. Built from the main line on the graph: its steps in route order from the start.
    /// </summary>
    internal static async Task<object> LegacySnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var (_,g)=await ReadAsync(c,tx,ct);var p=await LoadAsync(c,tx,user,ct,false);var q=g.Quests.FirstOrDefault(x=>x.Id==MainLine)??g.Quests[0];
        var order=new List<Step>();var seen=new HashSet<string>();
        for(var id=q.Start;id is not null&&seen.Add(id);){var s=q.Steps.FirstOrDefault(x=>x.Id==id);if(s is null)break;order.Add(s);id=s.Next is {Length:>0}?s.Next[^1].Step:null;}
        var (current,status)=p.States.GetValueOrDefault(q.Id,(q.Start,"Active"));
        int stage=status=="Done"?order.Count:Math.Max(0,order.FindIndex(s=>s.Id==current));
        var goals=await CommunityEndpoints.Rows(c,tx,"SELECT step_id,goal_id,progress FROM quest_goal_progress WHERE user_id=@u AND quest_id=@q",ct,("u",user),("q",q.Id));
        var talked=(await CommunityEndpoints.Rows(c,tx,"SELECT talk_id,picks FROM quest_talk_paths WHERE user_id=@u",ct,("u",user))).ToDictionary(r=>(string)r["talk_id"]!,r=>(string)r["picks"]!);
        var npcs=g.Npcs.ToDictionary(n=>n.Id,n=>n.Name);
        // Older builds play a conversation as a list of lines; only a conversation without branches can be shown that way.
        object? Lines(Talk? t)
        {
            if(t is null)return null;var nodes=t.Nodes.ToDictionary(n=>n.Id);var lines=new List<object>();var seenNodes=new HashSet<string>();
            for(var id=t.Start;id is not null;)
            {
                if(!seenNodes.Add(id)||!nodes.TryGetValue(id,out var n))return null;
                if(n.Options is null){lines.Add(new{text=n.Text,options=(object?)null});id=n.Next;continue;}
                var next=n.Options.Select(o=>o.Next).Distinct().ToArray();if(next.Length!=1)return null;
                lines.Add(new{text=(string?)null,options=n.Options.Select(o=>new{text=o.Text,replies=o.Replies}).ToArray()});id=next[0];
            }
            return new{npc=npcs.GetValueOrDefault(t.Npc,t.Npc),lines};
        }
        return new{name=q.Name,baseStory=q.BaseStory,revision=p.Revision,stage,completed=status=="Done",
            steps=order.Select((s,i)=>
            {
                var t=s.Talk is null?null:g.Talks.FirstOrDefault(x=>x.Id==s.Talk);bool unlocked=i<=stage;
                return new{id=s.Id,unlocked,title=unlocked?s.Title:"",active=i==stage&&status!="Done",completed=i<stage,canSkip=i==stage&&IsTutorialStep(s),story=i<stage?s.Story:"",
                    npc=unlocked&&t is not null?npcs.GetValueOrDefault(t.Npc,t.Npc):null,talk=unlocked?Lines(t):null,talked=t is not null&&talked.ContainsKey(t.Id),
                    choices=t is not null&&talked.TryGetValue(t.Id,out var picks)?picks.Split(',',StringSplitOptions.RemoveEmptyEntries).Select(int.Parse).ToArray():[],
                    goals=s.Goals.Where(_=>unlocked).Select(x=>new{id=x.Id,label=x.Label,target=x.Target,
                        progress=i<stage?x.Target:Math.Min(x.Target,goals.Where(r=>(string)r["step_id"]! ==s.Id&&(string)r["goal_id"]! ==x.Id).Select(r=>Convert.ToInt32(r["progress"])).FirstOrDefault())}).ToArray()};
            }).ToArray(),
            notices=await CommunityEndpoints.Rows(c,tx,"SELECT revision,kind,quest_name questName,step_title stepTitle FROM main_quest_notices WHERE user_id=@u ORDER BY revision DESC LIMIT 24",ct,("u",user))};
    }

    // ---- Admin: read with how many players stand on each step, check, save with moves. ----
    internal static void Map(WebApplication app)
    {
        app.MapGet("/api/admin/quest-graph",async(HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);var (revision,graph)=await ReadAsync(c,null,ct);
            var players=(await CommunityEndpoints.Rows(c,null,"SELECT quest_id,step_id,COUNT(*) n FROM quest_state WHERE status='Active' GROUP BY quest_id,step_id",ct))
                .Select(r=>new{quest=(string)r["quest_id"]!,step=r["step_id"] as string,count=Convert.ToInt32(r["n"])}).ToArray();
            return Results.Ok(new{revision,graph,players,metrics=Metrics});
        }).RequireRateLimiting("admin");
        app.MapPost("/api/admin/quest-graph/check",(Graph graph,HttpRequest request,AdministratorOptions admin)=>
            !admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])?Results.Unauthorized():Results.Ok(new{errors=Validate(graph)})).RequireRateLimiting("admin");
        app.MapPut("/api/admin/quest-graph",async(SaveRequest body,HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            var errors=Validate(body.Graph);if(errors.Count>0)return Results.BadRequest(new{message="그래프를 확인하세요.",errors});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision FROM quest_graph_config WHERE id=1 FOR UPDATE",ct);
            if(Convert.ToInt64(rows[0]["revision"])!=body.Revision)return Results.Conflict(new{message="다른 곳에서 먼저 저장했습니다. 다시 불러오세요."});
            var result=await ApplyMovesAsync(c,tx,body.Graph,body.Moves??[],ct);if(result is not null)return Results.Conflict(new{message=result.Value.Message,stranded=result.Value.Stranded});
            await CommunityEndpoints.Exec(c,tx,"UPDATE quest_graph_config SET revision=revision+1,graph_json=@j WHERE id=1",ct,("j",JsonSerializer.Serialize(body.Graph,Json)));
            await tx.CommitAsync(ct);return Results.Ok(new{message="저장했습니다.",revision=body.Revision+1});
        }).RequireRateLimiting("admin");
    }
    /// <summary>
    /// Players stay where they are. A step someone stands on that the new graph no longer has needs a move to a step of
    /// the same quest; a removed quest someone is partway through needs an explicit drop (To empty), which ends that
    /// quest for them (its progress is removed, so it can open again if the quest comes back). Without these the save is
    /// refused and the stranded steps are listed with player counts.
    /// </summary>
    internal static async Task<(string Message,object[] Stranded)?> ApplyMovesAsync(MySqlConnection c,MySqlTransaction tx,Graph g,Move[] moves,CancellationToken ct)
    {
        var standing=await CommunityEndpoints.Rows(c,tx,"SELECT quest_id,step_id,COUNT(*) n FROM quest_state WHERE status='Active' GROUP BY quest_id,step_id FOR UPDATE",ct);
        var stranded=new List<object>();var apply=new List<Move>();
        foreach(var r in standing)
        {
            string q=(string)r["quest_id"]!;string? s=r["step_id"] as string;var quest=g.Quests.FirstOrDefault(x=>x.Id==q);
            if(quest is not null&&quest.Steps.Any(x=>x.Id==s))continue;
            var move=moves.FirstOrDefault(m=>m.Quest==q&&m.From==s);
            bool ok=move is not null&&(quest is null?string.IsNullOrEmpty(move.To):quest.Steps.Any(x=>x.Id==move.To));
            if(!ok){stranded.Add(new{quest=q,step=s,count=Convert.ToInt32(r["n"]),questRemoved=quest is null});continue;}
            apply.Add(move!);
        }
        if(stranded.Count>0)return("플레이어가 있는 단계나 퀘스트가 사라집니다. 옮길 단계를 정하거나, 지운 퀘스트는 진행 기록을 지울지 정하세요.",stranded.ToArray());
        foreach(var m in apply)
        {
            const string Moved="SELECT user_id FROM (SELECT user_id FROM quest_state WHERE quest_id=@q AND step_id=@from AND status='Active') moved";
            if(string.IsNullOrEmpty(m.To))
            {
                await CommunityEndpoints.Exec(c,tx,$"DELETE FROM quest_goal_progress WHERE quest_id=@q AND user_id IN ({Moved})",ct,("q",m.Quest),("from",m.From));
                await CommunityEndpoints.Exec(c,tx,$"DELETE FROM quest_step_log WHERE quest_id=@q AND user_id IN ({Moved})",ct,("q",m.Quest),("from",m.From));
                await CommunityEndpoints.Exec(c,tx,"DELETE FROM quest_state WHERE quest_id=@q AND step_id=@from AND status='Active'",ct,("q",m.Quest),("from",m.From));
                continue;
            }
            await CommunityEndpoints.Exec(c,tx,$"DELETE FROM quest_goal_progress WHERE quest_id=@q AND step_id IN (@from,@to) AND user_id IN ({Moved})",ct,("q",m.Quest),("from",m.From),("to",m.To));
            await CommunityEndpoints.Exec(c,tx,"UPDATE quest_state SET step_id=@to WHERE quest_id=@q AND step_id=@from AND status='Active'",ct,("q",m.Quest),("from",m.From),("to",m.To));
        }
        return null;
    }
}
