using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

internal static class OpenObserverChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin,GameServerDemandManager manager,DemandSessionChecks.FakeFactory factory,string output,IConfiguration configuration)
    {
        int checks=0;void Check(bool condition,string name){if(!condition)throw new Exception(name);checks++;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> task){using var response=await task;string body=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"Observer API {(int)response.StatusCode}: {body}");return JsonDocument.Parse(body).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> task,HttpStatusCode status,string name){using var response=await task;Check(response.StatusCode==status,name+" "+response.StatusCode);}
        async Task Sql(string sql,params (string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,p);}
        var people=new List<(ulong Id,HttpClient Client)>();
        for(int i=0;i<6;i++){var player=await OperationsIntegration.Player(db);var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);people.Add((player.Id,client));}
        var host=people[0].Client;string? oldObservers=configuration["Bots:EnableOpenObservers"],oldBots=configuration["Bots:EnableDedicatedMatches"];
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};using var server=new HttpClient{BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        try
        {
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            configuration["Bots:EnableOpenObservers"]="false";
            await Expect(host.PostAsJsonAsync("/api/rooms/current/participants",new{action="observe"}),HttpStatusCode.ServiceUnavailable,"미검증 관전 실행 경로는 기본 비활성");
            configuration["Bots:EnableOpenObservers"]="true";configuration["Bots:EnableDedicatedMatches"]="true";
            var room=(await Read(host.PostAsJsonAsync("/api/rooms",new{name="관전 3+2 검증",isPublic=true}))).GetProperty("room");string code=room.GetProperty("joinCode").GetString()!;
            for(int i=1;i<5;i++)await Read(people[i].Client.PostAsJsonAsync("/api/rooms/"+code+"/join",new{}));
            room=(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room");
            Check(room.GetProperty("members").GetArrayLength()==5&&room.GetProperty("members").EnumerateArray().Count(m=>m.GetProperty("isObserver").GetBoolean())==2,"정원 3명 이후에는 별도 관전 자리 2명으로 자동 입장");
            await Expect(people[5].Client.PostAsJsonAsync("/api/rooms/"+code+"/join",new{}),HttpStatusCode.Conflict,"플레이 3+관전 2 이후 초과 입장 차단");
            await Expect(anonymous.PostAsJsonAsync("/api/rooms/current/participants",new{action="observe"}),HttpStatusCode.Unauthorized,"미로그인 참가 구성 변경 차단");
            await Expect(people[1].Client.PostAsJsonAsync("/api/rooms/current/participants",new{action="observe",userId=people[2].Id.ToString()}),HttpStatusCode.Forbidden,"비방장이 타인의 참가 유형 변경 불가");
            await Expect(host.PostAsJsonAsync("/api/rooms/current/participants",new{action="observe"}),HttpStatusCode.Conflict,"관전 2명 상한 유지");
            await Expect(people[3].Client.PostAsJsonAsync("/api/rooms/current/participants",new{action="play"}),HttpStatusCode.Conflict,"관전자도 플레이 자리가 있어야 변경 가능");
            Check((await Read(people[3].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}))).GetProperty("room").GetProperty("isReady").GetBoolean(),"사람 관전자도 준비 완료 가능");
            await Read(host.PostAsJsonAsync("/api/rooms/current/participants",new{action="kick",userId=people[2].Id.ToString()}));
            await Expect(people[2].Client.GetAsync("/api/rooms/current"),HttpStatusCode.NotFound,"방장이 강퇴하면 해당 방 참가 정보 제거");
            await Read(people[3].Client.PostAsJsonAsync("/api/rooms/current/participants",new{action="play"}));
            await Read(host.PostAsJsonAsync("/api/rooms/current/participants",new{action="observe"}));
            await Read(host.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=2}));
            room=(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room");
            Check(room.GetProperty("bots").GetArrayLength()==1&&room.GetProperty("members").EnumerateArray().Count(m=>!m.GetProperty("isObserver").GetBoolean())==2,"관전자는 봇 포함 경기 정원을 소모하지 않음");
            await Sql("INSERT INTO social_friends(user_low,user_high) VALUES(@lo,@hi)",("lo",Math.Min(people[0].Id,people[2].Id)),("hi",Math.Max(people[0].Id,people[2].Id)));
            await Read(people[2].Client.PostAsJsonAsync("/api/social/action",new{operation="Presence",presence="Online",context="Lobby"}));
            string account;
            await using(var c=await db.OpenConnectionAsync(default)){var rows=await CommunityEndpoints.Rows(c,null,"SELECT account_id FROM users WHERE id=@u",default,("u",people[2].Id));account=(string)rows[0]["account_id"]!;}
            await Expect(host.PostAsJsonAsync("/api/social/action",new{operation="Invite",target=account}),HttpStatusCode.BadRequest,"초대도 경기 3명·관전 2명 정원을 초과하지 않음");
            await Read(people[4].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            await Read(host.PostAsJsonAsync("/api/social/action",new{operation="Invite",target=account}));
            string invitation;
            await using(var c=await db.OpenConnectionAsync(default)){var rows=await CommunityEndpoints.Rows(c,null,"SELECT CAST(id AS CHAR) id FROM social_room_invites WHERE recipient_id=@u",default,("u",people[2].Id));invitation=(string)rows[0]["id"]!;}
            await Read(people[2].Client.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=invitation}));
            var invited=(await Read(people[2].Client.GetAsync("/api/rooms/current"))).GetProperty("room");
            Check(invited.GetProperty("isObserver").GetBoolean(),"경기 정원이 찬 방의 친구 초대를 수락하면 빈 관전 자리로만 입장");
            await Read(people[2].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            for(int i=1;i<5;i++)if(i!=2&&i!=4)await Read(people[i].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            await Read(host.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=3}));
            await Read(host.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=5}));
            string fixture=Path.Combine(output,"ObserverFixture",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(fixture);string executable=Path.Combine(fixture,"ProjectDFSDServer.exe");await File.WriteAllTextAsync(executable,"FakeFactory only");
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=true,executable,arguments="MultiplayTest -log",firstPort=24977,maximumConcurrent=1,projectRoot=fixture}));
            room=(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room");Check(room.GetProperty("canStart").GetBoolean(),"관전 방장은 준비 면제이며 봇은 자동 준비로 시작 가능");
            await Expect(host.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}),HttpStatusCode.BadRequest,"관전 중인 방장도 준비 대신 시작 버튼 사용");
            await Read(people[5].Client.PostAsJsonAsync("/api/rooms/"+code+"/join",new{}));
            Check(!(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("canStart").GetBoolean(),"다른 관전자가 미준비면 관전 방장 시작 비활성화");
            await Expect(host.PostAsJsonAsync("/api/rooms/current/start",new{}),HttpStatusCode.Conflict,"미준비 관전자를 무시하고 직접 시작 요청 차단");
            await Read(people[5].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));
            Check((await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("canStart").GetBoolean(),"관전자 포함 전원 준비 후 관전 방장의 시작 활성화");
            await Read(people[5].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=false}));Check(!(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("canStart").GetBoolean(),"관전자의 준비 취소도 시작 비활성화");
            await Read(people[5].Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));
            string id=room.GetProperty("roomId").GetString()!;await Read(host.PostAsJsonAsync("/api/rooms/current/start",new{}));await Sql("UPDATE game_rooms SET started_at_utc=UTC_TIMESTAMP(6)-INTERVAL 6 SECOND WHERE id=@r",("r",id));
            await Read(host.PostAsJsonAsync("/api/rooms/current/confirm-start",new{}));await manager.TickAsync(default);var process=factory.Started.Last();server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Port",process.Port.ToString());
            await Read(server.PostAsJsonAsync("/api/server/heartbeat",new{port=process.Port,bootId="obs-"+process.Instance,managedId=process.Instance,ready=true}));await manager.TickAsync(default);
            room=(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room");Check(room.GetProperty("started").GetBoolean()&&room.GetProperty("assignedRole").GetString()=="Observer","권위 서버 준비 이후 관전 역할로만 확정");
            var ticket=await Read(host.PostAsJsonAsync("/api/game-tickets",new{}));var claim=await Read(server.PostAsJsonAsync("/api/server/game-tickets/consume",new{ticket=ticket.GetProperty("ticket").GetString()}));
            Check(claim.GetProperty("role").GetString()=="Observer"&&claim.GetProperty("bots").GetArrayLength()==3,"관전도 일회용 티켓 검증을 거치고 봇 3명 스냅샷 유지");
            await Expect(server.PostAsJsonAsync("/api/server/matches/results",new{matchId=room.GetProperty("matchId").GetString(),userId=people[0].Id.ToString(),role="Survivor",success=false}),HttpStatusCode.Forbidden,"관전자를 경기 결과·레이팅 참가자로 위조할 수 없음");
            Console.WriteLine($"PASS: {checks} observer HTTP/DB checks; native spectator camera not covered here");
        }
        finally
        {
            configuration["Bots:EnableOpenObservers"]=oldObservers;configuration["Bots:EnableDedicatedMatches"]=oldBots;
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            foreach(var person in people){await Sql("DELETE FROM game_rooms WHERE host_user_id=@u;DELETE FROM users WHERE id=@u",("u",person.Id));person.Client.Dispose();}
        }
    }
}
