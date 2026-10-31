using MySqlConnector;

internal sealed record ServerHeartbeat(int Port=7777,string BootId="legacy",bool Ready=true,string? MatchId=null,string? ManagedId=null);
// One host, multiple isolated Unreal processes. Room reservations survive backend restarts.
internal static class GameServerSlots
{
    internal static async Task HeartbeatAsync(MySqlConnection connection,ServerHeartbeat body,CancellationToken ct)
    {
        await using var command=new MySqlCommand("""
            INSERT INTO game_server_slots(port,boot_id,ready,reported_match_id,last_heartbeat_utc)
            VALUES(@port,@boot,@ready,@match,UTC_TIMESTAMP(6))
            ON DUPLICATE KEY UPDATE
              assigned_room_id=IF(boot_id<>@boot,NULL,assigned_room_id),
              assigned_match_id=IF(boot_id<>@boot,NULL,assigned_match_id),
              has_started=IF(boot_id<>@boot,FALSE,has_started),
              boot_id=@boot,ready=@ready,reported_match_id=@match,last_heartbeat_utc=UTC_TIMESTAMP(6);
            UPDATE game_server_slots SET has_started=TRUE
              WHERE port=@port AND assigned_match_id=@match AND @match IS NOT NULL;
            UPDATE game_server_slots SET assigned_room_id=NULL,assigned_match_id=NULL,has_started=FALSE
              WHERE port=@port AND ready=TRUE AND (has_started=TRUE OR assigned_room_id IS NULL
                OR (assigned_match_id IS NULL AND reserved_at_utc<UTC_TIMESTAMP(6)-INTERVAL 90 SECOND)
                OR (boot_id<>'legacy' AND assigned_match_id IS NOT NULL AND reserved_at_utc<UTC_TIMESTAMP(6)-INTERVAL 120 SECOND AND reported_match_id IS NULL)
                OR (assigned_match_id IS NOT NULL AND EXISTS(SELECT 1 FROM game_rooms r WHERE r.id=assigned_room_id AND r.status='Waiting')));
            """,connection);
        command.Parameters.AddWithValue("@port",body.Port);command.Parameters.AddWithValue("@boot",body.BootId);
        command.Parameters.AddWithValue("@ready",body.Ready);command.Parameters.AddWithValue("@match",(object?)body.MatchId??DBNull.Value);
        await command.ExecuteNonQueryAsync(ct);
    }

    internal static async Task<int?> ReserveAsync(MySqlConnection connection,MySqlTransaction transaction,string roomId,CancellationToken ct)
    {
        await using var select=new MySqlCommand("""
            SELECT port FROM game_server_slots
            WHERE last_heartbeat_utc>UTC_TIMESTAMP(6)-INTERVAL 10 SECOND AND ready=TRUE AND assigned_room_id IS NULL
            ORDER BY port LIMIT 1 FOR UPDATE SKIP LOCKED;
            """,connection,transaction);
        object? value=await select.ExecuteScalarAsync(ct);
        if(value is null or DBNull) return null;
        int port=Convert.ToInt32(value);
        await using var update=new MySqlCommand("""
            UPDATE game_server_slots SET assigned_room_id=@room,assigned_match_id=NULL,has_started=FALSE,reserved_at_utc=UTC_TIMESTAMP(6)
            WHERE port=@port;
            """,connection,transaction);
        update.Parameters.AddWithValue("@room",roomId);update.Parameters.AddWithValue("@port",port);
        await update.ExecuteNonQueryAsync(ct);
        return port;
    }

    internal static async Task<bool> ConfirmAsync(MySqlConnection connection,MySqlTransaction transaction,string roomId,string matchId,CancellationToken ct)
    {
        await using var command=new MySqlCommand("""
            UPDATE game_server_slots SET assigned_match_id=@match,reserved_at_utc=UTC_TIMESTAMP(6)
            WHERE assigned_room_id=@room AND last_heartbeat_utc>UTC_TIMESTAMP(6)-INTERVAL 10 SECOND AND ready=TRUE;
            """,connection,transaction);
        command.Parameters.AddWithValue("@room",roomId);command.Parameters.AddWithValue("@match",matchId);
        return await command.ExecuteNonQueryAsync(ct)==1;
    }

    internal static async Task<bool> IsAssignedAsync(MySqlConnection connection,MySqlTransaction transaction,string roomId,string matchId,int? port,CancellationToken ct)
    {
        await using var command=new MySqlCommand("""
            SELECT COUNT(*) FROM game_server_slots WHERE assigned_room_id=@room AND assigned_match_id=@match
              AND (@port IS NULL OR port=@port) AND last_heartbeat_utc>UTC_TIMESTAMP(6)-INTERVAL 10 SECOND;
            """,connection,transaction);
        command.Parameters.AddWithValue("@room",roomId);command.Parameters.AddWithValue("@match",matchId);
        command.Parameters.AddWithValue("@port",(object?)port??DBNull.Value);
        return Convert.ToInt32(await command.ExecuteScalarAsync(ct))==1;
    }

    internal static int RequestPort(HttpRequest request) => int.TryParse(request.Headers["X-ProjectProject01-Server-Port"],out int port)?port:7777;
}
