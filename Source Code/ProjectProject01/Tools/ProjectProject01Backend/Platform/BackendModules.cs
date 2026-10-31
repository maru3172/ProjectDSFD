using MySqlConnector;

/// <summary>
/// A feature set plugged into the platform (accounts, rooms, matchmaking, tickets, servers, updates, notices, reports,
/// crashes, diagnostics, voice). The platform calls every registered module at fixed points of its own flow and never
/// names a game feature; a game adds its rules by registering modules in the host (Program.cs).
/// Every member is optional.
/// </summary>
internal interface IBackendModule
{
    /// <summary>Creates or upgrades the module's tables. Runs at startup after the platform's own tables.</summary>
    Task EnsureSchemaAsync(MySqlConnection connection, CancellationToken ct) => Task.CompletedTask;
    /// <summary>Adds the module's HTTP routes. <paramref name="canLaunchOrConnect"/> tells whether a game server can take a match now.</summary>
    void MapRoutes(WebApplication app, Func<bool> canLaunchOrConnect) { }
    /// <summary>Runs inside the transaction that records a started match (roles are already assigned).</summary>
    Task OnMatchStartedAsync(MySqlConnection connection, MySqlTransaction transaction, string matchId, CancellationToken ct) => Task.CompletedTask;
    /// <summary>Adds fields to the game server's ticket check response (keys are the JSON names, camelCase).</summary>
    Task AddTicketFieldsAsync(MySqlConnection connection, MySqlTransaction transaction, string matchId, IDictionary<string, object?> fields, CancellationToken ct) => Task.CompletedTask;
}

internal static class BackendModules
{
    private static readonly List<IBackendModule> modules = [];
    public static IReadOnlyList<IBackendModule> All => modules;

    /// <summary>Called once by the host before the app starts. Order matters: schemas, routes and hooks run in this order.</summary>
    public static void Register(IEnumerable<IBackendModule> list) { foreach (var module in list) if (!modules.Contains(module)) modules.Add(module); }

    public static async Task EnsureSchemasAsync(MySqlConnection connection, CancellationToken ct) { foreach (var m in modules) await m.EnsureSchemaAsync(connection, ct); }
    public static void MapRoutes(WebApplication app, Func<bool> canLaunchOrConnect) { foreach (var m in modules) m.MapRoutes(app, canLaunchOrConnect); }
    public static async Task OnMatchStartedAsync(MySqlConnection connection, MySqlTransaction transaction, string matchId, CancellationToken ct)
    { foreach (var m in modules) await m.OnMatchStartedAsync(connection, transaction, matchId, ct); }
    public static async Task AddTicketFieldsAsync(MySqlConnection connection, MySqlTransaction transaction, string matchId, IDictionary<string, object?> fields, CancellationToken ct)
    { foreach (var m in modules) await m.AddTicketFieldsAsync(connection, transaction, matchId, fields, ct); }
}
