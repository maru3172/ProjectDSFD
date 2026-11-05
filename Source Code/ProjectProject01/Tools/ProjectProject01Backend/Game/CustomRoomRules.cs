using MySqlConnector;

// Custom game "playground" rules (Docs/CustomGamePlayground.md). Same ranges and combinations as
// Source/ProjectProject01/Public/ProjectCustomMatchRules.h; keep both in step.
/// <summary>One player's handicap (Docs/CustomGamePlayground.md 12.1). Same ranges as FProjectPlaygroundHandicap.</summary>
internal sealed record HandicapEntry(string UserId,int DeathCount=0,int SpeedPct=0,int StaminaPct=0,int GazePct=0,int BodySpeedPct=0,int AttackRangePct=0);
internal sealed record CustomRuleSet(bool Enabled=false,int DeathCount=3,int MannequinCount=10,int TimeLimitSeconds=1800,
    bool ItemCards=false,bool InfiniteLives=false,bool Infection=false,bool Ghosts=false,int HideRevealSeconds=0,
    int GazeLimitSeconds=0,int Darkness=0,int SoundOnlyMeters=0,int EscapeMode=0,bool SpreadingFire=false,
    bool HideAndSeek=false,bool Waves=false,bool RuleVote=false,bool RoleSwap=false)
{
    public string[] AffixIds{get;init;}=[];
    /// <summary>Per player; kept with the room and the match snapshot, never in presets or rule codes.</summary>
    public HandicapEntry[] Handicaps{get;init;}=[];
    internal bool HasPlaygroundRules=>(AffixIds?.Length??0)>0||ItemCards||InfiniteLives||Infection||Ghosts||HideRevealSeconds!=0||GazeLimitSeconds!=0||Darkness!=0||SoundOnlyMeters!=0||EscapeMode!=0||SpreadingFire||(Handicaps?.Length??0)>0||HideAndSeek||Waves||RuleVote||RoleSwap;
    public bool Equals(CustomRuleSet? o)=>o is not null&&Enabled==o.Enabled&&DeathCount==o.DeathCount&&MannequinCount==o.MannequinCount&&TimeLimitSeconds==o.TimeLimitSeconds
        &&ItemCards==o.ItemCards&&InfiniteLives==o.InfiniteLives&&Infection==o.Infection&&Ghosts==o.Ghosts&&HideRevealSeconds==o.HideRevealSeconds
        &&GazeLimitSeconds==o.GazeLimitSeconds&&Darkness==o.Darkness&&SoundOnlyMeters==o.SoundOnlyMeters&&EscapeMode==o.EscapeMode&&SpreadingFire==o.SpreadingFire
        &&HideAndSeek==o.HideAndSeek&&Waves==o.Waves&&RuleVote==o.RuleVote&&RoleSwap==o.RoleSwap
        &&(AffixIds??[]).SequenceEqual(o.AffixIds??[])&&(Handicaps??[]).SequenceEqual(o.Handicaps??[]);
    public override int GetHashCode()=>HashCode.Combine(Enabled,DeathCount,MannequinCount,TimeLimitSeconds,HashCode.Combine(ItemCards,InfiniteLives,Infection,Ghosts,HideRevealSeconds,GazeLimitSeconds,Darkness,HashCode.Combine(SoundOnlyMeters,EscapeMode,SpreadingFire,HideAndSeek,Waves,RuleVote,RoleSwap)),string.Join(',',AffixIds??[]));
}
internal static class CustomRoomRules
{
    internal const int MaxAffixes=6,MaxAffixesPerGroup=3,HideRevealMin=20,HideRevealMax=120,HideRevealStep=5,InfiniteLivesMinTimeLimit=300,InfectionMaxMannequins=8,
        GazeLimitMin=3,GazeLimitMax=15,DarknessMax=3,SoundOnlyMin=4,SoundOnlyMax=15,EscapeModeMax=3,
        MaxHandicaps=4,HandicapDeathMax=2,HandicapSpeedMax=10,HandicapStaminaMax=20,HandicapGazeMax=10,HandicapBodySpeedMax=10,HandicapAttackRangeMax=15,WaveSurviveSeconds=720;
    // A rule may be set only once the game server runs it (same switches as ProjectPlaygroundRules in the game).
    internal const bool AffixesLive=true,ItemCardsLive=true,InfiniteLivesLive=true,InfectionLive=true,GhostsLive=true,HideRevealLive=true,
        GazeLimitLive=true,DarknessLive=true,SoundOnlyLive=true,EscapeModeLive=true,SpreadingFireLive=true,HandicapLive=true,
        HideAndSeekLive=true,WavesLive=true,RuleVoteLive=true,RoleSwapLive=true;
    private const string Columns="enabled,death_count,mannequin_count,time_limit_seconds,infinite_lives,infection,ghosts,hide_reveal_seconds,gaze_limit_seconds,darkness,sound_only_meters,escape_mode,spreading_fire,hide_and_seek,waves,rule_vote,role_swap";
    private const string ValueList="@e,@d,@n,@t,@il,@inf,@g,@h,@gl,@dk,@so,@em,@sf,@hs,@wv,@rv,@rs";
    private const string UpdateList="enabled=@e,death_count=@d,mannequin_count=@n,time_limit_seconds=@t,infinite_lives=@il,infection=@inf,ghosts=@g,hide_reveal_seconds=@h,gaze_limit_seconds=@gl,darkness=@dk,sound_only_meters=@so,escape_mode=@em,spreading_fire=@sf,hide_and_seek=@hs,waves=@wv,rule_vote=@rv,role_swap=@rs";

