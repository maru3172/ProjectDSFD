// File: Tools/ProjectProject01Backend/ClientUpdateEndpoints.cs
// Public read-only delivery of a client update manifest and its packaged files.

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

            if (!requestedPath.StartsWith(rootWithSeparator, StringComparison.OrdinalIgnoreCase) ||
                !File.Exists(requestedPath) ||
                string.Equals(Path.GetFileName(requestedPath), ManifestFileName, StringComparison.OrdinalIgnoreCase))
            {
                return Results.NotFound();
            }

            context.Response.Headers.CacheControl = "public, max-age=3600";
            return Results.File(requestedPath, "application/octet-stream", enableRangeProcessing: true);
        });
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
            return searchRoot;
        }

        string? manifest = Directory.EnumerateFiles(searchRoot, ManifestFileName, SearchOption.AllDirectories)
            .OrderByDescending(File.GetLastWriteTimeUtc)
            .FirstOrDefault();
        return manifest is null ? null : Path.GetDirectoryName(manifest);
    }
}
