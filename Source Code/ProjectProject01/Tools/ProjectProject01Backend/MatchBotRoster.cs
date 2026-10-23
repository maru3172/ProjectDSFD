using System.Security.Cryptography;
using MySqlConnector;

// Server-owned participants. Never represented by users/auth_sessions or given voice credentials.
internal sealed record MatchBot(string BotId, string DisplayName, int Difficulty, string AssignedRole);
internal sealed record CreateBotMatchRequest(string? Role, int Difficulty = 2);
internal sealed record EditRoomBotRequest(string? Action,string? BotId=null,int Difficulty=2);

internal static partial class LobbyEndpoints
{
    internal static async Task EnsureMatchBotSchemaAsync(MySqlConnection c, CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c,null,"""
            CREATE TABLE IF NOT EXISTS room_bots(
              bot_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
              room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              difficulty TINYINT UNSIGNED NOT NULL,
              assigned_role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NULL,
              KEY ix_room_bots_room(room_id),
              FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS bot_rooms(
              room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
              human_role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS bot_match_rosters(
              match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              bot_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              display_name VARCHAR(48) NOT NULL,
              difficulty TINYINT UNSIGNED NOT NULL,
              role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
              PRIMARY KEY(match_id,bot_id));
            """,ct);
        var columns=await CommunityEndpoints.Rows(c,null,"SELECT column_name FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='bot_match_rosters' AND column_name='display_name'",ct);
        if(columns.Count==0)await CommunityEndpoints.Exec(c,null,"ALTER TABLE bot_match_rosters ADD COLUMN display_name VARCHAR(48) NOT NULL DEFAULT '봇'",ct);
        await BotSkillRatings.EnsureAsync(c,ct);
        await EnsureObserverSchemaAsync(c,ct);
    }

    private static void MapBotMatches(this WebApplication app,Func<bool> available)
    {
        app.MapOpenObservers(available);
        app.MapPost("/api/bot-matches",async(CreateBotMatchRequest body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(request,db,ct);
            if(user is null)return Results.Unauthorized();
            // Fail closed until dedicated-server execution and admission are wired and verified.
            // A local editor client (loopback) may use the editor dedicated server instead.
            if(!LocalEditorTesting.BotsEnabled(request))
                return Results.Json(new LobbyFailure("봇전 게임 서버 연결을 준비 중입니다."),statusCode:503);
            if(await LocalEditorTesting.EnsureEditorServerAsync(request,available(),ct) is { } editorError)
                return Results.Json(new LobbyFailure(editorError),statusCode:503);
            if(body.Role is not ("Mannequin" or "Survivor") || body.Difficulty is <1 or >5)
                return Results.BadRequest(new LobbyFailure("생존자/마네킹과 1~5단계 난이도를 선택하세요."));
            try
            {
                await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
                int limit=await LockLobbyStateAsync(c,tx,ct);
                await CancelUserSearchesAsync(c,tx,user.Id,ct);await CleanupStaleRoomsAsync(c,tx,ct);
                if(await FindCurrentRoomIdAsync(c,tx,user.Id,ct) is not null)
                    return Results.Conflict(new LobbyFailure("현재 방이나 매칭에서 나온 뒤 봇전을 선택하세요."));
                if(await CountRoomsAsync(c,tx,ct)>=limit)
                    return Results.Conflict(new LobbyFailure("생성 가능한 방이 모두 사용 중입니다."));
                string room=Guid.NewGuid().ToString("D"),code=await GenerateUniqueJoinCodeAsync(c,tx,ct);
                await CommunityEndpoints.Exec(c,tx,"""
                    INSERT INTO game_rooms(id,join_code,name,host_user_id,is_public) VALUES(@r,@code,@name,@u,FALSE);
                    INSERT INTO room_members(room_id,user_id,is_ready) VALUES(@r,@u,FALSE);
                    INSERT INTO bot_rooms(room_id,human_role) VALUES(@r,@role);
                    """,ct,("r",room),("code",code),("name","봇전 "+code),("u",user.Id),("role",body.Role));
                for(int i=0;i<2;++i)
                    await CommunityEndpoints.Exec(c,tx,"INSERT INTO room_bots(bot_id,room_id,difficulty) VALUES(@b,@r,@d)",ct,
                        ("b",Guid.NewGuid().ToString("D")),("r",room),("d",body.Difficulty));
                await tx.CommitAsync(ct);
                return Results.Ok(new {message="봇전 구성을 완료했습니다. 실제 게임 서버가 준비되면 시작할 수 있습니다.",room=await LoadCurrentRoomAsync(c,null,user.Id,available(),ct)});
            }
            catch(MySqlException ex) when(ex.Number==1062){return Results.Conflict(new LobbyFailure("이미 참가 중인 방이 있습니다."));}
            catch(MySqlException){return DatabaseUnavailable();}
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/bots",async(EditRoomBotRequest body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            if(!LocalEditorTesting.BotsEnabled(request))return Results.Json(new LobbyFailure("봇 경기 기능을 준비 중입니다."),statusCode:503);
            if(await LocalEditorTesting.EnsureEditorServerAsync(request,available(),ct) is { } editorError)
                return Results.Json(new LobbyFailure(editorError),statusCode:503);
            if(body.Action is not ("add" or "remove" or "difficulty") || body.Difficulty is <1 or >5 ||
                (body.Action!="add"&&!Guid.TryParse(body.BotId,out _)))return Results.BadRequest(new LobbyFailure("봇과 1~5단계 난이도를 확인하세요."));
            try
            {
                await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
                await LockLobbyStateAsync(c,tx,ct);
                var membership=await LoadMembershipAsync(c,tx,user.Id,true,ct);
                if(membership is null)return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                if(!membership.IsHost)return Results.Json(new LobbyFailure("방장만 봇을 관리할 수 있습니다."),statusCode:403);
                if(membership.Status!="Waiting"||await IsRandomPartyAsync(c,tx,membership.RoomId,ct)||await IsBotRoomAsync(c,tx,membership.RoomId,ct))
                    return Results.Conflict(new LobbyFailure("오픈매칭 대기방에서만 봇을 관리할 수 있습니다."));
                var bots=await LoadRoomBotsAsync(c,tx,membership.RoomId,ct);
                if(body.Action=="add")
                {
                    var people=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE room_id=@r AND is_observer=FALSE",ct,("r",membership.RoomId));
                    if(people.Count+bots.Count>=MaximumPlayers)return Results.Conflict(new LobbyFailure("플레이 인원은 봇을 포함해 최대 3명입니다."));
                    await CommunityEndpoints.Exec(c,tx,"INSERT INTO room_bots(bot_id,room_id,difficulty) VALUES(@b,@r,@d)",ct,
                        ("b",Guid.NewGuid().ToString("D")),("r",membership.RoomId),("d",body.Difficulty));
                }
                else
                {
                    string bot=Guid.Parse(body.BotId!).ToString("D");
                    if(!bots.Any(b=>b.BotId==bot))return Results.NotFound(new LobbyFailure("이 방의 봇이 아닙니다."));
                    await CommunityEndpoints.Exec(c,tx,body.Action=="remove"?"DELETE FROM room_bots WHERE room_id=@r AND bot_id=@b":
                        "UPDATE room_bots SET difficulty=@d WHERE room_id=@r AND bot_id=@b",ct,("r",membership.RoomId),("b",bot),("d",body.Difficulty));
                }
                // Everyone must consent again after a roster/level change.
                await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET is_ready=FALSE WHERE room_id=@r",ct,("r",membership.RoomId));
                await tx.CommitAsync(ct);
                return Results.Ok(new{message="봇 구성을 반영했습니다.",room=await LoadCurrentRoomAsync(c,null,user.Id,available(),ct)});
            }
            catch(MySqlException){return DatabaseUnavailable();}
        }).RequireRateLimiting("lobby");
    }

    internal static async Task<List<MatchBot>> LoadRoomBotsAsync(MySqlConnection c,MySqlTransaction? tx,string room,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT bot_id,difficulty,assigned_role FROM room_bots WHERE room_id=@r ORDER BY bot_id",ct,("r",room));
        if(rows.Count==0)return [];
        // Players already know bots are present before the match; names just should not read as a program.
        var people=await CommunityEndpoints.Rows(c,tx,"SELECT u.display_name FROM room_members m INNER JOIN users u ON u.id=m.user_id WHERE m.room_id=@r",ct,("r",room));
        var names=BotNames.ForRoster(rows.Select(r=>Convert.ToString(r["bot_id"])!),people.Select(p=>Convert.ToString(p["display_name"])!));
        return rows.Select((r,i)=>new MatchBot(Convert.ToString(r["bot_id"])!,names[i],Convert.ToInt32(r["difficulty"]),
            r["assigned_role"] is DBNull?"":Convert.ToString(r["assigned_role"])!)).ToList();
    }
    internal static async Task<bool> IsBotRoomAsync(MySqlConnection c,MySqlTransaction? tx,string room,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM bot_rooms WHERE room_id=@r",ct,("r",room))).Count!=0;

    internal static async Task<bool> HasCompletePlayingRosterAsync(MySqlConnection c,MySqlTransaction? tx,string room,int humans,CancellationToken ct)
    {
        var bots=await LoadRoomBotsAsync(c,tx,room,ct);
        return humans+bots.Count==MaximumPlayers && bots.All(b=>b.Difficulty is >=1 and <=5) &&
            (await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE room_id=@r LIMIT 1",ct,("r",room))).Count>0;
    }

    internal static async Task AssignPlayingRolesAsync(MySqlConnection c,MySqlTransaction tx,string room,IReadOnlyList<ulong> humans,CancellationToken ct)
    {
        var bots=await LoadRoomBotsAsync(c,tx,room,ct);
        if(!await HasCompletePlayingRosterAsync(c,tx,room,humans.Count,ct))throw new InvalidOperationException("Invalid playing roster");
        var chosen=await CommunityEndpoints.Rows(c,tx,"SELECT human_role FROM bot_rooms WHERE room_id=@r",ct,("r",room));
        if((await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM random_match_rooms WHERE room_id=@r",ct,("r",room))).Count>0)
        {
            var assigned=await CommunityEndpoints.Rows(c,tx,"SELECT assigned_role FROM room_members WHERE room_id=@r",ct,("r",room));
            if(assigned.Count(r=>Convert.ToString(r["assigned_role"])=="Mannequin")+bots.Count(b=>b.AssignedRole=="Mannequin")==1 &&
                assigned.All(r=>Convert.ToString(r["assigned_role"]) is "Mannequin" or "Survivor")&&bots.All(b=>b.AssignedRole is "Mannequin" or "Survivor"))
            {await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET returned_to_room=FALSE WHERE room_id=@r",ct,("r",room));return;}
        }
        int mannequin;
        if(chosen.Count!=0)
        {
            if(humans.Count!=1 || bots.Count!=2)throw new InvalidOperationException("Invalid solo bot match");
            mannequin=Convert.ToString(chosen[0]["human_role"])=="Mannequin"?0:1+RandomNumberGenerator.GetInt32(2);
        }
        else if(bots.Count==0)
        {
            ulong chosenHuman=await ChooseMatchMannequinAsync(c,tx,room,humans.ToList(),ct);
            mannequin=humans.ToList().IndexOf(chosenHuman);
        }
        else mannequin=RandomNumberGenerator.GetInt32(MaximumPlayers);
        await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET assigned_role=IF(is_observer,'Observer','Survivor'),returned_to_room=FALSE WHERE room_id=@r;UPDATE room_bots SET assigned_role='Survivor' WHERE room_id=@r",ct,("r",room));
        if(mannequin<humans.Count)
            await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET assigned_role='Mannequin' WHERE room_id=@r AND user_id=@u",ct,("r",room),("u",humans[mannequin]));
        else await CommunityEndpoints.Exec(c,tx,"UPDATE room_bots SET assigned_role='Mannequin' WHERE room_id=@r AND bot_id=@b",ct,("r",room),("b",bots[mannequin-humans.Count].BotId));
    }
    internal static async Task SnapshotMatchBotsAsync(MySqlConnection c,MySqlTransaction tx,string room,string match,CancellationToken ct)
    {
        foreach(var bot in await LoadRoomBotsAsync(c,tx,room,ct))
            await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO bot_match_rosters(match_id,bot_id,display_name,difficulty,role) VALUES(@m,@b,@name,@d,@role)",ct,
                ("m",match),("b",bot.BotId),("name",bot.DisplayName),("d",bot.Difficulty),("role",bot.AssignedRole));
    }
    internal static async Task<List<MatchBot>> LoadMatchBotsAsync(MySqlConnection c,MySqlTransaction? tx,string match,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT bot_id,display_name,difficulty,role FROM bot_match_rosters WHERE match_id=@m ORDER BY bot_id",ct,("m",match));
        return rows.Select(r=>new MatchBot(Convert.ToString(r["bot_id"])!,Convert.ToString(r["display_name"])!,Convert.ToInt32(r["difficulty"]),Convert.ToString(r["role"])!)).ToList();
    }
}