    /// <summary>Ranges and combinations, regardless of which rules are implemented yet.</summary>
    internal static bool WellFormed(CustomRuleSet? r)
    {
        if(r is null||r.AffixIds is null)return false;
        if(r.DeathCount is <1 or >20||r.MannequinCount is <1 or >10||r.TimeLimitSeconds is <0 or >7200||r.TimeLimitSeconds%60!=0)return false;
        if(!r.Enabled&&r.HasPlaygroundRules)return false;
        if(r.HideRevealSeconds!=0&&(r.HideRevealSeconds<HideRevealMin||r.HideRevealSeconds>HideRevealMax||r.HideRevealSeconds%HideRevealStep!=0))return false;
        if(r.InfiniteLives&&r.TimeLimitSeconds<InfiniteLivesMinTimeLimit)return false; // 0 (unlimited) is also too short here.
        if(r.Ghosts&&(r.Infection||r.InfiniteLives))return false;
        if(r.Infection&&r.MannequinCount>InfectionMaxMannequins)return false;
        if(r.GazeLimitSeconds!=0&&(r.GazeLimitSeconds<GazeLimitMin||r.GazeLimitSeconds>GazeLimitMax))return false;
        if(r.Darkness is <0 or >DarknessMax||r.EscapeMode is <0 or >EscapeModeMax)return false;
        if(r.SoundOnlyMeters!=0&&(r.SoundOnlyMeters<SoundOnlyMin||r.SoundOnlyMeters>SoundOnlyMax))return false;
        if(r.Waves&&r.TimeLimitSeconds!=WaveSurviveSeconds)return false;
        if(r.Handicaps is null||r.Handicaps.Length>MaxHandicaps||r.Handicaps.Select(h=>h?.UserId).Distinct().Count()!=r.Handicaps.Length)return false;
        foreach(var h in r.Handicaps)
            if(h is null||string.IsNullOrEmpty(h.UserId)||h.UserId.Length>32||!h.UserId.All(ch=>char.IsAsciiLetterOrDigit(ch)||ch=='-'||ch=='_')
                ||Math.Abs(h.DeathCount)>HandicapDeathMax||Math.Abs(h.SpeedPct)>HandicapSpeedMax||Math.Abs(h.StaminaPct)>HandicapStaminaMax
                ||Math.Abs(h.GazePct)>HandicapGazeMax||Math.Abs(h.BodySpeedPct)>HandicapBodySpeedMax||Math.Abs(h.AttackRangePct)>HandicapAttackRangeMax)return false;
        if(r.AffixIds.Length>MaxAffixes||r.AffixIds.Distinct().Count()!=r.AffixIds.Length||!r.AffixIds.All(id=>MatchAffixes.Playable.Contains(id)))return false;
        return r.AffixIds.GroupBy(id=>id[0]).All(g=>g.Count()<=MaxAffixesPerGroup);
    }
    /// <summary>Well formed and only uses rules the game server already runs.</summary>
    internal static bool Valid(CustomRuleSet? r)=>WellFormed(r)&&(AffixesLive||r!.AffixIds.Length==0)&&(ItemCardsLive||!r!.ItemCards)&&(InfiniteLivesLive||!r!.InfiniteLives)
        &&(InfectionLive||!r!.Infection)&&(GhostsLive||!r!.Ghosts)&&(HideRevealLive||r!.HideRevealSeconds==0)
        &&(GazeLimitLive||r!.GazeLimitSeconds==0)&&(DarknessLive||r!.Darkness==0)&&(SoundOnlyLive||r!.SoundOnlyMeters==0)&&(EscapeModeLive||r!.EscapeMode==0)&&(SpreadingFireLive||!r!.SpreadingFire)&&(HandicapLive||r!.Handicaps.Length==0)
        &&(HideAndSeekLive||!r!.HideAndSeek)&&(WavesLive||!r!.Waves)&&(RuleVoteLive||!r!.RuleVote)&&(RoleSwapLive||!r!.RoleSwap);

