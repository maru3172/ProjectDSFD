using System.Globalization;
using System.Text;
using MySqlConnector;

/// <summary>
/// Banned words for text other players see (Docs/ChatFilter.md). Chat and 1:1 messages are sent with the words masked
/// (*); names, room names, profile text and shared preset posts are refused. Matching ignores letter case, spaces,
/// punctuation and other separators put between letters (for example "시 . 발"), and an allowed word wins over a banned
/// word inside it (시발점). The lists live in the database and are edited in the 운영 도우미; a default list is put in
/// once, so a word an operator removes stays removed.
/// </summary>
internal sealed class ChatFilter
{
    internal const int MaxWords=2000,MaxWordLength=40;
    internal const string Refusal="사용할 수 없는 말이 들어 있습니다. 바꿔서 다시 입력하세요.";
    // A starting list: common Korean and English swear words and their usual shortenings. Words that are also everyday
    // Korean on their own (새끼 "young animal", 보지 마 "don't look", 졸라 "pester") are left out on purpose.
    internal static readonly string[] DefaultWords=["시발","씨발","씨빨","씨바","시바","ㅅㅂ","ㅆㅂ","ㅅ1ㅂ","병신","븅신","ㅂㅅ","개새끼","개새","개색기","개섀끼","ㄱㅅㄲ","씹새","씹새끼","좆","좆같","존나","ㅈㄴ",
        "지랄","ㅈㄹ","미친놈","미친년","엠창","느금마","니애미","니미럴","애미뒤진","썅","fuck","fucking","shit","bitch","asshole","motherfucker","cunt"];
    internal static readonly string[] DefaultAllowed=["시발점","시바견"];

    readonly string[] words,allowed;
    ChatFilter(IEnumerable<string> words,IEnumerable<string> allowed)
    {
        this.words=words.Select(Key).Where(w=>w.Length>0).Distinct().ToArray();
        this.allowed=allowed.Select(Key).Where(w=>w.Length>0).Distinct().ToArray();
    }
    internal static ChatFilter Create(IEnumerable<string> words,IEnumerable<string> allowed)=>new(words,allowed);

    /// <summary>The comparable form of a word: letters and digits only, lower case.</summary>
    internal static string Key(string text){var (key,_)=Normalize(text);return key;}
    static (string Key,int[] Source) Normalize(string text)
    {
        var key=new StringBuilder(text.Length);var source=new List<int>(text.Length);
        for(int i=0;i<text.Length;i++)
        {
            char c=text[i];var category=char.GetUnicodeCategory(c);
            // Letters (Hangul syllables and jamo included) and digits count; spaces, punctuation, symbols and invisible
            // characters placed between letters are skipped, so they cannot hide a word.
            if(!char.IsLetterOrDigit(c)||category is UnicodeCategory.Format or UnicodeCategory.NonSpacingMark)continue;
            key.Append(char.ToLowerInvariant(c));source.Add(i);
        }
        return(key.ToString(),source.ToArray());
    }
    /// <summary>The banned spans in the text, as ranges of the original text (allowed words excepted).</summary>
    List<(int Start,int End)> Find(string text)
    {
        var (key,source)=Normalize(text);var safe=new bool[key.Length];var hits=new List<(int,int)>();
        foreach(var a in allowed)for(int at=key.IndexOf(a,StringComparison.Ordinal);at>=0;at=key.IndexOf(a,at+1,StringComparison.Ordinal))for(int k=at;k<at+a.Length;k++)safe[k]=true;
        foreach(var w in words)
            for(int at=key.IndexOf(w,StringComparison.Ordinal);at>=0;at=key.IndexOf(w,at+1,StringComparison.Ordinal))
            {
                bool covered=true;for(int k=at;k<at+w.Length;k++)covered&=safe[k];
                if(!covered)hits.Add((source[at],source[at+w.Length-1]));
            }
        return hits;
    }
    internal bool Contains(string? text)=>!string.IsNullOrEmpty(text)&&Find(text).Count>0;
    /// <summary>The text with every banned word (and anything put between its letters) replaced by *.</summary>
    internal string Mask(string text)
    {
        var hits=Find(text);if(hits.Count==0)return text;var chars=text.ToCharArray();
        foreach(var (start,end) in hits)for(int i=start;i<=end;i++)if(!char.IsWhiteSpace(chars[i]))chars[i]='*';
        return new string(chars);
    }

