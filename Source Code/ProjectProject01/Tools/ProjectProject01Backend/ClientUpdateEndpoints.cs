// File: Tools/ProjectProject01Backend/ClientUpdateEndpoints.cs
// Public read-only delivery of a client update manifest and its packaged files.

using System.Text.Json;

internal static class ClientUpdateEndpoints
{
    private const string ManifestFileName = "client-update-manifest.json";

    public static void MapProjectProject01ClientUpdates(this WebApplication app)
    {
        app.MapGet("/api/client-updates/manifest", (HttpContext context, IWebHostEnvironment environment) =>
        {
            string? publishedRoot = ResolvePublishedRoot(environment.ContentRootPath, app.Configuration);
            if (publishedRoot is null)
            {
                return Results.Json(new
                {
                    code = "ClientUpdateNotPublished",
                    message = "게시된 클라이언트 업데이트가 없습니다. 서버 도우미에서 클라이언트 업데이트 게시를 실행하세요."
                }, statusCode: StatusCodes.Status404NotFound);
            }

            context.Response.Headers.CacheControl = "no-store, no-cache, must-revalidate";
            return Results.File(
                Path.Combine(publishedRoot, ManifestFileName),
                "application/json; charset=utf-8",
                enableRangeProcessing: false);
        });

        app.MapGet("/api/client-updates/files/{**relativePath}",
            (string? relativePath, HttpContext context, IWebHostEnvironment environment) =>
        {
            string? publishedRoot = ResolvePublishedRoot(environment.ContentRootPath, app.Configuration);
            if (publishedRoot is null || string.IsNullOrWhiteSpace(relativePath))
            {
                return Results.NotFound();
            }

            string normalizedRelativePath = Uri.UnescapeDataString(relativePath)
                .Replace('/', Path.DirectorySeparatorChar)
                .TrimStart(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string rootWithSeparator = Path.TrimEndingDirectorySeparator(publishedRoot) + Path.DirectorySeparatorChar;
            string requestedPath;
            try
            {
                requestedPath = Path.GetFullPath(Path.Combine(publishedRoot, normalizedRelativePath));
            }
            catch
            {
                return Results.BadRequest(new { message = "올바르지 않은 업데이트 파일 경로입니다." });
            }

            if (!IsSafePublishedPath(publishedRoot,requestedPath) ||
                !File.Exists(requestedPath) ||
                string.Equals(Path.GetFileName(requestedPath), ManifestFileName, StringComparison.OrdinalIgnoreCase))
            {
                return Results.NotFound();
            }

            if(!IsPublishedFile(publishedRoot,normalizedRelativePath,context.Request.Query["release"].ToString()))return Results.NotFound();

            context.Response.Headers.CacheControl = "public, max-age=3600";
            return Results.File(requestedPath, "application/octet-stream", enableRangeProcessing: true);
        });
    }

    internal static bool IsSafePublishedPath(string root,string path)
    {
        root=Path.GetFullPath(root);path=Path.GetFullPath(path);
        if(!path.StartsWith(Path.TrimEndingDirectorySeparator(root)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))return false;
        for(string? item=path;item is not null;item=Path.GetDirectoryName(item))
            if((File.Exists(item)||Directory.Exists(item))&&(File.GetAttributes(item)&FileAttributes.ReparsePoint)!=0)return false;
        return true;
    }
    internal static bool IsPublishedFile(string root,string relative,string release)
    {
        try
        {
            string manifest=Path.Combine(root,ManifestFileName);
            if(!IsSafePublishedPath(root,manifest)||new FileInfo(manifest).Length>4*1024*1024)return false;
            using var json=JsonDocument.Parse(File.ReadAllText(manifest));
            var document=json.RootElement;
            if(!document.TryGetProperty("releaseId",out var id)&&!document.TryGetProperty("ReleaseId",out id))return false;
            if(!string.IsNullOrEmpty(release)&&id.GetString()!=release)return false;
            if(!document.TryGetProperty("files",out var files)&&!document.TryGetProperty("Files",out files))return false;
            if(files.GetArrayLength()>20000)return false;
            string normalized=relative.Replace('\\','/');
            foreach(var file in files.EnumerateArray())
                if((file.TryGetProperty("path",out var path)||file.TryGetProperty("Path",out path))&&string.Equals(path.GetString()?.Replace('\\','/'),normalized,StringComparison.OrdinalIgnoreCase))return true;
            return false;
        }
        catch(Exception ex)when(ex is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException){return false;}
    }

    private static string? ResolvePublishedRoot(string contentRootPath, IConfiguration configuration)
    {
        string configuredRoot = configuration["ClientUpdates:RootPath"]?.Trim() ?? string.Empty;
        string searchRoot = configuredRoot.Length > 0
            ? (Path.IsPathRooted(configuredRoot)
                ? configuredRoot
                : Path.GetFullPath(Path.Combine(contentRootPath, configuredRoot)))
            : Path.GetFullPath(Path.Combine(contentRootPath, "..", "..", "Builds", "Client"));

        if (!Directory.Exists(searchRoot))
        {
            return null;
        }

        string directManifest = Path.Combine(searchRoot, ManifestFileName);
        if (File.Exists(directManifest))
        {
            return IsSafePublishedPath(searchRoot,directManifest)?searchRoot:null;
        }

        string? manifest = Directory.EnumerateFiles(searchRoot, ManifestFileName,new EnumerationOptions{RecurseSubdirectories=true,AttributesToSkip=FileAttributes.ReparsePoint,MaxRecursionDepth=16})
            .Where(path=>IsSafePublishedPath(searchRoot,path))
            .OrderByDescending(File.GetLastWriteTimeUtc)
            .FirstOrDefault();
        return manifest is null ? null : Path.GetDirectoryName(manifest);
    }
}
