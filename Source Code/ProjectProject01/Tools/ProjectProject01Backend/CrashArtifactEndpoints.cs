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

    internal static string ScrubText(string text)
    {
        text = Regex.Replace(text, @"(?is)(<CommandLine>).*?(?=</CommandLine>)", "$1[REDACTED]");
        text = Regex.Replace(text, @"(?im)(Command line:)[^\r\n]*", "$1[REDACTED]");
        text = Regex.Replace(text, @"(?i)(Bearer\s+)[A-Za-z0-9._~+/=-]+", "$1[REDACTED]");
        text = Regex.Replace(text,
            @"(?i)((?:access[_-]?token|refresh[_-]?token|client[_-]?secret|password|authorization|encryption[_-]?key|api[_-]?key|game[_-]?server[_-]?secret|admin[_-]?key|EncryptionToken)[""']?\s*[:=]\s*)(?:""[^""]*""|'[^']*'|[^\s<>;,]+)",
            "$1[REDACTED]");
        text = Regex.Replace(text,@"(?is)(<(access[_-]?token|refresh[_-]?token|client[_-]?secret|password|encryption[_-]?key)>).*?(?=</\2>)","$1[REDACTED]");
        text = Regex.Replace(text, @"(?i)C:\\Users\\[^\\\s<>""']+", @"C:\Users\[USER]");
        return text;
    }

    public static void MapProjectProject01CrashArtifacts(this WebApplication app)
    {
        string root = Path.Combine(app.Environment.ContentRootPath, "Saved", "CrashReportArtifacts");
        app.MapPost("/api/crash-reports/{reportId}/artifacts/{name}", async (
            string reportId, string name, HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            if (!Guid.TryParse(reportId, out var id) || !Names.Contains(name)) return Results.BadRequest();
            if (request.ContentLength is > MaximumFileBytes) return Results.StatusCode(413);
            var limit = request.HttpContext.Features.Get<IHttpMaxRequestBodySizeFeature>();
            if (limit is { IsReadOnly: false }) limit.MaxRequestBodySize = MaximumFileBytes;
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var exists = new MySqlCommand("SELECT COUNT(*) FROM crash_reports WHERE report_id=@id;", connection);
            exists.Parameters.AddWithValue("@id", id.ToString("D"));
            if (Convert.ToInt32(await exists.ExecuteScalarAsync(ct)) == 0) return Results.NotFound();
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
            string directory=Path.Combine(root,id.ToString("D"));
            Directory.CreateDirectory(directory);
            string temporary=Path.Combine(directory,$"{Guid.NewGuid():N}.upload");
            try
            {
                await File.WriteAllBytesAsync(temporary,bytes,ct);
                File.Move(temporary,Path.Combine(directory,name),true);
            }
            finally { if(File.Exists(temporary))File.Delete(temporary); }
            return Results.Ok(new { name, bytes=bytes.Length });
        }).RequireRateLimiting("crash-report");

        app.MapGet("/api/admin/crash-reports/{reportId}/artifacts", (
            string reportId, HttpRequest request, AdministratorOptions admin) =>
        {
            if (!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"].ToString())) return Results.Unauthorized();
            if (!Guid.TryParse(reportId,out var id))return Results.BadRequest();
            string directory=Path.Combine(root,id.ToString("D"));
            string[] files=Directory.Exists(directory) ? Directory.GetFiles(directory).Where(p=>Names.Contains(Path.GetFileName(p))).ToArray() : [];
            if(files.Length==0)return Results.NotFound(new {message="업로드된 덤프/로그가 없습니다."});
            using var memory=new MemoryStream();
            using(var archive=new ZipArchive(memory,ZipArchiveMode.Create,true))
                foreach(string path in files) archive.CreateEntryFromFile(path,Path.GetFileName(path),CompressionLevel.Fastest);
            return Results.File(memory.ToArray(),"application/zip",$"Crash-{id:D}.zip");
        }).RequireRateLimiting("admin");
    }
}
