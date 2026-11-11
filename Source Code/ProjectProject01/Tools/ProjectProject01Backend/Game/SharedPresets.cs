using MySqlConnector;

/// <summary>One shared preset as the board shows it. ClearRate is 0-100, null before anyone finished a match.
/// MyVote is 1 (추천), -1 (비추천) or 0. Hidden posts are only ever shown to their author.</summary>
internal sealed record SharedPresetItem(long Id,string Code,string Name,string Summary,string Author,string CreatedAtUtc,int Plays,int? ClearRate,
    int SurvivorWins,int MannequinWins,int Likes,int Dislikes,int MyVote,bool Mine,bool Cleared,bool Reported,bool Hidden,string ShareCode,bool Published,CustomRuleSet Rules);

/// <summary>
/// Custom game shared preset board (Docs/CustomGamePlayground.md 13). A player may post the rules they are editing,
/// by choice, once they have cleared them: finished one match with that rule code to the end, in either role (a
/// forfeit or a desertion does not count; unfinished matches have no verified result). The board is a list of rule
/// codes with a name and one line; anyone can load one. Sorting only (newest, most played, most recommended), no rank
/// numbers. Clear rate = finished player-matches / (finished + forfeited) with that code. 추천/비추천 need a clear too
/// (so they come from someone who played it), one vote per player, never on one's own post. Reports go to the
/// moderators; like player reports, a report alone never hides anything (a moderator hides, restores or deletes).
/// </summary>
internal static class SharedPresets
{
    internal const int PageSize=4,MaxPostsPerUser=10,MaxName=32,MaxSummary=60;
    internal static readonly string[] ReportReasons=["Name","Summary","Other"];

