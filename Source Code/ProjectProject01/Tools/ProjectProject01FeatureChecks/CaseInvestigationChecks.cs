using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class CaseInvestigationChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int checks=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);checks++;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string body=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Investigation API {(int)r.StatusCode}: {body}");return JsonDocument.Parse(body).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode code,string name){using var r=await pending;Check(r.StatusCode==code,name+" "+r.StatusCode);}
        async Task Sql(string sql,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,p);}
        var player=await OperationsIntegration.Player(db);var other=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        using var otherClient=new HttpClient{BaseAddress=admin.BaseAddress};otherClient.DefaultRequestHeaders.Authorization=new("Bearer",other.Token);
        using var server=new HttpClient{BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        string match=Guid.NewGuid().ToString(),room=Guid.NewGuid().ToString(),map="/Game/MyProject/Level/MultiplayTest";
        var records=Enumerable.Range(0,117).Select(i=>new CaseInvestigation.Record($"p{i/9+1:D2}f{i%9+1:D2}",$"기록 {i+1}",$"clue-{i}",map,$"region{i}",i/9+1,i%9+1,30)).ToArray();
        var stories=Enumerable.Range(1,13).Select(i=>new CaseInvestigation.PartStory(i,$"파트 제목 {i}",$"full-story-part-{i}\r\n\r\n운영자가 작성한 전체 원문.  공백 유지.")).ToArray();
        List<Dictionary<string,object?>> oldStories;
        string oldJson;int oldLimit;DateTimeOffset? oldEnd;string? oldArchive;
        await using(var c=await db.OpenConnectionAsync(default)){oldJson=(string)(await CommunityEndpoints.Rows(c,null,"SELECT records_json FROM case_investigation_catalogue WHERE singleton_id=1",default))[0]["records_json"]!;oldLimit=await CaseInvestigation.WeeklyLimitAsync(c,null,default);oldEnd=await CaseInvestigation.SeasonEndAsync(c,null,default);oldArchive=(await CommunityEndpoints.Rows(c,null,"SELECT archive_json FROM case_investigation_seasons WHERE season=@s",default,("s",CaseRecords.Season)))[0]["archive_json"] as string;}
        await using(var c=await db.OpenConnectionAsync(default))oldStories=await CommunityEndpoints.Rows(c,null,"SELECT stage,title,story FROM case_investigation_stories WHERE season=@s",default,("s",CaseRecords.Season));
        object Visit(int index,int port=24477,string? user=null)=>new{matchId=match,userId=user??player.Id.ToString(),region="region"+index,map,serverPort=port,observedAtUtc=DateTimeOffset.UtcNow};
        Task<JsonElement> State()=>Read(client.GetAsync("/api/case-records"));
        try
        {
            await Sql("UPDATE case_investigation_seasons SET ends_at_utc=NULL,archive_json=NULL WHERE season=@s",("s",CaseRecords.Season));
            await Expect(client.PutAsJsonAsync("/api/admin/case-records/settings",new{weeklyLimit=117}),HttpStatusCode.Unauthorized,"일반 계정 주간 조사 수량 변경 차단");
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/settings",new{weeklyLimit=14}),HttpStatusCode.BadRequest,"주간 최소 15조각 수집 가능 수량 보장");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/settings",new{weeklyLimit=15}));
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/catalogue",records.Take(116)),HttpStatusCode.BadRequest,"117조각 누락된 구성 게시 거부");
            var duplicated=records.ToArray();duplicated[1]=duplicated[1] with{Id=duplicated[0].Id};
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/catalogue",duplicated),HttpStatusCode.BadRequest,"중복 조각 ID 게시 거부");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/catalogue",records));
            await Expect(client.GetAsync("/api/admin/case-records/stories"),HttpStatusCode.Unauthorized,"일반 계정 운영 스토리 원문 조회 차단");
            await Expect(client.PutAsJsonAsync("/api/admin/case-records/stories",stories),HttpStatusCode.Unauthorized,"일반 계정 스토리 수정 차단");
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/stories",stories.Take(12)),HttpStatusCode.BadRequest,"13파트 스토리 구성 누락 거부");
            var duplicate=stories.ToArray();duplicate[1]=duplicate[0];await Expect(admin.PutAsJsonAsync("/api/admin/case-records/stories",duplicate),HttpStatusCode.BadRequest,"중복 파트 스토리 거부");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/stories",stories));
            var authored=await Read(admin.GetAsync("/api/admin/case-records/stories"));Check(authored.GetProperty("stories")[0].GetProperty("story").GetString()==stories[0].Story,"스토리 원문 줄바꿈과 내부 공백 그대로 저장");
            var state=await State();var investigation=state.GetProperty("season").GetProperty("investigation");
            Check(investigation.GetProperty("records").GetArrayLength()==117&&investigation.GetProperty("stages").GetArrayLength()==13,"13파트마다 9조각, 총 117조각 연결");
            Check(!state.ToString().Contains("full-story-part-")&&!state.ToString().Contains("clue-"),"미발견 단서와 잠긴 스토리는 클라이언트에 전송하지 않음");
            await Sql("INSERT INTO authoritative_matches(match_id,room_id,room_name,created_at_utc) VALUES(@m,@r,'조사 검증',UTC_TIMESTAMP(6)-INTERVAL 1 HOUR);INSERT INTO authoritative_match_players(match_id,user_id,role) VALUES(@m,@u,'Survivor');INSERT INTO case_match_servers(match_id,port) VALUES(@m,24477)",("m",match),("r",room),("u",player.Id));
            await Expect(client.PostAsJsonAsync("/api/server/case-records/visit",Visit(0)),HttpStatusCode.Unauthorized,"클라이언트가 조사 구역 방문을 직접 위조할 수 없음");
            await Expect(server.PostAsJsonAsync("/api/server/case-records/visit",Visit(0,24478)),HttpStatusCode.Forbidden,"다른 경기 서버 포트에서 조사 기록 위조 차단");
            await Expect(server.PostAsJsonAsync("/api/server/case-records/visit",Visit(9)),HttpStatusCode.Conflict,"1파트 완성 전 2파트 조각 발견 차단");
            for(int i=0;i<9;i++)
            {
                await Read(server.PostAsJsonAsync("/api/server/case-records/visit",Visit(i)));
                if(i==0)
                {
                    var discovered=await State();var pieceRows=discovered.GetProperty("season").GetProperty("investigation").GetProperty("records");
                    Check(pieceRows[0].GetProperty("description").GetString()=="clue-0"&&pieceRows[1].GetProperty("description").GetString()=="","발견한 조각의 단어 단서만 즉시 공개");
                    Check(!discovered.ToString().Contains("full-story-part-"),"단서를 발견해도 별도 스토리는 아직 비공개");
                }
            }
            var unclaimed=await State();
            Check(unclaimed.GetProperty("season").GetProperty("investigation").GetProperty("stages")[0].GetProperty("complete").GetBoolean()&&!unclaimed.ToString().Contains("full-story-part-"),"9조각 완성만으로는 이야기 비공개, 파트 보상 수령 필요");
            Check(!(await Read(otherClient.GetAsync("/api/case-records"))).ToString().Contains("full-story-part-"),"시즌 중 다른 계정에는 완성자의 파트 이야기가 공개되지 않음");
            using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
            Check((await Read(anonymous.GetAsync("/api/case-records/story-archives"))).GetProperty("archives").GetArrayLength()==0,"시즌 중에는 익명 공개 보관함에 이야기 노출 없음");
            for(int i=0;i<3;i++)Check((await Read(server.PostAsJsonAsync("/api/server/case-records/visit",Visit(0)))).GetProperty("newCount").GetInt32()==0,"중복 방문은 발견 알림을 다시 만들지 않음");
            investigation=(await State()).GetProperty("season").GetProperty("investigation");
            Check(investigation.GetProperty("collectedThisWeek").GetInt32()==9,"같은 조각 중복 방문은 주간 수량을 다시 소모하지 않음");
            Check(investigation.GetProperty("records")[9].GetProperty("stageAvailable").GetBoolean(),"9조각 완성 직후 다음 파트 해금");
            for(int i=0;i<2;i++)await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimRecord",id=records[0].Id}));
            Check((await State()).GetProperty("credits").GetInt64()==30,"조각 보상 중복 수령 차단");
            Check(!(await State()).ToString().Contains("full-story-part-"),"개별 조각 보상 수령으로 파트 이야기 해금 불가");
            for(int i=0;i<2;i++)await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimInvestigationStage",id="1"}));
            Check((await State()).GetProperty("inventory").GetArrayLength()==1,"파트 완성 희귀 보상은 1회만 지급");
            Check((await State()).GetProperty("season").GetProperty("investigation").GetProperty("stages")[0].GetProperty("story").GetString()==stories[0].Story,"파트 보상 수령 후 별도 작성한 전체 원문 공개, 단서 합치기 없음");
            Check(!(await Read(otherClient.GetAsync("/api/case-records"))).ToString().Contains("full-story-part-"),"파트 보상 수령 이후에도 다른 계정에는 비공개");
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="Equip",id=CaseInvestigation.RareItem(1)}));
            Check((await State()).GetProperty("loadout")[0].GetProperty("itemId").GetString()==CaseInvestigation.RareItem(1),"조사 희귀 보상 실제 장착 가능");
            for(int i=9;i<15;i++)await Read(server.PostAsJsonAsync("/api/server/case-records/visit",Visit(i)));
            await Expect(server.PostAsJsonAsync("/api/server/case-records/visit",Visit(15)),HttpStatusCode.Conflict,"하루 제한 없이 같은 날 15조각 수집 후 주간 한도 보호");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/settings",new{weeklyLimit=30}));
            await Read(server.PostAsJsonAsync("/api/server/case-records/visit",Visit(15)));
            investigation=(await State()).GetProperty("season").GetProperty("investigation");
            Check(investigation.GetProperty("collectedThisWeek").GetInt32()==16&&investigation.GetProperty("remainingThisWeek").GetInt32()==14,"운영자가 수량을 늘리면 기록 초기화 없이 추가 수집 가능");
            Check(!investigation.GetProperty("carryOver").GetBoolean(),"미사용 주간 수량 누적 이월 없음");
            var moved=records.ToArray();moved[0]=moved[0] with{Region="changed"};
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/catalogue",moved),HttpStatusCode.Conflict,"발견된 조각 위치·파트 변경으로 기존 기록 파괴 차단");
            Check((await State()).GetProperty("season").GetProperty("investigation").GetProperty("stages")[1].GetProperty("story").GetString()=="","다음 파트 일부만 모은 경우 이야기 비공개 유지");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/settings",new{weeklyLimit=30,endsAtUtc=DateTimeOffset.UtcNow.AddSeconds(-2),updateSeasonEnd=true}));
            var archive=(await Read(anonymous.GetAsync("/api/case-records/story-archives"))).GetProperty("archives");
            Check(archive.GetArrayLength()==1&&archive[0].GetProperty("stages").GetArrayLength()==13&&archive[0].GetProperty("stages")[12].GetProperty("story").GetString()==stories[12].Story,"시즌 종료 후 별도 작성한 13파트 전체 이야기를 로그인 없이 공개·보관");
            var otherAfter=await Read(otherClient.GetAsync("/api/case-records"));Check(otherAfter.GetProperty("credits").GetInt32()==0&&otherAfter.GetProperty("inventory").GetArrayLength()==0,"시즌 종료 공개는 미참가 계정에 시즌 보상을 지급하지 않음");
            await Expect(server.PostAsJsonAsync("/api/server/case-records/visit",Visit(16)),HttpStatusCode.Conflict,"시즌 종료 후 신규 조각 수집 중지");
            var revised=records.ToArray();revised[116]=revised[116] with{Description="changed-after-archive"};await Read(admin.PutAsJsonAsync("/api/admin/case-records/catalogue",revised));
            var revisedStories=stories.ToArray();revisedStories[12]=revisedStories[12] with{Story="changed-after-archive"};await Read(admin.PutAsJsonAsync("/api/admin/case-records/stories",revisedStories));
            Check(!(await Read(anonymous.GetAsync("/api/case-records/story-archives"))).ToString().Contains("changed-after-archive"),"시즌 종료 때 저장된 이야기 원본은 이후 구성 수정에도 보존");
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/settings",new{weeklyLimit=30,endsAtUtc=DateTimeOffset.UtcNow.AddDays(90),updateSeasonEnd=true}),HttpStatusCode.Conflict,"전체 공개 후 종료 날짜를 늦춰 비공개로 되돌릴 수 없음");
            int before=(await State()).GetProperty("credits").GetInt32();await Read(anonymous.GetAsync("/api/case-records/story-archives"));Check((await State()).GetProperty("credits").GetInt32()==before,"공개 이야기 열람으로 재화·미발견 조각 보상을 지급하지 않음");
            Console.WriteLine($"PASS: {checks} investigation HTTP/DB checks");
        }
        finally
        {
            await Sql("DELETE FROM case_investigation_stories WHERE season=@s",("s",CaseRecords.Season));
            foreach(var s in oldStories)await Sql("INSERT INTO case_investigation_stories(season,stage,title,story) VALUES(@s,@stage,@title,@story)",("s",CaseRecords.Season),("stage",s["stage"]),("title",s["title"]),("story",s["story"]));
            await Sql("UPDATE case_investigation_catalogue SET records_json=@j WHERE singleton_id=1;UPDATE case_investigation_settings SET weekly_limit=@n WHERE singleton_id=1;UPDATE case_investigation_seasons SET ends_at_utc=@e,archive_json=@a WHERE season=@s;DELETE FROM case_match_servers WHERE match_id=@m;DELETE FROM authoritative_matches WHERE match_id=@m;DELETE FROM users WHERE id=@u OR id=@other",("j",oldJson),("n",oldLimit),("e",oldEnd?.UtcDateTime),("a",oldArchive),("s",CaseRecords.Season),("m",match),("u",player.Id),("other",other.Id));
        }
    }
}
