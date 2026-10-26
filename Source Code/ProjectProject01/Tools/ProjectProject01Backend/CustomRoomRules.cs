using MySqlConnector;

internal sealed record CustomRuleSet(bool Enabled=false,int DeathCount=3,int MannequinCount=10,int TimeLimitSeconds=1800);
internal static class CustomRoomRules
{
    internal static bool Valid(CustomRuleSet? r)=>r is not null&&r.DeathCount is >=1 and <=20&&r.MannequinCount is >=1 and <=10&&r.TimeLimitSeconds is >=0 and <=7200&&r.TimeLimitSeconds%60==0;
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS room_custom_rules(room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          enabled BOOLEAN NOT NULL,death_count INT NOT NULL,mannequin_count INT NOT NULL,time_limit_seconds INT NOT NULL,
          FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS match_custom_rules(match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          enabled BOOLEAN NOT NULL,death_count INT NOT NULL,mannequin_count INT NOT NULL,time_limit_seconds INT NOT NULL);
        CREATE TABLE IF NOT EXISTS room_rule_presets(user_id BIGINT UNSIGNED NOT NULL,name VARCHAR(32) NOT NULL,
          enabled BOOLEAN NOT NULL,death_count INT NOT NULL,mannequin_count INT NOT NULL,time_limit_seconds INT NOT NULL,
          PRIMARY KEY(user_id,name),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
    private static CustomRuleSet From(Dictionary<string,object?> row)=>new(Convert.ToBoolean(row["enabled"]),Convert.ToInt32(row["death_count"]),Convert.ToInt32(row["mannequin_count"]),Convert.ToInt32(row["time_limit_seconds"]));
    internal static async Task<CustomRuleSet> RoomAsync(MySqlConnection c,MySqlTransaction? tx,string id,CancellationToken ct)
    {var rows=await CommunityEndpoints.Rows(c,tx,"SELECT enabled,death_count,mannequin_count,time_limit_seconds FROM room_custom_rules WHERE room_id=@id",ct,("id",id));return rows.Count==0?new():From(rows[0]);}
    internal static async Task<CustomRuleSet> MatchAsync(MySqlConnection c,MySqlTransaction? tx,string id,CancellationToken ct)
    {var rows=await CommunityEndpoints.Rows(c,tx,"SELECT enabled,death_count,mannequin_count,time_limit_seconds FROM match_custom_rules WHERE match_id=@id",ct,("id",id));return rows.Count==0?new():From(rows[0]);}
    internal static async Task<bool> IsCustomAsync(MySqlConnection c,MySqlTransaction? tx,string id,CancellationToken ct)=>(await MatchAsync(c,tx,id,ct)).Enabled;
    internal static string OfficialFilter(string column)=>$"NOT EXISTS (SELECT 1 FROM match_custom_rules cr WHERE cr.match_id={column} AND cr.enabled=TRUE)";
    internal static Task CaptureAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)=>CommunityEndpoints.Exec(c,tx,"""
        INSERT IGNORE INTO match_custom_rules(match_id,enabled,death_count,mannequin_count,time_limit_seconds)
        SELECT r.match_id,c.enabled,c.death_count,c.mannequin_count,c.time_limit_seconds FROM game_rooms r JOIN room_custom_rules c ON c.room_id=r.id
        WHERE r.match_id=@m AND r.status='Started';
        """,ct,("m",match));
    internal static async Task<object[]> PresetsAsync(MySqlConnection c,MySqlTransaction? tx,ulong user,CancellationToken ct)
    {var rows=await CommunityEndpoints.Rows(c,tx,"SELECT name,enabled,death_count,mannequin_count,time_limit_seconds FROM room_rule_presets WHERE user_id=@u ORDER BY name",ct,("u",user));return rows.Select(r=>(object)new{name=(string)r["name"]!,rules=From(r)}).ToArray();}
}

internal static partial class LobbyEndpoints
{
    internal sealed record CustomRulesRequest(string RoomId,CustomRuleSet Rules);
    internal sealed record RulePresetRequest(string Action,string Name,CustomRuleSet? Rules=null);
    private static void MapCustomRoomRules(this WebApplication app,Func<bool> available)
    {
        app.MapGet("/api/rooms/current/custom-rules",async(HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);
            var rooms=await CommunityEndpoints.Rows(c,null,"SELECT r.id,r.status,r.host_user_id FROM game_rooms r JOIN room_members m ON m.room_id=r.id WHERE m.user_id=@u",ct,("u",user.Id));
            if(rooms.Count!=1)return Results.NotFound();var room=rooms[0];string id=Convert.ToString(room["id"])!;
            if(Convert.ToUInt64(room["host_user_id"])!=user.Id)return Results.StatusCode(403);
            bool open=(await CommunityEndpoints.Rows(c,null,"SELECT room_id FROM bot_rooms WHERE room_id=@r UNION ALL SELECT room_id FROM random_match_rooms WHERE room_id=@r UNION ALL SELECT room_id FROM random_match_parties WHERE room_id=@r",ct,("r",id))).Count==0;
            return Results.Ok(new{roomId=id,editable=open&&(string)room["status"]! =="Waiting",rules=await CustomRoomRules.RoomAsync(c,null,id,ct),presets=await CustomRoomRules.PresetsAsync(c,null,user.Id,ct)});
        }).RequireRateLimiting("lobby");
        app.MapPut("/api/rooms/current/custom-rules",async(CustomRulesRequest body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(!Guid.TryParse(body.RoomId,out var id)||!CustomRoomRules.Valid(body.Rules))return Results.BadRequest(new{message="규칙 수치를 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await LockLobbyStateAsync(c,tx,ct);
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT r.host_user_id,r.status FROM game_rooms r JOIN room_members m ON m.room_id=r.id WHERE r.id=@r AND m.user_id=@u FOR UPDATE",ct,("r",id.ToString()),("u",user.Id));
            if(rows.Count!=1)return Results.NotFound();if(Convert.ToUInt64(rows[0]["host_user_id"])!=user.Id)return Results.StatusCode(403);
            if((string)rows[0]["status"]! !="Waiting"||(await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM bot_rooms WHERE room_id=@r UNION ALL SELECT room_id FROM random_match_rooms WHERE room_id=@r UNION ALL SELECT room_id FROM random_match_parties WHERE room_id=@r",ct,("r",id.ToString()))).Count>0)return Results.Conflict(new{message="대기 중인 오픈매칭 방에서만 변경할 수 있습니다."});
            var r=body.Rules;await CommunityEndpoints.Exec(c,tx,"INSERT INTO room_custom_rules(room_id,enabled,death_count,mannequin_count,time_limit_seconds) VALUES(@r,@e,@d,@n,@t) ON DUPLICATE KEY UPDATE enabled=@e,death_count=@d,mannequin_count=@n,time_limit_seconds=@t;UPDATE room_members SET is_ready=FALSE WHERE room_id=@r",ct,("r",id.ToString()),("e",r.Enabled),("d",r.DeathCount),("n",r.MannequinCount),("t",r.TimeLimitSeconds));
            await tx.CommitAsync(ct);return Results.Ok(new{message="규칙을 적용했습니다.",room=await LoadCurrentRoomAsync(c,null,user.Id,available(),ct)});
        }).RequireRateLimiting("lobby");
        app.MapPost("/api/room-rule-presets",async(RulePresetRequest body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            string name=body.Name?.Trim()??"";if(name.Length is <1 or >32||name.Any(char.IsControl)||body.Action is not ("Save" or "Delete")||(body.Action=="Save"&&!CustomRoomRules.Valid(body.Rules)))return Results.BadRequest(new{message="프리셋 이름과 수치를 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            if(body.Action=="Delete")await CommunityEndpoints.Exec(c,tx,"DELETE FROM room_rule_presets WHERE user_id=@u AND name=@n",ct,("u",user.Id),("n",name));
            else
            {
                var saved=await CustomRoomRules.PresetsAsync(c,tx,user.Id,ct);var exists=await CommunityEndpoints.Rows(c,tx,"SELECT name FROM room_rule_presets WHERE user_id=@u AND name=@n",ct,("u",user.Id),("n",name));
                if(saved.Length>=20&&exists.Count==0)return Results.Conflict(new{message="프리셋은 최대 20개입니다."});
                var r=body.Rules!;await CommunityEndpoints.Exec(c,tx,"INSERT INTO room_rule_presets(user_id,name,enabled,death_count,mannequin_count,time_limit_seconds) VALUES(@u,@n,@e,@d,@m,@t) ON DUPLICATE KEY UPDATE enabled=@e,death_count=@d,mannequin_count=@m,time_limit_seconds=@t",ct,("u",user.Id),("n",name),("e",r.Enabled),("d",r.DeathCount),("m",r.MannequinCount),("t",r.TimeLimitSeconds));
            }
            var presets=await CustomRoomRules.PresetsAsync(c,tx,user.Id,ct);await tx.CommitAsync(ct);return Results.Ok(new{message="프리셋을 반영했습니다.",presets});
        }).RequireRateLimiting("lobby");
    }
}
