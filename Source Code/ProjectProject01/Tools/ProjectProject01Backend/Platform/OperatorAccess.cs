using System.Security.Cryptography;
using System.Text;

internal static class OperatorAccess
{
    private const string Header = "X-ProjectProject01-Admin-Key";
    private sealed record Operator(string Name, string Role, byte[] KeyHash);

    public static void UseOperatorAccess(this WebApplication app)
    {
        var admin = app.Services.GetRequiredService<AdministratorOptions>();
        var operators = Load(app.Configuration, admin);
        app.Use(async (context, next) =>
        {
            if (!context.Request.Path.StartsWithSegments("/api/admin") ||
                !context.Request.Headers.TryGetValue(Header, out var provided) || provided.Count != 1 ||
                provided[0] is not { Length: >= 32 and <= 256 } key)
            {
                await next();
                return;
            }
            var route = context.GetEndpoint() as RouteEndpoint;
            string pattern = route?.RoutePattern.RawText ?? context.Request.Path.Value ?? "";
            bool owner = admin.IsAuthorized(key);
            var credential = owner ? null : Find(operators, key);
            if (!owner && credential is null)
            {
                context.Response.StatusCode = StatusCodes.Status401Unauthorized;
                return;
            }
            if (credential is not null && (route is null || !admin.IsConfigured || !Permits(credential.Role, context.Request.Method, pattern)))
            {
                context.Response.StatusCode = StatusCodes.Status403Forbidden;
                await Record(context, credential.Name, pattern, 403);
                return;
            }
            context.Items[typeof(OperatorAccess)] = credential?.Name ?? "owner";
            // Existing handlers keep their established credential check. Scoped keys reach them only after route authorization.
            if (!owner) context.Request.Headers[Header] = admin.ApiKey;
            int status = 500;
            try
            {
                await next();
                status = context.Response.StatusCode;
            }
            finally
            {
                context.Request.Headers[Header] = provided;
                if (!HttpMethods.IsGet(context.Request.Method) && !HttpMethods.IsHead(context.Request.Method))
                    await Record(context, credential?.Name ?? "owner", pattern, status);
            }
        });
    }

    public static string Actor(HttpContext context) =>
        context.Items.TryGetValue(typeof(OperatorAccess), out object? name) ? (string)name! : "owner";

    internal static bool Permits(string role, string method, string route)
    {
        static bool Under(string route, string area) =>
            route.Equals(area, StringComparison.OrdinalIgnoreCase) ||
            route.StartsWith(area + "/", StringComparison.OrdinalIgnoreCase);
        return role switch
        {
            "Observer" => HttpMethods.IsGet(method) && (route is "/api/admin/service" or "/api/admin/operations/health" or
                "/api/admin/operations/analytics" or "/api/admin/diagnostics"),
            "Support" => route is "/api/admin/community" || Under(route, "/api/admin/community/inquiries") ||
                Under(route, "/api/admin/community/reports") || Under(route, "/api/admin/crash-reports"),
            "Content" => Under(route, "/api/admin/main-quest") || Under(route, "/api/admin/surveys") ||
                Under(route, "/api/admin/operations/rollout"),
            "Service" => Under(route, "/api/admin/service") || Under(route, "/api/admin/game-server-demand") ||
                Under(route, "/api/admin/room-capacity") || route is "/api/admin/operations/health" or "/api/admin/operations/analytics",
            _ => false
        };
    }

    private static Operator[] Load(IConfiguration configuration, AdministratorOptions admin)
    {
        var result = new List<Operator>();
        foreach (var entry in configuration.GetSection("Administrator:Operators").GetChildren())
        {
            string name = entry.Key;
            string? key = entry["ApiKey"];
            string? role = entry["Role"];
            if (name.Length is < 1 or > 48 || name.Equals("owner", StringComparison.OrdinalIgnoreCase) ||
                name.Any(c => !char.IsAsciiLetterOrDigit(c) && c is not '_' and not '-') ||
                key is not { Length: >= 32 and <= 256 } || role is not ("Observer" or "Support" or "Content" or "Service") ||
                admin.IsAuthorized(key) || Find(result, key) is not null)
                throw new InvalidOperationException($"운영자 설정이 잘못되었습니다. 항목: {name}");
            result.Add(new(name, role, SHA256.HashData(Encoding.UTF8.GetBytes(key))));
        }
        return result.ToArray();
    }

    private static Operator? Find(IEnumerable<Operator> operators, string key)
    {
        byte[] digest = SHA256.HashData(Encoding.UTF8.GetBytes(key));
        return operators.FirstOrDefault(candidate => CryptographicOperations.FixedTimeEquals(candidate.KeyHash, digest));
    }

    private static async Task Record(HttpContext context, string actor, string route, int status)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
        await SecurityAudit.WriteAsync(context.RequestServices.GetRequiredService<AuthDatabase>(), context.Request,
            "operator.change", status is >= 200 and < 300 ? "success" : "denied-or-failed", null,
            $"{actor} | {context.Request.Method} {route} | HTTP {status}", timeout.Token);
    }
}