    internal static async Task EnsureAsync(MySqlConnection c,CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c,null,"""
            CREATE TABLE IF NOT EXISTS shared_rule_presets(id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
              rule_code VARCHAR(12) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,user_id BIGINT UNSIGNED NOT NULL,
              name VARCHAR(32) NOT NULL,summary VARCHAR(60) NOT NULL,created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
              KEY ix_shared_presets_user(user_id),KEY ix_shared_presets_created(created_at_utc),
              FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS shared_rule_preset_likes(preset_id BIGINT UNSIGNED NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
              created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),PRIMARY KEY(preset_id,user_id),
              FOREIGN KEY(preset_id) REFERENCES shared_rule_presets(id) ON DELETE CASCADE,FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            CREATE TABLE IF NOT EXISTS shared_rule_preset_reports(preset_id BIGINT UNSIGNED NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
              reason VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
              PRIMARY KEY(preset_id,user_id),
              FOREIGN KEY(preset_id) REFERENCES shared_rule_presets(id) ON DELETE CASCADE,FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
            """,ct);
        // Added on 2026-11-06 with 비추천 and moderation; defaults keep earlier rows as "추천" and visible.
        foreach(var (table,column,definition) in new[]{("shared_rule_preset_likes","vote","TINYINT NOT NULL DEFAULT 1"),("shared_rule_presets","hidden","BOOLEAN NOT NULL DEFAULT FALSE"),
            ("shared_rule_presets","reviewed_at_utc","DATETIME(6) NULL"),("shared_rule_presets","published","BOOLEAN NOT NULL DEFAULT TRUE")})
        {
            var found=await CommunityEndpoints.Rows(c,null,"SELECT column_name FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name=@t AND column_name=@c",ct,("t",table),("c",column));
            if(found.Count==0)await CommunityEndpoints.Exec(c,null,$"ALTER TABLE {table} ADD COLUMN {column} {definition}",ct);
        }
    }

    internal static bool ValidText(string? text,int min,int max)=>text is not null&&text.Trim().Length>=min&&text.Trim().Length<=max&&!text.Any(char.IsControl);

    /// <summary>True when this player finished a match with the code (verified result, no forfeit/desertion).</summary>
    internal static async Task<bool> ClearedAsync(MySqlConnection c,MySqlTransaction? tx,string code,ulong user,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"""
            SELECT 1 FROM match_custom_rules m JOIN verified_match_results r ON r.match_id=m.match_id AND r.user_id=@u
            LEFT JOIN match_desertions d ON d.match_id=m.match_id AND d.user_id=@u
            WHERE m.rule_code=@c AND d.match_id IS NULL LIMIT 1
            """,ct,("c",code),("u",user))).Count>0;

    internal static int? ClearRate(int cleared,int forfeited)=>cleared+forfeited==0?null:(int)Math.Round(100.0*cleared/(cleared+forfeited));

    internal sealed record PublishResult(bool Ok,string Message,string Code,string Share);

    /// <summary>Posts (or re-posts) these rules as mine. A first post needs a clear; a taken-down post of mine comes back with its
    /// votes and fixed share code. KeepSummary leaves an existing post's one-line summary as it was (bulk posting has none).</summary>
    internal static async Task<PublishResult> PublishAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string name,string summary,bool keepSummary,CustomRuleSet rules,CancellationToken ct)
    {
        // A board post's name and summary are public: refused when they contain a banned word (Docs/ChatFilter.md).
        var filter=await ChatFilter.CurrentAsync(c,tx,ct);
        if(filter.Contains(name)||filter.Contains(summary))return new(false,"이름·한 줄 소개에 "+ChatFilter.Refusal,"","");
        string code=await RuleCodes.EnsureCodeAsync(c,tx,rules,ct);
        var existing=await CommunityEndpoints.Rows(c,tx,"SELECT user_id,hidden FROM shared_rule_presets WHERE rule_code=@c FOR UPDATE",ct,("c",code));
        bool mine=existing.Count>0&&Convert.ToUInt64(existing[0]["user_id"])==user;
        if(existing.Count>0&&!mine)return new(false,"이미 다른 사람이 올린 규칙입니다. 게시판에서 찾아 불러오세요.",code,"");
        if(mine&&Convert.ToBoolean(existing[0]["hidden"]))return new(false,"운영자가 숨긴 글은 다시 게시할 수 없습니다.",code,"");
        if(!mine&&!await ClearedAsync(c,tx,code,user,ct))return new(false,"이 규칙으로 한 판을 끝까지 마쳐야 올릴 수 있습니다. 기권한 판은 세지 않습니다.",code,"");
        if((await CommunityEndpoints.Rows(c,tx,"SELECT id FROM shared_rule_presets WHERE user_id=@u AND published AND rule_code<>@c",ct,("u",user),("c",code))).Count>=MaxPostsPerUser)
            return new(false,"게시판에는 한 사람이 10개까지 게시할 수 있습니다. 하나를 내린 뒤 올리세요.",code,"");
        await CommunityEndpoints.Exec(c,tx,"INSERT INTO shared_rule_presets(rule_code,user_id,name,summary) VALUES(@c,@u,@n,@s) ON DUPLICATE KEY UPDATE name=VALUES(name),summary=IF(@k,summary,VALUES(summary)),published=TRUE",ct,
            ("c",code),("u",user),("n",name),("s",summary),("k",keepSummary));
        // The post's share code is permanent and fixed (Docs/CustomGamePlayground.md 14).
        long preset=Convert.ToInt64((await CommunityEndpoints.Rows(c,tx,"SELECT id FROM shared_rule_presets WHERE rule_code=@c",ct,("c",code)))[0]["id"]);
        string share=await ShareCodes.EnsureBoardCodeAsync(c,tx,user,preset,name,rules,ct);
        return new(true,"공유 게시판에 올렸습니다.",code,share);
    }

    /// <summary>Takes my post of these rules off the board (kept in "my posts" with its votes and fixed code).</summary>
    internal static async Task<PublishResult> UnpublishAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CustomRuleSet rules,CancellationToken ct)
    {
        string code=await RuleCodes.EnsureCodeAsync(c,tx,rules,ct);
        int changed=await CommunityEndpoints.Exec(c,tx,"UPDATE shared_rule_presets SET published=FALSE WHERE rule_code=@c AND user_id=@u AND published",ct,("c",code),("u",user));
        return changed>0?new(true,"게시판에서 내렸습니다.",code,""):new(false,"게시 중인 글이 없습니다.",code,"");
    }

    // Per code: finished matches, wins per side, cleared and forfeited player-matches, votes.
    private const string StatsSql="""
        SELECT s.id,s.rule_code,s.name,s.summary,s.user_id,u.display_name,s.created_at_utc,s.hidden,s.published,
          (SELECT COUNT(DISTINCT r.match_id) FROM match_custom_rules m JOIN verified_match_results r ON r.match_id=m.match_id WHERE m.rule_code=s.rule_code) AS plays,
          (SELECT COUNT(DISTINCT CASE WHEN r.role='Survivor' AND r.success THEN r.match_id END) FROM match_custom_rules m JOIN verified_match_results r ON r.match_id=m.match_id WHERE m.rule_code=s.rule_code) AS survivor_wins,
          (SELECT COUNT(DISTINCT CASE WHEN r.role='Mannequin' AND r.success THEN r.match_id END) FROM match_custom_rules m JOIN verified_match_results r ON r.match_id=m.match_id WHERE m.rule_code=s.rule_code) AS mannequin_wins,
          (SELECT COUNT(*) FROM match_custom_rules m JOIN verified_match_results r ON r.match_id=m.match_id
             LEFT JOIN match_desertions d ON d.match_id=r.match_id AND d.user_id=r.user_id WHERE m.rule_code=s.rule_code AND d.match_id IS NULL) AS cleared,
          (SELECT COUNT(*) FROM match_custom_rules m JOIN match_desertions d ON d.match_id=m.match_id WHERE m.rule_code=s.rule_code) AS forfeited,
          (SELECT COUNT(*) FROM shared_rule_preset_likes l WHERE l.preset_id=s.id AND l.vote>0) AS likes,
          (SELECT COUNT(*) FROM shared_rule_preset_likes l WHERE l.preset_id=s.id AND l.vote<0) AS dislikes,
          (SELECT COALESCE(SUM(SIGN(l.vote)),0) FROM shared_rule_preset_likes l WHERE l.preset_id=s.id) AS score,
          COALESCE((SELECT l.vote FROM shared_rule_preset_likes l WHERE l.preset_id=s.id AND l.user_id=@me),0) AS my_vote,
          EXISTS(SELECT 1 FROM shared_rule_preset_reports p WHERE p.preset_id=s.id AND p.user_id=@me) AS reported,
          (SELECT x.code FROM rule_share_codes x WHERE x.board_preset_id=s.id) AS share_code
        FROM shared_rule_presets s JOIN users u ON u.id=s.user_id
        """;
    // The board shows published posts (a hidden one only to its author); "mine" shows all of my posts, also taken-down ones.
    internal static string WhereFor(string? sort)=>sort=="mine"?" WHERE s.user_id=@me":" WHERE s.published AND (s.hidden=FALSE OR s.user_id=@me)";

    internal static string OrderFor(string? sort)=>sort switch
    {
        "played"=>"plays DESC,s.created_at_utc DESC",
        "liked"=>"score DESC,likes DESC,s.created_at_utc DESC", // 추천순: 추천 - 비추천.
        _=>"s.created_at_utc DESC"
    };

    internal static async Task<(List<SharedPresetItem> Items,bool More)> ListAsync(MySqlConnection c,ulong me,string? sort,int page,CancellationToken ct)
    {
        page=Math.Clamp(page,0,1000);
        var rows=await CommunityEndpoints.Rows(c,null,StatsSql+WhereFor(sort)+$" ORDER BY {OrderFor(sort)} LIMIT @n OFFSET @o",ct,("me",me),("n",PageSize+1),("o",page*PageSize));
        var items=new List<SharedPresetItem>();
        foreach(var r in rows.Take(PageSize))
        {
            string code=Convert.ToString(r["rule_code"])!;var rules=await RuleCodes.RulesAsync(c,null,code,ct);
            if(rules is null||!CustomRoomRules.Valid(rules))continue; // Rules this build no longer runs are not offered.
            int Int(string k)=>r[k] is null or DBNull?0:Convert.ToInt32(r[k]);
            ulong author=Convert.ToUInt64(r["user_id"]);
            items.Add(new SharedPresetItem(Convert.ToInt64(r["id"]),RuleCodes.Display(code),Convert.ToString(r["name"])!,Convert.ToString(r["summary"])!,
                Convert.ToString(r["display_name"])!,((DateTime)r["created_at_utc"]!).ToString("O"),Int("plays"),ClearRate(Int("cleared"),Int("forfeited")),
                Int("survivor_wins"),Int("mannequin_wins"),Int("likes"),Int("dislikes"),Math.Sign(Int("my_vote")),author==me,author!=me&&await ClearedAsync(c,null,code,me,ct),
                Convert.ToBoolean(r["reported"]),Convert.ToBoolean(r["hidden"]),r["share_code"] is string share?ShareCodes.Display(share):"",Convert.ToBoolean(r["published"]),rules));
        }
        return(items,rows.Count>PageSize);
    }
}

