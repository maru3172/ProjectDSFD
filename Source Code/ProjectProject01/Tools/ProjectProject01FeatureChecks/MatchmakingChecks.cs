using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

internal static class MatchmakingChecks
{
    public static async Task RunAsync(AuthDatabase db,HttpClient admin,GameServerDemandManager manager,DemandSessionChecks.FakeFactory factory,string output)
    {
        int passed=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);passed++;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> task){using var r=await task;string s=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Matchmaking {(int)r.StatusCode}: {s}");return JsonDocument.Parse(s).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> task,HttpStatusCode code,string name){using var r=await task;Check(r.StatusCode==code,name+" "+r.StatusCode+(r.StatusCode==code?"":" "+await r.Content.ReadAsStringAsync()));}
        async Task Sql(string sql,params (string,object?)[] args){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,args);}
        var clients=new List<(HttpClient Client,ulong Id,string Account)>();
        for(int i=0;i<12;i++)
        {
            string account="mm_"+Guid.NewGuid().ToString("N")[..16],token=Guid.NewGuid().ToString("N");
            await Sql("INSERT INTO users(account_id,display_name,password_hash) VALUES(@a,'매칭 검증','unused')",("a",account));
            await using var c=await db.OpenConnectionAsync(default);var rows=await CommunityEndpoints.Rows(c,null,"SELECT id FROM users WHERE account_id=@a",default,("a",account));ulong id=Convert.ToUInt64(rows[0]["id"]);
            await Sql("INSERT INTO auth_sessions(user_id,access_token_hash,refresh_token_hash,access_expires_at_utc,refresh_expires_at_utc) VALUES(@u,@h,@h,UTC_TIMESTAMP()+INTERVAL 1 HOUR,UTC_TIMESTAMP()+INTERVAL 1 DAY)",("u",id),("h",SHA256.HashData(Encoding.UTF8.GetBytes(token))));
            var http=new HttpClient{BaseAddress=admin.BaseAddress};http.DefaultRequestHeaders.Authorization=new("Bearer",token);clients.Add((http,id,account));
        }
        Task<JsonElement> Party(int i)=>Read(clients[i].Client.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}));
        Task<JsonElement> Join(int i,string s)=>Read(clients[i].Client.PostAsJsonAsync("/api/rooms/matchmaking/join",new{searchId=s}));
        Task<JsonElement> Poll(int i,string s)=>Read(clients[i].Client.GetAsync("/api/rooms/matchmaking/status/"+s));
        Task<JsonElement> Cancel(int i,string s)=>Read(clients[i].Client.PostAsJsonAsync("/api/rooms/matchmaking/cancel",new{searchId=s}));
        async Task<JsonElement> Room(int i)=>(await Read(clients[i].Client.GetAsync("/api/rooms/current"))).GetProperty("room");
        async Task Invite(int a,int b)
        {
            await Sql("INSERT INTO social_friends(user_low,user_high) VALUES(@a,@b) ON DUPLICATE KEY UPDATE user_low=@a",("a",Math.Min(clients[a].Id,clients[b].Id)),("b",Math.Max(clients[a].Id,clients[b].Id)));
            foreach(int i in new[]{a,b})await Read(clients[i].Client.PostAsJsonAsync("/api/social/action",new{operation="Presence",presence="Online",context="Lobby"}));
            await Read(clients[a].Client.PostAsJsonAsync("/api/social/action",new{operation="Invite",target=clients[b].Account}));
            string invite=(await Read(clients[b].Client.GetAsync("/api/social"))).GetProperty("invites")[0].GetProperty("inviteId").GetString()!;
            await Read(clients[b].Client.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=invite}));
        }
        string fixture=Path.Combine(output,"MatchmakingFixture",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(fixture);string executable=Path.Combine(fixture,"ProjectDFSDServer.exe");await File.WriteAllTextAsync(executable,"FakeFactory only; never executed");
        int oldMaximum;
        await using(var c=await db.OpenConnectionAsync(default))oldMaximum=Convert.ToInt32((await CommunityEndpoints.Rows(c,null,"SELECT max_rooms FROM lobby_state WHERE id=1",default))[0]["max_rooms"]);
        using var anon=new HttpClient{BaseAddress=admin.BaseAddress};
        try
        {
            await Sql("UPDATE lobby_state SET max_rooms=100 WHERE id=1");
            await Expect(anon.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}),HttpStatusCode.Unauthorized,"미로그인 랜덤 파티 생성 차단");
            await Expect(anon.GetAsync("/api/rooms/matchmaking/status/"+Guid.NewGuid()),HttpStatusCode.Unauthorized,"미로그인 대기열 조회 차단");
            await Party(0);string s=Guid.NewGuid().ToString("D");
            Check((await Join(0,s)).GetProperty("state").GetString()=="Unavailable","서버 운영 중지 시 대기열 등록 안 함");
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=true,executable,arguments="MultiplayTest -log",firstPort=24677,maximumConcurrent=2,projectRoot=fixture}));
            s=Guid.NewGuid().ToString("D");var state=await Join(0,s);Check(state.GetProperty("waitingCount").GetInt32()==1&&factory.Live==0,"1/3 대기는 경기 서버를 실행하지 않음");
            Check((await Join(0,s)).GetProperty("waitingCount").GetInt32()==1,"같은 신청 재전송은 중복 등록하지 않음");
            await Expect(clients[0].Client.PostAsJsonAsync("/api/rooms/matchmaking/join",new{searchId=Guid.NewGuid()}),HttpStatusCode.Conflict,"다른 신청 ID도 같은 파티 중복 등록 차단");
            await Expect(clients[1].Client.GetAsync("/api/rooms/matchmaking/status/"+s),HttpStatusCode.NotFound,"타인의 신청 상태 조회 차단");
            Check((await Cancel(0,s)).GetProperty("state").GetString()=="Cancelled"&&(await Room(0)).GetProperty("members").GetArrayLength()==1,"취소 시 대기열만 제거·파티 유지");
            Check((await Join(0,s)).GetProperty("state").GetString()=="Cancelled","취소 뒤 늦은 시작 요청이 대기열을 부활시키지 않음");
            s=Guid.NewGuid().ToString("D");await Cancel(0,s);Check((await Join(0,s)).GetProperty("state").GetString()=="Cancelled","시작보다 취소가 먼저 도착해도 안전");
            s=Guid.NewGuid().ToString("D");await Join(0,s);await Sql("UPDATE random_match_searches SET last_seen_utc=UTC_TIMESTAMP()-INTERVAL 31 SECOND WHERE search_id=@s",("s",s));Check((await Poll(0,s)).GetProperty("state").GetString()=="Expired","30초 응답 없는 대기 신청 제거·늦은 폴링 부활 금지");
            s=Guid.NewGuid().ToString("D");await Join(0,s);await Sql("UPDATE random_match_searches SET created_at_utc=UTC_TIMESTAMP()-INTERVAL 181 SECOND WHERE search_id=@s",("s",s));Check((await Poll(0,s)).GetProperty("state").GetString()=="Expired","180초 대기 상한 뒤 재시작 가능");
            await Sql("INSERT INTO social_friends(user_low,user_high) VALUES(@a,@b)",("a",Math.Min(clients[0].Id,clients[1].Id)),("b",Math.Max(clients[0].Id,clients[1].Id)));
            await Expect(clients[0].Client.PostAsJsonAsync("/api/social/action",new{operation="Invite",target=clients[1].Account}),HttpStatusCode.BadRequest,"랜덤 매칭 친구 초대 차단");
            string staging=(await Room(0)).GetProperty("roomId").GetString()!,staleInvite=Guid.NewGuid().ToString("D");
            await Sql("INSERT INTO social_room_invites(id,room_id,sender_id,recipient_id,expires_at_utc) VALUES(@id,@r,@a,@b,UTC_TIMESTAMP()+INTERVAL 2 MINUTE)",("id",staleInvite),("r",staging),("a",clients[0].Id),("b",clients[1].Id));
            await Expect(clients[1].Client.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=staleInvite}),HttpStatusCode.BadRequest,"구버전 랜덤 초대 수락 차단");
            await Expect(clients[0].Client.PostAsJsonAsync("/api/rooms/current/chat",new{message="사용 불가"}),HttpStatusCode.Conflict,"랜덤 채팅 API 차단");
            await Sql("INSERT INTO room_chat_messages(room_id,user_id,message) VALUES(@r,@u,'과거 채팅')",("r",staging),("u",clients[0].Id));
            Check((await Room(0)).GetProperty("chatMessages").GetArrayLength()==0,"구버전 랜덤 채팅 기록 비노출");
            await Expect(clients[1].Client.PostAsJsonAsync("/api/rooms/"+(await Room(0)).GetProperty("joinCode").GetString()+"/join",new{}),HttpStatusCode.Conflict,"코드로 랜덤 대기 공간 참가 차단");
            var open=(await Read(clients[3].Client.PostAsJsonAsync("/api/rooms",new{name="오픈 매칭 검증",isPublic=true}))).GetProperty("room");
            await Invite(3,4);await Read(clients[3].Client.PostAsJsonAsync("/api/rooms/current/chat",new{message="오픈 방 한글 대화"}));
            Check((await Room(4)).GetProperty("chatMessages")[0].GetProperty("message").GetString()=="오픈 방 한글 대화","오픈 매칭 친구 초대·방 채팅 유지");
            await Party(1);await Party(2);
            s=Guid.NewGuid().ToString("D");Check((await Join(0,s)).GetProperty("waitingCount").GetInt32()==1,"솔로 첫 신청 1/3");
            Check((await Join(1,Guid.NewGuid().ToString("D"))).GetProperty("waitingCount").GetInt32()==2,"솔로 두 번째 신청 2/3");
            state=await Join(2,Guid.NewGuid().ToString("D"));Check(state.GetProperty("state").GetString()=="Matched","명시적으로 신청한 솔로 세 명만 확정");
            string roomId=(await Room(0)).GetProperty("roomId").GetString()!;var roles=new Dictionary<int,string>();
            foreach(int i in new[]{0,1,2})
            {
                var r=await Room(i);Check(r.GetProperty("roomId").GetString()==roomId,"확정 참가자 동일 방 "+i);roles[i]=r.GetProperty("assignedRole").GetString()!;
                Check(r.GetProperty("members").EnumerateArray().Count(m=>!string.IsNullOrEmpty(m.GetProperty("assignedRole").GetString()))==1,"각자 자기 역할만 공개 "+i);
            }
            Check(roles.Values.Count(r=>r=="Mannequin")==1&&roles.Values.Count(r=>r=="Survivor")==2,"카운트다운 전에 역할 1:2 확정");
            Check((await Room(3)).GetProperty("roomId").GetString()==open.GetProperty("roomId").GetString(),"오픈 매칭 참가자는 랜덤 충원 대상이 아님");
            Check((await Room(2)).GetProperty("chatMessages").GetArrayLength()==0,"확정된 랜덤 참가자에게 채팅 미노출");
            await Expect(clients[2].Client.PostAsJsonAsync("/api/rooms/current/chat",new{message="확정 후 사용 불가"}),HttpStatusCode.Conflict,"매칭 확정 후에도 채팅 차단");
            Check((await Cancel(0,s)).GetProperty("state").GetString()=="Matched","3명 확정 후 매칭 취소는 결과를 되돌리지 않음");
            await Expect(clients[0].Client.PostAsJsonAsync("/api/rooms/current/cancel-start",new{}),HttpStatusCode.Conflict,"확정 후 시작 취소 API도 차단");
            await Expect(clients[0].Client.PostAsJsonAsync("/api/rooms/current/confirm-start",new{}),HttpStatusCode.Conflict,"5초 전 데디 접속 확정 차단");
            await Expect(clients[2].Client.PostAsJsonAsync("/api/game-tickets",new{}),HttpStatusCode.Conflict,"카운트다운 중 접속 티켓 발급 차단");
            await Sql("UPDATE game_rooms SET started_at_utc=UTC_TIMESTAMP()-INTERVAL 6 SECOND WHERE id=@r",("r",roomId));
            await Read(clients[0].Client.PostAsJsonAsync("/api/rooms/current/confirm-start",new{}));await manager.TickAsync(default);Check(factory.Live==1,"카운트다운 후에만 데디 세션 준비");
            var process=factory.Started.Last();using var server=new HttpClient{BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
            await Read(server.PostAsJsonAsync("/api/server/heartbeat",new{port=process.Port,bootId="mm-boot",ready=true,managedId=process.Instance}));await manager.TickAsync(default);
            foreach(int i in new[]{0,1,2}){var r=await Room(i);Check(r.GetProperty("started").GetBoolean()&&r.GetProperty("assignedRole").GetString()==roles[i],"실제 시작 후 역할 재배정 없이 유지 "+i);}
            foreach(int i in new[]{0,1,2})await Read(clients[i].Client.PostAsJsonAsync("/api/rooms/current/return",new{}));
            await Read(server.PostAsJsonAsync("/api/server/matches/complete",new {matchId=(await Room(0)).GetProperty("matchId").GetString()}));
            var returned=new HashSet<string>();
            foreach(int i in new[]{0,1,2})
            {
                var restored=await Room(i);returned.Add(restored.GetProperty("roomId").GetString()!);
                Check(restored.GetProperty("members").GetArrayLength()==1&&!restored.GetProperty("randomMatch").GetBoolean()&&restored.GetProperty("matchSearchId").GetString()=="","경기 복귀 후 개인 신청 전 상태·자동 재매칭 없음 "+i);
            }
            Check(returned.Count==3,"복귀 후 낯선 참가자와 같은 대기 공간에 남지 않음");
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=true,executable,arguments="MultiplayTest -log",firstPort=24677,maximumConcurrent=2,projectRoot=fixture}));
            await Party(6);string legacy=(await Room(6)).GetProperty("roomId").GetString()!;
            await Sql("INSERT INTO room_members(room_id,user_id) VALUES(@r,@u)",("r",legacy),("u",clients[5].Id));
            await Expect(clients[6].Client.PostAsJsonAsync("/api/rooms/matchmaking/join",new{searchId=Guid.NewGuid()}),HttpStatusCode.Conflict,"구버전 다인 파티 신청도 서버에서 거절");
            await Sql("DELETE FROM room_members WHERE user_id=@u",("u",clients[5].Id));
            foreach(int i in new[]{8,9,10})await Party(i);
            string r1=Guid.NewGuid().ToString("D"),r2=Guid.NewGuid().ToString("D"),r3=Guid.NewGuid().ToString("D");await Join(8,r1);await Join(9,r2);
            var race=await Task.WhenAll(Cancel(8,r1),Join(10,r3));
            bool matched=race[0].GetProperty("state").GetString()=="Matched";
            Check(matched?(await Room(8)).GetProperty("members").GetArrayLength()==3:(await Room(8)).GetProperty("members").GetArrayLength()==1,"취소와 마지막 신청 경합도 확정 또는 취소 한쪽만 성립");
            Console.WriteLine($"PASS: {passed} solo matchmaking checks (isolated HTTP/MySQL, fake UE processes)");
        }
        finally
        {
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            foreach(var p in clients){await Sql("DELETE FROM game_rooms WHERE host_user_id=@u;DELETE FROM users WHERE id=@u",("u",p.Id));p.Client.Dispose();}
            await Sql("UPDATE lobby_state SET max_rooms=@n WHERE id=1",("n",oldMaximum));
        }
    }
}
