using MySqlConnector;

internal static class AuthoritativeMatchLedger
{
    // Called in the same transaction as role assignment. Never rewrite an existing roster.
    internal static async Task CaptureAsync(MySqlConnection connection, MySqlTransaction transaction,string match,CancellationToken ct)
    {
        await using var command=new MySqlCommand("""
            INSERT IGNORE INTO authoritative_matches(match_id,room_id,room_name)
            SELECT match_id,id,name FROM game_rooms WHERE match_id=@match AND status='Started';
            INSERT IGNORE INTO authoritative_match_players(match_id,user_id,role)
            SELECT r.match_id,m.user_id,m.assigned_role FROM game_rooms r JOIN room_members m ON m.room_id=r.id
            WHERE r.match_id=@match AND r.status='Started' AND m.assigned_role IN ('Mannequin','Survivor');
            """,connection,transaction){CommandTimeout=5};
        command.Parameters.AddWithValue("@match",match);await command.ExecuteNonQueryAsync(ct);
        var rooms=await CommunityEndpoints.Rows(connection,transaction,"SELECT id FROM game_rooms WHERE match_id=@m AND status='Started'",ct,("m",match));
        if(rooms.Count==1)await LobbyEndpoints.SnapshotMatchBotsAsync(connection,transaction,Convert.ToString(rooms[0]["id"])!,match,ct);
        await BotSkillRatings.CaptureAsync(connection,transaction,match,ct);
    }
    internal static async Task<bool> EndAsync(MySqlConnection connection,MySqlTransaction transaction,string match,CancellationToken ct)
    {
        await CaptureAsync(connection,transaction,match,ct); // live-upgrade compatibility only; absent room cannot fabricate admission
        string? room=null;
        await using(var read=new MySqlCommand("SELECT room_id FROM authoritative_matches WHERE match_id=@match FOR UPDATE;",connection,transaction))
        {read.Parameters.AddWithValue("@match",match);room=Convert.ToString(await read.ExecuteScalarAsync(ct));}
        if(string.IsNullOrEmpty(room))return false;
        await using(var end=new MySqlCommand("UPDATE authoritative_matches SET ended_at_utc=COALESCE(ended_at_utc,UTC_TIMESTAMP(6)) WHERE match_id=@match;",connection,transaction))
        {end.Parameters.AddWithValue("@match",match);await end.ExecuteNonQueryAsync(ct);}
        await LobbyEndpoints.ResetStartedRoomIfAllMembersReturnedAsync(connection,transaction,room,ct);
        return true;
    }
}
