using System.Security.Cryptography;
using System.Text;
using MySqlConnector;

// Custom game rule codes (Docs/CustomGamePlayground.md 6). A code names one rule set: the same rules always give the
// same code, so a code is shared by everyone who plays those rules. Per-code records are a plain result summary
// (games, wins per side, my results); no rankings, and nothing here touches the official records.
internal static class RuleCodes
{
    // Crockford Base32 without I, L, O, U, so codes are easy to read aloud and type.
    private const string Alphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    internal const int MinLength=8,MaxLength=12;

    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS rule_codes(code VARCHAR(12) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          rules_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
          rules_json VARCHAR(1024) NOT NULL,created_at DATETIME(6) NOT NULL);
        """,ct);

    /// <summary>One canonical text per rule set: field order fixed, affix ids sorted.</summary>
    internal static string Canonical(CustomRuleSet r)=>string.Join('|',"v3",r.Enabled?1:0,r.DeathCount,r.MannequinCount,r.TimeLimitSeconds,
        string.Join(',',(r.AffixIds??[]).OrderBy(id=>id,StringComparer.Ordinal)),r.ItemCards?1:0,r.InfiniteLives?1:0,r.Infection?1:0,r.Ghosts?1:0,r.HideRevealSeconds,
        r.GazeLimitSeconds,r.Darkness,r.SoundOnlyMeters,r.EscapeMode,r.SpreadingFire?1:0,
        r.HideAndSeek?1:0,r.Waves?1:0,r.RuleVote?1:0,r.RoleSwap?1:0);
    internal static string Hash(CustomRuleSet r)=>Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Canonical(r)))).ToLowerInvariant();
    internal static string Encode(string hexHash,int length)
    {
        var bytes=Convert.FromHexString(hexHash);var text=new StringBuilder(length);int bit=0;
        for(int i=0;i<length;i++,bit+=5)
        {
            int value=0;for(int b=0;b<5;b++){int at=bit+b;value=(value<<1)|((bytes[at/8]>>(7-at%8))&1);}
            text.Append(Alphabet[value]);
        }
        return text.ToString();
    }
    /// <summary>"zb7k-2qxm" and "ZB7K2QXM" are the same code; anything outside the alphabet is not a code.</summary>
    internal static string? Normalize(string? input)
    {
        if(input is null)return null;
        var text=input.Replace("-","").Replace(" ","").ToUpperInvariant();
        return text.Length is >=MinLength and <=MaxLength&&text.All(ch=>Alphabet.Contains(ch))?text:null;
    }
    internal static string Display(string code)=>code.Length>4?code[..4]+"-"+code[4..]:code;

    /// <summary>The code for these rules, created on first use. Callers pass rules that passed CustomRoomRules.Valid.</summary>
    internal static async Task<string> EnsureCodeAsync(MySqlConnection c,MySqlTransaction? tx,CustomRuleSet rules,CancellationToken ct)
    {
        rules=rules with{Handicaps=[]}; // Handicaps name players; a code is only the rules.
        string hash=Hash(rules);
        var known=await CommunityEndpoints.Rows(c,tx,"SELECT code FROM rule_codes WHERE rules_hash=@h",ct,("h",hash));
        if(known.Count>0)return Convert.ToString(known[0]["code"])!;
        string json=System.Text.Json.JsonSerializer.Serialize(rules,new System.Text.Json.JsonSerializerOptions(System.Text.Json.JsonSerializerDefaults.Web));
        for(int length=MinLength;length<=MaxLength;length++)
        {
            string code=Encode(hash,length);
            // INSERT IGNORE: a code already taken by other rules (or a racing insert) moves on to a longer code.
            await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO rule_codes(code,rules_hash,rules_json,created_at) VALUES(@c,@h,@j,UTC_TIMESTAMP(6))",ct,("c",code),("h",hash),("j",json));
            var row=await CommunityEndpoints.Rows(c,tx,"SELECT code FROM rule_codes WHERE rules_hash=@h",ct,("h",hash));
            if(row.Count>0)return Convert.ToString(row[0]["code"])!;
        }
        throw new InvalidOperationException("No free rule code.");
    }
    internal static async Task<CustomRuleSet?> RulesAsync(MySqlConnection c,MySqlTransaction? tx,string code,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT rules_json FROM rule_codes WHERE code=@c",ct,("c",code));
        if(rows.Count==0)return null;
        return System.Text.Json.JsonSerializer.Deserialize<CustomRuleSet>(Convert.ToString(rows[0]["rules_json"])!,new System.Text.Json.JsonSerializerOptions(System.Text.Json.JsonSerializerDefaults.Web));
    }

    /// <summary>Plain summary of finished matches played with this code. "Mine" only ever describes the caller.</summary>
    internal static async Task<object> SummaryAsync(MySqlConnection c,string code,ulong user,CancellationToken ct)
    {
        var all=await CommunityEndpoints.Rows(c,null,"""
            SELECT COUNT(DISTINCT r.match_id) AS games,
              COUNT(DISTINCT CASE WHEN r.role='Survivor' AND r.success=TRUE THEN r.match_id END) AS survivor_wins,
              COUNT(DISTINCT CASE WHEN r.role='Mannequin' AND r.success=TRUE THEN r.match_id END) AS mannequin_wins
            FROM match_custom_rules m JOIN verified_match_results r ON r.match_id=m.match_id WHERE m.rule_code=@c
            """,ct,("c",code));
        var mine=await CommunityEndpoints.Rows(c,null,"""
            SELECT COUNT(*) AS games,COALESCE(SUM(r.success),0) AS wins
            FROM match_custom_rules m JOIN verified_match_results r ON r.match_id=m.match_id WHERE m.rule_code=@c AND r.user_id=@u
            """,ct,("c",code),("u",user));
        int Get(List<Dictionary<string,object?>> rows,string key)=>rows.Count==0||rows[0][key] is null or DBNull?0:Convert.ToInt32(rows[0][key]);
        return new{games=Get(all,"games"),survivorWins=Get(all,"survivor_wins"),mannequinWins=Get(all,"mannequin_wins"),myGames=Get(mine,"games"),myWins=Get(mine,"wins")};
    }

    /// <summary>At match start: the snapshot gets the code of the room's rules (custom rules on only).</summary>
    internal static async Task CaptureAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)
    {
        var rooms=await CommunityEndpoints.Rows(c,tx,"SELECT id FROM game_rooms WHERE match_id=@m AND status='Started'",ct,("m",match));
        if(rooms.Count!=1)return;
        var rules=await CustomRoomRules.RoomAsync(c,tx,Convert.ToString(rooms[0]["id"])!,ct);
        if(!rules.Enabled||!CustomRoomRules.Valid(rules))return;
        string code=await EnsureCodeAsync(c,tx,rules,ct);
        await CommunityEndpoints.Exec(c,tx,"UPDATE match_custom_rules SET rule_code=@c WHERE match_id=@m AND rule_code=''",ct,("c",code),("m",match));
    }
}

internal static partial class LobbyEndpoints
{
    internal sealed record RuleCodeRequest(CustomRuleSet Rules);
    internal static void MapRuleCodes(this WebApplication app)
    {
        app.MapPost("/api/rule-codes",async(RuleCodeRequest body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(body.Rules is null||!body.Rules.Enabled||!CustomRoomRules.Valid(body.Rules))return Results.BadRequest(new{message="사용자 지정을 켠 올바른 규칙만 코드로 만들 수 있습니다."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            string code=await RuleCodes.EnsureCodeAsync(c,tx,body.Rules,ct);await tx.CommitAsync(ct);
            return Results.Ok(new{code=RuleCodes.Display(code),summary=await RuleCodes.SummaryAsync(c,code,user.Id,ct)});
        }).RequireRateLimiting("lobby");
        app.MapGet("/api/rule-codes/{code}",async(string code,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            string? key=RuleCodes.Normalize(code);if(key is null)return Results.BadRequest(new{message="코드 형식을 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);
            var rules=await RuleCodes.RulesAsync(c,null,key,ct);
            // A code whose rules this build no longer accepts is reported as unknown rather than half-loaded.
            if(rules is null||!CustomRoomRules.Valid(rules))return Results.NotFound(new{message="없는 코드입니다."});
            // Records only: a rule code names rules (everyone with the same rules has the same code), so it is not a way to
            // receive them. Rules travel by share codes (ShareCodes.cs), which their owner can expire or revoke.
            return Results.Ok(new{code=RuleCodes.Display(key),summary=await RuleCodes.SummaryAsync(c,key,user.Id,ct)});
        }).RequireRateLimiting("lobby");
    }
}