    internal static async Task EnsureAsync(MySqlConnection c,CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS room_custom_rules(room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          enabled BOOLEAN NOT NULL,death_count INT NOT NULL,mannequin_count INT NOT NULL,time_limit_seconds INT NOT NULL,
          FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS match_custom_rules(match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          enabled BOOLEAN NOT NULL,death_count INT NOT NULL,mannequin_count INT NOT NULL,time_limit_seconds INT NOT NULL);
        CREATE TABLE IF NOT EXISTS room_rule_presets(user_id BIGINT UNSIGNED NOT NULL,name VARCHAR(32) NOT NULL,
          enabled BOOLEAN NOT NULL,death_count INT NOT NULL,mannequin_count INT NOT NULL,time_limit_seconds INT NOT NULL,
          PRIMARY KEY(user_id,name),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
        // Playground columns (2026-11-05). Defaults keep existing rooms, snapshots and presets valid as "rules off".
        foreach(var table in new[]{"room_custom_rules","match_custom_rules","room_rule_presets"})
            foreach(var (column,definition) in new[]{("infinite_lives","BOOLEAN NOT NULL DEFAULT FALSE"),("infection","BOOLEAN NOT NULL DEFAULT FALSE"),("ghosts","BOOLEAN NOT NULL DEFAULT FALSE"),("hide_reveal_seconds","INT NOT NULL DEFAULT 0"),
                ("gaze_limit_seconds","INT NOT NULL DEFAULT 0"),("darkness","INT NOT NULL DEFAULT 0"),("sound_only_meters","INT NOT NULL DEFAULT 0"),("escape_mode","INT NOT NULL DEFAULT 0"),("spreading_fire","BOOLEAN NOT NULL DEFAULT FALSE"),
                ("hide_and_seek","BOOLEAN NOT NULL DEFAULT FALSE"),("waves","BOOLEAN NOT NULL DEFAULT FALSE"),("rule_vote","BOOLEAN NOT NULL DEFAULT FALSE"),("role_swap","BOOLEAN NOT NULL DEFAULT FALSE")})
            {
                var found=await CommunityEndpoints.Rows(c,null,"SELECT column_name FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name=@t AND column_name=@c",ct,("t",table),("c",column));
                if(found.Count==0)await CommunityEndpoints.Exec(c,null,$"ALTER TABLE {table} ADD COLUMN {column} {definition}",ct);
            }
        // Rule codes: the code table and the code each match snapshot was played with (empty: no custom rules).
        await RuleCodes.EnsureAsync(c,ct);
        {
            var found=await CommunityEndpoints.Rows(c,null,"SELECT column_name FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='match_custom_rules' AND column_name='rule_code'",ct);
            if(found.Count==0)await CommunityEndpoints.Exec(c,null,"ALTER TABLE match_custom_rules ADD COLUMN rule_code VARCHAR(12) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '', ADD INDEX ix_match_custom_rules_code(rule_code)",ct);
        }
        // Presets also keep the mix switches (rooms and matches keep them in the affix set and match mode).
        foreach(var (column,definition) in new[]{("affix_ids","VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT ''"),("item_cards","BOOLEAN NOT NULL DEFAULT FALSE")})
        {
            var found=await CommunityEndpoints.Rows(c,null,"SELECT column_name FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='room_rule_presets' AND column_name=@c",ct,("c",column));
            if(found.Count==0)await CommunityEndpoints.Exec(c,null,$"ALTER TABLE room_rule_presets ADD COLUMN {column} {definition}",ct);
        }
        // Personal handicaps: rooms and match snapshots only (they name players, so presets and codes never hold them).
        foreach(var table in new[]{"room_custom_rules","match_custom_rules"})
        {
            var found=await CommunityEndpoints.Rows(c,null,"SELECT column_name FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name=@t AND column_name='handicaps_json'",ct,("t",table));
            if(found.Count==0)await CommunityEndpoints.Exec(c,null,$"ALTER TABLE {table} ADD COLUMN handicaps_json VARCHAR(1024) NOT NULL DEFAULT '[]'",ct);
        }
    }
    // Mix switches live where the ticket already reads them: affixes in the room/match affix set (mode Custom) and item
    // cards in the room/match mode (ItemRoguelike). They count only while the custom rules are on; otherwise those tables
    // belong to bot rooms and the separate PvP mode choice.
    private static readonly System.Text.Json.JsonSerializerOptions HandicapJson=new(System.Text.Json.JsonSerializerDefaults.Web);
    private static HandicapEntry[] ReadHandicaps(Dictionary<string,object?> row)
    {
        if(!row.TryGetValue("handicaps_json",out var raw)||raw is null or DBNull)return [];
        try{return System.Text.Json.JsonSerializer.Deserialize<HandicapEntry[]>(Convert.ToString(raw)!,HandicapJson)??[];}catch(System.Text.Json.JsonException){return [];}
    }
    private static CustomRuleSet From(Dictionary<string,object?> row,string[] affixIds,bool itemCards)
    {
        bool enabled=Convert.ToBoolean(row["enabled"]);
        return new(enabled,Convert.ToInt32(row["death_count"]),Convert.ToInt32(row["mannequin_count"]),Convert.ToInt32(row["time_limit_seconds"]),
            enabled&&itemCards,Convert.ToBoolean(row["infinite_lives"]),Convert.ToBoolean(row["infection"]),Convert.ToBoolean(row["ghosts"]),Convert.ToInt32(row["hide_reveal_seconds"]),
            Convert.ToInt32(row["gaze_limit_seconds"]),Convert.ToInt32(row["darkness"]),Convert.ToInt32(row["sound_only_meters"]),Convert.ToInt32(row["escape_mode"]),Convert.ToBoolean(row["spreading_fire"]),
            Convert.ToBoolean(row["hide_and_seek"]),Convert.ToBoolean(row["waves"]),Convert.ToBoolean(row["rule_vote"]),Convert.ToBoolean(row["role_swap"])){AffixIds=enabled?affixIds:[],Handicaps=enabled?ReadHandicaps(row):[]};
    }
    private static (string,object?)[] Values(CustomRuleSet r)=>[("e",r.Enabled),("d",r.DeathCount),("n",r.MannequinCount),("t",r.TimeLimitSeconds),("il",r.InfiniteLives),("inf",r.Infection),("g",r.Ghosts),("h",r.HideRevealSeconds),
        ("gl",r.GazeLimitSeconds),("dk",r.Darkness),("so",r.SoundOnlyMeters),("em",r.EscapeMode),("sf",r.SpreadingFire),
        ("hs",r.HideAndSeek),("wv",r.Waves),("rv",r.RuleVote),("rs",r.RoleSwap)];
    private static string[] CustomAffixes(AffixSet set)=>set.Mode=="Custom"?set.List:[];
    internal static async Task<CustomRuleSet> RoomAsync(MySqlConnection c,MySqlTransaction? tx,string id,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,$"SELECT {Columns},handicaps_json FROM room_custom_rules WHERE room_id=@id",ct,("id",id));if(rows.Count==0)return new();
        return From(rows[0],CustomAffixes(await MatchAffixes.RoomAsync(c,tx,id,ct)),await MatchModes.RoomAsync(c,tx,id,ct)==MatchModes.ItemRoguelike);
    }
    internal static async Task<CustomRuleSet> MatchAsync(MySqlConnection c,MySqlTransaction? tx,string id,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,$"SELECT {Columns},handicaps_json FROM match_custom_rules WHERE match_id=@id",ct,("id",id));if(rows.Count==0)return new();
        return From(rows[0],CustomAffixes(await MatchAffixes.MatchAsync(c,tx,id,ct)),await MatchModes.MatchAsync(c,tx,id,ct)==MatchModes.ItemRoguelike);
    }
    internal static async Task<bool> IsCustomAsync(MySqlConnection c,MySqlTransaction? tx,string id,CancellationToken ct)=>(await MatchAsync(c,tx,id,ct)).Enabled||await MatchAffixes.MatchModeAsync(c,tx,id,ct)=="Custom"||await MatchModes.IsUnofficialAsync(c,tx,id,ct);
    internal static string OfficialFilter(string column)=>$"NOT EXISTS (SELECT 1 FROM match_custom_rules cr WHERE cr.match_id={column} AND cr.enabled=TRUE) AND NOT EXISTS (SELECT 1 FROM match_affixes ma WHERE ma.match_id={column} AND ma.mode='Custom') AND {MatchModes.OfficialFilter(column)}";
    internal static Task CaptureAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)=>CommunityEndpoints.Exec(c,tx,$"""
        INSERT IGNORE INTO match_custom_rules(match_id,{Columns},handicaps_json)
        SELECT r.match_id,c.enabled,c.death_count,c.mannequin_count,c.time_limit_seconds,c.infinite_lives,c.infection,c.ghosts,c.hide_reveal_seconds,c.gaze_limit_seconds,c.darkness,c.sound_only_meters,c.escape_mode,c.spreading_fire,c.hide_and_seek,c.waves,c.rule_vote,c.role_swap,c.handicaps_json
        FROM game_rooms r JOIN room_custom_rules c ON c.room_id=r.id
        WHERE r.match_id=@m AND r.status='Started';
        """,ct,("m",match));
    internal static async Task<object[]> PresetsAsync(MySqlConnection c,MySqlTransaction? tx,ulong user,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,$"SELECT name,{Columns},affix_ids,item_cards FROM room_rule_presets WHERE user_id=@u ORDER BY name",ct,("u",user));
        return rows.Select(r=>(object)new{name=(string)r["name"]!,rules=From(r,Convert.ToString(r["affix_ids"])!.Split(',',StringSplitOptions.RemoveEmptyEntries),Convert.ToBoolean(r["item_cards"]))}).ToArray();
    }
    /// <summary>One apply writes the rules, the room's affix set and its match mode together (callers hold the transaction).</summary>
    internal static async Task SaveRoomAsync(MySqlConnection c,MySqlTransaction tx,string room,CustomRuleSet r,CancellationToken ct)
    {
        string handicaps=System.Text.Json.JsonSerializer.Serialize(r.Enabled?r.Handicaps:[],HandicapJson);
        await CommunityEndpoints.Exec(c,tx,$"INSERT INTO room_custom_rules(room_id,{Columns},handicaps_json) VALUES(@r,{ValueList},@hc) ON DUPLICATE KEY UPDATE {UpdateList},handicaps_json=@hc;UPDATE room_members SET is_ready=FALSE WHERE room_id=@r",ct,[("r",(object?)room),..Values(r),("hc",handicaps)]);
        // Open rooms have no affixes of their own, so turning the custom rules off clears them. The match mode is also a
        // separate PvP choice (item roguelike attraction) and is left as it is when the custom rules are off.
        await MatchAffixes.SetRoomAsync(c,tx,room,r.Enabled&&r.AffixIds.Length>0?"Custom":"None",r.Enabled?r.AffixIds:[],"",ct);
        if(r.Enabled)await MatchModes.SetRoomAsync(c,tx,room,r.ItemCards?MatchModes.ItemRoguelike:MatchModes.Normal,ct);
    }
    internal static Task SavePresetAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string name,CustomRuleSet r,CancellationToken ct)=>
        CommunityEndpoints.Exec(c,tx,$"INSERT INTO room_rule_presets(user_id,name,{Columns},affix_ids,item_cards) VALUES(@u,@pn,{ValueList},@ai,@ic) ON DUPLICATE KEY UPDATE {UpdateList},affix_ids=@ai,item_cards=@ic",ct,
            [("u",(object?)user),("pn",(object?)name),..Values(r),("ai",string.Join(',',r.AffixIds)),("ic",r.ItemCards)]);
}

