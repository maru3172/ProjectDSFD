using MySqlConnector;

internal static class StartupRecoveryChecks
{
    internal static async Task RunAsync(AuthDatabase database,GameServerDemandManager manager,Action<bool,string> check)
    {
        var users=new List<(ulong Id,string Account,string Token)>();var rooms=new List<string>();
        string[] phases={"Waiting","Starting","Preparing","Started","Queued"};
        for(int index=0;index<phases.Length;index++)
        {
            var user=await OperationsIntegration.Player(database);users.Add(user);
            string room=Guid.NewGuid().ToString("D");rooms.Add(room);
            await using var connection=await database.OpenConnectionAsync(default);
            await using var command=new MySqlCommand("INSERT INTO game_rooms(id,join_code,name,host_user_id,status) VALUES(@room,@code,'restart fixture',@user,@status); INSERT INTO room_members(room_id,user_id) VALUES(@room,@user);",connection);
            command.Parameters.AddWithValue("@room",room);command.Parameters.AddWithValue("@code",Guid.NewGuid().ToString("N")[..8]);
            command.Parameters.AddWithValue("@user",user.Id);command.Parameters.AddWithValue("@status",phases[index]);await command.ExecuteNonQueryAsync();
        }
        await using(var connection=await database.OpenConnectionAsync(default))
        {
            await GameServerSlots.HeartbeatAsync(connection,new(25577,"old-backend-boot"),default);
            await using var assign=new MySqlCommand("UPDATE game_server_slots SET assigned_room_id=@room,assigned_match_id=@match,reported_match_id=@match,has_started=TRUE WHERE port=25577;",connection);
            assign.Parameters.AddWithValue("@room",rooms[3]);assign.Parameters.AddWithValue("@match",Guid.NewGuid().ToString("D"));await assign.ExecuteNonQueryAsync();
        }
        try
        {
            await manager.ReconcileStartupAsync(default);
            await using var connection=await database.OpenConnectionAsync(default);
            for(int index=0;index<rooms.Count;index++)
            {
                await using var room=new MySqlCommand("SELECT COUNT(*) FROM game_rooms WHERE id=@id;",connection);room.Parameters.AddWithValue("@id",rooms[index]);
                check(Convert.ToInt32(await room.ExecuteScalarAsync())==(index==0?1:0),"F17 시작 복구의 방 처리: "+phases[index]);
                await using var session=new MySqlCommand("SELECT revoked_at_utc IS NOT NULL FROM auth_sessions WHERE user_id=@id;",connection);session.Parameters.AddWithValue("@id",users[index].Id);
                check(Convert.ToBoolean(await session.ExecuteScalarAsync())==(index!=0),"F17 고아 경기 토큰만 폐기·대기방 토큰은 유지: "+phases[index]);
            }
            await using(var slot=new MySqlCommand("SELECT ready=FALSE AND assigned_room_id IS NULL AND assigned_match_id IS NULL AND reported_match_id IS NULL AND has_started=FALSE FROM game_server_slots WHERE port=25577;",connection))
                check(Convert.ToBoolean(await slot.ExecuteScalarAsync()),"F17 이전 서버 예약·매치·준비 상태 전부 초기화");
            await manager.ReconcileStartupAsync(default);
            await using(var room=new MySqlCommand("SELECT COUNT(*) FROM game_rooms WHERE id=@id;",connection))
            {room.Parameters.AddWithValue("@id",rooms[0]);check(Convert.ToInt32(await room.ExecuteScalarAsync())==1,"F17 복구를 반복해도 정상 대기방은 보존");}
        }
        finally
        {
            foreach(string room in rooms)await OperationsIntegration.Execute(database,$"DELETE FROM game_rooms WHERE id='{room}';");
            await OperationsIntegration.Execute(database,"DELETE FROM game_server_slots WHERE port=25577;");
        }
    }
}
