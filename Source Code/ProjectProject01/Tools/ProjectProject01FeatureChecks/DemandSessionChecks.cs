using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class DemandSessionChecks
{
    internal sealed class FakeClock : TimeProvider
    {private DateTimeOffset now=DateTimeOffset.UtcNow;public override DateTimeOffset GetUtcNow()=>now;public void Advance(int seconds)=>now=now.AddSeconds(seconds);}
    internal sealed class FakeProcess : IManagedGameProcess
    {public bool HasExited {get;set;} public bool Stopped,StopRequested; public void RequestStop(){StopRequested=true;}public void Stop(){RequestStop();Stopped=true;HasExited=true;}public void Dispose(){} }
    internal sealed class FakeFactory : IGameServerProcessFactory
    {
        internal readonly List<(int Port,string Instance,FakeProcess Process)> Started=[];
        internal bool FailNext;
        public bool IsPortAvailable(int port)=>!Started.Any(p=>p.Port==port&&!p.Process.HasExited);
        public IManagedGameProcess Start(GameServerDemandConfiguration configuration,int port,string instanceId,string secret)
        {
            if(FailNext){FailNext=false;throw new InvalidOperationException("test simulated process failure");}
            var process=new FakeProcess();Started.Add((port,instanceId,process));return process;
        }
        internal int Live=>Started.Count(p=>!p.Process.HasExited);
    }
    private static int checks;
    private static void Check(bool value,string text){if(!value)throw new Exception(text);checks++;Console.WriteLine("PASS "+text);}
    private static async Task<JsonElement> Read(Task<HttpResponseMessage> pending)
    {using var response=await pending;string text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"{response.StatusCode} {text}");using var json=JsonDocument.Parse(text);return json.RootElement.Clone();}
    private static async Task Status(Task<HttpResponseMessage> pending,HttpStatusCode code,string text)
    {using var response=await pending;Check(response.StatusCode==code,text+" "+response.StatusCode+" "+(response.StatusCode!=code?await response.Content.ReadAsStringAsync():""));}
    public static async Task RunAsync(AuthDatabase database,HttpClient admin,GameServerDemandManager manager,FakeFactory factory,FakeClock clock,string output)
    {
        // Actual HTTP and the parent's validated isolated MySQL. Never execute the fixture EXE.
        string fixture=Path.Combine(output,"DemandFixture",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(fixture);
        string executable=Path.Combine(fixture,"ProjectDFSDServer.exe");await File.WriteAllTextAsync(executable,"test fixture: FakeFactory only");
        string editor=Path.Combine(fixture,"UnrealEditor-Cmd.exe");await File.WriteAllTextAsync(editor,"test fixture: FakeFactory only");
        await File.WriteAllTextAsync(Path.Combine(fixture,"ProjectDFSD.uproject"),"{}");
        await Status(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=true,executable=editor,projectRoot=fixture}),HttpStatusCode.Conflict,"Normal server mode rejects an editor executable");
        await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=true,executable=editor,projectRoot=fixture,editorLocalTest=true,firstPort=27777}));
        var editorState=await Read(admin.GetAsync("/api/admin/game-server-demand"));
        Check(editorState.GetProperty("editorLocalTest").GetBoolean() && editorState.GetProperty("activeSessions").GetInt32()==0,"Local authenticated editor setup does not prelaunch fake sessions or skip admission");
        await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=false}));
        var clients=new List<HttpClient>();var rooms=new string[3];
        using var server=new HttpClient {BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        async Task<JsonElement> State(int room=0)=>(await Read(clients[room*3].GetAsync("/api/rooms/current"))).GetProperty("room");
        async Task<JsonElement> Start(int room=0)=>(await Read(clients[room*3].PostAsJsonAsync("/api/rooms/current/start",new {}))).GetProperty("room");
        async Task ReadyAgain(int room=0){for(int member=1;member<3;member++)await Read(clients[room*3+member].PostAsJsonAsync("/api/rooms/current/ready",new {ready=true}));}
        Task Age(int room,int seconds)=>OperationsIntegration.Execute(database,$"UPDATE game_rooms SET started_at_utc=UTC_TIMESTAMP(6)-INTERVAL {seconds} SECOND WHERE id='{rooms[room]}';");
        async Task<JsonElement> Queue(int room=0)
        {
            await Start(room);await Age(room,6);
            return (await Read(clients[room*3].PostAsJsonAsync("/api/rooms/current/confirm-start",new {}))).GetProperty("room");
        }
        async Task Cancel(int room=0){await Read(clients[room*3].PostAsJsonAsync("/api/rooms/current/cancel-start",new {}));}
        async Task Heartbeat(bool ready,string? match=null)
        {
            var current=factory.Started.Last();await Read(server.PostAsJsonAsync("/api/server/heartbeat",new {port=current.Port,bootId="boot-"+current.Instance,ready,matchId=match,managedId=current.Instance}));
        }
        Task Configure(int count=1)=>Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=true,executable,arguments="MultiplayTest -log",firstPort=24577,maximumConcurrent=count,projectRoot=fixture}));
        try
        {
            using var anonymous=new HttpClient {BaseAddress=admin.BaseAddress};
            await Status(anonymous.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=false}),HttpStatusCode.Unauthorized,"일반 클라이언트 운영 설정 차단");
            await Status(anonymous.PutAsJsonAsync("/api/admin/game-server-demand/capacity",new {maximumConcurrent=2}),HttpStatusCode.Unauthorized,"일반 클라이언트 한도 변경 차단");
            await Status(admin.PutAsJsonAsync("/api/admin/game-server-demand/capacity",new {maximumConcurrent=0}),HttpStatusCode.BadRequest,"경기 한도 0 거부");
            await Status(admin.PutAsJsonAsync("/api/admin/game-server-demand/capacity",new {maximumConcurrent=101}),HttpStatusCode.BadRequest,"경기 한도 100 초과 거부");
            await Read(admin.PutAsJsonAsync("/api/admin/room-capacity",new {maximumRooms=3}));
            await Status(admin.PutAsJsonAsync("/api/admin/game-server-demand/capacity",new {maximumConcurrent=4}),HttpStatusCode.BadRequest,"방 한도 초과 경기 수 거부");
            var limit=await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand/capacity",new {maximumConcurrent=2}));
            Check(!limit.GetProperty("enabled").GetBoolean()&&factory.Live==0,"한도 적용은 운영·프로세스를 시작하지 않음");
            await Configure();Check(manager.Enabled&&factory.Live==0,"운영 활성화해도 유휴 서버 0개");
            for(int index=0;index<9;index++)
            {
                var player=await OperationsIntegration.Player(database);var client=new HttpClient {BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);clients.Add(client);
            }
            for(int room=0;room<3;room++)
            {
                var created=(await Read(clients[room*3].PostAsJsonAsync("/api/rooms",new {name="대기열 검증 "+room,isPublic=true}))).GetProperty("room");
                rooms[room]=created.GetProperty("roomId").GetString()!;string code=created.GetProperty("joinCode").GetString()!;
                for(int member=1;member<3;member++)await Read(clients[room*3+member].PostAsJsonAsync($"/api/rooms/{code}/join",new {}));
                await ReadyAgain(room);
            }
            var countdown=await Start();await manager.TickAsync(default);
            Check(countdown.GetProperty("starting").GetBoolean()&&!countdown.GetProperty("serverPreparing").GetBoolean()&&factory.Live==0,"시작은 카운트다운 먼저; 아직 세션 생성 안 함");
            await Status(clients[0].PostAsJsonAsync("/api/rooms/current/confirm-start",new {}),HttpStatusCode.Conflict,"5초 전 세션 찾기 진입 차단");
            await Cancel();await manager.TickAsync(default);Check(factory.Live==0&&(await State()).GetProperty("canStart").GetBoolean(),"카운트다운 취소 시 방·로그인 유지");
            var searching=await Queue();
            Check(searching.GetProperty("sessionWaitPhase").GetString()=="Queued"&&searching.GetProperty("sessionWaitElapsedSeconds").GetInt32()>=0,"빈자리 대기 단계와 경과 초를 별도로 제공");
            Check(searching.GetProperty("serverPreparing").GetBoolean()&&!searching.GetProperty("starting").GetBoolean()&&!searching.GetProperty("started").GetBoolean(),"카운트다운 후 세션 찾는 상태로 전환");
            Check(searching.GetProperty("members").EnumerateArray().All(m=>string.IsNullOrEmpty(m.GetProperty("assignedRole").GetString())),"찾는 동안 역할은 배정하지 않음");
            var duplicateQueue=await Read(clients[0].PostAsJsonAsync("/api/rooms/current/confirm-start",new {}));
            Check(duplicateQueue.GetProperty("room").GetProperty("serverPreparing").GetBoolean()&&factory.Live==0,"대기 진입 응답 재시도도 중복 예약·실행 없음");
            await Status(clients[0].PostAsJsonAsync("/api/game-tickets",new {}),HttpStatusCode.Conflict,"준비 전 입장 허가 없음");
            await manager.TickAsync(default);Check(factory.Live==1,"대기열에서 자리를 얻은 방만 서버 실행");
            Check((await State()).GetProperty("sessionWaitPhase").GetString()=="Preparing","서버 실행 단계는 빈자리 대기와 구분");
            await Status(admin.PutAsJsonAsync("/api/admin/game-server-demand/capacity",new {maximumConcurrent=2}),HttpStatusCode.Conflict,"준비 중 한도 변경 거부·기존 세션 보존");
            await Heartbeat(false);await manager.TickAsync(default);Check(!(await State()).GetProperty("started").GetBoolean(),"서버 준비 완료 전 게임 시작 금지");
            await Status(server.PostAsJsonAsync("/api/server/heartbeat",new {port=24577,bootId="stale",ready=true,managedId=Guid.NewGuid().ToString("N")}),HttpStatusCode.Conflict,"다른 세대 heartbeat 거부");
            await Status(server.PostAsJsonAsync("/api/server/heartbeat",new {port=24577,bootId="old",ready=true}),HttpStatusCode.Conflict,"관리 ID 없는 구형 데디 거부");
            await Queue(1);await Queue(2);await manager.TickAsync(default);
            Check(factory.Live==1&&(await State(1)).GetProperty("serverPreparing").GetBoolean()&&(await State(2)).GetProperty("serverPreparing").GetBoolean(),"한도 초과 두 방은 대기; 서버 수 1개 유지");
            await Cancel(1);Check(!(await State(1)).GetProperty("serverPreparing").GetBoolean(),"자리 없는 방도 세션 대기 취소 가능");
            await Queue(1); // Room 2 now precedes room 1.
            await Heartbeat(true);await manager.TickAsync(default);var started=await State();
            Check(started.GetProperty("sessionWaitPhase").GetString()==""&&started.GetProperty("sessionWaitElapsedSeconds").GetInt32()==0,"입장 허가 이후 대기 단계·시간 제거");
            Check(started.GetProperty("started").GetBoolean()&&!started.GetProperty("starting").GetBoolean(),"준비 완료하면 두 번째 카운트다운 없이 실제 시작");
            var members=started.GetProperty("members").EnumerateArray().ToArray();
            Check(members.Count(m=>m.GetProperty("assignedRole").GetString()=="Mannequin")==1&&members.Count(m=>m.GetProperty("assignedRole").GetString()=="Survivor")==2,"실제 시작 시에만 역할 1:2 배정");
            string roleJson=started.GetProperty("members").GetRawText();
            var repeated=await Read(clients[0].PostAsJsonAsync("/api/rooms/current/confirm-start",new {}));
            Check(repeated.GetProperty("room").GetProperty("members").GetRawText()==roleJson,"중복 확정이 역할을 재배정하지 않음");
            var ticket=await Read(clients[0].PostAsJsonAsync("/api/game-tickets",new {}));string match=ticket.GetProperty("matchId").GetString()!;
            Check(started.GetProperty("matchId").GetString()==match,"방 상태와 접속 티켓의 권위적 MatchId 일치");
            using(var consume=new HttpRequestMessage(HttpMethod.Post,"/api/server/game-tickets/consume") {Content=JsonContent.Create(new {ticket=ticket.GetProperty("ticket").GetString()})})
            {consume.Headers.Add("X-ProjectProject01-Server-Port","24577");await Read(server.SendAsync(consume));Check(true,"준비 완료된 세션에서 티켓 소비");}
            await Heartbeat(false,match);await manager.TickAsync(default);
            await Read(clients[0].PostAsJsonAsync("/api/rooms/current/return",new {}));await manager.TickAsync(default);Check(factory.Live==1,"한 명 복귀로 진행 중 세션을 닫지 않음");
            for(int member=1;member<3;member++)await Read(clients[member].PostAsJsonAsync("/api/rooms/current/return",new {}));
            await manager.TickAsync(default);Check(factory.Live==1,"서버 종료 확인 없이 전원 복귀만으로 세션 종료 불가");
            await Read(server.PostAsJsonAsync("/api/server/matches/complete",new {matchId=match}));
            await manager.TickAsync(default);Check(factory.Live==0&&(await State()).GetProperty("roomId").GetString()==rooms[0],"전원 복귀 후 세션 종료·기존 방 유지");
            string previous=factory.Started.Last().Instance;
            await manager.TickAsync(default);
            var allocated=(await Read(admin.GetAsync("/api/admin/game-server-demand"))).GetProperty("sessions");
            Check(factory.Live==1&&allocated[0].GetProperty("roomId").GetString()==rooms[2],"먼저 기다린 방이 자리 반환 후 자동 세션 준비");
            Check(factory.Started.Last().Instance!=previous,"포트 재사용도 새 세션 ID 사용");
            await Cancel(2);await manager.TickAsync(default);await manager.TickAsync(default);await Cancel(1);await manager.TickAsync(default);
            Check(factory.Live==0,"대기·준비 취소 후 남는 경기 프로세스 없음");
            await ReadyAgain();await Queue();await Age(0,121);await manager.TickAsync(default);
            Check(!(await State()).GetProperty("serverPreparing").GetBoolean()&&factory.Live==0,"자리 대기 120초 초과 시 복구·뒤늦은 실행 없음");
            await ReadyAgain();await Queue();
            await Read(clients[1].PostAsJsonAsync("/api/rooms/current/leave",new {}));await manager.TickAsync(default);
            Check(!(await State()).GetProperty("serverPreparing").GetBoolean()&&factory.Live==0,"대기 중 인원 이탈은 대기열 취소·뒤늦은 실행 없음");
            string rejoinCode=(await State()).GetProperty("joinCode").GetString()!;
            await Read(clients[1].PostAsJsonAsync($"/api/rooms/{rejoinCode}/join",new {}));
            await ReadyAgain();await Queue();
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=false}));await manager.TickAsync(default);
            Check(!(await State()).GetProperty("serverPreparing").GetBoolean()&&factory.Live==0,"서버 운영 중지 시 자리 대기 방도 복구");
            await Configure();
            await ReadyAgain();await Queue();await manager.TickAsync(default);clock.Advance(91);await manager.TickAsync(default);
            Check(factory.Live==0&&!(await State()).GetProperty("serverPreparing").GetBoolean(),"세션 준비 90초 초과 시 취소·정리");
            await ReadyAgain();await Start();await Age(0,31);await manager.TickAsync(default);
            Check(!(await State()).GetProperty("starting").GetBoolean()&&factory.Live==0,"확정 응답 없는 카운트다운도 30초 후 해제");
            await ReadyAgain();factory.FailNext=true;await Queue();await manager.TickAsync(default);
            Check(factory.Live==0&&(await State()).GetProperty("roomId").GetString()==rooms[0],"실행 실패 시 무한 재시도 없이 방 유지");
            await ReadyAgain();await Queue();await manager.TickAsync(default);factory.Started.Last().Process.HasExited=true;await manager.TickAsync(default);
            Check(!(await State()).GetProperty("serverPreparing").GetBoolean(),"준비 중 프로세스 크래시 복구");
            await ReadyAgain();await Queue();await manager.TickAsync(default);await Heartbeat(true);await manager.TickAsync(default);clock.Advance(151);await manager.TickAsync(default);
            Check(!(await State()).GetProperty("started").GetBoolean()&&factory.Live==0,"입장하지 않는 확정 세션도 시간 초과 정리");
            await ReadyAgain();await Queue();await manager.TickAsync(default);
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=false}));await manager.TickAsync(default);
            Check(!manager.Enabled&&factory.Live==0&&!(await State()).GetProperty("gameServerOnline").GetBoolean(),"운영 중지 시 준비 세션 정리·재활성화 차단");
            await Status(clients[0].PostAsJsonAsync("/api/rooms/current/start",new {}),HttpStatusCode.Conflict,"운영 중지 뒤 새 시작 안전 거부");
            await Configure(2);await ReadyAgain();await ReadyAgain(1);
            await Task.WhenAll(Queue(),Queue(1));await manager.TickAsync(default);
            Check(factory.Live==2,"동시 한도 2개까지 독립 프로세스 생성");
            foreach(var running in factory.Started.Where(p=>!p.Process.HasExited))await Read(server.PostAsJsonAsync("/api/server/heartbeat",new {port=running.Port,bootId="boot-"+running.Instance,ready=true,managedId=running.Instance}));
            await manager.TickAsync(default);var one=await State();var other=await State(1);
            Check(one.GetProperty("started").GetBoolean()&&other.GetProperty("started").GetBoolean()&&one.GetProperty("travelUrl").GetString()!=other.GetProperty("travelUrl").GetString(),"두 방의 실제 시작·접속 포트 분리");
            foreach(var running in factory.Started.Where(p=>!p.Process.HasExited))
            {
                await OperationsIntegration.Execute(database,$"UPDATE game_server_slots SET ready=FALSE,reported_match_id=assigned_match_id WHERE port={running.Port};");
            }
            await manager.TickAsync(default);
            await OperationsIntegration.Execute(database,"UPDATE game_server_slots SET last_heartbeat_utc=UTC_TIMESTAMP()-INTERVAL 45 SECOND;");
            clock.Advance(29);await manager.TickAsync(default);Check(factory.Live==2,"진행 경기 heartbeat 유예 중 정상 프로세스 보존");
            clock.Advance(2);await manager.TickAsync(default);Check(factory.Live==0&&!(await State()).GetProperty("started").GetBoolean(),"살아 있어도 heartbeat 멈춘 경기 회수·방 복구");
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=false}));await manager.TickAsync(default);
            await Configure(2);var changed=await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand/capacity",new {maximumConcurrent=1}));
            Check(changed.GetProperty("enabled").GetBoolean()&&changed.GetProperty("maximumConcurrent").GetInt32()==1&&factory.Live==0,"유휴 중 한도 적용은 운영 상태 유지");
            Console.WriteLine($"PASS: {checks} queued/on-demand checks (fake UE processes; real HTTP and isolated MySQL)");
        }
        finally
        {
            await manager.ConfigureAsync(new(false),default);await manager.TickAsync(default);
            foreach(var client in clients)client.Dispose();
            await OperationsIntegration.Execute(database,"DELETE FROM game_rooms; DELETE FROM game_server_slots;");
        }
    }
}
