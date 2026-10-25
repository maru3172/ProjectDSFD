using MySqlConnector;

internal static class OpenParties
{
    internal sealed record ActionRequest(string? Action,string? Target=null,string? Mode=null,string? Role=null);
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_parties(id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,leader_id BIGINT UNSIGNED NOT NULL,mode VARCHAR(8) NOT NULL,
          FOREIGN KEY(leader_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_party_members(user_id BIGINT UNSIGNED PRIMARY KEY,party_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          joined_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),KEY ix_case_party(party_id),
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,FOREIGN KEY(party_id) REFERENCES case_parties(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_party_invites(id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,party_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          recipient_id BIGINT UNSIGNED NOT NULL,expires_at_utc DATETIME(6) NOT NULL,UNIQUE KEY uq_case_invite(party_id,recipient_id),
          FOREIGN KEY(party_id) REFERENCES case_parties(id) ON DELETE CASCADE,FOREIGN KEY(recipient_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_role_preferences(user_id BIGINT UNSIGNED PRIMARY KEY,role VARCHAR(16) NOT NULL DEFAULT 'Any',
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
    private static async Task<bool> Busy(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"SELECT m.user_id FROM room_members m JOIN game_rooms r ON r.id=m.room_id WHERE m.user_id=@u AND (r.status<>'Waiting' OR EXISTS(SELECT 1 FROM random_match_searches q WHERE q.user_id=@u AND q.state='Waiting'))",ct,("u",user))).Count>0;
    private static async Task<string?> PartyOf(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"SELECT CAST(party_id AS CHAR) id FROM case_party_members WHERE user_id=@u",ct,("u",user))).Select(r=>(string)r["id"]!).FirstOrDefault();
    private static async Task<(bool Allowed,string? Room)> PartyLocation(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT CAST(g.id AS CHAR) id,g.status,EXISTS(SELECT 1 FROM bot_rooms b WHERE b.room_id=g.id) bot,EXISTS(SELECT 1 FROM random_match_parties q WHERE q.room_id=g.id) randomParty FROM game_rooms g JOIN room_members m ON m.room_id=g.id WHERE m.user_id=@u",ct,("u",user));
        if(rows.Count==0)return (true,null);
        return (Convert.ToString(rows[0]["status"])=="Waiting"&&!Convert.ToBoolean(rows[0]["bot"])&&!Convert.ToBoolean(rows[0]["randomParty"]),Convert.ToString(rows[0]["id"]));
    }
    internal static async Task<bool> HasPartyAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)=>await PartyOf(c,tx,user,ct)is not null;
    internal static async Task<int> AdmissionSizeAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string mode,CancellationToken ct)
    {
        string? id=await PartyOf(c,tx,user,ct);if(id is null)return 1;
        var p=await CommunityEndpoints.Rows(c,tx,"SELECT leader_id,mode FROM case_parties WHERE id=@p",ct,("p",id));
        if(p.Count!=1||Convert.ToUInt64(p[0]["leader_id"])!=user)return -1;
        return (await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_party_members WHERE party_id=@p",ct,("p",id))).Count;
    }
    internal static async Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT CAST(p.id AS CHAR) id,CAST(p.leader_id AS CHAR) leaderId,p.mode FROM case_parties p JOIN case_party_members m ON m.party_id=p.id WHERE m.user_id=@u",ct,("u",user));
        object? party=null;
        if(rows.Count>0)
        {
            string id=(string)rows[0]["id"]!;
            var members=await CommunityEndpoints.Rows(c,tx,"SELECT CAST(u.id AS CHAR) userId,u.account_id accountId,u.display_name displayName,COALESCE(a.xp,0) xp,COALESCE(b.human_role,r.role,'Any') role FROM case_party_members m JOIN users u ON u.id=m.user_id LEFT JOIN case_accounts a ON a.user_id=u.id LEFT JOIN case_role_preferences r ON r.user_id=u.id LEFT JOIN room_members rm ON rm.user_id=u.id LEFT JOIN game_rooms g ON g.id=rm.room_id AND g.host_user_id=u.id LEFT JOIN bot_rooms b ON b.room_id=g.id WHERE m.party_id=@p ORDER BY m.joined_at_utc,m.user_id",ct,("p",id));
            foreach(var member in members){member["level"]=CaseRecords.Level(Convert.ToInt64(member["xp"]));member.Remove("xp");}
            party=new{id,leaderId=rows[0]["leaderId"],mode=rows[0]["mode"],maximum=3,isLeader=Convert.ToString(rows[0]["leaderId"])==user.ToString(),members};
        }
        var invites=await CommunityEndpoints.Rows(c,tx,"SELECT CAST(i.id AS CHAR) id,u.display_name leaderName,p.mode FROM case_party_invites i JOIN case_parties p ON p.id=i.party_id JOIN users u ON u.id=p.leader_id WHERE i.recipient_id=@u AND i.expires_at_utc>UTC_TIMESTAMP(6) ORDER BY i.expires_at_utc LIMIT 16",ct,("u",user));
        var preference=await CommunityEndpoints.Rows(c,tx,"SELECT COALESCE((SELECT b.human_role FROM bot_rooms b JOIN game_rooms g ON g.id=b.room_id WHERE g.host_user_id=@u AND EXISTS(SELECT 1 FROM room_members m WHERE m.room_id=g.id AND m.user_id=@u)),p.role,'Any') role FROM users u LEFT JOIN case_role_preferences p ON p.user_id=u.id WHERE u.id=@u",ct,("u",user));
        var rooms=await CommunityEndpoints.Rows(c,tx,"SELECT CAST(room_id AS CHAR) id FROM room_members WHERE user_id=@u",ct,("u",user));
        return new{party,invites,role=preference.Count==0?"Any":preference[0]["role"],currentRoomId=rooms.Count==0?"":rooms[0]["id"]};
    }
    internal static async Task LeaveAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        string? id=await PartyOf(c,tx,user,ct);if(id is null)return;
        await CommunityEndpoints.Rows(c,tx,"SELECT id FROM case_parties WHERE id=@p FOR UPDATE",ct,("p",id));
        var current=await CommunityEndpoints.Rows(c,tx,"SELECT CAST(party_id AS CHAR) id FROM case_party_members WHERE user_id=@u FOR UPDATE",ct,("u",user));
        if(current.Count==0||Convert.ToString(current[0]["id"])!=id)return;
        await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_party_members WHERE user_id=@u",ct,("u",user));
        var remaining=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_party_members WHERE party_id=@p ORDER BY joined_at_utc,user_id FOR UPDATE",ct,("p",id));
        if(remaining.Count==0)await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_parties WHERE id=@p",ct,("p",id));
        else await CommunityEndpoints.Exec(c,tx,"UPDATE case_parties SET leader_id=@next WHERE id=@p AND leader_id=@u",ct,("p",id),("u",user),("next",remaining[0]["user_id"]));
    }
    internal static async Task<bool> AttachToRoomAsync(MySqlConnection c,MySqlTransaction tx,ulong leader,string room,string mode,CancellationToken ct)
    {
        string? id=await PartyOf(c,tx,leader,ct);if(id is null){await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_role_preferences(user_id,role) VALUES(@u,'Any') ON DUPLICATE KEY UPDATE role='Any'",ct,("u",leader));return await RoomRoleChoices.CanReserveAsync(c,tx,room,ct);}
        var party=await CommunityEndpoints.Rows(c,tx,"SELECT leader_id,mode FROM case_parties WHERE id=@p FOR UPDATE",ct,("p",id));
        if(party.Count==0||Convert.ToUInt64(party[0]["leader_id"])!=leader)return false;
        var members=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_party_members WHERE party_id=@p ORDER BY joined_at_utc,user_id",ct,("p",id));
        if(mode=="Bot"&&members.Count>2)return false;
        foreach(var member in members)
        {
            ulong user=Convert.ToUInt64(member["user_id"]);if(user==leader)continue;
            if(!await CanEnter(c,tx,user,ct)||(await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE user_id=@u",ct,("u",user))).Count>0)return false;
        }
        foreach(var member in members)
        {
            ulong user=Convert.ToUInt64(member["user_id"]);if(user!=leader)await CommunityEndpoints.Exec(c,tx,"INSERT INTO room_members(room_id,user_id,is_ready) VALUES(@r,@u,FALSE)",ct,("r",room),("u",user));
        }
        foreach(var member in members)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_role_preferences(user_id,role) VALUES(@u,'Any') ON DUPLICATE KEY UPDATE role='Any'",ct,("u",member["user_id"]));
        if(mode=="Bot")
        {
            var bots=await CommunityEndpoints.Rows(c,tx,"SELECT bot_id FROM room_bots WHERE room_id=@r ORDER BY bot_id",ct,("r",room));
            foreach(var bot in bots.Take(Math.Max(0,members.Count-1)))await CommunityEndpoints.Exec(c,tx,"DELETE FROM room_bots WHERE bot_id=@b",ct,("b",bot["bot_id"]));
        }
        return await RoomRoleChoices.CanReserveAsync(c,tx,room,ct);
    }
    private static async Task<bool> CanEnter(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"""
            SELECT s.user_id FROM auth_sessions s WHERE s.user_id=@u AND s.revoked_at_utc IS NULL AND s.access_expires_at_utc>UTC_TIMESTAMP(6)
            AND NOT EXISTS(SELECT 1 FROM auth_session_liveness l WHERE l.session_id=s.id AND l.deadline_utc<=UTC_TIMESTAMP(6))
            AND NOT EXISTS(SELECT 1 FROM account_restrictions r WHERE r.user_id=@u AND r.lifted_at_utc IS NULL AND (r.expires_at_utc IS NULL OR r.expires_at_utc>UTC_TIMESTAMP(6))) LIMIT 1
            """,ct,("u",user))).Count>0;
    internal static void MapOpenParties(this WebApplication app)
    {
        app.MapPost("/api/parties/action",async(ActionRequest body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            if(body.Action is not ("Create" or "Leave" or "Invite" or "Accept" or "Decline" or "Kick" or "Role"))return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            await LobbyEndpoints.LockLobbyStateAsync(c,tx,ct);
            await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_party_invites WHERE expires_at_utc<=UTC_TIMESTAMP(6)",ct);
            if(await Busy(c,tx,user.Id,ct))return Results.Conflict(new{message="매칭 확정 또는 경기 중에는 파티·역할을 변경할 수 없습니다."});
            string? id=await PartyOf(c,tx,user.Id,ct);
            if(id is not null)await CommunityEndpoints.Rows(c,tx,"SELECT id FROM case_parties WHERE id=@p FOR UPDATE",ct,("p",id));
            if(body.Action=="Role")
            {
                if(body.Role is not ("Any" or "Survivor" or "Mannequin"))return Results.BadRequest();
                if(body.Role!="Any"&&(await CommunityEndpoints.Rows(c,tx,"SELECT p.room_id FROM random_match_parties p JOIN room_members m ON m.room_id=p.room_id WHERE m.user_id=@u",ct,("u",user.Id))).Count==0)
                {
                    var others=await CommunityEndpoints.Rows(c,tx,"SELECT m.user_id FROM room_members m JOIN game_rooms g ON g.id=m.room_id LEFT JOIN bot_rooms b ON b.room_id=g.id LEFT JOIN case_role_preferences p ON p.user_id=m.user_id WHERE m.room_id=(SELECT room_id FROM room_members WHERE user_id=@u) AND m.user_id<>@u AND m.is_observer=FALSE AND COALESCE(IF(m.user_id=g.host_user_id,b.human_role,NULL),p.role,'Any')=@role",ct,("u",user.Id),("role",body.Role));
                    if(others.Count>=(body.Role=="Mannequin"?1:2))return Results.Conflict(new{message="방의 해당 역할 정원이 찼습니다."});
                }
                await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_role_preferences(user_id,role) VALUES(@u,@role) ON DUPLICATE KEY UPDATE role=@role",ct,("u",user.Id),("role",body.Role));
                await CommunityEndpoints.Exec(c,tx,"UPDATE bot_rooms b JOIN game_rooms r ON r.id=b.room_id JOIN room_members m ON m.room_id=r.id SET b.human_role=@role WHERE r.host_user_id=@u AND m.user_id=@u AND r.status='Waiting'",ct,("u",user.Id),("role",body.Role));
                await CommunityEndpoints.Exec(c,tx,"UPDATE room_members m JOIN game_rooms r ON r.id=m.room_id SET m.is_ready=FALSE WHERE r.status='Waiting' AND r.id=(SELECT room_id FROM (SELECT room_id FROM room_members WHERE user_id=@u) current_room)",ct,("u",user.Id));
            }
            else if(body.Action=="Create")
            {
                if(!string.IsNullOrEmpty(body.Mode)&&body.Mode is not ("Common" or "Open" or "Bot"))return Results.BadRequest();
                if(id is null)
                {
                    if(!(await PartyLocation(c,tx,user.Id,ct)).Allowed)return Results.Conflict(new{message="로비 또는 대기 중인 오픈매칭 방에서 파티를 구성하세요."});
                    id=Guid.NewGuid().ToString();await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_parties(id,leader_id,mode) VALUES(@p,@u,@mode);INSERT INTO case_party_members(user_id,party_id) VALUES(@u,@p)",ct,("p",id),("u",user.Id),("mode","Common"));
                }
            }
            else if(body.Action=="Leave")await LeaveAsync(c,tx,user.Id,ct);
            else if(body.Action is "Accept" or "Decline")
            {
                if(!Guid.TryParse(body.Target,out var invite))return Results.BadRequest();
                var invitations=await CommunityEndpoints.Rows(c,tx,"SELECT CAST(party_id AS CHAR) partyId FROM case_party_invites WHERE id=@i AND recipient_id=@u AND expires_at_utc>UTC_TIMESTAMP(6)",ct,("i",invite.ToString()),("u",user.Id));
                if(invitations.Count==0)return Results.NotFound();
                if(body.Action=="Accept")
                {
                    if(id is not null)return Results.Conflict(new{message="현재 파티를 먼저 나가세요."});
                    string targetParty=(string)invitations[0]["partyId"]!;
                    var leader=await CommunityEndpoints.Rows(c,tx,"SELECT leader_id,mode FROM case_parties WHERE id=@p FOR UPDATE",ct,("p",targetParty));
                    var members=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_party_members WHERE party_id=@p FOR UPDATE",ct,("p",targetParty));
                    int maximum=3;
                    if(members.Count>=maximum)return Results.Conflict(new{message=maximum==2?"봇전은 최소 1명의 봇을 유지하도록 파티 최대 2명입니다.":"오픈매칭 파티 정원은 최대 3명입니다."});
                    if(leader.Count==0||!await CanEnter(c,tx,Convert.ToUInt64(leader[0]["leader_id"]),ct)||await Busy(c,tx,Convert.ToUInt64(leader[0]["leader_id"]),ct))return Results.Conflict(new{message="파티장이 접속 중인 로비에서만 수락할 수 있습니다."});
                    var blocked=await CommunityEndpoints.Rows(c,tx,"SELECT owner_id FROM social_blocks WHERE (owner_id=@u AND target_id IN (SELECT user_id FROM case_party_members WHERE party_id=@p)) OR (target_id=@u AND owner_id IN (SELECT user_id FROM case_party_members WHERE party_id=@p))",ct,("u",user.Id),("p",targetParty));
                    if(blocked.Count>0)return Results.StatusCode(403);
                    var mine=await PartyLocation(c,tx,user.Id,ct);var theirs=await PartyLocation(c,tx,Convert.ToUInt64(leader[0]["leader_id"]),ct);
                    if(!mine.Allowed||!theirs.Allowed||(mine.Room is not null&&theirs.Room is not null&&mine.Room!=theirs.Room))return Results.Conflict(new{message="로비 또는 같은 대기 중인 오픈매칭 방에서 수락하세요."});
                    await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_party_members(user_id,party_id) VALUES(@u,@p)",ct,("u",user.Id),("p",targetParty));
                    // A new member starts with no hard role reservation, avoiding an impossible party roster.
                    if(mine.Room is null)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_role_preferences(user_id,role) VALUES(@u,'Any') ON DUPLICATE KEY UPDATE role='Any'",ct,("u",user.Id));
                }
                await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_party_invites WHERE id=@i AND recipient_id=@u",ct,("i",invite.ToString()),("u",user.Id));
            }
            else
            {
                if(id is null)
                {
                    if(body.Action!="Invite")return Results.Conflict(new{message="먼저 파티를 만드세요."});
                    if(!(await PartyLocation(c,tx,user.Id,ct)).Allowed)return Results.Conflict(new{message="로비 또는 대기 중인 오픈매칭 방에서 초대하세요."});
                    id=Guid.NewGuid().ToString();await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_parties(id,leader_id,mode) VALUES(@p,@u,'Common');INSERT INTO case_party_members(user_id,party_id) VALUES(@u,@p)",ct,("p",id),("u",user.Id));
                }
                var party=await CommunityEndpoints.Rows(c,tx,"SELECT leader_id,mode FROM case_parties WHERE id=@p",ct,("p",id));
                if(party.Count==0||Convert.ToUInt64(party[0]["leader_id"])!=user.Id)return Results.StatusCode(403);
                if(body.Action=="Kick")
                {
                    if(!ulong.TryParse(body.Target,out ulong target)||target==user.Id)return Results.BadRequest();
                    if(await PartyOf(c,tx,target,ct)!=id)return Results.NotFound();if(await Busy(c,tx,target,ct))return Results.Conflict();
                    await LeaveAsync(c,tx,target,ct);
                }
                else
                {
                    if(string.IsNullOrWhiteSpace(body.Target)||body.Target.Length>32)return Results.BadRequest();
                    var targets=await CommunityEndpoints.Rows(c,tx,"SELECT u.id FROM users u WHERE u.account_id=@account AND u.id<>@u AND (EXISTS(SELECT 1 FROM social_friends f WHERE f.user_low=LEAST(@u,u.id) AND f.user_high=GREATEST(@u,u.id)) OR EXISTS(SELECT 1 FROM room_members me JOIN room_members peer ON peer.room_id=me.room_id JOIN game_rooms g ON g.id=me.room_id WHERE me.user_id=@u AND peer.user_id=u.id AND g.status='Waiting' AND NOT EXISTS(SELECT 1 FROM bot_rooms b WHERE b.room_id=g.id) AND NOT EXISTS(SELECT 1 FROM random_match_parties q WHERE q.room_id=g.id)))",ct,("u",user.Id),("account",body.Target));
                    if(targets.Count!=1)return Results.Conflict(new{message="친구 또는 같은 오픈매칭 방의 계정을 확인하세요."});
                    ulong target=Convert.ToUInt64(targets[0]["id"]);
                    int maximum=3;
                    if((await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_party_members WHERE party_id=@p",ct,("p",id))).Count>=maximum)return Results.Conflict(new{message=maximum==2?"봇전 파티는 봇 자리를 남기기 위해 최대 2명입니다.":"파티 정원은 최대 3명입니다."});
                    var blocked=await CommunityEndpoints.Rows(c,tx,"SELECT owner_id FROM social_blocks WHERE (owner_id=@u AND target_id=@t) OR (owner_id=@t AND target_id=@u)",ct,("u",user.Id),("t",target));
                    if(blocked.Count>0||!await CanEnter(c,tx,target,ct)||await Busy(c,tx,target,ct))return Results.Conflict(new{message="초대를 전달할 수 없습니다."});
                    await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_party_invites WHERE expires_at_utc<=UTC_TIMESTAMP(6);INSERT INTO case_party_invites(id,party_id,recipient_id,expires_at_utc) VALUES(@i,@p,@t,UTC_TIMESTAMP(6)+INTERVAL 2 MINUTE) ON DUPLICATE KEY UPDATE id=@i,expires_at_utc=UTC_TIMESTAMP(6)+INTERVAL 2 MINUTE",ct,("i",Guid.NewGuid().ToString()),("p",id),("t",target));
                }
            }
            var snapshot=await SnapshotAsync(c,tx,user.Id,ct);await tx.CommitAsync(ct);return Results.Ok(new{message="파티 구성을 갱신했습니다.",partyState=snapshot});
        }).RequireRateLimiting("social");
    }
}
