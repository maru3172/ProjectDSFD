using MySqlConnector;

internal static partial class CommunityEndpoints
{
    // A cursor hides only the caller's history. Deleting never erases the other participant's copy.
    private static async Task<List<Dictionary<string,object?>>> ConversationRows(MySqlConnection c,ulong me,CancellationToken ct)
    {
        var rows=await Rows(c,null,"""
        SELECT u.account_id accountId,u.display_name displayName,w.body,CAST(w.id AS CHAR) lastId,w.created_at_utc createdAtUtc,
          EXISTS(SELECT 1 FROM social_whispers incoming WHERE incoming.sender_id=u.id AND incoming.recipient_id=@me
            AND incoming.id>GREATEST(COALESCE(s.read_through_id,0),COALESCE(s.deleted_through_id,0))
            AND incoming.created_at_utc>UTC_TIMESTAMP(6)-INTERVAL 7 DAY) unread
        FROM (SELECT IF(sender_id=@me,recipient_id,sender_id) other_id,MAX(id) last_id FROM social_whispers
          WHERE (sender_id=@me OR recipient_id=@me) AND created_at_utc>UTC_TIMESTAMP(6)-INTERVAL 7 DAY
          GROUP BY other_id) thread JOIN users u ON u.id=thread.other_id JOIN social_whispers w ON w.id=thread.last_id
        LEFT JOIN social_conversation_state s ON s.owner_id=@me AND s.target_id=u.id
        WHERE w.id>COALESCE(s.deleted_through_id,0)
          AND EXISTS(SELECT 1 FROM social_presence p WHERE p.user_id=@me AND p.context='Lobby' AND p.last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 20 SECOND)
          AND NOT EXISTS(SELECT 1 FROM room_members m JOIN game_rooms r ON r.id=m.room_id WHERE m.user_id=@me AND r.status='Started' AND m.returned_to_room=0)
          AND NOT EXISTS(SELECT 1 FROM social_blocks b WHERE (b.owner_id=@me AND b.target_id=u.id) OR (b.owner_id=u.id AND b.target_id=@me))
        ORDER BY w.id DESC LIMIT 100
        """,ct,("me",me));
        foreach(var row in rows)row["unread"]=Convert.ToBoolean(row["unread"]);
        return rows;
    }

    private static void MapConversations(this WebApplication app)
    {
        app.MapGet("/api/social/conversations/{account}",async(string account,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var me=await LobbyEndpoints.AuthenticateAsync(req,db,ct);if(me is null)return Results.Unauthorized();
            if(account.Length is <3 or >32)return Error("상대 계정 ID를 확인하세요.");
            await using var c=await db.OpenConnectionAsync(ct);
            var target=await Rows(c,null,"SELECT id,account_id accountId,display_name displayName FROM users WHERE account_id=@account AND id<>@me",ct,("account",account),("me",me.Id));
            if(target.Count==0)return Results.NotFound(new{message="상대 계정을 확인하세요."});
            ulong other=Convert.ToUInt64(target[0]["id"]);
            var permission=await Rows(c,null,"""
                SELECT user_id FROM social_presence WHERE user_id=@me AND context='Lobby' AND last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 20 SECOND
                AND NOT EXISTS(SELECT 1 FROM room_members m JOIN game_rooms r ON r.id=m.room_id WHERE m.user_id=@me AND r.status='Started' AND m.returned_to_room=0)
                AND NOT EXISTS(SELECT 1 FROM social_blocks b WHERE (b.owner_id=@me AND b.target_id=@other) OR (b.owner_id=@other AND b.target_id=@me))
                """,ct,("me",me.Id),("other",other));
            if(permission.Count==0)return Error("대화는 로비·방에서 차단하지 않은 상대와만 열 수 있습니다.");
            var messages=await Rows(c,null,"""
                SELECT CAST(id AS CHAR) id,body,created_at_utc createdAtUtc,IF(sender_id=@me,'Outgoing','Incoming') direction
                FROM social_whispers WHERE ((sender_id=@me AND recipient_id=@other) OR (sender_id=@other AND recipient_id=@me))
                  AND id>COALESCE((SELECT deleted_through_id FROM social_conversation_state WHERE owner_id=@me AND target_id=@other),0)
                  AND created_at_utc>UTC_TIMESTAMP(6)-INTERVAL 7 DAY ORDER BY id DESC LIMIT 100
                """,ct,("me",me.Id),("other",other));
            return Results.Ok(new{accountId=target[0]["accountId"],displayName=target[0]["displayName"],messages,message="최근 7일 중 최대 100개 메시지를 표시합니다."});
        }).RequireRateLimiting("social");
    }
}