    // ---- Storage: two small tables, read through a short cache. ----
    static ChatFilter? cached;static DateTime cachedAt;static readonly object gate=new();
    internal static async Task EnsureAsync(MySqlConnection c,CancellationToken ct)
    {
        await CommunityEndpoints.Exec(c,null,"""
            CREATE TABLE IF NOT EXISTS chat_filter_words(word VARCHAR(40) NOT NULL,allowed BOOLEAN NOT NULL,PRIMARY KEY(word,allowed));
            CREATE TABLE IF NOT EXISTS chat_filter_meta(id TINYINT PRIMARY KEY,seeded BOOLEAN NOT NULL DEFAULT FALSE,revision BIGINT NOT NULL DEFAULT 0);
            INSERT IGNORE INTO chat_filter_meta(id) VALUES(1);
            """,ct);
        await using var tx=await c.BeginTransactionAsync(ct);
        var meta=await CommunityEndpoints.Rows(c,tx,"SELECT seeded FROM chat_filter_meta WHERE id=1 FOR UPDATE",ct);
        if(!Convert.ToBoolean(meta[0]["seeded"]))
        {
            foreach(var w in DefaultWords)await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO chat_filter_words(word,allowed) VALUES(@w,FALSE)",ct,("w",w));
            foreach(var w in DefaultAllowed)await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO chat_filter_words(word,allowed) VALUES(@w,TRUE)",ct,("w",w));
            await CommunityEndpoints.Exec(c,tx,"UPDATE chat_filter_meta SET seeded=TRUE WHERE id=1",ct);
        }
        await tx.CommitAsync(ct);
    }
    static async Task<(string[] Words,string[] Allowed,long Revision)> ReadAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT word,allowed FROM chat_filter_words ORDER BY word",ct);
        var revision=Convert.ToInt64((await CommunityEndpoints.Rows(c,tx,"SELECT revision FROM chat_filter_meta WHERE id=1",ct))[0]["revision"]);
        return([..rows.Where(r=>!Convert.ToBoolean(r["allowed"])).Select(r=>(string)r["word"]!)],[..rows.Where(r=>Convert.ToBoolean(r["allowed"])).Select(r=>(string)r["word"]!)],revision);
    }
    /// <summary>The current filter; the lists are read again at most every 30 seconds (and right after a change here).</summary>
    internal static async Task<ChatFilter> CurrentAsync(MySqlConnection c,MySqlTransaction? tx,CancellationToken ct)
    {
        lock(gate)if(cached is not null&&DateTime.UtcNow-cachedAt<TimeSpan.FromSeconds(30))return cached;
        var (words,allowed,_)=await ReadAsync(c,tx,ct);var filter=new ChatFilter(words,allowed);
        lock(gate){cached=filter;cachedAt=DateTime.UtcNow;}
        return filter;
    }
    static void Forget(){lock(gate)cached=null;}
    /// <summary>The current filter for a request that has no connection open yet (opens one only when the cache is stale).</summary>
    internal static async Task<ChatFilter> CurrentAsync(AuthDatabase db,CancellationToken ct)
    {
        lock(gate)if(cached is not null&&DateTime.UtcNow-cachedAt<TimeSpan.FromSeconds(30))return cached;
        await using var c=await db.OpenConnectionAsync(ct);return await CurrentAsync(c,null,ct);
    }

    internal sealed record ListsRequest(long Revision,string[]? Words,string[]? Allowed);
    internal sealed record TestRequest(string? Text,string[]? Words,string[]? Allowed);
    internal static void Map(WebApplication app)
    {
        app.MapGet("/api/admin/community/chat-filter",async(HttpRequest req,AdministratorOptions admin,AuthDatabase db,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);var (words,allowed,revision)=await ReadAsync(c,null,ct);
            return Results.Ok(new{revision,words,allowed});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/community/chat-filter",async(ListsRequest body,HttpRequest req,AdministratorOptions admin,AuthDatabase db,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            static string[] Clean(string[]? list)=>(list??[]).Select(w=>w.Trim()).Where(w=>w.Length>0).Distinct().ToArray();
            var words=Clean(body.Words);var allowed=Clean(body.Allowed);
            if(words.Length+allowed.Length>MaxWords||words.Concat(allowed).Any(w=>w.Length>MaxWordLength||Key(w).Length==0))
                return Results.BadRequest(new{message=$"단어는 모두 {MaxWords}개 이하, 하나에 {MaxWordLength}자 이하이고 글자나 숫자가 있어야 합니다."});
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            var meta=await CommunityEndpoints.Rows(c,tx,"SELECT revision FROM chat_filter_meta WHERE id=1 FOR UPDATE",ct);
            if(Convert.ToInt64(meta[0]["revision"])!=body.Revision)return Results.Conflict(new{message="다른 곳에서 먼저 저장했습니다. 다시 불러오세요."});
            await CommunityEndpoints.Exec(c,tx,"DELETE FROM chat_filter_words",ct);
            foreach(var w in words)await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO chat_filter_words(word,allowed) VALUES(@w,FALSE)",ct,("w",w));
            foreach(var w in allowed)await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO chat_filter_words(word,allowed) VALUES(@w,TRUE)",ct,("w",w));
            await CommunityEndpoints.Exec(c,tx,"UPDATE chat_filter_meta SET revision=revision+1,seeded=TRUE WHERE id=1",ct);
            await tx.CommitAsync(ct);Forget();
            return Results.Ok(new{message="저장했습니다. 이미 남겨진 채팅·글에는 적용되지 않고 새로 쓰는 글부터 적용됩니다.",revision=body.Revision+1});
        }).RequireRateLimiting("admin");
        // Try a sentence against the lists being edited (not saved yet), as the game would treat it.
        app.MapPost("/api/admin/community/chat-filter/test",(TestRequest body,HttpRequest req,AdministratorOptions admin)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            var text=body.Text;if(string.IsNullOrEmpty(text)||text.Length>500)return Results.BadRequest(new{message="시험할 문장을 500자 이내로 입력하세요."});
            var filter=new ChatFilter(body.Words??[],body.Allowed??[]);
            return Results.Ok(new{masked=filter.Mask(text),blocked=filter.Contains(text)});
        }).RequireRateLimiting("admin");
    }
}
