using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MySqlConnector;

internal static class OperationsIntegration
{
    private static int passed;
    private static void Check(bool condition,string name){if(!condition)throw new Exception(name);passed++;Console.WriteLine("PASS "+name);}
    private static async Task<JsonElement> Read(HttpResponseMessage response)
    {using(response){string body=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception(body);return JsonDocument.Parse(body).RootElement.Clone();}}
    private static async Task Status(Task<HttpResponseMessage> pending,HttpStatusCode expected,string name)
    {using var response=await pending;Check(response.StatusCode==expected,name+": "+(int)response.StatusCode+" "+await response.Content.ReadAsStringAsync());}
    internal static async Task Execute(AuthDatabase database,string sql)
    {await using var connection=await database.OpenConnectionAsync(default);await using var command=new MySqlCommand(sql,connection);await command.ExecuteNonQueryAsync();}
    internal static async Task<(ulong Id,string Account,string Token)> Player(AuthDatabase database)
    {
        string account=Guid.NewGuid().ToString("N")[..16],token=Guid.NewGuid().ToString("N");
        await using var connection=await database.OpenConnectionAsync(default);
        await using var command=new MySqlCommand("INSERT INTO users(account_id,display_name,password_hash) VALUES(@account,'운영 검증','test');",connection);
        command.Parameters.AddWithValue("@account",account);await command.ExecuteNonQueryAsync();ulong id=(ulong)command.LastInsertedId;
        command.CommandText="INSERT INTO auth_sessions(user_id,access_token_hash,refresh_token_hash,access_expires_at_utc,refresh_expires_at_utc) VALUES(@id,@hash,@hash,UTC_TIMESTAMP()+INTERVAL 1 HOUR,UTC_TIMESTAMP()+INTERVAL 1 DAY);";
        command.Parameters.AddWithValue("@id",id);command.Parameters.AddWithValue("@hash",SHA256.HashData(Encoding.UTF8.GetBytes(token)));await command.ExecuteNonQueryAsync();
        return(id,account,token);
    }
    public static async Task RunAsync(AuthDatabase database,HttpClient admin)
    {
        // Caller verifies port 23307 AND the isolated data directory before reaching this routine.
        await Execute(database,"DELETE FROM game_rooms; DELETE FROM game_server_slots;");
        using var anonymous=new HttpClient {BaseAddress=admin.BaseAddress};
        await Status(anonymous.PutAsJsonAsync("/api/admin/room-capacity",new {maximumRooms=100}),HttpStatusCode.Unauthorized,"일반 클라이언트 방 한도 변경 차단");
        await Status(admin.PutAsJsonAsync("/api/admin/room-capacity",new {maximumRooms=0}),HttpStatusCode.BadRequest,"방 한도 하한 검증");
        await Status(admin.PutAsJsonAsync("/api/admin/room-capacity",new {maximumRooms=101}),HttpStatusCode.BadRequest,"방 한도 상한 검증");
        await Read(await admin.PutAsJsonAsync("/api/admin/room-capacity",new {maximumRooms=1}));
        var first=await Player(database);var second=await Player(database);
        using var player1=new HttpClient {BaseAddress=admin.BaseAddress};player1.DefaultRequestHeaders.Authorization=new("Bearer",first.Token);
        using var player2=new HttpClient {BaseAddress=admin.BaseAddress};player2.DefaultRequestHeaders.Authorization=new("Bearer",second.Token);
        await Status(player1.PostAsJsonAsync("/api/rooms",new {name="첫 번째 방",isPublic=true}),HttpStatusCode.OK,"한도 내 방 생성");
        await Execute(database,"UPDATE game_rooms SET status='Started';");
        await Status(player2.PostAsJsonAsync("/api/rooms",new {name="두 번째 방",isPublic=true}),HttpStatusCode.Conflict,"경기 중인 방도 최대 방 수에 포함");
        await Read(await admin.PutAsJsonAsync("/api/admin/room-capacity",new {maximumRooms=2}));
        await Status(player2.PostAsJsonAsync("/api/rooms",new {name="두 번째 방",isPublic=true}),HttpStatusCode.OK,"한도 확대 즉시 적용");
        await Read(await admin.PutAsJsonAsync("/api/admin/room-capacity",new {maximumRooms=1}));
        var capacity=await Read(await admin.GetAsync("/api/admin/room-capacity"));
        Check(capacity.GetProperty("maximumRooms").GetInt32()==1&&capacity.GetProperty("currentRooms").GetInt32()==2,"한도 축소 시 기존 방 보존");
        await using(var connection=await database.OpenConnectionAsync(default))
        await using(var command=new MySqlCommand("SELECT max_rooms FROM lobby_state WHERE id=1",connection))
            Check(Convert.ToInt32(await command.ExecuteScalarAsync())==1,"방 한도 DB 영속 저장");

        string restriction=$"/api/admin/accounts/{first.Account}/restriction";
        await Status(anonymous.PostAsJsonAsync(restriction,new {reason="금지",durationMinutes=60}),HttpStatusCode.Unauthorized,"이용 제한 관리자 인증");
        await Status(admin.PostAsJsonAsync(restriction,new {reason="",durationMinutes=60}),HttpStatusCode.BadRequest,"빈 이용 제한 사유 차단");
        await Status(admin.PostAsJsonAsync(restriction,new {reason="반복 악용",durationMinutes=60}),HttpStatusCode.OK,"계정 기간 제한");
        await Status(player1.GetAsync("/api/rooms/current"),HttpStatusCode.Unauthorized,"이용 제한 시 기존 토큰 즉시 폐기");
        await using(var connection=await database.OpenConnectionAsync(default))
            Check((await AccountRestrictions.GetMessageAsync(connection,null,first.Id,default))?.Contains("반복 악용")==true,"이용 제한 사유 안내");
        await Status(admin.DeleteAsync(restriction),HttpStatusCode.OK,"관리자 이용 제한 해제");
        await using(var connection=await database.OpenConnectionAsync(default))
            Check(await AccountRestrictions.GetMessageAsync(connection,null,first.Id,default)==null,"해제 후 로그인 제한 해제");
        await Status(player1.GetAsync("/api/rooms/current"),HttpStatusCode.Unauthorized,"해제 후 옛 토큰 재활성화 금지");
        await Status(admin.PostAsJsonAsync(restriction,new {reason="영구 제한"}),HttpStatusCode.OK,"계정 영구 제한");
        var history=await Read(await admin.GetAsync("/api/admin/account-restrictions"));
        Check(history.GetProperty("restrictions").EnumerateArray().Count(r=>r.GetProperty("accountId").GetString()==first.Account)==2,"제한·해제 이력 보존");
        await Execute(database,$"UPDATE account_restrictions SET expires_at_utc=UTC_TIMESTAMP()-INTERVAL 1 SECOND WHERE user_id={first.Id};");
        await using(var connection=await database.OpenConnectionAsync(default))
            Check(await AccountRestrictions.GetMessageAsync(connection,null,first.Id,default)==null,"기간 만료 자동 해제");

        await Execute(database,"DELETE FROM game_rooms; DELETE FROM game_server_slots;");
        var rooms=new List<string>();
        for(int i=0;i<3;i++)
        {
            string room=Guid.NewGuid().ToString("D");rooms.Add(room);
            string code=Guid.NewGuid().ToString("N")[..8];
            await Execute(database,$"INSERT INTO game_rooms(id,join_code,name,host_user_id) VALUES('{room}','{code}','슬롯 테스트',{second.Id});");
        }
        await using var slotConnection=await database.OpenConnectionAsync(default);
        await GameServerSlots.HeartbeatAsync(slotConnection,new(24477,"boot-a"),default);
        await GameServerSlots.HeartbeatAsync(slotConnection,new(24478,"boot-b"),default);
        async Task<int?> Reserve(string room)
        {await using var connection=await database.OpenConnectionAsync(default);await using var tx=await connection.BeginTransactionAsync();int? port=await GameServerSlots.ReserveAsync(connection,tx,room,default);await tx.CommitAsync();return port;}
        int?[] ports=await Task.WhenAll(Reserve(rooms[0]),Reserve(rooms[1]));
        Check(ports.All(p=>p.HasValue)&&ports.Distinct().Count()==2,"동시 방 배정 시 서버 포트 분리");
        Check(await Reserve(rooms[2])==null,"서버 부족 시 다른 경기 서버 공유 금지");
        string match=Guid.NewGuid().ToString("D");
        await Execute(database,$"UPDATE game_rooms SET status='Started',match_id='{match}' WHERE id='{rooms[0]}';");
        await using(var tx=await slotConnection.BeginTransactionAsync())
        {
            Check(await GameServerSlots.ConfirmAsync(slotConnection,tx,rooms[0],match,default),"예약 서버에 매치 확정");
            Check(await GameServerSlots.IsAssignedAsync(slotConnection,tx,rooms[0],match,ports[0],default),"올바른 방·매치·포트 티켓 허용");
            Check(!await GameServerSlots.IsAssignedAsync(slotConnection,tx,rooms[0],match,ports[1],default),"다른 경기 서버의 티켓 사용 차단");await tx.CommitAsync();
        }
        string boot=ports[0]==24477?"boot-a":"boot-b";
        await GameServerSlots.HeartbeatAsync(slotConnection,new(ports[0]!.Value,boot,false,match),default);
        await GameServerSlots.HeartbeatAsync(slotConnection,new(ports[0]!.Value,boot,true),default);
        Check(await Reserve(rooms[2])==ports[0],"경기 종료 후 빈 서버 재사용");
        await GameServerSlots.HeartbeatAsync(slotConnection,new(ports[1]!.Value,"restarted"),default);
        Check(await Reserve(rooms[0])==ports[1],"서버 재시작 시 이전 예약 안전 초기화");
        await Execute(database,"UPDATE game_server_slots SET last_heartbeat_utc=UTC_TIMESTAMP()-INTERVAL 11 SECOND,assigned_room_id=NULL;");
        Check(await Reserve(rooms[1])==null,"heartbeat 만료 서버 배정 차단");
        await Execute(database,"DELETE FROM game_rooms; DELETE FROM game_server_slots; UPDATE lobby_state SET max_rooms=30 WHERE id=1;");
        await CheckTwoRoomsAsync(database,admin.BaseAddress!);
        Console.WriteLine($"PASS: {passed} operations integration checks");
    }
    private static async Task CheckTwoRoomsAsync(AuthDatabase database,Uri address)
    {
        var clients=new List<HttpClient>();
        using var server=new HttpClient {BaseAddress=address};
        server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        try
        {
            for(int i=0;i<6;i++)
            {var player=await Player(database);var http=new HttpClient {BaseAddress=address};http.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);clients.Add(http);}
            for(int room=0;room<2;room++)
            {
                var created=await Read(await clients[room*3].PostAsJsonAsync("/api/rooms",new {name=$"동시 경기 {room+1}",isPublic=true}));
                string code=created.GetProperty("room").GetProperty("joinCode").GetString()!;
                for(int member=1;member<3;member++)
                {
                    await Read(await clients[room*3+member].PostAsJsonAsync($"/api/rooms/{code}/join",new {}));
                    await Read(await clients[room*3+member].PostAsJsonAsync("/api/rooms/current/ready",new {ready=true}));
                }
            }
            // Simulation is explicitly confined to this isolated API/DB test, never the production smoke runner.
            foreach(int port in new[]{24477,24478})await Read(await server.PostAsJsonAsync("/api/server/heartbeat",new {port,bootId="api-"+port,ready=true}));
            await Read(await clients[0].PostAsJsonAsync("/api/rooms/current/start",new {}));
            await Read(await clients[0].PostAsJsonAsync("/api/rooms/current/cancel-start",new {}));
            Check(true,"카운트다운 취소 후 서버 예약 해제");
            var starts=await Task.WhenAll(Read(await clients[0].PostAsJsonAsync("/api/rooms/current/start",new {})),Read(await clients[3].PostAsJsonAsync("/api/rooms/current/start",new {})));
            int[] ports=starts.Select(s=>new Uri("http://"+s.GetProperty("room").GetProperty("travelUrl").GetString()).Port).ToArray();
            Check(ports.Distinct().Count()==2,"실제 시작 API에서 두 방의 travel URL 포트 분리");
            await Task.Delay(5300);
            string[] matches=new string[2];
            for(int room=0;room<2;room++)
            {
                var confirmed=await Read(await clients[room*3].PostAsJsonAsync("/api/rooms/current/confirm-start",new {}));
                var members=confirmed.GetProperty("room").GetProperty("members").EnumerateArray().ToArray();
                Check(members.Count(m=>m.GetProperty("assignedRole").GetString()=="Mannequin")==1&&members.Count(m=>m.GetProperty("assignedRole").GetString()=="Survivor")==2,$"방 {room+1} 역할 1:2 배정");
                for(int member=0;member<3;member++)
                {
                    var ticket=await Read(await clients[room*3+member].PostAsJsonAsync("/api/game-tickets",new {}));
                    string match=ticket.GetProperty("matchId").GetString()!;
                    if(member==0)matches[room]=match;
                    Check(match==matches[room],$"방 {room+1} 참가자 {member+1} MatchId 일치");
                    async Task<HttpResponseMessage> Consume(int port)
                    {
                        using var request=new HttpRequestMessage(HttpMethod.Post,"/api/server/game-tickets/consume") {Content=JsonContent.Create(new {ticket=ticket.GetProperty("ticket").GetString()})};
                        request.Headers.Add("X-ProjectProject01-Server-Port",port.ToString());return await server.SendAsync(request);
                    }
                    await Status(Consume(ports[1-room]),HttpStatusCode.Conflict,"다른 방 서버의 실제 티켓 소비 차단");
                    var consumed=await Read(await Consume(ports[room]));
                    Check(consumed.GetProperty("matchId").GetString()==match,"배정된 서버만 실제 티켓 소비 성공");
                }
            }
            Check(matches[0]!=matches[1],"동시 경기 MatchId 완전 분리");
            var revokedTicket=await Read(await clients[0].PostAsJsonAsync("/api/game-tickets",new{}));
            await using(var c=await database.OpenConnectionAsync(default))
            {
                var tokenHash=SHA256.HashData(Encoding.UTF8.GetBytes(clients[0].DefaultRequestHeaders.Authorization!.Parameter!));
                await using var revoke=new MySqlCommand("UPDATE auth_sessions SET revoked_at_utc=UTC_TIMESTAMP(6) WHERE access_token_hash=@hash",c);revoke.Parameters.AddWithValue("@hash",tokenHash);await revoke.ExecuteNonQueryAsync();
            }
            await Status(server.PostAsJsonAsync("/api/server/game-tickets/consume",new{ticket=revokedTicket.GetProperty("ticket").GetString()}),HttpStatusCode.Unauthorized,"세션 폐기 후 미사용 티켓 소비 차단");
            var departing=await Read(await clients[1].PostAsJsonAsync("/api/game-tickets",new{}));
            await Read(await clients[1].PostAsJsonAsync("/api/rooms/current/leave",new{}));
            await Status(server.PostAsJsonAsync("/api/server/game-tickets/consume",new{ticket=departing.GetProperty("ticket").GetString()}),HttpStatusCode.Unauthorized,"퇴장과 티켓 폐기가 같은 트랜잭션으로 적용");
        }
        finally
        {
            foreach(var client in clients)client.Dispose();
            await Execute(database,"DELETE FROM game_rooms; DELETE FROM game_server_slots;");
        }
    }
}
