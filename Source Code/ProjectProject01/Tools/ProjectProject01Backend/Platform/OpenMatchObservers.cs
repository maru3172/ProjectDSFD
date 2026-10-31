using MySqlConnector;

internal sealed record ObserverRoomAction(string? Action,string? UserId=null);
internal static partial class LobbyEndpoints
{
    internal static async Task EnsureObserverSchemaAsync(MySqlConnection c,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,null,"SELECT column_name FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='room_members' AND column_name='is_observer'",ct);
        if(rows.Count==0)await CommunityEndpoints.Exec(c,null,"ALTER TABLE room_members ADD COLUMN is_observer BOOLEAN NOT NULL DEFAULT FALSE",ct);
        // A player the host removed from the room is told so once, on their next room refresh.
        await CommunityEndpoints.Exec(c,null,"CREATE TABLE IF NOT EXISTS room_removal_notices(user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,removed_at_utc DATETIME(6) NOT NULL,FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE)",ct);
    }
    private static void MapOpenObservers(this WebApplication app,Func<bool> available)
    {
        app.MapPost("/api/rooms/current/participants",async(ObserverRoomAction body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(!LocalEditorTesting.ObserversEnabled(req))return Results.Json(new LobbyFailure("오픈매칭 관전을 준비 중입니다."),statusCode:503);
            if(await LocalEditorTesting.EnsureEditorServerAsync(req,available(),ct) is { } editorError)
                return Results.Json(new LobbyFailure(editorError),statusCode:503);
            if(body.Action is not ("observe" or "play" or "kick"))return Results.BadRequest();
            ulong target=user.Id;if(body.UserId is not null&&!ulong.TryParse(body.UserId,out target))return Results.BadRequest();
            try
            {
                await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
                await LockLobbyStateAsync(c,tx,ct);var membership=await LoadMembershipAsync(c,tx,user.Id,true,ct);
                if(membership is null)return Results.NotFound();
                if(membership.Status!="Waiting"||await IsRandomPartyAsync(c,tx,membership.RoomId,ct)||await IsBotRoomAsync(c,tx,membership.RoomId,ct))
                    return Results.Conflict(new LobbyFailure("오픈매칭 대기방에서만 변경할 수 있습니다."));
                if((target!=user.Id||body.Action=="kick")&&!membership.IsHost)return Results.Json(new LobbyFailure("방장만 다른 참가자를 변경할 수 있습니다."),statusCode:403);
                var people=await CommunityEndpoints.Rows(c,tx,"SELECT user_id,is_observer FROM room_members WHERE room_id=@r FOR UPDATE",ct,("r",membership.RoomId));
                var person=people.FirstOrDefault(p=>Convert.ToUInt64(p["user_id"])==target);if(person is null)return Results.NotFound();
                if(body.Action=="kick")
                {
                    if(target==user.Id)return Results.BadRequest(new LobbyFailure("자신은 방 나가기를 사용하세요."));
                    await RemoveUserFromCurrentRoomAsync(c,tx,target,ct);
                    await CommunityEndpoints.Exec(c,tx,"INSERT INTO room_removal_notices(user_id,removed_at_utc) VALUES(@u,UTC_TIMESTAMP(6)) ON DUPLICATE KEY UPDATE removed_at_utc=UTC_TIMESTAMP(6)",ct,("u",target));
                }
                else
                {
                    bool observer=body.Action=="observe",wasObserver=Convert.ToBoolean(person["is_observer"]);
                    if(observer!=wasObserver)
                    {
                        int spectators=people.Count(p=>Convert.ToBoolean(p["is_observer"]));
                        int playing=people.Count-spectators+(await LoadRoomBotsAsync(c,tx,membership.RoomId,ct)).Count;
                        if(observer?spectators>=2:playing>=3)return Results.Conflict(new LobbyFailure(observer?"관전 인원은 최대 2명입니다.":"경기 참가자 자리가 없습니다."));
                    }
                    await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET is_observer=@o,is_ready=FALSE,assigned_role=NULL WHERE room_id=@r AND user_id=@u",ct,("o",observer),("r",membership.RoomId),("u",target));
                }
                await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET is_ready=FALSE WHERE room_id=@r",ct,("r",membership.RoomId));
                await tx.CommitAsync(ct);return Results.Ok(new{message="참가 구성을 변경했습니다.",room=await LoadCurrentRoomAsync(c,null,user.Id,available(),ct)});
            }
            catch(MySqlException){return DatabaseUnavailable();}
        }).RequireRateLimiting("lobby");
    }
}
internal static partial class LobbyEndpoints
{
    // True once (the notice is cleared) when the host removed this player from a room in the last 10 minutes.
    internal static async Task<bool> TakeRoomRemovalNoticeAsync(MySqlConnection c,ulong user,CancellationToken ct)
    {
        int taken=await CommunityEndpoints.Exec(c,null,"DELETE FROM room_removal_notices WHERE user_id=@u AND removed_at_utc>UTC_TIMESTAMP(6)-INTERVAL 10 MINUTE",ct,("u",user));
        await CommunityEndpoints.Exec(c,null,"DELETE FROM room_removal_notices WHERE user_id=@u",ct,("u",user));
        return taken>0;
    }
}
