using MySqlConnector;

internal static partial class CommunityEndpoints
{
    internal sealed record ProfileRequest(string StatusMessage,string Bio,string Accent,string Title);
    internal static bool ValidProfile(ProfileRequest p)=>p.StatusMessage is not null&&p.StatusMessage.Length<=160&&p.Bio is not null&&p.Bio.Length<=500
        &&(p.StatusMessage.Length==0||ValidText(p.StatusMessage,160))&&(p.Bio.Length==0||ValidText(p.Bio,500))
        &&(p.Accent is "Slate" or "Crimson" or "Ocean" or "Forest" or "Gold")&&(p.Title is "Rookie" or "Survivor" or "Mannequin" or "Explorer");

    private static void MapSocialProfiles(this WebApplication app)
    {
        app.MapGet("/api/social/search",async(string? q,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            string query=q?.Trim()??"";if(query.Length is <2 or >32)return Error("검색어를 2~32자로 입력하세요.");
            string pattern="%"+query.Replace("=","==").Replace("%","=%").Replace("_","=_")+"%";
            await using var c=await db.OpenConnectionAsync(ct);
            return Results.Ok(new{players=await Rows(c,null,"""
                SELECT u.account_id accountId,u.display_name displayName,COALESCE(p.status_message,'') statusMessage
                FROM users u LEFT JOIN social_profiles p ON p.user_id=u.id
                WHERE u.id<>@me AND (u.account_id LIKE @q ESCAPE '=' OR u.display_name LIKE @q ESCAPE '=')
                ORDER BY u.account_id LIMIT 20
                """,ct,("me",user.Id),("q",pattern))});
        }).RequireRateLimiting("social");
        app.MapGet("/api/social/profiles/{account}",async(string account,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var me=await LobbyEndpoints.AuthenticateAsync(req,db,ct);if(me is null)return Results.Unauthorized();
            if(account.Length is <3 or >32)return Error("계정 ID를 확인하세요.");
            await using var c=await db.OpenConnectionAsync(ct);
            var users=await Rows(c,null,"SELECT id FROM users WHERE account_id=@account",ct,("account",account));if(users.Count==0)return Results.NotFound();
            return await ProfileResponse(c,me.Id,Convert.ToUInt64(users[0]["id"]),ct);
        }).RequireRateLimiting("social");
        app.MapGet("/api/social/room-profiles/{userId}",async(string userId,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var me=await LobbyEndpoints.AuthenticateAsync(req,db,ct);if(me is null)return Results.Unauthorized();
            if(!ulong.TryParse(userId,out ulong id)||id==0)return Error("참가자 정보를 확인하세요.");
            await using var c=await db.OpenConnectionAsync(ct);
            var members=await Rows(c,null,"""
                SELECT target.user_id FROM room_members viewer JOIN room_members target ON target.room_id=viewer.room_id
                WHERE viewer.user_id=@me AND target.user_id=@target
                """,ct,("me",me.Id),("target",id));
            if(members.Count==0)return Results.NotFound(new{message="같은 방에 있는 참가자의 프로필만 열 수 있습니다. 방 목록을 다시 확인하세요."});
            return await ProfileResponse(c,me.Id,id,ct);
        }).RequireRateLimiting("social");
        app.MapPut("/api/social/profile",async(ProfileRequest input,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var me=await LobbyEndpoints.AuthenticateAsync(req,db,ct);if(me is null)return Results.Unauthorized();
            if(!ValidProfile(input))return Error("상태메시지는 160자, 소개글은 500자 이내이며 제공된 색상·칭호를 선택하세요.");
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            await Rows(c,tx,"SELECT id FROM users WHERE id=@id FOR UPDATE",ct,("id",me.Id));
            var recent=await Rows(c,tx,"SELECT user_id FROM social_profiles WHERE user_id=@id AND updated_at_utc>UTC_TIMESTAMP(6)-INTERVAL 5 SECOND",ct,("id",me.Id));
            if(recent.Count>0)return Results.Json(new{message="프로필은 5초 후 다시 변경할 수 있습니다."},statusCode:429);
            await Exec(c,tx,"""
                INSERT INTO social_profiles(user_id,status_message,bio,accent,title) VALUES(@id,@status,@bio,@accent,@title)
                ON DUPLICATE KEY UPDATE status_updated_at_utc=IF(status_message<>@status,UTC_TIMESTAMP(6),status_updated_at_utc),
                  status_message=@status,bio=@bio,accent=@accent,title=@title,updated_at_utc=UTC_TIMESTAMP(6)
                """,ct,("id",me.Id),("status",input.StatusMessage.Trim()),("bio",input.Bio.Trim()),("accent",input.Accent),("title",input.Title));
            await tx.CommitAsync(ct);return Results.Ok(new{message="프로필을 저장했습니다. 상태메시지는 다른 로그인 플레이어에게 공개됩니다."});
        }).RequireRateLimiting("social");
    }
    private static async Task<IResult> ProfileResponse(MySqlConnection c,ulong me,ulong id,CancellationToken ct)
    {
        var profiles=await ProfileRows(c,id,ct);if(profiles.Count==0)return Results.NotFound();
        var stats=await Rows(c,null,"""
            SELECT role,COUNT(*) matches,COALESCE(SUM(success),0) wins,COALESCE(SUM(capture_count),0) captures,
              COALESCE(SUM(rescue_count),0) rescues,MIN(CASE WHEN success=1 THEN escape_seconds END) bestEscapeSeconds
            FROM verified_match_results WHERE user_id=@id
              AND NOT EXISTS(SELECT 1 FROM match_custom_rules cr WHERE cr.match_id=verified_match_results.match_id AND cr.enabled=TRUE) GROUP BY role
            """,ct,("id",id));
        var relation=await Rows(c,null,"""
            SELECT EXISTS(SELECT 1 FROM social_friends WHERE user_low=@low AND user_high=@high) isFriend,
              EXISTS(SELECT 1 FROM social_blocks WHERE owner_id=@me AND target_id=@id) isBlocked
            """,ct,("low",Math.Min(me,id)),("high",Math.Max(me,id)),("me",me),("id",id));
        return Results.Ok(new{profile=profiles[0],statistics=stats,progression=await CaseRecords.PublicProfileAsync(c,id,ct),isSelf=id==me,
            isFriend=Convert.ToInt32(relation[0]["isFriend"])!=0,isBlocked=Convert.ToInt32(relation[0]["isBlocked"])!=0,
            statisticsNote="서버가 검증해 보관한 경기 기록 기준입니다. 미수집 경기는 포함하지 않습니다."});
    }
    private static Task<List<Dictionary<string,object?>>> ProfileRows(MySqlConnection c,ulong id,CancellationToken ct)=>Rows(c,null,"""
        SELECT u.account_id accountId,u.display_name displayName,COALESCE(p.status_message,'') statusMessage,COALESCE(p.bio,'') bio,
          COALESCE(p.accent,'Slate') accent,COALESCE(p.title,'Rookie') title
        FROM users u LEFT JOIN social_profiles p ON p.user_id=u.id WHERE u.id=@id
        """,ct,("id",id));

