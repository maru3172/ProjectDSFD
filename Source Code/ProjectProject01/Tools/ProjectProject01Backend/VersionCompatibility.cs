// File: Tools/ProjectProject01Backend/VersionCompatibility.cs
// One compatibility contract shared by authentication, lobby, and dedicated-server APIs.

internal sealed record VersionCompatibilityOptions(
    string ClientBuildVersion,
    string DedicatedServerBuildVersion,
    string ApiVersion,
    string GameDataVersion,
    string NetworkProtocolVersion,
    string UpdateMessage)
{
    public const string ClientBuildHeader = "X-ProjectProject01-Client-Build";
    public const string DedicatedServerBuildHeader = "X-ProjectProject01-Server-Build";
    public const string ApiVersionHeader = "X-ProjectProject01-Api-Version";
    public const string GameDataVersionHeader = "X-ProjectProject01-Game-Data-Version";
    public const string NetworkProtocolHeader = "X-ProjectProject01-Network-Protocol";

    public static VersionCompatibilityOptions FromConfiguration(IConfiguration configuration) => new(
        Normalize(configuration["Compatibility:ClientBuildVersion"], "1.0.0"),
        Normalize(configuration["Compatibility:DedicatedServerBuildVersion"], "1.0.0"),
        Normalize(configuration["Compatibility:ApiVersion"], "1"),
        Normalize(configuration["Compatibility:GameDataVersion"], "1"),
        Normalize(configuration["Compatibility:NetworkProtocolVersion"], "1"),
        Normalize(configuration["Compatibility:UpdateMessage"],
            "게임 버전이 서버와 맞지 않습니다. 최신 빌드로 업데이트하세요."));

    public object ToResponse() => new
    {
        clientBuildVersion = ClientBuildVersion,
        dedicatedServerBuildVersion = DedicatedServerBuildVersion,
        apiVersion = ApiVersion,
        gameDataVersion = GameDataVersion,
        networkProtocolVersion = NetworkProtocolVersion,
        updateMessage = UpdateMessage
    };

    public IReadOnlyList<string> GetMismatches(IHeaderDictionary headers, bool requireServerBuild)
    {
        var mismatches = new List<string>(4);
        Compare(headers, ClientBuildHeader, ClientBuildVersion, "ClientBuild", mismatches);
        if (requireServerBuild)
            Compare(headers, DedicatedServerBuildHeader, DedicatedServerBuildVersion, "DedicatedServerBuild", mismatches);
        Compare(headers, ApiVersionHeader, ApiVersion, "Api", mismatches);
        Compare(headers, GameDataVersionHeader, GameDataVersion, "GameData", mismatches);
        Compare(headers, NetworkProtocolHeader, NetworkProtocolVersion, "NetworkProtocol", mismatches);
        return mismatches;
    }

    private static void Compare(
        IHeaderDictionary headers, string header, string expected, string label, List<string> mismatches)
    {
        if (!headers.TryGetValue(header, out var supplied) ||
            !string.Equals(supplied.ToString().Trim(), expected, StringComparison.Ordinal))
        {
            mismatches.Add(label);
        }
    }

    private static string Normalize(string? value, string fallback) =>
        string.IsNullOrWhiteSpace(value) ? fallback : value.Trim();
}

internal static class VersionCompatibilityMiddleware
{
    public static IApplicationBuilder UseProjectProject01VersionCompatibility(this IApplicationBuilder app) =>
        app.Use(async (context, next) =>
        {
            if (!RequiresCompatibilityCheck(context.Request.Path))
            {
                await next();
                return;
            }

            var contract = context.RequestServices.GetRequiredService<VersionCompatibilityOptions>();
            var mismatches = contract.GetMismatches(
                context.Request.Headers, context.Request.Path.StartsWithSegments("/api/server"));
            if (mismatches.Count == 0)
            {
                await next();
                return;
            }

            var logger = context.RequestServices.GetRequiredService<ILoggerFactory>()
                .CreateLogger("ProjectProject01Compatibility");
            logger.LogWarning(
                "Rejected incompatible request {Method} {Path}. Mismatches={Mismatches}",
                context.Request.Method, context.Request.Path.Value, string.Join(',', mismatches));
            context.Response.StatusCode = StatusCodes.Status426UpgradeRequired;
            await context.Response.WriteAsJsonAsync(new
            {
                code = "VersionMismatch",
                message = $"{contract.UpdateMessage} (불일치: {string.Join(", ", mismatches)})",
                updateRequired = true,
                expected = contract.ToResponse()
            });
        });

    private static bool RequiresCompatibilityCheck(PathString path) =>
        path.StartsWithSegments("/api/auth") ||
        path.StartsWithSegments("/api/rooms") ||
        path.StartsWithSegments("/api/game-tickets") ||
        path.StartsWithSegments("/api/leaderboards") ||
        path.StartsWithSegments("/api/server");
}
