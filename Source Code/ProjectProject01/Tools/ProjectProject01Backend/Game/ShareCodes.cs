using System.Security.Cryptography;
using MySqlConnector;

/// <summary>One share code as its owner sees it in "my share codes".</summary>
internal sealed record ShareCodeItem(string Code,string Name,string? ExpiresAtUtc,bool Board);

/// <summary>
/// Custom game share codes (Docs/CustomGamePlayground.md 14). Rule codes name rules and keep per-code records, but
/// everyone with the same rules has the same rule code, so a rule code cannot be private and does not hand out rules.
/// Rules are passed on with a share code instead: a fresh random code per share (10 characters, ~50 bits), made by its
/// owner with an expiry (24 h, 7 days or none), revocable at any time. A board post gets one permanent share code that
/// cannot be revoked or expire; it stops working only when the post is taken down or hidden by a moderator.
/// </summary>
internal static class ShareCodes
{
    private const string Alphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    internal const int Length=10,MaxActivePerUser=20;
    internal static readonly int[] AllowedHours=[24,168,0]; // 0 = no expiry.

    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS rule_share_codes(code CHAR(10) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          user_id BIGINT UNSIGNED NOT NULL,rule_code VARCHAR(12) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,name VARCHAR(32) NOT NULL,
          expires_at_utc DATETIME(6) NULL,board_preset_id BIGINT UNSIGNED NULL UNIQUE,created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
          KEY ix_share_codes_user(user_id),
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
          FOREIGN KEY(board_preset_id) REFERENCES shared_rule_presets(id) ON DELETE CASCADE);
        """,ct);

    internal static string NewCode(){var chars=new char[Length];for(int i=0;i<Length;i++)chars[i]=Alphabet[RandomNumberGenerator.GetInt32(Alphabet.Length)];return new string(chars);}
    internal static string Display(string code)=>code.Length==Length?code[..5]+"-"+code[5..]:code;
    /// <summary>"abcde-fghij" and "ABCDEFGHIJ" are the same share code; anything else is not one.</summary>
    internal static string? Normalize(string? input)
    {
        if(input is null)return null;
        var text=input.Replace("-","").Replace(" ","").ToUpperInvariant();
        return text.Length==Length&&text.All(ch=>Alphabet.Contains(ch))?text:null;
    }

    /// <summary>Makes a new share code for these rules. Rules must have passed CustomRoomRules.Valid.</summary>
    internal static async Task<string> CreateAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string name,CustomRuleSet rules,int hours,long? boardPreset,CancellationToken ct)
    {
        string ruleCode=await RuleCodes.EnsureCodeAsync(c,tx,rules,ct);
        for(int attempt=0;attempt<5;attempt++)
        {
            string code=NewCode();
            int added=await CommunityEndpoints.Exec(c,tx,"""
                INSERT IGNORE INTO rule_share_codes(code,user_id,rule_code,name,expires_at_utc,board_preset_id)
                VALUES(@c,@u,@r,@n,IF(@h=0,NULL,UTC_TIMESTAMP(6)+INTERVAL @h HOUR),@b)
                """,ct,("c",code),("u",user),("r",ruleCode),("n",name),("h",hours),("b",boardPreset));
            if(added==1)return code;
        }
        throw new InvalidOperationException("No free share code.");
    }

    /// <summary>The board post's permanent share code (made once, kept while the post exists).</summary>
    internal static async Task<string> EnsureBoardCodeAsync(MySqlConnection c,MySqlTransaction tx,ulong user,long preset,string name,CustomRuleSet rules,CancellationToken ct)
    {
        var known=await CommunityEndpoints.Rows(c,tx,"SELECT code FROM rule_share_codes WHERE board_preset_id=@p",ct,("p",preset));
        if(known.Count>0){await CommunityEndpoints.Exec(c,tx,"UPDATE rule_share_codes SET name=@n WHERE board_preset_id=@p",ct,("n",name),("p",preset));return Convert.ToString(known[0]["code"])!;}
        return await CreateAsync(c,tx,user,name,rules,0,preset,ct);
    }
}

internal static partial class LobbyEndpoints
{
    internal sealed record ShareCodeRequest(string? Name,CustomRuleSet? Rules,int Hours);

    internal static void MapShareCodes(this WebApplication app)
    {
        app.MapPost("/api/share-codes",async(ShareCodeRequest body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(!SharedPresets.ValidText(body.Name,1,32))return Results.BadRequest(new{message="공유할 이름(1~32자)을 확인하세요."});
            if(!ShareCodes.AllowedHours.Contains(body.Hours))return Results.BadRequest(new{message="기한은 24시간, 7일, 무기한 중 하나입니다."});
            var rules=body.Rules is null?null:body.Rules with{Handicaps=[]};
            if(rules is null||!rules.Enabled||!CustomRoomRules.Valid(rules))return Results.BadRequest(new{message="사용자 지정을 켠 올바른 규칙만 공유할 수 있습니다."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            var active=await CommunityEndpoints.Rows(c,tx,"SELECT code FROM rule_share_codes WHERE user_id=@u AND board_preset_id IS NULL AND (expires_at_utc IS NULL OR expires_at_utc>UTC_TIMESTAMP(6))",ct,("u",user.Id));
            if(active.Count>=ShareCodes.MaxActivePerUser)return Results.Conflict(new{message="공유 코드는 20개까지 둘 수 있습니다. 쓰지 않는 코드를 취소하세요."});
            string code=await ShareCodes.CreateAsync(c,tx,user.Id,body.Name!.Trim(),rules,body.Hours,null,ct);
            await tx.CommitAsync(ct);
            return Results.Ok(new{code=ShareCodes.Display(code),message=body.Hours==0?"공유 코드를 만들었습니다. 취소할 때까지 쓸 수 있습니다.":$"공유 코드를 만들었습니다. {(body.Hours==24?"24시간":"7일")} 동안 쓸 수 있습니다."});
        }).RequireRateLimiting("lobby");

        // What a share code holds, so the receiver can look before loading or saving. Expired, revoked, hidden: unknown.
        app.MapGet("/api/share-codes/{code}",async(string code,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            string? key=ShareCodes.Normalize(code);if(key is null)return Results.BadRequest(new{message="공유 코드는 10글자입니다(예: AB3CD-EF7GH)."});
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,null,"""
                SELECT s.rule_code,s.name,u.display_name,s.expires_at_utc,s.board_preset_id FROM rule_share_codes s JOIN users u ON u.id=s.user_id
                LEFT JOIN shared_rule_presets p ON p.id=s.board_preset_id
                WHERE s.code=@c AND (s.expires_at_utc IS NULL OR s.expires_at_utc>UTC_TIMESTAMP(6)) AND (s.board_preset_id IS NULL OR (p.hidden=FALSE AND p.published))
                """,ct,("c",key));
            if(rows.Count==0)return Results.NotFound(new{message="없거나 기한이 지난 공유 코드입니다."});
            string ruleCode=Convert.ToString(rows[0]["rule_code"])!;var rules=await RuleCodes.RulesAsync(c,null,ruleCode,ct);
            if(rules is null||!CustomRoomRules.Valid(rules))return Results.NotFound(new{message="이 버전에서 쓸 수 없는 규칙입니다."});
            return Results.Ok(new{code=ShareCodes.Display(key),name=Convert.ToString(rows[0]["name"]),owner=Convert.ToString(rows[0]["display_name"]),
                expiresAtUtc=rows[0]["expires_at_utc"] is DateTime at?at.ToString("O"):null,board=rows[0]["board_preset_id"] is not (null or DBNull),
                ruleCode=RuleCodes.Display(ruleCode),rules,summary=await RuleCodes.SummaryAsync(c,ruleCode,user.Id,ct)});
        }).RequireRateLimiting("lobby");

        app.MapGet("/api/share-codes",async(HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,null,"""
                SELECT code,name,expires_at_utc,board_preset_id FROM rule_share_codes
                WHERE user_id=@u AND (expires_at_utc IS NULL OR expires_at_utc>UTC_TIMESTAMP(6)) ORDER BY created_at_utc DESC LIMIT 40
                """,ct,("u",user.Id));
            var items=rows.Select(r=>new ShareCodeItem(ShareCodes.Display(Convert.ToString(r["code"])!),Convert.ToString(r["name"])!,
                r["expires_at_utc"] is DateTime at?at.ToString("O"):null,r["board_preset_id"] is not (null or DBNull))).ToList();
            return Results.Ok(new{items});
        }).RequireRateLimiting("lobby");

        // Revoke. A board post's code is fixed: it goes away only with the post.
        app.MapDelete("/api/share-codes/{code}",async(string code,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            string? key=ShareCodes.Normalize(code);if(key is null)return Results.BadRequest(new{message="공유 코드 형식을 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,null,"SELECT user_id,board_preset_id FROM rule_share_codes WHERE code=@c",ct,("c",key));
            if(rows.Count==0)return Results.NotFound(new{message="이미 없는 공유 코드입니다."});
            if(Convert.ToUInt64(rows[0]["user_id"])!=user.Id)return Results.StatusCode(403);
            if(rows[0]["board_preset_id"] is not (null or DBNull))return Results.Conflict(new{message="게시판 글의 코드는 고정입니다. 글을 내리면 함께 사라집니다."});
            await CommunityEndpoints.Exec(c,null,"DELETE FROM rule_share_codes WHERE code=@c AND user_id=@u",ct,("c",key),("u",user.Id));
            return Results.Ok(new{message="공유 코드를 취소했습니다. 이제 이 코드로는 불러올 수 없습니다."});
        }).RequireRateLimiting("lobby");
    }
}