internal static partial class LobbyEndpoints
{
    internal sealed record CustomRulesRequest(string RoomId,CustomRuleSet Rules);
    internal sealed record RulePresetRequest(string Action,string Name,CustomRuleSet? Rules=null);
    internal sealed record RulePresetDeleteRequest(string?[]? Names);
    internal static void MapCustomRoomRules(this WebApplication app,Func<bool> available)
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
            if(body.Rules.Handicaps.Length>0)
            {
                var members=(await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE room_id=@r",ct,("r",id.ToString()))).Select(m=>Convert.ToString(m["user_id"])).ToHashSet();
                if(body.Rules.Handicaps.Any(h=>!members.Contains(h.UserId)))return Results.BadRequest(new{message="핸디캡은 이 방의 참가자에게만 줄 수 있습니다."});
            }
            await CustomRoomRules.SaveRoomAsync(c,tx,id.ToString(),body.Rules,ct);
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
                await CustomRoomRules.SavePresetAsync(c,tx,user.Id,name,body.Rules!,ct);
            }
            var presets=await CustomRoomRules.PresetsAsync(c,tx,user.Id,ct);await tx.CommitAsync(ct);return Results.Ok(new{message="프리셋을 반영했습니다.",presets});
        }).RequireRateLimiting("lobby");
        // Several saved presets at once (the "my presets" overview). Board posts are not touched.
        app.MapPost("/api/room-rule-presets/delete",async(RulePresetDeleteRequest body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            var names=(body.Names??[]).Select(n=>(n??"").Trim()).Where(n=>n.Length is >=1 and <=32).Distinct().ToArray();
            if(names.Length is <1 or >20)return Results.BadRequest(new{message="지울 프리셋(1~20개)을 고르세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            int removed=0;foreach(var n in names)removed+=await CommunityEndpoints.Exec(c,tx,"DELETE FROM room_rule_presets WHERE user_id=@u AND name=@n",ct,("u",user.Id),("n",n));
            var presets=await CustomRoomRules.PresetsAsync(c,tx,user.Id,ct);await tx.CommitAsync(ct);
            return Results.Ok(new{message=$"프리셋 {removed}개를 지웠습니다. 게시판 글은 그대로입니다.",presets});
        }).RequireRateLimiting("lobby");
    }
}
