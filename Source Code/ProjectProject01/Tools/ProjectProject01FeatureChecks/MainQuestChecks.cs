using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class MainQuestChecks
{
    // Steps are addressed by id: talk steps sit between the original steps, so fixed numbers would drift.
    static int At(string id)=>Array.FindIndex(MainQuest.Default.Steps,s=>s.Id==id);
    static string? FindUp(string relative){foreach(var start in new[]{AppContext.BaseDirectory,Environment.CurrentDirectory})for(var d=new DirectoryInfo(start);d is not null;d=d.Parent){var p=Path.Combine(d.FullName,relative);if(File.Exists(p))return p;}return null;}

    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int count=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);++count;Console.WriteLine("PASS "+name);}
        var stories=ProjectProject01BuildServerHelper.MainQuestStoryCsv.Ids.ToDictionary(id=>id,_=>"");stories["base"]="기본 이야기";stories["survivor_tutorial"]="첫 이야기\r\n\"증거, 기록\"";
        var encoded=new StringWriter();ProjectProject01BuildServerHelper.MainQuestStoryCsv.Write(encoded,stories);
        var decoded=ProjectProject01BuildServerHelper.MainQuestStoryCsv.Read(new StringReader(encoded.ToString()));
        Check(stories.All(p=>decoded[p.Key]==p.Value),"스토리 CSV는 한글·쉼표·따옴표·여러 줄을 보존");
        bool Reject(string csv){try{ProjectProject01BuildServerHelper.MainQuestStoryCsv.Read(new StringReader(csv));return false;}catch(System.IO.InvalidDataException){return true;}}
        Check(Reject(encoded+"base,중복\r\n"),"CSV 중복 단계 거부로 기존 이야기 덮어쓰기 방지");
        Check(Reject(encoded.ToString().Replace("capture,\"\"", "unknown,\"\"")),"CSV에 알 수 없는 단계가 있으면 적용하지 않음");
        Check(Reject("단계ID,스토리\r\nbase,누락\r\n"),"CSV에 빠진 단계가 있으면 기존 스토리를 지우지 않음");

        // Phone conversations: the CSV round-trips, the checked-in source matches the server defaults, bad files are refused.
        string Json(object v)=>JsonSerializer.Serialize(v,MainQuest.Json);
        var talkCsv=FindUp(Path.Combine("Docs","QuestStories","MainQuestTalks.csv"));Check(talkCsv is not null,"대화 CSV 원본 위치 확인");
        var talks=ProjectProject01BuildServerHelper.MainQuestTalkCsv.ReadFile(talkCsv!);
        Check(MainQuest.Default.Steps.Where(s=>s.Talk is not null).All(s=>Json(talks[s.Id])==Json(s.Talk!)),"대화 CSV 원본과 서버 기본 대화가 같음 (NPC 말·선택지·답장)");
        var talkOut=new StringWriter();ProjectProject01BuildServerHelper.MainQuestTalkCsv.Write(talkOut,talks);
        var talkBack=ProjectProject01BuildServerHelper.MainQuestTalkCsv.Read(new StringReader(talkOut.ToString()));
        Check(talks.All(p=>Json(talkBack[p.Key])==Json(p.Value)),"대화 CSV 내보내기·불러오기는 여러 줄 답장과 선택지 묶음을 보존");
        bool RejectTalk(string csv){try{ProjectProject01BuildServerHelper.MainQuestTalkCsv.Read(new StringReader(csv));return false;}catch(System.IO.InvalidDataException){return true;}}
        Check(RejectTalk(talkOut.ToString().Replace("atea_talk,","unknown_talk,")),"대화 CSV에 알 수 없는 대화ID가 있으면 적용하지 않음");
        Check(RejectTalk("대화ID,상대,종류,내용,답장\r\nkalin_talk,캘린,NPC,안녕,\r\n"),"대화 CSV에 빠진 대화가 있으면 기존 대화를 지우지 않음");
        Check(RejectTalk(talkOut.ToString().Replace(",NPC,",",말,")),"대화 CSV 종류는 NPC·선택만 허용");

        // Plans and progress saved before talk steps existed.
        var legacySteps=MainQuest.Default.Steps.Where(s=>s.Talk is null).ToArray();int legacyMatch=Array.FindIndex(legacySteps,s=>s.Id=="first_match");
        legacySteps[legacyMatch]=legacySteps[legacyMatch] with{Title="어떤 매칭이든 게임 1번 플레이",Goals=[new("play","Match","경기 1회 플레이")],Story="작성한 이야기는 유지"};
        var upgraded=MainQuest.UpgradePlan(MainQuest.Default with{Steps=legacySteps});var upMatch=upgraded.Steps[At("first_match")];
        Check(upMatch.Title=="어떤 매칭이든 생존자로 게임 1번 플레이"&&upMatch.Goals[0].Metric=="SurvivorMatch"&&upMatch.Goals[0].Label=="생존자로 경기 1회 플레이"&&upMatch.Story=="작성한 이야기는 유지","기존 운영 설정도 생존자 플레이 조건으로 이행하고 작성 스토리는 보존");
        Check(upgraded.Steps.Select(s=>s.Id).SequenceEqual(MainQuest.Default.Steps.Select(s=>s.Id))&&MainQuest.Valid(upgraded),"대화 단계 이전에 저장한 8단계 구성은 대화 단계를 제자리에 넣어 그대로 쓸 수 있음");
        var L=MainQuest.LegacyIds;var F=MainQuest.FirstTalkIds;
        Check(MainQuest.MigratedStage(L,0,true)==At("survivor_tutorial")&&MainQuest.MigratedStage(L,1,true)==At("kalin_talk")&&MainQuest.MigratedStage(L,1,false)==At("first_match"),"대화 단계 전 계정: 튜토리얼 차례면 튜토리얼부터, 생존자 경기 차례면 캘린 대화부터, 목표 진행도는 같은 단계로");
        Check(MainQuest.MigratedStage(L,5,true)==At("atea_talk")&&MainQuest.MigratedStage(L,2,true)==At("third_floor")&&MainQuest.MigratedStage(L,8,true)==MainQuest.Default.Steps.Length,"마네킹 튜토리얼 차례면 아테아 대화부터, 나머지는 같은 단계·완료 계정은 완료 유지");
        Check(MainQuest.MigratedStage(F,0,true)==At("survivor_tutorial")&&MainQuest.MigratedStage(F,1,true)==At("survivor_tutorial")&&MainQuest.MigratedStage(F,2,true)==At("first_match")&&MainQuest.MigratedStage(F,0,false)==At("kalin_talk")&&MainQuest.MigratedStage(F,6,true)==At("atea_talk"),"캘린이 튜토리얼 앞이던 배치: 튜토리얼을 아직 안 한 계정은 튜토리얼 뒤에 캘린과 대화, 이미 지난 계정은 그대로");
        var firstTalkPlan=MainQuest.Default with{Steps=[..F.Select(id=>MainQuest.Default.Steps.First(s=>s.Id==id) with{Story=id=="kalin_talk"?"보존":""})]};
        var reordered=MainQuest.UpgradePlan(firstTalkPlan);
        Check(reordered.Steps.Select(s=>s.Id).SequenceEqual(MainQuest.Default.Steps.Select(s=>s.Id))&&reordered.Steps[At("kalin_talk")].Story=="보존"&&MainQuest.Valid(reordered),"캘린이 앞에 있던 저장 구성은 새 순서로 정렬하고 내용은 보존");
        Check(!MainQuest.Valid(MainQuest.Default with{Steps=MainQuest.Default.Steps.Select(s=>s with{Talk=null}).ToArray()}),"대화 내용이 빠진 구성은 저장 거부 (오래된 관리 도구로 대화가 지워지지 않음)");
        var noAnswer=MainQuest.Default.Steps.Select(s=>s.Talk is null?s:s with{Talk=s.Talk with{Lines=[new(null,[])]}}).ToArray();
        Check(!MainQuest.Valid(MainQuest.Default with{Steps=noAnswer}),"선택지 없는 선택 줄은 저장 거부");

        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var response=await pending;string text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"{response.RequestMessage?.RequestUri}: {(int)response.StatusCode} {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode expected,string name){using var response=await pending;Check(response.StatusCode==expected,name);}
        var player=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        using var server=new HttpClient{BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        var original=await Read(admin.GetAsync("/api/admin/main-quest"));var previous=original.GetProperty("plan").Deserialize<MainQuest.Plan>(MainQuest.Json)!;long revision=original.GetProperty("revision").GetInt64();string match=Guid.NewGuid().ToString();
        async Task Sql(string sql,params(string,object?)[] args){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,args);}
        async Task<JsonElement> Quest()=>(await Read(client.GetAsync("/api/case-records"))).GetProperty("mainQuest");
        async Task Emit(string key,Dictionary<string,int> values){await using var c=await db.OpenConnectionAsync(default);await using var tx=await c.BeginTransactionAsync();await MainQuest.AdvanceAsync(c,tx,player.Id,key,values,default);await tx.CommitAsync();}
        // The real migration on progress saved in the old 8-step layout; everyone else's rows are put back afterwards.
        {
            var a=await OperationsIntegration.Player(db);var b=await OperationsIntegration.Player(db);
            await using var c=await db.OpenConnectionAsync(default);
            var keepState=await CommunityEndpoints.Rows(c,null,"SELECT user_id,stage FROM main_quest_state",default);var keepGoals=await CommunityEndpoints.Rows(c,null,"SELECT user_id,stage,goal_id,progress FROM main_quest_goals",default);
            async Task<int> StageOf((ulong Id,string Account,string Token) p)=>Convert.ToInt32((await CommunityEndpoints.Rows(c,null,"SELECT stage FROM main_quest_state WHERE user_id=@u",default,("u",p.Id)))[0]["stage"]);
            async Task<string[]> GoalsOf((ulong Id,string Account,string Token) p)=>(await CommunityEndpoints.Rows(c,null,"SELECT stage,goal_id FROM main_quest_goals WHERE user_id=@u ORDER BY stage",default,("u",p.Id))).Select(r=>$"{r["stage"]}:{r["goal_id"]}").ToArray();
            // 8-step layout (no talk steps yet).
            await CommunityEndpoints.Exec(c,null,"DELETE FROM main_quest_migrations;INSERT INTO main_quest_state(user_id,stage) VALUES(@a,1),(@b,5);INSERT INTO main_quest_goals(user_id,stage,goal_id,progress) VALUES(@b,2,'escape',1),(@b,3,'escape',1),(@b,5,'tutorial',0)",default,("a",a.Id),("b",b.Id));
            await MainQuest.EnsureAsync(c,default);await MainQuest.EnsureAsync(c,default);
            Check(await StageOf(a)==At("kalin_talk")&&await StageOf(b)==At("atea_talk"),"기존 진행 이행: 각 계정은 다음 단계 앞의 대화부터 (두 번 실행해도 한 번만)");
            Check((await GoalsOf(b)).SequenceEqual([$"{At("third_floor")}:escape",$"{At("second_floor")}:escape",$"{At("mannequin_tutorial")}:tutorial"]),"기존 목표 진행도는 같은 단계로 이동");
            // Layout with 캘린 before the tutorial: a talked to 캘린 but has not done the tutorial; b is already past it.
            await CommunityEndpoints.Exec(c,null,"DELETE FROM main_quest_migrations;INSERT INTO main_quest_migrations(name,applied_at) VALUES('talk_steps',UTC_TIMESTAMP(6));DELETE FROM main_quest_goals WHERE user_id IN (@a,@b);UPDATE main_quest_state SET stage=1 WHERE user_id=@a;UPDATE main_quest_state SET stage=2 WHERE user_id=@b;"+
                "INSERT INTO main_quest_goals(user_id,stage,goal_id,progress) VALUES(@a,0,'talk',1),(@b,0,'talk',1),(@b,1,'tutorial',1);INSERT INTO main_quest_talks(user_id,step_id,choices,talked_at) VALUES(@a,'kalin_talk','0,0,0',UTC_TIMESTAMP(6)),(@b,'kalin_talk','1,1,0',UTC_TIMESTAMP(6));INSERT INTO main_quest_events(user_id,event_key) VALUES(@a,'Talk:kalin_talk'),(@b,'Talk:kalin_talk')",default,("a",a.Id),("b",b.Id));
            await MainQuest.EnsureAsync(c,default);await MainQuest.EnsureAsync(c,default);
            var talkRows=(await CommunityEndpoints.Rows(c,null,"SELECT user_id FROM main_quest_talks WHERE user_id IN (@a,@b)",default,("a",a.Id),("b",b.Id))).Select(r=>Convert.ToUInt64(r["user_id"])).ToArray();
            var eventRows=(await CommunityEndpoints.Rows(c,null,"SELECT user_id FROM main_quest_events WHERE user_id IN (@a,@b) AND event_key='Talk:kalin_talk'",default,("a",a.Id),("b",b.Id))).Select(r=>Convert.ToUInt64(r["user_id"])).ToArray();
            Check(await StageOf(a)==At("survivor_tutorial")&&(await GoalsOf(a)).Length==0&&!talkRows.Contains(a.Id)&&!eventRows.Contains(a.Id),"튜토리얼 전에 캘린과 대화했던 계정은 튜토리얼부터, 캘린 대화는 튜토리얼 뒤에 새로 (막히지 않게 기록 초기화)");
            Check(await StageOf(b)==At("first_match")&&(await GoalsOf(b)).SequenceEqual([$"{At("survivor_tutorial")}:tutorial",$"{At("kalin_talk")}:talk"])&&talkRows.Contains(b.Id),"이미 튜토리얼을 지난 계정은 진행·대화 기록 그대로");
            foreach(var r in keepState)await CommunityEndpoints.Exec(c,null,"UPDATE main_quest_state SET stage=@s WHERE user_id=@u",default,("s",r["stage"]),("u",r["user_id"]));
            await CommunityEndpoints.Exec(c,null,"DELETE FROM main_quest_goals WHERE user_id NOT IN (@a,@b)",default,("a",a.Id),("b",b.Id));
            foreach(var r in keepGoals)await CommunityEndpoints.Exec(c,null,"INSERT INTO main_quest_goals(user_id,stage,goal_id,progress) VALUES(@u,@s,@g,@p)",default,("u",r["user_id"]),("s",r["stage"]),("g",r["goal_id"]),("p",r["progress"]));
            await CommunityEndpoints.Exec(c,null,"DELETE FROM users WHERE id IN (@a,@b)",default,("a",a.Id),("b",b.Id));
        }
        int Stage(JsonElement q)=>q.GetProperty("stage").GetInt32();
        JsonElement StepOf(JsonElement q,string id)=>q.GetProperty("steps")[At(id)];
        Task<HttpResponseMessage> Talk(string step,params int[] choices)=>client.PostAsJsonAsync("/api/case-records/main-quest",new{action="TalkComplete",expectedStep=step,choices});
        try
        {
            var steps=MainQuest.Default.Steps.ToArray();
            steps[At("survivor_tutorial")]=steps[At("survivor_tutorial")] with{Story=decoded["survivor_tutorial"]};
            steps[At("first_match")]=steps[At("first_match")] with{Story="두 번째 이야기",Goals=[new("play","SurvivorMatch","생존자로 경기 1회 플레이"),new("finish","MatchComplete","경기 1회 완료"),new("escape","Escape","탈출 성공")]};
            steps[At("capture")]=steps[At("capture")] with{Goals=[new("capture","Capture","생존자 2명 붙잡기",2)]};
            var plan=MainQuest.Default with{BaseStory=decoded["base"],Steps=steps};await Read(admin.PutAsJsonAsync("/api/admin/main-quest",new MainQuest.Configure(revision++,plan)));
            var q=await Quest();Check(Stage(q)==At("survivor_tutorial")&&q.GetProperty("steps").GetArrayLength()==MainQuest.Default.Steps.Length&&!q.GetProperty("completed").GetBoolean(),"탐사 새 계정은 생존자 튜토리얼부터");
            Check(StepOf(q,"survivor_tutorial").GetProperty("unlocked").GetBoolean()&&q.GetProperty("steps").EnumerateArray().Skip(1).All(s=>!s.GetProperty("unlocked").GetBoolean()&&s.GetProperty("title").GetString()==""&&s.GetProperty("goals").GetArrayLength()==0),"처음에는 첫 퀘스트만 공개하고 이후 목표·문구는 잠금");
            Check(StepOf(q,"kalin_talk").GetProperty("talk").ValueKind==JsonValueKind.Null&&StepOf(q,"kalin_talk").GetProperty("npc").ValueKind==JsonValueKind.Null,"잠긴 대화는 내용·상대 이름도 숨김");
            await Expect(Talk("kalin_talk",1,0,0),HttpStatusCode.Conflict,"튜토리얼 전에는 캘린과 대화할 수 없음");
            await Expect(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="third_floor"}),HttpStatusCode.BadRequest,"일반 퀘스트 건너뛰기 차단");
            var completion=await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="TutorialComplete",expectedStep="survivor_tutorial",role="Survivor"}));
            Check(completion.GetProperty("questNotices").GetArrayLength()==1&&completion.GetProperty("questNotices")[0].GetProperty("kind").GetString()=="Stage","튜토리얼 완료 응답에 이번 완료 알림을 직접 포함");
            var repeated=await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="survivor_tutorial"}));
            Check(repeated.GetProperty("questNotices").GetArrayLength()==0,"완료 보고 재시도에는 과거 알림을 다시 보내지 않음");
            q=await Quest();Check(Stage(q)==At("kalin_talk")&&StepOf(q,"survivor_tutorial").GetProperty("story").GetString()==decoded["survivor_tutorial"]&&StepOf(q,"kalin_talk").GetProperty("talk").GetProperty("npc").GetString()=="캘린"&&StepOf(q,"atea_talk").GetProperty("talk").ValueKind==JsonValueKind.Null,"튜토리얼을 끝내면 캘린과 대화하기가 열리고, 끝낸 이야기만 공개·아테아는 잠금");
            await Expect(Talk("atea_talk",0,0,0),HttpStatusCode.Conflict,"아직 열리지 않은 대화는 완료할 수 없음");
            await Expect(Talk("kalin_talk",0,0),HttpStatusCode.BadRequest,"선택 횟수가 대화와 다르면 거부");
            await Expect(Talk("kalin_talk",0,5,0),HttpStatusCode.BadRequest,"없는 선택지 번호는 거부");
            await Expect(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="TalkComplete",expectedStep="third_floor",choices=Array.Empty<int>()}),HttpStatusCode.BadRequest,"대화가 아닌 단계는 대화로 완료할 수 없음");
            var talked=await Read(Talk("kalin_talk",1,0,0));q=await Quest();
            Check(talked.GetProperty("questNotices").GetArrayLength()==1&&Stage(q)==At("first_match")&&StepOf(q,"kalin_talk").GetProperty("talked").GetBoolean()&&StepOf(q,"kalin_talk").GetProperty("choices").EnumerateArray().Select(c=>c.GetInt32()).SequenceEqual([1,0,0]),"캘린과 대화를 끝내면 단계 완료·고른 선택을 저장하고 생존자 경기 단계로");
            long talkRevision=q.GetProperty("revision").GetInt64();await Read(Talk("kalin_talk",0,1,0));q=await Quest();
            Check(q.GetProperty("revision").GetInt64()==talkRevision&&StepOf(q,"kalin_talk").GetProperty("choices").EnumerateArray().Select(c=>c.GetInt32()).SequenceEqual([0,1,0]),"끝난 대화를 다시 보면 선택만 갱신·진행·알림은 그대로");
            Check(StepOf(q,"first_match").GetProperty("story").GetString()=="","아직 끝내지 않은 단계의 이야기는 공개하지 않음");
            Check(StepOf(q,"first_match").GetProperty("unlocked").GetBoolean()&&!StepOf(q,"third_floor").GetProperty("unlocked").GetBoolean()&&StepOf(q,"first_match").GetProperty("goals").GetArrayLength()==3,"앞 단계 완료 시 바로 다음 퀘스트 하나만 해금");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.StartedAsync(c,tx,player.Id,"mannequin-first-result","Mannequin",default);await tx.CommitAsync();}
            q=await Quest();Check(Stage(q)==At("first_match")&&StepOf(q,"first_match").GetProperty("goals")[0].GetProperty("progress").GetInt32()==0,"마네킹으로 완료한 경기는 생존자로 경기 플레이 조건에 집계하지 않음");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();var update=await MainQuest.StartUpdateAsync(c,tx,player.Id,"survivor-first-result","Survivor",default);await tx.CommitAsync();var payload=JsonSerializer.SerializeToElement(update,MainQuest.Json);Check(payload.GetProperty("questNotices").GetArrayLength()==1,"접속 허가 응답에 경기 시작 시점의 새 알림을 즉시 포함");}q=await Quest();
            Check(Stage(q)==At("first_match")&&q.GetProperty("notices")[0].GetProperty("kind").GetString()=="Condition"&&q.GetProperty("notices")[0].GetProperty("stepTitle").GetString()=="생존자로 경기 1회 플레이","생존자 경기 시작만으로 플레이 조건 달성·경기 종료를 기다리지 않음");
            Check(StepOf(q,"first_match").GetProperty("goals")[1].GetProperty("progress").GetInt32()==0,"경기 완료 조건은 시작만으로 달성되지 않음");
            long before=q.GetProperty("revision").GetInt64();await Emit("Start:survivor-first-result",new(){{"Escape",1}});Check((await Quest()).GetProperty("revision").GetInt64()==before,"같은 시작 사건 재전송으로 다른 목표 완료 불가");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.VerifiedAsync(c,tx,player.Id,"survivor-first-result","Survivor",false,0,0,default);await tx.CommitAsync();}q=await Quest();
            Check(StepOf(q,"first_match").GetProperty("goals")[0].GetProperty("progress").GetInt32()==1&&StepOf(q,"first_match").GetProperty("goals")[1].GetProperty("progress").GetInt32()==1,"결과 확정 시 완료 조건 집계·시작 조건은 중복 증가하지 않음");before=q.GetProperty("revision").GetInt64();
            await Emit("goal2",new(){{"Escape",1}});q=await Quest();Check(Stage(q)==At("third_floor")&&q.GetProperty("revision").GetInt64()==before+1&&q.GetProperty("notices")[0].GetProperty("kind").GetString()=="Stage","마지막 조건은 조건 알림 생략·단계 완료 하나만 생성");
            await Sql("INSERT INTO authoritative_matches(match_id,room_id,room_name,created_at_utc) VALUES(@m,@r,'Quest fixture',UTC_TIMESTAMP(6));INSERT INTO authoritative_match_players(match_id,user_id,role) VALUES(@m,@u,'Survivor');INSERT INTO case_match_servers(match_id,port) VALUES(@m,24477)",("m",match),("r",Guid.NewGuid().ToString()),("u",player.Id));
            var floor=new MainQuest.FloorEvent(match,player.Id.ToString(),"/Game/MyProject/Level/MultiplayTest",24477,DateTimeOffset.UtcNow);
            await Expect(client.PostAsJsonAsync("/api/server/main-quest/floor",floor),HttpStatusCode.Unauthorized,"플레이어가 직접 층 탈출 보고 위조 불가");
            await Expect(server.PostAsJsonAsync("/api/server/main-quest/floor",floor with{ServerPort=24478}),HttpStatusCode.Forbidden,"배정되지 않은 서버 포트의 진행 보고 차단");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor));await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor));Check(Stage(await Quest())==At("second_floor"),"실제 입장 기록·서버 포트 검증 후 3층 탈출 한 번 반영");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor with{Map="/Game/MyProject/Level/MultiplayTest1",ObservedAtUtc=DateTimeOffset.UtcNow}));Check(Stage(await Quest())==At("department_escape"),"2층 탈출 다음 단계 연결");
            var escaped=new MainQuest.ProgressEvent(match,player.Id.ToString(),"Escape",1,24477,DateTimeOffset.UtcNow);
            await Expect(client.PostAsJsonAsync("/api/server/main-quest/event",escaped),HttpStatusCode.Unauthorized,"클라이언트의 직접 탈출 이벤트 위조 차단");
            await Expect(server.PostAsJsonAsync("/api/server/main-quest/event",escaped with{ServerPort=24478}),HttpStatusCode.Forbidden,"실시간 목표도 배정 서버 포트 검증");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/event",escaped));await Read(server.PostAsJsonAsync("/api/server/main-quest/event",escaped));
            q=await Quest();Check(Stage(q)==At("atea_talk")&&StepOf(q,"atea_talk").GetProperty("talk").GetProperty("npc").GetString()=="아테아","생존자 탈출 즉시 완료하고 다음은 아테아와 대화하기");
            await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="TutorialComplete",expectedStep="mannequin_tutorial",role="Mannequin"}));Check(Stage(await Quest())==At("atea_talk"),"아테아와 대화하기 전에는 마네킹 튜토리얼을 완료할 수 없음");
            await Read(Talk("atea_talk",0,1,1));
            await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="TutorialComplete",expectedStep="mannequin_tutorial",role="Mannequin"}));Check(Stage(await Quest())==At("capture"),"대화 뒤 마네킹 튜토리얼 완료");
            await Expect(server.PostAsJsonAsync("/api/server/main-quest/event",escaped with{Kind="Capture"}),HttpStatusCode.Forbidden,"생존자 역할로 마네킹 포획 조건을 보고할 수 없음");
            await Sql("UPDATE authoritative_match_players SET role='Mannequin' WHERE match_id=@m AND user_id=@u",("m",match),("u",player.Id));
            var captured=escaped with{Kind="Capture",ObservedAtUtc=DateTimeOffset.UtcNow};
            await Read(server.PostAsJsonAsync("/api/server/main-quest/event",captured));await Read(server.PostAsJsonAsync("/api/server/main-quest/event",captured));q=await Quest();
            Check(Stage(q)==At("capture")&&StepOf(q,"capture").GetProperty("goals")[0].GetProperty("progress").GetInt32()==1,"포획은 즉시 집계하고 같은 누적 횟수 재전송은 중복 계산하지 않음");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/event",captured with{Count=2}));await Read(server.PostAsJsonAsync("/api/server/main-quest/event",captured));
            Check(Stage(await Quest())==At("mannequin_win"),"목표 포획 수 도달 즉시 단계 완료·과거 누적 보고가 뒤늦게 와도 유지");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.VerifiedAsync(c,tx,player.Id,match,"Mannequin",true,2,0,default);await tx.CommitAsync();}
            q=await Quest();Check(q.GetProperty("completed").GetBoolean()&&Stage(q)==MainQuest.Default.Steps.Length&&q.GetProperty("notices")[0].GetProperty("kind").GetString()=="Complete"&&q.GetProperty("notices")[1].GetProperty("kind").GetString()=="Stage","전체 완료는 마지막에만, 단계·퀘스트 완료는 별도 순서 알림");
            await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="survivor_tutorial"}));Check((await Quest()).GetProperty("revision").GetInt64()==q.GetProperty("revision").GetInt64(),"완료 후 재접속·스킵 재요청도 알림 중복 없음");
            await Expect(admin.PutAsJsonAsync("/api/admin/main-quest",new MainQuest.Configure(revision-1,plan)),HttpStatusCode.Conflict,"스토리 동시 편집 충돌 보호");
            await Expect(admin.PutAsJsonAsync("/api/admin/main-quest",new MainQuest.Configure(revision,plan with{Steps=plan.Steps.Where(s=>s.Talk is null).ToArray()})),HttpStatusCode.BadRequest,"대화 단계를 뺀 구성은 저장 거부");
            Console.WriteLine($"PASS: {count} main quest checks (isolated API/MySQL)");
        }
        finally
        {
            await Read(admin.PutAsJsonAsync("/api/admin/main-quest",new MainQuest.Configure(revision,MainQuest.UpgradePlan(previous))));
            await Sql("DELETE FROM authoritative_matches WHERE match_id=@m;DELETE FROM case_match_servers WHERE match_id=@m;DELETE FROM users WHERE id=@u",("m",match),("u",player.Id));
        }
    }
}
