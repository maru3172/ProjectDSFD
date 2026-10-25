using MySqlConnector;
using System.Text.RegularExpressions;
using System.Text.Json;

// A server-authored investigation schedule. IncidentTimestamp is story text, never this schedule's clock.
internal static partial class CaseInvestigation
{
    internal sealed record Record(string Id,string Title,string Description,string Map,string Region,int Stage,int Part,int Points=30);
    internal sealed record Visit(string MatchId,string UserId,string Region,string Map,int ServerPort,DateTimeOffset ObservedAtUtc);
    internal sealed record Settings(int WeeklyLimit,DateTimeOffset? EndsAtUtc=null,bool UpdateSeasonEnd=false);
    [GeneratedRegex("^[a-zA-Z0-9_.-]{1,48}$")] private static partial Regex IdPattern();
    internal static Record[] Definitions(IConfiguration config)
    {
        var records=config.GetSection("CaseRecords:Investigation:Records").Get<Record[]>()??[];
        return ValidDefinitions(records)?records:[];
    }
    private static bool ValidDefinitions(Record[] records)
    {
        // Refuse the whole schedule on ambiguity, rather than letting two definitions award the same part differently.
        if(records.Length!=117||records.Any(r=>r is null||r.Id is null||!IdPattern().IsMatch(r.Id)||r.Region is null||!IdPattern().IsMatch(r.Region)||
            string.IsNullOrWhiteSpace(r.Title)||r.Title.Length>100||r.Description is null||r.Description.Length>2000||
            r.Map is null||!r.Map.StartsWith("/Game/",StringComparison.Ordinal)||r.Map.Length>200||r.Map.Contains("..")||
            r.Stage is <1 or >13||r.Part is <1 or >9||r.Points is <0 or >500))return false;
        return records.Select(r=>r.Id).Distinct(StringComparer.Ordinal).Count()==records.Length&&records.Select(r=>(r.Stage,r.Part)).Distinct().Count()==records.Length;
    }
    private static async Task<Record[]> DefinitionsAsync(MySqlConnection c,MySqlTransaction? tx,IConfiguration config,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT records_json FROM case_investigation_catalogue WHERE singleton_id=1",ct);
        if(rows.Count==0)return Definitions(config);
        try{var records=JsonSerializer.Deserialize<Record[]>((string)rows[0]["records_json"]!)??[];return ValidDefinitions(records)?records:[];}catch(JsonException){return [];}
    }
    internal static string Day(DateTimeOffset utc)=>utc.ToOffset(TimeSpan.FromHours(9)).ToString("yyyy-MM-dd");
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_investigation_records(user_id BIGINT UNSIGNED NOT NULL,season VARCHAR(24) NOT NULL,record_id VARCHAR(48) NOT NULL,
          match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,visited_at_utc DATETIME(6) NOT NULL,claimed BOOLEAN NOT NULL DEFAULT FALSE,
          PRIMARY KEY(user_id,season,record_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_match_servers(match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,port INT NOT NULL);
        CREATE TABLE IF NOT EXISTS case_investigation_weekly(user_id BIGINT UNSIGNED NOT NULL,season VARCHAR(24) NOT NULL,week_id VARCHAR(16) NOT NULL,collected INT NOT NULL DEFAULT 0,
          PRIMARY KEY(user_id,season,week_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_investigation_settings(singleton_id TINYINT PRIMARY KEY,weekly_limit INT NOT NULL DEFAULT 15);
        INSERT IGNORE INTO case_investigation_settings(singleton_id,weekly_limit) VALUES(1,15);
        CREATE TABLE IF NOT EXISTS case_investigation_catalogue(singleton_id TINYINT PRIMARY KEY,records_json LONGTEXT NOT NULL);
        INSERT IGNORE INTO case_investigation_catalogue(singleton_id,records_json) VALUES(1,'[]');
        CREATE TABLE IF NOT EXISTS case_investigation_seasons(season VARCHAR(24) PRIMARY KEY,ends_at_utc DATETIME(6) NULL,archive_json LONGTEXT NULL);
        INSERT IGNORE INTO case_investigation_seasons(season) VALUES(@season);
        CREATE TABLE IF NOT EXISTS case_investigation_stories(season VARCHAR(24) NOT NULL,stage INT NOT NULL,title VARCHAR(100) NOT NULL,story LONGTEXT NOT NULL,PRIMARY KEY(season,stage));
        """,ct,("season",CaseRecords.Season));
    internal static async Task<int> WeeklyLimitAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT weekly_limit FROM case_investigation_settings WHERE singleton_id=1",ct);
        return rows.Count==0?15:Math.Clamp(Convert.ToInt32(rows[0]["weekly_limit"]),15,117);
    }
    internal static Task CaptureServerAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)=>CommunityEndpoints.Exec(c,tx,
        "INSERT IGNORE INTO case_match_servers(match_id,port) SELECT assigned_match_id,port FROM game_server_slots WHERE assigned_match_id=@m",ct,("m",match));
    internal static async Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,IConfiguration config,CancellationToken ct)
    {
        var records=await DefinitionsAsync(c,tx,config,ct);await PublishStoryArchiveAsync(c,tx,config,ct);
        var stories=await StoriesAsync(c,tx,ct);
        var end=await SeasonEndAsync(c,tx,ct);bool ended=end is not null&&DateTimeOffset.UtcNow>=end;
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT record_id recordId,claimed FROM case_investigation_records WHERE user_id=@u AND season=@s",ct,("u",user),("s",CaseRecords.Season));
        var found=rows.Select(r=>(string)r["recordId"]!).ToHashSet(StringComparer.Ordinal);
        bool StageDone(int stage)=>records.Where(r=>r.Stage==stage).All(r=>found.Contains(r.Id));
        var claims=await CommunityEndpoints.Rows(c,tx,"SELECT reward_key FROM case_claims WHERE user_id=@u AND reward_key LIKE @prefix",ct,("u",user),("prefix",CaseRecords.Season+":Investigation:%"));
        var claimed=claims.Select(r=>(string)r["reward_key"]!).ToHashSet();
        bool StoryOpen(int stage)=>ended||claimed.Contains(StageKey(stage));
        DateTimeOffset now=DateTimeOffset.UtcNow;
        int weeklyLimit=await WeeklyLimitAsync(c,tx,ct);string week=CaseRecords.Periods(now).Weekly;
        var usage=await CommunityEndpoints.Rows(c,tx,"SELECT collected FROM case_investigation_weekly WHERE user_id=@u AND season=@s AND week_id=@w",ct,("u",user),("s",CaseRecords.Season),("w",week));
        int used=usage.Count==0?0:Convert.ToInt32(usage[0]["collected"]);
        return new{enabled=records.Length==117,endsAtUtc=end,ended,todayKst=Day(now),totalParts=13,piecesPerPart=9,totalPieces=117,weeklyLimit,collectedThisWeek=used,remainingThisWeek=Math.Max(0,weeklyLimit-used),carryOver=false,
            records=records.Select(r=>new{id=r.Id,title=ended||found.Contains(r.Id)?r.Title:$"{r.Part}번 조각",r.Stage,r.Part,availableToday=!ended&&used<weeklyLimit,
            stageAvailable=records.Where(p=>p.Stage<r.Stage).All(p=>found.Contains(p.Id)),discovered=found.Contains(r.Id),
            description=ended||found.Contains(r.Id)?r.Description:"",points=r.Points,
            claimed=rows.Any(x=>(string)x["recordId"]! ==r.Id&&Convert.ToBoolean(x["claimed"]))}),
            stages=records.Select(r=>r.Stage).Distinct().Order().Select(s=>new{stage=s,parts=records.Count(r=>r.Stage==s),collected=records.Count(r=>r.Stage==s&&found.Contains(r.Id)),
                complete=StageDone(s),storyAvailable=StoryOpen(s),title=StoryOpen(s)?stories.Single(p=>p.Stage==s).Title:$"사건 기록 {s}파트",story=StoryOpen(s)?stories.Single(p=>p.Stage==s).Story:"",claimed=claimed.Contains(StageKey(s)),rareItem=RareItem(s),rareName=$"사건 기록 {s}단계 완성 봉인"})};
    }
    private static string StageKey(int stage)=>CaseRecords.Season+":Investigation:"+stage;
    internal static string RareItem(int stage)=>"investigation_seal_"+stage;
    internal static bool IsRareItem(string id)=>id.StartsWith("investigation_seal_",StringComparison.Ordinal)&&int.TryParse(id[19..],out int stage)&&stage is >=1 and <=13;
    internal static async Task<bool> ClaimAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string operation,string id,IConfiguration config,CancellationToken ct)
    {
        var records=await DefinitionsAsync(c,tx,config,ct);
        if(operation=="ClaimRecord")
        {
            var record=records.FirstOrDefault(r=>r.Id==id);if(record is null)return false;
            int changed=await CommunityEndpoints.Exec(c,tx,"UPDATE case_investigation_records SET claimed=TRUE WHERE user_id=@u AND season=@s AND record_id=@r AND claimed=FALSE",ct,("u",user),("s",CaseRecords.Season),("r",id));
            var exists=await CommunityEndpoints.Rows(c,tx,"SELECT record_id FROM case_investigation_records WHERE user_id=@u AND season=@s AND record_id=@r",ct,("u",user),("s",CaseRecords.Season),("r",id));
            if(exists.Count==0)return false;
            if(changed>0)await CommunityEndpoints.Exec(c,tx,"UPDATE case_accounts SET credits=LEAST(1000000000,credits+@p) WHERE user_id=@u",ct,("p",record.Points),("u",user));
            return true;
        }
        if(operation!="ClaimInvestigationStage"||!int.TryParse(id,out int stage))return false;
        var needed=records.Where(r=>r.Stage<=stage).ToArray();if(!needed.Any(r=>r.Stage==stage))return false;
        var collected=await CommunityEndpoints.Rows(c,tx,"SELECT record_id FROM case_investigation_records WHERE user_id=@u AND season=@s",ct,("u",user),("s",CaseRecords.Season));
        if(needed.Any(r=>!collected.Any(x=>(string)x["record_id"]! ==r.Id)))return false;
        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO case_claims(user_id,reward_key) VALUES(@u,@key);INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,@id)",ct,("u",user),("key",StageKey(stage)),("id",RareItem(stage)));
        return true;
    }
    internal static void MapCaseInvestigation(this WebApplication app)
    {
        app.MapCaseStoryArchives();
        app.MapGet("/api/admin/case-records/catalogue",async(HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);return Results.Ok(new{records=await DefinitionsAsync(c,null,app.Configuration,ct)});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/case-records/catalogue",async(Record[] body,HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(!ValidDefinitions(body))return Results.BadRequest(new{message="13파트별 1~9조각, 총 117개가 필요합니다. ID 중복, 맵·구역·문구를 확인하세요."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            await CommunityEndpoints.Rows(c,tx,"SELECT singleton_id FROM case_investigation_catalogue WHERE singleton_id=1 FOR UPDATE",ct);
            // Do not silently reassign an already discovered ID to a different part/location.
            var old=await DefinitionsAsync(c,tx,app.Configuration,ct);
            var collected=await CommunityEndpoints.Rows(c,tx,"SELECT DISTINCT record_id FROM case_investigation_records WHERE season=@s",ct,("s",CaseRecords.Season));
            foreach(var row in collected)
            {
                string id=(string)row["record_id"]!;var before=old.FirstOrDefault(r=>r.Id==id);var after=body.FirstOrDefault(r=>r.Id==id);
                if(before is null||after is null||before.Stage!=after.Stage||before.Part!=after.Part||before.Region!=after.Region||before.Map!=after.Map||before.Points!=after.Points)return Results.Conflict(new{message="이미 발견된 조각의 ID·파트·위치·보상은 변경할 수 없습니다. 문구는 수정할 수 있습니다."});
            }
            await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_investigation_catalogue(singleton_id,records_json) VALUES(1,@json) ON DUPLICATE KEY UPDATE records_json=@json",ct,("json",JsonSerializer.Serialize(body)));
            await tx.CommitAsync(ct);return Results.Ok(new{message="117조각의 조사 기록 구성을 저장했습니다."});
        }).RequireRateLimiting("admin");
        app.MapGet("/api/admin/case-records/settings",async(HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);return Results.Ok(new{weeklyLimit=await WeeklyLimitAsync(c,null,ct),endsAtUtc=await SeasonEndAsync(c,null,ct),archivePublished=await IsStoryArchivedAsync(c,null,ct),parts=13,piecesPerPart=9,totalPieces=117,carryOver=false});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/case-records/settings",async(Settings body,HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(body.WeeklyLimit is <15 or >117)return Results.BadRequest(new{message="주간 수집 가능 수량은 15~117개입니다."});
            await using var c=await db.OpenConnectionAsync(ct);
            await using var tx=await c.BeginTransactionAsync(ct);
            // A public archive is permanent. Weekly collection settings remain independently editable.
            if(body.UpdateSeasonEnd&&await IsStoryArchivedAsync(c,tx,ct)&&body.EndsAtUtc!=await SeasonEndAsync(c,tx,ct))return Results.Conflict(new{message="이미 전체 공개된 시즌은 다시 비공개로 되돌릴 수 없습니다."});
            await CommunityEndpoints.Exec(c,tx,"UPDATE case_investigation_settings SET weekly_limit=@n WHERE singleton_id=1",ct,("n",body.WeeklyLimit));
            if(body.UpdateSeasonEnd)await CommunityEndpoints.Exec(c,tx,"UPDATE case_investigation_seasons SET ends_at_utc=@e WHERE season=@s",ct,("e",body.EndsAtUtc?.UtcDateTime),("s",CaseRecords.Season));
            await PublishStoryArchiveAsync(c,tx,app.Configuration,ct);var end=await SeasonEndAsync(c,tx,ct);await tx.CommitAsync(ct);
            return Results.Ok(new{message="조사 설정을 적용했습니다. 기존 조각·보상·수집량은 유지됩니다.",weeklyLimit=body.WeeklyLimit,endsAtUtc=end});
        }).RequireRateLimiting("admin");
        app.MapPost("/api/server/case-records/visit",async(Visit body,HttpRequest request,AuthDatabase db,ProjectSecurityOptions security,CancellationToken ct)=>
        {
            if(!security.IsAuthorizedGameServer(request.Headers["X-ProjectProject01-Server-Secret"].ToString()))return Results.Unauthorized();
            if(!Guid.TryParse(body.MatchId,out var match)||!ulong.TryParse(body.UserId,out ulong user)||body.ServerPort is <1 or >65535)return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            var seasonEnd=await SeasonEndAsync(c,tx,ct);
            if(seasonEnd is not null&&DateTimeOffset.UtcNow>=seasonEnd)return Results.Conflict(new{message="시즌이 종료되었습니다. 공개된 이야기는 사건 기록 보관함에서 열람할 수 있습니다."});
            await CommunityEndpoints.Rows(c,tx,"SELECT singleton_id FROM case_investigation_catalogue WHERE singleton_id=1 FOR UPDATE",ct);
            DateTimeOffset now=DateTimeOffset.UtcNow;var all=await DefinitionsAsync(c,tx,app.Configuration,ct);
            var records=all.Where(r=>r.Region==body.Region&&r.Map==body.Map).ToArray();
            if(records.Length==0||body.ObservedAtUtc>now.AddMinutes(1)||body.ObservedAtUtc<now.AddHours(-48))return Results.Conflict(new{message="오늘의 조사 일정과 구역을 확인하세요."});
            // Freeze the admitted server port alongside the immutable roster. Retries after room removal remain verifiable.
            await CaptureServerAsync(c,tx,match.ToString(),ct);
            var admission=await CommunityEndpoints.Rows(c,tx,"""
                SELECT a.ended_at_utc ended FROM authoritative_match_players p JOIN authoritative_matches a ON a.match_id=p.match_id
                JOIN case_match_servers s ON s.match_id=p.match_id WHERE p.match_id=@m AND p.user_id=@u AND p.role IN ('Survivor','Mannequin')
                AND s.port=@port AND a.created_at_utc<=@at AND (a.ended_at_utc IS NULL OR a.ended_at_utc>=@at)
                """,ct,("m",match.ToString()),("u",user),("port",body.ServerPort),("at",body.ObservedAtUtc.UtcDateTime));
            if(admission.Count!=1)return Results.StatusCode(403);
            await CaseRecords.LockAccountAsync(c,tx,user,ct);
            var previous=await CommunityEndpoints.Rows(c,tx,"SELECT record_id FROM case_investigation_records WHERE user_id=@u AND season=@s",ct,("u",user),("s",CaseRecords.Season));
            var eligible=records.Where(record=>all.Where(r=>r.Stage<record.Stage).All(r=>previous.Any(p=>(string)p["record_id"]! ==r.Id))).ToArray();
            if(eligible.Length==0)return Results.Conflict(new{message="이전 단계의 조사 파츠를 먼저 완성하세요."});
            var unseen=eligible.Where(r=>!previous.Any(p=>(string)p["record_id"]! ==r.Id)).OrderBy(r=>r.Stage).ThenBy(r=>r.Part).ToArray();
            string week=CaseRecords.Periods(body.ObservedAtUtc).Weekly;
            var usage=await CommunityEndpoints.Rows(c,tx,"SELECT collected FROM case_investigation_weekly WHERE user_id=@u AND season=@s AND week_id=@w",ct,("u",user),("s",CaseRecords.Season),("w",week));
            int remaining=await WeeklyLimitAsync(c,tx,ct)-(usage.Count==0?0:Convert.ToInt32(usage[0]["collected"]));
            if(unseen.Length>0&&remaining<=0)return Results.Conflict(new{message="이번 주 조사 가능한 조각을 모두 수집했습니다."});
            var newlyFound=unseen.Take(Math.Max(0,remaining)).ToArray();
            foreach(var record in newlyFound)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_investigation_records(user_id,season,record_id,match_id,visited_at_utc) VALUES(@u,@s,@r,@m,@at)",ct,("u",user),("s",CaseRecords.Season),("r",record.Id),("m",match.ToString()),("at",body.ObservedAtUtc.UtcDateTime));
            if(newlyFound.Length>0)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_investigation_weekly(user_id,season,week_id,collected) VALUES(@u,@s,@w,@n) ON DUPLICATE KEY UPDATE collected=collected+@n",ct,("u",user),("s",CaseRecords.Season),("w",week),("n",newlyFound.Length));
            await tx.CommitAsync(ct);return Results.Ok(new{message="조사 결과를 확인했습니다.",newCount=newlyFound.Length,records=newlyFound.Select(r=>r.Id)});
        }).RequireRateLimiting("game-server");
    }
}