    private static async Task<IResult?> InviteToRoom(MySqlConnection c,MySqlTransaction tx,ulong me,ulong other,string name,bool blocked,bool allowObservers,CancellationToken ct)
    {
        if(blocked)return Error("초대를 전달할 수 없습니다.");
        if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_friends WHERE user_low=@low AND user_high=@high",ct,("low",Math.Min(me,other)),("high",Math.Max(me,other)))!=1)return Error("친구에게만 방 초대를 보낼 수 있습니다.");
        var rooms=await Rows(c,tx,"SELECT r.id,r.name,r.status FROM game_rooms r JOIN room_members m ON m.room_id=r.id WHERE m.user_id=@me FOR UPDATE",ct,("me",me));
        if(rooms.Count==0||Convert.ToString(rooms[0]["status"])!="Waiting")return Error("대기 중인 내 방에서만 초대할 수 있습니다.");
        string room=Convert.ToString(rooms[0]["id"])!;
        if(await LobbyEndpoints.IsRandomPartyAsync(c,tx,room,ct))return Error("랜덤 매칭은 솔로 전용입니다. 친구 초대는 오픈 매칭에서 이용하세요.");
        if(await LobbyEndpoints.IsBotRoomAsync(c,tx,room,ct))return Error("봇전에서는 친구를 초대할 수 없습니다. 오픈 매칭을 이용하세요.");
        if(await Scalar(c,tx,"SELECT (SELECT COUNT(*) FROM room_members WHERE room_id=@room AND is_observer=FALSE)+(SELECT COUNT(*) FROM room_bots WHERE room_id=@room)",ct,("room",room))>=3&&
            (!allowObservers||await Scalar(c,tx,"SELECT COUNT(*) FROM room_members WHERE room_id=@room AND is_observer=TRUE",ct,("room",room))>=2))return Error("방이 가득 찼습니다.");
        if(await Scalar(c,tx,"""
            SELECT COUNT(*) FROM social_presence p WHERE p.user_id=@other AND p.context='Lobby' AND p.presence<>'Offline'
            AND p.last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 20 SECOND
            AND EXISTS(SELECT 1 FROM auth_sessions s WHERE s.user_id=p.user_id AND s.revoked_at_utc IS NULL AND s.access_expires_at_utc>UTC_TIMESTAMP(6))
            AND NOT EXISTS(SELECT 1 FROM room_members m JOIN game_rooms r ON r.id=m.room_id WHERE m.user_id=p.user_id AND r.status='Started' AND m.returned_to_room=0)
            """,ct,("other",other))!=1)return Error("친구가 로비 또는 방에 접속 중일 때만 초대할 수 있습니다.");
        if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_room_invites WHERE room_id=@room AND recipient_id=@other AND expires_at_utc>UTC_TIMESTAMP(6)",ct,("room",room),("other",other))>0)return Error("이미 보낸 초대가 있습니다. 초대 유효시간은 2분입니다.");
        await Exec(c,tx,"DELETE FROM social_room_invites WHERE expires_at_utc<=UTC_TIMESTAMP(6);INSERT INTO social_room_invites(id,room_id,sender_id,recipient_id,expires_at_utc) VALUES(@id,@room,@me,@other,UTC_TIMESTAMP(6)+INTERVAL 2 MINUTE) ON DUPLICATE KEY UPDATE id=@id,sender_id=@me,expires_at_utc=UTC_TIMESTAMP(6)+INTERVAL 2 MINUTE,created_at_utc=UTC_TIMESTAMP(6)",ct,("id",Guid.NewGuid().ToString("D")),("room",room),("me",me),("other",other));
        await Notify(c,tx,other,"RoomInvite",name+"님이 '"+rooms[0]["name"]+"' 방에 초대했습니다.",ct);return null;
    }
    private static async Task<IResult> RespondToRoomInvite(MySqlConnection c,ulong me,string name,ActionRequest input,bool allowObservers,CancellationToken ct)
    {
        if(!Guid.TryParse(input.Target,out var id))return Error("초대 정보를 확인하세요.");
        await using var tx=await c.BeginTransactionAsync(ct);
        await LobbyEndpoints.LockLobbyStateAsync(c,tx,ct);
        var refs=await Rows(c,tx,"SELECT CAST(room_id AS CHAR) room FROM social_room_invites WHERE id=@id AND recipient_id=@me",ct,("id",id.ToString("D")),("me",me));
        if(refs.Count==0)return Error("이미 처리되었거나 만료된 초대입니다.");
        string room=(string)refs[0]["room"]!;
        var rooms=await Rows(c,tx,"SELECT status FROM game_rooms WHERE id=@room FOR UPDATE",ct,("room",room));
        var invitations=await Rows(c,tx,"SELECT sender_id FROM social_room_invites WHERE id=@id AND recipient_id=@me AND expires_at_utc>UTC_TIMESTAMP(6) FOR UPDATE",ct,("id",id.ToString("D")),("me",me));
        if(invitations.Count==0)return Error("초대가 만료되었거나 취소되었습니다.");
        if(input.Operation=="DeclineInvite")
        {
            await Notify(c,tx,Convert.ToUInt64(invitations[0]["sender_id"]),"InviteDeclined",name+"님이 방 초대를 거절했습니다.",ct);
            await Exec(c,tx,"DELETE FROM social_room_invites WHERE id=@id",ct,("id",id.ToString("D")));await tx.CommitAsync(ct);return Results.Ok(new{message="초대를 거절했습니다."});
        }
        ulong sender=Convert.ToUInt64(invitations[0]["sender_id"]);
        if(rooms.Count==0||Convert.ToString(rooms[0]["status"])!="Waiting")return Error("방이 사라졌거나 이미 시작되었습니다.");
        if(await LobbyEndpoints.IsRandomPartyAsync(c,tx,room,ct))return Error("랜덤 매칭 초대는 사용할 수 없습니다. 오픈 매칭에서 다시 초대받으세요.");
        if(await LobbyEndpoints.IsBotRoomAsync(c,tx,room,ct))return Error("봇전 초대는 사용할 수 없습니다. 오픈 매칭을 이용하세요.");
        if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_blocks WHERE (owner_id=@me AND target_id=@sender) OR(owner_id=@sender AND target_id=@me)",ct,("me",me),("sender",sender))>0 ||
            await Scalar(c,tx,"SELECT COUNT(*) FROM social_friends WHERE user_low=@low AND user_high=@high",ct,("low",Math.Min(me,sender)),("high",Math.Max(me,sender)))!=1)return Error("초대를 사용할 수 없습니다.");
        if(await Scalar(c,tx,"SELECT COUNT(*) FROM room_members WHERE room_id=@room AND user_id=@sender",ct,("room",room),("sender",sender))!=1)return Error("초대한 친구가 방을 나갔습니다.");
        if(await Scalar(c,tx,"SELECT COUNT(*) FROM room_members WHERE user_id=@me",ct,("me",me))>0)return Error("현재 방을 먼저 나간 뒤 초대를 수락하세요.");
        if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_presence WHERE user_id=@me AND context='Lobby' AND last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 20 SECOND",ct,("me",me))!=1)return Error("로비에서 초대를 수락하세요.");
        int partySize=await OpenParties.AdmissionSizeAsync(c,tx,me,"Open",ct);
        if(partySize<1)return Error("오픈매칭 파티장만 방 초대를 수락할 수 있습니다.");
        long playing=await Scalar(c,tx,"SELECT (SELECT COUNT(*) FROM room_members WHERE room_id=@room AND is_observer=FALSE)+(SELECT COUNT(*) FROM room_bots WHERE room_id=@room)",ct,("room",room));
        if(partySize>1&&playing+partySize>3)return Error("파티 전원이 플레이할 자리가 부족합니다.");
        bool asObserver=playing>=3;
        if(asObserver&&(!allowObservers||await Scalar(c,tx,"SELECT COUNT(*) FROM room_members WHERE room_id=@room AND is_observer=TRUE",ct,("room",room))>=2))return Error("방 인원이 가득 찼습니다.");
        await Exec(c,tx,"INSERT INTO room_members(room_id,user_id,is_ready,is_observer) VALUES(@room,@me,FALSE,@observer);DELETE FROM social_room_invites WHERE recipient_id=@me",ct,("room",room),("me",me),("observer",asObserver));
        if(!await OpenParties.AttachToRoomAsync(c,tx,me,room,"Open",ct))return Error("파티원 상태가 바뀌어 전원 입장을 취소했습니다.");
        await Notify(c,tx,sender,"InviteAccepted",name+"님이 방 초대를 수락했습니다.",ct);
        await tx.CommitAsync(ct);return Results.Ok(new{message="친구의 방에 참가했습니다. 준비 상태는 초기화됩니다."});
    }
}
