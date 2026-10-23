using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

internal static class BotMatchChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin,GameServerDemandManager manager,
        DemandSessionChecks.FakeFactory factory,string output,IConfiguration configuration)
    {
        // Parent fixture requires BOTH port 23307 and the exact isolated Saved data directory.
        int passed=0;void Check(bool value,string name){if(!value)throw new Exception(name);++passed;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending)
        {using var response=await pending;string text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"Bot match {(int)response.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode code,string name)
        {using var response=await pending;Check(response.StatusCode==code,name+" "+response.StatusCode+(response.StatusCode==code?"":" "+await response.Content.ReadAsStringAsync()));}
        async Task Sql(string sql,params (string,object?)[] values)
        {await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,values);}
        async Task<int> Count(string sql)
        {await using var c=await db.OpenConnectionAsync(default);return Convert.ToInt32((await CommunityEndpoints.Rows(c,null,sql,default))[0]["n"]);}
        var player=await OperationsIntegration.Player(db);var outsider=await OperationsIntegration.Player(db);
        using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        using var other=new HttpClient{BaseAddress=admin.BaseAddress};other.DefaultRequestHeaders.Authorization=new("Bearer",outsider.Token);
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
        using var server=new HttpClient{BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        string? previous=configuration["Bots:EnableDedicatedMatches"];
        int oldMaximum=await Count("SELECT max_rooms n FROM lobby_state WHERE id=1");
        string fixture=Path.Combine(output,"BotMatchFixture",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(fixture);
        string executable=Path.Combine(fixture,"ProjectDFSDServer.exe");await File.WriteAllTextAsync(executable,"FakeFactory only; never executed");
        async Task<JsonElement> Room()=>(await Read(client.GetAsync("/api/rooms/current"))).GetProperty("room");
        try
        {
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            await Sql("DELETE FROM game_rooms;DELETE FROM game_server_slots");
            await Read(admin.PutAsJsonAsync("/api/admin/room-capacity",new{maximumRooms=3}));
            configuration["Bots:EnableDedicatedMatches"]="false";
            await Expect(anonymous.PostAsJsonAsync("/api/bot-matches",new{role="Survivor",difficulty=2}),HttpStatusCode.Unauthorized,"미로그인 봇전 차단");
            await Expect(client.PostAsJsonAsync("/api/bot-matches",new{role="Survivor",difficulty=2}),HttpStatusCode.ServiceUnavailable,"미완성 서버 실행 경로는 기본 비활성");
            configuration["Bots:EnableDedicatedMatches"]="true";
            var open=(await Read(client.PostAsJsonAsync("/api/rooms",new{name="봇 오픈방",isPublic=true}))).GetProperty("room");
            string openId=open.GetProperty("roomId").GetString()!,openCode=open.GetProperty("joinCode").GetString()!;
            await Expect(anonymous.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=2}),HttpStatusCode.Unauthorized,"미로그인 봇 관리 차단");
            await Read(client.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=1}));
            open=(await Read(client.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=5}))).GetProperty("room");
            Check(open.GetProperty("bots").GetArrayLength()==2,"방장이 오픈방의 빈 경기 슬롯에 봇 추가");
            await Expect(client.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=3}),HttpStatusCode.Conflict,"봇 포함 3명 정원 초과 차단");
            await Expect(other.PostAsJsonAsync("/api/rooms/"+openCode+"/join",new{}),HttpStatusCode.Conflict,"봇이 채운 슬롯에 사람 중복 입장 차단");
            string botId=open.GetProperty("bots")[0].GetProperty("botId").GetString()!;
            await Read(client.PostAsJsonAsync("/api/rooms/current/bots",new{action="remove",botId}));
            await Read(other.PostAsJsonAsync("/api/rooms/"+openCode+"/join",new{}));
            await Expect(other.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=1}),HttpStatusCode.Forbidden,"비방장의 봇 변경 거부");
            await Read(other.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));
            open=await Room();botId=open.GetProperty("bots")[0].GetProperty("botId").GetString()!;
            open=(await Read(client.PostAsJsonAsync("/api/rooms/current/bots",new{action="difficulty",botId,difficulty=4}))).GetProperty("room");
            Check(open.GetProperty("bots")[0].GetProperty("difficulty").GetInt32()==4,"방장이 개별 봇 난이도 변경");
            Check(open.GetProperty("members").EnumerateArray().All(m=>!m.GetProperty("ready").GetBoolean()),"봇 난이도 변경 후 사람의 준비 동의 초기화");
            await Expect(client.PostAsJsonAsync("/api/rooms/current/bots",new{action="difficulty",botId,difficulty=6}),HttpStatusCode.BadRequest,"오픈봇도 5단계 상한 유지");
            await Expect(client.PostAsJsonAsync("/api/rooms/current/bots",new{action="remove",botId=Guid.NewGuid().ToString("D")}),HttpStatusCode.NotFound,"다른 방 봇 ID 변경 차단");
            await Sql("UPDATE game_rooms SET status='Starting' WHERE id=@r",("r",openId));
            await Expect(client.PostAsJsonAsync("/api/rooms/current/bots",new{action="remove",botId}),HttpStatusCode.Conflict,"시작 확정 중 봇 삭제 차단");
            await Sql("UPDATE game_rooms SET status='Waiting' WHERE id=@r",("r",openId));
            await Read(other.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            await Read(client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            foreach(int level in new[]{0,6})await Expect(client.PostAsJsonAsync("/api/bot-matches",new{role="Survivor",difficulty=level}),HttpStatusCode.BadRequest,"난이도 범위 위반 차단");
            await Expect(client.PostAsJsonAsync("/api/bot-matches",new{role="Observer",difficulty=2}),HttpStatusCode.BadRequest,"봇전의 위조 관전 역할 차단");
            int users=await Count("SELECT COUNT(*) n FROM users");
            foreach(string role in new[]{"Survivor","Mannequin"})foreach(int level in Enumerable.Range(1,5))
            {
                await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=true,executable,arguments="MultiplayTest -log",firstPort=24877,maximumConcurrent=1,projectRoot=fixture}));
                var room=(await Read(client.PostAsJsonAsync("/api/bot-matches",new{role,difficulty=level}))).GetProperty("room");
                string id=room.GetProperty("roomId").GetString()!;
                Check(room.GetProperty("botMatch").GetBoolean()&&room.GetProperty("members").GetArrayLength()==1&&room.GetProperty("bots").GetArrayLength()==2,"봇전은 사람 1명과 별도 봇 2명");
                Check(room.GetProperty("bots").EnumerateArray().All(b=>b.GetProperty("difficulty").GetInt32()==level),"선택한 1~5단계가 서버 로스터에 고정");
                Check(room.GetProperty("canStart").GetBoolean(),"봇도 경기 정원에 포함하고 가짜 준비 요청 불필요");
                await Expect(client.PostAsJsonAsync("/api/bot-matches",new{role,difficulty=level}),HttpStatusCode.Conflict,"중복 봇전 방 생성 차단");
                await Expect(other.PostAsJsonAsync("/api/rooms/"+room.GetProperty("joinCode").GetString()+"/join",new{}),HttpStatusCode.Conflict,"참가 코드로 개인 봇전 침입 차단");
                await Read(client.PostAsJsonAsync("/api/rooms/current/start",new{}));
                await Expect(client.PostAsJsonAsync("/api/rooms/current/confirm-start",new{}),HttpStatusCode.Conflict,"봇전도 5초 전 확정 금지");
                await Sql("UPDATE game_rooms SET started_at_utc=UTC_TIMESTAMP(6)-INTERVAL 6 SECOND WHERE id=@r",("r",id));
                await Read(client.PostAsJsonAsync("/api/rooms/current/confirm-start",new{}));await manager.TickAsync(default);
                Check(factory.Live==1,"봇전도 확정 후에만 실제 서버 프로세스 할당 경로 사용");
                var process=factory.Started.Last();server.DefaultRequestHeaders.Remove("X-ProjectProject01-Server-Port");server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Port",process.Port.ToString());
                await Read(server.PostAsJsonAsync("/api/server/heartbeat",new{port=process.Port,bootId="bots-"+process.Instance,managedId=process.Instance,ready=true}));await manager.TickAsync(default);
                room=await Room();string match=room.GetProperty("matchId").GetString()!;
                Check(room.GetProperty("started").GetBoolean()&&room.GetProperty("assignedRole").GetString()==role,"실제 서버 준비 후에만 선택 역할로 입장 허용");
                int mannequins=(role=="Mannequin"?1:0)+room.GetProperty("bots").EnumerateArray().Count(b=>b.GetProperty("assignedRole").GetString()=="Mannequin");
                Check(mannequins==1,"사람과 봇 전체에서 마네킹 1명·생존자 2명 유지");
                // Even a direct DB edit to the waiting configuration cannot rewrite an admitted match.
                await Sql("UPDATE room_bots SET difficulty=1 WHERE room_id=@r",("r",id));
                var ticket=await Read(client.PostAsJsonAsync("/api/game-tickets",new{}));
                var claim=await Read(server.PostAsJsonAsync("/api/server/game-tickets/consume",new{ticket=ticket.GetProperty("ticket").GetString()}));
                Check(claim.GetProperty("bots").GetArrayLength()==2&&claim.GetProperty("bots").EnumerateArray().All(b=>b.GetProperty("difficulty").GetInt32()==level),"신뢰된 서버 접속 허가에는 확정 스냅샷만 전달");
                Check(await Count("SELECT COUNT(*) n FROM users")==users,"봇 생성은 사용자/인증 계정을 만들지 않음");
                int beforeGames=await Count($"SELECT games n FROM bot_skill_ratings WHERE user_id={player.Id} AND role='{role}'");
                var result=new{matchId=match,userId=player.Id.ToString(),role,success=true,
                    captureCount=role=="Mannequin"?(int?)2:null,firstCaptureSeconds=role=="Mannequin"?(double?)10:null,
                    allCapturedSeconds=role=="Mannequin"?(double?)25:null,rescueCount=role=="Survivor"?(int?)0:null,escapeSeconds=role=="Survivor"?(double?)40:null};
                await Read(server.PostAsJsonAsync("/api/server/matches/results",result));
                await Read(server.PostAsJsonAsync("/api/server/matches/results",result));
                Check(await Count($"SELECT games n FROM bot_skill_ratings WHERE user_id={player.Id} AND role='{role}'")==beforeGames+1,"검증 결과만 레이팅 반영·재전송 중복 상승 방지");
                await Read(server.PostAsJsonAsync("/api/server/matches/complete",new{matchId=match}));
                await Read(client.PostAsJsonAsync("/api/rooms/current/return",new{}));room=await Room();
                Check(!room.GetProperty("started").GetBoolean()&&room.GetProperty("bots").EnumerateArray().All(b=>b.GetProperty("assignedRole").GetString()==""),"경기 종료·복귀 시 봇 역할도 초기화");
                await Read(client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
                await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
                Check(factory.Live==0,"봇전 종료 후 할당 프로세스 정리");
            }
            foreach(var scenario in new[]{(People:1,Rating:1900,Level:5),(People:2,Rating:650,Level:1)})
            {
                foreach(ulong user in new[]{player.Id,outsider.Id})foreach(string r in new[]{"Mannequin","Survivor"})
                    await Sql("INSERT INTO bot_skill_ratings(user_id,role,rating) VALUES(@u,@r,@v) ON DUPLICATE KEY UPDATE rating=@v",("u",user),("r",r),("v",scenario.Rating));
                await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=true,executable,arguments="MultiplayTest -log",firstPort=24877,maximumConcurrent=1,projectRoot=fixture}));
                await Read(client.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}));string search=Guid.NewGuid().ToString("D");
                await Read(client.PostAsJsonAsync("/api/rooms/matchmaking/join",new{searchId=search}));
                await Sql("UPDATE random_match_searches SET created_at_utc=UTC_TIMESTAMP(6)-INTERVAL 299 SECOND WHERE search_id=@s",("s",search));
                string otherSearch=Guid.NewGuid().ToString("D");
                if(scenario.People==2)
                {await Read(other.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}));await Read(other.PostAsJsonAsync("/api/rooms/matchmaking/join",new{searchId=otherSearch}));}
                Check((await Read(client.GetAsync("/api/rooms/matchmaking/status/"+search))).GetProperty("state").GetString()=="Waiting","5분 전에 봇을 성급하게 투입하지 않음");
                await Sql("UPDATE random_match_searches SET created_at_utc=UTC_TIMESTAMP(6)-INTERVAL 301 SECOND WHERE search_id=@s",("s",search));
                var found=await Read(client.GetAsync("/api/rooms/matchmaking/status/"+search));var randomRoom=found.GetProperty("room");
                Check(found.GetProperty("state").GetString()=="Matched"&&randomRoom.GetProperty("bots").GetArrayLength()==3-scenario.People,"5분 이후 랜덤 대기열의 부족한 슬롯만 봇 보충");
                Check(randomRoom.GetProperty("bots").EnumerateArray().All(b=>b.GetProperty("difficulty").GetInt32()==scenario.Level),"상대 역할 레이팅에 맞춰 난이도 선택");
                Check((await Read(client.PostAsJsonAsync("/api/rooms/matchmaking/cancel",new{searchId=search}))).GetProperty("state").GetString()=="Matched","봇 보충 확정 후 취소로 로스터 변경 불가");
                string roomId=randomRoom.GetProperty("roomId").GetString()!;
                var fixedRoles=randomRoom.GetProperty("bots").EnumerateArray().Select(b=>b.GetProperty("assignedRole").GetString()).ToArray();
                await Sql("UPDATE game_rooms SET started_at_utc=UTC_TIMESTAMP(6)-INTERVAL 6 SECOND WHERE id=@r",("r",roomId));
                await Read(client.PostAsJsonAsync("/api/rooms/current/confirm-start",new{}));await manager.TickAsync(default);
                var process=factory.Started.Last();server.DefaultRequestHeaders.Remove("X-ProjectProject01-Server-Port");server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Port",process.Port.ToString());
                await Read(server.PostAsJsonAsync("/api/server/heartbeat",new{port=process.Port,bootId="random-bots-"+process.Instance,managedId=process.Instance,ready=true}));await manager.TickAsync(default);
                randomRoom=await Room();
                Check(randomRoom.GetProperty("started").GetBoolean()&&randomRoom.GetProperty("bots").EnumerateArray().Select(b=>b.GetProperty("assignedRole").GetString()).SequenceEqual(fixedRoles),"서버 준비 후에도 랜덤 확정 역할을 다시 추첨하지 않음");
                await Read(server.PostAsJsonAsync("/api/server/matches/complete",new{matchId=randomRoom.GetProperty("matchId").GetString()}));
                await Read(client.PostAsJsonAsync("/api/rooms/current/return",new{}));
                var restored=await Room();Check(restored.GetProperty("randomParty").GetBoolean()&&restored.GetProperty("bots").GetArrayLength()==0,"복귀한 개인은 봇 없는 새 랜덤 대기 상태로 복원");
                await Read(client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
                if(scenario.People==2){await Read(other.PostAsJsonAsync("/api/rooms/current/return",new{}));await Read(other.PostAsJsonAsync("/api/rooms/current/leave",new{}));}
                await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            }
            Console.WriteLine($"PASS: {passed} bot match API/DB checks; fake UE processes, not an actual game-server execution test");
        }
        finally
        {
            configuration["Bots:EnableDedicatedMatches"]=previous;
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            await Sql("DELETE FROM game_rooms WHERE host_user_id IN (@a,@b);DELETE FROM users WHERE id IN (@a,@b);UPDATE lobby_state SET max_rooms=@n WHERE id=1",("a",player.Id),("b",outsider.Id),("n",oldMaximum));
        }
    }
}
