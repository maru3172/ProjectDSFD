using System.Security.Cryptography;
using MySqlConnector;

internal static partial class LobbyEndpoints
{
    private sealed class MatchmakingChangedException:Exception {}
    internal sealed record MatchSearchRequest(string SearchId);
    internal sealed record MatchSearchState(string State,string Message,int WaitingCount=0,int Position=0,int SecondsRemaining=0,RoomState? Room=null);
    private static void MapRandomMatchmaking(this WebApplication app,string travelUrl,Func<bool> available,GameServerDemandManager? demand)
    {
        app.MapPost("/api/rooms/matchmaking/party",async(HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(req.HttpContext.RequestServices.GetRequiredService<ServiceControlState>().Get().MaintenanceEnabled)return Results.Conflict(new{message="점검 중에는 랜덤 매칭을 준비할 수 없습니다."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            int maximum=await LockLobbyStateAsync(c,tx,ct);
            string? current=await FindCurrentRoomIdAsync(c,tx,user.Id,ct);
            if(current is not null)
            {
                if(!await IsRandomPartyAsync(c,tx,current,ct))return Results.Conflict(new{message="오픈 매칭 방을 먼저 나가세요."});
            }
            else
            {
                if(await CountRoomsAsync(c,tx,ct)>=maximum)return Results.Conflict(new{message="방 한도가 가득 찼습니다. 잠시 후 다시 시도하세요."});
                current=Guid.NewGuid().ToString("D");string code=await GenerateUniqueJoinCodeAsync(c,tx,ct);
                await CommunityEndpoints.Exec(c,tx,"INSERT INTO game_rooms(id,join_code,name,host_user_id,is_public) VALUES(@r,@code,@name,@u,FALSE);INSERT INTO room_members(room_id,user_id) VALUES(@r,@u);INSERT INTO random_match_parties(room_id) VALUES(@r)",ct,("r",current),("code",code),("name","랜덤 매칭 "+code),("u",user.Id));
            }
            var room=await LoadCurrentRoomAsync(c,tx,user.Id,available(),ct);await tx.CommitAsync(ct);
            return Results.Ok(new{message="시작을 누르면 개인 신청자끼리 랜덤 매칭합니다.",room});
        }).RequireRateLimiting("lobby");
        app.MapPost("/api/rooms/matchmaking/join",(MatchSearchRequest input,HttpRequest req,AuthDatabase db,CancellationToken ct)=>Search("Join",input.SearchId,req,db,ct)).RequireRateLimiting("lobby");
        app.MapGet("/api/rooms/matchmaking/status/{searchId}",(string searchId,HttpRequest req,AuthDatabase db,CancellationToken ct)=>Search("Poll",searchId,req,db,ct)).RequireRateLimiting("lobby");
        app.MapPost("/api/rooms/matchmaking/cancel",(MatchSearchRequest input,HttpRequest req,AuthDatabase db,CancellationToken ct)=>Search("Cancel",input.SearchId,req,db,ct)).RequireRateLimiting("lobby");

        async Task<IResult> Search(string operation,string searchId,HttpRequest req,AuthDatabase db,CancellationToken ct)
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(!Guid.TryParse(searchId,out var guid))return Results.BadRequest(new{message="매칭 요청 식별자를 확인하세요."});searchId=guid.ToString("D");
            try
            {
                await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
                await LockLobbyStateAsync(c,tx,ct); // Cancels, party changes and formation have a single authoritative ordering.
                await TouchMemberAsync(c,tx,user.Id,ct);await SweepMatchSearchesAsync(c,tx,ct);
                var attempt=await CommunityEndpoints.Rows(c,tx,"SELECT user_id,state FROM random_match_searches WHERE search_id=@s FOR UPDATE",ct,("s",searchId));
                bool owner=attempt.Count==0||Convert.ToUInt64(attempt[0]["user_id"])==user.Id;
                if(!owner)
                {
                    var member=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM random_match_search_members WHERE search_id=@s AND user_id=@u",ct,("s",searchId),("u",user.Id));
                    if(member.Count==0)return Results.NotFound();
                    if(operation!="Poll")return Results.Json(new{message="본인의 매칭 신청만 취소할 수 있습니다."},statusCode:403);
                }
                if(operation=="Cancel")
                    await CommunityEndpoints.Exec(c,tx,"INSERT INTO random_match_searches(search_id,user_id,state) VALUES(@s,@u,'Cancelled') ON DUPLICATE KEY UPDATE state=IF(state='Waiting','Cancelled',state)",ct,("s",searchId),("u",user.Id));
                else if(operation=="Join"&&attempt.Count==0)
                {
                    var membership=await LoadMembershipAsync(c,tx,user.Id,true,ct);
                    if(membership is null||!membership.IsHost||membership.Status!="Waiting"||!await IsRandomPartyAsync(c,tx,membership.RoomId,ct))
                        return Results.Conflict(new{message="랜덤 매칭 화면에서 시작하세요."});
                    var size=await CommunityEndpoints.Rows(c,tx,"SELECT COUNT(*) n FROM room_members WHERE room_id=@r",ct,("r",membership.RoomId));
                    if(Convert.ToInt32(size[0]["n"])!=1)return Results.Conflict(new{message="랜덤 매칭은 개인 신청만 가능합니다. 기존 방을 나간 뒤 다시 시도하세요."});
                    if(!available()||req.HttpContext.RequestServices.GetRequiredService<ServiceControlState>().Get().MaintenanceEnabled)
                        return Results.Ok(new MatchSearchState("Unavailable","게임 서버 운영 중지 또는 점검 중이라 시작할 수 없습니다."));
                    var existing=await CommunityEndpoints.Rows(c,tx,"SELECT search_id FROM random_match_searches WHERE room_id=@r AND state='Waiting'",ct,("r",membership.RoomId));
                    if(existing.Count>0)return Results.Conflict(new{message="이미 랜덤 매칭 중입니다."});
                    var count=await CommunityEndpoints.Rows(c,tx,"SELECT COUNT(*) n FROM random_match_searches WHERE state='Waiting'",ct);
                    if(Convert.ToInt32(count[0]["n"])>=300)return Results.Ok(new MatchSearchState("Unavailable","매칭 대기열이 가득 찼습니다."));
                    await CommunityEndpoints.Exec(c,tx,"""
                        INSERT INTO random_match_searches(search_id,user_id,room_id) VALUES(@s,@u,@r);
                        INSERT INTO random_match_search_members(search_id,user_id) SELECT @s,user_id FROM room_members WHERE room_id=@r;
                        UPDATE random_match_parties SET search_id=@s WHERE room_id=@r;
                        """,ct,("s",searchId),("u",user.Id),("r",membership.RoomId));
                }
                else if(operation=="Poll")
                    await CommunityEndpoints.Exec(c,tx,"UPDATE random_match_searches SET last_seen_utc=UTC_TIMESTAMP(6) WHERE search_id=@s AND state='Waiting'",ct,("s",searchId));

                bool online=available()&&!req.HttpContext.RequestServices.GetRequiredService<ServiceControlState>().Get().MaintenanceEnabled;
                if(operation!="Cancel")
                {
                    if(!online)await CommunityEndpoints.Exec(c,tx,"UPDATE random_match_searches SET state='Unavailable' WHERE search_id=@s AND state='Waiting'",ct,("s",searchId));
                    else await FormRandomGroupAsync(c,tx,req,travelUrl,demand,ct);
                }
                await ClearInactivePartySearchesAsync(c,tx,ct);
                var rows=await CommunityEndpoints.Rows(c,tx,"SELECT state,room_id,GREATEST(0,180-TIMESTAMPDIFF(SECOND,created_at_utc,UTC_TIMESTAMP(6))) remaining FROM random_match_searches WHERE search_id=@s",ct,("s",searchId));
                string state=rows.Count==0?"Idle":Convert.ToString(rows[0]["state"])!;
                var current=await LoadCurrentRoomAsync(c,tx,user.Id,available(),ct);
                if(state=="Matched"&&current?.RoomId!=Convert.ToString(rows[0]["room_id"]))state="Idle";
                var waiting=await CommunityEndpoints.Rows(c,tx,"SELECT q.search_id,COUNT(m.user_id) n FROM random_match_searches q JOIN random_match_search_members m ON m.search_id=q.search_id WHERE q.state='Waiting' GROUP BY q.search_id ORDER BY MIN(q.created_at_utc),q.search_id",ct);
                int total=waiting.Sum(r=>Convert.ToInt32(r["n"])),position=waiting.FindIndex(r=>Convert.ToString(r["search_id"])==searchId)+1;
                string message=state switch{"Waiting"=>"매칭 중… 개인 신청자 3명을 모집하고 있습니다.","Matched"=>"3/3 확정. 자기 역할을 확인하고 게임 시작을 기다려주세요.","Cancelled"=>"매칭을 취소했습니다.","Expired"=>"접속 확인 또는 대기 시간이 만료되었습니다. 다시 시작할 수 있습니다.","Unavailable"=>"게임 서버 운영 중지 또는 점검으로 매칭을 종료했습니다.",_=>"진행 중인 매칭이 없습니다."};
                await tx.CommitAsync(ct);
                return Results.Ok(new MatchSearchState(state,message,total,position,state=="Waiting"?Convert.ToInt32(rows[0]["remaining"]):0,current));
            }
            catch(MatchmakingChangedException){return Results.Conflict(new{message="참가 인원이 변경되어 매칭 상태를 다시 확인합니다."});}
            catch(MySqlException ex)when(ex.Number is 1062 or 1213 or 1205 or 1452){return Results.Conflict(new{message="동시 요청을 처리 중입니다. 매칭 상태를 다시 확인하세요."});}
        }
    }
    private static async Task FormRandomGroupAsync(MySqlConnection c,MySqlTransaction tx,HttpRequest req,string url,GameServerDemandManager? demand,CancellationToken ct)
    {
        // Internal staging rooms are single-user only. Never recruit from open rooms or grouped parties.
        var selected=await CommunityEndpoints.Rows(c,tx,"SELECT q.search_id,q.room_id FROM random_match_searches q JOIN random_match_search_members m ON m.search_id=q.search_id WHERE q.state='Waiting' GROUP BY q.search_id,q.room_id HAVING COUNT(m.user_id)=1 ORDER BY MIN(q.created_at_utc),q.search_id LIMIT 3",ct);
        if(selected.Count!=3)return;
        string room=Convert.ToString(selected[0]["room_id"])!;
        int? port=demand?.Configured==true?7777:await GameServerSlots.ReserveAsync(c,tx,room,ct);if(port is null)return;
        List<ulong> ids=[];
        foreach(var entry in selected)
        {
            string old=Convert.ToString(entry["room_id"])!;
            var members=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE room_id=@r ORDER BY joined_at_utc,user_id FOR UPDATE",ct,("r",old));
            ids.AddRange(members.Select(m=>Convert.ToUInt64(m["user_id"])));
            await CommunityEndpoints.Exec(c,tx,"INSERT INTO random_match_origins(room_id,user_id,party_key,original_host) SELECT @base,m.user_id,@key,r.host_user_id FROM room_members m JOIN game_rooms r ON r.id=m.room_id WHERE m.room_id=@old",ct,("base",room),("key",entry["search_id"]),("old",old));
            // Pre-match party chat must not become visible to strangers when parties merge.
            await CommunityEndpoints.Exec(c,tx,"DELETE FROM room_chat_messages WHERE room_id=@old",ct,("old",old));
            if(old!=room)await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET room_id=@r WHERE room_id=@old;DELETE FROM game_rooms WHERE id=@old",ct,("r",room),("old",old));
            await CommunityEndpoints.Exec(c,tx,"UPDATE random_match_searches SET state='Matched',room_id=@r WHERE search_id=@s",ct,("r",room),("s",entry["search_id"]));
        }
        if(ids.Count!=3||ids.Distinct().Count()!=3)throw new MatchmakingChangedException();
        ulong mannequin=ids[RandomNumberGenerator.GetInt32(3)];string host=new Uri("http://"+ResolveTravelUrlForRequest(req,url)).Host;
        await CommunityEndpoints.Exec(c,tx,"""
            INSERT INTO random_match_rooms(room_id) VALUES(@r) ON DUPLICATE KEY UPDATE room_id=@r;
            UPDATE room_members SET is_ready=TRUE,returned_to_room=FALSE,assigned_role=IF(user_id=@mannequin,'Mannequin','Survivor') WHERE room_id=@r;
            UPDATE game_rooms SET status='Starting',travel_url=@url,started_at_utc=UTC_TIMESTAMP(6) WHERE id=@r;
            UPDATE random_match_parties SET search_id=NULL WHERE room_id=@r;
            """,ct,("r",room),("url",$"{host}:{port}"),("mannequin",mannequin));
    }
    internal static async Task<bool> IsRandomPartyAsync(MySqlConnection c,MySqlTransaction? tx,string room,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM random_match_parties WHERE room_id=@r",ct,("r",room))).Count>0;
    private static async Task CancelUserSearchesAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c,tx,"UPDATE random_match_searches q SET state='Cancelled' WHERE state='Waiting' AND (user_id=@u OR EXISTS(SELECT 1 FROM random_match_search_members m WHERE m.search_id=q.search_id AND m.user_id=@u))",ct,("u",user));
        await ClearInactivePartySearchesAsync(c,tx,ct);
    }
    private static Task ClearInactivePartySearchesAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)=>
        CommunityEndpoints.Exec(c,tx,"UPDATE random_match_parties p JOIN random_match_searches q ON q.search_id=p.search_id SET p.search_id=NULL WHERE q.state<>'Waiting'",ct);
    internal static async Task SweepMatchSearchesAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        if(tx is null)
        {
            await using var cleanup=await c.BeginTransactionAsync(ct);await LockLobbyStateAsync(c,cleanup,ct);
            await SweepMatchSearchesAsync(c,cleanup,ct);await cleanup.CommitAsync(ct);return;
        }
        await CommunityEndpoints.Exec(c,tx,"""
            UPDATE random_match_searches q SET state='Expired' WHERE state='Waiting' AND
              (last_seen_utc<=UTC_TIMESTAMP(6)-INTERVAL 30 SECOND OR created_at_utc<=UTC_TIMESTAMP(6)-INTERVAL 180 SECOND
               OR NOT EXISTS(SELECT 1 FROM random_match_parties p WHERE p.room_id=q.room_id AND p.search_id=q.search_id)
               OR EXISTS(SELECT 1 FROM random_match_search_members sm WHERE sm.search_id=q.search_id AND
                (NOT EXISTS(SELECT 1 FROM room_members m WHERE m.user_id=sm.user_id AND m.room_id=q.room_id AND m.last_seen_at_utc>UTC_TIMESTAMP(6)-INTERVAL 30 SECOND)
                 OR NOT EXISTS(SELECT 1 FROM auth_sessions s WHERE s.user_id=sm.user_id AND s.revoked_at_utc IS NULL AND s.access_expires_at_utc>UTC_TIMESTAMP(6)
                  AND NOT EXISTS(SELECT 1 FROM auth_session_liveness l WHERE l.session_id=s.id AND l.deadline_utc<=UTC_TIMESTAMP(6)))
                 OR EXISTS(SELECT 1 FROM account_restrictions r WHERE r.user_id=sm.user_id AND r.lifted_at_utc IS NULL AND (r.expires_at_utc IS NULL OR r.expires_at_utc>UTC_TIMESTAMP(6)))))
               OR (SELECT COUNT(*) FROM room_members m WHERE m.room_id=q.room_id)<>1
               OR (SELECT COUNT(*) FROM random_match_search_members sm WHERE sm.search_id=q.search_id)<>1);
            DELETE FROM random_match_searches WHERE state<>'Waiting' AND created_at_utc<UTC_TIMESTAMP(6)-INTERVAL 1 DAY;
            """,ct);
        await ClearInactivePartySearchesAsync(c,tx,ct);
        var interrupted=await CommunityEndpoints.Rows(c,tx,"SELECT DISTINCT CAST(o.room_id AS CHAR) roomId FROM random_match_origins o JOIN game_rooms r ON r.id=o.room_id WHERE r.status='Waiting'",ct);
        foreach(var row in interrupted)await RestoreRandomPartiesAsync(c,tx,Convert.ToString(row["roomId"])!,ct);
    }
    internal static async Task<ulong> ChooseMatchMannequinAsync(MySqlConnection c,MySqlTransaction tx,string room,IReadOnlyList<ulong> members,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT m.user_id FROM room_members m JOIN random_match_rooms r ON r.room_id=m.room_id WHERE m.room_id=@r AND m.assigned_role='Mannequin'",ct,("r",room));
        return rows.Count==1&&members.Contains(Convert.ToUInt64(rows[0]["user_id"]))?Convert.ToUInt64(rows[0]["user_id"]):members[RandomNumberGenerator.GetInt32(members.Count)];
    }
    internal static async Task RestoreRandomPartiesAsync(MySqlConnection c,MySqlTransaction tx,string room,CancellationToken ct)
    {
        // Only remaining members are restored. A player who logged out or left is never resurrected.
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT o.user_id,o.party_key,o.original_host,r.host_user_id FROM random_match_origins o JOIN room_members m ON m.user_id=o.user_id AND m.room_id=o.room_id JOIN game_rooms r ON r.id=o.room_id WHERE o.room_id=@r AND r.status='Waiting' ORDER BY o.party_key,o.user_id FOR UPDATE",ct,("r",room));
        if(rows.Count==0)return;
        ulong currentHost=Convert.ToUInt64(rows[0]["host_user_id"]);
        var groups=rows.GroupBy(r=>Convert.ToString(r["party_key"])!).OrderByDescending(g=>g.Any(r=>Convert.ToUInt64(r["user_id"])==currentHost)).ToArray();
        bool first=true;
        foreach(var group in groups)
        {
            var users=group.Select(r=>Convert.ToUInt64(r["user_id"])).ToArray();ulong original=Convert.ToUInt64(group.First()["original_host"]),host=users.Contains(original)?original:users[0];
            string target=room;
            if(first)await CommunityEndpoints.Exec(c,tx,"UPDATE game_rooms SET host_user_id=@u WHERE id=@r",ct,("u",host),("r",room));
            else
            {
                target=Guid.NewGuid().ToString("D");string code=await GenerateUniqueJoinCodeAsync(c,tx,ct);
                await CommunityEndpoints.Exec(c,tx,"INSERT INTO game_rooms(id,join_code,name,host_user_id,is_public) VALUES(@r,@code,@name,@u,FALSE);INSERT INTO random_match_parties(room_id) VALUES(@r)",ct,("r",target),("code",code),("name","랜덤 매칭 "+code),("u",host));
            }
            foreach(ulong user in users)await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET room_id=@target,is_ready=FALSE,assigned_role=NULL,returned_to_room=FALSE WHERE room_id=@r AND user_id=@u",ct,("target",target),("r",room),("u",user));
            first=false;
        }
        await CommunityEndpoints.Exec(c,tx,"DELETE FROM random_match_origins WHERE room_id=@r;DELETE FROM random_match_rooms WHERE room_id=@r;UPDATE random_match_parties SET search_id=NULL WHERE room_id=@r",ct,("r",room));
    }

    // Called only after the authoritative match ledger says the match ended.
    // Solo queue has no persistent group: each returning player gets their own idle staging room.
    // Keep the old room/server/results intact for players still viewing their result screen.
    private static async Task<bool> RestoreReturnedRandomPlayersAsync(MySqlConnection c,MySqlTransaction tx,string room,CancellationToken ct)
    {
        if((await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM random_match_rooms WHERE room_id=@r",ct,("r",room))).Count==0)return false;
        var members=await CommunityEndpoints.Rows(c,tx,"SELECT user_id,returned_to_room FROM room_members WHERE room_id=@r ORDER BY joined_at_utc,user_id FOR UPDATE",ct,("r",room));
        foreach(var member in members.Where(m=>Convert.ToBoolean(m["returned_to_room"])))
        {
            ulong user=Convert.ToUInt64(member["user_id"]);
            string target=Guid.NewGuid().ToString("D"),code=await GenerateUniqueJoinCodeAsync(c,tx,ct);
            // Formation reserved space for all original solos in CountRoomsAsync; returning
            // is not a new room admission and must work even when that limit is full.
            await CommunityEndpoints.Exec(c,tx,"""
                INSERT INTO game_rooms(id,join_code,name,host_user_id,is_public) VALUES(@target,@code,@name,@u,FALSE);
                INSERT INTO random_match_parties(room_id) VALUES(@target);
                UPDATE room_members SET room_id=@target,is_ready=FALSE,assigned_role=NULL,returned_to_room=FALSE,last_seen_at_utc=UTC_TIMESTAMP(6)
                  WHERE room_id=@r AND user_id=@u;
                DELETE FROM game_join_tickets WHERE room_id=@r AND user_id=@u;
                DELETE FROM random_match_origins WHERE room_id=@r AND user_id=@u;
                """,ct,("target",target),("code",code),("name","랜덤 매칭 "+code),("u",user),("r",room));
        }
        var remaining=members.Where(m=>!Convert.ToBoolean(m["returned_to_room"])).ToArray();
        if(remaining.Length>0)
            await CommunityEndpoints.Exec(c,tx,"UPDATE game_rooms SET host_user_id=@u WHERE id=@r",ct,("u",remaining[0]["user_id"]),("r",room));
        else
            await CommunityEndpoints.Exec(c,tx,"DELETE FROM game_rooms WHERE id=@r",ct,("r",room));
        return true;
    }
}

internal sealed class MatchSearchCleanupWorker(AuthDatabase db,ILogger<MatchSearchCleanupWorker> log):BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer=new PeriodicTimer(TimeSpan.FromSeconds(5));
        try{while(await timer.WaitForNextTickAsync(stoppingToken))if(db.IsConfigured)
        {try{await using var c=await db.OpenConnectionAsync(stoppingToken);await LobbyEndpoints.SweepMatchSearchesAsync(c,null,stoppingToken);}catch(MySqlException ex){log.LogWarning("Match search cleanup deferred ({Code}).",ex.Number);}}}
        catch(OperationCanceledException)when(stoppingToken.IsCancellationRequested){}
    }
}
