using MySqlConnector;

/// <summary>
/// This game's modules on top of the platform (Platform/BackendModules.cs), in the order the platform runs them.
/// Another game replaces this list with its own; the platform code does not change.
/// </summary>
internal static class GameModules
{
    public static IReadOnlyList<IBackendModule> All { get; } =
    [
        new BotMatchModule(), new CustomRulesModule(), new AffixModule(), new BotRatingModule(), new CaseRecordsModule(), new MainQuestModule(),
    ];
}

/// <summary>Bot matches: bot rooms and rosters, stand-in bots, observers, desertion records, cross-match bot experience.</summary>
internal sealed class BotMatchModule : IBackendModule
{
    public Task EnsureSchemaAsync(MySqlConnection c, CancellationToken ct) => LobbyEndpoints.EnsureMatchBotSchemaAsync(c, ct);
    public void MapRoutes(WebApplication app, Func<bool> canLaunchOrConnect) => app.MapBotMatches(canLaunchOrConnect);
    public async Task OnMatchStartedAsync(MySqlConnection c, MySqlTransaction tx, string match, CancellationToken ct)
    {
        var rooms = await CommunityEndpoints.Rows(c, tx, "SELECT id FROM game_rooms WHERE match_id=@m AND status='Started'", ct, ("m", match));
        if (rooms.Count == 1) await LobbyEndpoints.SnapshotMatchBotsAsync(c, tx, Convert.ToString(rooms[0]["id"])!, match, ct);
    }
    public async Task AddTicketFieldsAsync(MySqlConnection c, MySqlTransaction tx, string match, IDictionary<string, object?> fields, CancellationToken ct)
    {
        var roster = await LobbyEndpoints.LoadMatchBotsAsync(c, tx, match, ct);
        // Each bot arrives with its role/level pool from earlier matches (optional field).
        var pools = await BotExperience.LoadAsync(c, tx, roster, ct);
        fields["bots"] = roster.Select(b => new { b.BotId, b.DisplayName, b.Difficulty, b.AssignedRole,
            experience = pools.TryGetValue($"{b.AssignedRole}:{b.Difficulty}", out var pool) ? pool : null }).ToList();
    }
}

/// <summary>Custom room rules (death count, mannequins, time limit); custom matches are unofficial.</summary>
internal sealed class CustomRulesModule : IBackendModule
{
    public Task EnsureSchemaAsync(MySqlConnection c, CancellationToken ct) => CustomRoomRules.EnsureAsync(c, ct);
    public void MapRoutes(WebApplication app, Func<bool> canLaunchOrConnect) => app.MapCustomRoomRules(canLaunchOrConnect);
    public Task OnMatchStartedAsync(MySqlConnection c, MySqlTransaction tx, string match, CancellationToken ct) => CustomRoomRules.CaptureAsync(c, tx, match, ct);
    public async Task AddTicketFieldsAsync(MySqlConnection c, MySqlTransaction tx, string match, IDictionary<string, object?> fields, CancellationToken ct) =>
        fields["customRules"] = await CustomRoomRules.MatchAsync(c, tx, match, ct);
}

/// <summary>Bot match affixes (Master+, Master++, weekly, custom).</summary>
internal sealed class AffixModule : IBackendModule
{
    public Task EnsureSchemaAsync(MySqlConnection c, CancellationToken ct) => MatchAffixes.EnsureAsync(c, ct);
    public void MapRoutes(WebApplication app, Func<bool> canLaunchOrConnect) => MatchAffixes.Map(app);
    public Task OnMatchStartedAsync(MySqlConnection c, MySqlTransaction tx, string match, CancellationToken ct) => MatchAffixes.CaptureAsync(c, tx, match, ct);
    public async Task AddTicketFieldsAsync(MySqlConnection c, MySqlTransaction tx, string match, IDictionary<string, object?> fields, CancellationToken ct)
    {
        var set = await MatchAffixes.MatchAsync(c, tx, match, ct);
        fields["affixes"] = new { mode = set.Mode, ids = set.List, weekName = set.WeekName };
    }
}

/// <summary>Players' skill against bots (needs the custom and affix captures before it).</summary>
internal sealed class BotRatingModule : IBackendModule
{
    public Task OnMatchStartedAsync(MySqlConnection c, MySqlTransaction tx, string match, CancellationToken ct) => BotSkillRatings.CaptureAsync(c, tx, match, ct);
}

/// <summary>Case records: progression, collection, draws, missions, investigation records, rewards.</summary>
internal sealed class CaseRecordsModule : IBackendModule
{
    public async Task EnsureSchemaAsync(MySqlConnection c, CancellationToken ct)
    {
        await CaseRecords.EnsureAsync(c, ct);await CaseCollection.EnsureAsync(c, ct);await CaseDraw.EnsureAsync(c, ct);
        await CaseMissionSlots.EnsureAsync(c, ct);await CaseInvestigation.EnsureAsync(c, ct);await CaseMissionBudget.EnsureAsync(c, ct);
    }
    public void MapRoutes(WebApplication app, Func<bool> canLaunchOrConnect) => app.MapCaseRecords();
    public async Task OnMatchStartedAsync(MySqlConnection c, MySqlTransaction tx, string match, CancellationToken ct)
    {
        await CaseRecords.CaptureModeAsync(c, tx, match, ct);
        await CaseInvestigation.CaptureServerAsync(c, tx, match, ct);
    }
}

/// <summary>The "탐사" main quest line (routes are mapped with the case records).</summary>
internal sealed class MainQuestModule : IBackendModule
{
    public Task EnsureSchemaAsync(MySqlConnection c, CancellationToken ct) => MainQuest.EnsureAsync(c, ct);
}

/// <summary>The game registers itself when the assembly loads, so the server and every check process see the same modules.</summary>
internal static class GameModuleRegistration
{
    [System.Runtime.CompilerServices.ModuleInitializer]
    internal static void Register() => BackendModules.Register(GameModules.All);
}
