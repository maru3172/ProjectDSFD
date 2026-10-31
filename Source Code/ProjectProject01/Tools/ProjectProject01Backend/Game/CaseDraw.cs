using System.Security.Cryptography;
using System.Text.Json;
using MySqlConnector;

// Cosmetic-only seasonal pool. Grade 1 is the highest; grades 1 and 2 are permanent one-time rewards.
internal static class CaseDraw
{
    internal const int TicketCost=500,PityLimit=100,SoftPityLimit=50,DuplicateCredits=5,MaxExchange=2000000;
    internal sealed record Prize(string Id,string Name,int Grade,int Weight,string? ItemId=null,int Credits=0);
    internal sealed record Pool(string Id,string Title,DateTimeOffset StartsAtUtc,DateTimeOffset EndsAtUtc,bool Published,Prize[] Prizes);
    internal sealed record Configure(long Revision,Pool? Pool);
    internal sealed record Command(string? Operation,string? RequestId,int Count,long Revision);
    internal sealed record Award(string Id,string Name,int Grade,string? ItemId,int Credits,bool Duplicate,bool Guaranteed);
    internal static readonly Pool Draft=new("record0","RECORD 0",DateTimeOffset.UtcNow,DateTimeOffset.UtcNow.AddMonths(3),false,BasePrizes());
    private static Prize[] BasePrizes()=>[
        new("afterhours","폐점 이후",1,0,"banner_afterhours"),new("embers","잔불",2,0,"banner_embers"),
        ..CaseCollection.DrawExtras.Select(p=>new Prize(p.Id,p.Name,p.Grade,0,p.Id)),
        new("archive","보관 문서 테두리",3,0,"frame_archive"),new("receipt","오래된 영수증",4,0,"icon_receipt"),new("points","기록 포인트",5,0,null,5)];
    private static readonly JsonSerializerOptions Json=new(JsonSerializerDefaults.Web);
    internal static async Task EnsureAsync(MySqlConnection c,CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_draw_settings(id TINYINT PRIMARY KEY,revision BIGINT NOT NULL DEFAULT 0,pool_json MEDIUMTEXT NULL);
        CREATE TABLE IF NOT EXISTS case_draw_catalogues(pool_id VARCHAR(32) PRIMARY KEY,signature TEXT NOT NULL);
        INSERT IGNORE INTO case_draw_settings(id) VALUES(1);
        CREATE TABLE IF NOT EXISTS case_draw_wallet(user_id BIGINT UNSIGNED PRIMARY KEY,tickets BIGINT NOT NULL DEFAULT 0,
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_draw_progress(user_id BIGINT UNSIGNED NOT NULL,pool_id VARCHAR(32) NOT NULL,pity INT NOT NULL DEFAULT 0,cycle BIGINT NOT NULL DEFAULT 0,total BIGINT NOT NULL DEFAULT 0,
          PRIMARY KEY(user_id,pool_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_draw_unique(user_id BIGINT UNSIGNED NOT NULL,item_id VARCHAR(32) NOT NULL,
          PRIMARY KEY(user_id,item_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_draw_acquisitions(user_id BIGINT UNSIGNED NOT NULL,pool_id VARCHAR(32) NOT NULL,item_id VARCHAR(32) NOT NULL,
          PRIMARY KEY(user_id,pool_id,item_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_draw_receipts(user_id BIGINT UNSIGNED NOT NULL,request_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          fingerprint VARCHAR(128) NOT NULL,result_json MEDIUMTEXT NOT NULL,retired BOOLEAN NOT NULL DEFAULT FALSE,created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
          PRIMARY KEY(user_id,request_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
        if((await CommunityEndpoints.Rows(c,null,"SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='case_draw_receipts' AND COLUMN_NAME='retired'",ct)).Count==0)
            try{await CommunityEndpoints.Exec(c,null,"ALTER TABLE case_draw_receipts ADD COLUMN retired BOOLEAN NOT NULL DEFAULT FALSE",ct);}
            catch(MySqlException ex)when(ex.Number==1060){/* Another instance completed the same migration. */}
    }
    internal static bool Validate(Pool? p,out string error)
    {
        error="시즌·5등급 상품·기간·가중치를 확인하세요.";
        if(p is null||!Id(p.Id)||string.IsNullOrWhiteSpace(p.Title)||p.Title.Length>80||p.EndsAtUtc<=p.StartsAtUtc||p.Prizes is null||p.Prizes.Length is <5 or >32)return false;
        if(p.Prizes.Any(x=>x is null||!Id(x.Id)||string.IsNullOrWhiteSpace(x.Name)||x.Name.Length>80||x.Grade is <1 or >5||x.Weight is <0 or >1000000||
            (x.ItemId is null?x.Grade<=2||x.Credits is <1 or >500:CaseCollection.Find(x.ItemId) is not {Achievement:null}||x.Credits!=0)))return false;
        if(p.Prizes.Select(x=>x.Id).Distinct().Count()!=p.Prizes.Length||p.Prizes.Where(x=>x.ItemId is not null).Select(x=>x.ItemId).Distinct().Count()!=p.Prizes.Count(x=>x.ItemId is not null))return false;
        if(!Enumerable.Range(1,5).All(g=>p.Prizes.Any(x=>x.Grade==g)))return false;
        if(p.Published&&(p.Prizes.Any(x=>x.Weight==0)||!p.Prizes.Any(x=>x.Grade>=3&&x.ItemId is null)))return false;
        error="";return true;
    }
    private static bool Id(string? s)=>s is {Length:>=1 and <=32}&&s.All(ch=>char.IsAsciiLetterOrDigit(ch)||ch is '_' or '-');
    internal static Pool ExpandLegacyDraft(Pool pool)
    {
        // Upgrade only the original, unconfigured five-prize draft. Published/custom pools keep their identities.
        var original=Draft.Prizes.Where(p=>new[]{"afterhours","embers","archive","receipt","points"}.Contains(p.Id)).ToArray();
        if(pool.Published||pool.Prizes.Length!=5||pool.Prizes.Any(p=>p.Weight!=0||!original.Any(x=>x.Id==p.Id&&x.Grade==p.Grade&&x.ItemId==p.ItemId&&x.Credits==p.Credits)))return pool;
        return pool with{Prizes=[..pool.Prizes,..Draft.Prizes.Where(p=>!original.Any(x=>x.Id==p.Id))]};
    }
    private static async Task<(long Revision,Pool? Pool)> Settings(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct,bool locked=false)
    {
        var r=(await CommunityEndpoints.Rows(c,tx,"SELECT revision,pool_json FROM case_draw_settings WHERE id=1"+(locked?" FOR SHARE":""),ct))[0];
        var pool=r["pool_json"] is string s?JsonSerializer.Deserialize<Pool>(s,Json):null;
        return(Convert.ToInt64(r["revision"]),pool is null?null:ExpandLegacyDraft(pool));
    }
    private static async Task<HashSet<string>> Owned(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"SELECT item_id FROM case_inventory WHERE user_id=@u UNION SELECT item_id FROM case_draw_unique WHERE user_id=@u",ct,("u",user))).Select(r=>(string)r["item_id"]!).ToHashSet();
    internal static Prize[] Eligible(Pool pool,ISet<string> owned)=>pool.Prizes.Where(p=>p.Weight>0&&(p.Grade>2||p.ItemId is not null&&!owned.Contains(p.ItemId))).ToArray();
    internal static Prize Pick(Prize[] eligible,bool guaranteed,Func<int,int>? random=null)
        =>PickGrade(eligible,guaranteed?1:0,random);
    internal static int GuaranteeGrade(Prize[] eligible,int pity,Func<int,int>? random=null)
    {
        bool first=eligible.Any(p=>p.Grade==1),second=eligible.Any(p=>p.Grade==2);
        if(pity>=PityLimit-1&&first)return 1;
        if(pity!=SoftPityLimit-1||(!first&&!second))return 0;
        if(!first)return 2;if(!second)return 1;
        return (random??RandomNumberGenerator.GetInt32)(2)==0?1:2;
    }
    internal static Prize PickGrade(Prize[] eligible,int grade,Func<int,int>? random=null)
    {
        var pool=eligible.Where(p=>grade==0||p.Grade==grade).ToArray();int total=pool.Sum(p=>p.Weight);
        if(total<=0)throw new InvalidOperationException("추첨 가능한 상품이 없습니다.");
        int value=(random??RandomNumberGenerator.GetInt32)(total);
        foreach(var p in pool){if(value<p.Weight)return p;value-=p.Weight;}throw new InvalidOperationException();
    }
    internal static int AdvancePity(int pity,int grade)=>grade==1?0:Math.Min(PityLimit,pity+1);
    private static async Task ResetAccountAsync(MySqlConnection c,MySqlTransaction tx,ulong user,Pool pool,CancellationToken ct)
    {
        var items=(await CommunityEndpoints.Rows(c,tx,"SELECT item_id FROM case_draw_acquisitions WHERE user_id=@u UNION SELECT item_id FROM case_draw_unique WHERE user_id=@u",ct,("u",user))).Select(r=>(string)r["item_id"]!).ToHashSet();
        // Legacy editor accounts may own a one-time rare without its acquisition/unique ledger.
        // These draw-only rare identities are reset even when that legacy bookkeeping is absent.
        foreach(var prize in Draft.Prizes.Concat(pool.Prizes).Where(p=>p.Grade<=2&&p.ItemId is not null))
            if(CaseCollection.Find(prize.ItemId!) is {Achievement:null}&&!CaseRecords.Catalogue.Any(p=>p.Id==prize.ItemId))items.Add(prize.ItemId!);
        // Older draws have no acquisition table/poolId. Their non-duplicate award receipts prove the origin.
        // Page by the primary key under the account lock; never load the account's entire receipt history at once.
        string last="";
        while(true)
        {
            var rows=await CommunityEndpoints.Rows(c,tx,"""
                SELECT request_id,result_json FROM case_draw_receipts
                WHERE user_id=@u AND request_id>@last AND fingerprint LIKE 'Draw:%' AND retired=FALSE
                ORDER BY request_id LIMIT 128
                """,ct,("u",user),("last",last));
            foreach(var row in rows)
            {
                last=row["request_id"] is Guid receiptId?receiptId.ToString("D"):Convert.ToString(row["request_id"],System.Globalization.CultureInfo.InvariantCulture)!;
                try
                {
                    using var json=JsonDocument.Parse((string)row["result_json"]!);
                    if(!json.RootElement.TryGetProperty("awards",out var awards)||awards.ValueKind!=JsonValueKind.Array)continue;
                    foreach(var award in awards.EnumerateArray())
                        if(award.TryGetProperty("itemId",out var id)&&id.ValueKind==JsonValueKind.String&&
                           award.TryGetProperty("duplicate",out var duplicate)&&duplicate.ValueKind==JsonValueKind.False&&
                           CaseCollection.Find(id.GetString()!) is {Achievement:null})items.Add(id.GetString()!);
                }
                catch(JsonException){/* Unknown legacy records cannot authorize removing unrelated items. */}
            }
            if(rows.Count<128)break;
        }
        foreach(string item in items)
            await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_profile_banners WHERE user_id=@u AND item_id=@i;DELETE FROM case_loadout WHERE user_id=@u AND item_id=@i;DELETE FROM case_inventory WHERE user_id=@u AND item_id=@i",ct,("u",user),("i",item));
        await CommunityEndpoints.Exec(c,tx,"""
            DELETE FROM case_draw_unique WHERE user_id=@u;DELETE FROM case_draw_acquisitions WHERE user_id=@u;DELETE FROM case_draw_progress WHERE user_id=@u;
            UPDATE case_draw_receipts SET retired=TRUE WHERE user_id=@u AND fingerprint LIKE 'Draw:%';
            """,ct,("u",user));
    }
    internal static async Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var setting=await Settings(c,tx,ct,true);var pool=setting.Pool??Draft;var owned=await Owned(c,tx,user,ct);
        var wallet=await CommunityEndpoints.Rows(c,tx,"SELECT tickets FROM case_draw_wallet WHERE user_id=@u",ct,("u",user));
        var state=pool is null?[]:await CommunityEndpoints.Rows(c,tx,"SELECT pity,cycle,total FROM case_draw_progress WHERE user_id=@u AND pool_id=@p",ct,("u",user),("p",pool.Id));
        var eligible=pool is null?[]:Eligible(pool,owned);int sum=eligible.Sum(p=>p.Weight);var now=DateTimeOffset.UtcNow;
        return new{revision=setting.Revision,ticketCost=TicketCost,tickets=wallet.Count==0?0:Convert.ToInt64(wallet[0]["tickets"]),pityLimit=PityLimit,
            softPityLimit=SoftPityLimit,pity=state.Count==0?0:Convert.ToInt32(state[0]["pity"]),total=state.Count==0?0:Convert.ToInt64(state[0]["total"]),
            active=pool is {Published:true}&&now>=pool.StartsAtUtc&&now<pool.EndsAtUtc&&sum>0,pityAvailable=eligible.Any(p=>p.Grade==1),
            pool=pool is null?null:new{id=pool.Id,title=pool.Title,startsAtUtc=pool.StartsAtUtc,endsAtUtc=pool.EndsAtUtc,
                prizes=pool.Prizes.Select(p=>new{p.Id,p.Name,p.Grade,p.ItemId,p.Credits,owned=p.ItemId is not null&&owned.Contains(p.ItemId),
                    probability=sum>0&&eligible.Contains(p)?100.0*p.Weight/sum:0,weight=p.Weight}),
                guaranteePrizes=eligible.Where(p=>p.Grade==1).Select(p=>new{p.Id,p.Name,probability=100.0*p.Weight/eligible.Where(x=>x.Grade==1).Sum(x=>x.Weight)}),
                softGuaranteePrizes=eligible.Where(p=>p.Grade<=2).Select(p=>new{p.Id,p.Name,probability=(eligible.Any(x=>x.Grade==1)&&eligible.Any(x=>x.Grade==2)?50.0:100.0)*p.Weight/eligible.Where(x=>x.Grade==p.Grade).Sum(x=>x.Weight)})}};
    }
    internal static void Map(WebApplication app)
    {
        app.MapGet("/api/admin/case-records/draw",async(HttpRequest req,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();await using var c=await db.OpenConnectionAsync(ct);var s=await Settings(c,null,ct);return Results.Ok(new{revision=s.Revision,pool=s.Pool??Draft,defaultPrizes=Draft.Prizes});}).RequireRateLimiting("admin");
        app.MapPut("/api/admin/case-records/draw",async(Configure body,HttpRequest req,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(!Validate(body.Pool,out string error))return Results.BadRequest(new{message=error});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT revision,pool_json FROM case_draw_settings WHERE id=1 FOR UPDATE",ct);
            if(Convert.ToInt64(rows[0]["revision"])!=body.Revision)return Results.Conflict(new{message="운영 설정을 다시 조회하세요."});
            // Existing prize identities/tiers stay immutable. New prizes may be appended without resetting ownership or pity.
            string signature=JsonSerializer.Serialize(body.Pool!.Prizes.OrderBy(p=>p.Id).Select(p=>new{p.Id,p.ItemId,p.Grade,p.Credits}),Json);
            var catalogue=await CommunityEndpoints.Rows(c,tx,"SELECT signature FROM case_draw_catalogues WHERE pool_id=@p",ct,("p",body.Pool.Id));
            if(catalogue.Count>0)
            {
                using var prior=JsonDocument.Parse((string)catalogue[0]["signature"]!);
                if(prior.RootElement.EnumerateArray().Any(x=>!body.Pool.Prizes.Any(p=>p.Id==x.GetProperty("id").GetString()&&p.ItemId==x.GetProperty("itemId").GetString()&&p.Grade==x.GetProperty("grade").GetInt32()&&p.Credits==x.GetProperty("credits").GetInt32())))
                    return Results.Conflict(new{message="기존 상품의 삭제·등급·보상 변경은 새 시즌 ID를 사용하세요."});
            }
            if(body.Pool.Published)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_draw_catalogues(pool_id,signature) VALUES(@p,@s) ON DUPLICATE KEY UPDATE signature=VALUES(signature)",ct,("p",body.Pool.Id),("s",signature));
            await CommunityEndpoints.Exec(c,tx,"UPDATE case_draw_settings SET revision=revision+1,pool_json=@j WHERE id=1",ct,("j",JsonSerializer.Serialize(body.Pool,Json)));
            await tx.CommitAsync(ct);return Results.Ok(new{revision=body.Revision+1,pool=body.Pool});
        }).RequireRateLimiting("admin");
        app.MapPost("/api/case-records/draw",async(Command body,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            bool reset=body.Operation=="EditorReset";
            if(reset&&!app.Services.GetRequiredService<CaseDebugCurrency>().CanGrant(req))return Results.Json(new{message="운영 도우미에서 에디터 디버그를 허용하고 127.0.0.1로 접속하세요."},statusCode:403);
            if(body.Operation is not ("Exchange" or "Draw" or "EditorReset")||
                (body.Operation=="Exchange"?body.Count is <1 or >MaxExchange:reset?body.Count!=1:body.Count is not (1 or 5))||!Guid.TryParse(body.RequestId,out var requestId))return Results.BadRequest();
            string key=requestId.ToString("D"),fingerprint=$"{body.Operation}:{body.Count}:{body.Revision}";
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);var account=await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            var receipt=await CommunityEndpoints.Rows(c,tx,"SELECT fingerprint,result_json,retired FROM case_draw_receipts WHERE user_id=@u AND request_id=@r",ct,("u",user.Id),("r",key));
            if(receipt.Count>0)
            {
                if((string)receipt[0]["fingerprint"]! !=fingerprint)return Results.Conflict(new{message="요청 정보가 일치하지 않습니다."});
                if(body.Operation=="Draw"&&Convert.ToBoolean(receipt[0]["retired"]))
                    return Results.Conflict(new{message="초기화 이전 뽑기 요청입니다. 새로 뽑기를 진행하세요."});
                using var saved=JsonDocument.Parse((string)receipt[0]["result_json"]!);var savedDraw=await SnapshotAsync(c,tx,user.Id,ct);var savedSnapshot=await CaseRecords.Snapshot(c,tx,user.Id,app.Configuration,ct);await tx.CommitAsync(ct);
                return Results.Ok(new{result=saved.RootElement.Clone(),draw=savedDraw,snapshot=savedSnapshot});
            }
            var setting=await Settings(c,tx,ct,true);var pool=setting.Pool??Draft;var now=DateTimeOffset.UtcNow;
            if(body.Revision!=setting.Revision)return Results.Conflict(new{message="상품과 확률이 변경되었습니다. 다시 확인하세요."});
            if(body.Operation=="Draw"&&(pool is not {Published:true}||now<pool.StartsAtUtc||now>=pool.EndsAtUtc))return Results.Conflict(new{message="시즌 뽑기 상품·확률이 아직 공개되지 않았습니다."});
            await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO case_draw_wallet(user_id) VALUES(@u)",ct,("u",user.Id));
            var owned=await Owned(c,tx,user.Id,ct);var awards=new List<Award>();
            if(reset)
            {
                await ResetAccountAsync(c,tx,user.Id,pool,ct);
            }
            else if(body.Operation=="Exchange")
            {
                long cost=(long)TicketCost*body.Count;
                if(Convert.ToInt64(account["credits"])<cost)return Results.Conflict(new{message="포인트가 부족합니다."});
                await CommunityEndpoints.Exec(c,tx,"UPDATE case_accounts SET credits=credits-@cost WHERE user_id=@u;UPDATE case_draw_wallet SET tickets=tickets+@n WHERE user_id=@u",ct,("u",user.Id),("cost",cost),("n",body.Count));
            }
            else
            {
                await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO case_draw_progress(user_id,pool_id) VALUES(@u,@p)",ct,("u",user.Id),("p",pool!.Id));
                var wallet=(await CommunityEndpoints.Rows(c,tx,"SELECT tickets FROM case_draw_wallet WHERE user_id=@u",ct,("u",user.Id)))[0];
                if(Convert.ToInt64(wallet["tickets"])<body.Count)return Results.Conflict(new{message="뽑기 재화가 부족합니다."});
                var progress=(await CommunityEndpoints.Rows(c,tx,"SELECT pity,cycle FROM case_draw_progress WHERE user_id=@u AND pool_id=@p",ct,("u",user.Id),("p",pool.Id)))[0];
                int pity=Convert.ToInt32(progress["pity"]);long cycle=Convert.ToInt64(progress["cycle"]);
                for(int i=0;i<body.Count;i++)
                {
                    var eligible=Eligible(pool,owned);int grade=GuaranteeGrade(eligible,pity);bool guaranteed=grade!=0;
                    var prize=PickGrade(eligible,grade);bool duplicate=prize.ItemId is not null&&owned.Contains(prize.ItemId);int credits=duplicate?DuplicateCredits:prize.Credits;
                    if(prize.ItemId is not null&&!duplicate)
                    {
                        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,@i);INSERT IGNORE INTO case_draw_acquisitions(user_id,pool_id,item_id) VALUES(@u,@p,@i)",ct,("u",user.Id),("p",pool.Id),("i",prize.ItemId));owned.Add(prize.ItemId);
                        if(prize.Grade<=2)await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO case_draw_unique(user_id,item_id) VALUES(@u,@i)",ct,("u",user.Id),("i",prize.ItemId));
                    }
                    if(credits>0)await CommunityEndpoints.Exec(c,tx,"UPDATE case_accounts SET credits=LEAST(1000000000,credits+@v) WHERE user_id=@u",ct,("u",user.Id),("v",credits));
                    pity=AdvancePity(pity,prize.Grade);if(prize.Grade==1)cycle++;
                    awards.Add(new(prize.Id,prize.Name,prize.Grade,prize.ItemId,credits,duplicate,guaranteed));
                }
                await CommunityEndpoints.Exec(c,tx,"UPDATE case_draw_wallet SET tickets=tickets-@n WHERE user_id=@u;UPDATE case_draw_progress SET pity=@v,cycle=@cycle,total=total+@n WHERE user_id=@u AND pool_id=@p",ct,("u",user.Id),("n",body.Count),("v",pity),("cycle",cycle),("p",pool.Id));
            }
            var result=new{requestId=key,operation=body.Operation,count=body.Count,poolId=pool?.Id,awards};string encoded=JsonSerializer.Serialize(result,Json);
            await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_draw_receipts(user_id,request_id,fingerprint,result_json,created_at_utc) VALUES(@u,@r,@f,@j,UTC_TIMESTAMP(6))",ct,("u",user.Id),("r",key),("f",fingerprint),("j",encoded));
            var view=await SnapshotAsync(c,tx,user.Id,ct);var snapshot=await CaseRecords.Snapshot(c,tx,user.Id,app.Configuration,ct);await tx.CommitAsync(ct);return Results.Ok(new{result,draw=view,snapshot});
        }).RequireRateLimiting("social");
    }
}
