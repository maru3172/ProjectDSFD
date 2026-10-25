using MySqlConnector;
using System.Text.Json;

internal static partial class CaseInvestigation
{
    internal sealed record PartStory(int Stage,string Title,string Story);
    // Piece descriptions are clues. Full stories are separately authored and never concatenated from clues.
    internal static async Task<PartStory[]> StoriesAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT stage,title,story FROM case_investigation_stories WHERE season=@s",ct,("s",CaseRecords.Season));
        return Enumerable.Range(1,13).Select(stage=>
        {
            var row=rows.SingleOrDefault(r=>Convert.ToInt32(r["stage"])==stage);
            return row is null?new PartStory(stage,$"사건 기록 {stage}파트",""):new PartStory(stage,(string)row["title"]!,(string)row["story"]!);
        }).ToArray();
    }
    internal static async Task<DateTimeOffset?> SeasonEndAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT ends_at_utc FROM case_investigation_seasons WHERE season=@s",ct,("s",CaseRecords.Season));
        return rows.Count==0||rows[0]["ends_at_utc"] is null or DBNull?null:new DateTimeOffset(DateTime.SpecifyKind((DateTime)rows[0]["ends_at_utc"]!,DateTimeKind.Utc));
    }
    internal static async Task<bool> IsStoryArchivedAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"SELECT season FROM case_investigation_seasons WHERE season=@s AND archive_json IS NOT NULL",ct,("s",CaseRecords.Season))).Count>0;
    internal static async Task PublishStoryArchiveAsync(MySqlConnection c,MySqlTransaction? tx,IConfiguration config,CancellationToken ct)
    {
        var end=await SeasonEndAsync(c,tx,ct);
        if(end is null||DateTimeOffset.UtcNow<end||await IsStoryArchivedAsync(c,tx,ct))return;
        var records=await DefinitionsAsync(c,tx,config,ct);if(records.Length!=117)return;
        var stories=await StoriesAsync(c,tx,ct);
        string json=JsonSerializer.Serialize(new{season=CaseRecords.Season,incidentTimestamp="202X-XX-XX 18:24:01",endedAtUtc=end,
            stages=stories.Select(s=>new{stage=s.Stage,title=s.Title,story=s.Story})});
        // First publication freezes a full copy: later catalogue edits cannot erase the archive.
        await CommunityEndpoints.Exec(c,tx,"UPDATE case_investigation_seasons SET archive_json=@j WHERE season=@s AND archive_json IS NULL",ct,("j",json),("s",CaseRecords.Season));
    }
    private static void MapCaseStoryArchives(this WebApplication app)
    {
        app.MapGet("/api/admin/case-records/stories",async(HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);return Results.Ok(new{stories=await StoriesAsync(c,null,ct)});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/case-records/stories",async(PartStory[] body,HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(body is null||body.Length!=13||body.Any(s=>s is null||s.Stage is <1 or >13||string.IsNullOrWhiteSpace(s.Title)||s.Title.Length>100||s.Story is null||s.Story.Length>20000)||body.Select(s=>s.Stage).Distinct().Count()!=13)
                return Results.BadRequest(new{message="1~13파트를 각각 한 번 작성하세요. 제목은 100자, 스토리는 파트당 20,000자까지입니다."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            foreach(var s in body)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_investigation_stories(season,stage,title,story) VALUES(@season,@stage,@title,@story) ON DUPLICATE KEY UPDATE title=@title,story=@story",ct,("season",CaseRecords.Season),("stage",s.Stage),("title",s.Title),("story",s.Story));
            await tx.CommitAsync(ct);return Results.Ok(new{message="파트별 스토리를 저장했습니다."});
        }).RequireRateLimiting("admin");
        app.MapGet("/api/case-records/story-archives",async(AuthDatabase db,CancellationToken ct)=>
        {
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            await PublishStoryArchiveAsync(c,tx,app.Configuration,ct);
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT archive_json FROM case_investigation_seasons WHERE archive_json IS NOT NULL ORDER BY ends_at_utc DESC,season",ct);
            var archives=new List<JsonElement>();foreach(var row in rows){using var json=JsonDocument.Parse((string)row["archive_json"]!);archives.Add(json.RootElement.Clone());}
            await tx.CommitAsync(ct);return Results.Ok(new{archives});
        }).RequireRateLimiting("lobby");
    }
}
