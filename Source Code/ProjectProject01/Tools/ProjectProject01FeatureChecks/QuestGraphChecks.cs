using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using static QuestGraph;

// Quest and conversation graph (Docs/QuestGraph.md): checks on the graph, branching by story flags, quests handed out
// in a conversation, parallel main lines, protection of players on removed steps or quests, and the 탐사 screen.
internal static class QuestGraphChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int count=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);++count;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"{r.RequestMessage?.RequestUri}: {(int)r.StatusCode} {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task<(HttpStatusCode Status,JsonElement Body)> Send(Task<HttpResponseMessage> pending){using var r=await pending;var text=await r.Content.ReadAsStringAsync();return(r.StatusCode,text.Length==0?default:JsonDocument.Parse(text).RootElement.Clone());}

        // ---- The default graph is 탐사, unchanged; and the checks catch the mistakes the editor must show. ----
        Check(Validate(Default).Count==0&&Default.Quests.Length==1&&Default.Quests[0].Steps.Length==MainQuest.Default.Steps.Length&&Default.Talks.Length==2,"기본 그래프는 탐사 10단계·대화 2개 그대로이고 검사 통과");
        Check(MainQuest.LegacyPlan(Default) is {} legacy&&JsonSerializer.Serialize(legacy,MainQuest.Json)==JsonSerializer.Serialize(MainQuest.Default,MainQuest.Json),"탐사 화면용 변환은 원래 구성과 같음");
        var kalin=Default.Talks.First(t=>t.Id=="kalin_talk");
        Check(Walk(kalin,[1,0,0])?.Count==3&&Walk(kalin,[1,0])is null&&Walk(kalin,[1,0,0,0])is null&&Walk(kalin,[0,5,0])is null,"대화 경로는 선택 수·번호가 정확할 때만 인정");

        var mira=new Talk("mira_talk","mira","n1",[
            new Node("n1","...거기 누구 있어요?","n2"),
            new Node("n2",Options:[new Option("도와줄게요.",["고마워요. 동생을 찾아 줘요."],["help_mira"],"side_lost","n3"),new Option("지금은 안 돼요.",["...알겠어요."])]),
            new Node("n3","2층 어딘가에 있을 거예요.")]);
        var story=new Quest("main_mira","미라의 부탁","Main",[],"meet",[
            new Step("meet","미라와 대화하기",[new Goal("talk","Dialogue","미라와 대화하기")],Talk:"mira_talk",Next:[new Route("help",Flag:"help_mira"),new Route("alone")]),
            new Step("help","미라를 도와 경기 1회",[new Goal("play","Match","경기 1회 플레이")]),
            new Step("alone","혼자 경기 1회",[new Goal("play","Match","경기 1회 플레이")])]);
        var side=new Quest("side_lost","잃어버린 동생","Side",[new Condition("Offered")],"look",[new Step("look","생존자로 경기 1회",[new Goal("play","SurvivorMatch","생존자로 경기 1회")])]);
        var graph=Default with{Npcs=[..Default.Npcs,new Npc("mira","미라")],Quests=[..Default.Quests,story,side],Talks=[..Default.Talks,mira]};
        Check(Validate(graph).Count==0,"메인 라인 2개·서브퀘스트·갈림길·대화로 받는 퀘스트를 가진 그래프는 검사 통과");
        bool Fails(Graph g,string contains)=>Validate(g).Any(e=>e.Contains(contains));
        Graph WithStory(Quest q)=>graph with{Quests=[..graph.Quests.Select(x=>x.Id==q.Id?q:x)]};
        Check(Fails(WithStory(story with{Steps=[..story.Steps,new Step("lost","갈 곳 없는 단계",[new Goal("play","Match","경기")])]}),"닿는 연결이 없습니다"),"시작에서 닿지 않는 단계는 거부");
        Check(Fails(WithStory(story with{Steps=[story.Steps[0],story.Steps[1] with{Next=[new Route("alone")]},story.Steps[2] with{Next=[new Route("help")]}]}),"끝날 수 없습니다"),"끝나지 않는 고리는 거부");
        Check(Fails(WithStory(story with{Steps=[story.Steps[0] with{Next=[new Route("help",Flag:"nobody_sets")]},story.Steps[1],story.Steps[2]]}),"남기는 선택지가 없습니다"),"아무도 남기지 않는 이야기 표시 조건은 거부");
        Check(Fails(WithStory(story with{Steps=[story.Steps[0] with{Next=[new Route("alone"),new Route("help",Flag:"help_mira")]},story.Steps[1],story.Steps[2]]}),"마지막 연결은 조건 없이"),"마지막 연결에 조건이 있으면 거부(표시 없는 플레이어가 막힘)");
        Check(Fails(graph with{Talks=[..graph.Talks.Where(t=>t.Id!="mira_talk"),mira with{Nodes=[mira.Nodes[0],mira.Nodes[1] with{Options=[mira.Nodes[1].Options![0] with{Gives=null},mira.Nodes[1].Options![1]]},mira.Nodes[2]]}]},"주는 선택지가 없습니다"),"'대화에서 받음' 퀘스트를 주는 선택지가 없으면 거부");
        Check(Fails(graph with{Talks=[..graph.Talks,mira with{Id="unused_talk"}]},"어느 단계에도 연결되지"),"단계에 연결되지 않은 대화는 거부");
        Check(Fails(graph with{Quests=[..graph.Quests,side with{Id="side_dup",Opens=[new Condition("QuestDone",Quest:"nope")]}]},"조건의 퀘스트"),"없는 퀘스트를 여는 조건은 거부");

        // ---- Save it, then two players take different routes. ----
        var original=await Read(admin.GetAsync("/api/admin/quest-graph"));long revision=original.GetProperty("revision").GetInt64();
        // The stored graph without this check's own entries (a run that stopped halfway can leave them behind).
        var stored=original.GetProperty("graph").Deserialize<Graph>(Json)!;
        var saved=stored with{Npcs=[..stored.Npcs.Where(n=>n.Id!="mira")],Quests=[..stored.Quests.Where(q=>q.Id is not("main_mira" or "side_lost"))],Talks=[..stored.Talks.Where(t=>t.Id!="mira_talk")]};
        Check(original.GetProperty("metrics").GetArrayLength()==Metrics.Length,"관리 화면은 목표 종류 목록을 함께 받음");
        var (status,body)=await Send(admin.PutAsJsonAsync("/api/admin/quest-graph",new QuestProgress.SaveRequest(revision,WithStory(story with{Start="none"}))));
        Check(status==HttpStatusCode.BadRequest&&body.GetProperty("errors").GetArrayLength()>0,"검사에 걸린 그래프는 저장 거부·이유 목록 반환");
        var checkedBody=await Read(admin.PostAsJsonAsync("/api/admin/quest-graph/check",graph));Check(checkedBody.GetProperty("errors").GetArrayLength()==0,"저장 전 검사 API");
        var merged=saved with{Npcs=[..saved.Npcs,new Npc("mira","미라")],Quests=[..saved.Quests,story,side],Talks=[..saved.Talks,mira]};
        await Read(admin.PutAsJsonAsync("/api/admin/quest-graph",new QuestProgress.SaveRequest(revision,merged)));
        (status,_)=await Send(admin.PutAsJsonAsync("/api/admin/quest-graph",new QuestProgress.SaveRequest(revision,merged)));Check(status==HttpStatusCode.Conflict,"같은 버전으로 두 번 저장하면 거부");
        revision++;
        Check((await Read(admin.GetAsync("/api/admin/main-quest"))).GetProperty("plan").GetProperty("steps").GetArrayLength()==MainQuest.Default.Steps.Length,"탐사가 한 줄이면 기존 탐사 화면도 계속 열림");

        var helper=await OperationsIntegration.Player(db);var loner=await OperationsIntegration.Player(db);
        HttpClient Client((ulong Id,string Account,string Token) p){var c=new HttpClient{BaseAddress=admin.BaseAddress};c.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);return c;}
        using var a=Client(helper);using var b=Client(loner);
        async Task<JsonElement> Quests(HttpClient c)=>(await Read(c.GetAsync("/api/case-records"))).GetProperty("questGraph");
        JsonElement? QuestOf(JsonElement g,string id)=>g.GetProperty("quests").EnumerateArray().Where(q=>q.GetProperty("id").GetString()==id).Cast<JsonElement?>().FirstOrDefault();
        string? Current(JsonElement g,string id)=>QuestOf(g,id) is {} q?q.GetProperty("steps").EnumerateArray().Where(s=>s.GetProperty("active").GetBoolean()).Select(s=>s.GetProperty("id").GetString()).FirstOrDefault():null;
        Task<HttpResponseMessage> Talk(HttpClient c,string quest,string step,params int[] choices)=>c.PostAsJsonAsync("/api/case-records/main-quest",new{action="TalkComplete",quest,expectedStep=step,choices});
        async Task Start(ulong user,string role){await using var c=await db.OpenConnectionAsync(default);await using var tx=await c.BeginTransactionAsync();await MainQuest.StartedAsync(c,tx,user,"qg-"+Guid.NewGuid(),role,default);await tx.CommitAsync();}

        var g=await Quests(a);
        Check(QuestOf(g,MainLine) is not null&&QuestOf(g,"main_mira") is not null&&QuestOf(g,"side_lost") is null,"조건 없는 메인 라인 두 개가 동시에 열리고, 대화로 받는 서브퀘스트는 닫혀 있음");
        Check(g.GetProperty("quests")[0].GetProperty("kind").GetString()=="Main"&&Current(g,"main_mira")=="meet"&&QuestOf(g,"main_mira")!.Value.GetProperty("steps")[0].GetProperty("talk").GetProperty("nodes").GetArrayLength()==3,"새 라인은 시작 단계와 대화 그래프(노드)를 보여 줌");
        (status,_)=await Send(Talk(a,"main_mira","meet",0,0));Check(status==HttpStatusCode.BadRequest,"대화 그래프와 맞지 않는 선택 경로는 거부");
        (status,_)=await Send(Talk(a,MainLine,"atea_talk",0,0,0));Check(status==HttpStatusCode.Conflict,"아직 닿지 않은 단계의 대화는 거부");
        var talked=await Read(Talk(a,"main_mira","meet",0));
        g=await Quests(a);
        Check(Current(g,"main_mira")=="help"&&QuestOf(g,"side_lost") is not null,"도와준다는 선택은 이야기 표시로 '돕기' 경로를 타고, 대화가 서브퀘스트를 줌");
        Check(talked.GetProperty("questNotices").EnumerateArray().Any(n=>n.GetProperty("kind").GetString()=="Open"&&n.GetProperty("questName").GetString()=="잃어버린 동생"),"받은 서브퀘스트는 열림 알림");
        await Read(Talk(b,"main_mira","meet",1));g=await Quests(b);
        Check(Current(g,"main_mira")=="alone"&&QuestOf(g,"side_lost") is null,"거절한 플레이어는 '혼자' 경로로 가고 서브퀘스트를 받지 않음");
        await Read(Talk(a,"main_mira","meet",1));g=await Quests(a);
        Check(QuestOf(g,"main_mira")!.Value.GetProperty("steps")[0].GetProperty("choices").EnumerateArray().Select(x=>x.GetInt32()).SequenceEqual([0])&&Current(g,"main_mira")=="help","다시 읽어도 처음 선택과 경로는 바뀌지 않음");
        await Start(helper.Id,"Survivor");g=await Quests(a);
        Check(QuestOf(g,"main_mira")!.Value.GetProperty("completed").GetBoolean()&&QuestOf(g,"side_lost")!.Value.GetProperty("completed").GetBoolean(),"경기 하나가 열려 있는 모든 퀘스트의 현재 단계에 반영");
        Check(QuestOf(g,"main_mira")!.Value.GetProperty("steps").EnumerateArray().Select(s=>s.GetProperty("id").GetString()).SequenceEqual(["meet","help"]),"끝난 퀘스트는 실제로 지나온 단계만 순서대로 보여 줌");

        // ---- Players on a removed step or quest. ----
        var noAlone=merged with{Quests=[..merged.Quests.Select(q=>q.Id!="main_mira"?q:q with{Steps=[q.Steps[0] with{Next=[new Route("help",Flag:"help_mira"),new Route("help")]},q.Steps[1]]})]};
        (status,body)=await Send(admin.PutAsJsonAsync("/api/admin/quest-graph",new QuestProgress.SaveRequest(revision,noAlone)));
        Check(status==HttpStatusCode.Conflict&&body.GetProperty("stranded")[0].GetProperty("step").GetString()=="alone"&&body.GetProperty("stranded")[0].GetProperty("count").GetInt32()==1,"플레이어가 있는 단계를 지우면 옮길 곳을 정할 때까지 저장 거부");
        await Read(admin.PutAsJsonAsync("/api/admin/quest-graph",new QuestProgress.SaveRequest(revision,noAlone,[new("main_mira","alone","help")])));revision++;
        Check(Current(await Quests(b),"main_mira")=="help","정한 단계로 옮겨지고 진행은 처음부터");
        (status,body)=await Send(admin.PutAsJsonAsync("/api/admin/quest-graph",new QuestProgress.SaveRequest(revision,saved)));
        Check(status==HttpStatusCode.Conflict&&body.GetProperty("stranded")[0].GetProperty("questRemoved").GetBoolean(),"진행 중인 플레이어가 있는 퀘스트를 지우면 기록 처리를 정할 때까지 거부");
        // Every position the server lists on the removed test quests (other accounts opened the parallel line too).
        var drops=body.GetProperty("stranded").EnumerateArray().Select(s=>new QuestProgress.Move(s.GetProperty("quest").GetString()!,s.GetProperty("step").GetString()!,"")).ToArray();
        await Read(admin.PutAsJsonAsync("/api/admin/quest-graph",new QuestProgress.SaveRequest(revision,saved,drops)));revision++;
        g=await Quests(b);Check(QuestOf(g,"main_mira") is null&&QuestOf(g,MainLine) is not null,"지운 퀘스트는 진행 중이던 기록을 지우고, 다른 퀘스트는 그대로");
        // ---- The one-time move of 탐사 progress saved by the step-number layout. ----
        {
            var mid=await OperationsIntegration.Player(db);var done=await OperationsIntegration.Player(db);
            int At(string id)=>Array.FindIndex(MainQuest.Default.Steps,s=>s.Id==id);
            await using var c=await db.OpenConnectionAsync(default);
            foreach(var p in new[]{mid,done})foreach(var t in new[]{"quest_state","quest_step_log","quest_goal_progress","quest_talk_paths","quest_user"})
                await CommunityEndpoints.Exec(c,null,$"DELETE FROM {t} WHERE user_id=@u",default,("u",p.Id));
            await CommunityEndpoints.Exec(c,null,"INSERT INTO main_quest_state(user_id,stage,revision) VALUES(@a,@s,7),(@b,@all,12) ON DUPLICATE KEY UPDATE stage=VALUES(stage),revision=VALUES(revision);"+
                "INSERT INTO main_quest_goals(user_id,stage,goal_id,progress) VALUES(@a,@s,'play',1) ON DUPLICATE KEY UPDATE progress=1;"+
                "INSERT INTO main_quest_talks(user_id,step_id,choices,talked_at) VALUES(@a,'kalin_talk','1,0,0',UTC_TIMESTAMP(6)) ON DUPLICATE KEY UPDATE choices='1,0,0';"+
                "DELETE FROM main_quest_migrations WHERE name='quest_graph_v1'",default,("a",mid.Id),("b",done.Id),("s",At("first_match")),("all",MainQuest.Default.Steps.Length));
            await MainQuest.EnsureAsync(c,default);await MainQuest.EnsureAsync(c,default);
            var state=await CommunityEndpoints.Rows(c,null,"SELECT user_id,step_id,status FROM quest_state WHERE quest_id=@q AND user_id IN (@a,@b) ORDER BY user_id=@b",default,("q",MainLine),("a",mid.Id),("b",done.Id));
            var log=(await CommunityEndpoints.Rows(c,null,"SELECT step_id FROM quest_step_log WHERE user_id=@a AND quest_id=@q ORDER BY seq",default,("a",mid.Id),("q",MainLine))).Select(r=>(string)r["step_id"]!).ToArray();
            var goal=await CommunityEndpoints.Rows(c,null,"SELECT progress FROM quest_goal_progress WHERE user_id=@a AND quest_id=@q AND step_id='first_match' AND goal_id='play'",default,("a",mid.Id),("q",MainLine));
            var path=await CommunityEndpoints.Rows(c,null,"SELECT picks FROM quest_talk_paths WHERE user_id=@a AND talk_id='kalin_talk'",default,("a",mid.Id));
            var rev=await CommunityEndpoints.Rows(c,null,"SELECT user_id,revision FROM quest_user WHERE user_id IN (@a,@b) ORDER BY user_id=@b",default,("a",mid.Id),("b",done.Id));
            Check(state.Count==2&&(string?)state[0]["step_id"]=="first_match"&&(string)state[0]["status"]! =="Active"&&state[1]["step_id"] is null&&(string)state[1]["status"]! =="Done","단계 번호는 단계 id로, 끝까지 간 계정은 완료로 옮김");
            Check(log.SequenceEqual(["survivor_tutorial","kalin_talk"])&&goal.Count==1&&Convert.ToInt32(goal[0]["progress"])==1,"지나온 단계 기록과 진행 중인 목표 수치를 그대로 옮김");
            Check(path.Count==1&&(string)path[0]["picks"]! =="1,0,0"&&Convert.ToInt64(rev[0]["revision"])==7&&Convert.ToInt64(rev[1]["revision"])==12,"고른 대화 선택과 알림 번호를 그대로 옮기고, 두 번 실행해도 한 번만 적용");
        }
        Console.WriteLine($"PASS: {count} quest graph checks");
    }
}
