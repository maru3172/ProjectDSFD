using MySqlConnector;

internal static class CaseCollection
{
    internal sealed record Cosmetic(string Id,string Name,string Slot,string? Achievement=null);
    internal sealed record DrawCosmetic(string Id,string Name,string Slot,int Grade);
    internal static readonly DrawCosmetic[] DrawExtras=[
        new("banner_lastlight","마지막 점등","Banner",1),new("banner_silentfloor","침묵의 층","Banner",1),new("frame_burntcase","그을린 사건철","Frame",1),
        new("banner_emergency","비상등 아래","Banner",2),new("banner_shutter","닫힌 셔터","Banner",2),new("plate_nightwatch","야간 경비 명찰","Nameplate",2),new("seal_unresolved","미결 사건 인장","Stamp",2),new("icon_masterkey","분실된 마스터키","Icon",2),
        new("frame_carbon","먹지 테두리","Frame",3),new("frame_evidence","증거 봉투 테두리","Frame",3),new("banner_escalator","멈춘 에스컬레이터","Banner",3),new("banner_service","직원 전용 통로","Banner",3),
        new("plate_lostfound","분실물 보관 명찰","Nameplate",3),new("stamp_closed","폐점 확인 소인","Stamp",3),new("badge_rooftop","옥상 출입 배지","Badge",3),new("icon_floormap","접힌 층별 안내도","Icon",3),
        new("icon_ticket","빛바랜 대기표","Icon",4),new("icon_locker","보관함 열쇠","Icon",4),new("stamp_received","접수 완료 도장","Stamp",4),new("stamp_checked","열람 확인 도장","Stamp",4),
        new("badge_basement","지하층 기념 배지","Badge",4),new("badge_service","안내 데스크 배지","Badge",4),new("frame_staple","스테이플 문서 테두리","Frame",4),new("plate_archive","기록실 직원 명찰","Nameplate",4)];
    internal static readonly Cosmetic[] Items=[
        new("stamp_checkpoint","첫 기록 인장","Stamp","campaign_checkpoint"),
        new("badge_exit","탈출구 기념 배지","Badge","campaign_escape"),
        new("seal_case","사건 종결 인장","Stamp","campaign_complete"),
        new("plate_investigator","조사 직원 명찰","Nameplate","pvp_first"),
        new("plate_training","교육 직원 명찰","Nameplate","bot_first"),
        new("badge_survivor","생존 기록 배지","Badge","survivor_escape"),
        new("stamp_mannequin","추적 기록 도장","Stamp","mannequin_victory"),
        new("banner_afterhours","폐점 이후","Banner"),new("banner_embers","잔불","Banner"),
        new("frame_archive","보관 문서 테두리","Frame"),new("icon_receipt","오래된 영수증","Icon"),
        new("stamp_store","백화점 소인","Stamp"),new("badge_directory","층별 안내 배지","Badge"),
        ..DrawExtras.Select(p=>new Cosmetic(p.Id,p.Name,p.Slot))];
    internal static Cosmetic? Find(string id)=>Items.FirstOrDefault(i=>i.Id==id);
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_featured_achievements(user_id BIGINT UNSIGNED NOT NULL,position TINYINT NOT NULL,code VARCHAR(40) NOT NULL,
          PRIMARY KEY(user_id,position),UNIQUE KEY(user_id,code),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_profile_banners(user_id BIGINT UNSIGNED NOT NULL,item_id VARCHAR(32) NOT NULL,position INT NOT NULL,
          PRIMARY KEY(user_id,item_id),FOREIGN KEY(user_id,item_id) REFERENCES case_inventory(user_id,item_id) ON DELETE CASCADE);
        INSERT IGNORE INTO case_profile_banners(user_id,item_id,position) SELECT user_id,item_id,0 FROM case_loadout WHERE slot='Banner';
        """,ct);
    internal static Task ShowBannerAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string item,CancellationToken ct)=>CommunityEndpoints.Exec(c,tx,
        "INSERT IGNORE INTO case_profile_banners(user_id,item_id,position) SELECT @u,@i,COALESCE(MAX(position),0)+1 FROM case_profile_banners WHERE user_id=@u",ct,("u",user),("i",item));
    internal static Task<List<Dictionary<string,object?>>> BannersAsync(MySqlConnection c,MySqlTransaction? tx,ulong user,CancellationToken ct)=>CommunityEndpoints.Rows(c,tx,"""
        SELECT item_id itemId,position FROM case_profile_banners WHERE user_id=@u
        UNION ALL SELECT l.item_id itemId,0 position FROM case_loadout l WHERE l.user_id=@u AND l.slot='Banner'
          AND NOT EXISTS(SELECT 1 FROM case_profile_banners b WHERE b.user_id=l.user_id AND b.item_id=l.item_id)
        ORDER BY position,itemId
        """,ct,("u",user));
    internal static async Task<bool> DisplayBannerAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string item,bool show,CancellationToken ct)
    {
        if(Find(item) is not {Slot:"Banner"}||(await CommunityEndpoints.Rows(c,tx,"SELECT item_id FROM case_inventory WHERE user_id=@u AND item_id=@i",ct,("u",user),("i",item))).Count==0)return false;
        if(show){await ShowBannerAsync(c,tx,user,item,ct);return true;}
        await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_profile_banners WHERE user_id=@u AND item_id=@i",ct,("u",user),("i",item));
        var primary=await CommunityEndpoints.Rows(c,tx,"SELECT item_id FROM case_loadout WHERE user_id=@u AND slot='Banner'",ct,("u",user));
        if(primary.Count>0&&(string)primary[0]["item_id"]! ==item)
        {
            await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_loadout WHERE user_id=@u AND slot='Banner'",ct,("u",user));
            var remaining=await BannersAsync(c,tx,user,ct);
            if(remaining.Count>0)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_loadout(user_id,slot,item_id) VALUES(@u,'Banner',@i)",ct,("u",user),("i",remaining[0]["itemId"]));
        }
        return true;
    }
    internal static async Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var profiles=await CommunityEndpoints.Rows(c,tx,"SELECT bio FROM social_profiles WHERE user_id=@u",ct,("u",user));
        return new{catalogue=Items,bio=profiles.Count==0?"":(string)profiles[0]["bio"]!,displayedBanners=await BannersAsync(c,tx,user,ct),featured=await FeaturedAsync(c,tx,user,ct),draw=await CaseDraw.SnapshotAsync(c,tx,user,ct)};
    }
    internal static Task<List<Dictionary<string,object?>>> FeaturedAsync(MySqlConnection c,MySqlTransaction? tx,ulong user,CancellationToken ct)=>CommunityEndpoints.Rows(c,tx,
        "SELECT f.code,f.position FROM case_featured_achievements f JOIN case_achievements a ON a.user_id=f.user_id AND a.code=f.code WHERE f.user_id=@u ORDER BY f.position",ct,("u",user));
    internal static async Task<object> AchievementRewardsAsync(MySqlConnection c,MySqlTransaction tx,ulong user,CancellationToken ct)
    {
        var earned=(await CommunityEndpoints.Rows(c,tx,"SELECT code FROM case_achievements WHERE user_id=@u",ct,("u",user))).Select(r=>(string)r["code"]!).ToHashSet();
        var owned=(await CommunityEndpoints.Rows(c,tx,"SELECT item_id FROM case_inventory WHERE user_id=@u",ct,("u",user))).Select(r=>(string)r["item_id"]!).ToHashSet();
        // Preserve previously auto-granted inventory as already claimed.
        return Items.Where(i=>i.Achievement is not null).Select(i=>new{code=i.Achievement,itemId=i.Id,name=i.Name,slot=i.Slot,earned=earned.Contains(i.Achievement!),claimed=owned.Contains(i.Id)}).ToArray();
    }
    internal static async Task<Cosmetic?> ClaimAchievementAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string code,CancellationToken ct)
    {
        var item=Items.FirstOrDefault(i=>i.Achievement==code);if(item is null)return null;
        if((await CommunityEndpoints.Rows(c,tx,"SELECT code FROM case_achievements WHERE user_id=@u AND code=@code",ct,("u",user),("code",code))).Count==0)return null;
        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,@item);INSERT IGNORE INTO case_claims(user_id,reward_key) VALUES(@u,@key)",ct,("u",user),("item",item.Id),("key","Achievement:"+code));
        return item;
    }
    internal sealed record FeaturedRequest(string[]? Codes,string? Bio=null);
    internal static void Map(WebApplication app)
    {
        CaseDraw.Map(app);
        app.MapPost("/api/case-records/featured",async(FeaturedRequest body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            if(body.Codes is null||body.Codes.Length>3||body.Codes.Any(s=>s is null||!CaseRecords.AchievementIds.Contains(s))||body.Codes.Distinct().Count()!=body.Codes.Length)return Results.BadRequest();
            if(body.Bio is not null&&(body.Bio.Length>500||body.Bio.Any(ch=>char.IsControl(ch)&&ch is not ('\r' or '\n' or '\t'))))return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await CaseRecords.LockAccountAsync(c,tx,user.Id,ct);
            var earned=await CommunityEndpoints.Rows(c,tx,"SELECT code FROM case_achievements WHERE user_id=@u",ct,("u",user.Id));
            if(body.Codes.Any(code=>!earned.Any(r=>(string)r["code"]! ==code)))return Results.Conflict(new{message="달성한 업적·도전과제만 대표로 지정할 수 있습니다."});
            await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_featured_achievements WHERE user_id=@u",ct,("u",user.Id));
            for(int n=0;n<body.Codes.Length;n++)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_featured_achievements(user_id,position,code) VALUES(@u,@p,@code)",ct,("u",user.Id),("p",n),("code",body.Codes[n]));
            if(body.Bio is not null)await CommunityEndpoints.Exec(c,tx,"INSERT INTO social_profiles(user_id,bio,accent,title,status_message) VALUES(@u,@b,'Slate','Rookie','') ON DUPLICATE KEY UPDATE bio=@b,updated_at_utc=UTC_TIMESTAMP(6)",ct,("u",user.Id),("b",body.Bio.Trim()));
            await tx.CommitAsync(ct);return Results.Ok(new{message="대표 업적·도전과제를 저장했습니다."});
        }).RequireRateLimiting("social");
    }
}
