using System.Security.Cryptography;
using MySqlConnector;

// Bot match affixes (Docs/멀티플레이_어픽스와_탐사_퀘스트_기획초안.md). The backend owns the set: Master+/Master++ are rolled
// here for every match, the weekly set comes from weekly_affixes (or a built-in rotation), and the game server only
// accepts what the match ticket carries. Ids must match Source/ProjectProject01/Public/ProjectAffixes.h.
internal sealed record AffixSet(string Mode="None",string[]? Ids=null,string WeekName="")
{
    internal string[] List=>Ids??[];
}
internal sealed record WeeklyAffixes(string Name,string[] Ids,DateTime WeekStartUtc);

internal static class MatchAffixes
{
    // Implemented on the game server and its bots. Add an id here only together with the game side.
    internal static readonly string[] PlayableSurvivor=["A01","A02","A03","A04","A05","A06","A07","A08","A09","A10"],
        PlayableMannequin=["B01","B02","B03","B04","B05","B06","B07","B08","B09","B10"],
        PlayableEnvironment=["C01","C02","C03","C04","C05","C06","C07","C08","C09","C10"];
    internal static IEnumerable<string> Playable=>PlayableSurvivor.Concat(PlayableMannequin).Concat(PlayableEnvironment);
    // Weekly rotation used when no operator set exists for the week: name + three ids.
    private static readonly (string Name,string[] Ids)[] Rotation=[
        ("러시아워 주간",["A02","B09","C05"]),("소리 없는 매장",["A08","B02","C10"]),("시선 싸움",["A09","B07","C01"]),
        ("마감 직전",["A10","B10","C06"]),("교대 근무",["A01","B06","C04"]),("흔들리는 진열",["A06","B08","C08"]),
        ("탁한 공기 주간",["A04","B01","C02"]),("폭주",["A03","B04","C09"]),("잔상 추적",["A05","B05","C03"]),("공유된 표식",["A07","B03","C07"])];

