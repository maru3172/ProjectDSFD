using MySqlConnector;

internal static class PlayAnalytics
{
    private sealed record TutorialEvent(string RunId, string Role, int Step, bool Completed);
    private static readonly HashSet<string> MatchMaps =
        ["/Game/MyProject/Level/MultiplayTest", "/Game/MyProject/Level/MultiplayTest1", "/Game/MyProject/Level/MultiplayTest2"];

    public static void Map(WebApplication app)
    {
        app.MapPost("/api/analytics/tutorial", async (TutorialEvent body, HttpRequest request, AuthDatabase db,
            VersionCompatibilityOptions version, CancellationToken ct) =>
        {
            if (!Guid.TryParse(body.RunId, out var run) || body.Role is not ("Survivor" or "Mannequin") ||
                body.Step is < 0 or > 64) return Results.BadRequest();
            var user = await LobbyEndpoints.AuthenticateAsync(request, db, ct);
            if (user is null) return Results.Unauthorized();
            await using var connection = await db.OpenConnectionAsync(ct);
            await Record(connection, null, "Tutorial", run.ToString("D"), user.Id,
                body.Completed ? "Done" : body.Step.ToString(System.Globalization.CultureInfo.InvariantCulture),
                body.Role, OperationsHealth.Build(version), ct);
            return Results.NoContent();
        }).RequireRateLimiting("social");

        app.MapGet("/api/admin/operations/analytics", async (int? days, HttpRequest request, AuthDatabase db,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            int period = Math.Clamp(days ?? 30, 1, 90);
            var since = DateTime.UtcNow.AddDays(-period);
            await using var c = await db.OpenConnectionAsync(ct);
            var tutorial = await CommunityEndpoints.Rows(c, null, """
                SELECT map AS role,build,step,COUNT(*) AS visits,
                  SUM(event_utc<UTC_TIMESTAMP(6)-INTERVAL 1 HOUR AND step<>'Done' AND NOT EXISTS(
                    SELECT 1 FROM play_analytics_events next
                    WHERE next.kind='Tutorial' AND next.context_id=e.context_id AND next.user_id=e.user_id
                      AND (next.step='Done' OR CAST(next.step AS UNSIGNED)>CAST(e.step AS UNSIGNED))
                  )) AS lastObserved
                FROM play_analytics_events e
                WHERE kind='Tutorial' AND event_utc>=@s GROUP BY map,build,step ORDER BY map,build,step
                """, ct, ("s", since));
            var floors = await CommunityEndpoints.Rows(c, null, """
                SELECT kind,map,build,COUNT(*) AS events,COUNT(DISTINCT context_id) AS matches
                FROM play_analytics_events WHERE kind IN ('Floor','Capture','Escape') AND event_utc>=@s
                GROUP BY kind,map,build ORDER BY map,build,kind
                """, ct, ("s", since));
            var survivors = await CommunityEndpoints.Rows(c, null, """
                WITH cohort AS (
                  SELECT p.match_id,COUNT(*) AS humans
                  FROM authoritative_match_players p JOIN authoritative_matches m ON m.match_id=p.match_id
                  WHERE p.role='Survivor' AND m.created_at_utc>=@s
                  GROUP BY p.match_id
                ), versions AS (
                  SELECT context_id,MAX(build) AS build FROM play_analytics_events
                  WHERE kind IN ('Floor','Capture','Escape') AND event_utc>=@s GROUP BY context_id
                )
                SELECT cohort.humans,COALESCE(versions.build,'unknown') AS build,
                  COUNT(DISTINCT r.match_id) AS matches,COUNT(*) AS survivorResults,SUM(r.success) AS escapes,
                  ROUND(100*AVG(r.success),2) AS escapeRate
                FROM verified_match_results r JOIN cohort ON cohort.match_id=r.match_id
                LEFT JOIN versions ON versions.context_id=r.match_id
                WHERE r.role='Survivor' GROUP BY cohort.humans,COALESCE(versions.build,'unknown')
                ORDER BY cohort.humans,build
                """, ct, ("s", since));
            return Results.Ok(new { days = period, tutorial, floors, survivors });
        }).RequireRateLimiting("admin");
    }

    internal static Task RecordFloor(MySqlConnection c, MySqlTransaction tx, string match, ulong user, string map,
        VersionCompatibilityOptions version, CancellationToken ct) =>
        Record(c, tx, "Floor", match, user, map[(map.LastIndexOf('/') + 1)..], map, OperationsHealth.Build(version, true), ct);

    internal static Task RecordOutcome(MySqlConnection c, MySqlTransaction tx, string match, ulong user, string kind,
        int count, string? map, VersionCompatibilityOptions version, CancellationToken ct) =>
        map is null || !MatchMaps.Contains(map) ? Task.CompletedTask :
            Record(c, tx, kind, match, user, count.ToString(System.Globalization.CultureInfo.InvariantCulture),
                map, OperationsHealth.Build(version, true), ct);

    private static Task Record(MySqlConnection c, MySqlTransaction? tx, string kind, string context, ulong user,
        string step, string map, string build, CancellationToken ct) =>
        CommunityEndpoints.Exec(c, tx, """
            INSERT IGNORE INTO play_analytics_events(kind,context_id,user_id,step,map,build) VALUES(@k,@c,@u,@p,@m,@b)
            """, ct, ("k", kind), ("c", context), ("u", user), ("p", step), ("m", map), ("b", build.Length > 100 ? build[..100] : build));
}
