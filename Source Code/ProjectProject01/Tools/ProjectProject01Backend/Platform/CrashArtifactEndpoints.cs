using System.IO.Compression;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Http.Features;
using MySqlConnector;

internal static class CrashArtifactEndpoints
{
    internal const long MaximumFileBytes = 64L * 1024 * 1024;
    internal static readonly HashSet<string> Names = new(StringComparer.Ordinal)
        { "Crash.dmp", "Game.log", "CrashContext.xml" };

    private static string Redact(string text,string pattern,string replacement)
    {
        try {return Regex.Replace(text,pattern,replacement,RegexOptions.None,TimeSpan.FromSeconds(1));}
        catch(RegexMatchTimeoutException) {return "[REDACTED: text processing limit exceeded]";}
    }

    internal static string ScrubText(string text)
    {
        text = Redact(text, @"(?is)(<CommandLine>).*?(?=</CommandLine>)", "$1[REDACTED]");
        text = Redact(text, @"(?im)(Command line:)[^\r\n]*", "$1[REDACTED]");
        text = Redact(text, @"(?i)(Bearer\s+)[A-Za-z0-9._~+/=-]+", "$1[REDACTED]");
        text = Redact(text, @"\beyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\b", "[JWT-REDACTED]");
        text = Redact(text,
            @"(?i)((?:access[_-]?token|refresh[_-]?token|id[_-]?token|productUserIdToken|JsonWebToken|participant[_-]?token|client[_-]?secret|password|authorization|encryption[_-]?key|api[_-]?key|game[_-]?server[_-]?secret|admin[_-]?key|EncryptionToken)[""']?\s*[:=]\s*)(?:""[^""]*""|'[^']*'|[^\s<>;,]+)",
            "$1[REDACTED]");
        text = Redact(text,@"(?is)(<(access[_-]?token|refresh[_-]?token|id[_-]?token|productUserIdToken|JsonWebToken|participant[_-]?token|client[_-]?secret|password|encryption[_-]?key)>).*?(?=</\2>)","$1[REDACTED]");
        text = Redact(text, @"(?i)C:\\Users\\[^\\\s<>""']+", @"C:\Users\[USER]");
        return text;
    }