internal static partial class LobbyEndpoints
{
    internal sealed record SharedPresetPost(string? Name,string? Summary,CustomRuleSet? Rules);
    internal sealed record SharedPresetVote(int Vote);
    internal sealed record SharedPresetReport(string? Reason);
    internal sealed record SharedPresetModeration(string? Action);
    internal sealed record SharedPresetPublish(bool Published);
    internal sealed record SharedPresetBulkItem(string? Name,CustomRuleSet? Rules);
    internal sealed record SharedPresetBulk(string? Action,SharedPresetBulkItem[]? Presets);

    internal static void MapSharedPresets(this WebApplication app)
    {
        app.MapGet("/api/shared-presets",async(string? sort,int? page,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);
            var (items,more)=await SharedPresets.ListAsync(c,user.Id,sort,page??0,ct);
            return Results.Ok(new{sort=sort is "played" or "liked" or "mine"?sort:"new",page=page??0,more,items});
        }).RequireRateLimiting("lobby");

        // All of my posts with their rules, so "my presets" can show which ones are on the board.
        app.MapGet("/api/shared-presets/mine",async(HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,null,"SELECT s.id,s.rule_code,s.name,s.published,s.hidden FROM shared_rule_presets s WHERE s.user_id=@u ORDER BY s.id",ct,("u",user.Id));
            var items=new List<object>();
            foreach(var r in rows)
            {
                var rules=await RuleCodes.RulesAsync(c,null,Convert.ToString(r["rule_code"])!,ct);if(rules is null)continue;
                items.Add(new{id=Convert.ToInt64(r["id"]),name=Convert.ToString(r["name"]),published=Convert.ToBoolean(r["published"]),hidden=Convert.ToBoolean(r["hidden"]),rules});
            }
            return Results.Ok(new{items});
        }).RequireRateLimiting("lobby");

        // Post (게시) or take down (내리기) one of my posts. Taking down keeps the post, its votes and its fixed share code
        // (which stops working until the post is published again).
        app.MapPut("/api/shared-presets/{id:long}",async(long id,SharedPresetPublish body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT user_id,hidden FROM shared_rule_presets WHERE id=@i FOR UPDATE",ct,("i",id));
            if(rows.Count==0)return Results.NotFound(new{message="없는 글입니다."});
            if(Convert.ToUInt64(rows[0]["user_id"])!=user.Id)return Results.StatusCode(403);
            if(body.Published&&Convert.ToBoolean(rows[0]["hidden"]))return Results.Conflict(new{message="운영자가 숨긴 글은 다시 게시할 수 없습니다."});
            if(body.Published&&(await CommunityEndpoints.Rows(c,tx,"SELECT id FROM shared_rule_presets WHERE user_id=@u AND published AND id<>@i",ct,("u",user.Id),("i",id))).Count>=SharedPresets.MaxPostsPerUser)
                return Results.Conflict(new{message="게시판에는 한 사람이 10개까지 게시할 수 있습니다. 하나를 내린 뒤 게시하세요."});
            await CommunityEndpoints.Exec(c,tx,"UPDATE shared_rule_presets SET published=@p WHERE id=@i",ct,("p",body.Published),("i",id));
            await tx.CommitAsync(ct);
            return Results.Ok(new{message=body.Published?"게시판에 다시 게시했습니다. 공유 코드도 다시 쓸 수 있습니다.":"게시판에서 내렸습니다. 다시 게시할 수 있습니다."});
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/shared-presets",async(SharedPresetPost body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(!SharedPresets.ValidText(body.Name,1,SharedPresets.MaxName)||!SharedPresets.ValidText(body.Summary??"",0,SharedPresets.MaxSummary))
                return Results.BadRequest(new{message="이름(1~32자)과 설명(60자까지)을 확인하세요."});
            var rules=body.Rules is null?null:body.Rules with{Handicaps=[]};
            if(rules is null||!rules.Enabled||!CustomRoomRules.Valid(rules))return Results.BadRequest(new{message="사용자 지정을 켠 올바른 규칙만 올릴 수 있습니다."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            var result=await SharedPresets.PublishAsync(c,tx,user.Id,body.Name!.Trim(),(body.Summary??"").Trim(),false,rules,ct);
            if(!result.Ok)return Results.Conflict(new{message=result.Message,code=RuleCodes.Display(result.Code)});
            await tx.CommitAsync(ct);
            return Results.Ok(new{message=result.Message,code=RuleCodes.Display(result.Code),shareCode=ShareCodes.Display(result.Share)});
        }).RequireRateLimiting("lobby");

        // Several of my presets at once (the "my presets" overview): publish or take down each, one result line per preset.
        app.MapPost("/api/shared-presets/bulk",async(SharedPresetBulk body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(body.Action is not ("Publish" or "Unpublish")||body.Presets is null||body.Presets.Length is <1 or >20)return Results.BadRequest(new{message="고른 프리셋(1~20개)과 작업을 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            var results=new List<object>();int done=0;
            foreach(var p in body.Presets)
            {
                string name=(p.Name??"").Trim();var rules=p.Rules is null?null:p.Rules with{Handicaps=[]};
                if(!SharedPresets.ValidText(name,1,SharedPresets.MaxName)||rules is null||!rules.Enabled||!CustomRoomRules.Valid(rules))
                {results.Add(new{name,ok=false,message="사용자 지정이 켜진 올바른 규칙이 아닙니다."});continue;}
                var r=body.Action=="Publish"?await SharedPresets.PublishAsync(c,tx,user.Id,name,"",true,rules,ct):await SharedPresets.UnpublishAsync(c,tx,user.Id,rules,ct);
                if(r.Ok)done++;results.Add(new{name,ok=r.Ok,message=r.Message});
            }
            await tx.CommitAsync(ct);
            string verb=body.Action=="Publish"?"게시":"내림";
            return Results.Ok(new{message=done==body.Presets.Length?$"{done}개 {verb} 완료.":$"{done}개 {verb}, {body.Presets.Length-done}개는 그대로입니다.",results});
        }).RequireRateLimiting("lobby");

        app.MapDelete("/api/shared-presets/{id:long}",async(long id,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,null,"SELECT user_id FROM shared_rule_presets WHERE id=@i",ct,("i",id));
            if(rows.Count==0)return Results.NotFound(new{message="이미 내려간 글입니다."});
            if(Convert.ToUInt64(rows[0]["user_id"])!=user.Id)return Results.StatusCode(403);
            await CommunityEndpoints.Exec(c,null,"DELETE FROM shared_rule_presets WHERE id=@i AND user_id=@u",ct,("i",id),("u",user.Id));
            return Results.Ok(new{message="게시판에서 내렸습니다."});
        }).RequireRateLimiting("lobby");

        // 추천 1, 비추천 -1, 취소 0. The same button again cancels on the client side by sending 0.
        app.MapPost("/api/shared-presets/{id:long}/vote",async(long id,SharedPresetVote body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(body.Vote is not (-1 or 0 or 1))return Results.BadRequest(new{message="추천 값을 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,null,"SELECT user_id,rule_code,hidden,published FROM shared_rule_presets WHERE id=@i",ct,("i",id));
            if(rows.Count==0||Convert.ToBoolean(rows[0]["hidden"])||!Convert.ToBoolean(rows[0]["published"]))return Results.NotFound(new{message="이미 내려간 글입니다."});
            if(Convert.ToUInt64(rows[0]["user_id"])==user.Id)return Results.Conflict(new{message="내가 올린 글에는 추천·비추천을 할 수 없습니다."});
            if(body.Vote!=0&&!await SharedPresets.ClearedAsync(c,null,Convert.ToString(rows[0]["rule_code"])!,user.Id,ct))
                return Results.Conflict(new{message="이 규칙으로 한 판을 끝까지 마친 뒤 추천·비추천할 수 있습니다."});
            if(body.Vote==0)await CommunityEndpoints.Exec(c,null,"DELETE FROM shared_rule_preset_likes WHERE preset_id=@i AND user_id=@u",ct,("i",id),("u",user.Id));
            else await CommunityEndpoints.Exec(c,null,"INSERT INTO shared_rule_preset_likes(preset_id,user_id,vote) VALUES(@i,@u,@v) ON DUPLICATE KEY UPDATE vote=VALUES(vote)",ct,("i",id),("u",user.Id),("v",body.Vote));
            return Results.Ok(new{message=body.Vote switch{1=>"추천했습니다.",-1=>"비추천했습니다.",_=>"취소했습니다."}});
        }).RequireRateLimiting("lobby");

        // A report goes to the moderators; it never hides the post by itself (same rule as player reports).
        app.MapPost("/api/shared-presets/{id:long}/report",async(long id,SharedPresetReport body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            if(!SharedPresets.ReportReasons.Contains(body.Reason))return Results.BadRequest(new{message="신고 사유를 고르세요."});
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,null,"SELECT user_id FROM shared_rule_presets WHERE id=@i",ct,("i",id));
            if(rows.Count==0)return Results.NotFound(new{message="이미 내려간 글입니다."});
            if(Convert.ToUInt64(rows[0]["user_id"])==user.Id)return Results.Conflict(new{message="내가 올린 글은 신고할 수 없습니다."});
            int added=await CommunityEndpoints.Exec(c,null,"INSERT IGNORE INTO shared_rule_preset_reports(preset_id,user_id,reason) VALUES(@i,@u,@r)",ct,("i",id),("u",user.Id),("r",body.Reason));
            return Results.Ok(new{message=added>0?"신고가 접수되었습니다. 운영자가 확인합니다.":"이미 신고한 글입니다."});
        }).RequireRateLimiting("social");

        // Moderators (admin key): reported or hidden posts, then hide / restore / dismiss reports / delete.
        app.MapGet("/api/admin/community/shared-presets",async(string? status,HttpRequest req,AdministratorOptions admin,AuthDatabase db,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(status is not (null or "Reported" or "Hidden" or "All"))return Results.BadRequest(new{message="상태를 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,null,"""
                SELECT CAST(s.id AS CHAR) id,s.rule_code ruleCode,s.name,s.summary,CAST(s.user_id AS CHAR) authorId,u.display_name author,s.hidden,
                  s.created_at_utc createdAtUtc,s.reviewed_at_utc reviewedAtUtc,
                  (SELECT COUNT(*) FROM shared_rule_preset_reports p WHERE p.preset_id=s.id) reports,
                  (SELECT GROUP_CONCAT(DISTINCT p.reason) FROM shared_rule_preset_reports p WHERE p.preset_id=s.id) reasons
                FROM shared_rule_presets s JOIN users u ON u.id=s.user_id
                WHERE @status='All' OR (@status='Hidden' AND s.hidden) OR (@status='Reported' AND EXISTS(SELECT 1 FROM shared_rule_preset_reports p WHERE p.preset_id=s.id))
                ORDER BY reports DESC,s.id DESC LIMIT 100
                """,ct,("status",status??"Reported"));
            return Results.Ok(new{posts=rows});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/community/shared-presets/{id:long}",async(long id,SharedPresetModeration body,HttpRequest req,AdministratorOptions admin,AuthDatabase db,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            string? sql=body.Action switch
            {
                "Hide"=>"UPDATE shared_rule_presets SET hidden=TRUE,reviewed_at_utc=UTC_TIMESTAMP(6) WHERE id=@i",
                "Restore"=>"UPDATE shared_rule_presets SET hidden=FALSE,reviewed_at_utc=UTC_TIMESTAMP(6) WHERE id=@i;DELETE FROM shared_rule_preset_reports WHERE preset_id=@i",
                "Dismiss"=>"UPDATE shared_rule_presets SET reviewed_at_utc=UTC_TIMESTAMP(6) WHERE id=@i;DELETE FROM shared_rule_preset_reports WHERE preset_id=@i",
                "Delete"=>"DELETE FROM shared_rule_presets WHERE id=@i",
                _=>null
            };
            if(sql is null)return Results.BadRequest(new{message="Hide, Restore, Dismiss, Delete 중 하나를 고르세요."});
            await using var c=await db.OpenConnectionAsync(ct);
            if((await CommunityEndpoints.Rows(c,null,"SELECT id FROM shared_rule_presets WHERE id=@i",ct,("i",id))).Count==0)return Results.NotFound();
            await CommunityEndpoints.Exec(c,null,sql,ct,("i",id));
            return Results.Ok(new{message=body.Action switch{"Hide"=>"글을 숨겼습니다.","Restore"=>"글을 다시 보이게 했습니다.","Dismiss"=>"신고를 정리했습니다.",_=>"글을 지웠습니다."}});
        }).RequireRateLimiting("admin");
    }
}