    internal static bool ModeAllowed(string? mode,int difficulty)=>mode switch
    {
        null or "None"=>true,
        "MasterPlus" or "MasterPlusPlus"=>difficulty==5,
        "Weekly" or "Custom"=>difficulty is >=1 and <=5,
        _=>false
    };
    internal static bool ValidCustom(string[]? ids)=>ids is {Length:>0}&&ids.Distinct().Count()==ids.Length&&ids.All(id=>Playable.Contains(id));
    internal static string[] Roll(string mode)
    {
        string Pick(string[] from)=>from[RandomNumberGenerator.GetInt32(from.Length)];
        return mode switch
        {
            "MasterPlus"=>[Pick(PlayableEnvironment)],
            "MasterPlusPlus"=>[Pick(PlayableSurvivor),Pick(PlayableMannequin),Pick(PlayableEnvironment)],
            _=>[]
        };
    }
    // Weeks start Monday 00:00 UTC.
    internal static DateTime WeekStart(DateTime utc){var d=utc.Date;int back=((int)d.DayOfWeek+6)%7;return d.AddDays(-back);}
    internal static async Task<WeeklyAffixes> WeeklyAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var now=await CommunityEndpoints.Rows(c,tx,"SELECT UTC_TIMESTAMP(6) utc",ct);
        var start=WeekStart(DateTime.SpecifyKind(Convert.ToDateTime(now[0]["utc"]),DateTimeKind.Utc));
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT name,ids FROM weekly_affixes WHERE week_start=@w",ct,("w",start));
        if(rows.Count==1)
        {
            var ids=Convert.ToString(rows[0]["ids"])!.Split(',',StringSplitOptions.RemoveEmptyEntries);
            if(ids.Length==3&&ValidCustom(ids))return new(Convert.ToString(rows[0]["name"])!,ids,start);
        }
        var pick=Rotation[(int)((start-new DateTime(2026,1,5,0,0,0,DateTimeKind.Utc)).TotalDays/7%Rotation.Length+Rotation.Length)%Rotation.Length];
        return new(pick.Name,pick.Ids,start);
    }

    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS room_affixes(room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          mode VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,ids VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          week_name VARCHAR(32) NOT NULL DEFAULT '',
          FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS match_affixes(match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          mode VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,ids VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          week_name VARCHAR(32) NOT NULL DEFAULT '');
        CREATE TABLE IF NOT EXISTS weekly_affixes(week_start DATE PRIMARY KEY,name VARCHAR(32) NOT NULL,
          ids VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL);
        CREATE TABLE IF NOT EXISTS weekly_affix_rewards(user_id BIGINT UNSIGNED NOT NULL,week_start DATE NOT NULL,difficulty TINYINT NOT NULL,
          xp INT NOT NULL,credits INT NOT NULL,match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,granted_at DATETIME(6) NOT NULL,
          PRIMARY KEY(user_id,week_start),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);

    private static AffixSet From(Dictionary<string,object?> row)=>new(Convert.ToString(row["mode"])!,
        Convert.ToString(row["ids"])!.Split(',',StringSplitOptions.RemoveEmptyEntries),Convert.ToString(row["week_name"])??"");
    internal static async Task<AffixSet> RoomAsync(MySqlConnection c,MySqlTransaction? tx,string room,CancellationToken ct)
    {var rows=await CommunityEndpoints.Rows(c,tx,"SELECT mode,ids,week_name FROM room_affixes WHERE room_id=@r",ct,("r",room));return rows.Count==0?new():From(rows[0]);}
    internal static async Task<AffixSet> MatchAsync(MySqlConnection c,MySqlTransaction? tx,string match,CancellationToken ct)
    {var rows=await CommunityEndpoints.Rows(c,tx,"SELECT mode,ids,week_name FROM match_affixes WHERE match_id=@m",ct,("m",match));return rows.Count==0?new():From(rows[0]);}
    internal static async Task<string> MatchModeAsync(MySqlConnection c,MySqlTransaction? tx,string match,CancellationToken ct)=>(await MatchAsync(c,tx,match,ct)).Mode;
    // Any affix changes the match, so bot growth and skill ratings learn only from plain matches.
    internal static async Task<bool> HasAffixesAsync(MySqlConnection c,MySqlTransaction? tx,string match,CancellationToken ct)=>(await MatchModeAsync(c,tx,match,ct))!="None";

    internal static Task SetRoomAsync(MySqlConnection c,MySqlTransaction tx,string room,string mode,string[] ids,string weekName,CancellationToken ct)=>
        CommunityEndpoints.Exec(c,tx,"INSERT INTO room_affixes(room_id,mode,ids,week_name) VALUES(@r,@m,@i,@w) ON DUPLICATE KEY UPDATE mode=@m,ids=@i,week_name=@w",ct,
            ("r",room),("m",mode),("i",string.Join(',',ids)),("w",weekName));
    // At match start: Master+/Master++ roll a fresh set for this match (the room shows it too), the weekly set is
    // re-read so a week change between matches takes effect, and the result is frozen for the match.
    internal static async Task CaptureAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)
    {
        var rooms=await CommunityEndpoints.Rows(c,tx,"SELECT r.id FROM game_rooms r JOIN room_affixes a ON a.room_id=r.id WHERE r.match_id=@m AND r.status='Started'",ct,("m",match));
        if(rooms.Count!=1)return;string room=Convert.ToString(rooms[0]["id"])!;
        var set=await RoomAsync(c,tx,room,ct);string[] ids=set.List;string week=set.WeekName;
        if(set.Mode is "MasterPlus" or "MasterPlusPlus")ids=Roll(set.Mode);
        else if(set.Mode=="Weekly"){var weekly=await WeeklyAsync(c,tx,ct);ids=weekly.Ids;week=weekly.Name;}
        await SetRoomAsync(c,tx,room,set.Mode,ids,week,ct);
        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO match_affixes(match_id,mode,ids,week_name) VALUES(@m,@mode,@i,@w)",ct,
            ("m",match),("mode",set.Mode),("i",string.Join(',',ids)),("w",week));
    }
    // Reward scale on top of the bot-match reward. Custom affix matches give nothing (they count as custom matches).
    internal static double RewardScale(string mode)=>mode switch{"MasterPlus"=>1.25,"MasterPlusPlus"=>1.5,"Weekly"=>2.0,_=>1.0};
    // Weekly bonus: the first weekly-affix win of each week pays this once, on top of the match reward (bot difficulty 1-5).
    internal static (int Xp,int Credits) WeeklyBonus(int difficulty)=>(400*Math.Clamp(difficulty,1,5),200*Math.Clamp(difficulty,1,5));
    internal static async Task<bool> GrantWeeklyBonusAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string match,int difficulty,CancellationToken ct)
    {
        var week=(await WeeklyAsync(c,tx,ct)).WeekStartUtc;var bonus=WeeklyBonus(difficulty);
        if(await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO weekly_affix_rewards(user_id,week_start,difficulty,xp,credits,match_id,granted_at) VALUES(@u,@w,@d,@x,@p,@m,UTC_TIMESTAMP(6))",ct,("u",user),("w",week),("d",Math.Clamp(difficulty,1,5)),("x",bonus.Xp),("p",bonus.Credits),("m",match))==0)return false;
        await CommunityEndpoints.Exec(c,tx,"UPDATE case_accounts SET xp=LEAST(1000000000,xp+@x),credits=LEAST(1000000000,credits+@p) WHERE user_id=@u",ct,("x",bonus.Xp),("p",bonus.Credits),("u",user));
        return true;
    }
    /** This week's bonus for the player: received or not, and what it paid. */
    internal static async Task<object> WeeklyRewardAsync(MySqlConnection c,MySqlTransaction? tx,ulong user,CancellationToken ct)
    {
        var week=(await WeeklyAsync(c,tx,ct)).WeekStartUtc;
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT difficulty,xp,credits FROM weekly_affix_rewards WHERE user_id=@u AND week_start=@w",ct,("u",user),("w",week));
        return rows.Count==0?new{received=false,difficulty=0,xp=0,credits=0}:new{received=true,difficulty=Convert.ToInt32(rows[0]["difficulty"]),xp=Convert.ToInt32(rows[0]["xp"]),credits=Convert.ToInt32(rows[0]["credits"])};
    }

    internal static void Map(WebApplication app)
    {
        app.MapGet("/api/affixes/weekly",async(HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            if(await LobbyEndpoints.AuthenticateAsync(request,db,ct) is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);
            var weekly=await WeeklyAsync(c,null,ct);
            return Results.Ok(new{name=weekly.Name,ids=weekly.Ids,weekStartUtc=weekly.WeekStartUtc.ToString("yyyy-MM-dd")});
        }).RequireRateLimiting("lobby");
    }
}
