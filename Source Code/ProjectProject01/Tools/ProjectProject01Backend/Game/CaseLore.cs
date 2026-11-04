using MySqlConnector;
using System.Text.Json;
using System.Text.RegularExpressions;

// 기본 기록: operator-written lore (the player's own file and everything around it), opened per account by a condition.
// A locked entry never leaves the server with its title or text, only its category and what opens it.
internal static partial class CaseLore
{
    internal sealed record Entry(string Id,string Category,string Title,string Body,string Unlock,string Value="",int Order=0);
    internal static readonly string[] Unlocks=["always","achievement","level","part"];
    internal const int MaxEntries=200,MaxBody=20000;
    [GeneratedRegex("^[a-zA-Z0-9_.-]{1,48}$")] private static partial Regex IdPattern();
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_lore(singleton_id TINYINT PRIMARY KEY,entries_json LONGTEXT NOT NULL);
        INSERT IGNORE INTO case_lore(singleton_id,entries_json) VALUES(1,'[]');
        """,ct);
    internal static string? Invalid(Entry[]? entries)
    {
        if(entries is null||entries.Length>MaxEntries)return $"기본 기록은 {MaxEntries}개까지입니다.";
        foreach(var e in entries)
        {
            if(e is null||e.Id is null||!IdPattern().IsMatch(e.Id))return "기록 ID는 영문·숫자·_.- 48자까지입니다.";
            if(string.IsNullOrWhiteSpace(e.Category)||e.Category.Length>20)return $"{e.Id}: 분류는 1~20자입니다.";
            if(string.IsNullOrWhiteSpace(e.Title)||e.Title.Length>100)return $"{e.Id}: 제목은 1~100자입니다.";
            if(e.Body is null||e.Body.Length>MaxBody)return $"{e.Id}: 본문은 {MaxBody:N0}자까지입니다.";
            if(!Unlocks.Contains(e.Unlock))return $"{e.Id}: 열림 조건을 고르세요.";
            string value=e.Value??"";
            bool ok=e.Unlock switch
            {
                "always"=>true,
                "achievement"=>IdPattern().IsMatch(value),
                "level"=>int.TryParse(value,out int level)&&level is >=1 and <=100,
                _=>int.TryParse(value,out int part)&&part is >=1 and <=13
            };
            if(!ok)return $"{e.Id}: 조건 값이 맞지 않습니다(업적 코드 / 레벨 1~100 / 파트 1~13).";
        }
        return entries.Select(e=>e.Id).Distinct(StringComparer.Ordinal).Count()==entries.Length?null:"같은 기록 ID가 두 번 있습니다.";
    }
    internal static async Task<Entry[]> EntriesAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT entries_json FROM case_lore WHERE singleton_id=1",ct);
        if(rows.Count==0)return [];
        try{var entries=JsonSerializer.Deserialize<Entry[]>((string)rows[0]["entries_json"]!)??[];return Invalid(entries) is null?entries:[];}catch(JsonException){return [];}
    }
    internal static async Task<object> SnapshotAsync(MySqlConnection c,MySqlTransaction tx,ulong user,int level,CancellationToken ct)
    {
        var entries=await EntriesAsync(c,tx,ct);
        var earned=(await CommunityEndpoints.Rows(c,tx,"SELECT code FROM case_achievements WHERE user_id=@u",ct,("u",user))).Select(r=>(string)r["code"]!).ToHashSet(StringComparer.Ordinal);
        var claimed=(await CommunityEndpoints.Rows(c,tx,"SELECT reward_key FROM case_claims WHERE user_id=@u AND reward_key LIKE @prefix",ct,("u",user),("prefix",CaseRecords.Season+":Investigation:%"))).Select(r=>(string)r["reward_key"]!).ToHashSet();
        var end=await CaseInvestigation.SeasonEndAsync(c,tx,ct);bool ended=end is not null&&DateTimeOffset.UtcNow>=end;
        // A part's lore opens with the part's story: its reward claimed, or the season over.
        bool Open(Entry e)=>e.Unlock switch
        {
            "always"=>true,
            "achievement"=>earned.Contains(e.Value),
            "level"=>level>=int.Parse(e.Value),
            _=>ended||claimed.Contains(CaseRecords.Season+":Investigation:"+e.Value)
        };
        return new{records=entries.OrderBy(e=>e.Order).ThenBy(e=>e.Id,StringComparer.Ordinal).Select(e=>
        {
            bool open=Open(e);
            return new{id=e.Id,category=e.Category,unlocked=open,title=open?e.Title:"",body=open?e.Body:"",unlock=e.Unlock,value=e.Unlock=="always"?"":e.Value};
        })};
    }
    internal static void MapCaseLore(this WebApplication app)
    {
        app.MapGet("/api/admin/case-records/lore",async(HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);return Results.Ok(new{entries=await EntriesAsync(c,null,ct)});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/case-records/lore",async(Entry[] body,HttpRequest request,AuthDatabase db,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            body=body?.Select((e,i)=>e is null?e!:e with{Value=e.Unlock=="always"?"":(e.Value??"").Trim(),Order=i}).ToArray()!;
            if(Invalid(body) is string message)return Results.BadRequest(new{message});
            await using var c=await db.OpenConnectionAsync(ct);
            await CommunityEndpoints.Exec(c,null,"UPDATE case_lore SET entries_json=@j WHERE singleton_id=1",ct,("j",JsonSerializer.Serialize(body)));
            return Results.Ok(new{message="기본 기록을 저장했습니다."});
        }).RequireRateLimiting("admin");
    }
}
