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
            var steps=MainQuest.Default.Steps.ToArray();steps[0]=steps[0] with{Story=decoded["survivor_tutorial"]};steps[1]=steps[1] with{Story="두 번째 이야기",Goals=[new("play","Match","경기 1회 플레이"),new("escape","Escape","탈출 성공")]};
            var plan=MainQuest.Default with{BaseStory=decoded["base"],Steps=steps};await Read(admin.PutAsJsonAsync("/api/admin/main-quest",new MainQuest.Configure(revision++,plan)));
            var q=await Quest();Check(q.GetProperty("stage").GetInt32()==0&&q.GetProperty("steps").GetArrayLength()==8&&!q.GetProperty("completed").GetBoolean(),"탐사 8단계 새 계정 진행 상태");
            Check(q.GetProperty("steps")[0].GetProperty("unlocked").GetBoolean()&&q.GetProperty("steps").EnumerateArray().Skip(1).All(s=>!s.GetProperty("unlocked").GetBoolean()&&s.GetProperty("title").GetString()==""&&s.GetProperty("goals").GetArrayLength()==0),"처음에는 첫 퀘스트만 공개하고 이후 목표·문구는 잠금");
            await Expect(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="third_floor"}),HttpStatusCode.BadRequest,"일반 퀘스트 건너뛰기 차단");
            await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="survivor_tutorial"}));await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="SkipTutorial",expectedStep="survivor_tutorial"}));
            q=await Quest();Check(q.GetProperty("stage").GetInt32()==1&&q.GetProperty("steps")[0].GetProperty("story").GetString()==decoded["survivor_tutorial"]&&q.GetProperty("steps")[1].GetProperty("story").GetString()=="","CSV 이야기 저장·튜토리얼 스킵 멱등 처리·끝낸 이야기만 누적 공개");
            Check(q.GetProperty("steps")[1].GetProperty("unlocked").GetBoolean()&&!q.GetProperty("steps")[2].GetProperty("unlocked").GetBoolean()&&q.GetProperty("steps")[1].GetProperty("goals").GetArrayLength()==2,"앞 단계 완료 시 바로 다음 퀘스트 하나만 해금");
            await Emit("goal1",new(){{"Match",1}});q=await Quest();
            Check(q.GetProperty("stage").GetInt32()==1&&q.GetProperty("notices")[0].GetProperty("kind").GetString()=="Condition"&&q.GetProperty("notices")[0].GetProperty("stepTitle").GetString()=="경기 1회 플레이","조건만 완료하면 조건명 포함 조건 완료 알림");
            long before=q.GetProperty("revision").GetInt64();await Emit("goal1",new(){{"Escape",1}});Check((await Quest()).GetProperty("revision").GetInt64()==before,"같은 사건 재전송으로 다른 목표 완료 불가");
            await Emit("goal2",new(){{"Escape",1}});q=await Quest();Check(q.GetProperty("stage").GetInt32()==2&&q.GetProperty("revision").GetInt64()==before+1&&q.GetProperty("notices")[0].GetProperty("kind").GetString()=="Stage","마지막 조건은 조건 알림 생략·단계 완료 하나만 생성");
            await Sql("INSERT INTO authoritative_matches(match_id,room_id,room_name,created_at_utc) VALUES(@m,@r,'Quest fixture',UTC_TIMESTAMP(6));INSERT INTO authoritative_match_players(match_id,user_id,role) VALUES(@m,@u,'Survivor');INSERT INTO case_match_servers(match_id,port) VALUES(@m,24477)",("m",match),("r",Guid.NewGuid().ToString()),("u",player.Id));
            var floor=new MainQuest.FloorEvent(match,player.Id.ToString(),"/Game/MyProject/Level/MultiplayTest",24477,DateTimeOffset.UtcNow);
            await Expect(client.PostAsJsonAsync("/api/server/main-quest/floor",floor),HttpStatusCode.Unauthorized,"플레이어가 직접 층 탈출 보고 위조 불가");
            await Expect(server.PostAsJsonAsync("/api/server/main-quest/floor",floor with{ServerPort=24478}),HttpStatusCode.Forbidden,"배정되지 않은 서버 포트의 진행 보고 차단");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor));await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor));Check((await Quest()).GetProperty("stage").GetInt32()==3,"실제 입장 기록·서버 포트 검증 후 3층 탈출 한 번 반영");
            await Read(server.PostAsJsonAsync("/api/server/main-quest/floor",floor with{Map="/Game/MyProject/Level/MultiplayTest1",ObservedAtUtc=DateTimeOffset.UtcNow}));Check((await Quest()).GetProperty("stage").GetInt32()==4,"2층 탈출 다음 단계 연결");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.VerifiedAsync(c,tx,player.Id,"escape-result","Survivor",true,0,42,default);await tx.CommitAsync();}
            Check((await Quest()).GetProperty("stage").GetInt32()==5,"검증된 생존자 최종 탈출 반영");
            await Read(client.PostAsJsonAsync("/api/case-records/main-quest",new{action="TutorialComplete",expectedStep="mannequin_tutorial",role="Mannequin"}));
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.VerifiedAsync(c,tx,player.Id,"capture-result","Mannequin",true,1,0,default);await MainQuest.VerifiedAsync(c,tx,player.Id,"capture-result","Mannequin",true,1,0,default);await tx.CommitAsync();}
            Check((await Quest()).GetProperty("stage").GetInt32()==7,"마네킹 튜토리얼·포획 연결, 같은 경기 재보고로 승리 퀘스트 건너뛰지 않음");
            await using(var c=await db.OpenConnectionAsync(default)){await using var tx=await c.BeginTransactionAsync();await MainQuest.VerifiedAsync(c,tx,player.Id,"win-result","Mannequin",true,1,0,default);await tx.CommitAsync();}
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
