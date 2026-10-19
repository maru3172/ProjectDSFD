using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class DrainSessionChecks
{
    private static int passed;
    private static void Check(bool condition,string text){if(!condition)throw new Exception(text);passed++;Console.WriteLine("PASS "+text);}
    private static async Task<JsonElement> Read(Task<HttpResponseMessage> task)
    {using var response=await task;string text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception(text);using var json=JsonDocument.Parse(text);return json.RootElement.Clone();}
    internal static async Task RunAsync(AuthDatabase database,HttpClient admin,GameServerDemandManager manager,DemandSessionChecks.FakeFactory factory,string output)
    {
        string fixture=Path.Combine(output,"DrainFixture",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(fixture);
        string exe=Path.Combine(fixture,"ProjectProject01Server.exe");await File.WriteAllTextAsync(exe,"fixture only; never executed");
        var clients=new List<HttpClient>();string[] roomIds=new string[2];
        async Task<JsonElement> State(int room=0)=>(await Read(clients[room*3].GetAsync("/api/rooms/current"))).GetProperty("room");
        async Task Queue(int room)
        {
            await Read(clients[room*3].PostAsJsonAsync("/api/rooms/current/start",new {}));
            await OperationsIntegration.Execute(database,$"UPDATE game_rooms SET started_at_utc=UTC_TIMESTAMP(6)-INTERVAL 6 SECOND WHERE id='{roomIds[room]}';");
            await Read(clients[room*3].PostAsJsonAsync("/api/rooms/current/confirm-start",new {}));
        }
        async Task<JsonElement> Drain(bool cancel=false)=>await Read(admin.PostAsJsonAsync("/api/admin/game-server-demand/drain",new {cancel}));
        using var server=new HttpClient {BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        async Task Heartbeat(bool ready,string? match=null)
        {
            var current=factory.Started.Last();await Read(server.PostAsJsonAsync("/api/server/heartbeat",new {port=current.Port,bootId="drain-"+current.Instance,ready,matchId=match,managedId=current.Instance}));
        }
        try
        {
            using var anonymous=new HttpClient {BaseAddress=admin.BaseAddress};using(var response=await anonymous.PostAsJsonAsync("/api/admin/game-server-demand/drain",new {}))
                Check(response.StatusCode==HttpStatusCode.Unauthorized,"일반 클라이언트 안전 종료 조작 차단");
            await Read(admin.PutAsJsonAsync("/api/admin/room-capacity",new {maximumRooms=3}));
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=true,executable=exe,firstPort=26577,maximumConcurrent=1,projectRoot=fixture}));
            for(int index=0;index<6;index++)
            {
                var player=await OperationsIntegration.Player(database);var client=new HttpClient {BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);clients.Add(client);
            }
            for(int room=0;room<2;room++)
            {
                var created=(await Read(clients[room*3].PostAsJsonAsync("/api/rooms",new {name="안전 종료 "+room,isPublic=true}))).GetProperty("room");
                roomIds[room]=created.GetProperty("roomId").GetString()!;string code=created.GetProperty("joinCode").GetString()!;
                for(int member=1;member<3;member++)
                {
                    await Read(clients[room*3+member].PostAsJsonAsync($"/api/rooms/{code}/join",new {}));
                    await Read(clients[room*3+member].PostAsJsonAsync("/api/rooms/current/ready",new {ready=true}));
                }
            }
            await Queue(0);await manager.TickAsync(default);await Heartbeat(true);await manager.TickAsync(default);
            Check((await State()).GetProperty("started").GetBoolean()&&factory.Live==1,"안전 종료 검사 전 실제 시작 상태 준비");
            await Queue(1);var requested=await Drain();
            Check(requested.GetProperty("draining").GetBoolean()&&manager.Enabled&&!manager.AcceptingNewMatches,"종료 대기는 기존 운영 유지·신규 시작만 즉시 차단");
            var ticket=await Read(clients[0].PostAsJsonAsync("/api/game-tickets",new {}));
            string match=ticket.GetProperty("matchId").GetString()!;
            Check(ticket.GetProperty("ticket").GetString()!.Length>0,"종료 대기 중에도 이미 시작된 경기의 입장 허가 유지");
            using(var consume=new HttpRequestMessage(HttpMethod.Post,"/api/server/game-tickets/consume"){Content=JsonContent.Create(new {ticket=ticket.GetProperty("ticket").GetString()})})
            {consume.Headers.Add("X-ProjectProject01-Server-Port","26577");await Read(server.SendAsync(consume));}
            await Heartbeat(false,match);await manager.TickAsync(default);
            Check((await State()).GetProperty("started").GetBoolean()&&factory.Live==1,"종료 대기 시작이 진행 경기를 끊지 않음");
            Check(!(await State(1)).GetProperty("serverPreparing").GetBoolean(),"종료 대기 시작 시 자리 대기 방은 안전하게 복구");
            using(var response=await clients[3].PostAsJsonAsync("/api/rooms/current/start",new {}))Check(response.StatusCode==HttpStatusCode.Conflict,"종료 대기 중 신규 시작 거부");
            await Drain(true);Check(manager.AcceptingNewMatches&&factory.Live==1,"종료 대기 취소는 현재 경기 유지·신규 시작 다시 허용");
            await Drain();await Read(clients[0].PostAsJsonAsync("/api/rooms/current/return",new {}));await manager.TickAsync(default);
            Check(manager.Enabled&&factory.Live==1,"한 명 복귀만으로 안전 종료 완료하지 않음");
            for(int member=1;member<3;member++)await Read(clients[member].PostAsJsonAsync("/api/rooms/current/return",new {}));
            await Read(server.PostAsJsonAsync("/api/server/matches/complete",new {matchId=match}));
            await manager.TickAsync(default);var done=await Read(admin.GetAsync("/api/admin/game-server-demand"));
            Check(!manager.Enabled&&factory.Live==0&&done.GetProperty("drainCompleted").GetBoolean(),"모든 경기 세션 정리 후에만 운영 중지 완료");
            Check((await State()).GetProperty("roomId").GetString()==roomIds[0],"안전 종료 뒤 로그인과 기존 대기방 유지");
            using(var response=await admin.PostAsJsonAsync("/api/admin/game-server-demand/drain",new {cancel=true}))Check(response.StatusCode==HttpStatusCode.Conflict,"이미 완료된 종료 취소가 운영을 무단 재시작하지 않음");
            await manager.TickAsync(default);Check(factory.Live==0&&!manager.AcceptingNewMatches,"안전 종료 완료 뒤 뒤늦은 세션 실행 없음");
            await Drain();Check(!manager.Enabled,"중지 상태 안전 종료 요청도 서버를 시작하지 않음");
            Console.WriteLine($"PASS: {passed} graceful drain checks (fake UE processes; isolated HTTP/MySQL)");
        }
        finally
        {
            await manager.ConfigureAsync(new(false),default);await manager.TickAsync(default);
            foreach(var client in clients)client.Dispose();
            await OperationsIntegration.Execute(database,"DELETE FROM game_rooms; DELETE FROM game_server_slots;");
        }
    }
}