    public static void MapProjectProject01CrashArtifacts(this WebApplication app)
    {
        app.MapGet("/api/admin/crash-artifact-policy", async (HttpRequest request,AdministratorOptions admin,CrashArtifactStore store,CancellationToken ct) =>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"].ToString()))return Results.Unauthorized();
            return Results.Ok(await store.StatusAsync(ct));
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/crash-artifact-policy", async (HttpRequest request,AdministratorOptions admin,CrashArtifactStore store,CrashArtifactPolicy policy,CancellationToken ct) =>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"].ToString()))return Results.Unauthorized();
            if(!CrashArtifactStore.IsValid(policy))return Results.BadRequest();
            await store.SetPolicyAsync(policy,ct);
            return Results.Ok(await store.StatusAsync(ct));
        }).RequireRateLimiting("admin");
        app.MapPost("/api/crash-reports/{reportId}/artifacts/{name}", async (
            string reportId, string name, HttpRequest request, AuthDatabase database, CrashArtifactStore store, CancellationToken ct) =>
        {
            if (!Guid.TryParse(reportId, out var id) || !Names.Contains(name)) return Results.BadRequest();
            if(name=="Crash.dmp" && !store.Policy.AllowMinidumps)return Results.NoContent();
            if (request.ContentLength is > MaximumFileBytes) return Results.StatusCode(413);
            var limit = request.HttpContext.Features.Get<IHttpMaxRequestBodySizeFeature>();
            if (limit is { IsReadOnly: false }) limit.MaxRequestBodySize = MaximumFileBytes;
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var exists = new MySqlCommand("SELECT COUNT(*) FROM crash_reports WHERE report_id=@id;", connection);
            exists.Parameters.AddWithValue("@id", id.ToString("D"));
            if (Convert.ToInt32(await exists.ExecuteScalarAsync(ct)) == 0) return Results.NotFound();
            // Bound memory across uploads, and serialize capacity checks with writes/retention.
            using var timeout=CancellationTokenSource.CreateLinkedTokenSource(ct);timeout.CancelAfter(TimeSpan.FromSeconds(60));
            if(!await store.Gate.WaitAsync(TimeSpan.FromSeconds(2),ct))return Results.StatusCode(429);
            try
            {
            ct=timeout.Token;
            using var memory = new MemoryStream();
            byte[] buffer = new byte[81920];
            int read;
            while ((read = await request.Body.ReadAsync(buffer, ct)) > 0)
            {
                if (memory.Length + read > MaximumFileBytes) return Results.StatusCode(413);
                await memory.WriteAsync(buffer.AsMemory(0,read),ct);
            }
            byte[] bytes=memory.ToArray();
            if (bytes.Length == 0 || (name == "Crash.dmp" &&
                (bytes.Length < 4 || Encoding.ASCII.GetString(bytes,0,4)!="MDMP"))) return Results.BadRequest();
            if (name != "Crash.dmp") bytes=Encoding.UTF8.GetBytes(ScrubText(Encoding.UTF8.GetString(bytes)));
            if(bytes.Length>MaximumFileBytes)return Results.StatusCode(413);
            string directory=store.ReportDirectory(id);
            string destination=Path.Combine(directory,name);
            if(File.Exists(destination) && (File.GetAttributes(destination)&FileAttributes.ReparsePoint)!=0)return Results.BadRequest();
            if(!store.HasCapacity(bytes.Length,destination))return Results.Json(new {message="크래시 첨부 저장 용량 한도입니다. 운영자에게 문의하세요."},statusCode:507);
            if(name=="Crash.dmp" && !store.Policy.AllowMinidumps)return Results.NoContent();
            Directory.CreateDirectory(directory);
            string temporary=Path.Combine(directory,$"{Guid.NewGuid():N}.upload");
            try
            {
                await File.WriteAllBytesAsync(temporary,bytes,ct);
                File.Move(temporary,destination,true);
            }
            finally { if(File.Exists(temporary))File.Delete(temporary); }
            return Results.Ok(new { name, bytes=bytes.Length });
            }
            finally {store.Gate.Release();}
        }).RequireRateLimiting("crash-report");

        app.MapGet("/api/admin/crash-reports/{reportId}/artifacts", async (
            string reportId, HttpRequest request, AdministratorOptions admin, CrashArtifactStore store,CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"].ToString())) return Results.Unauthorized();
            if (!Guid.TryParse(reportId,out var id))return Results.BadRequest();
            await store.Gate.WaitAsync(ct);
            try
            {
            string directory=store.ReportDirectory(id);
            string[] files=Directory.Exists(directory) ? Directory.GetFiles(directory).Where(p=>Names.Contains(Path.GetFileName(p)) && (File.GetAttributes(p)&FileAttributes.ReparsePoint)==0).ToArray() : [];
            if(files.Length==0)return Results.NotFound(new {message="업로드된 덤프/로그가 없습니다."});
            // Spool ZIP to a delete-on-close temporary file, not a second 192MiB buffer per download.
            var zip=new FileStream(Path.Combine(Path.GetTempPath(),$"PP01-crash-{Guid.NewGuid():N}.zip"),FileMode.CreateNew,
                FileAccess.ReadWrite,FileShare.None,81920,FileOptions.Asynchronous|FileOptions.DeleteOnClose);
            try
            {
                using(var archive=new ZipArchive(zip,ZipArchiveMode.Create,true))
                    foreach(string path in files) archive.CreateEntryFromFile(path,Path.GetFileName(path),CompressionLevel.Fastest);
                zip.Position=0;
                return Results.Stream(zip,"application/zip",$"Crash-{id:D}.zip");
            }
            catch {await zip.DisposeAsync();throw;}
            }
            finally {store.Gate.Release();}
        }).RequireRateLimiting("admin");
    }
}
