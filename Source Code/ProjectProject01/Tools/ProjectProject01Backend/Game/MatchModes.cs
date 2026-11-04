using MySqlConnector;

// PvP match modes (Docs/ItemRoguelikeMode.md). "Normal" is the plain game; "ItemRoguelike" adds the per-floor card picks,
// which the game server rolls and judges by itself. The backend only stores the room's choice, freezes it for the match
// and puts it in the match ticket. Item roguelike matches are unofficial like custom-rule matches. Random matching keeps
// one queue per mode. Mode keys must match Source/ProjectProject01/Public/ProjectItemRoguelike.h.
internal static class MatchModes
{
    internal const string Normal="Normal",ItemRoguelike="ItemRoguelike";
    internal static readonly string[] All=[Normal,ItemRoguelike];
    internal static bool Valid(string? mode)=>mode is not null&&All.Contains(mode);

    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS room_match_modes(room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          mode VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS match_modes(match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          mode VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL);
        """,ct);

    internal static async Task<string> RoomAsync(MySqlConnection c,MySqlTransaction? tx,string room,CancellationToken ct)
    {var rows=await CommunityEndpoints.Rows(c,tx,"SELECT mode FROM room_match_modes WHERE room_id=@r",ct,("r",room));return rows.Count==0?Normal:Convert.ToString(rows[0]["mode"])!;}
    internal static async Task<string> MatchAsync(MySqlConnection c,MySqlTransaction? tx,string match,CancellationToken ct)
    {var rows=await CommunityEndpoints.Rows(c,tx,"SELECT mode FROM match_modes WHERE match_id=@m",ct,("m",match));return rows.Count==0?Normal:Convert.ToString(rows[0]["mode"])!;}
    internal static async Task<bool> IsUnofficialAsync(MySqlConnection c,MySqlTransaction? tx,string match,CancellationToken ct)=>await MatchAsync(c,tx,match,ct)!=Normal;
    internal static string OfficialFilter(string column)=>$"NOT EXISTS (SELECT 1 FROM match_modes mm WHERE mm.match_id={column} AND mm.mode<>'{Normal}')";
    /// <summary>SQL condition on a random-match search (alias q) for one mode's queue.</summary>
    internal static string QueueFilter(string roomColumn)=>$"COALESCE((SELECT mode FROM room_match_modes rm WHERE rm.room_id={roomColumn}),'{Normal}')=@queueMode";

    internal static Task SetRoomAsync(MySqlConnection c,MySqlTransaction tx,string room,string mode,CancellationToken ct)=>
        CommunityEndpoints.Exec(c,tx,"INSERT INTO room_match_modes(room_id,mode) VALUES(@r,@m) ON DUPLICATE KEY UPDATE mode=@m",ct,("r",room),("m",mode));
    /// <summary>A random-match staging room made again for a returning player keeps the queue it came from.</summary>
    internal static Task CopyRoomAsync(MySqlConnection c,MySqlTransaction tx,string from,string to,CancellationToken ct)=>
        CommunityEndpoints.Exec(c,tx,"INSERT INTO room_match_modes(room_id,mode) SELECT @to,mode FROM room_match_modes WHERE room_id=@from ON DUPLICATE KEY UPDATE mode=VALUES(mode)",ct,("to",to),("from",from));
    internal static Task CaptureAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)=>CommunityEndpoints.Exec(c,tx,"""
        INSERT IGNORE INTO match_modes(match_id,mode)
        SELECT r.match_id,m.mode FROM game_rooms r JOIN room_match_modes m ON m.room_id=r.id
        WHERE r.match_id=@m AND r.status='Started' AND m.mode<>'Normal'
          AND NOT EXISTS(SELECT 1 FROM bot_rooms b WHERE b.room_id=r.id);
        """,ct,("m",match));
}

internal static partial class LobbyEndpoints
{
    internal sealed record MatchModeRequest(string RoomId,string Mode);
    internal static void MapMatchModes(this WebApplication app,Func<bool> available)
    {
        // The host of a waiting PvP room picks the mode: an open room (everyone's ready is reset) or a random-match
        // staging room before the search starts. Bot rooms have their own difficulty and affix modes.
        app.MapPut("/api/rooms/current/match-mode",async(MatchModeRequest body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(!Guid.TryParse(body.RoomId,out var id)||!MatchModes.Valid(body.Mode))return Results.BadRequest(new{message="경기 모드를 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await LockLobbyStateAsync(c,tx,ct);
            string room=id.ToString("D");
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT r.host_user_id,r.status FROM game_rooms r JOIN room_members m ON m.room_id=r.id WHERE r.id=@r AND m.user_id=@u FOR UPDATE",ct,("r",room),("u",user.Id));
            if(rows.Count!=1)return Results.NotFound();if(Convert.ToUInt64(rows[0]["host_user_id"])!=user.Id)return Results.StatusCode(403);
            if((string)rows[0]["status"]! !="Waiting")return Results.Conflict(new{message="대기 중인 방에서만 바꿀 수 있습니다."});
            bool bot=(await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM bot_rooms WHERE room_id=@r UNION ALL SELECT room_id FROM random_match_rooms WHERE room_id=@r",ct,("r",room))).Count>0;
            if(bot)return Results.Conflict(new{message="봇전과 진행 중인 랜덤 매칭에서는 바꿀 수 없습니다."});
            bool searching=(await CommunityEndpoints.Rows(c,tx,"SELECT search_id FROM random_match_searches WHERE room_id=@r AND state='Waiting'",ct,("r",room))).Count>0;
            if(searching)return Results.Conflict(new{message="매칭 중에는 바꿀 수 없습니다. 취소한 뒤 바꾸세요."});
            await MatchModes.SetRoomAsync(c,tx,room,body.Mode,ct);
            await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET is_ready=FALSE WHERE room_id=@r",ct,("r",room));
            await tx.CommitAsync(ct);
            return Results.Ok(new{message=body.Mode==MatchModes.ItemRoguelike?"아이템 로그라이크 모드로 바꿨습니다.":"일반 모드로 바꿨습니다.",room=await LoadCurrentRoomAsync(c,null,user.Id,available(),ct)});
        }).RequireRateLimiting("lobby");
    }
}
