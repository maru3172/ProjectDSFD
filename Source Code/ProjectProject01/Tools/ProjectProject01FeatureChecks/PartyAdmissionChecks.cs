using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

internal static class PartyAdmissionChecks
{
    // Existing party regressions now explicitly accept consent before checking group entry.
    internal static async Task<JsonElement> ReadWithConsent(Task<HttpResponseMessage> pending,AuthDatabase db,IReadOnlyList<(ulong Id,HttpClient Client,string Account)> people)
    {
        using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Party API {(int)r.StatusCode}: {text}");
        var json=JsonDocument.Parse(text).RootElement.Clone();if(r.StatusCode!=HttpStatusCode.Accepted)return json;
        string id=json.GetProperty("partyAdmission").GetProperty("id").GetString()!;
        await using var c=await db.OpenConnectionAsync(default);var votes=await CommunityEndpoints.Rows(c,null,"SELECT user_id FROM case_party_admission_members WHERE admission_id=@a AND accepted=FALSE",default,("a",id));
        foreach(var vote in votes){var voter=people.Single(p=>p.Id==Convert.ToUInt64(vote["user_id"]));using var answer=await voter.Client.PostAsJsonAsync("/api/parties/admission",new{id,action="Accept"});if(!answer.IsSuccessStatusCode)throw new Exception(await answer.Content.ReadAsStringAsync());}
        using var transport=new HttpClient();using var retry=Clone(r.RequestMessage!,id);return await ReadWithConsent(transport.SendAsync(retry),db,people);
    }
    private static HttpRequestMessage Clone(HttpRequestMessage source,string id)
    {
        var retry=new HttpRequestMessage(source.Method,source.RequestUri);
        foreach(var h in source.Headers)retry.Headers.TryAddWithoutValidation(h.Key,h.Value);
        retry.Headers.Remove(OpenParties.AdmissionHeader);retry.Headers.Add(OpenParties.AdmissionHeader,id);
        if(source.Content is not null){retry.Content=new ByteArrayContent(source.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult());foreach(var h in source.Content.Headers)retry.Content.Headers.TryAddWithoutValidation(h.Key,h.Value);}return retry;
    }
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin,IConfiguration configuration,GameServerDemandManager manager,string output)
    {
        int count=0;void Check(bool ok,string message){if(!ok)throw new Exception(message);count++;Console.WriteLine("PASS "+message);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Consent {(int)r.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> task,HttpStatusCode status,string message){using var r=await task;Check(r.StatusCode==status,message+" "+r.StatusCode);}
        async Task Sql(string sql,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,p);}
        var people=new List<(ulong Id,HttpClient Client,string Account)>();for(int i=0;i<4;i++){var p=await OperationsIntegration.Player(db);var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);await using var c=await db.OpenConnectionAsync(default);var rows=await CommunityEndpoints.Rows(c,null,"SELECT account_id FROM users WHERE id=@u",default,("u",p.Id));people.Add((p.Id,client,(string)rows[0]["account_id"]!));}
        var host=people[0].Client;string? oldBots=configuration["Bots:EnableDedicatedMatches"];
        Task<JsonElement> Act(int i,string action,string? target=null)=>Read(people[i].Client.PostAsJsonAsync("/api/parties/action",new{action,target}));
        async Task Invite(int i){await Act(0,"Invite",people[i].Account);var state=await Read(people[i].Client.GetAsync("/api/case-records"));string id=state.GetProperty("partyState").GetProperty("invites")[0].GetProperty("id").GetString()!;await Act(i,"Accept",id);}
        async Task<string> Propose(string name="동의 검사")
        {using var r=await host.PostAsJsonAsync("/api/rooms",new{name,isPublic=false});var j=JsonDocument.Parse(await r.Content.ReadAsStringAsync()).RootElement.Clone();Check(r.StatusCode==HttpStatusCode.Accepted,"전원 수락 전 요청은 202 대기");return j.GetProperty("partyAdmission").GetProperty("id").GetString()!;}
        Task<JsonElement> Vote(int i,string id,string action="Accept")=>Read(people[i].Client.PostAsJsonAsync("/api/parties/admission",new{id,action}));
        async Task<JsonElement> Commit(string id,string name="동의 검사")
        {using var request=new HttpRequestMessage(HttpMethod.Post,"/api/rooms"){Content=JsonContent.Create(new{name,isPublic=false})};request.Headers.Add(OpenParties.AdmissionHeader,id);return await Read(host.SendAsync(request));}
        async Task NoRoom(){foreach(var p in people.Take(3))await Expect(p.Client.GetAsync("/api/rooms/current"),HttpStatusCode.NotFound,"동의 대기·거절 시 방 이동 없음");}
        try
        {
            configuration["Bots:EnableDedicatedMatches"]="true";
            for(int i=1;i<4;i++)await Sql("INSERT INTO social_friends(user_low,user_high) VALUES(@lo,@hi)",("lo",Math.Min(people[0].Id,people[i].Id)),("hi",Math.Max(people[0].Id,people[i].Id)));
            await Act(0,"Create");await Invite(1);await Invite(2);
            await Expect(people[1].Client.PostAsJsonAsync("/api/parties/action",new{action="Transfer",target=people[2].Id.ToString()}),HttpStatusCode.Forbidden,"일반 파티원 위임 위조 차단");
            await Expect(host.PostAsJsonAsync("/api/parties/action",new{action="Transfer",target=people[3].Id.ToString()}),HttpStatusCode.NotFound,"파티 밖 계정에 위임 차단");
            await Expect(host.PostAsJsonAsync("/api/parties/action",new{action="Transfer",target=people[0].Id.ToString()}),HttpStatusCode.BadRequest,"본인에게 중복 위임 차단");
            await Act(0,"Transfer",people[1].Id.ToString());var state=await Read(people[1].Client.GetAsync("/api/case-records"));
            Check(state.GetProperty("partyState").GetProperty("party").GetProperty("isLeader").GetBoolean(),"선택한 파티원에게 파티장 권한 이전");
            await Expect(host.PostAsJsonAsync("/api/parties/action",new{action="Kick",target=people[2].Id.ToString()}),HttpStatusCode.Forbidden,"이전 파티장 권한 즉시 회수");await Act(1,"Transfer",people[0].Id.ToString());
            string proposal=await Propose();await NoRoom();
            state=(await Read(host.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission");
            Check(state.GetProperty("isLeader").GetBoolean()&&state.GetProperty("hasAccepted").GetBoolean()&&state.GetProperty("required").GetInt32()==2&&state.GetProperty("accepted").GetInt32()==0,"파티장은 자동 동의하고 나머지 두 명만 집계");
            await Expect(host.PostAsJsonAsync("/api/parties/admission",new{id=proposal,action="Accept"}),HttpStatusCode.Forbidden,"파티장에게 수락 버튼·중복 수락 없음");
            await Expect(people[3].Client.PostAsJsonAsync("/api/parties/admission",new{id=proposal,action="Accept"}),HttpStatusCode.Forbidden,"외부 계정 투표 위조 차단");
            await Vote(1,proposal);await Vote(1,proposal);state=(await Read(host.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission");Check(state.GetProperty("accepted").GetInt32()==1&&state.GetProperty("state").GetString()=="Pending","중복 응답은 한 표이며 한 명 미동의 시 이동 없음");
            await Vote(2,proposal,"Decline");await NoRoom();Check((await Read(host.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission").GetProperty("state").GetString()=="Cancelled","한 명 거절로 전체 입장 취소");
            proposal=await Propose();await Sql("UPDATE case_party_admissions SET expires_at_utc=UTC_TIMESTAMP(6)-INTERVAL 1 SECOND WHERE id=@a",("a",proposal));state=(await Read(people[1].Client.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission");Check(state.GetProperty("state").GetString()=="Cancelled","30초 무응답 시 안전 취소");await NoRoom();
            proposal=await Propose();await Act(0,"Transfer",people[1].Id.ToString());Check((await Read(people[2].Client.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission").GetProperty("state").GetString()=="Cancelled","파티장 위임은 진행 중인 입장 동의 취소");await Act(1,"Transfer",people[0].Id.ToString());
            proposal=await Propose();await Vote(1,proposal);await Act(2,"Leave");Check((await Read(host.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission").GetProperty("state").GetString()=="Cancelled","파티원 이탈은 수락 진행 취소");await Invite(2);
            proposal=await Propose();await Vote(1,proposal);await Vote(2,proposal);await NoRoom();Check((await Read(host.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission").GetProperty("state").GetString()=="Ready","전원 수락 후에도 파티장 원래 요청 확정 전 입장 없음");
            using(var wrong=new HttpRequestMessage(HttpMethod.Post,"/api/rooms"){Content=JsonContent.Create(new{name="다른 목적지",isPublic=false})}){wrong.Headers.Add(OpenParties.AdmissionHeader,proposal);await Expect(host.SendAsync(wrong),HttpStatusCode.Conflict,"동의한 목적지·옵션을 다른 요청으로 바꿔치기 차단");}
            var entered=await Commit(proposal);Check(entered.GetProperty("room").GetProperty("members").GetArrayLength()==3,"전원 수락 시 세 명 동시 입장");
            await using(var c=await db.OpenConnectionAsync(default)){var rows=await CommunityEndpoints.Rows(c,null,"SELECT state,request_hash FROM case_party_admissions WHERE id=@a",default,("a",proposal));Check((string)rows[0]["state"]! =="Completed"&&!Convert.ToString(rows[0]["request_hash"])!.Contains("동의 검사"),"동의는 한 번 소비하고 요청 내용은 해시로 보관");}
            foreach(var p in people.Take(3))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            using(var old=new HttpRequestMessage(HttpMethod.Post,"/api/rooms"){Content=JsonContent.Create(new{name="동의 검사",isPublic=false})}){old.Headers.Add(OpenParties.AdmissionHeader,proposal);await Expect(host.SendAsync(old),HttpStatusCode.Conflict,"완료된 동의 재사용으로 재입장 차단");}
            await Act(2,"Leave");
            var target=(await Read(people[3].Client.PostAsJsonAsync("/api/rooms",new{name="입장 대상",isPublic=false}))).GetProperty("room");string code=target.GetProperty("joinCode").GetString()!;
            using(var request=await host.PostAsJsonAsync($"/api/rooms/{code}/join",new{}))
            {Check(request.StatusCode==HttpStatusCode.Accepted,"참가 코드 입장도 파티원 동의 필요");proposal=JsonDocument.Parse(await request.Content.ReadAsStringAsync()).RootElement.GetProperty("partyAdmission").GetProperty("id").GetString()!;}
            await Vote(1,proposal);using(var request=new HttpRequestMessage(HttpMethod.Post,$"/api/rooms/{code}/join"){Content=JsonContent.Create(new{})}){request.Headers.Add(OpenParties.AdmissionHeader,proposal);entered=await Read(host.SendAsync(request));}Check(entered.GetProperty("room").GetProperty("members").GetArrayLength()==3,"기존 방에도 동의한 파티 전원 입장");
            foreach(var p in people.Take(2))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));await Read(people[3].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            using(var request=await host.PostAsJsonAsync("/api/bot-matches",new{role="Any",difficulty=4}))
            {Check(request.StatusCode==HttpStatusCode.Accepted,"2인 봇전도 동의 전 대기");proposal=JsonDocument.Parse(await request.Content.ReadAsStringAsync()).RootElement.GetProperty("partyAdmission").GetProperty("id").GetString()!;}
            await Vote(1,proposal);using(var request=new HttpRequestMessage(HttpMethod.Post,"/api/bot-matches"){Content=JsonContent.Create(new{role="Any",difficulty=4})}){request.Headers.Add(OpenParties.AdmissionHeader,proposal);entered=await Read(host.SendAsync(request));}
            Check(entered.GetProperty("room").GetProperty("members").GetArrayLength()==2&&entered.GetProperty("room").GetProperty("bots").GetArrayLength()==1,"봇전 전원 수락 후 2인과 봇 한 명 구성 유지");
            string fixture=Path.Combine(output,"PartyReadinessFixture",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(fixture);string executable=Path.Combine(fixture,"ProjectDFSDServer.exe");await File.WriteAllTextAsync(executable,"FakeFactory only");
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=true,executable,arguments="MultiplayTest -log",firstPort=25177,maximumConcurrent=1,projectRoot=fixture}));
            await Expect(host.PostAsJsonAsync("/api/rooms/current/start",new{}),HttpStatusCode.Conflict,"2인 봇전 미준비 상태 시작 차단");
            state=await Read(host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));Check(state.GetProperty("room").GetProperty("isReady").GetBoolean()&&!state.GetProperty("room").GetProperty("starting").GetBoolean(),"2인 봇전 방장만 준비하면 대기 유지");
            await Expect(host.PostAsJsonAsync("/api/rooms/current/start",new{}),HttpStatusCode.Conflict,"방장만 준비한 상태 시작 차단");
            state=await Read(host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=false}));Check(!state.GetProperty("room").GetProperty("isReady").GetBoolean(),"두 명 준비 전에는 준비 취소 가능");
            state=await Read(people[1].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));Check(!state.GetProperty("room").GetProperty("starting").GetBoolean(),"동료만 준비한 상태도 대기 유지");
            state=await Read(host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));Check(state.GetProperty("room").GetProperty("starting").GetBoolean(),"방장이 마지막 준비를 누르면 별도 시작 요청 없이 카운트다운 시작");
            Check((await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("starting").GetBoolean(),"자동 시작은 상대방에게도 동일하게 제공");
            await Expect(people[1].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=false}),HttpStatusCode.Conflict,"시작 확정 후 늦은 준비 응답으로 상태 오염 차단");
            await Expect(host.PostAsJsonAsync("/api/rooms/current/start",new{}),HttpStatusCode.Conflict,"자동 시작 이후 수동 시작 중복 차단");
            await Read(host.PostAsJsonAsync("/api/rooms/current/cancel-start",new{}));
            await Read(host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));
            state=await Read(people[1].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));Check(state.GetProperty("room").GetProperty("starting").GetBoolean()&&!state.GetProperty("room").GetProperty("isHost").GetBoolean(),"동료가 마지막 준비여도 서버가 자동 시작");
            await Read(host.PostAsJsonAsync("/api/rooms/current/cancel-start",new{}));
            var simultaneous=await Task.WhenAll(Read(host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true})),Read(people[1].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true})));
            Check((await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("starting").GetBoolean(),"두 명이 동시에 준비해도 카운트다운 진입");
            await using(var c=await db.OpenConnectionAsync(default)){var rows=await CommunityEndpoints.Rows(c,null,"SELECT match_id FROM game_rooms WHERE id=@r",default,("r",entered.GetProperty("room").GetProperty("roomId").GetString()));Check(rows[0]["match_id"] is null or DBNull,"자동 준비 시작 단계에는 경기나 역할을 미리 만들지 않음");}
            await Read(host.PostAsJsonAsync("/api/rooms/current/cancel-start",new{}));
            await Read(host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));
            async Task<HttpStatusCode> ReadyRace(){using var response=await people[1].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true});return response.StatusCode;}
            async Task<HttpStatusCode> UnreadyRace(){using var response=await host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=false});return response.StatusCode;}
            var readinessRace=await Task.WhenAll(ReadyRace(),UnreadyRace());state=(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room");
            bool started=state.GetProperty("starting").GetBoolean();Check(readinessRace[0]==HttpStatusCode.OK&&(started?readinessRace[1]==HttpStatusCode.Conflict:readinessRace[1]==HttpStatusCode.OK&&!state.GetProperty("isReady").GetBoolean()),"마지막 준비와 준비 취소 경쟁도 하나의 일관된 상태로 확정");
            if(started)await Read(host.PostAsJsonAsync("/api/rooms/current/cancel-start",new{}));
            foreach(var p in people.Take(2))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            // Capacity is checked again at commit: no partial group entry when a seat is taken.
            target=(await Read(people[3].Client.PostAsJsonAsync("/api/rooms",new{name="정원 변경",isPublic=false}))).GetProperty("room");code=target.GetProperty("joinCode").GetString()!;
            using(var request=await host.PostAsJsonAsync($"/api/rooms/{code}/join",new{})){Check(request.StatusCode==HttpStatusCode.Accepted,"정원 변경 전 파티 입장 대기");proposal=JsonDocument.Parse(await request.Content.ReadAsStringAsync()).RootElement.GetProperty("partyAdmission").GetProperty("id").GetString()!;}
            await Vote(1,proposal);await Read(people[2].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{}));
            using(var request=new HttpRequestMessage(HttpMethod.Post,$"/api/rooms/{code}/join"){Content=JsonContent.Create(new{})}){request.Headers.Add(OpenParties.AdmissionHeader,proposal);await Expect(host.SendAsync(request),HttpStatusCode.Conflict,"수락 중 자리가 차면 그룹 입장 전체 거부");}
            foreach(var p in people.Take(2))await Expect(p.Client.GetAsync("/api/rooms/current"),HttpStatusCode.NotFound,"정원 부족 시 파티 일부만 들어가지 않음");
            Check((await Read(host.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission").GetProperty("state").GetString()=="Cancelled","대상 방 정원 변경도 대기 취소");
            foreach(var i in new[]{2,3})await Read(people[i].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            // A friend's room invitation cannot bypass party consent.
            await Read(people[3].Client.PostAsJsonAsync("/api/rooms",new{name="친구 초대 검증",isPublic=false}));
            await Read(host.PostAsJsonAsync("/api/social/action",new{operation="Presence",presence="Online",context="Lobby"}));
            await Read(people[3].Client.PostAsJsonAsync("/api/social/action",new{operation="Invite",target=people[0].Account}));
            string invitation;
            await using(var c=await db.OpenConnectionAsync(default)){var rows=await CommunityEndpoints.Rows(c,null,"SELECT id FROM social_room_invites WHERE recipient_id=@u",default,("u",people[0].Id));invitation=Convert.ToString(rows[0]["id"])!;}
            using(var request=await host.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=invitation})){Check(request.StatusCode==HttpStatusCode.Accepted,"친구의 방 초대를 수락해도 파티원 확인 필요");proposal=JsonDocument.Parse(await request.Content.ReadAsStringAsync()).RootElement.GetProperty("partyAdmission").GetProperty("id").GetString()!;}
            await Vote(1,proposal);
            using(var request=new HttpRequestMessage(HttpMethod.Post,"/api/social/action"){Content=JsonContent.Create(new{operation="AcceptInvite",target=invitation})}){request.Headers.Add(OpenParties.AdmissionHeader,proposal);await Read(host.SendAsync(request));}
            Check((await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("members").GetArrayLength()==3,"친구 초대도 전원 수락 후 함께 입장");
            foreach(var i in new[]{0,1,3})await Read(people[i].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            // Concurrent completion consumes the proposal exactly once.
            proposal=await Propose("동시 입장");await Vote(1,proposal);
            async Task<HttpStatusCode> Race(){using var request=new HttpRequestMessage(HttpMethod.Post,"/api/rooms"){Content=JsonContent.Create(new{name="동시 입장",isPublic=false})};request.Headers.Add(OpenParties.AdmissionHeader,proposal);using var response=await host.SendAsync(request);return response.StatusCode;}
            var statuses=await Task.WhenAll(Race(),Race());Check(statuses.Count(s=>s==HttpStatusCode.OK)==1&&statuses.Count(s=>s==HttpStatusCode.Conflict)==1,"동시 입장 확정도 한 번만 실행");
            Check((await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("members").GetArrayLength()==2,"동시 확정 후에도 파티원 중복 없이 유지");
            var metadata=await Read(people[1].Client.GetAsync("/api/parties/admission"));Check(metadata.GetProperty("partyLeaderId").GetString()==people[0].Id.ToString()&&metadata.GetProperty("partyMemberCount").GetInt32()==2,"가벼운 상태 조회에 현재 파티장·인원 제공");
            await Read(host.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=2}));
            Check(!(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("botMatch").GetBoolean(),"오픈매칭에 봇을 추가해도 별도 봇전으로 취급하지 않음");
            await Expect(host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}),HttpStatusCode.BadRequest,"오픈매칭의 봇 추가 방은 방장 준비 규칙을 바꾸지 않음");
            await Read(people[1].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));Check((await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("canStart").GetBoolean(),"봇 포함 오픈매칭은 기존처럼 다른 참가자 준비 후 방장이 시작");
            foreach(var p in people.Take(2))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            // Force logout is detected before an accepted proposal can commit.
            proposal=await Propose("연결 만료");await Vote(1,proposal);await Sql("UPDATE auth_sessions SET revoked_at_utc=UTC_TIMESTAMP(6) WHERE user_id=@u",("u",people[1].Id));
            Check((await Read(host.GetAsync("/api/parties/admission"))).GetProperty("partyAdmission").GetProperty("state").GetString()=="Cancelled","수락 후 강제 로그아웃도 입장 취소");
            Console.WriteLine($"PASS: {count} party admission and leadership HTTP/DB checks");
        }
        finally{configuration["Bots:EnableDedicatedMatches"]=oldBots;await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);foreach(var p in people){await Sql("DELETE FROM game_rooms WHERE host_user_id=@u;DELETE FROM users WHERE id=@u",("u",p.Id));p.Client.Dispose();}}
    }
}
