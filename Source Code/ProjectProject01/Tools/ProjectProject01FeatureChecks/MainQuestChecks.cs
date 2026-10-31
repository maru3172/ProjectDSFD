using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class MainQuestChecks
{
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
        var legacySteps=MainQuest.Default.Steps.ToArray();legacySteps[1]=legacySteps[1] with{Title="어떤 매칭이든 게임 1번 플레이",Goals=[new("play","Match","경기 1회 플레이")],Story="작성한 이야기는 유지"};
        var upgraded=MainQuest.UpgradePlan(MainQuest.Default with{Steps=legacySteps});
        Check(upgraded.Steps[1].Title=="어떤 매칭이든 생존자로 게임 1번 플레이"&&upgraded.Steps[1].Goals[0].Metric=="SurvivorMatch"&&upgraded.Steps[1].Goals[0].Label=="생존자로 경기 1회 플레이"&&upgraded.Steps[1].Story==legacySteps[1].Story,"기존 운영 설정도 생존자 플레이 조건으로 이행하고 작성 스토리는 보존");
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var response=await pending;string text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"{response.RequestMessage?.RequestUri}: {(int)response.StatusCode} {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode expected,string name){using var response=await pending;Check(response.StatusCode==expected,name);}
        var player=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        using var server=new HttpClient{BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        var original=await Read(admin.GetAsync("/api/admin/main-quest"));var previous=original.GetProperty("plan").Deserialize<MainQuest.Plan>(MainQuest.Json)!;long revision=original.GetProperty("revision").GetInt64();string match=Guid.NewGuid().ToString();
        async Task Sql(string sql,params(string,object?)[] args){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,args);}
        async Task<JsonElement> Quest()=>(await Read(client.GetAsync("/api/case-records"))).GetProperty("mainQuest");
        async Task Emit(string key,Dictionary<string,int> values){await using var c=await db.OpenConnectionAsync(default);await using var tx=await c.BeginTransactionAsync();await MainQuest.AdvanceAsync(c,tx,player.Id,key,values,default);await tx.CommitAsync();}
        try
        {
            var steps=MainQuest.Default.Steps.ToArray();steps[0]=steps[0] with{Story=decoded["survivor_tutorial"]};steps[1]=steps[1] with{Story="두 번째 이야기",Goals=[new("play","SurvivorMatch","생존자로 경기 1회 플레이"),new("finish","MatchComplete","경기 1회 완료"),new("escape","Escape","탈출 성공")]};steps[6]=steps[6] with{Goals=[new("capture","Capture","생존자 2명 붙잡기",2)]};
            var plan=MainQuest.Default with{BaseStory=decoded["base"],Steps=steps};await Read(admin.PutAsJsonAsync("/api/admin/main-quest",new MainQuest.Configure(revision++,plan)));
            var q=await Quest();Check(q.GetProperty("stage").GetInt32()==0&&q.GetProperty("steps").GetArrayLength()==8&&!q.GetProperty("completed").GetBoolean(),"탐사 8단계 새 계정 진행 상태");
            Check(q.GetProperty("steps")[0].GetProperty("unlocked").GetBoolean()&&q.GetProperty("steps").EnumerateArray().Skip(1).All(s=>!s.GetProperty("unlocked").GetBoolean()&&s.GetProperty("title").GetString()==""&&s.GetProperty("goals").GetArrayLength()==0),"처음에는 첫 퀘스트만 공개하고 이후 목표·문구는 잠금");
            await Expect(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="third_floor"}),HttpStatusCode.BadRequest,"일반 퀘스트 건너뛰기 차단");
            var completion=await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="TutorialComplete",expectedStep="survivor_tutorial",role="Survivor"}));
            Check(completion.GetProperty("questNotices").GetArrayLength()==1&&completion.GetProperty("questNotices")[0].GetProperty("kind").GetString()=="Stage","튜토리얼 완료 응답에 이번 완료 알림을 직접 포함");
            var repeated=await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="survivor_tutorial"}));
            Check(repeated.GetProperty("questNotices").GetArrayLength()==0,"완료 보고 재시도에는 과거 알림을 다시 보내지 않음");
            q=await Quest();Check(q.GetProperty("stage").GetInt32()==1&&q.GetProperty("steps")[0].GetProperty("story").GetString()==decoded["survivor_tutorial"]&&q.GetProperty("steps")[1].GetProperty("story").GetString()=="","CSV 이야기 저장·튜토리얼 스킵 멱등 처리·끝낸 이야기만 누적 공개");
            Check(q.GetProperty("steps")[1].GetProperty("unlocked").GetBoolean()&&!q.GetProperty("steps")[2].GetProperty("unlocked").GetBoolean()&&q.GetProperty("steps")[1].GetProperty("goals").GetArrayLength()==3,"앞 단계 완료 시 바로 다음 퀘스트 하나만 해금");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.StartedAsync(c,tx,player.Id,"mannequin-first-result","Mannequin",default);await tx.CommitAsync();}
            q=await Quest();Check(q.GetProperty("stage").GetInt32()==1&&q.GetProperty("steps")[1].GetProperty("goals")[0].GetProperty("progress").GetInt32()==0,"마네킹으로 완료한 경기는 생존자로 경기 플레이 조건에 집계하지 않음");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();var update=await MainQuest.StartUpdateAsync(c,tx,player.Id,"survivor-first-result","Survivor",default);await tx.CommitAsync();var payload=JsonSerializer.SerializeToElement(update,MainQuest.Json);Check(payload.GetProperty("questNotices").GetArrayLength()==1,"접속 허가 응답에 경기 시작 시점의 새 알림을 즉시 포함");}q=await Quest();
            Check(q.GetProperty("stage").GetInt32()==1&&q.GetProperty("notices")[0].GetProperty("kind").GetString()=="Condition"&&q.GetProperty("notices")[0].GetProperty("stepTitle").GetString()=="생존자로 경기 1회 플레이","생존자 경기 시작만으로 플레이 조건 달성·경기 종료를 기다리지 않음");
            Check(q.GetProperty("steps")[1].GetProperty("goals")[1].GetProperty("progress").GetInt32()==0,"경기 완료 조건은 시작만으로 달성되지 않음");
            long before=q.GetProperty("revision").GetInt64();await Emit("Start:survivor-first-result",new(){{"Escape",1}});Check((await Quest()).GetProperty("revision").GetInt64()==before,"같은 시작 사건 재전송으로 다른 목표 완료 불가");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.VerifiedAsync(c,tx,player.Id,"survivor-first-result","Survivor",false,0,0,default);await tx.CommitAsync();}q=await Quest();
            Check(q.GetProperty("steps")[1].GetProperty("goals")[0].GetProperty("progress").GetInt32()==1&&q.GetProperty("steps")[1].GetProperty("goals")[1].GetProperty("progress").GetInt32()==1,"결과 확정 시 완료 조건 집계·시작 조건은 중복 증가하지 않음");before=q.GetProperty("revision").GetInt64();
            await Emit("goal2",new(){{"Escape",1}});q=await Quest();Check(q.GetProperty("stage").GetInt32()==2&&q.GetProperty("revision").GetInt64()==before+1&&q.GetProperty("notices")[0].GetProperty("kind").GetString()=="Stage","마지막 조건은 조건 알림 생략·단계 완료 하나만 생성");
            await Sql("INSERT INTO authoritative_matches(match_id,room_id,room_name,created_at_utc) VALUES(@m,@r,'Quest fixture',UTC_TIMESTAMP(6));INSERT INTO authoritative_match_players(match_id,user_id,role) VALUES(@m,@u,'Survivor');INSERT INTO case_match_servers(match_id,port) VALUES(@m,24477)",("m",match),("r",Guid.NewGuid().ToString()),("u",player.Id));
            var floor=new MainQuest.FloorEvent(match,player.Id.ToString(),"/Game/MyProject/Level/MultiplayTest",24477,DateTimeOffset.UtcNow);
            await Expect(client.PostAsJsonAsync("/api/server/main-quest/floor",floor),HttpStatusCode.Unauthorized,"플레이어가 직접 층 탈출 보고 위조 불가");
            await Expect(server.PostAsJsonAsync("/api/server/main-quest/floor",floor with{ServerPort=24478}),HttpStatusCode.Forbidden,"배정되지 않은 서버 포트의 진행 보고 차단");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor));await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor));Check((await Quest()).GetProperty("stage").GetInt32()==3,"실제 입장 기록·서버 포트 검증 후 3층 탈출 한 번 반영");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor with{Map="/Game/MyProject/Level/MultiplayTest1",ObservedAtUtc=DateTimeOffset.UtcNow}));Check((await Quest()).GetProperty("stage").GetInt32()==4,"2층 탈출 다음 단계 연결");
            var escaped=new MainQuest.ProgressEvent(match,player.Id.ToString(),"Escape",1,24477,DateTimeOffset.UtcNow);
            await Expect(client.PostAsJsonAsync("/api/server/main-quest/event",escaped),HttpStatusCode.Unauthorized,"클라이언트의 직접 탈출 이벤트 위조 차단");
            await Expect(server.PostAsJsonAsync("/api/server/main-quest/event",escaped with{ServerPort=24478}),HttpStatusCode.Forbidden,"실시간 목표도 배정 서버 포트 검증");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/event",escaped));await Read(server.PostAsJsonAsync("/api/server/main-quest/event",escaped));
            Check((await Quest()).GetProperty("stage").GetInt32()==5,"생존자 탈출 즉시 완료·다른 참가자의 경기 종료를 기다리지 않음");
            await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="TutorialComplete",expectedStep="mannequin_tutorial",role="Mannequin"}));
            await Expect(server.PostAsJsonAsync("/api/server/main-quest/event",escaped with{Kind="Capture"}),HttpStatusCode.Forbidden,"생존자 역할로 마네킹 포획 조건을 보고할 수 없음");
            await Sql("UPDATE authoritative_match_players SET role='Mannequin' WHERE match_id=@m AND user_id=@u",("m",match),("u",player.Id));
            var captured=escaped with{Kind="Capture",ObservedAtUtc=DateTimeOffset.UtcNow};
            await Read(server.PostAsJsonAsync("/api/server/main-quest/event",captured));await Read(server.PostAsJsonAsync("/api/server/main-quest/event",captured));q=await Quest();
            Check(q.GetProperty("stage").GetInt32()==6&&q.GetProperty("steps")[6].GetProperty("goals")[0].GetProperty("progress").GetInt32()==1,"포획은 즉시 집계하고 같은 누적 횟수 재전송은 중복 계산하지 않음");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/event",captured with{Count=2}));await Read(server.PostAsJsonAsync("/api/server/main-quest/event",captured));
            Check((await Quest()).GetProperty("stage").GetInt32()==7,"목표 포획 수 도달 즉시 단계 완료·과거 누적 보고가 뒤늦게 와도 유지");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.VerifiedAsync(c,tx,player.Id,match,"Mannequin",true,2,0,default);await tx.CommitAsync();}
            q=await Quest();Check(q.GetProperty("completed").GetBoolean()&&q.GetProperty("stage").GetInt32()==8&&q.GetProperty("notices")[0].GetProperty("kind").GetString()=="Complete"&&q.GetProperty("notices")[1].GetProperty("kind").GetString()=="Stage","전체 완료는 마지막에만, 단계·퀘스트 완료는 별도 순서 알림");
            await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="survivor_tutorial"}));Check((await Quest()).GetProperty("revision").GetInt64()==q.GetProperty("revision").GetInt64(),"완료 후 재접속·스킵 재요청도 알림 중복 없음");
            await Expect(admin.PutAsJsonAsync("/api/admin/main-quest",new MainQuest.Configure(revision-1,plan)),HttpStatusCode.Conflict,"스토리 동시 편집 충돌 보호");
            Console.WriteLine($"PASS: {count} main quest checks (isolated API/MySQL)");
        }
        finally
        {
            await Read(admin.PutAsJsonAsync("/api/admin/main-quest",new MainQuest.Configure(revision,previous)));
            await Sql("DELETE FROM authoritative_matches WHERE match_id=@m;DELETE FROM case_match_servers WHERE match_id=@m;DELETE FROM users WHERE id=@u",("m",match),("u",player.Id));
        }
    }
}
