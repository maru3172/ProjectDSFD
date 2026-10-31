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
    // Parsed manifest cache. Every block range request used to re-read and re-parse the whole
    // manifest and scan up to 20,000 entries. The cache is keyed by full path, write time and
    // length, so a republished/replaced manifest is parsed again. Parse failures are not cached.
    private sealed record ParsedManifest(string FullPath, DateTime WriteUtc, long Length,
        bool HasReleaseId, bool ReleaseIdIsString, string? ReleaseId, bool FilesValid, HashSet<string> Files);
    private static ParsedManifest? parsedManifestCache;

    internal static bool IsPublishedFile(string root,string relative,string release)
    {
        try
        {
            string manifest=Path.Combine(root,ManifestFileName);
            if(!IsSafePublishedPath(root,manifest))return false;
            var info=new FileInfo(manifest);
            if(info.Length>4*1024*1024)return false;
            ParsedManifest parsed=GetParsedManifest(info);
            if(!parsed.HasReleaseId)return false;
            // Same outcome as the previous JsonElement.GetString() throwing for a non-string id.
            if(!string.IsNullOrEmpty(release)&&(!parsed.ReleaseIdIsString||parsed.ReleaseId!=release))return false;
            if(!parsed.FilesValid)return false;
            return parsed.Files.Contains(relative.Replace('\\','/'));
        }
        catch(Exception ex)when(ex is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException){return false;}
    }

    private static ParsedManifest GetParsedManifest(FileInfo info)
    {
        var cached=Volatile.Read(ref parsedManifestCache);
        DateTime writeUtc=info.LastWriteTimeUtc;long length=info.Length;
        if(cached is not null&&cached.Length==length&&cached.WriteUtc==writeUtc&&
            string.Equals(cached.FullPath,info.FullName,StringComparison.OrdinalIgnoreCase))
            return cached;
        using var json=JsonDocument.Parse(File.ReadAllText(info.FullName));
        var document=json.RootElement;
        bool hasRelease=document.TryGetProperty("releaseId",out var id)||document.TryGetProperty("ReleaseId",out id);
        bool releaseIsString=hasRelease&&id.ValueKind is JsonValueKind.String or JsonValueKind.Null;
        string? releaseId=releaseIsString?id.GetString():null;
        var set=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        bool valid=(document.TryGetProperty("files",out var files)||document.TryGetProperty("Files",out files))&&
            files.ValueKind==JsonValueKind.Array&&files.GetArrayLength()<=20000;
        if(valid)
        {
            foreach(var file in files.EnumerateArray())
            {
                // The previous linear scan threw at the first malformed entry, so only entries
                // before it could ever match. Keep exactly that behavior.
                if(file.ValueKind!=JsonValueKind.Object)break;
                if(!file.TryGetProperty("path",out var path)&&!file.TryGetProperty("Path",out path))continue;
                if(path.ValueKind is not (JsonValueKind.String or JsonValueKind.Null))break;
                string? value=path.GetString();
                if(value is not null)set.Add(value.Replace('\\','/'));
            }
        }
        // Write time/length are captured before parsing; a concurrent replacement is detected on the next request.
        var parsed=new ParsedManifest(info.FullName,writeUtc,length,hasRelease,releaseIsString,releaseId,valid,set);
        Volatile.Write(ref parsedManifestCache,parsed);
        return parsed;
    }

    // A packaged client publishes its manifest below Builds/Client/<Platform>, so the previous
    // lookup walked the whole packaged tree (thousands of files) on every manifest and file/block
    // request. Reuse a found root for a short time while its manifest still exists at the same
    // safe path. Publishing deletes old manifests first, which invalidates the cache immediately;
    // otherwise a newer manifest elsewhere is picked up within RootCacheLifetimeMilliseconds.
    private sealed record ResolvedPublishedRoot(string SearchRoot, string Root, long ExpiresAtMilliseconds);
    private static ResolvedPublishedRoot? resolvedRootCache;
    private const long RootCacheLifetimeMilliseconds = 10_000;

    private static string? ResolvePublishedRoot(string contentRootPath, IConfiguration configuration)
    {
        string configuredRoot = configuration["ClientUpdates:RootPath"]?.Trim() ?? string.Empty;
        string searchRoot = configuredRoot.Length > 0
            ? (Path.IsPathRooted(configuredRoot)
                ? configuredRoot
                : Path.GetFullPath(Path.Combine(contentRootPath, configuredRoot)))
            : Path.GetFullPath(Path.Combine(contentRootPath, "..", "..", "Builds", "Client"));

        long now = Environment.TickCount64;
        var cached = Volatile.Read(ref resolvedRootCache);
        if (cached is not null && now < cached.ExpiresAtMilliseconds &&
            string.Equals(cached.SearchRoot, searchRoot, StringComparison.OrdinalIgnoreCase))
        {
            string cachedManifest = Path.Combine(cached.Root, ManifestFileName);
            if (File.Exists(cachedManifest) && IsSafePublishedPath(searchRoot, cachedManifest))
            {
                return cached.Root;
            }
        }

        string? root = ResolvePublishedRootUncached(searchRoot);
        Volatile.Write(ref resolvedRootCache,
            root is null ? null : new ResolvedPublishedRoot(searchRoot, root, now + RootCacheLifetimeMilliseconds));
        return root;
    }

    private static string? ResolvePublishedRootUncached(string searchRoot)
    {
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
